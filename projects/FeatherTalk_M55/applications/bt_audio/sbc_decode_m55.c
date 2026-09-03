/* sbc_decode_m55.c - A0-2/M4b SBC 解码消费链 (M55 侧)
 *
 * 帧化协议: 每块 = 2B 小端 length + 载荷 (M33 侧 ft_audio_sbc_test 为单 SBC 帧,
 * M4b A2DP 媒体包为去 RTP 头后的载荷, 可含多个 SBC 帧; 解码器自身流式分帧)。
 * 拼块后交给 btstack bluedroid SBC 解码器 (单例 API), DWT CYCCNT 测每块耗时。
 *
 * M4b 起 PCM 落地 sound0: stream_begin 时 claim (FT_AUDIO_OUTPUT_OWNER_BT_A2DP)
 * -> 48k/16/2 (A3 契约) -> open; 协商 44.1k 时线性重采样到 48k (相位累加,
 * 跨块连续, 参考 feathertalk_player.c 的线性插值); 攒满 4096B 块写设备。
 * 音量: 每块查 ring 控制块 volume_percent (M33 由 AVRCP 写入), 变化即应用。
 * 流结束 (fmt_rate=0 的换代) -> ft_sbc_stream_end(): 冲刷尾块, close+release。
 *
 * msh: ft_sbc_stats
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>

#include <feathertalk/audio_link.h>   /* 顺带引入 CMSIS core 头 (DWT/CoreDebug) */

#include "classic/btstack_sbc.h"
#include "ft_sbc_decode.h"
#include "feathertalk_audio.h"

#define FT_SBC_HDR_LEN      2U
/* 块长 sanity 上界: A0-2 单帧 ~119B; M4b 媒体包载荷按 L2CAP MTU 1024 封顶 */
#define FT_SBC_MAX_FRAME    1024U

/* sound0 写块: 4096B = 1024 立体声帧 (48k/16/2ch) */
#define FT_OUT_BLOCK_BYTES      4096U
#define FT_OUT_BLOCK_SAMPLES    (FT_OUT_BLOCK_BYTES / 2U)   /* int16 数 */

/* 44.1k -> 48k 重采样步进 (16.16 定点): 44100/48000 */
#define FT_RS_STEP_441_480      ((44100UL << 16) / 48000UL)
/* 单块最大输入 128 帧 -> 输出上界 141 帧, 留余量 */
#define FT_RS_OUT_CAP_FRAMES    160U

/* DWT 使能 (兼容新旧 CMSIS 宏名) */
#if defined(CoreDebug_DEMCR_TRCENA_Msk)
#define FT_TRCENA_Msk       CoreDebug_DEMCR_TRCENA_Msk
#elif defined(DWT_DEMCR_TRCENA_Msk)
#define FT_TRCENA_Msk       DWT_DEMCR_TRCENA_Msk
#endif
#if defined(DWT_CTRL_CYCCNTENA_Msk)
#define FT_CYCCNTENA_Msk    DWT_CTRL_CYCCNTENA_Msk
#endif

static btstack_sbc_decoder_state_t s_sbc_dec;
static rt_bool_t        s_dec_ready;
static rt_bool_t        s_dwt_ready;

/* 流式拼块缓冲 */
static rt_uint8_t       s_frame_buf[FT_SBC_HDR_LEN + FT_SBC_MAX_FRAME];
static rt_uint32_t      s_frame_have;   /* 已缓存字节 (含 2B 头) */
static rt_uint32_t      s_frame_want;   /* 当前块总长 (含 2B 头), 0 = 待块头 */

/* sound0 输出 */
static rt_device_t      s_sound_dev;
static rt_bool_t        s_sound_open;
static rt_bool_t        s_claim_ok;
static rt_bool_t        s_resample_active;
static rt_uint32_t      s_stream_rate;  /* 协商采样率 (ring fmt_rate) */
static rt_uint8_t       s_cur_volume = 0xFFU;
static int16_t          s_out_buf[FT_OUT_BLOCK_SAMPLES];
static rt_uint32_t      s_out_fill;     /* int16 数 */

/* 重采样状态 (相位累加, 跨块连续) */
static int16_t          s_rs_prev[2];
static rt_uint32_t      s_rs_phase;     /* 16.16, 相对当前输入块起点 */
static rt_bool_t        s_rs_have_prev;

