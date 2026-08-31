#ifndef FEATHERTALK_M33_IPC_H
#define FEATHERTALK_M33_IPC_H

#include <rtthread.h>

int feathertalk_ipc_start(void);

/* Delivers a numeric event code to the M55 console (MSG_EVENT frame). */
void feathertalk_ipc_send_event(rt_uint32_t code);

/* 1 once the M55 responder is online (HELLO handshake done). */
int feathertalk_ipc_peer_online(void);

#endif /* FEATHERTALK_M33_IPC_H */
