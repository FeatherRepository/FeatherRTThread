/* bt_bringup.c - 蓝牙 bring-up (S0-S5 上电时序/HCD/波特率切换/启停)
 * 自 bt_main.c 机械拆分 (2026-10-24), 内容零改动。 */
#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>
#include <stdlib.h>
#include "board.h"
#include <feathertalk/radio_manager.h>

#include "cycfg_pins.h"
#include "ipc/feathertalk_ipc.h"
#include "bt_service_api.h"

#include <btstack_config.h>
#include "btstack_run_loop.h"
#include "btstack_run_loop_embedded.h"
#include "btstack_uart_block.h"
#include "btstack_event.h"
#include "hci.h"
#include "hci_transport.h"
#include "hci_transport_h4.h"
#include "gap.h"
#include "btstack_memory.h"
#include "hci_dump.h"
#include "hci_dump_embedded_stdout.h"
#include "ble/att_server.h"
/* M8.1: LE SMP (ASCS 控制点要求加密写入, 手机先配对再配流) */
#include "ble/sm.h"
#include "ft_gatt.h"
/* M4a Classic: SDP server + link key 内存库 (SSP Just Works 配对) */
#include "bluetooth.h"
#include "classic/sdp_server.h"
#include "classic/btstack_link_key_db_memory.h"

/* M4b: A2DP Sink + AVRCP (bt_a2dp_sink.c) */
extern int bt_a2dp_sink_setup(void);
extern void bt_a2dp_sink_quiesce(void);
extern void bt_uart_shutdown(void);
extern int bt_a2dp_source_setup(void);
extern const btstack_link_key_db_t * bt_bond_store_instance(void);

/* M8.1: LE Audio BAP Unicast Server (bt_le_audio_unicast.c) */
#include "bt_le_audio.h"
#include "bt_internal.h"

#define FT_BT_DOWNLOAD_BAUD         3000000UL
/* 运行时波特率: A2DP 媒体流需要 ~50KB/s, 115200 (≈11.5KB/s) 根本喂不饱,
 * 实测手机播放时 ACL 到达仅 ~0.2KB/s 突发 (控制器缓冲溢出丢包) -> 全程断音。
 * HCD 打补丁后模组回默认 115200, 用厂商命令 FC18 切 3M (命令完成事件
 * 在旧波特率返回, 之后模组即切; 主机随后切同速)。 */
#define FT_BT_RUNTIME_BAUD          3000000UL
#define HCI_VSC_UPDATE_BAUD         0xFC18U
#define FT_BT_AUTOBAUD_RESET_MS     100U
#define FT_BT_CTS_TIMEOUT_MS        2000U

#define HCI_RESET                   0x0C03U

extern int  bt_hcd_download_run(void);
extern int  bt_hcd_send_raw_command(uint16_t opcode, uint8_t plen, const uint8_t *params);
extern int  bt_uart_raw_open(uint32_t baud);
extern int  bt_uart_raw_set_baud(uint32_t baud);
extern const btstack_uart_block_t *btstack_uart_block_embedded_instance(void);
/* h4 传输配置: 必须是 hci_transport_config_uart_t (首字段 type=
 * HCI_TRANSPORT_CONFIG_UART)。之前误传 btstack_uart_config_t: 该工程
 * -fshort-enums, type 仅读首字段低 1B —— 115200 低字节恰为 0x00 混过
 * 检查, 切 3M (低字节 0xC0) 后暴露, 传输层静默失效不广播。
 * baudrate_init 在 S4b 切速成功后改 3M, 失败保持 115200 (与模组一致)。 */