/* 统计 (msh ft_sbc_stats 可读) */
static rt_uint32_t      s_stat_frames;      /* 成功解码 SBC 帧数 (PCM 回调次数) */
static rt_uint32_t      s_stat_bad_len;     /* 块头长度非法 -> 重同步次数 */
static rt_uint32_t      s_stat_pcm_bytes;
static rt_uint64_t      s_stat_energy;      /* |sample| 累加 */
static rt_uint32_t      s_stat_samples;
static rt_uint32_t      s_stat_dec_cycles;  /* 解码总 cycle */
static rt_uint32_t      s_stat_dec_max;     /* 单块最大 cycle */
static rt_uint32_t      s_win_first_cyc;    /* 统计窗口: 首块解码起点 */
static rt_uint32_t      s_win_last_cyc;     /* 统计窗口: 末块解码终点 */
static rt_uint32_t      s_last_rate;
static rt_uint32_t      s_last_ch;
static rt_uint32_t      s_stat_out_blocks;  /* sound0 写块数 */
static rt_uint32_t      s_stat_out_bytes;   /* sound0 写字节数 */
static rt_uint32_t      s_stat_streams;     /* stream_begin 次数 */

static void ft_dwt_init_once(void)
{
#if defined(FT_TRCENA_Msk) && defined(FT_CYCCNTENA_Msk)
    if (!s_dwt_ready)
    {
        CoreDebug->DEMCR |= FT_TRCENA_Msk;
        DWT->CYCCNT = 0U;
        DWT->CTRL |= FT_CYCCNTENA_Msk;
        s_dwt_ready = RT_TRUE;
    }
#endif
}

/* 攒 4096B 块写 sound0 (未打开则只计数: 解码统计不受输出影响) */
static void ft_sound_push(const int16_t *data, rt_uint32_t samples)
{
    while (samples > 0U)
    {
        rt_uint32_t room = FT_OUT_BLOCK_SAMPLES - s_out_fill;
        rt_uint32_t n = (samples < room) ? samples : room;
        memcpy(&s_out_buf[s_out_fill], data, n * 2U);
        s_out_fill += n;
        data += n;
        samples -= n;
        if (s_out_fill == FT_OUT_BLOCK_SAMPLES)
        {
            if (s_sound_open)
            {
                rt_device_write(s_sound_dev, 0, s_out_buf, FT_OUT_BLOCK_BYTES);
            }
            s_stat_out_blocks++;
            s_stat_out_bytes += FT_OUT_BLOCK_BYTES;
            s_out_fill = 0U;
        }
    }
}

/* 44.1k -> 48k 线性插值 (立体声交织), 相位跨块连续;
 * 返回输出帧数 (每帧 2 个 int16) */
static rt_uint32_t ft_resample_441_480(const int16_t *in, rt_uint32_t in_frames,
                                       int16_t *out, rt_uint32_t out_cap_frames)
{
    rt_uint32_t pos = s_rs_phase;
    rt_uint32_t o = 0;

    while (((pos >> 16) < in_frames) && (o < out_cap_frames))
    {
        rt_uint32_t idx = pos >> 16;
        rt_uint32_t frac = pos & 0xFFFFU;
        for (rt_uint32_t ch = 0; ch < 2U; ch++)
        {
            rt_int32_t s0 = (idx == 0U) ? (s_rs_have_prev ? s_rs_prev[ch] : in[ch])
                                        : in[(idx - 1U) * 2U + ch];
            rt_int32_t s1 = in[idx * 2U + ch];
            out[o * 2U + ch] = (int16_t)(s0 + (((s1 - s0) * (rt_int32_t)frac) >> 16));
        }
        o++;
        pos += FT_RS_STEP_441_480;
    }
    s_rs_prev[0] = in[(in_frames - 1U) * 2U];
    s_rs_prev[1] = in[(in_frames - 1U) * 2U + 1U];
    s_rs_have_prev = RT_TRUE;
    s_rs_phase = pos - (in_frames << 16);
    return o;
}

/* 解码器 PCM 回调: 统计 + 重采样(如需) + 攒块写 sound0 */
static void ft_sbc_pcm_cb(int16_t *data, int num_samples, int num_channels,
                          int sample_rate, void *context)
{
    rt_uint32_t n = (rt_uint32_t)num_samples * (rt_uint32_t)num_channels;

    (void)context;
    for (rt_uint32_t i = 0; i < n; i++)
    {
        rt_int32_t v = data[i];
        s_stat_energy += (rt_uint32_t)((v < 0) ? -v : v);
    }
    s_stat_samples += n;
    s_stat_pcm_bytes += n * 2U;
    s_stat_frames++;
    s_last_rate = (rt_uint32_t)sample_rate;
    s_last_ch = (rt_uint32_t)num_channels;

    if (num_channels == 2)
    {
        if ((rt_uint32_t)sample_rate == 48000U)
        {
            ft_sound_push(data, n);
        }
        else if ((rt_uint32_t)sample_rate == 44100U)
        {
            static int16_t rs_out[FT_RS_OUT_CAP_FRAMES * 2U];
            rt_uint32_t frames_out = ft_resample_441_480(data, (rt_uint32_t)num_samples,
                                                         rs_out, FT_RS_OUT_CAP_FRAMES);
            ft_sound_push(rs_out, frames_out * 2U);
        }
        /* 其它采样率不出声 (本契约只有 44.1k/48k), 统计照计 */
    }
}

