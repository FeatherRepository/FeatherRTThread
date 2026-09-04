/* bt_main.c - FeatherTalk 蓝牙服务 (M33, btstack HCI-UART, 开源自研栈)
 *
 * Bring-up 流程 (bt_loop_thread_entry), 与 AIROC 参考实现
 * (applications/bluetooth/feathertalk_bt_controller.c) 板测验证的时序一致:
 *   S0 向 Radio Manager 申请 BT 域 (公共供电已由板级管理器上电)
 *      + DEVICE_WAKE 禁休眠 + RTS 拉低跨 BT_REG_ON
 *      翻转进入自动波特率模式
 *   S1 SCB4 UART @3M, 等 CTS 拉低
 *   S2 HCI_Reset @3M (自动波特率训练)
 *   S3 HCD 固件下载 (519 条记录, 官方 wlbga 固件, 来自 bt-fw 子模块)
 *   S4 主机回 115200 (新固件默认运行波特率) + 再次 HCI_Reset
 *   S4b 厂商命令 FC18 将控制器和主机运行波特率同步切至 3M
 *   S5 btstack 首次初始化 (后续复用) -> hci_power_on -> WORKING
 *   WORKING 后启动 BLE 广播 (名称 FeatherTalk), 状态经 IPC QUICK_STATUS 上报
 *
 * msh 命令: bt_on / bt_off / bt_info (M33 控制台)
 * IPC: QUICK_COMMAND{BLUETOOTH} 远程驱动; QUICK_STATUS 回报能力/使能;
 *      各阶段经 feathertalk_ipc_send_event 上报事件码 (M55 控制台可见)
 */
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
#include "ft_gatt.h"
/* M4a Classic: SDP server + link key 内存库 (SSP Just Works 配对) */
#include "bluetooth.h"
#include "classic/sdp_server.h"
#include "classic/btstack_link_key_db_memory.h"

/* M4b: A2DP Sink + AVRCP (bt_a2dp_sink.c) */
extern int bt_a2dp_sink_setup(void);
extern void bt_a2dp_sink_quiesce(void);
extern void bt_uart_shutdown(void);

/* FT Data CCCD 使能值 (Bluetooth 规范) */
#define FT_GATT_CCCD_NOTIFICATION   0x0001U

#ifndef FEATHERTALK_BT_UART_DEV
#define FEATHERTALK_BT_UART_DEV     "uart4"
#endif
#ifndef FEATHERTALK_BT_UART_BAUD
#define FEATHERTALK_BT_UART_BAUD    115200
#endif

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

static rt_thread_t  s_loop_thread;
static volatile int s_bt_state;
volatile int s_bt_err;        /* 0=无错误；非零为最近一次生命周期失败码 */
static volatile rt_bool_t s_bt_starting;   /* bring-up 进行中, 防重复创建 btloop */
static volatile rt_bool_t s_desired_on;
static volatile rt_bool_t s_request_pending;
static rt_bool_t s_stack_initialized;
static rt_bool_t s_stack_setup_failed;
static void (*s_owner_callback)(void);
static struct rt_mutex s_control_lock;
static struct rt_semaphore s_control_wake;
enum { BT_OFF, BT_STARTING, BT_READY, BT_STOPPING, BT_ERROR };
/* One owner thread for UART, HCD, HCI, timers and profiles, for the lifetime
 * of the service. IPC/UI never call stack APIs or delete this thread. */
int bt_service_start_cancelled(void) { return !s_desired_on; }
static btstack_packet_callback_registration_t s_hci_event_handler;

/* 崩溃现场存活检查点: 写在静态变量里, 即使整机死锁也能用 GDB 读出最后一步 */
volatile uint32_t g_bt_checkpoint;
#define BT_CP(code) do { g_bt_checkpoint = (uint32_t)(code); } while (0)

/* Passive coexistence diagnostics. Count replies to EXISTING HCI traffic;
 * do not send resets/probes, allocate memory, or add periodic IPC traffic.
 * Read via the M33 debugger even when its serial connector is unplugged. */
