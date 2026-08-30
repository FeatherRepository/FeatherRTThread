/*
 * Copyright (c) 2026 FeatherTalk contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "feathertalk_bt_controller.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <rtdevice.h>
#include <finsh.h>

#include "board.h"
#include "cy_scb_uart.h"
#include "cycfg_peripherals.h"
#include "cycfg_pins.h"
#include "mtb_hal_uart.h"

#define FT_BT_MODULE_POWER_PIN          GET_PIN(16, 3)

#define FT_BT_DOWNLOAD_BAUD             3000000UL
#define FT_BT_DEFAULT_BAUD              115200UL
#define FT_BT_COMMAND_TIMEOUT_MS        3000UL
#define FT_BT_CTS_TIMEOUT_MS            2000UL
#define FT_BT_TX_TIMEOUT_MS             1000UL
#define FT_BT_AUTOBAUD_RESET_MS         100UL
#define FT_BT_LAUNCH_DELAY_MS           250UL
#define FT_BT_BAUD_SETTLE_MS            100UL

#define HCI_H4_COMMAND_PACKET           0x01U
#define HCI_H4_ACL_PACKET               0x02U
#define HCI_H4_SCO_PACKET               0x03U
#define HCI_H4_EVENT_PACKET             0x04U
#define HCI_H4_ISO_PACKET               0x05U

#define HCI_EVENT_COMMAND_COMPLETE      0x0EU
#define HCI_EVENT_COMMAND_STATUS        0x0FU

#define HCI_RESET                       0x0C03U
#define HCI_READ_LOCAL_VERSION          0x1001U
#define HCI_READ_BD_ADDR                0x1009U
#define HCI_VSC_UPDATE_BAUDRATE         0xFC18U
#define HCI_VSC_WRITE_RAM               0xFC4CU
#define HCI_VSC_LAUNCH_RAM              0xFC4EU

#define FT_BT_HCI_EVENT_MAX             255U
#define FT_BT_HCI_COMMAND_MAX           259U

typedef enum
{
    FT_BT_STATE_OFF = 0,
    FT_BT_STATE_STARTING,
    FT_BT_STATE_DOWNLOADING,
    FT_BT_STATE_READY,
    FT_BT_STATE_FAILED,
    FT_BT_STATE_FW_MISSING
} ft_bt_state_t;

typedef enum
{
    FT_BT_OK = 0,
    FT_BT_ERR_BUSY = -1,
    FT_BT_ERR_FW_MISSING = -2,
    FT_BT_ERR_UART_INIT = -3,
    FT_BT_ERR_UART_BAUD = -4,
    FT_BT_ERR_CTS_TIMEOUT = -5,
    FT_BT_ERR_TX_TIMEOUT = -6,
    FT_BT_ERR_RX_TIMEOUT = -7,
    FT_BT_ERR_HCI_STATUS = -8,
    FT_BT_ERR_HCD_FORMAT = -9,
    FT_BT_ERR_HCD_LAUNCH = -10,
    FT_BT_ERR_THREAD = -11
} ft_bt_result_t;

typedef struct
{
    volatile ft_bt_state_t state;
    volatile int last_error;
    uint32_t baud;
    uint32_t hcd_records;
    uint32_t hcd_bytes;
    uint8_t bd_addr[6];
    uint8_t hci_version;
    uint16_t hci_revision;
    uint8_t lmp_version;
    uint16_t manufacturer;
    uint16_t lmp_subversion;
} ft_bt_status_t;

static ft_bt_status_t g_bt_status;
static struct rt_mutex g_bt_lock;
static bool g_bt_lock_ready;
static mtb_hal_uart_t g_bt_uart;
static cy_stc_scb_uart_context_t g_bt_uart_context;

#ifdef FEATHERTALK_BT_FW_PRESENT
extern const char brcm_patch_version[];
extern const uint8_t brcm_patchram_buf[];
extern const int brcm_patch_ram_length;
#endif

static const char *ft_bt_state_name(ft_bt_state_t state)
{
    switch (state)
    {
    case FT_BT_STATE_OFF:
        return "off";
    case FT_BT_STATE_STARTING:
        return "starting";
    case FT_BT_STATE_DOWNLOADING:
        return "downloading";
    case FT_BT_STATE_READY:
        return "ready";
    case FT_BT_STATE_FAILED:
        return "failed";
    case FT_BT_STATE_FW_MISSING:
        return "firmware-missing";
    default:
        return "unknown";
    }
}

static bool ft_bt_timeout_expired(rt_tick_t start, uint32_t timeout_ms)
{
    return (rt_tick_get() - start) >= rt_tick_from_millisecond(timeout_ms);
}

static void ft_bt_set_error(ft_bt_result_t error)
{
    g_bt_status.last_error = error;
    g_bt_status.state = FT_BT_STATE_FAILED;
}

static int ft_bt_uart_set_baud(uint32_t baud)
{
    uint32_t actual_baud = 0;
    cy_rslt_t result;

    result = mtb_hal_uart_set_baud(&g_bt_uart, baud, &actual_baud);
    if (result != CY_RSLT_SUCCESS)
    {
        rt_kprintf("[BT] set baud %lu failed: 0x%08lx\r\n",
                   (unsigned long)baud,
                   (unsigned long)result);
        return FT_BT_ERR_UART_BAUD;
    }

    g_bt_status.baud = actual_baud;
    rt_kprintf("[BT] HCI UART baud %lu (requested %lu)\r\n",
               (unsigned long)actual_baud,
               (unsigned long)baud);
    return FT_BT_OK;
}

static int ft_bt_uart_init(uint32_t baud)
{
    cy_en_scb_uart_status_t uart_status;
    cy_rslt_t hal_status;

    Cy_SCB_UART_Disable(CYBSP_BT_UART_HW, &g_bt_uart_context);
    Cy_SCB_UART_DeInit(CYBSP_BT_UART_HW);
    memset(&g_bt_uart_context, 0, sizeof(g_bt_uart_context));

    uart_status = Cy_SCB_UART_Init(CYBSP_BT_UART_HW,
                                  &CYBSP_BT_UART_config,
                                  &g_bt_uart_context);
    if (uart_status != CY_SCB_UART_SUCCESS)
    {
        rt_kprintf("[BT] SCB4 init failed: %d\r\n", (int)uart_status);
        return FT_BT_ERR_UART_INIT;
    }

    Cy_SCB_UART_Enable(CYBSP_BT_UART_HW);
    hal_status = mtb_hal_uart_setup(&g_bt_uart,
                                    &CYBSP_BT_UART_hal_config,
                                    &g_bt_uart_context,
                                    NULL);
    if (hal_status != CY_RSLT_SUCCESS)
    {
        rt_kprintf("[BT] HAL UART setup failed: 0x%08lx\r\n",
                   (unsigned long)hal_status);
        return FT_BT_ERR_UART_INIT;
    }

    Cy_SCB_UART_ClearRxFifo(CYBSP_BT_UART_HW);
    Cy_SCB_UART_ClearTxFifo(CYBSP_BT_UART_HW);
    Cy_SCB_UART_EnableCts(CYBSP_BT_UART_HW);
    return ft_bt_uart_set_baud(baud);
}

static int ft_bt_uart_write(const uint8_t *data, uint32_t length)
{
    rt_tick_t start = rt_tick_get();
    uint32_t offset = 0;

    while (offset < length)
    {
        offset += Cy_SCB_UART_PutArray(CYBSP_BT_UART_HW,
                                      (void *)(data + offset),
                                      length - offset);
        if (offset < length)
        {
            if (ft_bt_timeout_expired(start, FT_BT_TX_TIMEOUT_MS))
            {
                return FT_BT_ERR_TX_TIMEOUT;
            }
            rt_thread_yield();
        }
    }

    while (!Cy_SCB_UART_IsTxComplete(CYBSP_BT_UART_HW))
    {
        if (ft_bt_timeout_expired(start, FT_BT_TX_TIMEOUT_MS))
        {
            return FT_BT_ERR_TX_TIMEOUT;
        }
        rt_thread_yield();
    }
    return FT_BT_OK;
}

static int ft_bt_uart_read_byte(uint8_t *value, uint32_t timeout_ms)
{
    rt_tick_t start = rt_tick_get();

    while (Cy_SCB_UART_GetNumInRxFifo(CYBSP_BT_UART_HW) == 0U)
    {
        if (ft_bt_timeout_expired(start, timeout_ms))
        {
            return FT_BT_ERR_RX_TIMEOUT;
        }
        rt_thread_mdelay(1);
    }

    *value = (uint8_t)Cy_SCB_UART_Get(CYBSP_BT_UART_HW);
    return FT_BT_OK;
}

static int ft_bt_uart_discard(uint32_t length, uint32_t timeout_ms)
{
    uint8_t ignored;
    uint32_t i;

    for (i = 0; i < length; ++i)
    {
        int result = ft_bt_uart_read_byte(&ignored, timeout_ms);
        if (result != FT_BT_OK)
        {
            return result;
        }
    }
    return FT_BT_OK;
}

static int ft_bt_read_hci_event(uint16_t expected_opcode,
                                uint8_t *return_params,
                                uint8_t *return_length,
                                uint32_t timeout_ms)
{
    uint8_t packet_type;
    uint8_t event_code;
    uint8_t event_length;
    uint8_t payload[FT_BT_HCI_EVENT_MAX];
    rt_tick_t start = rt_tick_get();

    while (!ft_bt_timeout_expired(start, timeout_ms))
    {
        uint32_t remaining_ms = timeout_ms;
        rt_tick_t elapsed = rt_tick_get() - start;
        rt_tick_t timeout_ticks = rt_tick_from_millisecond(timeout_ms);

        if (elapsed < timeout_ticks)
        {
            remaining_ms = ((timeout_ticks - elapsed) * 1000U) / RT_TICK_PER_SECOND + 1U;
        }

        if (ft_bt_uart_read_byte(&packet_type, remaining_ms) != FT_BT_OK)
        {
            break;
        }

        if (packet_type == HCI_H4_EVENT_PACKET)
        {
            uint16_t opcode;
            uint8_t status;
            uint32_t i;

            if (ft_bt_uart_read_byte(&event_code, remaining_ms) != FT_BT_OK ||
                ft_bt_uart_read_byte(&event_length, remaining_ms) != FT_BT_OK)
            {
                break;
            }
            for (i = 0; i < event_length; ++i)
            {
                if (ft_bt_uart_read_byte(&payload[i], remaining_ms) != FT_BT_OK)
                {
                    return FT_BT_ERR_RX_TIMEOUT;
                }
            }

            if (event_code == HCI_EVENT_COMMAND_COMPLETE && event_length >= 4U)
            {
                opcode = (uint16_t)payload[1] | ((uint16_t)payload[2] << 8);
                if (opcode != expected_opcode)
                {
                    continue;
                }

                status = payload[3];
                if (return_params != NULL && return_length != NULL)
                {
                    uint8_t available = (uint8_t)(event_length - 3U);
                    uint8_t copy_length = *return_length < available ? *return_length : available;
                    memcpy(return_params, &payload[3], copy_length);
                    *return_length = copy_length;
                }
                return status == 0U ? FT_BT_OK : FT_BT_ERR_HCI_STATUS;
            }

            if (event_code == HCI_EVENT_COMMAND_STATUS && event_length >= 4U)
            {
                opcode = (uint16_t)payload[2] | ((uint16_t)payload[3] << 8);
                if (opcode != expected_opcode)
                {
                    continue;
                }
                status = payload[0];
                return status == 0U ? FT_BT_OK : FT_BT_ERR_HCI_STATUS;
            }
        }
        else if (packet_type == HCI_H4_ACL_PACKET || packet_type == HCI_H4_ISO_PACKET)
        {
            uint8_t header[4];
            uint32_t length;
            uint32_t i;

            for (i = 0; i < sizeof(header); ++i)
            {
                if (ft_bt_uart_read_byte(&header[i], remaining_ms) != FT_BT_OK)
                {
                    return FT_BT_ERR_RX_TIMEOUT;
                }
            }
            length = (uint32_t)header[2] | ((uint32_t)header[3] << 8);
            if (ft_bt_uart_discard(length, remaining_ms) != FT_BT_OK)
            {
                return FT_BT_ERR_RX_TIMEOUT;
            }
        }
        else if (packet_type == HCI_H4_SCO_PACKET)
        {
            uint8_t header[3];
            uint32_t i;

            for (i = 0; i < sizeof(header); ++i)
            {
                if (ft_bt_uart_read_byte(&header[i], remaining_ms) != FT_BT_OK)
                {
                    return FT_BT_ERR_RX_TIMEOUT;
                }
            }
            if (ft_bt_uart_discard(header[2], remaining_ms) != FT_BT_OK)
            {
                return FT_BT_ERR_RX_TIMEOUT;
            }
        }
    }

    return FT_BT_ERR_RX_TIMEOUT;
}

static int ft_bt_hci_command(uint16_t opcode,
                             const uint8_t *parameters,
                             uint8_t parameter_length,
                             uint8_t *return_params,
                             uint8_t *return_length)
{
    uint8_t command[FT_BT_HCI_COMMAND_MAX];
    int result;

    command[0] = HCI_H4_COMMAND_PACKET;
    command[1] = (uint8_t)(opcode & 0xFFU);
    command[2] = (uint8_t)(opcode >> 8);
    command[3] = parameter_length;
    if (parameter_length > 0U)
    {
        memcpy(&command[4], parameters, parameter_length);
    }

    result = ft_bt_uart_write(command, (uint32_t)parameter_length + 4U);
    if (result != FT_BT_OK)
    {
        return result;
    }

    return ft_bt_read_hci_event(opcode,
                                return_params,
                                return_length,
                                FT_BT_COMMAND_TIMEOUT_MS);
}

static int ft_bt_wait_cts_low(void)
{
    rt_tick_t start = rt_tick_get();

    while (Cy_GPIO_Read(CYBSP_BT_UART_CTS_PORT, CYBSP_BT_UART_CTS_PIN) != 0U)
    {
        if (ft_bt_timeout_expired(start, FT_BT_CTS_TIMEOUT_MS))
        {
            return FT_BT_ERR_CTS_TIMEOUT;
        }
        rt_thread_mdelay(10);
    }
    return FT_BT_OK;
}

static void ft_bt_prepare_autobaud(void)
{
    /* The carrier-board FET enables the shared 1.8 V/3.3 V radio rails. */
    rt_pin_mode(FT_BT_MODULE_POWER_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(FT_BT_MODULE_POWER_PIN, PIN_HIGH);
    rt_thread_mdelay(20);

    /* Keep controller sleep disabled during initial validation. */
    Cy_GPIO_Pin_FastInit(CYBSP_BT_DEVICE_WAKE_PORT,
                         CYBSP_BT_DEVICE_WAKE_PIN,
                         CY_GPIO_DM_STRONG,
                         0U,
                         HSIOM_SEL_GPIO);

    /* CYW55500 enters autobaud mode when host RTS is held low over REG_ON. */
    Cy_GPIO_Pin_FastInit(CYBSP_BT_UART_RTS_PORT,
                         CYBSP_BT_UART_RTS_PIN,
                         CY_GPIO_DM_STRONG,
                         0U,
                         HSIOM_SEL_GPIO);
    Cy_GPIO_Pin_FastInit(CYBSP_BT_POWER_PORT,
                         CYBSP_BT_POWER_PIN,
                         CY_GPIO_DM_PULLUP,
                         1U,
                         HSIOM_SEL_GPIO);
    Cy_GPIO_Write(CYBSP_BT_POWER_PORT, CYBSP_BT_POWER_PIN, 0U);
    rt_thread_mdelay(FT_BT_AUTOBAUD_RESET_MS);
    Cy_GPIO_Write(CYBSP_BT_POWER_PORT, CYBSP_BT_POWER_PIN, 1U);
    rt_thread_mdelay(FT_BT_AUTOBAUD_RESET_MS);

    /* Return all HCI pins to their BSP mux before enabling SCB4. */
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
}

