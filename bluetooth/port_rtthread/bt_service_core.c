/* bt_service_core.c - 蓝牙服务生命周期 (原 bt_main.c 拆分, 2026-10-24)
 * owner 线程/状态机/控制 API/A2DP 角色/msh/自动启动/复位原因捕获
 * bring-up 流程注释见 bt_bringup.c 文件头。内容零改动。 */
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
#include "bt_internal.h"

static rt_thread_t  s_loop_thread;
volatile int s_bt_state;
volatile int s_bt_err;        /* 0=无错误；非零为最近一次生命周期失败码 */
volatile rt_bool_t s_bt_starting;   /* bring-up 进行中, 防重复创建 btloop */
volatile rt_bool_t s_desired_on;
static volatile rt_bool_t s_request_pending;
rt_bool_t s_stack_initialized;
rt_bool_t s_stack_setup_failed;
void (*s_owner_callback)(void);
static struct rt_mutex s_control_lock;
static struct rt_semaphore s_control_wake;
/* One owner thread for UART, HCD, HCI, timers and profiles, for the lifetime
 * of the service. IPC/UI never call stack APIs or delete this thread. */
int bt_service_start_cancelled(void) { return !s_desired_on; }
btstack_packet_callback_registration_t s_hci_event_handler;
/* HCI 包处理器: 状态机到达 WORKING 即启动 BLE 广播 */
static void bt_iso_probe_print(const uint8_t *mask);
static volatile int s_iso_probe_pending;

/* M6-BT: A2DP 角色管理 (A2 单角色决策)。0=SINK 音箱(被动可连) 1=SOURCE 转发。
 * 切换语义: ->SOURCE 先挂断 Sink ACL (手机/PC 链路随 AVDTP 释放);
 *           ->SINK 断开 Source 链路并停泵 (M55 编码器让位, UAC 恢复) */
static int s_a2dp_role;
extern void bt_a2dp_source_drop(void);
int bt_service_enabled(void);
int bt_service_connected(void);
void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    UNUSED(channel);
    UNUSED(size);
    if (packet_type != HCI_EVENT_PACKET) return;

    /* 全事件记录: 定位"连接事件到底来没来" (GDB 可读) */
    bt_evt_log(0xE000U | packet[0],
               (packet[0] == 0x3EU && size > 2U) ? packet[2] : 0xEEEEU);

    /* LE 音频诊断: a=事件标识, b=状态/关键字节
     * 加密: [2]=status [3..4]=handle; 断开: [4]=reason */
    switch (packet[0])
    {
    case HCI_EVENT_ENCRYPTION_CHANGE:
        leaud_log(0x0008 | ((size > 5U && packet[5]) ? 0x8000U : 0U),
                  size > 2U ? packet[2] : 0xFEU);
        break;
    case HCI_EVENT_ENCRYPTION_KEY_REFRESH_COMPLETE:
        leaud_log(0x0030, size > 2U ? packet[2] : 0xFEU);
        break;
    case HCI_EVENT_DISCONNECTION_COMPLETE:
        /* 布局: [2]=status [3..4]=handle [5]=reason */
        leaud_log(0x0005, size > 5U ? packet[5] : 0xFEU);
        break;
    case ATT_EVENT_MTU_EXCHANGE_COMPLETE:
        leaud_log(0x00B5, 0);
        break;
    case HCI_EVENT_LE_META:
        if (size > 2U)
        {
            switch (packet[2])
            {
            case 0x01U:  /* LE Connection Complete: [3]=status */
                leaud_log(0x3E01, size > 3U ? packet[3] : 0xFEU);
                break;
            case 0x0AU:  /* LE Enhanced Connection Complete: [3]=status */
                leaud_log(0x3E0A, size > 3U ? packet[3] : 0xFEU);
                break;
            case 0x0DU:  /* LE Extended Connection Complete: [3]=status */
                leaud_log(0x3E0D, size > 3U ? packet[3] : 0xFEU);
                break;
            case 0x08U:  /* LE Encryption Change: [3]=status [4]=enabled */
                leaud_log(0x3E08 | ((size > 4U && packet[4]) ? 0x8000U : 0U),
                          size > 3U ? packet[3] : 0xFEU);
                break;
            case 0x05U:  /* LE Long Term Key Request: [3]=status 无, 仅标记 */
                leaud_log(0x3E05, 0);
                break;
            default:
                break;
            }
        }
        break;
    default:
        break;
    }

    /* M8.0 探针: 拦截 Read_Local_Supported_Commands 应答, 解码 LE ISO 能力 */
    if (s_iso_probe_pending &&
        hci_event_packet_get_type(packet) == HCI_EVENT_COMMAND_COMPLETE &&
        hci_event_command_complete_get_command_opcode(packet) ==
            HCI_OPCODE_HCI_READ_LOCAL_SUPPORTED_COMMANDS)
    {
        s_iso_probe_pending = 0;
        const uint8_t *ret = hci_event_command_complete_get_return_parameters(packet);
        if (ret[0] == 0U)
        {
            bt_iso_probe_print(ret + 1);
        }
        else
        {
            rt_kprintf("[ISO] command complete status %u\n", (unsigned)ret[0]);
        }
    }

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
            /* M8.x: GATT Database Hash 计算 (btstack_crypto 异步 CMAC) */
            {
                extern void ft_gatt_hash_start(void);
                ft_gatt_hash_start();
            }
            feathertalk_ipc_send_event(40);
            BT_CP(60);
            /* 双模/仅 LE 通道: 启动 LE 可连接广播; 仅经典通道跳过 */
            if (s_audio_transport != FT_TRANSPORT_CLASSIC_ONLY)
            {
                bt_adv_start();
            }
            /* M4a: Classic 可见+可连 (与 LE 广播并存, 双模标准做法)。
             * 这些是 gap task 型 API, 经 hci_run 排队发送, 不与上面的
             * adv 串行链冲突 */
            gap_set_class_of_device(0x240418U);  /* Audio/Rendering + Loudspeaker */
            gap_set_local_name("FeatherTalk");
            /* M8.1: Classic EIR 声明 LE Audio 服务 (CAS/ASCS) —— 手机在
             * 配对时读 EIR 判定 "对端支持 LE Audio", 缺失则不建 LE 链路
             * (实测: 小米15U 只连经典 A2DP, 从不发起 LE 连接) */
            bt_classic_eir_setup();