volatile struct {
    uint32_t starts;
    uint32_t command_completes;
    uint32_t advertising_acks;
    uint32_t hardware_errors;
    uint32_t last_complete_ms;
    uint32_t stops;
    uint32_t stack_inits;
} g_bt_coex_diag;

/* 三条广播命令的 Command Complete 状态 (0xFF=未收到), GDB 可读 */
volatile uint8_t g_adv_cc_status[3] = { 0xFF, 0xFF, 0xFF };
#define ADV_CC_PARAMS  0
#define ADV_CC_DATA    1
#define ADV_CC_ENABLE  2

/* 事件环: 最近 12 个事件的 [type, opcode/status], GDB 可读 */
volatile uint16_t g_bt_evt_log[12][2];
volatile uint8_t  g_bt_evt_log_pos;
static void bt_evt_log(uint16_t a, uint16_t b)
{
    uint8_t i = g_bt_evt_log_pos % 12U;
    g_bt_evt_log[i][0] = a;
    g_bt_evt_log[i][1] = b;
    g_bt_evt_log_pos++;
}

/* 线路嗅探环 (hal_uart_dma 填充完成时记录包类型), GDB 可读 */
volatile uint8_t g_rx_log[16][2];
volatile uint8_t g_rx_log_pos;

/* h4 TX 计数 (定位"Reset 是否发出/TX 是否被 CTS 卡死"), GDB 可读 */
extern volatile uint32_t g_h4_tx_calls;
extern volatile uint32_t g_h4_tx_timeout;
/* h4 TX 内容嗅探: 最近 4 次 [len, bytes...] */
volatile uint8_t g_h4_tx_log[4][20];
/* SCB 回环自测结果: [31:16]=matched [15:0]=avail */
volatile uint32_t g_lb_result;
/* 裸路径 Reset 在 h4 open 前的应答码 (二分定位用) */
volatile int g_raw_reset_rc = -99;

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

/* 抓包减负 (A2DP 媒体流必需): 3M HCI + ~45KB/s 媒体流下, 全量 ACL 十六进制
 * dump 远超 115200 控制台带宽, rt_kprintf 同步发送会反过来拖死 btloop。
 * 过滤包装: 丢 ACL 数据帧与高频 NOP(Number Of Completed Packets, 0x13)事件,
 * 其余事件/日志原样保留 (排障能力不降)。 */
static const hci_dump_t *s_dump_stdout;
static void ft_dump_log_packet(uint8_t packet_type, uint8_t in,
                               uint8_t *packet, uint16_t len)
{
    if (packet_type == HCI_ACL_DATA_PACKET)
    {
        return;
    }
    if ((packet_type == HCI_EVENT_PACKET) && (len >= 1) && (packet[0] == 0x13U))
    {
        return;
    }
    s_dump_stdout->log_packet(packet_type, in, packet, len);
}
static void ft_dump_log_message(int log_level, const char *format, va_list argptr)
{
    s_dump_stdout->log_message(log_level, format, argptr);
}
static const hci_dump_t s_dump_filtered = {
    .reset = RT_NULL,
    .log_packet = ft_dump_log_packet,
    .log_message = ft_dump_log_message,
};

/* ---- GATT 服务状态 (att_server, M2) ---- */
static hci_con_handle_t s_gatt_con_handle;
static volatile int   s_gatt_connected;
/* M4a: Classic ACL 链路状态 (与 LE 连接互不排斥, 双模并存) */
static hci_con_handle_t s_classic_handle;
static volatile int   s_classic_connected;
static volatile int   s_notify_enabled;
static btstack_timer_source_t s_notify_timer;
static uint16_t       s_notify_counter;
static uint8_t        s_ft_cmd;      /* 最近一条 FT CMD 命令字节 (供回读) */

