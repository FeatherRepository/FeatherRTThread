/* hal_uart_dma_rtthread.c - btstack hal_uart_dma 契约的 mtb_hal 直驱实现
 *
 * 传输对象: SCB4 = CYW55512 BT HCI UART
 *   - 配置符号 CYBSP_BT_UART_config / CYBSP_BT_UART_hal_config / CYBSP_BT_UART_clock_ref
 *     由 Configurator 生成 (projects/libs/TARGET_APP_KIT_PSE84_EVAL_EPC2/config/GeneratedSource)
 *   - 引脚: P10.0-RX / P10.1-TX / P10.2-CTS / P10.3-RTS (hal_config 内含, setup 时自动施加)
 *
 * 设计说明:
 *   - 不走 rt_serial/BSP_USING_UART4: 启动期注册 SCB4 会导致 M55 启动挂死 (实测),
 *     改为 bt_on 时经 mtb_hal_uart_setup 现场初始化
 *   - RX: 中断驱动异步读 mtb_hal_uart_read_async, 参考 AIROC hci_uart 平台层
 *     (vendor/infineon/hci_uart, Apache-2.0) 的成熟模型——大帧 (Read Local Name
 *     252 字节) 由 HAL 内部管理 FIFO 阈值与流控, 不再自写轮询/环形缓冲。
 *   - TX: mtb_hal_uart_write 循环写满
 */
#include <rtthread.h>
#include <string.h>
#include <stdint.h>

#include "cy_result.h"
#include "mtb_hal_uart.h"
#include "cy_scb_uart.h"
#include "cycfg_peripherals.h"

#include <hal_uart_dma.h>

#ifndef FEATHERTALK_BT_UART_BAUD
#define FEATHERTALK_BT_UART_BAUD    115200
#endif

static mtb_hal_uart_t            s_obj;
static mtb_async_transfer_context_t s_async_ctx;
static cy_stc_scb_uart_context_t s_uart_context;
static void (*s_block_received)(void);
static void (*s_block_sent)(void);
static volatile int        s_init_err;                    /* 0=ok 1=setup fail 2=baud fail */
static volatile rt_bool_t  s_initialized;
static volatile rt_bool_t  s_rx_irq_on;
static uint32_t            s_cur_baud;

/* Owner-thread only: no ISR may reference H4 buffers after return. Also used
 * after a failed HCD attempt, before another raw download starts. */
void bt_uart_shutdown(void)
{
    NVIC_DisableIRQ(CYBSP_BT_UART_IRQ);
    if (s_initialized) {
        mtb_hal_uart_enable_event(&s_obj, MTB_HAL_UART_IRQ_RX_DONE, false);
        (void)mtb_hal_uart_read_abort(&s_obj);
    }
    s_rx_irq_on = RT_FALSE;
    s_block_received = RT_NULL;
    s_block_sent = RT_NULL;
    Cy_SCB_UART_Disable(CYBSP_BT_UART_HW, &s_uart_context);
    Cy_SCB_UART_DeInit(CYBSP_BT_UART_HW);
    NVIC_ClearPendingIRQ(CYBSP_BT_UART_IRQ);
    s_initialized = RT_FALSE;
    s_cur_baud = 0;
}

/* h4 TX 观测 (GDB 可读): 调用数 / CTS 超时数 */
volatile uint32_t g_h4_tx_calls;
volatile uint32_t g_h4_tx_timeout;

/* ---- 中断回调: RX_DONE 表示一次 read_async 完成 (对齐 AIROC cybt_uart_rx_done_irq) ---- */
static void bt_uart_event_handler(void *arg, mtb_hal_uart_event_t event)
{
    (void)arg;
    if ((event & MTB_HAL_UART_IRQ_RX_DONE) != 0)
    {
        /* btstack 的 block_received 设计为可在 ISR 上下文调用
         * (btstack_uart_block_received -> btstack_run_loop_poll_data_sources_from_irq) */
        if (s_block_received)
        {
            s_block_received();
        }
    }
}

static void bt_uart_irq_handler(void)
{
    mtb_hal_uart_process_interrupt(&s_obj);
}

/* 使能 RX 中断 (h4 阶段开始前调用; 下载阶段的裸读在此之前已完成) */
void hal_uart_dma_rtthread_start_rx_thread(void)
{
    cy_stc_sysint_t irq_cfg = { CYBSP_BT_UART_IRQ, 7U };

    mtb_hal_uart_register_callback(&s_obj, bt_uart_event_handler, NULL);
    Cy_SysInt_Init(&irq_cfg, bt_uart_irq_handler);
    mtb_hal_uart_enable_event(&s_obj, MTB_HAL_UART_IRQ_RX_DONE, true);
    NVIC_EnableIRQ(CYBSP_BT_UART_IRQ);
    s_rx_irq_on = RT_TRUE;
    rt_kprintf("[BT-uart] async RX irq on (RX_DONE)\n");
}

