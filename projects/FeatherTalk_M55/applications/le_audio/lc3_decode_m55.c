/* lc3_decode_m55.c - M8.1 LE Audio LC3 解码消费链 (M55 侧)
 *
 * 数据面 (M33 bt_le_audio_unicast.c 写 ring):
 *   [len16 LE][ch u8 (0x01=L 0x02=R)][LC3 帧], len = 1 + 帧字节数。
 *   一次 ISO SDU = 一帧; 双 ASE 立体声 (ASE1=FL, ASE2=FR), 48k/10ms。
 *
 * 解码: google liblc3, 双声道各一个 decoder 实例 (lc3_setup_decoder),
 * PCM 48k/16/2 直出 sound0 (LE Audio 原生 48k, A0 契约零重采样)。
 * 丢帧: 某声道 SDU 未到时以 liblc3 PLC (in=NULL) 补齐该声道, 不失同步。
 * 声道配对策略: 收到任一声道新帧时, 若另一声道还挂着上一周期的帧,
 * 先 PLC 补齐旧的再解码新的 (自愈乱序/单声道退化)。
 *
 * M4b 水位管理全套复用 (预缓冲/饥饿分级/恢复水位), 详见 ft_sbc_decode.h;
 * LC3 48k/10ms 立体声约 28.8KB/s, 预缓冲取 6KB (≈213ms)。
 *
 * msh: ft_lc3_stats
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>

#include <feathertalk/audio_link.h>
#include "lc3.h"
#include "ft_lc3_decode.h"
#include "feathertalk_audio.h"
#include <math.h>

/* LE Audio 固定规格 (PACS 静态声明, v1 只支持 48k/10ms) */
#define FT_LC3_FRAME_US     10000
#define FT_LC3_RATE_HZ      48000

/* ring 帧头 2B len + 1B 声道; 帧长上界 128B (PACS 声明 30..120) */
#define FT_LC3_HDR_LEN      3U
#define FT_LC3_FRAME_MAX    128U

/* sound0 写块: 4096B = 1024 立体声帧 (48k/16/2ch), 与 SBC 链一致 */
#define FT_OUT_BLOCK_BYTES      4096U
#define FT_OUT_BLOCK_SAMPLES    (FT_OUT_BLOCK_BYTES / 2U)

/* ---- 水位管理 (M4b 全套复用, 尺寸按 LC3 带宽折算) ---- */
#define FT_PREBUF_BYTES         6144U   /* ≈213ms @28.8KB/s */
#define FT_PREBUF_TIMEOUT_MS    800U
#define FT_STARVE_HOLDOFF_MS    50U
#define FT_REPREBUF_BYTES       3072U

/* liblc3 解码器内存 (2 声道): 走 rt_malloc (lc3_bench 同款手法)。
 * m55_data_INTERNAL (256KB) 本就近满, 静态 .bss 会直接链接溢出
 * (实测 2x16KB 静态数组 -> overflow 13828B); RT-Thread 堆在系统区。 */
static lc3_decoder_t    s_dec[2];       /* [0]=L [1]=R */
static rt_uint8_t      *s_dec_mem;      /* 两块解码器内存的基址 */
static rt_bool_t        s_dec_ready;

/* 每声道待解帧 */
static rt_uint8_t       s_frame[2][FT_LC3_FRAME_MAX];
static rt_uint16_t      s_frame_len[2];
static rt_uint8_t       s_frame_have[2];

/* ring 流式拼块 */
static rt_uint8_t       s_buf[FT_LC3_HDR_LEN + FT_LC3_FRAME_MAX];
static rt_uint32_t      s_buf_have;
static rt_uint32_t      s_buf_want;     /* 当前帧总长 (含 3B 头), 0 = 待头 */

/* sound0 输出 */
static rt_device_t      s_sound_dev;
static rt_bool_t        s_sound_open;
static rt_bool_t        s_claim_ok;
static rt_uint8_t       s_cur_volume = 0xFFU;
static int16_t          s_out_buf[FT_OUT_BLOCK_SAMPLES];
static rt_uint32_t      s_out_fill;

