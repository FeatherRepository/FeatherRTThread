/* lc3_bench.c - M8.0 探针3: liblc3 (google) 在 M55 的解码/编码基准
 *
 * 场景: LE Audio 48kHz/10ms 帧/立体声, LC3 帧 120 字节 (bitpool 常用值)。
 * 流程: 合成正弦 PCM -> lc3_encode 出帧 -> lc3_decode 回 PCM,
 * DWT CYCCNT 测每帧解码/编码周期, 折算单帧预算占用 (帧周期 10ms)。
 * RAM: 编码器+解码器实例大小 (双声道) 由 liblc3 报告。
 *
 * msh: lc3_bench
 */
#include <rtthread.h>
#include <string.h>
#include <math.h>
#include "lc3.h"

#include <feathertalk/audio_link.h>   /* 顺带引入 CMSIS core 头 (DWT) */

#define DT_US        10000
#define SR_HZ        48000
#define NUM_CH       2
#define FRAME_SAMPLES 480            /* 48k * 10ms */
#define FRAME_BYTES  120
#define BENCH_FRAMES 300

static rt_uint32_t s_dwt_ready;

static void dwt_init(void)
{
    if (s_dwt_ready) return;
#if defined(CoreDebug_DEMCR_TRCENA_Msk)
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
#elif defined(DWT_DEMCR_TRCENA_Msk)
    DWT->DEMCR |= DWT_DEMCR_TRCENA_Msk;
#else
    CoreDebug->DEMCR |= (1UL << 24);
#endif
    DWT->CYCCNT = 0;
    DWT->CTRL |= 1;
    s_dwt_ready = 1;
}

/* 基准工作体: 必须跑在大栈线程 —— liblc3 编解码内部栈消耗大,
 * 在 tshell (4KB 栈) 直接跑会栈溢出 (实测踩坏内核对象 -> 断言挂死) */
