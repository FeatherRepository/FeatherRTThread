/*
 * Copyright (c) 2026 FeatherTalk contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "feathertalk_bt_host.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <finsh.h>
#include <rtdevice.h>

#include "board.h"

#ifdef FEATHERTALK_BT_HOST_PRESENT

#include "wiced_bt_ble.h"
#include "wiced_bt_stack.h"

#include "cybt_platform_interface.h"
#include "cybt_platform_trace.h"

#define FT_BT_MODULE_POWER_PIN GET_PIN(16, 3)
#define FT_BT_DEVICE_NAME "FeatherTalk-E84"
#define FT_BT_MAX_SCAN_RESULTS 24U
#define FT_BT_SCAN_NAME_LENGTH 24U

typedef enum
{
    FT_BT_HOST_OFF = 0,
    FT_BT_HOST_STARTING,
    FT_BT_HOST_READY,
    FT_BT_HOST_FAILED
} ft_bt_host_state_t;

typedef struct
{
    uint8_t address[6];
    uint8_t address_type;
    int8_t rssi;
    uint8_t event_type;
    char name[FT_BT_SCAN_NAME_LENGTH];
} ft_bt_scan_result_t;

typedef struct
{
    volatile ft_bt_host_state_t state;
    volatile int last_error;
    volatile bool scanning;
    volatile bool advertising;
    uint8_t local_address[6];
    uint32_t reports;
    uint32_t unique_devices;
    ft_bt_scan_result_t devices[FT_BT_MAX_SCAN_RESULTS];
} ft_bt_host_status_t;

static ft_bt_host_status_t g_bt;
static struct rt_mutex g_bt_status_lock;
static bool g_bt_status_lock_ready;

/* The upstream integration exposes this hook weakly.  Route its diagnostics
 * to the existing M33 console so failures before BTM_ENABLED_EVT are visible. */
cybt_result_t cybt_debug_uart_send_trace(uint16_t length, uint8_t *data)
{
    rt_device_t console = rt_console_get_device();

    if ((console != RT_NULL) && (data != RT_NULL) && (length != 0U))
    {
        (void)rt_device_write(console, 0, data, length);
        (void)rt_device_write(console, 0, "\r\n", 2U);
    }
    return CYBT_SUCCESS;
}

static uint8_t g_adv_flags = BTM_BLE_GENERAL_DISCOVERABLE_FLAG | BTM_BLE_BREDR_NOT_SUPPORTED;
static uint8_t g_adv_name[] = FT_BT_DEVICE_NAME;
static wiced_bt_ble_advert_elem_t g_adv_data[] =
{
    {
        .advert_type = BTM_BLE_ADVERT_TYPE_FLAG,
        .len = 1U,
        .p_data = &g_adv_flags,
    },
    {
        .advert_type = BTM_BLE_ADVERT_TYPE_NAME_COMPLETE,
        .len = sizeof(g_adv_name) - 1U,
        .p_data = g_adv_name,
    },
};

static const wiced_bt_cfg_ble_scan_settings_t g_scan_settings =
{
    .scan_mode = BTM_BLE_SCAN_MODE_ACTIVE,
    .high_duty_scan_interval = WICED_BT_CFG_DEFAULT_HIGH_DUTY_SCAN_INTERVAL,
    .high_duty_scan_window = WICED_BT_CFG_DEFAULT_HIGH_DUTY_SCAN_WINDOW,
    .high_duty_scan_duration = 8U,
    .low_duty_scan_interval = WICED_BT_CFG_DEFAULT_LOW_DUTY_SCAN_INTERVAL,
    .low_duty_scan_window = WICED_BT_CFG_DEFAULT_LOW_DUTY_SCAN_WINDOW,
    /* A zero low-duty duration means infinite scanning.  Give the automatic
     * low-duty tail a short finite duration so every command is bounded. */
    .low_duty_scan_duration = 1U,
    .high_duty_conn_scan_interval = WICED_BT_CFG_DEFAULT_HIGH_DUTY_CONN_SCAN_INTERVAL,
    .high_duty_conn_scan_window = WICED_BT_CFG_DEFAULT_HIGH_DUTY_CONN_SCAN_WINDOW,
    .high_duty_conn_duration = 0U,
    .low_duty_conn_scan_interval = WICED_BT_CFG_DEFAULT_LOW_DUTY_CONN_SCAN_INTERVAL,
    .low_duty_conn_scan_window = WICED_BT_CFG_DEFAULT_LOW_DUTY_CONN_SCAN_WINDOW,
    .low_duty_conn_duration = 0U,
    .conn_min_interval = WICED_BT_CFG_DEFAULT_CONN_MIN_INTERVAL,
    .conn_max_interval = WICED_BT_CFG_DEFAULT_CONN_MAX_INTERVAL,
    .conn_latency = WICED_BT_CFG_DEFAULT_CONN_LATENCY,
    .conn_supervision_timeout = WICED_BT_CFG_DEFAULT_CONN_SUPERVISION_TIMEOUT,
};