static hci_transport_config_uart_t s_uart_config = {
    .type = HCI_TRANSPORT_CONFIG_UART,
    .baudrate_init = FEATHERTALK_BT_UART_BAUD,
    .baudrate_main = 0,                    /* 0 = 与 init 同速 */
    .flowcontrol = 0,
    .device_name = FEATHERTALK_BT_UART_DEV,
    .parity = 0,
};
/* S0: 模组上电并进入自动波特率模式 (参考 feathertalk_bt_controller.c) */
static int bt_prepare_autobaud(void)
{
    int result = ft_radio_acquire(FT_RADIO_BT);
    if (result != RT_EOK) return result;

    /* 保持控制器禁休眠: DEVICE_WAKE 必须拉高 (本板启动时序实测:
     * "DEVICE_WAKE 保持高(禁休眠)")。参考实现里写成 0 是错的——
     * 拉低=允许休眠, btstack init 的空闲期会让控制器睡死不再应答 */
    Cy_GPIO_Pin_FastInit(CYBSP_BT_DEVICE_WAKE_PORT,
                         CYBSP_BT_DEVICE_WAKE_PIN,
                         CY_GPIO_DM_STRONG,
                         1U,
                         HSIOM_SEL_GPIO);

    /* CYW55500 在 REG_ON 翻转期间主机 RTS 保持低电平 -> 自动波特率模式 */
    Cy_GPIO_Pin_FastInit(CYBSP_BT_UART_RTS_PORT,
                         CYBSP_BT_UART_RTS_PIN,
                         CY_GPIO_DM_STRONG,
                         0U,
                         HSIOM_SEL_GPIO);
    result = ft_radio_reset(FT_RADIO_BT, FT_BT_AUTOBAUD_RESET_MS, FT_BT_AUTOBAUD_RESET_MS);
    if (result != RT_EOK) return result;

    /* HCI 引脚归还 BSP 复用配置, 随后才能开 SCB4 */
    Cy_GPIO_Pin_Init(CYBSP_BT_UART_RX_PORT,
                     CYBSP_BT_UART_RX_PIN,
                     &CYBSP_BT_UART_RX_config);
    Cy_GPIO_Pin_Init(CYBSP_BT_UART_TX_PORT,
                     CYBSP_BT_UART_TX_PIN,
                     &CYBSP_BT_UART_TX_config);
    Cy_GPIO_Pin_Init(CYBSP_BT_UART_CTS_PORT,
                     CYBSP_BT_UART_CTS_PIN,
                     &CYBSP_BT_UART_CTS_config);
    Cy_GPIO_Pin_Init(CYBSP_BT_UART_RTS_PORT,
                     CYBSP_BT_UART_RTS_PIN,
                     &CYBSP_BT_UART_RTS_config);
    return RT_EOK;
}

static int bt_wait_cts_low(void)
{
    rt_tick_t start = rt_tick_get();

    while (Cy_GPIO_Read(CYBSP_BT_UART_CTS_PORT, CYBSP_BT_UART_CTS_PIN) != 0U)
    {
        if ((rt_tick_get() - start) >= rt_tick_from_millisecond(FT_BT_CTS_TIMEOUT_MS))
        {
            return -1;
        }
        rt_thread_mdelay(10);
    }
    return 0;
}

static void power_seq_off(void)
{
    ft_radio_release(FT_RADIO_BT);
}

