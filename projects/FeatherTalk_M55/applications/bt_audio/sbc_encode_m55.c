/* sbc_encode_m55.c - M5 A2DP Source 编码段 (M55 侧)
 *
 * 职责: PCM (48k/16/2ch) -> btstack bluedroid SBC 编码 -> 帧化 [2B LE len +
 * SBC 帧] 写反向 ring (FT_ALINK2, M55 producer -> M33 consumer)。
 * 与 M4b 解码链对称, 编码器 API 与 M33 A0-2 自测同款 (已实证)。
 *
 * 数据源分两步接入: 阶段1 msh 自测 (ft_sbc_enc_test, 1kHz 正弦);
 * 阶段2 Source 模式 tap UAC s_out_ring (A3 契约 §2, 互斥由 claim 仲裁)。
 *
 * msh: ft_sbc_enc_test <frames>
 */
#include <rtthread.h>
#include <stdlib.h>
#include <string.h>

#include <feathertalk/audio_link.h>   /* FT_ALINK2 + cache 维护宏 */

#include "classic/btstack_sbc.h"
#include "feathertalk_audio.h"        /* claim 仲裁 */
#include "feathertalk_usb_uac.h"      /* ft_usb_uac_output_read (A3 §2 tap) */

/* 与 M33 侧自测同一图案: 1kHz @48kHz 正弦表 (一周期 48 样点, 免 libm) */
static const rt_int16_t ft_sine_1k_48k[48] =
{
         0,   1566,   3106,   4592,   6000,   7305,   8485,   9520,  10392,  11087,  11591,  11897,
     12000,  11897,  11591,  11087,  10392,   9520,   8485,   7305,   6000,   4592,   3106,   1566,
         0,  -1566,  -3106,  -4592,  -6000,  -7305,  -8485,  -9520, -10392, -11087, -11591, -11897,
    -12000, -11897, -11591, -11087, -10392,  -9520,  -8485,  -7305,  -6000,  -4592,  -3106,  -1566,
};

/* M55 -> M33 生产入口: 帧化写反向 ring (限流等待, 超时丢弃计数) */
static rt_uint32_t ft_audio2_produce(const rt_uint8_t *data, rt_uint32_t len)
{
    rt_uint32_t off = 0, wait_ms = 0;
    while (off < len)
    {
        rt_uint32_t w = ft_alink_write(FT_ALINK2, data + off, len - off);
        if (w == 0U)
        {
            if (++wait_ms > 50U)
            {
                FT_ALINK2->stats_overrun += len - off;
                break;
            }
            rt_thread_mdelay(1);
            continue;
        }
        off += w;
    }
    if (off > 0U)
    {
        FT_ALINK2->seq++;
        FT_ALINK2->stats_doorbell++;   /* 反向门铃字段借用: M33 消费端轮询即可 */
    }
    return off;
}

/* ---- msh 自测: ft_sbc_enc_test <frames> ----
 * 编码 1kHz 正弦 -> 帧化写 ring2; M33 侧 ft_audio2_test 收验。
 * 帧节奏 ~10ms (比实时慢, 验证链路正确性优先, 不上速率压力)。 */
