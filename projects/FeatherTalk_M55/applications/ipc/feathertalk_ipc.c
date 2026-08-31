#include <rtdevice.h>
#include <rtthread.h>
#include <rthw.h>

#include <drv_ipc.h>
#include <feathertalk/ipc_protocol.h>

#include "feathertalk_ipc.h"

#define FEATHERTALK_REPORT_INTERVAL_MS 10000U

static rt_device_t g_ipc_rx = RT_NULL;
static rt_device_t g_ipc_tx = RT_NULL;
static rt_thread_t g_ipc_thread = RT_NULL;
static volatile rt_uint32_t g_local_status = 0;
static volatile rt_uint32_t g_peer_status = 0;
static volatile rt_uint32_t g_rx_count = 0;
static volatile rt_uint32_t g_tx_count = 0;
static volatile rt_uint32_t g_error_count = 0;
static volatile rt_uint32_t g_system_generation = 0;
static volatile rt_uint32_t g_quick_generation = 0;
static feathertalk_system_status_t g_system_status;
static feathertalk_quick_status_t g_quick_status;
static volatile rt_bool_t g_quick_command_pending = RT_FALSE;
static volatile rt_uint8_t g_quick_command_control;
static volatile rt_uint8_t g_quick_command_value;
static volatile rt_uint32_t g_quick_command_sequence;

static void feathertalk_system_write_begin(void)
{
    g_system_generation++;
    rt_hw_dmb();
}

static void feathertalk_system_write_end(void)
{
    rt_hw_dmb();
    g_system_generation++;
}

static void feathertalk_system_update_heartbeat(const feathertalk_ipc_message_t *message)
{
    feathertalk_system_write_begin();
    g_system_status.m33_uptime_ms = message->uptime_ms;
    g_system_status.received_ms = rt_tick_get_millisecond();
    feathertalk_system_write_end();
}

static void feathertalk_system_update_status(const feathertalk_ipc_system_status_t *message)
{
    feathertalk_system_write_begin();
    g_system_status.sequence = message->sequence;
    g_system_status.received_ms = rt_tick_get_millisecond();
    g_system_status.unix_time = message->unix_time;
    g_system_status.battery_percent = message->battery_percent;
    g_system_status.network_state = message->network_state;
    g_system_status.signal_percent = message->signal_percent;
    g_system_status.flags = message->flags;
    feathertalk_system_write_end();
}

static void feathertalk_quick_update_status(const feathertalk_ipc_quick_status_t *message)
{
    g_quick_generation++;
    rt_hw_dmb();
    g_quick_status.sequence = message->sequence;
    g_quick_status.received_ms = rt_tick_get_millisecond();
    g_quick_status.capabilities = message->capabilities;
    g_quick_status.enabled = message->enabled;
    g_quick_status.connected = message->connected;
    g_quick_status.wifi_signal_percent = message->wifi_signal_percent;
    g_quick_status.brightness_percent = message->brightness_percent;
    g_quick_status.rotation = message->rotation;
    g_quick_status.last_control = message->last_control;
    g_quick_status.result = message->result;
    rt_hw_dmb();
    g_quick_generation++;
}

static rt_bool_t feathertalk_ipc_send_quick_command(rt_uint32_t sequence,
                                                    rt_uint8_t control,
                                                    rt_uint8_t value)
{
    edge_rc_frame_t frame;
    feathertalk_ipc_quick_command_t message;

    rt_memset(&frame, 0, sizeof(frame));
    rt_memset(&message, 0, sizeof(message));
    message.abi_version = FEATHERTALK_IPC_ABI_VERSION;
    message.message_id = FEATHERTALK_IPC_MSG_QUICK_COMMAND;
    message.sequence = sequence;
    message.control = control;
    message.value = value;
    rt_memcpy(frame.channel, &message, sizeof(message));
    frame.seq = sequence;
    if (rt_device_write(g_ipc_tx, 0, &frame, 1) != 1)
    {
        g_error_count++;
        return RT_FALSE;
    }
    g_tx_count++;
    return RT_TRUE;
}