/* 新 SBC 流开始 (fmt_gen 换代且 fmt_rate 非 0): 复位解码器与统计, 开 sound0 */
void ft_sbc_stream_begin(void)
{
    ft_dwt_init_once();
    btstack_sbc_decoder_init(&s_sbc_dec, SBC_MODE_STANDARD, ft_sbc_pcm_cb, RT_NULL);

    s_frame_have = 0U;
    s_frame_want = 0U;
    s_stat_frames = 0U;
    s_stat_bad_len = 0U;
    s_stat_pcm_bytes = 0U;
    s_stat_energy = 0U;
    s_stat_samples = 0U;
    s_stat_dec_cycles = 0U;
    s_stat_dec_max = 0U;
    s_win_first_cyc = 0U;
    s_win_last_cyc = 0U;
    s_stat_out_blocks = 0U;
    s_stat_out_bytes = 0U;
    s_stat_streams++;
    s_out_fill = 0U;

    /* 协商格式 (M33 换代前已写入 ring 控制块) */
    FT_ALINK_DCACHE_INVALID(FT_ALINK_BASE, 32);
    s_stream_rate = FT_ALINK->fmt_rate;
    s_resample_active = (s_stream_rate == 44100U);
    s_rs_phase = 0U;
    s_rs_have_prev = RT_FALSE;

    /* M4b: claim sound0 -> 48k/16/2 (A3 契约) -> open */
    s_claim_ok = (ft_audio_claim_output(FT_AUDIO_OUTPUT_OWNER_BT_A2DP) == RT_EOK);
    s_sound_open = RT_FALSE;
    if (s_claim_ok)
    {
        if (ft_audio_set_output_format(48000U, 16U, 2U) == RT_EOK)
        {
            s_sound_dev = rt_device_find("sound0");
            if ((s_sound_dev != RT_NULL) &&
                (rt_device_open(s_sound_dev, RT_DEVICE_OFLAG_WRONLY) == RT_EOK))
            {
                s_sound_open = RT_TRUE;
            }
        }
    }
    s_cur_volume = 0xFFU;   /* 等 M33 报 AVRCP 音量 */
    s_dec_ready = RT_TRUE;
    rt_kprintf("[SBC] decoder ready (stream #%lu, rate=%lu%s, sound0 %s)\n",
               (unsigned long)s_stat_streams, (unsigned long)s_stream_rate,
               s_resample_active ? "->48k" : "",
               s_sound_open ? "open" : (s_claim_ok ? "open-failed" : "busy"));
}

/* 流结束 (fmt_rate=0 的换代): 冲刷尾块, close sound0 + release */
void ft_sbc_stream_end(void)
{
    if (s_dec_ready)
    {
        if (s_sound_open && (s_out_fill > 0U))
        {
            rt_device_write(s_sound_dev, 0, s_out_buf, s_out_fill * 2U);
            s_stat_out_bytes += s_out_fill * 2U;
            s_out_fill = 0U;
        }
        rt_kprintf("[SBC] stream end: frames=%lu out=%lu B (blocks=%lu)\n",
                   (unsigned long)s_stat_frames, (unsigned long)s_stat_out_bytes,
                   (unsigned long)s_stat_out_blocks);
    }
    s_dec_ready = RT_FALSE;
    if (s_sound_open && (s_sound_dev != RT_NULL))
    {
        rt_device_close(s_sound_dev);
    }
    s_sound_open = RT_FALSE;
    if (s_claim_ok)
    {
        ft_audio_release_output(FT_AUDIO_OUTPUT_OWNER_BT_A2DP);
    }
    s_claim_ok = RT_FALSE;
}

