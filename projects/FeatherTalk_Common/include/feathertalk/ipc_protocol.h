#ifndef FEATHERTALK_IPC_PROTOCOL_H
#define FEATHERTALK_IPC_PROTOCOL_H

#include <stdint.h>

#include <feathertalk/version.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FEATHERTALK_IPC_WIRE_BYTES 16U

typedef enum
{
    FEATHERTALK_IPC_MSG_INVALID       = 0,
    FEATHERTALK_IPC_MSG_HELLO         = 1,
    FEATHERTALK_IPC_MSG_HELLO_ACK     = 2,
    FEATHERTALK_IPC_MSG_HEARTBEAT     = 3,
    FEATHERTALK_IPC_MSG_HEARTBEAT_ACK = 4,
    FEATHERTALK_IPC_MSG_COMMAND       = 5,
    FEATHERTALK_IPC_MSG_EVENT         = 6,
    FEATHERTALK_IPC_MSG_SYSTEM_STATUS = 7,
    FEATHERTALK_IPC_MSG_QUICK_STATUS  = 8,
    FEATHERTALK_IPC_MSG_QUICK_COMMAND = 9
} feathertalk_ipc_message_id_t;

typedef enum
{
    FEATHERTALK_STATUS_M33_READY   = (1UL << 0),
    FEATHERTALK_STATUS_M55_READY   = (1UL << 1),
    FEATHERTALK_STATUS_IPC_READY   = (1UL << 2),
    FEATHERTALK_STATUS_LVGL_READY  = (1UL << 3),
    FEATHERTALK_STATUS_PEER_ONLINE = (1UL << 4)
} feathertalk_status_flag_t;

typedef enum
{
    FEATHERTALK_NETWORK_UNAVAILABLE  = 0,
    FEATHERTALK_NETWORK_DISCONNECTED = 1,
    FEATHERTALK_NETWORK_CONNECTING   = 2,
    FEATHERTALK_NETWORK_CONNECTED    = 3
} feathertalk_network_state_t;

typedef enum
{
    FEATHERTALK_SYSTEM_TIME_VALID      = (1U << 0),
    FEATHERTALK_SYSTEM_RTC_PRESENT     = (1U << 1),
    FEATHERTALK_SYSTEM_POWER_PRESENT   = (1U << 2),
    FEATHERTALK_SYSTEM_BATTERY_VALID   = (1U << 3),
    FEATHERTALK_SYSTEM_CHARGING        = (1U << 4),
    FEATHERTALK_SYSTEM_NETWORK_PRESENT = (1U << 5)
} feathertalk_system_flag_t;

#define FEATHERTALK_SYSTEM_VALUE_UNKNOWN 0xFFU

typedef enum
{
    FEATHERTALK_QUICK_WIFI       = 0,
    FEATHERTALK_QUICK_BLUETOOTH  = 1,
    FEATHERTALK_QUICK_BRIGHTNESS = 2,
    FEATHERTALK_QUICK_ROTATION   = 3,
    FEATHERTALK_QUICK_COUNT
} feathertalk_quick_control_t;

typedef enum
{
    FEATHERTALK_QUICK_CAP_WIFI       = (1U << FEATHERTALK_QUICK_WIFI),
    FEATHERTALK_QUICK_CAP_BLUETOOTH  = (1U << FEATHERTALK_QUICK_BLUETOOTH),
    FEATHERTALK_QUICK_CAP_BRIGHTNESS = (1U << FEATHERTALK_QUICK_BRIGHTNESS),
    FEATHERTALK_QUICK_CAP_ROTATION   = (1U << FEATHERTALK_QUICK_ROTATION)
} feathertalk_quick_capability_t;

typedef enum
{
    FEATHERTALK_QUICK_RESULT_NONE        = 0,
    FEATHERTALK_QUICK_RESULT_OK          = 1,
    FEATHERTALK_QUICK_RESULT_UNAVAILABLE = 2,
    FEATHERTALK_QUICK_RESULT_INVALID     = 3,
    FEATHERTALK_QUICK_RESULT_FAILED      = 4
} feathertalk_quick_result_t;

typedef struct
{
    uint16_t abi_version;
    uint16_t message_id;
    uint32_t sequence;
    uint32_t uptime_ms;
    uint32_t status;
} feathertalk_ipc_message_t;

typedef struct
{
    uint16_t abi_version;
    uint16_t message_id;
    uint32_t sequence;
    uint32_t unix_time;
    uint8_t battery_percent;
    uint8_t network_state;
    uint8_t signal_percent;
    uint8_t flags;
} feathertalk_ipc_system_status_t;

typedef struct
{
    uint16_t abi_version;
    uint16_t message_id;
    uint32_t sequence;
    uint8_t capabilities;
    uint8_t enabled;
    uint8_t connected;
    uint8_t wifi_signal_percent;
    uint8_t brightness_percent;
    uint8_t rotation;
    uint8_t last_control;
    uint8_t result;
} feathertalk_ipc_quick_status_t;

typedef struct
{
    uint16_t abi_version;
    uint16_t message_id;
    uint32_t sequence;
    uint8_t control;
    uint8_t value;
    uint8_t reserved[6];
} feathertalk_ipc_quick_command_t;

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(feathertalk_ipc_message_t) == FEATHERTALK_IPC_WIRE_BYTES,
               "FeatherTalk IPC payload must fit the PSoC E84 IPC frame");
_Static_assert(sizeof(feathertalk_ipc_system_status_t) == FEATHERTALK_IPC_WIRE_BYTES,
               "FeatherTalk system status must fit the PSoC E84 IPC frame");
_Static_assert(sizeof(feathertalk_ipc_quick_status_t) == FEATHERTALK_IPC_WIRE_BYTES,
               "FeatherTalk quick status must fit the PSoC E84 IPC frame");
_Static_assert(sizeof(feathertalk_ipc_quick_command_t) == FEATHERTALK_IPC_WIRE_BYTES,
               "FeatherTalk quick command must fit the PSoC E84 IPC frame");
#endif

#ifdef __cplusplus
}
#endif

#endif /* FEATHERTALK_IPC_PROTOCOL_H */