#ifdef FEATHERTALK_BT_FW_PRESENT
static int ft_bt_download_hcd(void)
{
    uint32_t offset = 0;
    uint32_t records = 0;
    bool launched = false;

    if (brcm_patch_ram_length <= 0)
    {
        return FT_BT_ERR_HCD_FORMAT;
    }

    g_bt_status.state = FT_BT_STATE_DOWNLOADING;
    g_bt_status.hcd_records = 0;
    g_bt_status.hcd_bytes = 0;
    rt_kprintf("[BT] download %s (%lu bytes)\r\n",
               brcm_patch_version,
               (unsigned long)brcm_patch_ram_length);

    while (offset < (uint32_t)brcm_patch_ram_length)
    {
        uint16_t opcode;
        uint8_t parameter_length;
        int result;

        if (offset + 3U > (uint32_t)brcm_patch_ram_length)
        {
            return FT_BT_ERR_HCD_FORMAT;
        }

        opcode = (uint16_t)brcm_patchram_buf[offset] |
                 ((uint16_t)brcm_patchram_buf[offset + 1U] << 8);
        parameter_length = brcm_patchram_buf[offset + 2U];
        if (offset + 3U + parameter_length > (uint32_t)brcm_patch_ram_length)
        {
            return FT_BT_ERR_HCD_FORMAT;
        }

        if (opcode != HCI_VSC_WRITE_RAM && opcode != HCI_VSC_LAUNCH_RAM)
        {
            rt_kprintf("[BT] unexpected HCD opcode 0x%04x at %lu\r\n",
                       opcode,
                       (unsigned long)offset);
        }

        result = ft_bt_hci_command(opcode,
                                   &brcm_patchram_buf[offset + 3U],
                                   parameter_length,
                                   NULL,
                                   NULL);
        if (result != FT_BT_OK)
        {
            rt_kprintf("[BT] HCD record %lu opcode 0x%04x failed: %d\r\n",
                       (unsigned long)records,
                       opcode,
                       result);
            return result;
        }

        offset += 3U + parameter_length;
        ++records;
        g_bt_status.hcd_records = records;
        g_bt_status.hcd_bytes = offset;

        if ((records & 0x7FU) == 0U)
        {
            rt_kprintf("[BT] HCD %lu/%lu bytes, %lu records\r\n",
                       (unsigned long)offset,
                       (unsigned long)brcm_patch_ram_length,
                       (unsigned long)records);
        }

        if (opcode == HCI_VSC_LAUNCH_RAM)
        {
            launched = true;
            break;
        }
    }

    if (!launched || offset != (uint32_t)brcm_patch_ram_length)
    {
        return FT_BT_ERR_HCD_LAUNCH;
    }

    rt_kprintf("[BT] HCD complete: %lu records\r\n", (unsigned long)records);
    rt_thread_mdelay(FT_BT_LAUNCH_DELAY_MS);
    return FT_BT_OK;
}
#endif