/* 水位状态 (M4b 同款) */
static rt_bool_t        s_out_active;
static rt_tick_t        s_empty_since;
static rt_bool_t        s_starve_counted;
static rt_uint32_t      s_cur_gen;

/* 统计 (msh ft_lc3_stats) */
static rt_uint32_t      s_stat_frames;      /* 完成解码的立体声帧周期数 */
static rt_uint32_t      s_stat_plc;         /* PLC 补齐的声道帧数 */
static rt_uint32_t      s_stat_bad_len;     /* 帧头非法重同步次数 */
static rt_uint32_t      s_stat_pcm_bytes;
static rt_uint64_t      s_stat_energy;
static rt_uint32_t      s_stat_samples;
static rt_uint32_t      s_stat_out_blocks;
static rt_uint32_t      s_stat_out_bytes;
static rt_uint32_t      s_stat_streams;
static rt_uint32_t      s_stat_starves;
static rt_uint32_t      s_stat_prebuf_ms;
static rt_uint32_t      s_last_ch_mask;     /* 最近解码的声道组合 */

/* 攒 4096B 块写 sound0 (与 SBC 链同款) */
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
                s_out_active = RT_TRUE;
                rt_device_write(s_sound_dev, 0, s_out_buf, FT_OUT_BLOCK_BYTES);
            }
            s_stat_out_blocks++;
            s_stat_out_bytes += FT_OUT_BLOCK_BYTES;
            s_out_fill = 0U;
        }
    }
}

/* 单声道帧解码 (in=RT_NULL 时 liblc3 走 PLC), 交织写入立体声缓冲 */
static void ft_lc3_decode_pair(const rt_uint8_t *in_l, rt_uint16_t len_l,
                               const rt_uint8_t *in_r, rt_uint16_t len_r)
{
    int16_t pcm[480 * 2];   /* 48k/10ms = 480 立体声帧, 双倍留余 */
    rt_uint16_t in[2] = { len_l, len_r };
    rt_uint32_t samples = (rt_uint32_t)lc3_frame_samples(FT_LC3_FRAME_US, FT_LC3_RATE_HZ);

    if (samples == 0U || samples > 480U)
    {
        return;
    }
    for (rt_uint32_t ch = 0U; ch < 2U; ch++)
    {
        const rt_uint8_t *in_p = (ch == 0U) ? in_l : in_r;
        if (in_p == RT_NULL)
        {
            s_stat_plc++;
        }
        /* stride=2: 直接交织写到 pcm[ch] (先清本声道, 解码覆盖) */
        lc3_decode(s_dec[ch], (in_p != RT_NULL) ? in_p : RT_NULL,
                   (ch == 0U) ? (int)in[0] : (int)in[1],
                   LC3_PCM_FORMAT_S16, &pcm[ch], 2);
    }
    for (rt_uint32_t i = 0U; i < samples * 2U; i++)
    {
        rt_int32_t v = pcm[i];
        s_stat_energy += (rt_uint32_t)((v < 0) ? -v : v);
    }
    s_stat_samples += samples * 2U;
    s_stat_pcm_bytes += samples * 4U;
    s_stat_frames++;
    s_last_ch_mask = (in_l != RT_NULL ? 0x01U : 0U) | (in_r != RT_NULL ? 0x02U : 0U);
    ft_sound_push(pcm, samples * 2U);
}

/* 收到某声道一帧:
 *  - 本声道还挂着未配对旧帧 (对端连续丢帧) -> PLC 补对端解掉旧帧
 *  - 双声道齐 -> 立体声配对解码; 只有一边 -> 挂起等配对 (对端缺则 PLC) */