/* NTF 状态帧: [cnt_lo][cnt_hi][bt_state][connected][err][last_cmd] */
static uint16_t ft_ntf_build_payload(uint8_t *out)
{
    out[0] = (uint8_t)(s_notify_counter & 0xFFU);
    out[1] = (uint8_t)(s_notify_counter >> 8);
    out[2] = (uint8_t)s_bt_state;
    out[3] = (uint8_t)s_gatt_connected;
    out[4] = (uint8_t)((s_bt_err < 0) ? 0xFF : s_bt_err);
    out[5] = s_ft_cmd;
    return 6;
}

static uint16_t att_read_callback(hci_con_handle_t con_handle, uint16_t att_handle,
                                  uint16_t offset, uint8_t *buffer, uint16_t buffer_size)
{
    (void)con_handle;
    if (att_handle == ATT_CHARACTERISTIC_46540002_4654_4541_4C4B_000000000002_01_VALUE_HANDLE)
    {
        if ((offset == 0U) && (buffer != NULL) && (buffer_size >= 1U))
        {
            buffer[0] = s_ft_cmd;
            return 1;
        }
        return 0;
    }
    if (att_handle == ATT_CHARACTERISTIC_46540003_4654_4541_4C4B_000000000003_01_VALUE_HANDLE)
    {
        uint8_t payload[8];
        uint16_t len = ft_ntf_build_payload(payload);
        if ((buffer != NULL) && (offset < len) && (buffer_size >= (len - offset)))
        {
            memcpy(buffer, &payload[offset], len - offset);
            return len - offset;
        }
        return 0;
    }
    return 0;
}

static int att_write_callback(hci_con_handle_t con_handle, uint16_t att_handle,
                              uint16_t transaction_mode, uint16_t offset,
                              uint8_t *buffer, uint16_t buffer_size)
{
    (void)transaction_mode;
    (void)offset;
    if (att_handle == ATT_CHARACTERISTIC_46540003_4654_4541_4C4B_000000000003_01_CLIENT_CONFIGURATION_HANDLE)
    {
        s_notify_enabled =
            (little_endian_read_16(buffer, 0) == FT_GATT_CCCD_NOTIFICATION) ? 1 : 0;
        s_gatt_con_handle = con_handle;
        feathertalk_ipc_send_event(s_notify_enabled ? 70u : 71u);
        return 0;
    }
    if (att_handle == ATT_CHARACTERISTIC_46540002_4654_4541_4C4B_000000000002_01_VALUE_HANDLE)
    {
        if (buffer_size > 0U)
        {
            s_ft_cmd = buffer[0];
        }
        feathertalk_ipc_send_event(72);
        return 0;
    }
    return 0;
}

static void ft_notify_timer_handler(struct btstack_timer_source *ts)
{
    if (s_bt_state == BT_READY && s_desired_on && s_notify_enabled)
    {
        att_server_request_can_send_now_event(s_gatt_con_handle);
    }
    btstack_run_loop_set_timer(ts, 2000);
    btstack_run_loop_add_timer(ts);
}

/* 广播保活: 连接尝试失败(未建立)时, 控制器会停广播且不产生断开事件,
 * 造成"广播假死、扫不到"。每 3s 若未连接且未在广播, 重新使能。
 * (对已建立连接无影响; 重发 enable 是幂等的) */
static btstack_timer_source_t s_adv_keepalive_timer;
static void ft_adv_keepalive_handler(struct btstack_timer_source *ts)
{
    if ((s_bt_state == BT_READY) && s_desired_on && !s_gatt_connected)
    {
        /* 控制器若已在广播, 此命令幂等; 若假死则救活 */
        hci_send_cmd(&hci_le_set_advertise_enable, 1);
    }
    btstack_run_loop_set_timer(ts, 3000);
    btstack_run_loop_add_timer(ts);
}

/* 广播三步: btstack 的 hci_send_cmd 在"上一命令在飞"时直接返回
 * ERROR_CODE_COMMAND_DISALLOWED 并丢弃命令 (实测: 连发三条只完成第一条),
 * 必须等上一条 CC 再发下一条 */