static int ft_bt_switch_to_feature_baud(void)
{
    uint8_t baud_parameters[6];
    int result;

    result = ft_bt_uart_set_baud(FT_BT_DEFAULT_BAUD);
    if (result != FT_BT_OK)
    {
        return result;
    }
    rt_thread_mdelay(FT_BT_BAUD_SETTLE_MS);

    baud_parameters[0] = 0U;
    baud_parameters[1] = 0U;
    baud_parameters[2] = (uint8_t)(FT_BT_DOWNLOAD_BAUD & 0xFFU);
    baud_parameters[3] = (uint8_t)((FT_BT_DOWNLOAD_BAUD >> 8) & 0xFFU);
    baud_parameters[4] = (uint8_t)((FT_BT_DOWNLOAD_BAUD >> 16) & 0xFFU);
    baud_parameters[5] = (uint8_t)((FT_BT_DOWNLOAD_BAUD >> 24) & 0xFFU);

    result = ft_bt_hci_command(HCI_VSC_UPDATE_BAUDRATE,
                               baud_parameters,
                               sizeof(baud_parameters),
                               NULL,
                               NULL);
    if (result != FT_BT_OK)
    {
        return result;
    }

    rt_thread_mdelay(FT_BT_BAUD_SETTLE_MS);
    result = ft_bt_uart_set_baud(FT_BT_DOWNLOAD_BAUD);
    rt_thread_mdelay(FT_BT_BAUD_SETTLE_MS);
    return result;
}

