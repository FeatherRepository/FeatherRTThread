/* bt_diag.c - 蓝牙诊断 (SWD 计数器/事件环/HCI 抓包环/dump 过滤)
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

/* 崩溃现场存活检查点: 写在静态变量里, 即使整机死锁也能用 GDB 读出最后一步 */
volatile uint32_t g_bt_checkpoint;
/* Persistent evidence: connects, disconnects, last reason, pair completions,
 * pair status/reason, last dynamic ATT handle, last write opcode. */
volatile uint32_t g_le_connect_diag[8];

/* Passive coexistence diagnostics. Count replies to EXISTING HCI traffic;
 * do not send resets/probes, allocate memory, or add periodic IPC traffic.
 * Read via the M33 debugger even when its serial connector is unplugged. */
volatile ft_bt_coex_diag_t g_bt_coex_diag;    /* typedef 见 bt_internal.h */

/* 三条广播命令的 Command Complete 状态 (0xFF=未收到), GDB 可读 */
volatile uint8_t g_adv_cc_status[3] = { 0xFF, 0xFF, 0xFF };

/* 事件环: 最近 12 个事件的 [type, opcode/status], GDB 可读 */
volatile uint16_t g_bt_evt_log[12][2];
volatile uint8_t  g_bt_evt_log_pos;
void bt_evt_log(uint16_t a, uint16_t b)
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
/* 抓包减负 (A2DP 媒体流必需): 3M HCI + ~45KB/s 媒体流下, 全量 ACL 十六进制
 * dump 远超 115200 控制台带宽, rt_kprintf 同步发送会反过来拖死 btloop。
 * 过滤包装: 丢 ACL 数据帧与高频 NOP(Number Of Completed Packets, 0x13)事件,
 * 其余事件/日志原样保留 (排障能力不降)。 */
const hci_dump_t *s_dump_stdout;   /* bt_bringup 赋值实例 */
/* Read-only debugger evidence, excludes SMP keys and audio payloads.
 * row: timestamp, type, direction, original length, captured length, data[56].
 * Commit count last so a debugger can retry a concurrent snapshot. */
volatile uint32_t g_le_trace_count;
volatile struct {
    uint32_t ms;
    uint8_t type, incoming, length, captured;
    uint8_t data[56];
} g_le_trace[64];
static void ft_trace_packet(uint8_t type, uint8_t incoming,
                            const uint8_t *packet, uint16_t length)
{
    uint16_t capture = 0;
    if (type == HCI_ACL_DATA_PACKET && length >= 9 &&
        ((little_endian_read_16(packet, 0) >> 12) & 3) != 1) {
        uint16_t cid = little_endian_read_16(packet, 6);
        if (cid == 4) capture = length; /* ATT */
        if (cid == 6) capture = 9;      /* SMP: opcode only, never keys */
    } else if (type == HCI_EVENT_PACKET && length >= 2) {
        if (packet[0] == 3 || packet[0] == 4 || packet[0] == 5 ||
            packet[0] == 8 || packet[0] == 0x30 ||
            packet[0] == 0x3e) capture = length;
    }
    if (!capture) return;
    if (capture > 56) capture = 56;
    uint32_t i = g_le_trace_count % 64;
    g_le_trace[i].ms = rt_tick_get_millisecond();
    g_le_trace[i].type = type; g_le_trace[i].incoming = incoming;
    g_le_trace[i].length = length > 255 ? 255 : length;
    g_le_trace[i].captured = capture;
    for (uint16_t j = 0; j < capture; j++) g_le_trace[i].data[j] = packet[j];
    __DMB();
    g_le_trace_count++;
}
static void ft_dump_log_packet(uint8_t packet_type, uint8_t in,
                               uint8_t *packet, uint16_t len)
{
    ft_trace_packet(packet_type, in, packet, len);
    /* ISO media can exceed 80 KB/s as hex text. Blocking 115200-baud
     * console output throttles H4 reception and loses audio SDUs. */
    if (packet_type == HCI_ACL_DATA_PACKET || packet_type == HCI_ISO_DATA_PACKET)
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
const hci_dump_t s_dump_filtered = {
    .reset = RT_NULL,
    .log_packet = ft_dump_log_packet,
    .log_message = ft_dump_log_message,
};
/* LE Audio 专用诊断环 (SWD 读): 手机双模下不建 LE 音频链路时,
 * 按此序列定位断点 —— LE 连接 -> 加密 -> 配对 -> MTU/GATT 探测 */
volatile uint16_t g_leaud_log[16][2];
volatile uint8_t  g_leaud_log_pos;
void leaud_log(uint16_t a, uint16_t b)
{
    uint8_t i = g_leaud_log_pos % 16U;
    g_leaud_log[i][0] = a;
    g_leaud_log[i][1] = b;
    g_leaud_log_pos++;
}