static void bt_adv_start(void)
{
    bd_addr_t null_addr = { 0, 0, 0, 0, 0, 0 };
    g_adv_cc_status[ADV_CC_PARAMS] = 0xFF;
    g_adv_cc_status[ADV_CC_DATA] = 0xFF;
    g_adv_cc_status[ADV_CC_ENABLE] = 0xFF;
    /* 本版本模板为 "22111B11": interval(2) ×2 + type(1) ×3 +
     * direct_addr(B) + channel_map(1) + filter(1) */
    hci_send_cmd(&hci_le_set_advertising_parameters,
                 0x0030, 0x0030, 0, 0, 0, null_addr, 0x07, 0);
    BT_CP(61);
}

static void bt_adv_set_data(void)
{
    uint8_t adv[31];
    uint8_t pos = 0;
    /* M4b: 双模音箱 flags 必须是 0x02 (LE 可发现 + 支持经典蓝牙)。
     * 原来写 0x06 (含 "BR/EDR Not Supported" bit) 会让 Windows 把我们当纯 BLE
     * 设备配对, 从不做经典 SDP 服务发现 -> 建不出 A2DP 音频端点 (实测)。 */
    adv[pos++] = 2;  adv[pos++] = 0x01;  adv[pos++] = 0x02;   /* flags: LE general + BR/EDR supported */
    adv[pos++] = 12;                                          /* len(1 type + 11 chars) */
    adv[pos++] = 0x09;                                        /* complete name */
    memcpy(&adv[pos], "FeatherTalk", 11);
    pos += 11;
    hci_send_cmd(&hci_le_set_advertising_data, pos, adv);
    BT_CP(62);
}

static void bt_adv_enable(void)
{
    hci_send_cmd(&hci_le_set_advertise_enable, 1);
    BT_CP(63);
}

