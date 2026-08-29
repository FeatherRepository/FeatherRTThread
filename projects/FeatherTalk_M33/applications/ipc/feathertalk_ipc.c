#include <rtdevice.h>
#include <rtthread.h>
#include <string.h>
#include <time.h>

#include <board.h>
#include <drv_ipc.h>
#include <feathertalk/ipc_protocol.h>

#include "feathertalk_ipc.h"

#define FEATHERTALK_HEARTBEAT_INTERVAL_MS 1000U
#define FEATHERTALK_PEER_TIMEOUT_MS       3000U
#define FEATHERTALK_REPORT_INTERVAL_MS   10000U

static rt_device_t g_ipc_tx = RT_NULL;
static rt_device_t g_ipc_rx = RT_NULL;
static rt_thread_t g_ipc_thread = RT_NULL;
static volatile rt_bool_t g_peer_online = RT_FALSE;
static volatile rt_bool_t g_force_hello = RT_FALSE;
static volatile rt_uint32_t g_sequence = 0;
static volatile rt_uint32_t g_last_rx_ms = 0;
static volatile rt_uint32_t g_peer_status = 0;
static volatile rt_uint32_t g_tx_count = 0;
static volatile rt_uint32_t g_rx_count = 0;
static volatile rt_uint32_t g_error_count = 0;
static rt_uint8_t g_quick_last_control = FEATHERTALK_SYSTEM_VALUE_UNKNOWN;
static rt_uint8_t g_quick_last_result = FEATHERTALK_QUICK_RESULT_NONE;