static rt_bool_t feathertalk_ipc_reply(feathertalk_ipc_message_id_t message_id,
                                      rt_uint32_t sequence)
{
    edge_rc_frame_t frame;
    feathertalk_ipc_message_t message;

    rt_memset(&frame, 0, sizeof(frame));
    message.abi_version = FEATHERTALK_IPC_ABI_VERSION;
    message.message_id = (uint16_t)message_id;
    message.sequence = sequence;
    message.uptime_ms = rt_tick_get_millisecond();
    message.status = g_local_status | FEATHERTALK_STATUS_PEER_ONLINE;
    rt_memcpy(frame.channel, &message, sizeof(message));
    frame.seq = sequence;

    if (rt_device_write(g_ipc_tx, 0, &frame, 1) != 1)
    {
        g_error_count++;
        return RT_FALSE;
    }

    g_tx_count++;
    return RT_TRUE;
}

static void feathertalk_ipc_thread_entry(void *parameter)
{
    rt_uint32_t last_report_ms = 0;

    (void)parameter;
    rt_kprintf("[FeatherTalk M55] IPC responder ready\n");

    while (1)
    {
        edge_rc_frame_t frame;
        rt_uint32_t now = rt_tick_get_millisecond();

        while (rt_device_read(g_ipc_rx, 0, &frame, 1) == 1)
        {
            feathertalk_ipc_message_t message;
            feathertalk_ipc_message_id_t response_id;

            rt_memcpy(&message, frame.channel, sizeof(message));
            if (message.abi_version != FEATHERTALK_IPC_ABI_VERSION)
            {
                g_error_count++;
                continue;
            }

            if (message.message_id == FEATHERTALK_IPC_MSG_SYSTEM_STATUS)
            {
                feathertalk_ipc_system_status_t system_message;

                rt_memcpy(&system_message, frame.channel, sizeof(system_message));
                g_rx_count++;
                feathertalk_system_update_status(&system_message);
                continue;
            }

            if (message.message_id == FEATHERTALK_IPC_MSG_QUICK_STATUS)
            {
                feathertalk_ipc_quick_status_t quick_message;
                rt_memcpy(&quick_message, frame.channel, sizeof(quick_message));
                g_rx_count++;
                feathertalk_quick_update_status(&quick_message);
                continue;
            }

            if (message.message_id == FEATHERTALK_IPC_MSG_EVENT)
            {
                /* M33 process tracing (e.g. Bluetooth bring-up stages):
                   the numeric code travels in sequence/status. */
                g_rx_count++;
                rt_kprintf("[M33 evt %lu @%lums]\n",
                           (unsigned long)message.status,
                           (unsigned long)message.uptime_ms);
                continue;
            }

            if (message.message_id == FEATHERTALK_IPC_MSG_HELLO)
            {
                response_id = FEATHERTALK_IPC_MSG_HELLO_ACK;
            }
            else if (message.message_id == FEATHERTALK_IPC_MSG_HEARTBEAT)
            {
                response_id = FEATHERTALK_IPC_MSG_HEARTBEAT_ACK;
            }
            else
            {
                continue;
            }

            g_rx_count++;
            g_peer_status = message.status;
            feathertalk_system_update_heartbeat(&message);
            (void)feathertalk_ipc_reply(response_id, message.sequence);
        }

        if (g_quick_command_pending)
        {
            rt_base_t level;
            rt_uint8_t control;
            rt_uint8_t value;
            rt_uint32_t sequence;
            level = rt_hw_interrupt_disable();
            control = g_quick_command_control;
            value = g_quick_command_value;
            sequence = g_quick_command_sequence;
            g_quick_command_pending = RT_FALSE;
            rt_hw_interrupt_enable(level);
            (void)feathertalk_ipc_send_quick_command(sequence, control, value);
        }

        if ((now - last_report_ms) >= FEATHERTALK_REPORT_INTERVAL_MS)
        {
            rt_kprintf("[FeatherTalk M55] tx=%lu rx=%lu err=%lu local=0x%08lx peer=0x%08lx\n",
                       (unsigned long)g_tx_count,
                       (unsigned long)g_rx_count,
                       (unsigned long)g_error_count,
                       (unsigned long)g_local_status,
                       (unsigned long)g_peer_status);
            last_report_ms = now;
        }

        rt_thread_mdelay(5);
    }
}

