#ifndef FEATHERTALK_IPC_PROTOCOL_H
#define FEATHERTALK_IPC_PROTOCOL_H

#include <stdint.h>

#include <feathertalk/version.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FEATHERTALK_IPC_MAGIC 0x46544950UL /* "FTIP" */

typedef enum
{
    FEATHERTALK_IPC_MSG_INVALID   = 0,
    FEATHERTALK_IPC_MSG_HELLO     = 1,
    FEATHERTALK_IPC_MSG_HEARTBEAT = 2,
    FEATHERTALK_IPC_MSG_COMMAND   = 3,
    FEATHERTALK_IPC_MSG_EVENT     = 4
} feathertalk_ipc_message_id_t;

typedef struct
{
    uint32_t magic;
    uint16_t abi_version;
    uint16_t message_id;
    uint32_t payload_size;
    uint32_t sequence;
} feathertalk_ipc_header_t;

#ifdef __cplusplus
}
#endif

#endif /* FEATHERTALK_IPC_PROTOCOL_H */