static const wiced_bt_cfg_ble_advert_settings_t g_adv_settings =
{
    .channel_map = BTM_BLE_ADVERT_CHNL_37 | BTM_BLE_ADVERT_CHNL_38 | BTM_BLE_ADVERT_CHNL_39,
    .high_duty_min_interval = WICED_BT_CFG_DEFAULT_HIGH_DUTY_ADV_MIN_INTERVAL,
    .high_duty_max_interval = WICED_BT_CFG_DEFAULT_HIGH_DUTY_ADV_MAX_INTERVAL,
    .high_duty_duration = 0U,
    .low_duty_min_interval = WICED_BT_CFG_DEFAULT_LOW_DUTY_ADV_MIN_INTERVAL,
    .low_duty_max_interval = WICED_BT_CFG_DEFAULT_LOW_DUTY_ADV_MAX_INTERVAL,
    .low_duty_duration = 0U,
    .high_duty_directed_min_interval = WICED_BT_CFG_DEFAULT_HIGH_DUTY_DIRECTED_ADV_MIN_INTERVAL,
    .high_duty_directed_max_interval = WICED_BT_CFG_DEFAULT_HIGH_DUTY_DIRECTED_ADV_MAX_INTERVAL,
    .low_duty_directed_min_interval = WICED_BT_CFG_DEFAULT_LOW_DUTY_DIRECTED_ADV_MIN_INTERVAL,
    .low_duty_directed_max_interval = WICED_BT_CFG_DEFAULT_LOW_DUTY_DIRECTED_ADV_MAX_INTERVAL,
    .low_duty_directed_duration = 0U,
    .high_duty_nonconn_min_interval = WICED_BT_CFG_DEFAULT_HIGH_DUTY_ADV_MIN_INTERVAL,
    .high_duty_nonconn_max_interval = WICED_BT_CFG_DEFAULT_HIGH_DUTY_ADV_MAX_INTERVAL,
    .high_duty_nonconn_duration = 0U,
    .low_duty_nonconn_min_interval = WICED_BT_CFG_DEFAULT_LOW_DUTY_ADV_MIN_INTERVAL,
    .low_duty_nonconn_max_interval = WICED_BT_CFG_DEFAULT_LOW_DUTY_ADV_MAX_INTERVAL,
    .low_duty_nonconn_duration = 0U,
};

static const wiced_bt_cfg_ble_t g_ble_settings =
{
    .ble_max_simultaneous_links = 2U,
    .ble_max_rx_pdu_size = 251U,
    .appearance = 0U,
    .rpa_refresh_timeout = 0U,
    .host_addr_resolution_db_size = 5U,
    .p_ble_scan_cfg = &g_scan_settings,
    .p_ble_advert_cfg = &g_adv_settings,
    .default_ble_power_level = 0,
};

static const wiced_bt_cfg_gatt_t g_gatt_settings =
{
    .max_db_service_modules = 0U,
    .max_eatt_bearers = 0U,
};

static const wiced_bt_cfg_l2cap_application_t g_l2cap_settings =
{
    .max_app_l2cap_psms = 0U,
    .max_app_l2cap_channels = 0U,
    .max_app_l2cap_le_fixed_channels = 0U,
    .max_app_l2cap_br_edr_ertm_chnls = 0U,
    .max_app_l2cap_br_edr_ertm_tx_win = 0U,
};

static const wiced_bt_cfg_settings_t g_bt_settings =
{
    .device_name = g_adv_name,
    .security_required = BTM_SEC_BEST_EFFORT,
    .p_br_cfg = NULL,
    .p_ble_cfg = &g_ble_settings,
    .p_gatt_cfg = &g_gatt_settings,
    .p_isoc_cfg = NULL,
    .p_l2cap_app_cfg = &g_l2cap_settings,
};