/* HCI 包处理器: 状态机到达 WORKING 即启动 BLE 广播 */
static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    UNUSED(channel);
    UNUSED(size);
    if (packet_type != HCI_EVENT_PACKET) return;

    /* 全事件记录: 定位"连接事件到底来没来" (GDB 可读) */
    bt_evt_log(0xE000U | packet[0],
               (packet[0] == 0x3EU && size > 2U) ? packet[2] : 0xEEEEU);

    switch (hci_event_packet_get_type(packet))
    {
    case BTSTACK_EVENT_STATE:
        bt_evt_log(0xFFFF, btstack_event_state_get_state(packet));
        if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING && s_desired_on)
        {
            bd_addr_t local_addr;
            gap_local_bd_addr(local_addr);
            rt_kprintf("[BT] HCI WORKING, addr %02x:%02x:%02x:%02x:%02x:%02x\n",
                       local_addr[0], local_addr[1], local_addr[2],
                       local_addr[3], local_addr[4], local_addr[5]);
            feathertalk_ipc_send_event(40);
            BT_CP(60);
            bt_adv_start();
            /* M4a: Classic 可见+可连 (与 LE 广播并存, 双模标准做法)。
             * 这些是 gap task 型 API, 经 hci_run 排队发送, 不与上面的
             * adv 串行链冲突 */
            gap_set_class_of_device(0x240418U);  /* Audio/Rendering + Loudspeaker */
            gap_set_local_name("FeatherTalk");
            gap_discoverable_control(1);
            gap_connectable_control(1);
        }
        break;
    case HCI_EVENT_COMMAND_COMPLETE:
    {
        /* 布局: [0]=0x0E [1]=len [2]=num [3..4]=opcode [5]=status */
        uint16_t opcode = little_endian_read_16(packet, 3);
        uint8_t status = (size > 5) ? packet[5] : 0xFE;
        g_bt_coex_diag.command_completes++;
        g_bt_coex_diag.last_complete_ms = rt_tick_get_millisecond();
        rt_kprintf("[BT] cmd complete (opcode %04x) status %u\n", opcode, status);
        bt_evt_log(opcode, status);
        if (opcode == 0x2006)
        {
            g_adv_cc_status[ADV_CC_PARAMS] = status;
            if (status == 0U && s_desired_on && s_bt_state != BT_STOPPING) bt_adv_set_data();
        }
        else if (opcode == 0x2008)
        {
            g_adv_cc_status[ADV_CC_DATA] = status;
            if (status == 0U && s_desired_on && s_bt_state != BT_STOPPING) bt_adv_enable();
        }
        else if (opcode == 0x200A)
        {
            if (status == 0U) g_bt_coex_diag.advertising_acks++;
            g_adv_cc_status[ADV_CC_ENABLE] = status;
            /* 保活定时器每 3s 会重发 enable, 只在"进入广播态"的边沿上报,
             * 否则控制台/IPC 每 3s 被刷屏 */
            if ((status == 0U) && s_desired_on && (s_bt_state == BT_STARTING))
            {
                s_bt_state = 2;
                s_bt_starting = RT_FALSE;
                ft_radio_set_state(FT_RADIO_BT, FT_RADIO_READY, 0);
                rt_kprintf("[BT] BLE advertising as FeatherTalk\n");
                feathertalk_ipc_send_event(41);
            }
        }
        break;
    }
    case HCI_EVENT_COMMAND_STATUS:
        bt_evt_log(0xFF0F, (uint16_t)((size > 1) ? packet[2] : 0xFD));
        break;
    case HCI_EVENT_LE_META:
        if (size >= 6 && packet[3] == 0 &&
            hci_event_le_meta_get_subevent_code(packet) == HCI_SUBEVENT_LE_CONNECTION_COMPLETE)
        {
            /* 布局: [0]=0x3E [1]=len [2]=subevent [3]=status [4..5]=handle LE16 */
            s_gatt_con_handle = little_endian_read_16(packet, 4);
            s_gatt_connected = 1;
            feathertalk_ipc_send_event(60);
        }
        break;
    case GAP_EVENT_INQUIRY_RESULT:
    {
        bd_addr_t inq_addr;
        gap_event_inquiry_result_get_bd_addr(packet, inq_addr);
        rt_kprintf("[BT] inquiry found %s CoD=0x%06x rssi=%d\n",
                   bd_addr_to_str(inq_addr),
                   (unsigned)gap_event_inquiry_result_get_class_of_device(packet),
                   (int)gap_event_inquiry_result_get_rssi(packet));
        break;
    }
    case GAP_EVENT_INQUIRY_COMPLETE:
        rt_kprintf("[BT] inquiry done\n");
        break;
    case HCI_EVENT_CONNECTION_COMPLETE:
        /* Classic ACL 连接建立 (LE 走 LE_META, 不走这里) */
        if ((size > 11U) && (packet[2] == 0U) && (packet[11] == 1U))
        {
            s_classic_handle = little_endian_read_16(packet, 3);
            s_classic_connected = 1;
            rt_kprintf("[BT] Classic connected (handle 0x%x)\n", s_classic_handle);
            feathertalk_ipc_send_event(62);
        }
        break;
    case HCI_EVENT_SIMPLE_PAIRING_COMPLETE:
        /* 配对失败(对端超时/拒绝)时主动断开: 否则链路挂死占据连接池,
         * 后续配对重试全部被陈旧链路顶掉 (实测: PC 配对超时后链路挂 sniff,
         * 重试时新连接建不起来, rc=31) */
        if ((size > 2U) && (packet[2] != 0U))
        {
            rt_kprintf("[BT] pairing failed (status 0x%x), disconnect link\n",
                       packet[2]);
            if (s_classic_connected)
            {
                gap_disconnect(s_classic_handle);
            }
        }
        break;
    case HCI_EVENT_LINK_KEY_NOTIFICATION:
        /* 配对成功, link key 已由 hci 层自动存入内存库 */
        rt_kprintf("[BT] link key stored (paired)\n");
        feathertalk_ipc_send_event(63);
        break;
    case HCI_EVENT_DISCONNECTION_COMPLETE:
    {
        uint16_t disc_handle = (size > 4U) ? little_endian_read_16(packet, 3) : 0;
        rt_kprintf("[BT] disconnect\n");
        if (disc_handle == s_classic_handle)
        {
            s_classic_connected = 0;
            rt_kprintf("[BT] Classic disconnected, back to discoverable\n");
            if (s_desired_on && s_bt_state == BT_READY) {
                gap_discoverable_control(1);
                gap_connectable_control(1);
            }
        }
        if (disc_handle == s_gatt_con_handle) {
            s_gatt_connected = 0;
            s_notify_enabled = 0;
        }
        feathertalk_ipc_send_event(61);
        if (s_desired_on && s_bt_state == BT_READY && !s_gatt_connected)
            bt_adv_start();   /* Never re-enable advertising during shutdown. */
        break;
    }
    case ATT_EVENT_CAN_SEND_NOW:
    {
        if (!s_desired_on || s_bt_state != BT_READY || !s_notify_enabled) break;
        uint8_t payload[8];
        uint16_t len = ft_ntf_build_payload(payload);
        att_server_notify(s_gatt_con_handle,
                          ATT_CHARACTERISTIC_46540003_4654_4541_4C4B_000000000003_01_VALUE_HANDLE,
                          payload, len);
        s_notify_counter++;
        break;
    }
    case HCI_EVENT_HARDWARE_ERROR:
        g_bt_coex_diag.hardware_errors++;
        s_bt_err = -RT_ERROR;
        ft_radio_set_state(FT_RADIO_BT, FT_RADIO_ERROR, -RT_ERROR);
        rt_kprintf("[BT] HARDWARE ERROR!\n");
        break;
    default:
        break;
    }
}

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

