/* ft_sbc_decode.h - A0-2/M4b SBC 解码消费链接口 (M55 侧)
 *
 * 由 audio_link_m55.c 的 consumer 调用:
 *  - fmt_gen 换代且 fmt_rate 非 0 -> ft_sbc_stream_begin() 复位解码器+开 sound0
 *  - fmt_gen 换代且 fmt_rate 为 0 -> ft_sbc_stream_end() 关流释放 sound0
 *  - 流活着期间 ring 读出的字节流 -> ft_sbc_feed() (帧化: 2B LE len + 载荷)
 */
#ifndef FT_SBC_DECODE_H
#define FT_SBC_DECODE_H

#include <rtthread.h>

void ft_sbc_stream_begin(void);
void ft_sbc_stream_end(void);
void ft_sbc_feed(const rt_uint8_t *data, rt_uint32_t len);

#endif /* FT_SBC_DECODE_H */