int bt_bringup(void)
{
    int rc;

    g_bt_coex_diag.starts++;
    s_bt_err = 0;
    s_uart_config.baudrate_init = FEATHERTALK_BT_UART_BAUD;

    /* 等 M55 IPC 上线 (最多 ~10s): 保证管道就绪、事件在 COM9 可见 */
    for (int i = 0; i < 100 && !feathertalk_ipc_peer_online(); i++)
    {
        if (!s_desired_on) return -RT_EBUSY;
        rt_thread_mdelay(100);
    }

    /* M3: 等 M55 铺好 bond 块 (通常数百 ms; M55 慢时最多 8s), 配对信息
     * 在连接可用之前就位, 避免连接先到而钥匙后到 */
    bt_bond_store_wait_ready(8000);

    rt_kprintf("[BT] S0 prepare autobaud (P16.3 + RTS low + REG_ON toggle)\n");
    feathertalk_ipc_send_event(1);
    rc = bt_prepare_autobaud();
    if (rc != RT_EOK) {
        s_bt_err = 90;
        ft_radio_set_state(FT_RADIO_BT, FT_RADIO_ERROR, rc);
        s_bt_starting = RT_FALSE;
        feathertalk_ipc_send_event(90);
        return -90;
    }

    if (bt_uart_raw_open(FT_BT_DOWNLOAD_BAUD) != 0)
    {
        rt_kprintf("[BT] raw open failed\n");
        ft_radio_set_state(FT_RADIO_BT, FT_RADIO_ERROR, -RT_ERROR);
        feathertalk_ipc_send_event(90);
        s_bt_starting = RT_FALSE;
        return -90;
    }
    feathertalk_ipc_send_event(20);

    if (bt_wait_cts_low() != 0)
    {
        rt_kprintf("[BT] CTS stayed high after autobaud reset\n");
        s_bt_err = 91;
        ft_radio_set_state(FT_RADIO_BT, FT_RADIO_ERROR, s_bt_err);
        feathertalk_ipc_send_event(91);
        s_bt_starting = RT_FALSE;
        return -91;
    }

    rt_kprintf("[BT] S2 HCI_Reset probe: 115200 first, then 3M\n");
    bt_uart_raw_set_baud(FEATHERTALK_BT_UART_BAUD);
    rt_thread_mdelay(50);
    rc = bt_hcd_send_raw_command(HCI_RESET, 0, RT_NULL);
    if (rc == 0)
    {
        rt_kprintf("[BT] answered @115200\n");
        feathertalk_ipc_send_event(50);
    }
    else
    {
        bt_uart_raw_set_baud(FT_BT_DOWNLOAD_BAUD);
        rt_thread_mdelay(50);
        rc = bt_hcd_send_raw_command(HCI_RESET, 0, RT_NULL);
        if (rc != 0)
        {
            rt_kprintf("[BT] initial HCI reset failed @3M and @115200: %d\n", rc);
            s_bt_err = 92;
            ft_radio_set_state(FT_RADIO_BT, FT_RADIO_ERROR, s_bt_err);
            feathertalk_ipc_send_event((rc > 0) ? (600u + (rt_uint32_t)rc) : 92u);
            s_bt_starting = RT_FALSE;
            return -92;
        }
        rt_kprintf("[BT] answered @3M (autobaud trained)\n");
        feathertalk_ipc_send_event(51);
    }

    rt_kprintf("[BT] S3 HCD download\n");
    feathertalk_ipc_send_event(3);
    BT_CP(30);
    rc = bt_hcd_download_run();
    if (rc != 0)
    {
        rt_kprintf("[BT] HCD fail rc=%d\n", rc);
        s_bt_err = 93;
        ft_radio_set_state(FT_RADIO_BT, FT_RADIO_ERROR, s_bt_err);
        feathertalk_ipc_send_event(93);
        s_bt_starting = RT_FALSE;
        return -93;
    }
    feathertalk_ipc_send_event(7);
    BT_CP(40);

    rt_kprintf("[BT] S4 host back to 115200 + HCI_Reset\n");
    bt_uart_raw_set_baud(FEATHERTALK_BT_UART_BAUD);
    BT_CP(41);
    rt_thread_mdelay(100);
    rc = bt_hcd_send_raw_command(HCI_RESET, 0, RT_NULL);
    if (rc != 0)
    {
        rt_kprintf("[BT] post-patch HCI reset failed: %d\n", rc);
        s_bt_err = 94;
        ft_radio_set_state(FT_RADIO_BT, FT_RADIO_ERROR, s_bt_err);
        feathertalk_ipc_send_event(94);
        s_bt_starting = RT_FALSE;
        return -94;
    }
    BT_CP(42);

    /* S4b: 运行时波特率 115200 -> 3M (A2DP 媒体流带宽刚需)。
     * FC18 载荷: [00 00][baud LE 4B]; 命令完成事件仍在旧波特率返回,
     * 收到后模组立即切速, 主机随即切同速。失败则保持 115200 继续
     * (BLE 够用, A2DP 会断音 —— 打日志明示, 不静默降级)。 */
    {
        uint8_t fc18[6] = { 0x00U, 0x00U,
                            (uint8_t)(FT_BT_RUNTIME_BAUD & 0xFFU),
                            (uint8_t)((FT_BT_RUNTIME_BAUD >> 8) & 0xFFU),
                            (uint8_t)((FT_BT_RUNTIME_BAUD >> 16) & 0xFFU),
                            (uint8_t)((FT_BT_RUNTIME_BAUD >> 24) & 0xFFU) };
        rc = bt_hcd_send_raw_command(HCI_VSC_UPDATE_BAUD, sizeof(fc18), fc18);
        if (rc == 0)
        {
            rt_kprintf("[BT] S4b runtime baud -> %lu\n", (unsigned long)FT_BT_RUNTIME_BAUD);
            bt_uart_raw_set_baud(FT_BT_RUNTIME_BAUD);
            s_uart_config.baudrate_init = FT_BT_RUNTIME_BAUD;
            rt_thread_mdelay(20);
        }
        else
        {
            rt_kprintf("[BT] S4b FC18 baud switch FAILED rc=%d, stay @115200 (A2DP 会断音!)\n", rc);
            feathertalk_ipc_send_event(95);
        }
        BT_CP(43);
    }

    s_bt_err = 0;
    if (!s_desired_on) return -RT_EBUSY;
    if (!s_stack_initialized) {
    rt_kprintf("[BT] S5 btstack init\n");
    feathertalk_ipc_send_event(8);
    /* HCI 抓包走 ACL/NOP 过滤包装 (见上; 全量 dump 会拖死媒体流) */
    s_dump_stdout = hci_dump_embedded_stdout_get_instance();
    hci_dump_init(&s_dump_filtered);
    /* 再关掉 DEBUG 级日志: hci.c 发送路径每包 3 行 log_debug, 115200 控制台
     * 每行阻塞 ~5ms, 实测把 A2DP Source 发送节流到了 27 pkt/s (带宽 1/4) */
    hci_dump_enable_log_level(HCI_DUMP_LOG_LEVEL_DEBUG, false);
    /* btstack_memory_init 必须在 hci_init 之前: 连接对象/L2CAP 通道都来自
     * 静态内存池。漏调时池为空 (BSS 全零), 首个连接到来时
     * btstack_memory_hci_connection_get 返回 NULL, hci.c:3763 提前 return,
     * 句柄永不登记 -> "acl_handler called with non-registered handle" (实测) */
    btstack_memory_init();
    btstack_run_loop_init(btstack_run_loop_embedded_get_instance());
    BT_CP(50);
    const hci_transport_t *transport =
        hci_transport_h4_instance(btstack_uart_block_embedded_instance());
    hci_init(transport, &s_uart_config);
    BT_CP(52);

    /* L2CAP 必须初始化: 它注册 ATT(CID4)/SM(CID6)/信令固定通道;
     * 缺了它 ACL 数据包在 l2cap 层全数丢弃, ATT 交换无法进行
     * (实测: 手机连接后服务发现超时)。且 hci_register_acl_packet_handler
     * 会覆盖 l2cap 的 ACL 路由, 因此绝不能再调它。 */
    l2cap_init();

    /* M4a Classic: SDP server + SSP Just Works + link key 内存库。
     * NoInputNoOutput + auto accept => 手机配对无需本机交互 */
    sdp_init();
    /* M3': link key 持久化库 (内存表 + 共享块, M55 托管 /flash 文件) */
    hci_set_link_key_db(bt_bond_store_instance());
    gap_ssp_set_io_capability(SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    gap_ssp_set_auto_accept(1);
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH |
                                         LM_LINK_POLICY_ENABLE_SNIFF_MODE);
    gap_set_allow_role_switch(true);

    /* M4b: A2DP Sink + AVRCP (SDP 记录/流端点/handler 注册, 纯静态登记,
     * 须在 hci_power_on 前完成) */
    /* SMP is initialized below before power-on; extended advertising uses
     * the GAP scheduler instead of competing direct legacy HCI commands. */
    if (bt_a2dp_sink_setup() != 0) {
        s_stack_initialized = RT_TRUE;
        s_stack_setup_failed = RT_TRUE;
        return -96; /* Partial profile setup must not be registered a second time. */
    }

    /* M5: A2DP Source (反向 ring 媒体泵, 静态登记; 连接由 msh bt_src 驱动) */
    bt_a2dp_source_setup();

    feathertalk_ipc_send_event(10);
    s_hci_event_handler.callback = &packet_handler;
    hci_add_event_handler(&s_hci_event_handler);
    feathertalk_ipc_send_event(11);
    BT_CP(53);

    /* M2: att_server (GAP+DIS+FT Data) */
    sm_init();
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_authentication_requirements(SM_AUTHREQ_BONDING | SM_AUTHREQ_SECURE_CONNECTION);
    s_sm_event_reg.callback = ft_sm_handler;
    sm_add_event_handler(&s_sm_event_reg);
    /* M8.x: LE bond 持久化 —— 把共享块 LE 段灌回 le_device_db (有界等待) */
    {
        extern void ft_le_bond_load(void);
        ft_le_bond_load();
    }
    att_server_init(profile_data, att_read_callback, att_write_callback);
    att_server_register_packet_handler(packet_handler);
    /* M8.1: LE Audio Unicast Server (PACS/ASCS/CIS acceptor/ISO 收流) */
    ft_le_audio_init();
    /* M8.2: Auracast 广播源 (msh bt_bcast_start 手动启动) */
    ft_le_audio_broadcast_init();
    s_stack_initialized = RT_TRUE;
    g_bt_coex_diag.stack_inits++;
    }
    feathertalk_ipc_send_event(12);
    BT_CP(54);

    /* M2: 2s 状态 notify 定时器 (订阅后方发出) */
    s_notify_timer.process = &ft_notify_timer_handler;
    btstack_run_loop_set_timer(&s_notify_timer, 2000);
    btstack_run_loop_add_timer(&s_notify_timer);

    /* 广播保活定时器: 每 3s 检查未连接且未广播则重新使能 (防连接失败假死) */
    s_adv_keepalive_timer.process = &ft_adv_keepalive_handler;
    btstack_run_loop_set_timer(&s_adv_keepalive_timer, 3000);
    btstack_run_loop_add_timer(&s_adv_keepalive_timer);

    rt_kprintf("[BT] hci_power_on\n");
    feathertalk_ipc_send_event(9);
    s_bt_state = 1;
    rc = hci_power_control(HCI_POWER_ON);
    BT_CP(56);
    feathertalk_ipc_send_event(13);
    return rc;
}

