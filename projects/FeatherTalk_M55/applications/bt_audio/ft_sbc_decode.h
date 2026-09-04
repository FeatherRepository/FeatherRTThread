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

/* 水位管理 (M4b 防卡顿, audio_link_m55.c 每轮 SBC 流模式时调用):
 *  - 播放中监测 ring 水位。ring 见空时下游 sound0 池+攒块还有 <=42ms 缓冲:
 *    短断流 (<FT_STARVE_HOLDOFF_MS) 静默容忍, 数据一到立即续播、不计数;
 *    断流超时则下游必然已欠载插零帧 -> 计一次饥饿 (≈一次可听卡顿, 验收
 *    硬指标) 并暂缓消费, 等 ring 回到 FT_REPREBUF_BYTES 再干净续播
 *    (避免按滴喂把一次长断流串成连续卡顿)。
 *  - 返回 RT_FALSE = 本轮暂缓消费 ring 数据 */
rt_bool_t ft_sbc_watermark_tick(void);

#endif /* FT_SBC_DECODE_H */