static int bt_bringup(void)
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
    hci_set_link_key_db(btstack_link_key_db_memory_instance());
    gap_ssp_set_io_capability(SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    gap_ssp_set_auto_accept(1);
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH |
                                         LM_LINK_POLICY_ENABLE_SNIFF_MODE);
    gap_set_allow_role_switch(true);

    /* M4b: A2DP Sink + AVRCP (SDP 记录/流端点/handler 注册, 纯静态登记,
     * 须在 hci_power_on 前完成) */
    if (bt_a2dp_sink_setup() != 0) {
        s_stack_initialized = RT_TRUE;
        s_stack_setup_failed = RT_TRUE;
        return -96; /* Partial profile setup must not be registered a second time. */
    }

    feathertalk_ipc_send_event(10);
    s_hci_event_handler.callback = &packet_handler;
    hci_add_event_handler(&s_hci_event_handler);
    feathertalk_ipc_send_event(11);
    BT_CP(53);

    /* M2: att_server (GAP+DIS+FT Data) */
    att_server_init(profile_data, att_read_callback, att_write_callback);
    att_server_register_packet_handler(packet_handler);
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

static int bt_stop_owned(void)
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

/* IPC can preempt btloop between testing the target and clearing its request.
 * Keep these tiny state publications atomic; never hold IRQs across HCI work. */
static void bt_settle_request(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    if ((!s_desired_on && s_bt_state == BT_OFF) ||
        (s_desired_on && s_bt_state == BT_READY))
        s_request_pending = RT_FALSE;
    rt_hw_interrupt_enable(level);
}

static int bt_take_start_request(void)
{
    int taken = 0;
    rt_base_t level = rt_hw_interrupt_disable();
    if (s_desired_on && s_request_pending &&
        (s_bt_state == BT_OFF || s_bt_state == BT_ERROR)) {
        s_request_pending = RT_FALSE;
        s_bt_starting = RT_TRUE;
        s_bt_state = BT_STARTING;
        taken = 1;
    }
    rt_hw_interrupt_enable(level);
    return taken;
}

