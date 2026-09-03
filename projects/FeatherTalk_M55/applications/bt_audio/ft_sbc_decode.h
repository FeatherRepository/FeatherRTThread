/* ft_sbc_decode.h - A0-2 SBC 解码消费链接口 (M55 侧)
 *
 * 由 audio_link_m55.c 的 consumer 调用:
 *  - fmt_gen 变化且非 0 -> ft_sbc_stream_begin() 复位解码器与统计
 *  - 之后 ring 读出的字节流 -> ft_sbc_feed() (帧化 SBC: 2B LE length + 帧)
 */
#ifndef FT_SBC_DECODE_H
#define FT_SBC_DECODE_H

#include <rtthread.h>

void ft_sbc_stream_begin(void);
void ft_sbc_feed(const rt_uint8_t *data, rt_uint32_t len);

#endif /* FT_SBC_DECODE_H */
