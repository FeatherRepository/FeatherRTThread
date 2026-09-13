/* bt_le_audio.h - M8.1 LE Audio BAP Unicast Server (M33) 对 bt_main 的接口 */
#ifndef FT_BT_LE_AUDIO_H
#define FT_BT_LE_AUDIO_H

#include <rtthread.h>
#include <btstack_defines.h>

/* bt_main 建栈时调用一次: 注册 HCI 事件/ISO 包处理器, ASE 全部复位为 Idle */
void ft_le_audio_init(void);

/* bt_main 的 att_read_callback 转发; 返回 0xFFFF = 非本模块句柄 */
uint16_t ft_le_audio_att_read(rt_uint16_t att_handle, rt_uint16_t offset,
                              rt_uint8_t *buffer, rt_uint16_t buffer_size);

/* bt_main 的 att_write_callback 转发; 返回 -2 = 非本模块句柄 */
int ft_le_audio_att_write(hci_con_handle_t con_handle, rt_uint16_t att_handle,
                          const rt_uint8_t *buffer, rt_uint16_t buffer_size);

#endif /* FT_BT_LE_AUDIO_H */

/* M8.2: Auracast 广播源 (bt_le_audio_broadcast.c) */
void ft_le_audio_broadcast_init(void);
int  ft_le_audio_broadcast_start(void);
int  ft_le_audio_broadcast_stop(void);