int bt_stop_owned(void)
{
    int result = RT_EOK;
    g_bt_coex_diag.stops++;
    s_bt_state = BT_STOPPING;
    s_owner_callback = RT_NULL;
    if (s_stack_initialized) {
        btstack_run_loop_remove_timer(&s_notify_timer);
        btstack_run_loop_remove_timer(&s_adv_keepalive_timer);
        bt_a2dp_sink_quiesce(); /* M55 must release sound0 even on controller loss. */
        hci_power_control(HCI_POWER_OFF);
        uint32_t start = rt_tick_get_millisecond();
        while (hci_get_state() != HCI_STATE_OFF) {
            btstack_run_loop_embedded_execute_once();
            if (rt_tick_get_millisecond() - start > 5000U) {
                result = -RT_ETIMEOUT;
                break;
            }
        }
    }
    bt_uart_shutdown(); /* Abort RX, mask IRQ, clear FIFO and pending callbacks. */
    if (s_stack_initialized) bt_a2dp_sink_quiesce(); /* Drop state from late disconnect callbacks too. */
    power_seq_off();
    s_gatt_connected = s_classic_connected = s_notify_enabled = 0;
    s_gatt_con_handle = s_classic_handle = HCI_CON_HANDLE_INVALID;
    s_bt_starting = RT_FALSE;
    s_bt_state = result == RT_EOK ? BT_OFF : BT_ERROR;
    s_bt_err = result;
    if (result) ft_radio_set_state(FT_RADIO_BT, FT_RADIO_ERROR, result);
    rt_kprintf("[BT] off (BT_REG_ON low; shared radio supply retained)\n");
    feathertalk_ipc_send_event(result ? 98U : 42U);
    return result;
}