static int ft_bt_read_identity(void)
{
    uint8_t response[16];
    uint8_t response_length;
    int result;

    response_length = sizeof(response);
    result = ft_bt_hci_command(HCI_READ_LOCAL_VERSION,
                               NULL,
                               0U,
                               response,
                               &response_length);
    if (result != FT_BT_OK || response_length < 9U)
    {
        return result != FT_BT_OK ? result : FT_BT_ERR_HCI_STATUS;
    }

    g_bt_status.hci_version = response[1];
    g_bt_status.hci_revision = (uint16_t)response[2] | ((uint16_t)response[3] << 8);
    g_bt_status.lmp_version = response[4];
    g_bt_status.manufacturer = (uint16_t)response[5] | ((uint16_t)response[6] << 8);
    g_bt_status.lmp_subversion = (uint16_t)response[7] | ((uint16_t)response[8] << 8);

    response_length = sizeof(response);
    result = ft_bt_hci_command(HCI_READ_BD_ADDR,
                               NULL,
                               0U,
                               response,
                               &response_length);
    if (result != FT_BT_OK || response_length < 7U)
    {
        return result != FT_BT_OK ? result : FT_BT_ERR_HCI_STATUS;
    }
    memcpy(g_bt_status.bd_addr, &response[1], sizeof(g_bt_status.bd_addr));
    return FT_BT_OK;
}