static const char *ft_bt_state_name(ft_bt_host_state_t state)
{
    switch (state)
    {
    case FT_BT_HOST_OFF:
        return "off";
    case FT_BT_HOST_STARTING:
        return "starting";
    case FT_BT_HOST_READY:
        return "ready";
    case FT_BT_HOST_FAILED:
        return "failed";
    default:
        return "unknown";
    }
}

static void ft_bt_lock(void)
{
    if (g_bt_status_lock_ready)
    {
        (void)rt_mutex_take(&g_bt_status_lock, RT_WAITING_FOREVER);
    }
}

static void ft_bt_unlock(void)
{
    if (g_bt_status_lock_ready)
    {
        (void)rt_mutex_release(&g_bt_status_lock);
    }
}

static void ft_bt_copy_name(char *destination, uint8_t *advertisement)
{
    uint8_t length = 0U;
    uint8_t *name = NULL;
    uint32_t copy_length;

    destination[0] = '\0';
    if (advertisement != NULL)
    {
        name = wiced_bt_ble_check_advertising_data(advertisement,
                                                   BTM_BLE_ADVERT_TYPE_NAME_COMPLETE,
                                                   &length);
        if (name == NULL)
        {
            name = wiced_bt_ble_check_advertising_data(advertisement,
                                                       BTM_BLE_ADVERT_TYPE_NAME_SHORT,
                                                       &length);
        }
    }

    if ((name == NULL) || (length == 0U))
    {
        return;
    }

    copy_length = length;
    if (copy_length >= FT_BT_SCAN_NAME_LENGTH)
    {
        copy_length = FT_BT_SCAN_NAME_LENGTH - 1U;
    }
    memcpy(destination, name, copy_length);
    destination[copy_length] = '\0';
}

static void ft_bt_scan_callback(wiced_bt_ble_scan_results_t *result,
                                uint8_t *advertisement)
{
    uint32_t index;
    ft_bt_scan_result_t *device = NULL;

    if (result == NULL)
    {
        g_bt.scanning = false;
        rt_kprintf("[BT] scan complete: reports=%lu unique=%lu\r\n",
                   (unsigned long)g_bt.reports,
                   (unsigned long)g_bt.unique_devices);
        return;
    }

    ft_bt_lock();
    ++g_bt.reports;
    for (index = 0U; index < g_bt.unique_devices; ++index)
    {
        if (memcmp(g_bt.devices[index].address, result->remote_bd_addr, 6U) == 0)
        {
            device = &g_bt.devices[index];
            break;
        }
    }

    if ((device == NULL) && (g_bt.unique_devices < FT_BT_MAX_SCAN_RESULTS))
    {
        device = &g_bt.devices[g_bt.unique_devices++];
        memset(device, 0, sizeof(*device));
        memcpy(device->address, result->remote_bd_addr, 6U);
        device->address_type = result->ble_addr_type;
    }

    if (device != NULL)
    {
        device->rssi = result->rssi;
        device->event_type = result->ble_evt_type;
        if (device->name[0] == '\0')
        {
            ft_bt_copy_name(device->name, advertisement);
        }
    }
    ft_bt_unlock();
}

static wiced_result_t ft_bt_start_scan(bool clear_results)
{
    wiced_result_t result;

    if (g_bt.state != FT_BT_HOST_READY)
    {
        return WICED_NOTUP;
    }

    if (clear_results)
    {
        ft_bt_lock();
        g_bt.reports = 0U;
        g_bt.unique_devices = 0U;
        memset(g_bt.devices, 0, sizeof(g_bt.devices));
        ft_bt_unlock();
    }

    result = wiced_bt_ble_scan(BTM_BLE_SCAN_TYPE_HIGH_DUTY,
                               WICED_TRUE,
                               ft_bt_scan_callback);
    if ((result == WICED_BT_PENDING) || (result == WICED_BT_SUCCESS))
    {
        g_bt.scanning = true;
    }
    return result;
}

static wiced_result_t ft_bt_set_advertising(bool enable)
{
    wiced_result_t result;

    if (g_bt.state != FT_BT_HOST_READY)
    {
        return WICED_NOTUP;
    }

    if (enable)
    {
        result = wiced_bt_ble_set_raw_advertisement_data(
            (uint8_t)(sizeof(g_adv_data) / sizeof(g_adv_data[0])),
            g_adv_data);
        if (result != WICED_BT_SUCCESS)
        {
            return result;
        }
        result = wiced_bt_start_advertisements(BTM_BLE_ADVERT_NONCONN_LOW,
                                               BLE_ADDR_PUBLIC,
                                               NULL);
    }
    else
    {
        result = wiced_bt_start_advertisements(BTM_BLE_ADVERT_OFF,
                                               BLE_ADDR_PUBLIC,
                                               NULL);
    }

    if (result == WICED_BT_SUCCESS)
    {
        g_bt.advertising = enable;
    }
    return result;
}