static int ft_sbc_enc_test(int argc, char **argv)
{
    static rt_int16_t pcm[256];      /* 16blk x 8sub = 128 帧/声道, 立体声 256 样点 */
    static rt_uint8_t blk[2 + 512];  /* 帧化块: 2B LE length + SBC 帧 (~119B) */
    static btstack_sbc_encoder_state_t sbc_enc;
    rt_uint32_t frames = 200;
    rt_uint32_t written_frames = 0, written_bytes = 0, dropped = 0;
    rt_uint32_t tick_start;
    rt_uint32_t phase = 0;

    if (!ft_alink_ready_at(FT_ALINK2))
    {
        rt_kprintf("ft_sbc_enc_test: ring2 not ready (M33 未初始化?)\n");
        return -1;
    }
    if (argc > 1) frames = (rt_uint32_t)strtoul(argv[1], RT_NULL, 0);

    /* 编码器: 48kHz (A3 契约) joint stereo 8sub/16blk bitpool53 (~119B/帧) */
    btstack_sbc_encoder_init(&sbc_enc, SBC_MODE_STANDARD, 16, 8,
                             SBC_ALLOCATION_METHOD_LOUDNESS, 48000, 53,
                             SBC_CHANNEL_MODE_JOINT_STEREO);

    /* 登记流格式并换代 (M33 消费端可据此识别) */
    FT_ALINK2->fmt_rate = 48000U;
    FT_ALINK2->fmt_bits = 16U;
    FT_ALINK2->fmt_ch   = 2U;
    __DMB();
    FT_ALINK2->fmt_gen++;
    FT_ALINK_DCACHE_CLEAN(FT_ALINK2_BASE, 32);

    rt_kprintf("ft_sbc_enc_test: %lu frames, 48k/16/2ch sine 1kHz -> ring2\n",
               (unsigned long)frames);
    tick_start = rt_tick_get();
    for (rt_uint32_t f = 0; f < frames; f++)
    {
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

        rt_uint32_t total = (rt_uint32_t)flen + 2U;
        rt_uint32_t w = ft_audio2_produce(blk, total);
        if (w == total)
        {
            written_frames++;
            written_bytes += total;
        }
        else
        {
            dropped += total - w;
        }
        rt_thread_mdelay(10);
    }
    rt_uint32_t elapsed_ms = rt_tick_get() - tick_start;
    rt_kprintf("ft_sbc_enc_test: %lu/%lu frames, %lu B in %lu ms, dropped %lu B\n",
               (unsigned long)written_frames, (unsigned long)frames,
               (unsigned long)written_bytes, (unsigned long)elapsed_ms,
               (unsigned long)dropped);
    return 0;
}
MSH_CMD_EXPORT_ALIAS(ft_sbc_enc_test, ft_sbc_enc_test,
                     M5: encode 1kHz sine to SBC frames into reverse ring2);

/* ---- M5-4 Source 模式: UAC tap -> SBC 编码 -> ring2 (自动跟随 M33 流状态) ----
 * M33 a2dp_source 流开时在 ring2 控制块写 fmt_rate=48000 并换代; 本线程据此
 * claim 音频输出 (A3 §2 互斥: UAC worker 拿不到 sound0, PCM 只走 BT),
 * 起编码循环;  fmt_rate=0 换代即停。 */
static btstack_sbc_encoder_state_t s_src_enc;
static rt_bool_t   s_src_active;
static rt_bool_t   s_src_claimed;
static rt_uint32_t s_src_last_gen;
static rt_uint32_t s_src_stat_frames;
static rt_uint32_t s_src_stat_bytes;
static rt_uint32_t s_src_stat_idle;     /* tap 无数据空转次数 */
static rt_uint32_t s_pcm_have;          /* 拼帧暂存进度 (字节) */

static void ft_src_encode_stop(void)
{
    if (s_src_claimed)
    {
        ft_audio_release_output(FT_AUDIO_OUTPUT_OWNER_BT_A2DP);
        s_src_claimed = RT_FALSE;
    }
    ft_usb_uac_set_bt_tap(RT_FALSE);   /* 本地播放恢复 */
    if (s_src_active)
    {
        rt_kprintf("[SRC-enc] stop: frames=%lu bytes=%lu idle=%lu\n",
                   (unsigned long)s_src_stat_frames, (unsigned long)s_src_stat_bytes,
                   (unsigned long)s_src_stat_idle);
    }
    s_src_active = RT_FALSE;
}

static void ft_src_encode_start(void)
{
    /* A3 §2 互斥: 先让 UAC 本地播放让位 (放 claim 关 sound0), 再抢仲裁位 */
    ft_usb_uac_set_bt_tap(RT_TRUE);
    rt_thread_mdelay(30);              /* 给 UAC worker 一个让位窗口 */
    s_src_claimed = (ft_audio_claim_output(FT_AUDIO_OUTPUT_OWNER_BT_A2DP) == RT_EOK);
    btstack_sbc_encoder_init(&s_src_enc, SBC_MODE_STANDARD, 16, 8,
                             SBC_ALLOCATION_METHOD_LOUDNESS, 48000, 53,
                             SBC_CHANNEL_MODE_JOINT_STEREO);
    s_src_stat_frames = 0U;
    s_src_stat_bytes = 0U;
    s_src_stat_idle = 0U;
    s_pcm_have = 0U;                    /* 拼帧暂存清零 (换流不带上次尾巴) */
    s_src_active = RT_TRUE;
    rt_kprintf("[SRC-enc] start: 48k/16/2 JS53, claim=%d\n", (int)s_src_claimed);
}