static void bt_loop_thread_entry(void *param)
{
    (void)param;
    for (;;) {
        if (!s_desired_on && s_bt_state != BT_OFF) {
            if (bt_stop_owned() != RT_EOK) {
                /* Do not reinitialize a still-live stack after a failed halt. */
                s_request_pending = RT_FALSE;
                rt_sem_take(&s_control_wake, RT_WAITING_FOREVER);
                continue;
            }
            bt_settle_request();
        }
        if (bt_take_start_request()) {
            int rc = bt_bringup();
            uint32_t start = rt_tick_get_millisecond();
            while (!rc && s_desired_on && s_bt_state == BT_STARTING && !s_bt_err) {
                btstack_run_loop_embedded_execute_once();
                if (rt_tick_get_millisecond() - start > 15000U) rc = -RT_ETIMEOUT;
            }
            if (rc || s_bt_err || !s_desired_on) {
                int error = s_desired_on ? (rc ? rc : s_bt_err) : 0;
                int stopped = bt_stop_owned();
                s_bt_err = stopped ? stopped : error;
                s_bt_state = s_bt_err ? BT_ERROR : BT_OFF;
                if (s_bt_err) ft_radio_set_state(FT_RADIO_BT, FT_RADIO_ERROR, s_bt_err);
                continue;
            }
        }
        if (s_bt_state == BT_READY) {
            bt_settle_request();
            rt_base_t level = rt_hw_interrupt_disable();
            void (*callback)(void) = s_owner_callback;
            s_owner_callback = RT_NULL;
            rt_hw_interrupt_enable(level);
            if (callback && s_desired_on) callback();
            if (s_bt_err) {
                int error = s_bt_err;
                bt_stop_owned();
                s_bt_err = error; s_bt_state = BT_ERROR;
                ft_radio_set_state(FT_RADIO_BT, FT_RADIO_ERROR, error);
            } else btstack_run_loop_embedded_execute_once();
        } else {
            bt_settle_request();
            rt_sem_take(&s_control_wake, RT_WAITING_FOREVER);
        }
    }
}

static int bt_control_init(void)
{
    rt_mutex_init(&s_control_lock, "btctl", RT_IPC_FLAG_PRIO);
    rt_sem_init(&s_control_wake, "btctl", 0, RT_IPC_FLAG_PRIO);
    return RT_EOK;
}
INIT_COMPONENT_EXPORT(bt_control_init);

rt_err_t bt_service_set_enabled(int on)
{
    if (on != 0 && on != 1) return -RT_EINVAL;
    rt_mutex_take(&s_control_lock, RT_WAITING_FOREVER);
    if (on && (s_stack_setup_failed ||
        (s_stack_initialized && s_bt_state == BT_ERROR && hci_get_state() != HCI_STATE_OFF))) {
        rt_mutex_release(&s_control_lock);
        return -RT_EBUSY;
    }
    if (!s_loop_thread && on) {
        s_loop_thread = rt_thread_create("btloop", bt_loop_thread_entry, RT_NULL, 8192, 25, 10);
        if (!s_loop_thread) { rt_mutex_release(&s_control_lock); return -RT_ENOMEM; }
        rt_thread_startup(s_loop_thread);
    }
    /* Repeated target is idempotent; an opposite request during HCD cancels
     * at the next record boundary. No competing stack/UART owner is created. */
    rt_base_t level = rt_hw_interrupt_disable();
    int changed = s_desired_on != !!on || s_bt_state == BT_ERROR;
    if (changed) {
        s_desired_on = !!on;
        s_request_pending = RT_TRUE;
    }
    rt_hw_interrupt_enable(level);
    if (changed) rt_sem_release(&s_control_wake);
    rt_mutex_release(&s_control_lock);
    return RT_EOK;
}

/* Small owner-thread mailbox for existing shell diagnostics. */
int bt_service_run_callback(void (*callback)(void))
{
    rt_base_t level = rt_hw_interrupt_disable();
    int result = -RT_EBUSY;
    if (callback && s_desired_on && s_bt_state == BT_READY && !s_owner_callback) {
        s_owner_callback = callback;
        result = RT_EOK;
    }
    rt_hw_interrupt_enable(level);
    return result;
}

/* ---- 栈中立服务 API (bt_service_api.h, FEATHERTALK_BT_STACK_BK 实现) ---- */

