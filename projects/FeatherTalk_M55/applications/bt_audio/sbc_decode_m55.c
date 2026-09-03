/* sbc_decode_m55.c - A0-2 SBC 解码消费链 (M55 侧)
 *
 * 帧化协议: 每块 = 2B 小端 length + SBC 帧原始字节 (M33 ft_audio_sbc_test 产生)。
 * 流式拼帧后交给 btstack bluedroid SBC 解码器 (单例 API), 用 DWT CYCCNT
 * 测每帧解码耗时与 CPU 占比 (CM55, 频率取 SystemCoreClock), 并统计 PCM
 * 输出字节数与能量 (|sample| 均值, 验证非全零)。msh ft_sbc_stats 查看。
 */
#include <rtthread.h>
#include <string.h>

#include <feathertalk/audio_link.h>   /* 顺带引入 CMSIS core 头 (DWT/CoreDebug) */

#include "classic/btstack_sbc.h"
#include "ft_sbc_decode.h"

#define FT_SBC_HDR_LEN      2U
/* 帧长 sanity 上界: 48k/JS/8sub/16blk/bitpool53 约 119B, 512 足够宽容 */
#define FT_SBC_MAX_FRAME    512U

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

/* 流式拼帧缓冲 */
static rt_uint8_t       s_frame_buf[FT_SBC_HDR_LEN + FT_SBC_MAX_FRAME];
static rt_uint32_t      s_frame_have;   /* 已缓存字节 (含 2B 头) */
static rt_uint32_t      s_frame_want;   /* 当前帧总长 (含 2B 头), 0 = 待帧头 */

/* 统计 (msh ft_sbc_stats 可读) */
static rt_uint32_t      s_stat_frames;      /* 成功解码帧数 (PCM 回调次数) */
static rt_uint32_t      s_stat_bad_len;     /* 帧头长度非法 -> 重同步次数 */
static rt_uint32_t      s_stat_pcm_bytes;
static rt_uint64_t      s_stat_energy;      /* |sample| 累加 */
static rt_uint32_t      s_stat_samples;
static rt_uint32_t      s_stat_dec_cycles;  /* 解码总 cycle */
static rt_uint32_t      s_stat_dec_max;     /* 单帧最大 cycle */
static rt_uint32_t      s_win_first_cyc;    /* 统计窗口: 首帧解码起点 */
static rt_uint32_t      s_win_last_cyc;     /* 统计窗口: 末帧解码终点 */
static rt_uint32_t      s_last_rate;
static rt_uint32_t      s_last_ch;

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

/* 解码器 PCM 回调: 统计输出字节数与能量 */
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
}

/* 新 SBC 流开始 (fmt_gen 变化): 复位解码器与统计 */
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
    s_dec_ready = RT_TRUE;
    rt_kprintf("[SBC] decoder ready (stream begin)\n");
}

/* 喂入帧化 SBC 字节流 (可跨 ring 读块任意切分) */
void ft_sbc_feed(const rt_uint8_t *data, rt_uint32_t len)
{
    if (!s_dec_ready)
    {
        return;
    }
    while (len > 0U)
    {
        rt_uint32_t need, take;

        if (s_frame_want == 0U)
        {
            /* 拼 2B 帧头 */
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

        /* 拼帧体 */
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

        /* 完整帧: 计时解码 (CYCCNT 32bit 回绕由无符号减法吸收) */
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
                     A0-2: show SBC decode stats (frames/cpu/pcm energy));