static void ft_lc3_on_frame(rt_uint8_t ch, const rt_uint8_t *data, rt_uint16_t len)
{
    rt_uint8_t other = (ch == 0U) ? 1U : 0U;

    if (s_frame_have[ch])
    {
        if (ch == 0U)
        {
            ft_lc3_decode_pair(s_frame[0], s_frame_len[0], RT_NULL, 0U);
        }
        else
        {
            ft_lc3_decode_pair(RT_NULL, 0U, s_frame[1], s_frame_len[1]);
        }
        s_frame_have[ch] = 0U;
    }
    memcpy(s_frame[ch], data, len);
    s_frame_len[ch] = len;
    s_frame_have[ch] = 1U;

    if (s_frame_have[other])
    {
        ft_lc3_decode_pair(s_frame[0], s_frame_len[0], s_frame[1], s_frame_len[1]);
        s_frame_have[0] = 0U;
        s_frame_have[1] = 0U;
    }
}

/* 新 LE Audio 流开始 (fmt_gen 换代且 flags bit0): 复位解码器, 开 sound0 */
void ft_lc3_stream_begin(void)
{
    unsigned mem_sz = (lc3_decoder_size(FT_LC3_FRAME_US, FT_LC3_RATE_HZ) + 3U) & ~3U;

    s_dec_mem = (rt_uint8_t *)rt_malloc(mem_sz * 2U);
    if (s_dec_mem == RT_NULL)
    {
        rt_kprintf("[LC3] decoder mem alloc failed (%u x2)\n", mem_sz);
return;
    }
    s_dec[0] = lc3_setup_decoder(FT_LC3_FRAME_US, FT_LC3_RATE_HZ,
                                 FT_LC3_RATE_HZ, &s_dec_mem[0]);
    s_dec[1] = lc3_setup_decoder(FT_LC3_FRAME_US, FT_LC3_RATE_HZ,
                                 FT_LC3_RATE_HZ, &s_dec_mem[mem_sz]);
    if ((s_dec[0] == RT_NULL) || (s_dec[1] == RT_NULL))
    {
        rt_kprintf("[LC3] decoder setup failed (mem_sz=%u)\n", mem_sz);
rt_free(s_dec_mem);
        s_dec_mem = RT_NULL;
        return;
    }

    memset(s_frame_have, 0, sizeof(s_frame_have));
    s_frame_len[0] = 0U;
    s_frame_len[1] = 0U;
    s_buf_have = 0U;
    s_buf_want = 0U;
    s_stat_frames = 0U;
    s_stat_plc = 0U;
    s_stat_bad_len = 0U;
    s_stat_pcm_bytes = 0U;
    s_stat_energy = 0U;
    s_stat_samples = 0U;
    s_stat_out_blocks = 0U;
    s_stat_out_bytes = 0U;
    s_stat_streams++;
    s_stat_starves = 0U;
    s_stat_prebuf_ms = 0U;
    s_stat_plc = 0U;
    s_out_fill = 0U;
    s_out_active = RT_FALSE;
    s_empty_since = 0;
    s_starve_counted = RT_FALSE;

    /* 流格式 (M33 换代前已写入 ring 控制块): LE Audio 恒 48k */
    FT_ALINK_DCACHE_INVALID(FT_ALINK_BASE, 32);

    s_claim_ok = (ft_audio_claim_output(FT_AUDIO_OUTPUT_OWNER_BT_LE_AUDIO) == RT_EOK);
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
    s_cur_volume = 0xFFU;

    /* 预缓冲 (M4b 同款): 首块出声前垫水位 */
    s_cur_gen = FT_ALINK->fmt_gen;
    if (s_sound_open)
    {
        rt_tick_t t0 = rt_tick_get();
        while ((rt_tick_get() - t0) < rt_tick_from_millisecond(FT_PREBUF_TIMEOUT_MS))
        {
            FT_ALINK_DCACHE_INVALID(FT_ALINK_BASE, 32);
            if (FT_ALINK->fmt_gen != s_cur_gen)
            {
                break;
            }
            if (ft_alink_used(FT_ALINK) >= FT_PREBUF_BYTES)
            {
                break;
            }
            rt_thread_mdelay(10);
        }
        s_stat_prebuf_ms = (rt_uint32_t)(rt_tick_get() - t0);
    }
    s_dec_ready = RT_TRUE;
    rt_kprintf("[LC3] decoder ready (stream #%lu, 48k/10ms stereo, sound0 %s)\n",
(unsigned long)s_stat_streams,
               s_sound_open ? "open" : (s_claim_ok ? "open-failed" : "busy"));
}

