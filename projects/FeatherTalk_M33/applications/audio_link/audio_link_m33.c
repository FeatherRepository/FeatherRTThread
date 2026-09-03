/* audio_link_m33.c - A0 跨核音频数据面 M33 侧 (producer)
 *
 * 职责: 启动早期初始化共享 ring (ft_alink_init), 提供 producer 写接口
 * (后续 M4b 的 A2DP media handler 直接调 ft_audio_produce),
 * 以及 msh 回环自测命令 ft_audio_test。
 *
 * 门铃经 feathertalk_ipc_send_audio_db 登记, 由 IPC 线程统一发出
 * (pipe 唯一写者原则)。
 */
#include <rtthread.h>
#include <stdlib.h>
#include <string.h>

#include <feathertalk/audio_link.h>
#include "ipc/feathertalk_ipc.h"

#ifdef FEATHERTALK_BT_STACK_BK
#include "classic/btstack_sbc.h"   /* A0-2: SBC 编码器 (bt group 编入) */
#endif

/* 测试图案: 与绝对位置绑定的确定性字节 (M55 侧同函数校验) */
static rt_uint8_t ft_audio_pattern(rt_uint32_t pos)
{
    return (rt_uint8_t)(((pos * 2654435761u) >> 24) ^ (pos & 0xFFu));
}

/* producer 写入口 (M4b 起由 A2DP 路径调用): 返回写入字节数 */
rt_uint32_t ft_audio_produce(const rt_uint8_t *data, rt_uint32_t len)
{
    rt_uint32_t written = ft_alink_write(FT_ALINK, data, len);

    if (written > 0U)
    {
        FT_ALINK->seq++;
        feathertalk_ipc_send_audio_db(FT_ALINK->wr, 0U);
        FT_ALINK->stats_doorbell++;
    }
    return written;
}

static int ft_alink_init_early(void)
{
    ft_alink_init();
    return 0;
}
/* M33 先启并监督 M55: 在应用初始化阶段铺好 ring, M55 侧等 magic */
INIT_APP_EXPORT(ft_alink_init_early);

/* ---- msh 自测: ft_audio_test [blocks] [chunk] ---- */
static int ft_audio_test(int argc, char **argv)
{
    static rt_uint8_t buf[4096];
    rt_uint32_t blocks = 64;
    rt_uint32_t chunk = 4096;
    rt_uint32_t interval_ms = 0;   /* 块间限速: 模拟实时流 (48k/16/2ch 4096B≈21.3ms) */
    rt_uint32_t total, written_total = 0;
    rt_uint32_t tick_start;
    rt_uint32_t pos0;

    if (!ft_alink_ready())
    {
        rt_kprintf("ft_audio_test: ring not ready\n");
        return -1;
    }
    if (argc > 1) blocks = (rt_uint32_t)strtoul(argv[1], RT_NULL, 0);
    if (argc > 2) chunk = (rt_uint32_t)strtoul(argv[2], RT_NULL, 0);
    if (argc > 3) interval_ms = (rt_uint32_t)strtoul(argv[3], RT_NULL, 0);
    if (chunk == 0U || chunk > sizeof(buf)) chunk = sizeof(buf);

    total = blocks * chunk;
    pos0 = FT_ALINK->wr;   /* 图案与绝对位置绑定 */
    rt_kprintf("ft_audio_test: %lu blocks x %lu B = %lu B, pos0=%lu\n",
               (unsigned long)blocks, (unsigned long)chunk,
               (unsigned long)total, (unsigned long)pos0);

    tick_start = rt_tick_get();
    for (rt_uint32_t b = 0; b < blocks; b++)
    {
        for (rt_uint32_t i = 0; i < chunk; i++)
        {
            buf[i] = ft_audio_pattern(pos0 + b * chunk + i);
        }
        rt_uint32_t off = 0;
        while (off < chunk)
        {
            rt_uint32_t w = ft_alink_write(FT_ALINK, buf + off, chunk - off);
            if (w == 0U)
            {
                FT_ALINK->stats_overrun += chunk - off;
                rt_kprintf("ft_audio_test: ring full at block %lu (+%lu B dropped)\n",
                           (unsigned long)b, (unsigned long)(chunk - off));
                break;
            }
            off += w;
        }
        FT_ALINK->seq++;
        feathertalk_ipc_send_audio_db(FT_ALINK->wr, 0U);
        FT_ALINK->stats_doorbell++;
        written_total += (off == chunk) ? chunk : off;
        if (off != chunk) break;
        if (interval_ms > 0U)
        {
            rt_thread_mdelay(interval_ms);
        }
    }
    rt_uint32_t elapsed_ms = rt_tick_get() - tick_start;
    rt_kprintf("ft_audio_test: wrote %lu B in %lu ms (%lu KB/s)\n",
               (unsigned long)written_total, (unsigned long)elapsed_ms,
               elapsed_ms ? (unsigned long)(written_total / elapsed_ms) : 0UL);
    return 0;
}
MSH_CMD_EXPORT_ALIAS(ft_audio_test, ft_audio_test,
                     A0: write test pattern into cross-core audio ring);