static wiced_result_t ft_bt_management_callback(wiced_bt_management_evt_t event,
                                                wiced_bt_management_evt_data_t *event_data)
{
    wiced_result_t result = WICED_BT_SUCCESS;

    switch (event)
    {
    case BTM_ENABLED_EVT:
        if ((event_data != NULL) && (event_data->enabled.status == WICED_BT_SUCCESS))
        {
            wiced_bt_dev_read_local_addr(g_bt.local_address);
            g_bt.state = FT_BT_HOST_READY;
            g_bt.last_error = 0;
            rt_kprintf("[BT] AIROC host stack ready, addr=%02X:%02X:%02X:%02X:%02X:%02X\r\n",
                       g_bt.local_address[0], g_bt.local_address[1],
                       g_bt.local_address[2], g_bt.local_address[3],
                       g_bt.local_address[4], g_bt.local_address[5]);

            result = ft_bt_set_advertising(true);
            rt_kprintf("[BT] non-connectable advertising: %d\r\n", (int)result);
            result = ft_bt_start_scan(true);
            rt_kprintf("[BT] startup BLE scan: %d\r\n", (int)result);
        }
        else
        {
            g_bt.state = FT_BT_HOST_FAILED;
            g_bt.last_error = event_data != NULL ? (int)event_data->enabled.status : -1;
            rt_kprintf("[BT] host stack enable failed: %d\r\n", g_bt.last_error);
        }
        break;

    case BTM_BLE_SCAN_STATE_CHANGED_EVT:
        if (event_data != NULL)
        {
            g_bt.scanning = (event_data->ble_scan_state_changed != BTM_BLE_SCAN_TYPE_NONE);
        }
        break;

    case BTM_BLE_ADVERT_STATE_CHANGED_EVT:
        if (event_data != NULL)
        {
            g_bt.advertising = (event_data->ble_advert_state_changed != BTM_BLE_ADVERT_OFF);
        }
        break;

    default:
        break;
    }

    return result;
}