static void ft_bt_print_status(void)
{
    rt_kprintf("[BT] state=%s error=%d baud=%lu fw_records=%lu fw_bytes=%lu\r\n",
               ft_bt_state_name(g_bt_status.state),
               g_bt_status.last_error,
               (unsigned long)g_bt_status.baud,
               (unsigned long)g_bt_status.hcd_records,
               (unsigned long)g_bt_status.hcd_bytes);

#ifdef FEATHERTALK_BT_FW_PRESENT
    rt_kprintf("[BT] firmware=%s\r\n", brcm_patch_version);
#else
    rt_kprintf("[BT] firmware=not linked\r\n");
#endif

    if (g_bt_status.state == FT_BT_STATE_READY)
    {
        rt_kprintf("[BT] address=%02X:%02X:%02X:%02X:%02X:%02X\r\n",
                   g_bt_status.bd_addr[5], g_bt_status.bd_addr[4],
                   g_bt_status.bd_addr[3], g_bt_status.bd_addr[2],
                   g_bt_status.bd_addr[1], g_bt_status.bd_addr[0]);
        rt_kprintf("[BT] HCI=%u rev=0x%04X LMP=%u sub=0x%04X manufacturer=%u\r\n",
                   g_bt_status.hci_version,
                   g_bt_status.hci_revision,
                   g_bt_status.lmp_version,
                   g_bt_status.lmp_subversion,
                   g_bt_status.manufacturer);
    }
}

