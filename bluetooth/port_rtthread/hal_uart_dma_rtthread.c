/* hal_uart_dma_rtthread.c - btstack hal_uart_dma 契约的 mtb_hal 直驱实现 (M1 正式版)
 *
 * 传输对象: SCB4 = CYW55512 BT HCI UART
 *   - 配置符号 CYBSP_BT_UART_config / CYBSP_BT_UART_hal_config / CYBSP_BT_UART_clock_ref
 *     由 Configurator 生成 (projects/libs/TARGET_APP_KIT_PSE84_EVAL_EPC2/config/GeneratedSource)
 *   - 引脚: P10.0-RX / P10.1-TX / P10.2-CTS / P10.3-RTS (hal_config 内含, setup 时自动施加)
 *
 * 设计说明:
 *   - 不走 rt_serial/BSP_USING_UART4: 启动期注册 SCB4 会导致 M55 启动挂死 (实测),
 *     改为 bt_on 时经 mtb_hal_uart_setup 现场初始化
 *   - RX: 专用线程轮询 mtb_hal_uart_read (115200 下轮询开销可忽略; HCD 下载切 3M 后
 *     需改异步, 见 TODO)
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

static mtb_hal_uart_t      s_obj;
static rt_sem_t            s_rx_sem;
static rt_thread_t         s_rx_thread;
static volatile uint8_t   *s_rx_buf;
static volatile uint16_t   s_rx_len;
static void (*s_block_received)(void);
static void (*s_block_sent)(void);
static volatile rt_bool_t  s_rx_pending;
static volatile int        s_init_err;                    /* 0=ok 1=setup fail 2=baud fail 3=no sem */
static volatile rt_bool_t  s_initialized;

static void rx_thread_entry(void *param)
{
    (void)param;
    while (1)
    {
        if (!s_initialized)
        {
            rt_thread_mdelay(20);
            continue;
        }
        /* 有未决的接收请求时优先填它 */
        if (s_rx_pending && s_rx_buf)
        {
            size_t want = s_rx_len;
            size_t got = 0;
            rt_tick_t start = rt_tick_get();
            while (got < want)
            {
                size_t rd = want - got;
                cy_rslt_t r = mtb_hal_uart_read(&s_obj, (void *)(s_rx_buf + got), &rd);
                if ((r == CY_RSLT_SUCCESS) && (rd > 0))
                {
                    got += rd;
                    start = rt_tick_get();   /* 有进展则续期 */
                }
                else
                {
                    /* 半帧超时 (2s 无进展): 放弃本轮, 释放槽位。
                     * btstack 初始化序列含厂商命令, 响应可能数百 ms,
                     * 阈值必须容忍; 模组断联时不得抱着请求无限自旋 */
                    if ((rt_tick_get() - start) >= rt_tick_from_millisecond(2000U))
                    {
                        break;
                    }
                    rt_thread_mdelay(2);
                }
            }
            s_rx_pending = RT_FALSE;
            /* 只有收满才回调 h4; 半帧放弃时 h4 请求自然滞留,
             * 链路易处于失同步状态, 恢复依赖整轮下电 (见 bt_main 异常流程) */
            if (got >= want && s_block_received)
            {
                s_block_received();
            }
        }
        else
        {
            /* 无接收请求时绝不能读 FIFO: h4 的 receive_block 由 run loop 轮询发起,
             * 控制器的应答往往先于请求到达 (Reset CC 仅 7 字节, 躺在 SCB FIFO
             * 里等请求即可)。早期版本在此排空丢弃, 把应答当垃圾扔掉 →
             * h4 永远等不到 → 状态机卡死 (实测 state 停在 1)。 */
            rt_thread_mdelay(2);
        }
    }
}