static void ft_bt_host_init_thread(void *parameter)
{
    wiced_result_t result;
    (void)parameter;

    rt_pin_mode(FT_BT_MODULE_POWER_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(FT_BT_MODULE_POWER_PIN, PIN_HIGH);
    rt_thread_mdelay(20);

    rt_kprintf("[BT] starting AIROC BLE host stack (download=3000000 runtime=115200)\r\n");
    /* Keep integration diagnostics available for genuine failures without
     * printing every PatchRAM HCI packet on the shared M33 console. */
    cybt_platform_set_trace_level(CYBT_TRACE_ID_ALL, CYBT_TRACE_LEVEL_ERROR);
    result = wiced_bt_stack_init(ft_bt_management_callback, &g_bt_settings);
    if (result != WICED_BT_SUCCESS)
    {
        g_bt.state = FT_BT_HOST_FAILED;
        g_bt.last_error = (int)result;
        rt_kprintf("[BT] wiced_bt_stack_init failed: %d\r\n", (int)result);
    }
}

rt_err_t feathertalk_bt_host_start(void)
{
    rt_thread_t thread;

    if (!g_bt_status_lock_ready)
    {
        if (rt_mutex_init(&g_bt_status_lock, "bthstat", RT_IPC_FLAG_PRIO) != RT_EOK)
        {
            return -RT_ERROR;
        }
        g_bt_status_lock_ready = true;
    }

    if (g_bt.state != FT_BT_HOST_OFF)
    {
        return RT_EOK;
    }
    g_bt.state = FT_BT_HOST_STARTING;

    thread = rt_thread_create("bt_host",
                              ft_bt_host_init_thread,
                              NULL,
                              6144U,
                              17U,
                              10U);
    if (thread == RT_NULL)
    {
        g_bt.state = FT_BT_HOST_FAILED;
        g_bt.last_error = -RT_ENOMEM;
        return -RT_ENOMEM;
    }
    return rt_thread_startup(thread);
}

static int ft_bt_status_command(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    rt_kprintf("BT host: state=%s error=%d scan=%s adv=%s reports=%lu unique=%lu\r\n",
               ft_bt_state_name(g_bt.state),
               g_bt.last_error,
               g_bt.scanning ? "on" : "off",
               g_bt.advertising ? "on" : "off",
               (unsigned long)g_bt.reports,
               (unsigned long)g_bt.unique_devices);
    if (g_bt.state == FT_BT_HOST_READY)
    {
        rt_kprintf("BT address: %02X:%02X:%02X:%02X:%02X:%02X name=%s\r\n",
                   g_bt.local_address[0], g_bt.local_address[1],
                   g_bt.local_address[2], g_bt.local_address[3],
                   g_bt.local_address[4], g_bt.local_address[5], FT_BT_DEVICE_NAME);
    }
    return 0;
}
MSH_CMD_EXPORT_ALIAS(ft_bt_status_command, bt_status,
                     Show AIROC host stack scan and advertising state.);

static int ft_bt_scan_command(int argc, char **argv)
{
    wiced_result_t result;
    (void)argc;
    (void)argv;
    result = ft_bt_start_scan(true);
    rt_kprintf("bt_scan: %d\r\n", (int)result);
    return result == WICED_BT_PENDING || result == WICED_BT_SUCCESS ? 0 : -RT_ERROR;
}
MSH_CMD_EXPORT_ALIAS(ft_bt_scan_command, bt_scan,
                     Start an 8 second active BLE scan and clear old results.);

static int ft_bt_scan_stop_command(int argc, char **argv)
{
    wiced_result_t result;
    (void)argc;
    (void)argv;
    result = wiced_bt_ble_scan(BTM_BLE_SCAN_TYPE_NONE, WICED_TRUE, ft_bt_scan_callback);
    rt_kprintf("bt_scan_stop: %d\r\n", (int)result);
    return result == WICED_BT_SUCCESS || result == WICED_BT_PENDING ? 0 : -RT_ERROR;
}
MSH_CMD_EXPORT_ALIAS(ft_bt_scan_stop_command, bt_scan_stop, Stop the active BLE scan.);

static int ft_bt_devices_command(int argc, char **argv)
{
    uint32_t index;
    ft_bt_scan_result_t snapshot[FT_BT_MAX_SCAN_RESULTS];
    uint32_t count;
    (void)argc;
    (void)argv;

    ft_bt_lock();
    count = g_bt.unique_devices;
    memcpy(snapshot, g_bt.devices, sizeof(snapshot));
    ft_bt_unlock();

    rt_kprintf("BLE devices: %lu\r\n", (unsigned long)count);
    for (index = 0U; index < count; ++index)
    {
        rt_kprintf("%2lu  %02X:%02X:%02X:%02X:%02X:%02X  RSSI=%d type=%u  %s\r\n",
                   (unsigned long)index,
                   snapshot[index].address[0], snapshot[index].address[1],
                   snapshot[index].address[2], snapshot[index].address[3],
                   snapshot[index].address[4], snapshot[index].address[5],
                   (int)snapshot[index].rssi,
                   snapshot[index].address_type,
                   snapshot[index].name[0] != '\0' ? snapshot[index].name : "(unnamed)");
    }
    return 0;
}
MSH_CMD_EXPORT_ALIAS(ft_bt_devices_command, bt_devices, Show unique BLE scan results.);

static int ft_bt_adv_command(int argc, char **argv)
{
    bool enable;
    wiced_result_t result;

    if (argc != 2)
    {
        rt_kprintf("usage: bt_adv on|off\r\n");
        return -RT_EINVAL;
    }
    if (strcmp(argv[1], "on") == 0)
    {
        enable = true;
    }
    else if (strcmp(argv[1], "off") == 0)
    {
        enable = false;
    }
    else
    {
        rt_kprintf("usage: bt_adv on|off\r\n");
        return -RT_EINVAL;
    }

    result = ft_bt_set_advertising(enable);
    rt_kprintf("bt_adv %s: %d\r\n", enable ? "on" : "off", (int)result);
    return result == WICED_BT_SUCCESS ? 0 : -RT_ERROR;
}
MSH_CMD_EXPORT_ALIAS(ft_bt_adv_command, bt_adv, Enable or disable FeatherTalk BLE advertising.);

#else

#include "feathertalk_bt_controller.h"

rt_err_t feathertalk_bt_host_start(void)
{
    rt_kprintf("[BT] AIROC host assets missing; using controller diagnostics only\r\n");
    return feathertalk_bt_controller_start();
}

#endif
