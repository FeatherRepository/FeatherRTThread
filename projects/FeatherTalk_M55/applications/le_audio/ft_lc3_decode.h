/* ft_lc3_decode.h - M8.1 LE Audio LC3 解码消费链接口 (M55 侧)
 *
 * 由 audio_link_m55.c 的 consumer 在 ring flags bit0 (LE Audio 模式) 时调用,
 * 接口形态与 ft_sbc_decode.h 完全对齐:
 *  - fmt_gen 换代且 fmt_rate 非 0 -> ft_lc3_stream_begin() 复位解码器+开 sound0
 *  - fmt_gen 换代且 fmt_rate 为 0 -> ft_lc3_stream_end() 关流释放 sound0
 *  - 流活着期间 ring 读出的字节流 -> ft_lc3_feed()
 *    (帧化: 2B LE len + 1B 声道 0x01=L/0x02=R + LC3 帧字节)
 *  - 水位管理 -> ft_lc3_watermark_tick() (返回 RT_FALSE = 本轮暂缓消费)
 */
#ifndef FT_LC3_DECODE_H
#define FT_LC3_DECODE_H

#include <rtthread.h>

void ft_lc3_stream_begin(void);
void ft_lc3_stream_end(void);
void ft_lc3_feed(const rt_uint8_t *data, rt_uint32_t len);
rt_bool_t ft_lc3_watermark_tick(void);

#endif /* FT_LC3_DECODE_H */