/* 流结束 (fmt_rate=0 的换代): 冲刷尾块, close + release */
void ft_lc3_stream_end(void)
{
    if (s_dec_ready)
    {
        if (s_sound_open && (s_out_fill > 0U))
        {
            rt_device_write(s_sound_dev, 0, s_out_buf, s_out_fill * 2U);
            s_stat_out_bytes += s_out_fill * 2U;
            s_out_fill = 0U;
        }
        rt_kprintf("[LC3] stream end: frames=%lu plc=%lu out=%lu B (blocks=%lu)\n",
(unsigned long)s_stat_frames, (unsigned long)s_stat_plc,
                   (unsigned long)s_stat_out_bytes,
                   (unsigned long)s_stat_out_blocks);
    }
    s_dec_ready = RT_FALSE;
    s_out_active = RT_FALSE;
    s_empty_since = 0;
    s_starve_counted = RT_FALSE;
    if (s_dec_mem != RT_NULL)
    {
        rt_free(s_dec_mem);
        s_dec_mem = RT_NULL;
    }
    if (s_sound_open && (s_sound_dev != RT_NULL))
    {
        rt_device_close(s_sound_dev);
    }
    s_sound_open = RT_FALSE;
    if (s_claim_ok)
    {
        ft_audio_release_output(FT_AUDIO_OUTPUT_OWNER_BT_LE_AUDIO);
    }
    s_claim_ok = RT_FALSE;
}

/* 喂入帧化字节流 (可跨 ring 读块任意切分) */
void ft_lc3_feed(const rt_uint8_t *data, rt_uint32_t len)
{
    if (!s_dec_ready)
    {
        return;
    }

    /* 音量 (ring 控制块 volume_percent, M33 侧 BAP VCS 时接入; 现恒 0xFF) */
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

        if (s_buf_want == 0U)
        {
            need = FT_LC3_HDR_LEN - s_buf_have;
            take = (len < need) ? len : need;
            memcpy(s_buf + s_buf_have, data, take);
            s_buf_have += take;
            data += take;
            len -= take;
            if (s_buf_have < FT_LC3_HDR_LEN)
            {
                break;
            }
            rt_uint32_t flen = (rt_uint32_t)s_buf[0] |
                               ((rt_uint32_t)s_buf[1] << 8);
            rt_uint8_t ch = s_buf[2];
            if (flen < 1U || flen > FT_LC3_FRAME_MAX || (ch != 1U && ch != 2U))
            {
                /* 非法头: 丢 1B 重同步 */
                memmove(s_buf, s_buf + 1, FT_LC3_HDR_LEN - 1U);
                s_buf_have = FT_LC3_HDR_LEN - 1U;
                s_stat_bad_len++;
                continue;
            }
            s_buf_want = FT_LC3_HDR_LEN + flen - 1U;
            /* flen 含声道字节: 帧体 = flen - 1 */
        }

        need = s_buf_want - s_buf_have;
        take = (len < need) ? len : need;
        memcpy(s_buf + s_buf_have, data, take);
        s_buf_have += take;
        data += take;
        len -= take;
        if (s_buf_have < s_buf_want)
        {
            break;
        }

        rt_uint8_t ch = (s_buf[2] == 1U) ? 0U : 1U;
        ft_lc3_on_frame(ch, &s_buf[FT_LC3_HDR_LEN],
                        (rt_uint16_t)(s_buf_want - FT_LC3_HDR_LEN));
        s_buf_have = 0U;
        s_buf_want = 0U;
    }
}