static int feather_m55_status(int argc, char **argv)
{
    edge_ipc_device_stats_t tx_stats = {0};
    edge_ipc_device_stats_t rx_stats = {0};

    (void)argc;
    (void)argv;
    if (g_ipc_tx != RT_NULL)
    {
        (void)rt_device_control(g_ipc_tx, EDGE_IPC_CTRL_GET_STATS, &tx_stats);
    }
    if (g_ipc_rx != RT_NULL)
    {
        (void)rt_device_control(g_ipc_rx, EDGE_IPC_CTRL_GET_STATS, &rx_stats);
    }

    rt_kprintf("FeatherTalk M55 responder\n");
    rt_kprintf("  local/peer     : 0x%08lx/0x%08lx\n",
               (unsigned long)g_local_status,
               (unsigned long)g_peer_status);
    rt_kprintf("  app tx/rx/err  : %lu/%lu/%lu\n",
               (unsigned long)g_tx_count,
               (unsigned long)g_rx_count,
               (unsigned long)g_error_count);
    rt_kprintf("  driver tx/rx   : %lu/%lu\n",
               (unsigned long)tx_stats.tx_ok,
               (unsigned long)rx_stats.rx_ok);
    rt_kprintf("  system seq/time: %lu/%lu flags=0x%02x battery=%u network=%u\n",
               (unsigned long)g_system_status.sequence,
               (unsigned long)g_system_status.unix_time,
               g_system_status.flags,
               g_system_status.battery_percent,
               g_system_status.network_state);
    rt_kprintf("  quick seq/caps : %lu/0x%02x enabled=0x%02x connected=0x%02x "
               "wifi-signal=%u brightness=%u rotation=%u result=%u\n",
               (unsigned long)g_quick_status.sequence,
               g_quick_status.capabilities, g_quick_status.enabled,
               g_quick_status.connected, g_quick_status.wifi_signal_percent,
               g_quick_status.brightness_percent, g_quick_status.rotation,
               g_quick_status.result);
    return 0;
}
MSH_CMD_EXPORT(feather_m55_status, Show FeatherTalk M55 IPC status);

static const char *feathertalk_quick_result_name(uint8_t result)
{
    switch (result)
    {
    case FEATHERTALK_QUICK_RESULT_NONE:        return "none";
    case FEATHERTALK_QUICK_RESULT_OK:          return "ok";
    case FEATHERTALK_QUICK_RESULT_UNAVAILABLE: return "unavailable";
    case FEATHERTALK_QUICK_RESULT_INVALID:     return "invalid";
    case FEATHERTALK_QUICK_RESULT_FAILED:      return "failed";
    default:                                   return "unknown";
    }
}

static int bt_status(int argc, char **argv)
{
    feathertalk_quick_status_t status;

    (void)argc;
    (void)argv;
    if (feathertalk_ipc_get_quick_status(&status) != RT_EOK)
    {
        rt_kprintf("bt_status: no M33 quick status received yet\n");
        return 0;
    }

    rt_kprintf("Bluetooth (M33 AIROC host, IPC quick seq=%lu age=%lums):\n",
               (unsigned long)status.sequence,
               (unsigned long)(rt_tick_get_millisecond() - status.received_ms));
    rt_kprintf("  available : %s\n",
               (status.capabilities & FEATHERTALK_QUICK_CAP_BLUETOOTH) != 0U ?
               "yes" : "no");
    rt_kprintf("  enabled   : %s\n",
               (status.enabled & FEATHERTALK_QUICK_CAP_BLUETOOTH) != 0U ?
               "yes (host READY)" : "no");
    rt_kprintf("  connected : %s\n",
               (status.connected & FEATHERTALK_QUICK_CAP_BLUETOOTH) != 0U ?
               "yes" : "no (connections not supported yet)");
    rt_kprintf("  last cmd  : control=%u result=%s\n",
               status.last_control,
               feathertalk_quick_result_name(status.result));
    return 0;
}
MSH_CMD_EXPORT(bt_status, Show Bluetooth state reported by M33 over IPC);

