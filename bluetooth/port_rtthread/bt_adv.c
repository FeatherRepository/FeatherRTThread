/* bt_adv.c - 广播与通道管理 (LE 双广播集/经典 EIR/三步广播/保活/三态切换)
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

/* 广播保活: 连接尝试失败(未建立)时, 控制器会停广播且不产生断开事件,
 * 造成"广播假死、扫不到"。每 3s 若未连接且未在广播, 重新使能。
 * (对已建立连接无影响; 重发 enable 是幂等的)
 * M8.1 自愈扩展: 真 SMP 引入后, sm_event_packet_handler 在 WORKING 边沿
 * 经 btstack_crypto 发 IR/ER 随机数 (LE_Rand), 命令在飞时广播链内的
 * 下一条 hci_send_cmd 被 DISALLOWED 丢弃 (0x2008 实测丢失 -> 15s 启动
 * 超时)。此处按 CC 记录补发丢失的一步, 广播链从此可自愈。 */
btstack_timer_source_t s_adv_keepalive_timer;
/* M8.x 音频通道模式 (用户可在 UI 直接选择发 BLE 还是经典):
 * 0=双模 (两套发现机制都开, 默认)  1=仅 LE (只发 LE 广播, 经典扫描关)
 * 2=仅经典 (LE 可连接广播停, 经典扫描开; 广播源 handle3 不受影响,
 *   转发场景 —— A2DP 收流 + Auracast 出局 —— 在经典模式下照常可用)。
 * 原 LE 优先配对模式 = 仅 LE 模式的复用 (HyperOS 配对判型需要)。 */
/* 开机默认经典 A2DP (用户设定): 只开经典扫描, LE 可连接广播不发 */
int s_audio_transport = FT_TRANSPORT_DUAL;   /* 开机默认双模 (A 方案: 两种发现机制都开) */
int bt_service_le_pairing_mode(void)
{
    return s_audio_transport == FT_TRANSPORT_LE_ONLY;
}
int bt_service_audio_transport(void)
{
    return s_audio_transport;
}
void ft_adv_keepalive_handler(struct btstack_timer_source *ts)
{
    if (s_desired_on && s_bt_state == BT_STARTING)
    {
        if (g_adv_cc_status[ADV_CC_PARAMS] == 0U &&
            g_adv_cc_status[ADV_CC_DATA] == 0xFFU)
        {
            bt_adv_set_data();   /* data 步被丢弃: 幂等重发 */
        }
        else if (g_adv_cc_status[ADV_CC_DATA] == 0U &&
                 g_adv_cc_status[ADV_CC_ENABLE] == 0xFFU)
        {
            bt_adv_enable();     /* enable 步被丢弃: 幂等重发 */
        }
    }
    if ((s_bt_state == BT_READY) && s_desired_on && !s_gatt_connected &&
        s_audio_transport != FT_TRANSPORT_CLASSIC_ONLY)
    {
        /* 控制器若已在广播, 此命令幂等; 若假死则救活。
         * 仅经典模式: LE 可连接广播被用户关闭, 保活不得复活它 */
        bt_adv_enable();
    }
    btstack_run_loop_set_timer(ts, 3000);
    btstack_run_loop_add_timer(ts);
}

/* 广播三步: btstack 的 hci_send_cmd 在"上一命令在飞"时直接返回
 * ERROR_CODE_COMMAND_DISALLOWED 并丢弃命令 (实测: 连发三条只完成第一条),
 * 必须等上一条 CC 再发下一条 */
/* M8.1: Classic EIR (240B, gap_set_extended_inquiry_response 不拷贝,
 * 必须常驻内存): 名字 + 16-bit 服务 UUID (CAS/ASCS/A2DP/AVRCP)。
 * 手机配对时读它判定对端 LE Audio 能力 */
static uint8_t s_classic_eir[240];

void bt_classic_eir_setup(void)
{
    uint8_t pos = 0;
    memset(s_classic_eir, 0, sizeof(s_classic_eir));
    s_classic_eir[pos++] = 12;                               /* len = type + 11 chars */
    s_classic_eir[pos++] = 0x09;                             /* complete local name */
    memcpy(&s_classic_eir[pos], "FeatherTalk", 11);
    pos += 11;
    s_classic_eir[pos++] = 9;                                /* len = type + 4 UUIDs */
    s_classic_eir[pos++] = 0x03;                             /* complete list of 16-bit service UUIDs */
    s_classic_eir[pos++] = 0x4E;  s_classic_eir[pos++] = 0x18;  /* ASCS  0x184E */
    s_classic_eir[pos++] = 0x53;  s_classic_eir[pos++] = 0x18;  /* CAS   0x1853 */
    s_classic_eir[pos++] = 0x0B;  s_classic_eir[pos++] = 0x11;  /* A2DP Sink 0x110B */
    s_classic_eir[pos++] = 0x0C;  s_classic_eir[pos++] = 0x11;  /* AVRCP Target 0x110C */
    gap_set_extended_inquiry_response(s_classic_eir);
}