#if !FEATHERTALK_BT_LE_AUDIO_ONLY
            /* 仅 LE 模式关闭经典可发现/可连; 双模/仅经典均开 */
            gap_discoverable_control(s_audio_transport == FT_TRANSPORT_LE_ONLY ? 0 : 1);
            gap_connectable_control(s_audio_transport == FT_TRANSPORT_LE_ONLY ? 0 : 1);
            /* 仅经典模式: READY 判定不经过 LE 广播链 (0x2039), 直接就绪 */
            if (s_audio_transport == FT_TRANSPORT_CLASSIC_ONLY)
            {
                s_bt_state = 2;
                ft_radio_set_state(FT_RADIO_BT, FT_RADIO_READY, 0);
                rt_kprintf("[BT] classic-only ready (A2DP discoverable)\n");
                feathertalk_ipc_send_event(41);
                sm_init();
                sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
                sm_set_authentication_requirements(SM_AUTHREQ_BONDING | SM_AUTHREQ_SECURE_CONNECTION);
            }
#else
            /* LE-only 验证: 关闭 Classic 可发现/可连, 手机无 A2DP 可走 */
            gap_discoverable_control(0);
            gap_connectable_control(0);
#endif
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
            if (status == 0U && s_desired_on && s_bt_state != BT_STOPPING &&
                s_audio_transport != FT_TRANSPORT_CLASSIC_ONLY) bt_adv_set_data();
        }
        else if (opcode == 0x2008)
        {
            g_adv_cc_status[ADV_CC_DATA] = status;
            if (status == 0U && s_desired_on && s_bt_state != BT_STOPPING &&
                s_audio_transport != FT_TRANSPORT_CLASSIC_ONLY) bt_adv_enable();
        }
        else if (opcode == 0x2039)
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
                /* M8.1: 广播就绪后再初始化 SMP — 此时 WORKING 事件已过,
                 * SM 的 IR/ER 随机数路径不会再与 HCI 命令队列竞争。
                 * sm_init 幂等 (sm_initialized 保护), 后续周期只执行一次 */
                sm_init();
                sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
                sm_set_authentication_requirements(SM_AUTHREQ_BONDING | SM_AUTHREQ_SECURE_CONNECTION);
            }
        }
        break;
    }
    case HCI_EVENT_COMMAND_STATUS:
        bt_evt_log(0xFF0F, (uint16_t)((size > 1) ? packet[2] : 0xFD));
        break;
    case HCI_EVENT_META_GAP:
        if (size >= 6 && packet[2] == GAP_SUBEVENT_LE_CONNECTION_COMPLETE) {
            uint8_t status = gap_subevent_le_connection_complete_get_status(packet);
            rt_kprintf("[BT] LE connection status=0x%02x\n", status);
            if (status == 0) {
                g_le_connect_diag[0]++;
                s_gatt_con_handle = gap_subevent_le_connection_complete_get_connection_handle(packet);
                s_gatt_connected = 1;
                gap_extended_advertising_stop(s_le_adv_handle);
                if (s_le_discovery_registered)
                    gap_extended_advertising_stop(s_le_discovery_handle);
                feathertalk_ipc_send_event(60);
            }
        }
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
        g_le_connect_diag[1]++;
        g_le_connect_diag[2] = size > 5 ? packet[5] : 0xFF;
        rt_kprintf("[BT] disconnect handle=0x%x reason=0x%02x\n",
                   disc_handle, (unsigned)g_le_connect_diag[2]);
        if (disc_handle == s_classic_handle)
        {
            s_classic_connected = 0;
            rt_kprintf("[BT] Classic disconnected, back to discoverable\n");
            if (s_desired_on && s_bt_state == BT_READY && !FEATHERTALK_BT_LE_AUDIO_ONLY && s_audio_transport != FT_TRANSPORT_LE_ONLY) {
                gap_discoverable_control(1);
                gap_connectable_control(1);
            }
        }
        if (disc_handle == s_gatt_con_handle) {
            s_gatt_connected = 0;
            s_notify_enabled = 0;
            ft_ir_fail_cancel();
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
    /* Bounded: this runs inline in the M33 IPC thread's receive path. A
     * FOREVER wait here can park the whole IPC loop (heartbeats included)
     * if the owner thread is slow or stuck. Timeout surfaces as a normal
     * service failure to the quick-status reply. */
    if (rt_mutex_take(&s_control_lock, rt_tick_from_millisecond(200)) != RT_EOK)
    {
        return -RT_ETIMEOUT;
    }
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

/* ---- M6-BT: A2DP 角色管理 (经 IPC quick command BT_A2DP_ROLE 驱动) ---- */

static void bt_role_switch_owned(void)
{
    if (s_bt_state != BT_READY) return;
    if (s_a2dp_role == 1)
    {
        /* -> SOURCE: 挂断 Sink ACL (对端手机/PC 的 AVDTP 随 ACL 释放) */
        if (s_classic_connected && s_classic_handle != 0U)
            gap_disconnect(s_classic_handle);
        /* 通知 M55 切换音频路由 (激活 UAC tap + SBC 编码) */
        feathertalk_ipc_send_event(80);   /* 80 = role -> SOURCE */
        rt_kprintf("[BT] a2dp role -> SOURCE\n");
        /* 自动扫描并连接耳机 */
        extern void bt_a2dp_source_start_scan(void);
        bt_a2dp_source_start_scan();
    }
    else
    {
        /* -> SINK: 断开 Source 链路, 恢复 Sink 待连 */
        bt_a2dp_source_drop();
        feathertalk_ipc_send_event(81);   /* 81 = role -> SINK */
        rt_kprintf("[BT] a2dp role -> SINK\n");
    }
}

rt_err_t bt_service_set_a2dp_role(int role)
{
    if (role != 0 && role != 1) return -RT_EINVAL;
    if (!bt_service_enabled()) return -RT_EBUSY;
    rt_base_t level = rt_hw_interrupt_disable();
    int changed = s_a2dp_role != role;
    s_a2dp_role = role;
    rt_hw_interrupt_enable(level);
    if (changed)
    {
        int rc = bt_service_run_callback(bt_role_switch_owned);
        if (rc != RT_EOK) return -RT_EBUSY;
    }
    return RT_EOK;
}

int bt_service_a2dp_role(void)
{
    return s_a2dp_role;
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

/* M8.2: 上电自启动蓝牙主机 (产品默认; 开发期可 bt_off 关闭)。
 * 此前每次断电重启都需手动 bt_on, 控制台不可达时蓝牙无从开启。
 * 久置冷启动死机排查: 自启动延迟 3s —— 冷启动瞬间 BT HCD 下载的电流
 * 浪涌与显示/IPC/flash 挂载叠加, 是 USB 供电边缘触发欠压复位/锁死的
 * 头号嫌疑; 错峰启动消除浪涌叠加, 复位原因由 g_boot_reset_cause 捕获 */
static int bt_autostart(void)
{
    bt_service_start();
    return RT_EOK;
}
static rt_timer_t s_autostart_timer;
static void bt_autostart_delayed(void *param)
{
    (void)param;
    rt_kprintf("[BT] autostart (delayed 3s)\n");
    bt_autostart();
}
static int bt_autostart_schedule(void)
{
    s_autostart_timer = rt_timer_create("btauto", bt_autostart_delayed,
                                        RT_NULL, rt_tick_from_millisecond(3000),
                                        RT_TIMER_FLAG_ONE_SHOT | RT_TIMER_FLAG_SOFT_TIMER);
    if (s_autostart_timer != RT_NULL)
    {
        rt_timer_start(s_autostart_timer);
    }
    else
    {
        bt_autostart();   /* 定时器耗尽: 退回立即启动 */
    }
    return RT_EOK;
}
INIT_APP_EXPORT(bt_autostart_schedule);

/* ---- 久置冷启动死机排查: 复位原因捕获 (SWD 读) ----
 * SRSS RES_CAUSE 位: bit0=WDT bit1=ACT_FAULT bit2=DPSLP_FAULT
 * bit3=调试复位 bit4..6=SOFT bit8..11=MCWDT0..3; 全 0 = POR/XRES */
volatile uint32_t g_boot_reset_cause;
volatile uint32_t g_boot_reset_cause2;
static int bt_reset_cause_capture(void)
{
    extern uint32_t Cy_SysLib_GetResetReason(void);
    g_boot_reset_cause = Cy_SysLib_GetResetReason();
    g_boot_reset_cause2 = *(volatile uint32_t *)(SRSS_BASE + 0x1BD4UL);
    return RT_EOK;
}
INIT_BOARD_EXPORT(bt_reset_cause_capture);

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

/* M8.0 探针: Read_Local_Supported_Commands 解码 LE ISO 能力位
 * (bit = 224 + OCF - 1; CIS/BIG/ISO Data Path 在 bytes 40-41) */
static volatile int s_iso_probe_pending;

static void bt_iso_probe_print(const uint8_t *mask)
{
    int i;
    rt_kprintf("[ISO] mask bytes 40-41: %02x %02x\n", mask[40], mask[41]);
    static const struct { const char *name; uint8_t byte, bit; } items[] = {
        { "LE Create CIS",          40, 3 },
        { "LE Remove CIS",          40, 4 },
        { "LE Accept CIS Request",  40, 5 },
        { "LE Create BIG",          40, 6 },
        { "LE BIG Create Sync",     40, 7 },
        { "LE BIG Terminate Sync",  41, 0 },
        { "LE Terminate BIG",       41, 1 },
        { "LE Setup ISO Data Path", 41, 5 },
        { "LE Remove ISO Data Path",41, 6 },
    };
    int fails = 0;
    for (i = 0; i < (int)(sizeof(items) / sizeof(items[0])); i++)
    {
        int ok = (mask[items[i].byte] >> items[i].bit) & 1;
        if (!ok) fails++;
        rt_kprintf("[ISO] %-26s %s\n", items[i].name, ok ? "YES" : "NO ");
    }
    rt_kprintf("[ISO] verdict: %s\n", fails ? "PARTIAL/NO ISO support" : "FULL ISO support");
}

static void bt_iso_probe_send(void)
{
    hci_send_cmd(&hci_read_local_supported_commands);
}

static int bt_iso_probe(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (s_bt_state != BT_READY)
    {
        rt_kprintf("[ISO] bt not ready (state=%d), run bt_on first\n", s_bt_state);
        return -1;
    }
    s_iso_probe_pending = 1;
    int rc = bt_service_run_callback(bt_iso_probe_send);
    rt_kprintf("[ISO] read local supported commands queued rc=%d\n", rc);
    return rc;
}
MSH_CMD_EXPORT_ALIAS(bt_iso_probe, bt_iso_probe, M8.0: probe controller LE ISO command support);