/* ---- 水位管理 (M4b 同款, audio_link_m55.c 每轮调用) ---- */
rt_bool_t ft_lc3_watermark_tick(void)
{
    if (!s_dec_ready || !s_sound_open || !s_out_active)
    {
        return RT_TRUE;
    }

    rt_uint32_t used = ft_alink_used(FT_ALINK);
    /* Only a continuous empty interval is starvation. Normal 10-ms packets
     * must cancel the timer even if they are smaller than the rebuffer target. */
    if (used != 0U && !s_starve_counted)
    {
        s_empty_since = 0;
        return RT_TRUE;
    }
    if (s_empty_since == 0)
    {
        if (used != 0U)
        {
            return RT_TRUE;
        }
        s_empty_since = rt_tick_get();
        return RT_TRUE;
    }
    if (used >= FT_REPREBUF_BYTES)
    {
        s_empty_since = 0;
        s_starve_counted = RT_FALSE;
        return RT_TRUE;
    }
    if (!s_starve_counted &&
        (rt_tick_get() - s_empty_since) >= rt_tick_from_millisecond(FT_STARVE_HOLDOFF_MS))
    {
        s_starve_counted = RT_TRUE;
        s_stat_starves++;
        rt_kprintf("[LC3] starve #%lu: ring empty %lu ms, hold until %lu B\n",
(unsigned long)s_stat_starves,
                   (unsigned long)FT_STARVE_HOLDOFF_MS,
                   (unsigned long)FT_REPREBUF_BYTES);
    }
    return s_starve_counted ? RT_FALSE : RT_TRUE;
}

/* ---- LC3 自环测试 (M8.1 管线验证, 不依赖手机/BT) ----
 * 本机 liblc3 编码 3 秒 440Hz 正弦 -> 按 ring 帧格式喂回真实解码路径
 * -> sound0 出声。验证: 解码器/声道配对/攒块/音量/sound0 全链路。
 * M8.0 教训: 帧编解码栈深, 测试体放独立 16KB 栈线程 (tshell 4KB 必溢出)。
 * msh: ft_lc3_test */
#define FT_LC3_ENC_BYTES 120

static void ft_lc3_test_worker(void *parameter)
{
    const rt_uint32_t frames = 300;              /* 3 秒 */
    const rt_uint32_t samples = 480;             /* 48k/10ms 每声道 */
    rt_uint8_t *enc = RT_NULL;
    int16_t *pcm = RT_NULL;
    rt_uint8_t *enc_mem = RT_NULL;
    lc3_encoder_t enc_l, enc_r;
    rt_uint32_t f;

    if (s_dec_ready)
    {
        rt_kprintf("[LC3] test: stream busy, stop it first\n");
        return;
    }

    unsigned enc_size_work = (lc3_encoder_size(FT_LC3_FRAME_US, FT_LC3_RATE_HZ) + 3U) & ~3U;
    pcm = (int16_t *)rt_malloc(sizeof(int16_t) * samples * 2);
    enc = (rt_uint8_t *)rt_malloc((FT_LC3_ENC_BYTES + 3U) * frames * 2U);
    enc_mem = (rt_uint8_t *)rt_malloc(enc_size_work * 2U);
    if ((pcm == RT_NULL) || (enc == RT_NULL) || (enc_mem == RT_NULL))
    {
        rt_kprintf("[LC3] test alloc failed\n");
        if (pcm) rt_free(pcm);
        if (enc) rt_free(enc);
        if (enc_mem) rt_free(enc_mem);
        return;
    }
    enc_l = lc3_setup_encoder(FT_LC3_FRAME_US, FT_LC3_RATE_HZ, FT_LC3_RATE_HZ, &enc_mem[0]);
    enc_r = lc3_setup_encoder(FT_LC3_FRAME_US, FT_LC3_RATE_HZ, FT_LC3_RATE_HZ, &enc_mem[enc_size_work]);

    for (f = 0; f < frames; f++)
    {
        for (rt_uint32_t i = 0; i < samples; i++)
        {
            int32_t v = (int32_t)(12000.0 * sin(2.0 * 3.14159265358979 * 440.0 *
                                                (double)(f * samples + i) / FT_LC3_RATE_HZ));
            pcm[i * 2] = (int16_t)v;
            pcm[i * 2 + 1] = (int16_t)v;
        }
        lc3_encode(enc_l, LC3_PCM_FORMAT_S16, pcm, 2, FT_LC3_ENC_BYTES, &enc[f * 2 * FT_LC3_ENC_BYTES]);
        lc3_encode(enc_r, LC3_PCM_FORMAT_S16, pcm + 1, 2, FT_LC3_ENC_BYTES, &enc[(f * 2 + 1) * FT_LC3_ENC_BYTES]);
    }
    rt_free(enc_mem);
    enc_mem = RT_NULL;
    rt_kprintf("[LC3] test: %lu frames encoded, starting decode->sound0\n",
               (unsigned long)frames);

    ft_lc3_stream_begin();
    if (!s_dec_ready)
    {
        rt_kprintf("[LC3] test: decoder not ready\n");
        rt_free(pcm); rt_free(enc); rt_free(enc_mem);
        return;
    }

    /* 按 ring 帧格式喂回解码路径: [len16][ch][帧], len = 1 + 帧长 */
    for (f = 0; f < frames; f++)
    {
        rt_uint8_t hdr_l[3] = { (rt_uint8_t)((FT_LC3_ENC_BYTES + 1U) & 0xFF),
                                (rt_uint8_t)(((FT_LC3_ENC_BYTES + 1U) >> 8) & 0xFF), 0x01 };
        rt_uint8_t hdr_r[3] = { (rt_uint8_t)((FT_LC3_ENC_BYTES + 1U) & 0xFF),
                                (rt_uint8_t)(((FT_LC3_ENC_BYTES + 1U) >> 8) & 0xFF), 0x02 };
        ft_lc3_feed(hdr_l, 3U);
        ft_lc3_feed(&enc[f * 2 * FT_LC3_ENC_BYTES], FT_LC3_ENC_BYTES);
        ft_lc3_feed(hdr_r, 3U);
        ft_lc3_feed(&enc[(f * 2 + 1) * FT_LC3_ENC_BYTES], FT_LC3_ENC_BYTES);
    }
    /* 收尾: 冲刷并关流 */
    ft_lc3_stream_end();
    rt_free(pcm);
    rt_free(enc);
    rt_kprintf("[LC3] test done (speaker should have played 3s 440Hz)\n");
}

