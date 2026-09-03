#ifndef FEATHERTALK_M33_IPC_H
#define FEATHERTALK_M33_IPC_H

#include <rtthread.h>

int feathertalk_ipc_start(void);

/* Delivers a numeric event code to the M55 console (MSG_EVENT frame). */
void feathertalk_ipc_send_event(rt_uint32_t code);

/* 1 once the M55 responder is online (HELLO handshake done). */
int feathertalk_ipc_peer_online(void);

/* A0 音频门铃: 投递"音频 ring 有新数据"通知 (MSG_AUDIO_DB)。
 * 投递方不直接写 pipe (IPC 线程是唯一写者), 这里只登记最新快照,
 * 由 IPC 线程合并后发出——纯通知可安全合并。 */
void feathertalk_ipc_send_audio_db(rt_uint32_t wr_snapshot, rt_uint8_t flags);

#endif /* FEATHERTALK_M33_IPC_H */