static int ft_bt_bringup(void)
{
    int result;

#ifndef FEATHERTALK_BT_FW_PRESENT
    g_bt_status.state = FT_BT_STATE_FW_MISSING;
    g_bt_status.last_error = FT_BT_ERR_FW_MISSING;
    rt_kprintf("[BT] official CYW55500A1 firmware is not linked\r\n");
    rt_kprintf("[BT] see applications/bluetooth/README.md\r\n");
    return FT_BT_ERR_FW_MISSING;
#else
    if (!g_bt_lock_ready || rt_mutex_take(&g_bt_lock, 0) != RT_EOK)
    {
        return FT_BT_ERR_BUSY;
    }

    memset(&g_bt_status, 0, sizeof(g_bt_status));
    g_bt_status.state = FT_BT_STATE_STARTING;
    rt_kprintf("[BT] CYW55500A1 controller bring-up start\r\n");

    ft_bt_prepare_autobaud();
    result = ft_bt_uart_init(FT_BT_DOWNLOAD_BAUD);
    if (result != FT_BT_OK)
    {
        goto failed;
    }

    result = ft_bt_wait_cts_low();
    if (result != FT_BT_OK)
    {
        rt_kprintf("[BT] CTS stayed high after autobaud reset\r\n");
        goto failed;
    }

    result = ft_bt_hci_command(HCI_RESET, NULL, 0U, NULL, NULL);
    if (result != FT_BT_OK)
    {
        rt_kprintf("[BT] initial HCI reset failed: %d\r\n", result);
        goto failed;
    }

    result = ft_bt_download_hcd();
    if (result != FT_BT_OK)
    {
        goto failed;
    }

    result = ft_bt_switch_to_feature_baud();
    if (result != FT_BT_OK)
    {
        rt_kprintf("[BT] feature baud switch failed: %d\r\n", result);
        goto failed;
    }

    result = ft_bt_hci_command(HCI_RESET, NULL, 0U, NULL, NULL);
    if (result != FT_BT_OK)
    {
        rt_kprintf("[BT] post-patch HCI reset failed: %d\r\n", result);
        goto failed;
    }

    result = ft_bt_read_identity();
    if (result != FT_BT_OK)
    {
        rt_kprintf("[BT] identity query failed: %d\r\n", result);
        goto failed;
    }

    g_bt_status.last_error = FT_BT_OK;
    g_bt_status.state = FT_BT_STATE_READY;
    rt_kprintf("[BT] controller ready\r\n");
    ft_bt_print_status();
    rt_mutex_release(&g_bt_lock);
    return FT_BT_OK;

failed:
    ft_bt_set_error((ft_bt_result_t)result);
    ft_bt_print_status();
    rt_mutex_release(&g_bt_lock);
    return result;
#endif
}

