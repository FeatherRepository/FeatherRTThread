#include <rtdevice.h>
#include <rtthread.h>

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
            (void)feathertalk_ipc_reply(response_id, message.sequence);
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
    return 0;
}
MSH_CMD_EXPORT(feather_m55_status, Show FeatherTalk M55 IPC status);

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