static le_advertising_set_t s_le_adv;
uint8_t s_le_adv_handle;
static uint8_t s_le_adv_registered;
/* A second BLE advertising set supplies scan-response-compatible discovery.
 * Both sets use HCI extended commands; BR/EDR remains disabled in LE-only mode. */
static le_advertising_set_t s_le_discovery_adv;
uint8_t s_le_discovery_handle;
uint8_t s_le_discovery_registered;
static const le_extended_advertising_parameters_t s_le_discovery_params = {
    .advertising_event_properties = 0x13, /* BLE legacy connectable/scannable */
    .primary_advertising_interval_min = 160,
    .primary_advertising_interval_max = 192,
    .primary_advertising_channel_map = 7,
    .own_address_type = BD_ADDR_TYPE_LE_PUBLIC,
    .primary_advertising_phy = 1,
    .secondary_advertising_phy = 1,
    .advertising_sid = 1,
};
static uint8_t s_le_discovery_data[] = {
#if FEATHERTALK_BT_LE_AUDIO_ONLY
    2, 0x01, 0x06,
#else
    2, 0x01, 0x02,
#endif
    15, 0x09, 'F','e','a','t','h','e','r','T','a','l','k','-','L','E',
    11, 0x03, 0x50, 0x18, 0x4e, 0x18, 0x53, 0x18,
    0x44, 0x18, 0x55, 0x18
};
static const le_extended_advertising_parameters_t s_le_adv_params = {
    .advertising_event_properties = 1,
    .primary_advertising_interval_min = 160,
    .primary_advertising_interval_max = 192,
    .primary_advertising_channel_map = 7,
    .own_address_type = BD_ADDR_TYPE_LE_PUBLIC,
    .advertising_tx_power = 0,
    .primary_advertising_phy = 1,
    .secondary_advertising_phy = 1,
};
/* BAP Unicast Server General Announcement, CAP Acceptor, TMAS UMR.
 * Persistent storage is required by the asynchronous GAP advertising API. */
static uint8_t s_le_adv_data[] = {
#if FEATHERTALK_BT_LE_AUDIO_ONLY
    2, 0x01, 0x06,
#else
    2, 0x01, 0x02, /* LE discoverable, BR/EDR supported */
#endif
    15, 0x09, 'F','e','a','t','h','e','r','T','a','l','k','-','L','E',
    /* UUID list supports OS service filters in addition to announcements. */
    11, 0x03, 0x50, 0x18, 0x4e, 0x18, 0x53, 0x18,
    0x44, 0x18, 0x55, 0x18,
    9, 0x16, 0x4e, 0x18, 0, 7, 0, 0, 0, 0,
    4, 0x16, 0x53, 0x18, 0,
    5, 0x16, 0x55, 0x18, 8, 0,
    /* M8.x: Fast Pair Model ID 服务数据 (0xFE2C) —— 触发手机主动连 LE
     * (seeker 连入读 Model ID, 顺带完成 GATT 发现, ASCS 入缓存)。
     * 未注册调试 Model ID 0x544654, 与 ft_gatt.gatt 的 FP 服务一致 */
    7, 0x16, 0x2c, 0xfe, 0x54, 0x46, 0x54,
};

/* M8.x: LE 优先配对模式开关 (经 IPC QUICK_BT_LE_ROLE value=3 驱动)。
 * on: 广播 flags 改 0x06 + 关经典可发现/可连 + 断开既有经典 ACL,
 *     手机只能以 LE 配对本机 (发现 ASCS, 建立 LE 音频关系)。
 * off: 恢复双模形态。 */
/* M8.x 音频通道切换 (UI 音频通道行 / IPC 驱动)。仅 READY 态可切
 * (避免与启动期广播链竞态); 模式为运行态, 重启回双模。 */