static int bt_on(int argc, char **argv)
{
    int rc;

    (void)argc;
    (void)argv;
    rc = feathertalk_ipc_set_quick_control(FEATHERTALK_QUICK_BLUETOOTH, 1U);
    rt_kprintf("bt_on: quick command %s (check bt_status in a few seconds)\n",
               (rc == RT_EOK) ? "queued" : "rejected (busy)");
    return 0;
}
MSH_CMD_EXPORT(bt_on, Ask M33 to start the Bluetooth host);

static int bt_off(int argc, char **argv)
{
    int rc;

    (void)argc;
    (void)argv;
    rc = feathertalk_ipc_set_quick_control(FEATHERTALK_QUICK_BLUETOOTH, 0U);
    rt_kprintf("bt_off: quick command %s (stop not implemented on M33 yet)\n",
               (rc == RT_EOK) ? "queued" : "rejected (busy)");
    return 0;
}
MSH_CMD_EXPORT(bt_off, Ask M33 to stop the Bluetooth host);

int feathertalk_ipc_start(void)
{
    rt_err_t result;

    if (g_ipc_thread != RT_NULL)
    {
        return RT_EOK;
    }

    result = edge_ipc_device_register();
    if (result != RT_EOK)
    {
        return result;
    }

    g_ipc_rx = edge_ipc_device_find(EDGE_IPC0_DEVICE_NAME);
    g_ipc_tx = edge_ipc_device_find(EDGE_IPC1_DEVICE_NAME);
    if (g_ipc_rx == RT_NULL || g_ipc_tx == RT_NULL)
    {
        return -RT_ENOSYS;
    }

    result = rt_device_open(g_ipc_rx, RT_DEVICE_OFLAG_RDWR);
    if (result != RT_EOK)
    {
        return result;
    }
    result = rt_device_open(g_ipc_tx, RT_DEVICE_OFLAG_RDWR);
    if (result != RT_EOK)
    {
        rt_device_close(g_ipc_rx);
        return result;
    }

    g_local_status = FEATHERTALK_STATUS_M55_READY | FEATHERTALK_STATUS_IPC_READY;
    g_ipc_thread = rt_thread_create("ft_m55",
                                    feathertalk_ipc_thread_entry,
                                    RT_NULL,
                                    2048,
                                    18,
                                    10);
    if (g_ipc_thread == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    rt_thread_startup(g_ipc_thread);
    return RT_EOK;
}

void feathertalk_ipc_set_lvgl_ready(void)
{
    g_local_status |= FEATHERTALK_STATUS_LVGL_READY;
}

int feathertalk_ipc_get_system_status(feathertalk_system_status_t *status)
{
    rt_uint32_t before;
    rt_uint32_t after;

    if (status == RT_NULL)
    {
        return -RT_EINVAL;
    }

    do
    {
        before = g_system_generation;
        if ((before & 1U) != 0U)
        {
            continue;
        }
        rt_hw_dmb();
        *status = g_system_status;
        rt_hw_dmb();
        after = g_system_generation;
    }
    while ((before != after) || ((after & 1U) != 0U));

    return (status->sequence != 0U) ? RT_EOK : -RT_EEMPTY;
}

int feathertalk_ipc_get_quick_status(feathertalk_quick_status_t *status)
{
    rt_uint32_t before;
    rt_uint32_t after;
    if (status == RT_NULL) return -RT_EINVAL;
    do
    {
        before = g_quick_generation;
        if ((before & 1U) != 0U) continue;
        rt_hw_dmb();
        *status = g_quick_status;
        rt_hw_dmb();
        after = g_quick_generation;
    }
    while ((before != after) || ((after & 1U) != 0U));
    return status->sequence != 0U ? RT_EOK : -RT_EEMPTY;
}

int feathertalk_ipc_set_quick_control(uint8_t control, uint8_t value)
{
    rt_base_t level;
    if (control >= FEATHERTALK_QUICK_COUNT) return -RT_EINVAL;
    level = rt_hw_interrupt_disable();
    if (g_quick_command_pending)
    {
        rt_hw_interrupt_enable(level);
        return -RT_EBUSY;
    }
    g_quick_command_control = control;
    g_quick_command_value = value;
    g_quick_command_sequence++;
    if (g_quick_command_sequence == 0U) g_quick_command_sequence = 1U;
    g_quick_command_pending = RT_TRUE;
    rt_hw_interrupt_enable(level);
    return RT_EOK;
}
