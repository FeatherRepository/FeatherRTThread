/* bt_gatt_ft.c - GATT 服务 (att 回调/FT Data/notify/SMP/身份解析卫生)
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

/* FT Data CCCD 使能值 (Bluetooth 规范) */
#define FT_GATT_CCCD_NOTIFICATION   0x0001U
/* ---- GATT 服务状态 (att_server, M2) ---- */
hci_con_handle_t s_gatt_con_handle;
volatile int   s_gatt_connected;
/* M4a: Classic ACL 链路状态 (与 LE 连接互不排斥, 双模并存) */
hci_con_handle_t s_classic_handle;
volatile int   s_classic_connected;
volatile int   s_notify_enabled;
btstack_timer_source_t s_notify_timer;
uint16_t       s_notify_counter;
uint8_t        s_ft_cmd;      /* 最近一条 FT CMD 命令字节 (供回读) */

/* NTF 状态帧: [cnt_lo][cnt_hi][bt_state][connected][err][last_cmd] */
uint16_t ft_ntf_build_payload(uint8_t *out)
{
    out[0] = (uint8_t)(s_notify_counter & 0xFFU);
    out[1] = (uint8_t)(s_notify_counter >> 8);
    out[2] = (uint8_t)s_bt_state;
    out[3] = (uint8_t)s_gatt_connected;
    out[4] = (uint8_t)((s_bt_err < 0) ? 0xFF : s_bt_err);
    out[5] = s_ft_cmd;
    return 6;
}