rt_err_t bt_service_start(void)
{
    return bt_service_set_enabled(1);
}

int bt_service_enabled(void)
{
    return s_bt_state == BT_READY;
}

int bt_service_busy(void) { return s_request_pending || s_bt_state == BT_STARTING || s_bt_state == BT_STOPPING; }
int bt_service_target(void) { return s_desired_on; }

int bt_service_connected(void)
{
    return s_bt_state == BT_READY && (s_gatt_connected || s_classic_connected);
}

/* IPC 快捷控制入口: value 1=on, 0=off */
int bt_service_quick_control(uint8_t on)
{
    return bt_service_set_enabled(on);
}

int bt_service_error(void)
{
    return s_bt_err;
}

static int bt_on(int argc, char **argv)
{
    return bt_service_start();
}
MSH_CMD_EXPORT_ALIAS(bt_on, bt_on, power on CYW55512 BT: HCD download + BLE adv);

static int bt_off(int argc, char **argv)
{
    return bt_service_set_enabled(0);
}
MSH_CMD_EXPORT_ALIAS(bt_off, bt_off, power off CYW55512 BT);

static int bt_info(int argc, char **argv)
{
    rt_kprintf("[BT] state=%d (0=off,1=starting,2=ready,3=stopping,4=error), uart=%s\n",
               s_bt_state, FEATHERTALK_BT_UART_DEV);
    rt_kprintf("[BT] target=%d busy=%d error=%d HCI=%d\n", s_desired_on,
               bt_service_busy(), s_bt_err, s_stack_initialized ? (int)hci_get_state() : -1);
    rt_kprintf("[BT] connected=%d notify=%d counter=%u\n",
               s_gatt_connected, s_notify_enabled, s_notify_counter);
    rt_kprintf("[BT] classic=%d handle=0x%x\n",
               s_classic_connected, (unsigned)s_classic_handle);
    return 0;
}
MSH_CMD_EXPORT_ALIAS(bt_info, bt_info, show btstack/HCI status);

/* 诊断: 裸路径直发 HCI_Reset, 测试控制器此刻是否应答 (绕过 btstack/h4) */
static int bt_raw_reset(int argc, char **argv)
{
    extern int bt_hcd_send_raw_command(uint16_t opcode, uint8_t plen, const uint8_t *params);
    (void)argc; (void)argv;
    /* A raw command would race H4 and consume its replies. */
    int rc = -RT_EBUSY;
    rt_kprintf("[BT] raw reset rejected (%d): use bt_off/bt_on through the HCI owner\n", rc);
    return 0;
}
MSH_CMD_EXPORT_ALIAS(bt_raw_reset, bt_raw_reset, diag: raw reset is blocked while service owns UART);

/* 诊断: Classic inquiry ~6s, 列出发现的经典蓝牙设备 (验证 BR/EDR 射频通路) */
static void bt_inquiry_owned(void)
{
    gap_inquiry_start(5);
}
static int bt_inq(int argc, char **argv)
{
    (void)argc; (void)argv;
    int rc = bt_service_run_callback(bt_inquiry_owned);
    rt_kprintf("[BT] inquiry queued rc=%d\n", rc);
    return rc;
}
MSH_CMD_EXPORT_ALIAS(bt_inq, bt_inq, diag: classic inquiry scan ~6s);

/* 诊断: 查 btstack 连接登记簿里有没有这个句柄 (定位 non-registered handle) */
static int bt_find(int argc, char **argv)
{
    unsigned h = 64;
    if (argc > 1) h = (unsigned)strtoul(argv[1], NULL, 0);
    hci_connection_t *conn = hci_connection_for_handle((hci_con_handle_t)h);
    rt_kprintf("[BT] find handle 0x%x -> %s (conn=%p, state=%d)\n",
               h, conn ? "REGISTERED" : "NOT FOUND", (void*)conn,
               conn ? (int)conn->state : -1);
    return 0;
}
MSH_CMD_EXPORT_ALIAS(bt_find, bt_find, diag: check if a con handle is registered in btstack);