static void ft_bt_bringup_thread(void *parameter)
{
    (void)parameter;
    (void)ft_bt_bringup();
}

rt_err_t feathertalk_bt_controller_start(void)
{
    rt_thread_t thread;

    if (!g_bt_lock_ready)
    {
        if (rt_mutex_init(&g_bt_lock, "ft_bt", RT_IPC_FLAG_PRIO) != RT_EOK)
        {
            return -RT_ERROR;
        }
        g_bt_lock_ready = true;
    }

    thread = rt_thread_create("ft_bt_init",
                              ft_bt_bringup_thread,
                              NULL,
                              4096,
                              18,
                              10);
    if (thread == RT_NULL)
    {
        g_bt_status.last_error = FT_BT_ERR_THREAD;
        g_bt_status.state = FT_BT_STATE_FAILED;
        return -RT_ENOMEM;
    }

    return rt_thread_startup(thread);
}

static int ft_bt_init_command(int argc, char **argv)
{
    int result;
    (void)argc;
    (void)argv;

    result = ft_bt_bringup();
    rt_kprintf("bt_init: %d\r\n", result);
    return result;
}
MSH_CMD_EXPORT_ALIAS(ft_bt_init_command, bt_init,
                     Power-cycle CYW55500A1 then download firmware and query identity.);

static int ft_bt_status_command(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    ft_bt_print_status();
    return 0;
}
MSH_CMD_EXPORT_ALIAS(ft_bt_status_command, bt_status,
                     Show CYW55500A1 firmware HCI version and BD address.);