uint16_t att_read_callback(hci_con_handle_t con_handle, uint16_t att_handle,
                                  uint16_t offset, uint8_t *buffer, uint16_t buffer_size)
{
    (void)con_handle;
    g_le_connect_diag[6] = att_handle;
    /* M8.x: Database Hash (0x2B2A) 读路由 (Robust Caching) */
    {
        extern uint16_t ft_gatt_hash_read(uint16_t offset, uint8_t *buffer, uint16_t buffer_size);
        if (att_handle == ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_DATABASE_HASH_01_VALUE_HANDLE)
        {
            return ft_gatt_hash_read(offset, buffer, buffer_size);
        }
    }
    /* M8.1: LE Audio ASE 状态读取先行路由 (0xFFFF = 非本模块句柄) */
    {
        uint16_t le_rc = ft_le_audio_att_read(att_handle, offset, buffer, buffer_size);
        if (le_rc != 0xFFFFU)
        {
            return le_rc;
        }
    }
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

int att_write_callback(hci_con_handle_t con_handle, uint16_t att_handle,
                              uint16_t transaction_mode, uint16_t offset,
                              uint8_t *buffer, uint16_t buffer_size)
{
    (void)transaction_mode;
    (void)offset;
    g_le_connect_diag[6] = att_handle;
    g_le_connect_diag[7] = buffer_size ? buffer[0] : 0xFFFFFFFFU;
    /* M8.1: LE Audio 控制点 / ASE CCCD 先行路由 (-2 = 非本模块句柄) */
    {
        int le_rc = ft_le_audio_att_write(con_handle, att_handle, buffer, buffer_size);
        if (le_rc != -2)
        {
            return le_rc;
        }
    }
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

void ft_notify_timer_handler(struct btstack_timer_source *ts)
{
    if (s_bt_state == BT_READY && s_desired_on && s_notify_enabled)
    {
        att_server_request_can_send_now_event(s_gatt_con_handle);
    }
    btstack_run_loop_set_timer(ts, 2000);
    btstack_run_loop_add_timer(ts);
}
btstack_packet_callback_registration_t s_sm_event_reg;

/* Bond-side hygiene: when the board loses its bond DB (e.g. after a reflash)
 * a still-bonded phone reconnects with an RPA, identity resolving fails, and
 * the peer keeps requesting encryption with LTKs we no longer have
 * (LTK Request -> IRK Lookup Failed) until it gives up on its own. First-time
 * pairing is unaffected (the peer sends a Pairing Request, which cancels the
 * timer via SM_EVENT_PAIRING_STARTED).
 * M8.x 双模: 本定时器会踢掉手机的全部 LE 连接 (RPA 解析失败在 LE 库为空时
 * 必然发生), 加密链路永远建不起来 -> ASCS 无法入缓存 -> LE 音频开关不出现。
 * 双模期间停用主动断链, 仅保留日志标记; LE bond 持久化落地后再评估恢复。 */
#define FT_IR_FAIL_DISCONNECT_MS 5000U
#define FT_IR_FAIL_DROP_ENABLE 0
static btstack_timer_source_t s_ir_fail_timer;
static hci_con_handle_t s_ir_fail_handle = HCI_CON_HANDLE_INVALID;

static void ft_ir_fail_disconnect(struct btstack_timer_source *ts)
{
    hci_con_handle_t handle = s_ir_fail_handle;
    s_ir_fail_handle = HCI_CON_HANDLE_INVALID;
    (void)ts;
#if FT_IR_FAIL_DROP_ENABLE
    if (handle != HCI_CON_HANDLE_INVALID &&
        s_gatt_connected && s_gatt_con_handle == handle)
    {
        rt_kprintf("[BT] identity unresolved, drop stale bonded link (handle 0x%x)\n",
                   handle);
        gap_disconnect(handle);
    }
#else
    if (handle != HCI_CON_HANDLE_INVALID)
    {
        rt_kprintf("[BT] identity unresolved on handle 0x%x (keep, dual-mode)\n",
                   handle);
    }
#endif
}

static void ft_ir_fail_arm(hci_con_handle_t handle)
{
    s_ir_fail_handle = handle;
    s_ir_fail_timer.process = ft_ir_fail_disconnect;
    btstack_run_loop_remove_timer(&s_ir_fail_timer);
    btstack_run_loop_set_timer(&s_ir_fail_timer, FT_IR_FAIL_DISCONNECT_MS);
    btstack_run_loop_add_timer(&s_ir_fail_timer);
}

void ft_ir_fail_cancel(void)
{
    s_ir_fail_handle = HCI_CON_HANDLE_INVALID;
    btstack_run_loop_remove_timer(&s_ir_fail_timer);
}

void ft_sm_handler(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size);
void ft_sm_handler(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    (void)channel;
    if (type != HCI_EVENT_PACKET || size < 2) return;
    /* LE 音频诊断: SM 事件序列 (a=0x5B00|SM 事件码, b=PAIRING_COMPLETE status) */
    leaud_log(0x5B00U | (packet[0] & 0xFFU),
              packet[0] == SM_EVENT_PAIRING_COMPLETE ? sm_event_pairing_complete_get_status(packet) : 0);
    if (packet[0] == SM_EVENT_JUST_WORKS_REQUEST)
        sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
    if (packet[0] == SM_EVENT_IDENTITY_RESOLVING_FAILED)
        ft_ir_fail_arm(sm_event_identity_resolving_failed_get_handle(packet));
    if (packet[0] == SM_EVENT_PAIRING_STARTED ||
        packet[0] == SM_EVENT_IDENTITY_CREATED ||
        packet[0] == SM_EVENT_PAIRING_COMPLETE)
        ft_ir_fail_cancel();
    if (packet[0] == SM_EVENT_PAIRING_COMPLETE) {
        g_le_connect_diag[3]++;
        g_le_connect_diag[4] = sm_event_pairing_complete_get_status(packet);
        g_le_connect_diag[5] = sm_event_pairing_complete_get_reason(packet);
        rt_kprintf("[BT] SMP pairing status=0x%02x reason=0x%02x\n",
            sm_event_pairing_complete_get_status(packet),
            sm_event_pairing_complete_get_reason(packet));
        /* M8.x: LE bond 持久化 —— 配对成功即发布 le_device_db 到共享块
         * (M55 落盘 /flash/bt_le_bond.bin), 重启/重烧后免重配 */
        if (sm_event_pairing_complete_get_status(packet) == 0U)
        {
            extern void ft_le_bond_publish(void);
            ft_le_bond_publish();
        }
    }
    if (packet[0] == SM_EVENT_IDENTITY_CREATED)
    {
        /* IRK/身份地址此时才最终入库, 再发布一次补全 */
        extern void ft_le_bond_publish(void);
        ft_le_bond_publish();
    }
}