/* ---- A0-2 msh: ft_audio_sbc_test <frames> ----
 * 48kHz/16bit/立体声正弦 (1kHz, 幅度 12000) -> btstack bluedroid SBC 编码 ->
 * 帧化块 (2B 小端 length + SBC 帧) 写共享 ring + 门铃, 帧间隔 ~10ms。
 * 开流前登记流格式 (fmt_rate/bits/ch) 并 fmt_gen++:
 * M55 consumer 检测 fmt_gen 变化切换到 SBC 解码模式。
 */
#ifdef FEATHERTALK_BT_STACK_BK

/* 1kHz @48kHz 正弦表 (一周期 48 样点, 免 libm) */
static const rt_int16_t ft_sine_1k_48k[48] =
{
         0,   1566,   3106,   4592,   6000,   7305,   8485,   9520,  10392,  11087,  11591,  11897,
     12000,  11897,  11591,  11087,  10392,   9520,   8485,   7305,   6000,   4592,   3106,   1566,
         0,  -1566,  -3106,  -4592,  -6000,  -7305,  -8485,  -9520, -10392, -11087, -11591, -11897,
    -12000, -11897, -11591, -11087, -10392,  -9520,  -8485,  -7305,  -6000,  -4592,  -3106,  -1566,
};

static int ft_audio_sbc_test(int argc, char **argv)
{
    static rt_int16_t pcm[256];      /* 16blk x 8sub = 128 帧/声道, 立体声 256 样点 */
    static rt_uint8_t blk[2 + 512];  /* 帧化块: 2B LE length + SBC 帧 (~119B) */
    static btstack_sbc_encoder_state_t sbc_enc;
    rt_uint32_t frames = 200;
    rt_uint32_t written_frames = 0, written_bytes = 0, dropped = 0;
    rt_uint32_t tick_start;
    rt_uint32_t phase = 0;

    if (!ft_alink_ready())
    {
        rt_kprintf("ft_audio_sbc_test: ring not ready\n");
        return -1;
    }
    if (argc > 1) frames = (rt_uint32_t)strtoul(argv[1], RT_NULL, 0);

    /* 编码器: 48kHz (A3 契约) joint stereo 8sub/16blk bitpool53 (~119B/帧) */
    btstack_sbc_encoder_init(&sbc_enc, SBC_MODE_STANDARD, 16, 8,
                             SBC_ALLOCATION_METHOD_LOUDNESS, 48000, 53,
                             SBC_CHANNEL_MODE_JOINT_STEREO);

    /* 登记流格式并换代 (M55 consumer 据此切 SBC 解码模式) */
    FT_ALINK->fmt_rate = 48000U;
    FT_ALINK->fmt_bits = 16U;
    FT_ALINK->fmt_ch   = 2U;
    __DMB();
    FT_ALINK->fmt_gen++;
    FT_ALINK_DCACHE_CLEAN(FT_ALINK_BASE, 32);

    rt_kprintf("ft_audio_sbc_test: %lu frames, 48k/16/2ch sine 1kHz\n",
               (unsigned long)frames);
    tick_start = rt_tick_get();
    for (rt_uint32_t f = 0; f < frames; f++)
    {
        /* 相位连续的 1kHz 立体声 (双声道同相) */
        for (rt_uint32_t i = 0; i < 128U; i++)
        {
            rt_int16_t v = ft_sine_1k_48k[(phase + i) % 48U];
            pcm[i * 2U]      = v;
            pcm[i * 2U + 1U] = v;
        }
        phase = (phase + 128U) % 48U;

        btstack_sbc_encoder_process_data(pcm);
        rt_uint16_t flen = btstack_sbc_encoder_sbc_buffer_length();
        blk[0] = (rt_uint8_t)(flen & 0xFFU);
        blk[1] = (rt_uint8_t)(flen >> 8);
        memcpy(blk + 2, btstack_sbc_encoder_sbc_buffer(), flen);

        /* 空间不足时限流等待 (M55 在 drain), 仍不够则丢帧计数 */
        rt_uint32_t off = 0, total = (rt_uint32_t)flen + 2U;
        rt_uint32_t wait_ms = 0;
        while (off < total)
        {
            rt_uint32_t w = ft_audio_produce(blk + off, total - off);
            if (w == 0U)
            {
                if (++wait_ms > 50U)
                {
                    dropped += total - off;
                    break;
                }
                rt_thread_mdelay(1);
                continue;
            }
            off += w;
        }
        if (off == total)
        {
            written_frames++;
            written_bytes += total;
        }
        rt_thread_mdelay(10);
    }
    rt_uint32_t elapsed_ms = rt_tick_get() - tick_start;
    rt_kprintf("ft_audio_sbc_test: %lu/%lu frames, %lu B in %lu ms, dropped %lu B\n",
               (unsigned long)written_frames, (unsigned long)frames,
               (unsigned long)written_bytes, (unsigned long)elapsed_ms,
               (unsigned long)dropped);
    return 0;
}
MSH_CMD_EXPORT_ALIAS(ft_audio_sbc_test, ft_audio_sbc_test,
                     A0-2: encode 1kHz sine to SBC frames into cross-core ring);
#endif /* FEATHERTALK_BT_STACK_BK */