static rt_bool_t feathertalk_ipc_send(feathertalk_ipc_message_id_t message_id,
                                     rt_uint32_t sequence)
{
    edge_rc_frame_t frame;
    feathertalk_ipc_message_t message;

    rt_memset(&frame, 0, sizeof(frame));
    message.abi_version = FEATHERTALK_IPC_ABI_VERSION;
    message.message_id = (uint16_t)message_id;
    message.sequence = sequence;
    message.uptime_ms = rt_tick_get_millisecond();
    message.status = FEATHERTALK_STATUS_M33_READY | FEATHERTALK_STATUS_IPC_READY;
    if (g_peer_online)
    {
        message.status |= FEATHERTALK_STATUS_PEER_ONLINE;
    }

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

static rt_bool_t feathertalk_ipc_send_system_status(rt_uint32_t sequence)
{
    edge_rc_frame_t frame;
    feathertalk_ipc_system_status_t message;
    time_t wall_clock = 0;

    rt_memset(&frame, 0, sizeof(frame));
    rt_memset(&message, 0, sizeof(message));
    message.abi_version = FEATHERTALK_IPC_ABI_VERSION;
    message.message_id = FEATHERTALK_IPC_MSG_SYSTEM_STATUS;
    message.sequence = sequence;
    message.battery_percent = FEATHERTALK_SYSTEM_VALUE_UNKNOWN;
    message.network_state = FEATHERTALK_NETWORK_UNAVAILABLE;
    message.signal_percent = FEATHERTALK_SYSTEM_VALUE_UNKNOWN;

#ifdef BSP_USING_RTC
    message.flags |= FEATHERTALK_SYSTEM_RTC_PRESENT;
    wall_clock = time(RT_NULL);
    if (wall_clock >= (time_t)1577836800)
    {
        message.unix_time = (uint32_t)wall_clock;
        message.flags |= FEATHERTALK_SYSTEM_TIME_VALID;
    }
#endif

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

static rt_bool_t feathertalk_ipc_send_quick_status(rt_uint32_t sequence)
{
    edge_rc_frame_t frame;
    feathertalk_ipc_quick_status_t message;

    rt_memset(&frame, 0, sizeof(frame));
    rt_memset(&message, 0, sizeof(message));
    message.abi_version = FEATHERTALK_IPC_ABI_VERSION;
    message.message_id = FEATHERTALK_IPC_MSG_QUICK_STATUS;
    message.sequence = sequence;
    /* Product drivers are not enabled yet. Capability bits deliberately stay
       clear so the M55 UI exposes the controls as unavailable, never as fake
       off states. */
    message.wifi_signal_percent = FEATHERTALK_SYSTEM_VALUE_UNKNOWN;
    message.brightness_percent = FEATHERTALK_SYSTEM_VALUE_UNKNOWN;
    message.rotation = FEATHERTALK_SYSTEM_VALUE_UNKNOWN;
    message.last_control = g_quick_last_control;
    message.result = g_quick_last_result;
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

static void feathertalk_ipc_receive(void)
{
    edge_rc_frame_t frame;

    while (rt_device_read(g_ipc_rx, 0, &frame, 1) == 1)
    {
        feathertalk_ipc_message_t message;

        rt_memcpy(&message, frame.channel, sizeof(message));
        if (message.abi_version != FEATHERTALK_IPC_ABI_VERSION)
        {
            g_error_count++;
            continue;
        }

        if (message.message_id == FEATHERTALK_IPC_MSG_QUICK_COMMAND)
        {
            feathertalk_ipc_quick_command_t command;
            rt_memcpy(&command, frame.channel, sizeof(command));
            g_rx_count++;
            g_quick_last_control = command.control;
            g_quick_last_result = command.control < FEATHERTALK_QUICK_COUNT ?
                                  FEATHERTALK_QUICK_RESULT_UNAVAILABLE :
                                  FEATHERTALK_QUICK_RESULT_INVALID;
            (void)feathertalk_ipc_send_quick_status(command.sequence);
            continue;
        }

        if (message.message_id != FEATHERTALK_IPC_MSG_HELLO_ACK &&
            message.message_id != FEATHERTALK_IPC_MSG_HEARTBEAT_ACK)
        {
            continue;
        }

        g_rx_count++;
        g_last_rx_ms = rt_tick_get_millisecond();
        g_peer_status = message.status;
        if (!g_peer_online)
        {
            g_peer_online = RT_TRUE;
            rt_kprintf("[FeatherTalk M33] M55 online: ABI=%u status=0x%08lx\r\n",
                       message.abi_version,
                       (unsigned long)message.status);
        }
    }
}

static void feathertalk_ipc_thread_entry(void *parameter)
{
    rt_uint32_t last_tx_ms = 0;
    rt_uint32_t last_report_ms = 0;

    (void)parameter;
    rt_kprintf("[FeatherTalk M33] waiting for M55 at 0x%08lx\r\n",
               (unsigned long)CY_CM55_APP_BOOT_ADDR);

    while (1)
    {
        rt_uint32_t now = rt_tick_get_millisecond();

        feathertalk_ipc_receive();

        if (g_peer_online && (now - g_last_rx_ms) > FEATHERTALK_PEER_TIMEOUT_MS)
        {
            g_peer_online = RT_FALSE;
            g_peer_status = 0;
            rt_kprintf("[FeatherTalk M33] M55 heartbeat timeout\r\n");
        }

        if (g_force_hello || (now - last_tx_ms) >= FEATHERTALK_HEARTBEAT_INTERVAL_MS)
        {
            rt_uint32_t sequence = ++g_sequence;
            feathertalk_ipc_message_id_t id = g_peer_online
                ? FEATHERTALK_IPC_MSG_HEARTBEAT
                : FEATHERTALK_IPC_MSG_HELLO;

            g_force_hello = RT_FALSE;
            (void)feathertalk_ipc_send(id, sequence);
            (void)feathertalk_ipc_send_system_status(sequence);
            (void)feathertalk_ipc_send_quick_status(sequence);
            last_tx_ms = now;
        }

        if ((now - last_report_ms) >= FEATHERTALK_REPORT_INTERVAL_MS)
        {
            rt_kprintf("[FeatherTalk M33] M55=%s tx=%lu rx=%lu err=%lu peer=0x%08lx\r\n",
                       g_peer_online ? "online" : "offline",
                       (unsigned long)g_tx_count,
                       (unsigned long)g_rx_count,
                       (unsigned long)g_error_count,
                       (unsigned long)g_peer_status);
            last_report_ms = now;
        }

        rt_thread_mdelay(10);
    }
}

static int feather_status(int argc, char **argv)
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

    rt_kprintf("FeatherTalk M33 supervisor\r\n");
    rt_kprintf("  CM55 hw status : 0x%08lx\r\n",
               (unsigned long)Cy_SysGetCM55Status(MXCM55));
    rt_kprintf("  peer           : %s\r\n", g_peer_online ? "online" : "offline");
    rt_kprintf("  peer status    : 0x%08lx\r\n", (unsigned long)g_peer_status);
    rt_kprintf("  app tx/rx/err  : %lu/%lu/%lu\r\n",
               (unsigned long)g_tx_count,
               (unsigned long)g_rx_count,
               (unsigned long)g_error_count);
    rt_kprintf("  driver tx/rx   : %lu/%lu\r\n",
               (unsigned long)tx_stats.tx_ok,
               (unsigned long)rx_stats.rx_ok);
    rt_kprintf("  driver busy    : %lu (retry %lu, timeout %lu)\r\n",
               (unsigned long)tx_stats.tx_busy,
               (unsigned long)tx_stats.tx_retry,
               (unsigned long)tx_stats.tx_timeout);
    return 0;
}
MSH_CMD_EXPORT(feather_status, Show FeatherTalk dual-core status);

static int feather_ping(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    g_peer_online = RT_FALSE;
    g_force_hello = RT_TRUE;
    rt_kprintf("FeatherTalk HELLO queued\r\n");
    return 0;
}
MSH_CMD_EXPORT(feather_ping, Force a FeatherTalk M55 HELLO probe);

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

    g_ipc_tx = edge_ipc_device_find(EDGE_IPC0_DEVICE_NAME);
    g_ipc_rx = edge_ipc_device_find(EDGE_IPC1_DEVICE_NAME);
    if (g_ipc_tx == RT_NULL || g_ipc_rx == RT_NULL)
    {
        return -RT_ENOSYS;
    }

    result = rt_device_open(g_ipc_tx, RT_DEVICE_OFLAG_RDWR);
    if (result != RT_EOK)
    {
        return result;
    }
    result = rt_device_open(g_ipc_rx, RT_DEVICE_OFLAG_RDWR);
    if (result != RT_EOK)
    {
        rt_device_close(g_ipc_tx);
        return result;
    }

    g_ipc_thread = rt_thread_create("ft_m33",
                                    feathertalk_ipc_thread_entry,
                                    RT_NULL,
                                    2048,
                                    20,
                                    10);
    if (g_ipc_thread == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    rt_thread_startup(g_ipc_thread);
    return RT_EOK;
}