static void lc3_bench_worker(void *param)
{
    (void)param;
    dwt_init();

    /* 实例内存 (双声道): liblc3 实测 enc≈5.4KB dec≈9.1KB 每声道。
     * 走堆分配 — m55_data_INTERNAL 静态区已顶满 (链接器实测溢出),
     * 主堆 1.3MB 空闲无压力 */
    unsigned enc_size = lc3_encoder_size(DT_US, SR_HZ);
    unsigned dec_size = lc3_decoder_size(DT_US, SR_HZ);
    uint8_t *enc_mem = rt_malloc(enc_size * NUM_CH);
    uint8_t *dec_mem = rt_malloc(dec_size * NUM_CH);
    int16_t *pcm_in = rt_malloc(sizeof(int16_t) * NUM_CH * FRAME_SAMPLES);
    int16_t *pcm_out = rt_malloc(sizeof(int16_t) * NUM_CH * FRAME_SAMPLES);
    if (!enc_mem || !dec_mem || !pcm_in || !pcm_out)
    {
        rt_free(enc_mem); rt_free(dec_mem);
        rt_free(pcm_in); rt_free(pcm_out);
        rt_kprintf("[LC3] OOM\n");
        return -1;
    }

    lc3_encoder_t enc[NUM_CH], dec[NUM_CH];
    for (int ch = 0; ch < NUM_CH; ch++)
    {
        enc[ch] = lc3_setup_encoder(DT_US, SR_HZ, 0, enc_mem + ch * enc_size);
        dec[ch] = lc3_setup_decoder(DT_US, SR_HZ, 0, dec_mem + ch * dec_size);
    }

    /* 输入 PCM: 1kHz 正弦 */
    for (int i = 0; i < FRAME_SAMPLES; i++)
    {
        int16_t v = (int16_t)(12000.0 * sin(2.0 * 3.14159265 * 1000.0 * i / SR_HZ));
        for (int ch = 0; ch < NUM_CH; ch++) pcm_in[i * NUM_CH + ch] = v;
    }

    uint8_t frames[NUM_CH][FRAME_BYTES];

    /* 两轮测量: LTPF 开(默认) / LTPF 关 —— 编码占帧周期 137% 超实时,
     * liblc3 文档注明 LTPF 分析吃大量 CPU, 量化它的占比决定 Source 可行性 */
    for (int pass = 0; pass < 2; pass++)
    {
        if (pass == 1)
            for (int ch = 0; ch < NUM_CH; ch++)
                lc3_encoder_disable_ltpf(enc[ch]);

        rt_uint64_t enc_total = 0, dec_total = 0;
        rt_uint32_t enc_max = 0, dec_max = 0;
        int plc_frames = 0, enc_err = 0;

        for (int f = 0; f < BENCH_FRAMES; f++)
        {
            rt_uint32_t c0 = DWT->CYCCNT;
            for (int ch = 0; ch < NUM_CH; ch++)
            {
                if (lc3_encode(enc[ch], LC3_PCM_FORMAT_S16,
                               pcm_in + ch, NUM_CH, FRAME_BYTES, frames[ch]) != 0)
                    enc_err++;
            }
            rt_uint32_t c1 = DWT->CYCCNT;
            rt_uint32_t e = c1 - c0;
            enc_total += e;
            if (e > enc_max) enc_max = e;

            c0 = DWT->CYCCNT;
            for (int ch = 0; ch < NUM_CH; ch++)
            {
                if (lc3_decode(dec[ch], frames[ch], FRAME_BYTES,
                               LC3_PCM_FORMAT_S16, pcm_out + ch, NUM_CH) == 1)
                    plc_frames++;
            }
            c1 = DWT->CYCCNT;
            e = c1 - c0;
            dec_total += e;
            if (e > dec_max) dec_max = e;
        }

        rt_uint32_t mhz = SystemCoreClock / 1000000U;
        rt_uint32_t dec_us = (rt_uint32_t)(dec_total / BENCH_FRAMES / mhz);
        rt_uint32_t enc_us = (rt_uint32_t)(enc_total / BENCH_FRAMES / mhz);
        rt_uint32_t cpu_x100 = (rt_uint32_t)((rt_uint64_t)dec_us * 10000U / DT_US);
        rt_uint32_t enc_x100 = (rt_uint32_t)((rt_uint64_t)enc_us * 10000U / DT_US);
        rt_kprintf("[LC3] pass%d (%s) %d frames @%luMHz: "
                   "decode %lu us (%lu.%02lu%% frame, max %lu us) | "
                   "encode %lu us (%lu.%02lu%% frame, max %lu us)\n",
                   pass, pass ? "LTPF OFF" : "LTPF ON", BENCH_FRAMES,
                   (unsigned long)mhz,
                   (unsigned long)dec_us,
                   (unsigned long)(cpu_x100 / 100U), (unsigned long)(cpu_x100 % 100U),
                   (unsigned long)(dec_max / mhz),
                   (unsigned long)enc_us,
                   (unsigned long)(enc_x100 / 100U), (unsigned long)(enc_x100 % 100U),
                   (unsigned long)(enc_max / mhz));
        if (pass == 1)
        {
            rt_kprintf("[LC3] RAM (heap): enc %uB x2 + dec %uB x2 + io %uB = %uB | "
                       "enc_err=%d plc=%d\n",
                       enc_size, dec_size,
                       (unsigned)(sizeof(int16_t) * NUM_CH * FRAME_SAMPLES * 2U),
                       (unsigned)(enc_size * 2 + dec_size * 2 +
                                  sizeof(int16_t) * NUM_CH * FRAME_SAMPLES * 2U),
                       enc_err, plc_frames);
            rt_kprintf("[LC3] Source 端判定: LTPF 关后 encode %s 10ms 帧周期 -> %s\n",
                       pass ? "" : "",
                       (enc_us < DT_US) ? "可实时 (Source 可行)" :
                       (enc_us < DT_US * 3 / 2) ? "仍超实时但接近 (需再优化)" :
                       "不可行 (需 MVE 向量化或降规格)");
        }
    }
    rt_free(enc_mem); rt_free(dec_mem);
    rt_free(pcm_in); rt_free(pcm_out);
}

/* msh 入口: 起独立 16KB 栈线程跑基准 (liblc3 内部栈消耗大) */
static int lc3_bench(int argc, char **argv)
{
    (void)argc; (void)argv;
    rt_thread_t t = rt_thread_create("lc3b", lc3_bench_worker, RT_NULL,
                                     16384, 20, 10);
    if (t == RT_NULL)
    {
        rt_kprintf("[LC3] create bench thread failed\n");
        return -1;
    }
    rt_thread_startup(t);
    return 0;
}
MSH_CMD_EXPORT_ALIAS(lc3_bench, lc3_bench, M8.0: liblc3 decode/encode benchmark on M55);