int bt_service_set_audio_transport(int mode)
{
    if (mode < FT_TRANSPORT_DUAL || mode > FT_TRANSPORT_CLASSIC_ONLY) return -RT_EINVAL;
    if (s_audio_transport == mode) return RT_EOK;
    if (s_bt_state != BT_READY) return -RT_EBUSY;
    s_audio_transport = mode;
    s_le_adv_data[2]       = (mode == FT_TRANSPORT_LE_ONLY) ? 0x06 : 0x02;
    s_le_discovery_data[2] = s_le_adv_data[2];
    if (mode == FT_TRANSPORT_LE_ONLY)
    {
        /* 仅 LE: 经典扫描关, 断既有经典 ACL; LE 广播带 0x06 flags 重发
         * (从开机经典默认态切来时广播集未注册, bt_adv_start 会注册) */
        gap_discoverable_control(0);
        gap_connectable_control(0);
        if (s_classic_connected && s_classic_handle != HCI_CON_HANDLE_INVALID)
        {
            gap_disconnect(s_classic_handle);
        }
        if (!s_le_adv_registered || !s_le_discovery_registered)
        {
            bt_adv_start();
        }
        bt_adv_set_data();
    }
    else if (mode == FT_TRANSPORT_CLASSIC_ONLY)
    {
        /* 仅经典: LE 可连接广播停 (广播源 handle3 不在管辖, 转发可用),
         * LE 链路断开 —— 手机重连时将发现 LE 不可连 */
        gap_discoverable_control(1);
        gap_connectable_control(1);
        if (s_gatt_connected && s_gatt_con_handle != HCI_CON_HANDLE_INVALID)
        {
            gap_disconnect(s_gatt_con_handle);
        }
        if (s_le_adv_registered)
        {
            (void)gap_extended_advertising_stop(s_le_adv_handle);
        }
        if (s_le_discovery_registered)
        {
            (void)gap_extended_advertising_stop(s_le_discovery_handle);
        }
    }
    else
    {
        /* 双模: 两套发现机制都开 (从仅经典切出时广播集未注册, 先注册) */
        gap_discoverable_control(1);
        gap_connectable_control(1);
        if (!s_le_adv_registered || !s_le_discovery_registered)
        {
            bt_adv_start();
        }
        bt_adv_set_data();
    }
    if (mode != FT_TRANSPORT_CLASSIC_ONLY && s_le_discovery_registered)
    {
        (void)gap_extended_advertising_set_adv_data(s_le_discovery_handle,
            sizeof(s_le_discovery_data), s_le_discovery_data);
    }
    rt_kprintf("[BT] audio transport -> %s\n",
               mode == FT_TRANSPORT_LE_ONLY ? "LE_ONLY" :
               mode == FT_TRANSPORT_CLASSIC_ONLY ? "CLASSIC_ONLY" : "DUAL");
    feathertalk_ipc_send_event(86);
    return RT_EOK;
}

int bt_service_set_le_pairing_mode(int on)
{
    return bt_service_set_audio_transport(on ? FT_TRANSPORT_LE_ONLY : FT_TRANSPORT_DUAL);
}
void bt_adv_enable(void)
{
    int rc = gap_extended_advertising_start(s_le_adv_handle, 0, 0);
    if (rc) { s_bt_err = rc; rt_kprintf("[BT] extended adv start rc=%d\n", rc); }
    if (s_le_discovery_registered) {
        rc = gap_extended_advertising_start(s_le_discovery_handle, 0, 0);
        if (rc) s_bt_err = rc;
    }
    BT_CP(63);
}
void bt_adv_set_data(void)
{
    int rc = gap_extended_advertising_set_adv_data(s_le_adv_handle,
                  sizeof(s_le_adv_data), s_le_adv_data);
    if (rc) { s_bt_err = rc; return; }
    bt_adv_enable();
}
void bt_adv_start(void)
{
    if (!s_le_adv_registered) {
        int rc = gap_extended_advertising_setup(&s_le_adv, &s_le_adv_params,
                                                 &s_le_adv_handle);
        if (rc) { s_bt_err = rc; return; }
        s_le_adv_registered = 1;
    }
    if (!s_le_discovery_registered) {
        int rc = gap_extended_advertising_setup(&s_le_discovery_adv,
            &s_le_discovery_params, &s_le_discovery_handle);
        if (rc) { s_bt_err = rc; return; }
        s_le_discovery_registered = 1;
        rc = gap_extended_advertising_set_adv_data(s_le_discovery_handle,
            sizeof(s_le_discovery_data), s_le_discovery_data);
        if (rc) { s_bt_err = rc; return; }
    }
    bt_adv_set_data();
}