void hal_uart_dma_init(void)
{
    s_init_err = 0;
    if (s_initialized)
    {
        return;
    }
    rt_kprintf("[BT-uart] SCB4 setup (CYW55512 HCI UART)...\n");
    /* 与 AIROC 参考实现的 uart_init 一致: 先 Disable+DeInit 再全新初始化,
     * 避免 cybsp_init 阶段的既有配置残留影响 SCB4 状态 */
    Cy_SCB_UART_Disable(CYBSP_BT_UART_HW, &s_uart_context);
    Cy_SCB_UART_DeInit(CYBSP_BT_UART_HW);
    memset(&s_uart_context, 0, sizeof(s_uart_context));
    memset(&s_async_ctx, 0, sizeof(s_async_ctx));
    memset(&s_obj, 0, sizeof(s_obj));
    cy_en_scb_uart_status_t uart_status;
    uart_status = Cy_SCB_UART_Init(CYBSP_BT_UART_HW, &CYBSP_BT_UART_config, &s_uart_context);
    if (uart_status != CY_SCB_UART_SUCCESS)
    {
        s_init_err = 1;
        rt_kprintf("[BT-uart] SCB init failed: %d\n", (int)uart_status);
        return;
    }
    Cy_SCB_UART_Enable(CYBSP_BT_UART_HW);
    cy_rslt_t r = mtb_hal_uart_setup(&s_obj, &CYBSP_BT_UART_hal_config, &s_uart_context, NULL);
    if (r != CY_RSLT_SUCCESS)
    {
        s_init_err = 1;
        rt_kprintf("[BT-uart] setup failed: 0x%08lx\n", (unsigned long)r);
        return;
    }
    /* 与 AIROC 参考实现一致: 清 FIFO 并显式使能 CTS 硬件流控 */
    Cy_SCB_UART_ClearRxFifo(CYBSP_BT_UART_HW);
    Cy_SCB_UART_ClearTxFifo(CYBSP_BT_UART_HW);
    Cy_SCB_UART_EnableCts(CYBSP_BT_UART_HW);

    /* 与 AIROC 一致: 配置异步传输上下文 (read_async 的大帧/流控管理依赖) */
    (void)mtb_hal_uart_config_async(&s_obj, &s_async_ctx);

    uint32_t actual = 0;
    r = mtb_hal_uart_set_baud(&s_obj, FEATHERTALK_BT_UART_BAUD, &actual);
    if (r != CY_RSLT_SUCCESS)
    {
        s_init_err = 2;
        rt_kprintf("[BT-uart] set_baud failed: 0x%08lx\n", (unsigned long)r);
        return;
    }
    s_cur_baud = FEATHERTALK_BT_UART_BAUD;
    s_initialized = RT_TRUE;
    rt_kprintf("[BT-uart] init ok (SCB4 @%d, actual %lu)\n",
               FEATHERTALK_BT_UART_BAUD, (unsigned long)actual);
}

/* ---- 裸读写接口 (HCD 下载阶段使用, 异步中断使能之前) ---- */
int bt_uart_raw_open(uint32_t baud)
{
    uint32_t actual = 0;
    if (!s_initialized)
    {
        hal_uart_dma_init();
        if (s_init_err) return -1;
    }
    if (s_cur_baud != baud)
    {
        if (mtb_hal_uart_set_baud(&s_obj, baud, &actual) != CY_RSLT_SUCCESS) return -2;
        s_cur_baud = baud;
        rt_kprintf("[BT-uart] raw open @%lu (actual %lu)\n",
                   (unsigned long)baud, (unsigned long)actual);
    }
    return 0;
}

int bt_uart_raw_write(const uint8_t *buf, uint16_t len)
{
    /* CTS 反压有界重试: 模组失联/链路损坏时不得无限自旋 */
    rt_tick_t start = rt_tick_get();
    size_t off = 0;
    while (off < len)
    {
        size_t txlen = len - off;
        if (mtb_hal_uart_write(&s_obj, (void *)(buf + off), &txlen) != CY_RSLT_SUCCESS)
            return -1;
        if (txlen == 0)
        {
            if ((rt_tick_get() - start) >= rt_tick_from_millisecond(1000U))
            {
                return -2;
            }
            rt_thread_mdelay(1);
        }
        else
        {
            off += txlen;
        }
    }
    return len;
}

