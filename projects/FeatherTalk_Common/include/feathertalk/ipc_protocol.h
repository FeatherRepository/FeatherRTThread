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
    FEATHERTALK_IPC_MSG_EVENT         = 6
} feathertalk_ipc_message_id_t;

typedef enum
{
    FEATHERTALK_STATUS_M33_READY   = (1UL << 0),
    FEATHERTALK_STATUS_M55_READY   = (1UL << 1),
    FEATHERTALK_STATUS_IPC_READY   = (1UL << 2),
    FEATHERTALK_STATUS_LVGL_READY  = (1UL << 3),
    FEATHERTALK_STATUS_PEER_ONLINE = (1UL << 4)
} feathertalk_status_flag_t;

typedef struct
{
    uint16_t abi_version;
    uint16_t message_id;
    uint32_t sequence;
    uint32_t uptime_ms;
    uint32_t status;
} feathertalk_ipc_message_t;

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(feathertalk_ipc_message_t) == FEATHERTALK_IPC_WIRE_BYTES,
               "FeatherTalk IPC payload must fit the PSoC E84 IPC frame");
#endif

#ifdef __cplusplus
}
#endif

#endif /* FEATHERTALK_IPC_PROTOCOL_H */