static int ft_lc3_test(int argc, char **argv)
{
    rt_thread_t t = rt_thread_create("lc3t", ft_lc3_test_worker, RT_NULL,
                                     16384, 20, 10);
    if (t == RT_NULL)
    {
        rt_kprintf("[LC3] test thread alloc failed\n");
        return -1;
    }
    rt_thread_startup(t);
    return 0;
}
MSH_CMD_EXPORT_ALIAS(ft_lc3_test, ft_lc3_test,
                     M8.1: LC3 pipeline self-test (3s sine -> sound0));

static int ft_lc3_stats(int argc, char **argv)
{
    (void)argc; (void)argv;
    rt_kprintf("[LC3] ready=%d streams=%lu frames=%lu plc=%lu bad_len=%lu ch_mask=%u\n",
(int)s_dec_ready, (unsigned long)s_stat_streams,
               (unsigned long)s_stat_frames, (unsigned long)s_stat_plc,
               (unsigned long)s_stat_bad_len, s_last_ch_mask);
    rt_kprintf("[LC3] pcm=%lu B samples=%lu energy_avg=%lu\n",
(unsigned long)s_stat_pcm_bytes, (unsigned long)s_stat_samples,
               s_stat_samples ? (unsigned long)(s_stat_energy / s_stat_samples) : 0UL);
    rt_kprintf("[LC3] sound0: claim=%d open=%d, wrote %lu blocks/%lu B, prebuf=%lu ms, starves=%lu\n",
(int)s_claim_ok, (int)s_sound_open,
               (unsigned long)s_stat_out_blocks, (unsigned long)s_stat_out_bytes,
               (unsigned long)s_stat_prebuf_ms, (unsigned long)s_stat_starves);
    return 0;
}
MSH_CMD_EXPORT_ALIAS(ft_lc3_stats, ft_lc3_stats,
                     M8.1: show LC3 decode + sound0 output stats);