static void ft_sbc_src_thread_entry(void *parameter)
{
    static rt_uint8_t  pcm[512];       /* 128 立体声帧 = 512B */
    static rt_uint8_t  blk[2 + 512];
    (void)parameter;

    while (!ft_alink_ready_at(FT_ALINK2))
    {
        rt_thread_mdelay(100);
    }
    s_src_last_gen = FT_ALINK2->fmt_gen;
    rt_kprintf("[SRC-enc] ring2 ready, watching gen=%lu\n",
               (unsigned long)s_src_last_gen);

    while (1)
    {
        FT_ALINK_DCACHE_INVALID(FT_ALINK2_BASE, 32);
        rt_uint32_t gen = FT_ALINK2->fmt_gen;
        if (gen != s_src_last_gen)
        {
            s_src_last_gen = gen;
            if (FT_ALINK2->fmt_rate != 0U)
            {
                ft_src_encode_start();
            }
            else
            {
                ft_src_encode_stop();
            }
        }
        if (!s_src_active)
        {
            rt_thread_mdelay(20);
            continue;
        }

        /* tap UAC: 攒满 512B (一帧 128 立体声样本) 才编码。
         * 关键: 部分读必须留到下轮拼接 —— 曾直接把不足 512B 的读数丢弃,
         * 每 2ms 白吞 ~384B, 编码器饿死 (实测 48MB 进 ring 只出 528 帧) */
        s_pcm_have += ft_usb_uac_output_read(pcm + s_pcm_have,
                                             sizeof(pcm) - s_pcm_have);
        if (s_pcm_have < sizeof(pcm))
        {
            s_src_stat_idle++;
            rt_thread_mdelay(2);
            continue;
        }
        s_pcm_have = 0U;
        btstack_sbc_encoder_process_data((rt_int16_t *)pcm);
        rt_uint16_t flen = btstack_sbc_encoder_sbc_buffer_length();
        blk[0] = (rt_uint8_t)(flen & 0xFFU);
        blk[1] = (rt_uint8_t)(flen >> 8);
        memcpy(blk + 2, btstack_sbc_encoder_sbc_buffer(), flen);
        rt_uint32_t total = (rt_uint32_t)flen + 2U;
        if (ft_audio2_produce(blk, total) == total)
        {
            s_src_stat_frames++;
            s_src_stat_bytes += total;
        }
    }
}

static int ft_sbc_src_init(void)
{
    rt_thread_t t = rt_thread_create("ft_senc", ft_sbc_src_thread_entry, RT_NULL,
                                     4096, 12, 10);
    if (t != RT_NULL)
    {
        rt_thread_startup(t);
        return 0;
    }
    return -1;
}
INIT_APP_EXPORT(ft_sbc_src_init);

/* M5 排障: 编码器侧计数 (与 M33 bt_src 的发送计数对照定位瓶颈) */
static int ft_sbc_enc_stats(int argc, char **argv)
{
    (void)argc; (void)argv;
    FT_ALINK_DCACHE_INVALID(FT_ALINK2_BASE, 128);
    rt_kprintf("[SRC-enc] active=%d claim=%d frames=%lu bytes=%lu idle=%lu | ring2 used=%lu overrun=%lu\n",
               (int)s_src_active, (int)s_src_claimed,
               (unsigned long)s_src_stat_frames, (unsigned long)s_src_stat_bytes,
               (unsigned long)s_src_stat_idle,
               (unsigned long)ft_alink_used(FT_ALINK2),
               (unsigned long)FT_ALINK2->stats_overrun);
    return 0;
}
MSH_CMD_EXPORT_ALIAS(ft_sbc_enc_stats, ft_sbc_enc_stats,
                     M5: show source encoder + ring2 stats);