/* 喂入帧化字节流 (可跨 ring 读块任意切分) */
void ft_sbc_feed(const rt_uint8_t *data, rt_uint32_t len)
{
    if (!s_dec_ready)
    {
        return;
    }

    /* M4b: AVRCP 绝对音量 (M33 写 ring 控制块), 变化才应用 */
    FT_ALINK_DCACHE_INVALID(FT_ALINK_BASE, 32);
    rt_uint32_t vol = FT_ALINK->volume_percent & 0xFFU;
    if ((vol != 0xFFU) && (vol != s_cur_volume))
    {
        s_cur_volume = (rt_uint8_t)vol;
        if (s_sound_open)
        {
            ft_audio_set_output_volume((rt_uint8_t)vol);
        }
    }

    while (len > 0U)
    {
        rt_uint32_t need, take;

        if (s_frame_want == 0U)
        {
            /* 拼 2B 块头 */
            need = FT_SBC_HDR_LEN - s_frame_have;
            take = (len < need) ? len : need;
            memcpy(s_frame_buf + s_frame_have, data, take);
            s_frame_have += take;
            data += take;
            len -= take;
            if (s_frame_have < FT_SBC_HDR_LEN)
            {
                break;
            }
            rt_uint32_t flen = (rt_uint32_t)s_frame_buf[0] |
                               ((rt_uint32_t)s_frame_buf[1] << 8);
            if (flen < 4U || flen > FT_SBC_MAX_FRAME)
            {
                /* 长度非法: 丢 1B 尝试重同步 (此时 s_frame_have==2) */
                s_frame_buf[0] = s_frame_buf[1];
                s_frame_have = 1U;
                s_stat_bad_len++;
                continue;
            }
            s_frame_want = FT_SBC_HDR_LEN + flen;
        }

        /* 拼块体 */
        need = s_frame_want - s_frame_have;
        take = (len < need) ? len : need;
        memcpy(s_frame_buf + s_frame_have, data, take);
        s_frame_have += take;
        data += take;
        len -= take;
        if (s_frame_have < s_frame_want)
        {
            break;
        }

        /* 完整块: 计时解码 (解码器内部流式分帧, 一块可多帧;
         * CYCCNT 32bit 回绕由无符号减法吸收) */
        rt_uint32_t c0 = DWT->CYCCNT;
        btstack_sbc_decoder_process_data(&s_sbc_dec, 0,
                                         s_frame_buf + FT_SBC_HDR_LEN,
                                         (int)(s_frame_want - FT_SBC_HDR_LEN));
        rt_uint32_t c1 = DWT->CYCCNT;
        rt_uint32_t dc = c1 - c0;
        s_stat_dec_cycles += dc;
        if (dc > s_stat_dec_max)
        {
            s_stat_dec_max = dc;
        }
        if (s_win_first_cyc == 0U)
        {
            s_win_first_cyc = c0;
        }
        s_win_last_cyc = c1;

        s_frame_have = 0U;
        s_frame_want = 0U;
    }
}

static int ft_sbc_stats(int argc, char **argv)
{
    rt_uint32_t mhz = SystemCoreClock / 1000000U;
    rt_uint32_t win_cyc = s_win_last_cyc - s_win_first_cyc;

    (void)argc; (void)argv;
    rt_kprintf("[SBC] ready=%d frames=%lu (dec good=%d bad=%d) bad_len=%lu\n",
               (int)s_dec_ready, (unsigned long)s_stat_frames,
               s_sbc_dec.good_frames_nr, s_sbc_dec.bad_frames_nr,
               (unsigned long)s_stat_bad_len);
    rt_kprintf("[SBC] pcm=%lu B samples=%lu energy_avg=%lu fmt=%lu Hz/%lu ch\n",
               (unsigned long)s_stat_pcm_bytes, (unsigned long)s_stat_samples,
               s_stat_samples ? (unsigned long)(s_stat_energy / s_stat_samples) : 0UL,
               (unsigned long)s_last_rate, (unsigned long)s_last_ch);
    rt_kprintf("[SBC] sound0: claim=%d open=%d resample=%d, wrote %lu blocks/%lu B, volume=%u\n",
               (int)s_claim_ok, (int)s_sound_open, (int)s_resample_active,
               (unsigned long)s_stat_out_blocks, (unsigned long)s_stat_out_bytes,
               (unsigned)((s_cur_volume == 0xFFU) ? 0U : s_cur_volume));
    if (s_stat_frames > 0U && mhz > 0U)
    {
        rt_uint32_t us_avg = s_stat_dec_cycles / s_stat_frames / mhz;
        rt_uint32_t us_max = s_stat_dec_max / mhz;
        rt_uint32_t cpu_x100 = 0U;
        if (win_cyc > 0U)
        {
            cpu_x100 = (rt_uint32_t)(((rt_uint64_t)s_stat_dec_cycles * 10000U) / win_cyc);
        }
        rt_kprintf("[SBC] decode %lu us/frame (max %lu), cpu=%lu.%02lu%% "
                   "(@%lu MHz, window %lu ms)\n",
                   (unsigned long)us_avg, (unsigned long)us_max,
                   (unsigned long)(cpu_x100 / 100U), (unsigned long)(cpu_x100 % 100U),
                   (unsigned long)mhz,
                   (unsigned long)(win_cyc / (mhz * 1000U)));
    }
    return 0;
}
MSH_CMD_EXPORT_ALIAS(ft_sbc_stats, ft_sbc_stats,
                     M4b: show SBC decode + sound0 output stats);