int bt_uart_raw_read(uint8_t *buf, uint16_t len, uint32_t timeout_ms)
{
    uint32_t waited = 0;
    uint16_t total = 0;
    while (total < len)
    {
        size_t rd = len - total;
        if (mtb_hal_uart_read(&s_obj, buf + total, &rd) == CY_RSLT_SUCCESS && rd > 0)
        {
            total += (uint16_t)rd;
        }
        else
        {
            if (waited >= timeout_ms) break;
            rt_thread_mdelay(2);
            waited += 2;
        }
    }
    return total;
}

int hal_uart_dma_rtthread_init_err(void)
{
    return s_init_err;
}

void hal_uart_dma_set_block_received(void (*callback)(void))
{
    s_block_received = callback;
}

void hal_uart_dma_set_block_sent(void (*callback)(void))
{
    s_block_sent = callback;
}

int hal_uart_dma_set_baud(uint32_t baud)
{
    uint32_t actual = 0;
    /* btstack 初始化早期会以栈默认值 0 调 set_baudrate; baud=0 会让
     * mtb_hal_clock_set_peri_clock_freq 以 0 为目标频率计算分频并崩溃
     * (实测 UsageFault → secure HardFault)。0 一律拒绝, 保持当前波特率。 */
    if (baud == 0U)
    {
        return -1;
    }
    /* 同值跳过: 重复的 set_baud 会临时 Disable UART 重配置, 可能惊扰链路 */
    if (s_initialized && (s_cur_baud == baud))
    {
        return 0;
    }
    cy_rslt_t r = mtb_hal_uart_set_baud(&s_obj, baud, &actual);
    rt_kprintf("[BT-uart] baud -> %lu (actual %lu)\n", (unsigned long)baud, (unsigned long)actual);
    if (r == CY_RSLT_SUCCESS)
    {
        s_cur_baud = baud;
    }
    return (r == CY_RSLT_SUCCESS) ? 0 : -1;
}

int hal_uart_dma_set_flowcontrol(int flowcontrol)
{
    /* CYBSP_BT_UART_hal_config 已含 rts_enable=1; CTS 硬件流控在 SCB config 中
     * enableCts=true。此处无需额外动作。 */
    (void)flowcontrol;
    return 0;
}

void hal_uart_dma_send_block(const uint8_t *buffer, uint16_t length)
{
    g_h4_tx_calls++;
    if (!s_initialized) return;
    rt_kprintf("[BT-uart] TX %u\n", (unsigned)length);
    rt_tick_t deadline = rt_tick_get() + rt_tick_from_millisecond(500);
    size_t off = 0;
    while (off < length)
    {
        size_t txlen = length - off;
        mtb_hal_uart_write(&s_obj, (void *)(buffer + off), &txlen);
        if (txlen == 0)
        {
            if (rt_tick_get() > deadline)
            {
                g_h4_tx_timeout++;
                rt_kprintf("[BT-uart] TX timeout (CTS backpressure?)\n");
                return;
            }
            rt_thread_mdelay(1);
        }
        else
        {
            off += txlen;
        }
    }
    if (s_block_sent)
    {
        s_block_sent();
    }
}

int bt_uart_raw_set_baud(uint32_t baud)
{
    uint32_t actual = 0;
    if (s_initialized && (s_cur_baud == baud)) return 0;
    if (mtb_hal_uart_set_baud(&s_obj, baud, &actual) != CY_RSLT_SUCCESS) return -1;
    s_cur_baud = baud;
    rt_kprintf("[BT-uart] baud -> %lu (actual %lu)\n", (unsigned long)baud, (unsigned long)actual);
    return 0;
}

/* btstack h4 契约: 请求收 len 字节, 完成后经 RX_DONE 中断回调 block_received。
 * 用 HAL 的 read_async——大帧 (Read Local Name 252B) 的 FIFO/流控由 HAL 管理。 */
void hal_uart_dma_receive_block(uint8_t *buffer, uint16_t len)
{
    if (!s_initialized) return;
    if (!s_rx_irq_on) return;
    cy_rslt_t r = mtb_hal_uart_read_async(&s_obj, buffer, len);
    if (r != CY_RSLT_SUCCESS)
    {
        rt_kprintf("[BT-uart] read_async failed: 0x%08lx\n", (unsigned long)r);
    }
}

void hal_uart_dma_set_csr_irq_handler(void (*csr_irq_handler)(void))
{
    (void)csr_irq_handler;
}

void hal_uart_dma_set_sleep(uint8_t sleep)
{
    (void)sleep;
}

int hal_uart_dma_get_supported_sleep_modes(void)
{
    return 0;
}