void hal_uart_dma_init(void)
{
    s_init_err = 0;
    if (s_initialized)
    {
        /* HCD 下载阶段已完成底层 setup, 仅确保运行波特率 */
        uint32_t actual = 0;
        mtb_hal_uart_set_baud(&s_obj, FEATHERTALK_BT_UART_BAUD, &actual);
        rt_kprintf("[BT-uart] already up, baud %d (actual %lu)\n",
                   FEATHERTALK_BT_UART_BAUD, (unsigned long)actual);
        return;
    }
    rt_kprintf("[BT-uart] SCB4 setup (CYW55512 HCI UART)...\n");
    /* 与 AIROC 参考实现的 uart_init 一致: 先 Disable+DeInit 再全新初始化,
     * 避免 cybsp_init 阶段的既有配置残留影响 SCB4 状态 */
    static cy_stc_scb_uart_context_t s_uart_context;
    Cy_SCB_UART_Disable(CYBSP_BT_UART_HW, &s_uart_context);
    Cy_SCB_UART_DeInit(CYBSP_BT_UART_HW);
    memset(&s_uart_context, 0, sizeof(s_uart_context));
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
    uint32_t actual = 0;
    r = mtb_hal_uart_set_baud(&s_obj, FEATHERTALK_BT_UART_BAUD, &actual);
    if (r != CY_RSLT_SUCCESS)
    {
        s_init_err = 2;
        rt_kprintf("[BT-uart] set_baud failed: 0x%08lx\n", (unsigned long)r);
        return;
    }
    s_rx_sem = rt_sem_create("bth4rx", 0, RT_IPC_FLAG_FIFO);
    if (s_rx_sem == RT_NULL)
    {
        s_init_err = 3;
        rt_kprintf("[BT-uart] sem create failed\n");
        return;
    }
    s_initialized = RT_TRUE;
    { extern void feathertalk_ipc_send_event(unsigned long code); feathertalk_ipc_send_event(20); }
    rt_kprintf("[BT-uart] init ok (SCB4 @%d, actual %lu)\n",
               FEATHERTALK_BT_UART_BAUD, (unsigned long)actual);
}

/* ---- 裸读写接口 (HCD 下载阶段使用, btstack 启动前) ---- */
int bt_uart_raw_open(uint32_t baud)
{
    uint32_t actual = 0;
    if (!s_initialized)
    {
        hal_uart_dma_init();
        if (s_init_err) return -1;
    }
    if (mtb_hal_uart_set_baud(&s_obj, baud, &actual) != CY_RSLT_SUCCESS) return -2;
    rt_kprintf("[BT-uart] raw open @%lu (actual %lu)\n",
               (unsigned long)baud, (unsigned long)actual);
    return 0;
}

int bt_uart_raw_write(const uint8_t *buf, uint16_t len)
{
    /* CTS 反压有界重试: 模组失联/链路损坏时不得无限自旋 (F2 风暴教训) */
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
    cy_rslt_t r = mtb_hal_uart_set_baud(&s_obj, baud, &actual);
    rt_kprintf("[BT-uart] baud -> %lu (actual %lu)\n", (unsigned long)baud, (unsigned long)actual);
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
    if (!s_initialized) return;
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
    if (mtb_hal_uart_set_baud(&s_obj, baud, &actual) != CY_RSLT_SUCCESS) return -1;
    rt_kprintf("[BT-uart] baud -> %lu (actual %lu)\n", (unsigned long)baud, (unsigned long)actual);
    return 0;
}

void hal_uart_dma_receive_block(uint8_t *buffer, uint16_t len)
{
    if (!s_initialized) return;
    if (s_rx_pending)
    {
        /* h4 设计上同一时刻只有一个未完成请求; 防御: 不覆盖在飞请求,
         * 否则 rx 线程会把数据写进新缓冲区同时毁坏两个请求 */
        rt_kprintf("[BT-uart] WARN: receive_block while pending\n");
        return;
    }
    s_rx_buf     = buffer;
    s_rx_len     = len;
    s_rx_pending = RT_TRUE;
    rt_sem_release(s_rx_sem);
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

/* 收包线程由 bt_main 在电源时序后创建 (栈 4096: h4 分帧+回调路径预留) */
void hal_uart_dma_rtthread_start_rx_thread(void)
{
    s_rx_thread = rt_thread_create("bth4rx", rx_thread_entry, RT_NULL,
                                   4096, 12, 10);
    if (s_rx_thread)
    {
        rt_thread_startup(s_rx_thread);
    }
}
