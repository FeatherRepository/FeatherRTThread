/*
 * KWS 唤醒词引擎（小麦项目 R0）
 *
 * 基于 Edge Impulse 实现（源自 Edgi_Talk_M55_XiaoZhi 的 xiaozhi_wakeword.cpp），
 * 针对小麦项目做了三项改进：
 *   1. 滑窗推理（可配置 hop）—— XiaoZhi 版是非重叠 1s 窗，唤醒词跨窗会漏检；
 *      改为环形缓冲 + hop 滑窗，延迟与漏检率同时下降。
 *   2. 唤醒词标签可配置（KWS_WAKE_WORD_LABEL），不再是硬编码 "xiaorui"。
 *   3. 分数日志 + 统计——每 N 次推理打印 top-2 分数，用于阈值标定；
 *      新增 kws record 命令直接录麦克风原始 PCM 到 SD 卡（R0-1 验证用）。
 */

#include "kws.h"
#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>
#include <rtconfig.h>
/* feathertalk_audio.h 是 C 头（工程惯例，无 extern "C" 包裹），从 C++ 引入必须自己圈 */
extern "C" {
#include "feathertalk_audio.h"
#include "ipc/feathertalk_ipc.h"
#include "led_indicator.h"
}

#include <fcntl.h>
#include <sys/unistd.h>
#include <math.h>
#include <new>

/* Edge Impulse SDK */
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "edge-impulse-sdk/dsp/numpy.hpp"
#include "model-parameters/model_metadata.h"

/* 工具链坑（已证）：libm.a(thumb/v8-m.main+dp/hard) 的 fmaf 全部码是
 *   vfma.f32 s0, s0, s1 ; bx lr  → 返回 a*b+a，第三个参数（在 s2）被丢掉。
 * 本工程 BUILD='debug' 分支不加 -O 档（-O2 在 else 里），-O0 下 GCC 不把 fmaf 折成
 * 硬件 vfma 而是真调库，于是 ei::numpy::log 的 5 次 fmaf 全中招：快速 log 恒等于
 * i*(1+ln2) → mel 端点放大 2.688 倍 → 32 个三角滤波 19 个钉在 Nyquist。
 * 给一个定义正确的 fmaf 顶掉库里的（a*b+c，-O0 下多一次舍入 ~1e-7，远小于容差 0.02）。 */
extern "C" float fmaf(float a, float b, float c)
{
    return a * b + c;
}

#define DBG_TAG "kws"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

/* ---------------- 配置 ---------------- */

#ifndef KWS_MIC_DEVICE_NAME
#define KWS_MIC_DEVICE_NAME "mic0"
#endif

/* PDM 帧几何必须和驱动 drv_pdm.c 的填充方式**用同一个宏**判定：
 * ENABLE_STEREO_INPUT_FEED 下每次中断交错写 L,R（10ms 帧 = 320 样本），否则每帧 160 样本。
 * 这里原先自造了同义不同名的 KWS_ENABLE_STEREO_INPUT_FEED（全工程无人定义），
 * 于是把交错的 L,R 当连续单声道读 = 2× 提速 + 双麦逐样本混叠，见 local_ai/firmware/CHANGES.md */
#ifdef ENABLE_STEREO_INPUT_FEED
#define KWS_PDM_FRAME_SAMPLES   320
#define KWS_PDM_MONO_FRAME_SAMPLES (KWS_PDM_FRAME_SAMPLES / 2)
#define KWS_PDM_IS_STEREO 1
#else
#define KWS_PDM_FRAME_SAMPLES   160
#define KWS_PDM_MONO_FRAME_SAMPLES KWS_PDM_FRAME_SAMPLES
#define KWS_PDM_IS_STEREO 0
#endif
#define KWS_PDM_FRAME_SIZE (KWS_PDM_FRAME_SAMPLES * sizeof(int16_t))

/* 每多少次推理打印一次分数（0 = 关闭） */
#ifndef KWS_SCORE_LOG_INTERVAL
#define KWS_SCORE_LOG_INTERVAL 20
#endif

/* kws eval 的特征缓冲上限（当前模型 nn_input_frame_size = 50 帧 × 13 cepstral = 650） */
#define KWS_EVAL_MAX_FEATURES 1024

/* ---------------- 状态 ---------------- */

static rt_device_t   s_audio_dev = RT_NULL;
static int16_t      *s_ring = RT_NULL;   /* 从堆分配（放 m55_data_secondary，避免占用内部 RAM） */
static volatile rt_bool_t s_enabled = RT_FALSE;
static rt_bool_t     s_inited = RT_FALSE;
static rt_thread_t   s_tid = RT_NULL;
static uint32_t      s_last_detection = 0;
static uint8_t       s_hist[KWS_HIST_LEN];   /* 唤醒词分数历史（持续性判据） */
static uint8_t       s_hist_pos = 0;
/* 每 hop 唤醒词概率的直方图：阈值只能由分布选，不能被几条抽样日志选。
 * kws start 清零、kws info 打印，空场跑一轮就知道"环境音到底有多少窗 >50%"。 */
static uint16_t      s_score_hist[11];
static float         s_score_max = 0.0f;
/* 判决三参数做成运行期可调：同一块板子、同一个模型，能在一次会话里扫完
 * "阈值 → 误触率/召回率"，不必每档重烧一次固件。宏值是默认值。 */
static float         s_conf        = KWS_CONFIDENCE;
static int           s_hist_hits   = KWS_HIST_HITS;
static uint32_t      s_cooldown_ms = KWS_COOLDOWN_MS;
static volatile int  s_dump_frames = 0;      /* >0 时线程进入转储模式：把麦克风样本流到串口 */
static volatile int  s_dump_idx = 0;         /* 已吐出的帧号，写进每行行首给 PC 定位（丢行可检出） */
/* kws capture：先把连续音频录进 RAM，录满后再按帧回吐串口。
 * 16kHz mono int16 = 32KB/s，而 hex 行 + 115200 波特最多带 2.88K 样本/s（18%），
 * 实时转储必然丢帧 ⇒ 板采样本必须走这条"先录后吐"的路。 */
static volatile int  s_cap_frames = 0;       /* 还差多少帧没录（0 = 不在采集） */
static int16_t      *s_cap_buf = RT_NULL;
static int           s_cap_len = 0;          /* RAM 中已录样本数 */
static int           s_cap_pos = 0;          /* 回吐游标 */

/* kws eval：录满后不回吐，而是把 RAM 里的音频按整窗重新喂给推理路径，逐窗打印
 * 板上分数（EVALW），评完后打印第 0 窗的 C 复刻浮点特征（EVALF），最后再走原
 * 回吐把同一段音频交给 PC。三方对账（板上分数 / 板上 C 复刻特征 / PC python 复刻）
 * 吃的是**同一段连续音频**——之前板上 554 命中/小时而 PC 复刻预测 0 命中，
 * 差在链路哪一环必须由这个口径判，不能再靠"应该同源"。 */
static volatile int  s_eval_windows = 0;     /* 请求：录完后评几窗 */
static volatile int  s_eval_run = 0;         /* 还差几窗没评 */
static int           s_eval_idx = 0;         /* 已评到第几窗 */

/* kws push/play：把 PCM 灌进 RAM，再从**板载喇叭**放出来（自测台的激励端）。
 * 激励源必须是设备自己的喇叭——小麦的应答本来就从这个出声，麦克风贴在同一个壳上
 * 听它，机内声路耦合才是真实的"自问自答误触"场景；PC 喇叭对着板子吹是另一回事
 * （上一轮 proof 已证那条路在本机不可信：数字静音对照和真音抬升一样高）。
 * 送 PCM 进板只有三条路：USB UAC、USB MSC 拷文件（两者都要接目标 USB 线，本机没接）、
 * 串口。串口 115200 下 hex 每样本 4 字符 ⇒ 净最多 2880 样本/s，而 16kHz 实时流要
 * 16000 样本/s（18%）⇒ 不能边灌边放，只能"整段灌满 RAM 再回放"，方向与 kws capture
 * 的回吐正好相反。放音走 sound0（ES8388 DAC→MD8002→喇叭），与 mic0（PDM）是两条独立
 * 管线，所以能在 kws start 监听的同时放：推理听到的就是喇叭发出的。 */
#define KWS_PLAY_SR          16000        /* 与模型采样率一致，回放不需重采样 */
#define KWS_PLAY_MAX_SEC     8            /* 8s mono16 = 256KB 堆 */
/* 灌数的对速瓶颈实测是**控制台每行固定开销**，不是波特率：16 样本/行时停止等待 ARQ 只跑
 * 到 392 样本/s（≈41ms/行，而波特率理论值 2400 样本/s）。所以把 FINSH_CMD_SIZE 80→1024、
 * RT_SERIAL_RB_BUFSZ 128→2048，一行带 250 样本（1000 个 hex 字符 + "kws push 999 " 前缀
 * = 1010 字符，仍在 1023 可用之内），行数减到 1/15.6。RT_CONSOLEBUF_SIZE=1024 只管日志
 * 输出，不管输入行。节奏仍由 PUSHACK 停止等待决定。 */
#define KWS_PUSH_SAMPLES     250
/* 板自己的单调时基（ms，RT_TICK_PER_SECOND=1000）。ulog 不带时间戳，声学回踩要靠
 * PLAY_BEGIN 与"唤醒词命中"落在同一把尺子上归因，所以这两类日志都打这个。 */
static inline rt_uint32_t board_ms(void) { return (rt_uint32_t)rt_tick_get(); }
static int16_t      *s_play_buf = RT_NULL;
static int           s_play_cap = 0;          /* 缓冲容量（样本） */
static volatile int  s_play_len = 0;          /* 已灌入样本数 */
static volatile int  s_play_seq = 0;          /* 期望的下一块序号（乱序即串口挤爆，PC 重发） */
static volatile uint32_t s_play_sum = 0;      /* 增量校验和，PC 用同一定义复算（剂量自证） */
static volatile int  s_play_peak = 0;
static volatile int  s_play_drop = 0;         /* 缓冲满之后被丢掉的样本数 */
static volatile rt_bool_t s_play_running = RT_FALSE;
static volatile rt_bool_t s_play_abort = RT_FALSE;
static volatile rt_uint32_t s_play_loops = 1;
static rt_thread_t   s_play_tid = RT_NULL;
static struct rt_semaphore s_play_req;   /* kws play → 常驻回放线程的请求铃 */
static rt_bool_t s_play_ready = RT_FALSE;/* 线程+信号量是否已建立（只建一次） */

static rt_event_t    s_event = RT_NULL;
static kws_detected_cb s_cb = RT_NULL;

/* 统计 */
static uint32_t s_inferences = 0;
static uint32_t s_detections = 0;
static uint32_t s_dsp_total_ms = 0;
static uint32_t s_nn_total_ms = 0;
static float    s_last_score = 0.0f;
static int      s_retries = 0;

/* 音频交付率（流量计）。16kHz mono = 16 样本/ms：
 * ratio < 100% ⇒ 采集链路在丢样本（record pipe 溢出被 put_force 覆盖，或线程被推理压住）。
 * 这是"板上推理听到的音频"和"kws capture 录到的音频"是否同一段的关键判据。 */
static uint32_t s_flow_consumed = 0;   /* 累计消费的 mono 样本 */
static uint32_t s_flow_wall_ms  = 0;   /* 对应墙钟时长（只统计监听循环） */
static uint32_t s_flow_lossy  = 0;     /* 交付率 <95% 的轮数 */

#define KWS_EVENT_START (1 << 0)
#define KWS_EVENT_STOP  (1 << 1)
#define KWS_EVENT_EXIT  (1 << 2)

/* ---------------- 音频缓冲 ---------------- */

static void ring_init(void)
{
    if (s_ring)
        memset(s_ring, 0, EI_CLASSIFIER_RAW_SAMPLE_COUNT * sizeof(int16_t));
}

/* 把 samples 写入环形缓冲末尾（保持最新 EI_CLASSIFIER_RAW_SAMPLE_COUNT 个样本） */
static void ring_push(const int16_t *src, int n)
{
    int win = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
    /* 唯一的数据源是 mono_buf[KWS_PDM_MONO_FRAME_SAMPLES]（mic_read 最多返回这么多）。
     * 把这条不变式写进代码：-O2 下 GCC 否则会按 n 可等于 win 来分析 n>=win 那条死分支，
     * 报 mono_buf 越界读（-Warray-bounds），而这正是"编译器帮你找真越界"时最不该混进来的
     * 假警报。超过容量的请求按容量截断，宁可少推也不读缓冲区外的字节。 */
    if (n > KWS_PDM_MONO_FRAME_SAMPLES)
        n = KWS_PDM_MONO_FRAME_SAMPLES;
    if (n >= win)
    {
        memcpy(s_ring, src + (n - win), win * sizeof(int16_t));
        return;
    }
    memmove(s_ring, s_ring + n, (win - n) * sizeof(int16_t));
    memcpy(s_ring + (win - n), src, n * sizeof(int16_t));
}

static int signal_get_data(size_t offset, size_t length, float *out_ptr)
{
    if (offset + length > EI_CLASSIFIER_RAW_SAMPLE_COUNT)
    {
        LOG_E("bad signal request off=%zu len=%zu", offset, length);
        return -1;
    }
    numpy::int16_to_float(&s_ring[offset], out_ptr, length);
    return 0;
}

/* ---------------- 推理 ---------------- */

static void report_scores(const ei_impulse_result_t *result)
{
    int best = 0, second = 0;
    for (int i = 1; i < EI_CLASSIFIER_LABEL_COUNT; i++)
    {
        if (result->classification[i].value > result->classification[best].value)
        {
            second = best;
            best = i;
        }
        else if (result->classification[i].value > result->classification[second].value)
        {
            second = i;
        }
    }
    if (best == second) second = (best + 1) % EI_CLASSIFIER_LABEL_COUNT;

    LOG_I("[kws] #%u dsp=%dms nn=%dms top1=%s(%d%%) top2=%s(%d%%)",
          s_inferences,
          result->timing.dsp, result->timing.classification,
          ei_classifier_inferencing_categories[best],
          (int)(result->classification[best].value * 100),
          ei_classifier_inferencing_categories[second],
          (int)(result->classification[second].value * 100));

    /* 记录唤醒词最新分数，供 info 查看 */
    for (int i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++)
    {
        if (strstr(ei_classifier_inferencing_categories[i], KWS_WAKE_WORD_LABEL) != RT_NULL)
        {
            s_last_score = result->classification[i].value;
            break;
        }
    }
}

/* ---------------- 采集 ---------------- */

/* record pipe 是**字节流**，rt_device_read 允许返回半帧（min(请求, 可用)）。
 * 半帧会让解交错的 L/R 相位整体错开一位（把左声道当右声道取），且不会自愈，
 * 所以这里用静态残料缓冲凑齐整帧再返回。 */
static int16_t  s_raw_acc[KWS_PDM_FRAME_SAMPLES];
static int      s_raw_acc_n = 0;       /* 残料里的交错样本数 */
static uint32_t s_raw_short = 0;       /* 半帧读次数（诊断用） */

/* 停流看门狗：连续 KWS_MIC_STALL_MS 拿不到任何麦克风样本 ⇒ 判定流已死。
 * 采集/回吐都是"读麦→写 RAM/串口"的单向流程，停流后继续等只会把会话钉死，
 * 所以到点直接放弃本次会话，让线程侧清理把 s_cap_buf/s_cap_frames 收干净。 */
#define KWS_MIC_STALL_MS 1500
static rt_bool_t s_mic_zero = RT_FALSE;
static rt_tick_t s_mic_zero_ms = 0;

static void mic_stream_reset(void)
{
    s_raw_acc_n = 0;
    s_raw_short = 0;
    s_mic_zero  = RT_FALSE;
    s_mic_zero_ms = 0;
}

/* 从 dev 读满一帧后解交错，mono 收单声道样本。
 * 返回：>0 mono 样本数；0 暂时没数据；-1 设备已不在读状态（生产者被关了）。
 * -1 这条必须存在：rt_device_read 在 ref_count==0 时**立刻返回 0**且永远不会有数据来，
 * 上层若把 0 当"稍等再来"就是死转（实测采集分支卡在这，s_cap_frames 挂着不清，
 * 之后所有 kws capture/eval 都报"上一次采集还没结束"，只能断电救）。 */
static int mic_read(rt_device_t dev, int16_t *mono)
{
    if (dev == RT_NULL || dev->ref_count == 0)
        return -1;
    /* pipe 按字节给，可能一次不到一帧：**立刻续读凑齐**，不要 return 0 让上层睡 1ms
     * （实测每轮 38 次半帧 × 1ms ≈ 38ms，正好是把交付率压到 94% 的那点损耗）。 */
    while (s_raw_acc_n < KWS_PDM_FRAME_SAMPLES)
    {
        rt_size_t want = (KWS_PDM_FRAME_SAMPLES - s_raw_acc_n) * sizeof(int16_t);
        rt_size_t rd = rt_device_read(dev, 0,
                                      (rt_uint8_t *)s_raw_acc + s_raw_acc_n * sizeof(int16_t),
                                      want);
        if (rd == 0)
            return 0;                      /* 真的没数据：交回上层小睡 */
        if (rd < want)
            s_raw_short++;
        s_raw_acc_n += (int)(rd / sizeof(int16_t));
    }

    s_raw_acc_n = 0;
#if KWS_PDM_IS_STEREO
    /* 取右声道——与驱动非交错路径留的那一路是同一个麦（ISR 里存的就是 RIGHT） */
    int m = KWS_PDM_FRAME_SAMPLES / 2;
    for (int i = 0; i < m; i++)
        mono[i] = s_raw_acc[i * 2 + 1];
#else
    int m = KWS_PDM_FRAME_SAMPLES;
    for (int i = 0; i < m; i++)
        mono[i] = s_raw_acc[i];
#endif
    return m;
}

/* 每读一次调一下：拿到数据就清零计时；连续读空且超过 KWS_MIC_STALL_MS ⇒ RT_TRUE。
 * 只在"读得回来"的世界里有效——record pipe 是无超时阻塞读（audio.c 建 pipe 时带
 * RT_PIPE_FLAG_BLOCK_RD），线程一旦 park 在 rt_device_read 上就再也轮不到这里，
 * 所以停流的真正防线是"只有工作线程自己关设备"（见 kws_stop）。 */
static rt_bool_t mic_stall_check(rt_bool_t got_data)
{
    if (got_data)
    {
        s_mic_zero = RT_FALSE;
        s_mic_zero_ms = 0;
        return RT_FALSE;
    }
    rt_tick_t now = rt_tick_get_millisecond();
    if (!s_mic_zero)
    {
        s_mic_zero = RT_TRUE;
        s_mic_zero_ms = now;
        return RT_FALSE;
    }
    return (now - s_mic_zero_ms) >= KWS_MIC_STALL_MS ? RT_TRUE : RT_FALSE;
}

/* ---------------- 主线程 ---------------- */
static void dump_frame(const int16_t *mono, int n, int idx);   /* 前置声明：线程转储用 */

/* 板上 NN 输入原样导出：调**和 run_classifier 完全同一个** DSP 函数
 * （ei_dsp_blocks[].extract_fn = extract_mfcc_features，内部已含 cmvnw），
 * 把 NN 真正吃到的那 650 个浮点打出来。
 * 打印 EVALF <窗> <特征数> <帧数> <每帧维数> <窗校验和> 后每 13 个一行，
 * 值为 特征×1000 取整（rt_kprintf 无 %f）。
 * 上一版这里误用了连续式 extract_mfcc_per_slice_features（run_classifier 根本不走它），
 * 打出来的是一份"另一条链路"的特征，对账结论不可用——见 local_ai/firmware/CHANGES.md */
static void eval_dump_features(signal_t *sig, int win_idx, uint32_t ck)
{
    static float feat[KWS_EVAL_MAX_FEATURES];
    int units = (int)impulse_333519_1.nn_input_frame_size;
    if (units > KWS_EVAL_MAX_FEATURES)
    {
        rt_kprintf("EVALF_ERR units=%d 超出 %d\n", units, KWS_EVAL_MAX_FEATURES);
        return;
    }

    matrix_t fm(1, units, feat);
    int ret = extract_mfcc_features(sig, &fm,
                                    (void *)&ei_dsp_config_333519_2,
                                    (float)EI_CLASSIFIER_FREQUENCY);
    if (ret != EIDSP_OK)
    {
        rt_kprintf("EVALF_ERR dsp=%d\n", ret);
        return;
    }

    rt_kprintf("EVALF %d %d %d %d %u\n",
               win_idx, units, (int)fm.rows, (int)fm.cols, (unsigned int)ck);
    int n = (int)fm.rows * (int)fm.cols;
    for (int i = 0; i < n; i++)
    {
        float v = feat[i];
        int iv = (int)(v * 1000.0f + (v >= 0.0f ? 0.5f : -0.5f));
        rt_kprintf(" %d", iv);
        if ((i + 1) % 13 == 0)
            rt_kprintf("\n");
    }
    if (n % 13)
        rt_kprintf("\n");
}

/* 一窗音频的校验和：PC 用它确认"板上打分的那段"与"串口回吐给我的那段"是同一段。
 * 只用加法（溢出无所谓）+ 峰值绝对值，够定位错位/丢帧。 */
/* kws dsp：把一段**合成**信号直接压进环形缓冲喂推理，逐级导出 DSP 结果。
 * 为什么要逐级：实盘对账已经证明「同一段音频（窗校验和一致）→ NN 段同源、DSP 段不同源」，
 * 但只拿归一化后的 650 维定不出是 mfe/log/dct/cmvnw 的哪一步偏了，也定不出偏在板上 C
 * 还是 PC 复刻。逐级打印 + 已知答案输入（不吃麦克风，无环境不确定性）一次就能夹出来。
 *   DSPM  帧×num_filters  log-mel（mfe 后、numpy::log 后，DCT 之前）
 *   DSPR  帧×num_cepstral 原始 MFCC（dct2 + dc_elimination，cmvnw 之前）
 *   DSPC  帧×num_cepstral 归一化后（cmvnw 之后，应与 EVALF 逐位相同——互为自检）
 * 值的编码：特征×1000 取整；2000000001=NaN，2000000002/3=超出 ±2e6（rt_kprintf 无 %f）。*/
static volatile int s_dsp_mode = -1;
static volatile int s_dsp_echo = 1;
static volatile int s_eval_stage_win = -1;   /* kws eval <秒> <窗> ：该窗额外走逐级导出 */
#define KWS_DSP_MODE_MAX 5

static uint32_t cap_cksum(const int16_t *p, int n, int *peak_out)
{
    uint32_t sum = 0;
    int peak = 0;
    for (int i = 0; i < n; i++)
    {
        sum += (uint32_t)(int32_t)p[i];
        int v = p[i] < 0 ? -p[i] : p[i];
        if (v > peak) peak = v;
    }
    if (peak_out) *peak_out = peak;
    return sum;
}

/* 合成测试信号：**只用整数递推**（无 sin/无浮点），PC 用同一套整数运算逐位复现，
 * 这样"两端吃的是同一份字节"不靠信任、靠构造。幅度和周期都取整样本边界对齐。
 *   0 全零静音          1 恒定 DC(500)：预加重后应只剩 10
 *   2 LCG 白噪声 ±8000  3 1kHz 等幅方波（8 样本半周期）
 *   4 方波幅度按 100ms 在 500/8000 间交替（能量阶跃，看 c0 是否跟得上）
 *   5 静音中夹一个 20ms 的 1kHz 突发（数字静音↔有声的边界）*/
static void dsp_synth(int16_t *buf, int n, int mode)
{
    uint32_t x = 0x1234ABCDu;
    for (int i = 0; i < n; i++)
    {
        int v = 0;
        switch (mode)
        {
        case 1:
            v = 500;
            break;
        case 2:
            x = (x * 1103515245u + 12345u) & 0xFFFFFFFFu;
            v = (int)(((x >> 16) & 0x7FFFu) * 16000u / 32767u) - 8000;
            break;
        case 3:
            v = ((i >> 3) & 1) ? 6000 : -6000;
            break;
        case 4:
        {
            int amp = ((i / 1600) & 1) ? 8000 : 500;
            v = ((i >> 3) & 1) ? amp : -amp;
            break;
        }
        case 5:
            if (i >= 8000 && i < 8320)
                v = ((i >> 3) & 1) ? 8000 : -8000;
            break;
        default:            /* 0 与未知模式：数字静音 */
            break;
        }
        buf[i] = (int16_t)v;
    }
}

/* 逐级导出用 **float32 位模式**（不是 ×1000 取整）：1e-10 与 0 在 ×1000 下都打平成 0，
 * 而"零值有没有被 clamp 成 1e-10"恰好是 log-mel 分歧的判据之一，必须无损。
 * PC 侧用同样的 uint32→float32 位模式比对，还能把"结构性分歧"和"1 ULP 噪声"分开。 */
static void print_feat_row(const char *tag, int win, int row, const float *v, int n)
{
    rt_kprintf("%s %d %d", tag, win, row);
    for (int i = 0; i < n; i++)
    {
        rt_uint32_t b;
        memcpy(&b, &v[i], sizeof(b));
        rt_kprintf(" %u", (unsigned int)b);
    }
    rt_kprintf("\n");
}

/* EI 的 preemphasis 是"挂在 signal_t 上的取数包装"，需要一个 C 函数指针转接
 * （生产路径在 ei_run_dsp.h 里也有一份同名的文件静态指针，这里独立一份互不干扰）。
 * speechpy::processing 里 class preemphasis 与同名自由函数并存，限定名查找只命中函数，
 * 必须用 `class` 展开式类型说明符（EI 自身也是这么写的）。 */
static class ei::speechpy::processing::preemphasis *s_pre_ref = RT_NULL;

static int pre_get_data(size_t offset, size_t length, float *out_ptr)
{
    return s_pre_ref->get_data(offset, length, out_ptr);
}

/* EI DSP 逐级导出：照抄 extract_mfcc_features 的调用序列，但在每个阶段后打印一行。
 * 与生产路径的差别只有一处：这里不调用 run_classifier，只跑 DSP。 */
static void mfcc_dump_stages(signal_t *sig, int win)
{
    ei_dsp_config_mfcc_t *cf = &ei_dsp_config_333519_2;
    ei::matrix_size_t sz = ei::speechpy::feature::calculate_mfe_buffer_size(
        sig->total_length, (uint32_t)EI_CLASSIFIER_FREQUENCY,
        cf->frame_length, cf->frame_stride, cf->num_filters, cf->implementation_version);

    int rows = (int)sz.rows;                 /* 1.0s 窗 = 50 帧 */
    int nf   = (int)cf->num_filters;         /* 32 */
    int nc   = (int)cf->num_cepstral;        /* 13 */
    if (rows <= 0 || nf <= 0)
    {
        rt_kprintf("DSP_ERR size rows=%d cols=%d\n", rows, nf);
        return;
    }

    float *mel = (float *)rt_malloc(rows * nf * sizeof(float));
    float *raw = (float *)rt_malloc(rows * nc * sizeof(float));
    float *en  = (float *)rt_malloc(rows * sizeof(float));
    if (!mel || !raw || !en)
    {
        rt_kprintf("DSP_ERR nomem %d %d\n", rows * nf, rows * nc);
        if (mel) rt_free(mel);
        if (raw) rt_free(raw);
        if (en)  rt_free(en);
        return;
    }

    rt_kprintf("DSP_META %d %d %d %d %d\n", win, rows, nf, nc, (int)sig->total_length);

    /* 1) 预加重：与生产同一个类、同一份参数（rescale=false） */
    class ei::speechpy::processing::preemphasis pre(sig, cf->pre_shift, cf->pre_cof, false);
    s_pre_ref = &pre;
    signal_t ps;
    ps.total_length = sig->total_length;
    ps.get_data = &pre_get_data;

    /* 几何/FFT 探针（只在合成自检路径打，量很小）：mel 级对不上时，光看 mel 输出分不清是
     * "mel 边界 bin 算错"还是"FFT 本身错"，所以用 EI **自己的** helper 把两样摊开：
     *   DSPB  nf+2 个 mel 边界 bin（照抄 mfe 里 linspace→mel_to_frequency→get_fft_bin_from_hertz）
     *   DSPP  第 0 帧的 129 点功率谱（numpy::power_spectrum，和 mfe 里同一份调用）
     *   DSPT  预加重后第 0 帧的**全部** 320 个样点（PC 拿这份自己算 FFT，才能把
     *         "预加重错/FFT 错/几何错"三者分开） */
    if (win < 100)
    {
        /* DSPMATH：板上算出的 mel 边界 bin 与 PC 差 2.688 倍（34 个点拟合 err=0），
         * 可 frequency_to_mel / mel_to_frequency 的源码和 PC 复刻一模一样。
         * "mel 值被线性放大" 与 "exp 除数被线性缩小" 在 bin 上数学等价、分不开，
         * 所以这两个函数各测一次，谁错一目了然。值按 float32 位模式打（rt_kprintf 无 %f）。 */
        float vm[12];
        vm[0] = ei::numpy::log(1.0f + 8000.0f / 700.0f);             /* 期望 2.5200 (=ln(12.4286)) */
        vm[1] = ei::speechpy::functions::frequency_to_mel(8000.0f);  /* 期望 2840.2 */
        vm[2] = ei::speechpy::functions::frequency_to_mel(700.0f);   /* 期望 781.0  */
        vm[3] = ei::speechpy::functions::mel_to_frequency(2840.2f);  /* 期望 8000.0 */
        vm[4] = ei::speechpy::functions::mel_to_frequency(1127.0f);  /* 期望 1200.3 */
        vm[5] = expf(2840.2f / 1127.0f);                             /* 期望 12.4286（直接用 libm 对照） */
        vm[6] = 700.0f * (expf(2840.2f / 1127.0f) - 1.0f);           /* 期望 8000.0 */
        vm[7] = (float)ei::speechpy::feature::get_fft_bin_from_hertz(
                    256, 8000.0f, 16000);                            /* 期望 128 */
        /* 追加：把"只有 numpy::log 坏"钉死，并确认另外两个快速 log 是否同病
         * （坏时返回值恒等于 i*(1+ln2)，i 是指数索引 → 取 i=1 / i=-1 / log2 三点） */
        vm[8]  = logf(1.0f + 8000.0f / 700.0f);                      /* 期望 2.5200（libm 对照） */
        vm[9]  = ei::numpy::log(2.0f);                                /* 期望 0.6931（坏时 1.6931） */
        vm[10] = ei::numpy::log(0.5f);                                /* 期望 -0.6931（坏时 -1.6931） */
        vm[11] = ei::numpy::log2(1.0f + 8000.0f / 700.0f);            /* 期望 3.6352（走 frexpf，另一条码路） */
        rt_kprintf("DSPMATH");
        for (int ix = 0; ix < 12; ix++)
        {
            rt_uint32_t b;
            memcpy(&b, &vm[ix], sizeof(b));
            rt_kprintf(" %u", (unsigned int)b);
        }
        rt_kprintf("\n");

        uint32_t sr = (uint32_t)EI_CLASSIFIER_FREQUENCY;
        uint32_t lo = cf->low_frequency, hi = cf->high_frequency;
        if (hi == 0) hi = sr / 2;
        if (cf->implementation_version < 4 && lo == 0) lo = 300;
        uint16_t max_bin = cf->implementation_version >= 4
                             ? cf->fft_length : (uint16_t)(cf->fft_length / 2 + 1);
        int mels_n = nf + 2;
        float *mm = (float *)rt_malloc(mels_n * sizeof(float));
        if (mm)
        {
            uint16_t *bb = (uint16_t *)mm;          /* 和 SDK 一样复用同一块内存 */
            ei::numpy::linspace(ei::speechpy::functions::frequency_to_mel((float)lo),
                                ei::speechpy::functions::frequency_to_mel((float)hi),
                                mels_n, mm);
            rt_kprintf("DSPB %d mb=%u fft=%u", win, (unsigned int)max_bin,
                       (unsigned int)cf->fft_length);
            for (int ix = 0; ix < mels_n - 1; ix++)
            {
                mm[ix] = ei::speechpy::functions::mel_to_frequency(mm[ix]);
                if (mm[ix] < (float)lo) mm[ix] = (float)lo;
                if (mm[ix] > (float)hi) mm[ix] = (float)hi;
                bb[ix] = (uint16_t)ei::speechpy::feature::get_fft_bin_from_hertz(
                             max_bin, mm[ix], sr);
                rt_kprintf(" %u", (unsigned int)bb[ix]);
            }
            mm[mels_n - 1] = ei::speechpy::functions::mel_to_frequency(mm[mels_n - 1]);
            if (mm[mels_n - 1] > (float)hi) mm[mels_n - 1] = (float)hi;
            mm[mels_n - 1] -= 0.001f;
            bb[mels_n - 1] = (uint16_t)ei::speechpy::feature::get_fft_bin_from_hertz(
                                max_bin, mm[mels_n - 1], sr);
            rt_kprintf(" %u\n", (unsigned int)bb[mels_n - 1]);
            rt_free(mm);
        }

        int flen = (int)(cf->frame_length * (float)sr + 0.5f);
        int psz  = (int)(cf->fft_length / 2 + 1);
        float *fr = (float *)rt_malloc(flen * sizeof(float));
        float *pw = (float *)rt_malloc(psz * sizeof(float));
        if (fr && pw)
        {
            ps.get_data(0, flen, fr);                   /* 预加重后的第 0 帧 */
            int pr = ei::numpy::power_spectrum(fr, flen, pw, psz, cf->fft_length);
            rt_kprintf("DSPP_HDR %d err=%d flen=%d psz=%d\n", win, pr, flen, psz);
            for (int c = 0; c < psz; c += 32)
                print_feat_row("DSPP", win, c / 32, pw + c,
                               (psz - c < 32) ? (psz - c) : 32);
            for (int c = 0; c < flen; c += 32)   /* 整帧都导出来：PC 拿这份自己算 FFT，
                                                  * 才能把"预加重错/FFT 错/几何错"三者分开 */
                print_feat_row("DSPT", win, c / 32, fr + c,
                               (flen - c < 32) ? (flen - c) : 32);
        }
        if (fr) rt_free(fr);
        if (pw) rt_free(pw);
    }

    ei::matrix_t melmat(rows, nf, mel);
    ei::matrix_t enmat(rows, 1, en);
    int ret = ei::speechpy::feature::mfe(&melmat, &enmat, &ps,
                (uint32_t)EI_CLASSIFIER_FREQUENCY, cf->frame_length, cf->frame_stride,
                cf->num_filters, cf->fft_length, cf->low_frequency, cf->high_frequency,
                cf->implementation_version);
    if (ret != EIDSP_OK)
    {
        rt_kprintf("DSP_ERR mfe=%d\n", ret);
        rt_free(mel); rt_free(raw); rt_free(en);
        return;
    }
    for (int r = 0; r < rows; r++)
        print_feat_row("DSPE", win, r, en + r, 1);      /* 帧能量（log 之前，原样） */
    for (int r = 0; r < rows; r++)
        print_feat_row("DSPF", win, r, mel + r * nf, nf);  /* mel 能量（log 之前，含 zero_handling） */

    /* 2) numpy::log 就地取对数——注意这是 EI 的快速 log 近似，不是 libm 的 logf */
    ret = ei::numpy::log(&melmat);
    if (ret != EIDSP_OK)
    {
        rt_kprintf("DSP_ERR log=%d\n", ret);
        rt_free(mel); rt_free(raw); rt_free(en);
        return;
    }
    for (int r = 0; r < rows; r++)
        print_feat_row("DSPM", win, r, mel + r * nf, nf);

    /* 3) DCT-II 正交 + dc_elimination（col0 换成 log(能量)）—— mfcc() 里紧随其后的两步 */
    ret = ei::numpy::dct2(&melmat, ei::DCT_NORMALIZATION_ORTHO);
    if (ret != EIDSP_OK)
    {
        rt_kprintf("DSP_ERR dct=%d\n", ret);
        rt_free(mel); rt_free(raw); rt_free(en);
        return;
    }
    for (int r = 0; r < rows; r++)
        melmat.buffer[r * nf] = ei::numpy::log(en[r]);
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < nc; c++)
            raw[r * nc + c] = melmat.buffer[r * nf + c];
    for (int r = 0; r < rows; r++)
        print_feat_row("DSPR", win, r, raw + r * nc, nc);

    /* 4) cmvnw(win=101, 方差归一=true)：NN 真正吃的那一步 */
    ei::matrix_t rawmat(rows, nc, raw);
    ret = ei::speechpy::processing::cmvnw(&rawmat, cf->win_size, true, false);
    if (ret != EIDSP_OK)
    {
        rt_kprintf("DSP_ERR cmvnw=%d\n", ret);
        rt_free(mel); rt_free(raw); rt_free(en);
        return;
    }
    for (int r = 0; r < rows; r++)
        print_feat_row("DSPC", win, r, raw + r * nc, nc);

    rt_free(mel); rt_free(raw); rt_free(en);
}

/* 音频交付率：本轮 + 累计。ratio = 消费样本 /(墙钟ms × 16)，100% = 一个样本没丢 */
static void report_flow(int got, uint32_t wall_ms)
{
    uint32_t round_pct = wall_ms ? (uint32_t)((uint64_t)got * 100 / ((uint64_t)wall_ms * 16)) : 0;
    uint32_t all_pct   = s_flow_wall_ms ? (uint32_t)((uint64_t)s_flow_consumed * 100 /
                                                     ((uint64_t)s_flow_wall_ms * 16)) : 0;
    uint32_t lost = (uint32_t)((uint64_t)s_flow_wall_ms * 16 - s_flow_consumed);
    LOG_I("[kws] 音频流 本轮 %d样本/%dms=%d%% 累计交付率 %d%% 丢失≈%dms 丢样轮 %u 短读%u",
          got, (int)wall_ms, (int)round_pct, (int)all_pct,
          (int)(lost / 16), s_flow_lossy, s_raw_short);
}


static void kws_thread(void *param)
{
    int hop_samples = KWS_HOP_MS * EI_CLASSIFIER_FREQUENCY / 1000;

    LOG_I("KWS 启动: model=%s sr=%d win=%dms hop=%dms 阈值=%d%% 标签=\"%s\"",
          EI_CLASSIFIER_PROJECT_NAME, EI_CLASSIFIER_FREQUENCY,
          KWS_WINDOW_MS, KWS_HOP_MS, (int)(s_conf * 100), KWS_WAKE_WORD_LABEL);
    for (int i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++)
        LOG_I("  label[%d] = %s", i, ei_classifier_inferencing_categories[i]);

    signal_t signal;
    signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
    signal.get_data = &signal_get_data;

    static int16_t mono_buf[KWS_PDM_MONO_FRAME_SAMPLES];

    while (RT_TRUE)
    {
        rt_uint32_t ev;
        if (rt_event_recv(s_event, KWS_EVENT_START | KWS_EVENT_EXIT,
                          RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                          RT_WAITING_FOREVER, &ev) != RT_EOK)
            continue;

        if (ev & KWS_EVENT_EXIT)
            break;

        if (!(ev & KWS_EVENT_START))
            continue;

        /* 先吃掉残留的 STOP 位再进监听态。rt_event_recv 的 CLEAR 只清"本次请求里的位"
         * （ipc.c: event->set &= ~set），而等 START 那次请求的集合根本没有 STOP ⇒ 上一次
         * kws stop 发的 STOP 一直挂着。下一轮 start/capture/eval 一进循环，顶部的 STOP 轮询
         * 就把它读成"用户按了停止"，开麦当场自杀（实测：kws stop 之后第一次 kws capture 打
         * 「开始监听」→「麦克风已关闭」，600 帧一帧没录）。AND 模式只摘 STOP，不动 EXIT。 */
        rt_event_recv(s_event, KWS_EVENT_STOP,
                      RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                      RT_WAITING_NO, &ev);

        /* 打开麦克风 */
        if (s_audio_dev == RT_NULL)
        {
            s_audio_dev = rt_device_find(KWS_MIC_DEVICE_NAME);
            if (s_audio_dev == RT_NULL)
            {
                LOG_E("找不到音频设备 '%s'", KWS_MIC_DEVICE_NAME);
                continue;
            }
            if (rt_device_open(s_audio_dev, RT_DEVICE_FLAG_RDONLY) != RT_EOK)
            {
                LOG_E("无法打开音频设备（麦克风不可用？）");
                s_audio_dev = RT_NULL;
                continue;
            }
            LOG_I("麦克风已打开（%s）", KWS_MIC_DEVICE_NAME);
        }

        ring_init();
        s_flow_consumed = 0;
        s_flow_wall_ms  = 0;
        s_flow_lossy    = 0;
        memset(s_score_hist, 0, sizeof(s_score_hist));
        s_score_max = 0.0f;
        mic_stream_reset();
        s_enabled = RT_TRUE;
        LOG_I("开始监听唤醒词...");

        while (s_enabled)
        {
            /* 停止检查。这里连 START 一起读：监听态收到 START 是空操作，但**必须吃掉**，
             * 否则 kws capture 在已监听时发的那枚 START 一直挂着，会话一退出就被外层
             * recv 当成"用户又按了开始"——实测 kws stop 后 10ms 麦又被打开
             * （28.46 关 → 28.47 开始监听），kws_stop 等"工作线程自己关设备"就永远等不到。
             * OR 模式返回实际命中的位，只有含 STOP 才算停。 */
            if (rt_event_recv(s_event, KWS_EVENT_STOP | KWS_EVENT_START,
                              RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                              RT_WAITING_NO, &ev) == RT_EOK)
            {
                if (ev & KWS_EVENT_STOP)
                {
                    s_enabled = RT_FALSE;
                    break;
                }
            }

            /* 自检模式（kws dsp）：合成信号压进环形缓冲→逐级导出 DSP→（可选）回吐字节。
             * 整段在**一次循环迭代内**做完，期间不读麦克风，所以 s_ring 不被别人踩。 */
            if (s_dsp_mode >= 0)
            {
                int mode = s_dsp_mode;
                int echo = s_dsp_echo;
                s_dsp_mode = -1;

                int win = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
                dsp_synth(s_ring, win, mode);
                int pk = 0;
                uint32_t ck = cap_cksum(s_ring, win, &pk);
                rt_kprintf("DSP_SYNTH %d %d %u %d\n", mode, win, (unsigned int)ck, pk);

                signal_t dsig;
                dsig.total_length = win;
                dsig.get_data = &signal_get_data;

                ei_impulse_result_t dr;
                memset(&dr, 0, sizeof(dr));
                run_classifier_init();
                EI_IMPULSE_ERROR de = run_classifier(&dsig, &dr, false);
                float dw = 0.0f;
                for (int i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++)
                    if (strstr(ei_classifier_inferencing_categories[i], KWS_WAKE_WORD_LABEL) != RT_NULL)
                        dw = dr.classification[i].value;
                rt_kprintf("DSP_SCORE %d %d err=%d dsp=%d nn=%d ck=%u\n",
                           mode, (int)(dw * 1000.0f), (int)de, dr.timing.dsp,
                           dr.timing.classification, (unsigned int)ck);

                mfcc_dump_stages(&dsig, mode);
                eval_dump_features(&dsig, mode, ck);   /* EVALF：应与 DSPC 逐位相同（互为自检） */

                if (echo)
                {
                    int nf = win / KWS_PDM_MONO_FRAME_SAMPLES;
                    rt_kprintf("KWS_DUMP_BEGIN %d %d %u %d\n",
                               KWS_PDM_MONO_FRAME_SAMPLES, nf, (unsigned int)ck, pk);
                    for (int f = 0; f < nf; f++)
                        dump_frame(&s_ring[f * KWS_PDM_MONO_FRAME_SAMPLES],
                                   KWS_PDM_MONO_FRAME_SAMPLES, f);
                    rt_kprintf("KWS_DUMP_END\n");
                }
                continue;
            }

            /* 采集模式（kws capture）：连续录进 RAM，录满后转成回吐；期间不推理 */
            if (s_cap_frames > 0)
            {
                int n = mic_read(s_audio_dev, mono_buf);
                if (n <= 0)
                {
                    /* 停流（-1：设备已不在读状态 / 连续 0：超过 KWS_MIC_STALL_MS 没数据）
                     * 就放弃本次采集并结束会话，让线程侧清理把缓冲和 s_cap_frames 收干净。
                     * 原来这里是 mdelay(1) 无限重来 ⇒ 采集状态永久挂着，
                     * 之后 kws capture/eval 全部"上一次还没结束"，只能断电救。 */
                    rt_bool_t dead = (n < 0) || mic_stall_check(RT_FALSE);
                    if (!dead)
                    {
                        rt_thread_mdelay(1);
                        continue;
                    }
                    rt_kprintf("KWS_CAPTURE_STALL %d %d\n", s_cap_len,
                               (int)(s_cap_frames * KWS_PDM_MONO_FRAME_SAMPLES));
                    s_enabled = RT_FALSE;
                    break;
                }
                mic_stall_check(RT_TRUE);
                memcpy(&s_cap_buf[s_cap_len], mono_buf, n * sizeof(int16_t));
                s_cap_len += n;
                if (--s_cap_frames <= 0)
                {
                    s_cap_frames = 0;
                    s_cap_pos = 0;
                    int win = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
                    int pk = 0;
                    uint32_t ckb = cap_cksum(s_cap_buf, s_cap_len, &pk);
                    if (s_eval_windows > 0)
                    {
                        /* 录满 → 先进评测模式（评完自动接回吐） */
                        int avail = s_cap_len / win;
                        s_eval_run = (s_eval_windows < avail) ? s_eval_windows : avail;
                        s_eval_windows = 0;
                        s_eval_idx = 0;
                        rt_kprintf("KWS_EVAL_BEGIN %d %d %d %u %d\n",
                                   win, s_eval_run, s_cap_len, (unsigned int)ckb, pk);
                        if (s_eval_run <= 0)
                        {
                            int out0 = s_cap_len / KWS_PDM_MONO_FRAME_SAMPLES;
                            s_dump_frames = out0;
                            s_dump_idx = 0;
                            rt_kprintf("KWS_DUMP_BEGIN %d %d %u %d\n",
                                       KWS_PDM_MONO_FRAME_SAMPLES, out0, (unsigned int)ckb, pk);
                        }
                    }
                    else
                    {
                        int out = s_cap_len / KWS_PDM_MONO_FRAME_SAMPLES;
                        s_dump_frames = out;
                        s_dump_idx = 0;
                        rt_kprintf("KWS_DUMP_BEGIN %d %d %u %d\n",
                                   KWS_PDM_MONO_FRAME_SAMPLES, out, (unsigned int)ckb, pk);
                    }
                }
                continue;
            }

            /* 评测模式（kws eval）：把 RAM 里刚录好的连续音频按整窗重放给推理路径。
             * 每次迭代评一窗（1.0s = 16000 样本，不重叠）：先出板上分数 EVALW，
             * 再出该窗 NN 真正吃到的 650 个浮点 EVALF，全部评完后交给回吐路径。
             * EVALW/EVALF 都带该窗校验和，PC 据此确认串口那份音频和板上打分的音频同段同位。 */
            if (s_eval_run > 0)
            {
                int win = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
                int off = s_eval_idx * win;
                int wk = s_eval_idx;
                uint32_t ck = 0;
                int peak = 0;

                if (s_cap_len < off + win)
                    s_eval_run = 0;                      /* 尾巴不足一窗：收尾 */
                else
                {
                    memcpy(s_ring, &s_cap_buf[off], win * sizeof(int16_t));
                    ck = cap_cksum(&s_cap_buf[off], win, &peak);
                }

                if (s_eval_run > 0)
                {
                    signal_t esig;
                    esig.total_length = win;
                    esig.get_data = &signal_get_data;

                    ei_impulse_result_t r;
                    memset(&r, 0, sizeof(r));
                    run_classifier_init();               /* 每窗独立：不留跨调用状态 */
                    EI_IMPULSE_ERROR e = run_classifier(&esig, &r, false);

                    float ww = 0.0f;
                    for (int i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++)
                        if (strstr(ei_classifier_inferencing_categories[i], KWS_WAKE_WORD_LABEL) != RT_NULL)
                            ww = r.classification[i].value;

                    /* 分数用千分比整数（rt_kprintf 没有 %f） */
                    rt_kprintf("EVALW %d %d err=%d dsp=%d nn=%d ck=%u peak=%d\n",
                               wk, (int)(ww * 1000.0f), (int)e,
                               r.timing.dsp, r.timing.classification,
                               (unsigned int)ck, peak);

                    eval_dump_features(&esig, wk, ck);
                    if (wk == s_eval_stage_win)
                        mfcc_dump_stages(&esig, 1000 + wk);   /* 实采窗也逐级导出（窗号+1000 区分来源） */

                    s_eval_idx++;
                    s_eval_run--;
                }

                if (s_eval_run <= 0)
                {
                    rt_kprintf("KWS_EVAL_END\n");
                    int out = s_cap_len / KWS_PDM_MONO_FRAME_SAMPLES;
                    int pk2 = 0;
                    uint32_t ck2 = cap_cksum(s_cap_buf, s_cap_len, &pk2);
                    s_dump_frames = out;
                    s_dump_idx = 0;
                    rt_kprintf("KWS_DUMP_BEGIN %d %d %u %d\n",
                               KWS_PDM_MONO_FRAME_SAMPLES, out, (unsigned int)ck2, pk2);
                }
                continue;
            }

            /* 转储模式：把 mono 样本按 hex 行流到串口（不推理）。
             * RAM 里已录好且没吐完 ⇒ 回放（不丢音频）；否则实时读麦（受串口带宽限会丢帧）。 */
            if (s_dump_frames > 0)
            {
                int n;
                if (s_cap_buf != RT_NULL && s_cap_pos < s_cap_len)
                {
                    n = KWS_PDM_MONO_FRAME_SAMPLES;
                    memcpy(mono_buf, &s_cap_buf[s_cap_pos], n * sizeof(int16_t));
                    s_cap_pos += n;
                    if (s_cap_pos >= s_cap_len)
                    {
                        rt_free(s_cap_buf);
                        s_cap_buf = RT_NULL;
                        s_cap_len = 0;
                        s_cap_pos = 0;
                    }
                }
                else
                {
                    n = mic_read(s_audio_dev, mono_buf);
                    if (n <= 0)
                    {
                        rt_bool_t dead = (n < 0) || mic_stall_check(RT_FALSE);
                        if (dead)
                        {
                            rt_kprintf("KWS_DUMP_STALL %d %d\n", s_dump_idx, s_dump_frames);
                            s_enabled = RT_FALSE;
                            break;
                        }
                    }
                    else
                    {
                        mic_stall_check(RT_TRUE);
                    }
                }
                if (n > 0)
                {
                    dump_frame(mono_buf, n, s_dump_idx);
                    s_dump_idx++;
                    s_dump_frames--;
                    if (s_dump_frames <= 0)
                    {
                        s_dump_frames = 0;
                        rt_kprintf("KWS_DUMP_END\n");
                    }
                }
                else
                {
                    rt_thread_mdelay(1);
                }
                continue;
            }

            /* 读帧累积 hop 个 mono 样本；每帧只入环形缓冲一次，且入的是解交错后的 mono
             * （原先 mic_read_frame 内 push 一次、这里再 push 一次 raw ⇒ 窗内 2× 时间拉伸）
             * 整轮（累积 + 推理）的墙钟时长与消费样本数一起计入流量计：
             * 麦克风按真实时间产样，ratio = 消费/（墙钟×16）< 100% 就说明这一轮丢了音频。 */
            rt_tick_t iter_t0 = rt_tick_get();
            int got = 0;
            while (got < hop_samples && s_enabled)
            {
                int n = mic_read(s_audio_dev, mono_buf);
                if (n <= 0)
                {
                    if ((n < 0) || mic_stall_check(RT_FALSE))
                    {
                        rt_kprintf("KWS_MIC_STALL %d\n", got);
                        s_enabled = RT_FALSE;
                        break;
                    }
                    rt_thread_mdelay(1);
                    continue;
                }
                mic_stall_check(RT_TRUE);
                ring_push(mono_buf, n);
                got += n;
            }

            if (!s_enabled)
                break;

            /* 推理 */
            ei_impulse_result_t result;
            memset(&result, 0, sizeof(result));
            EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);

            if (err != EI_IMPULSE_OK)
            {
                LOG_E("推理失败 (%d)", err);
                if (++s_retries > 5)
                {
                    LOG_E("连续推理失败，停止监听（需排查模型/内存）");
                    s_enabled = RT_FALSE;
                    break;
                }
                rt_thread_mdelay(10);
                continue;
            }
            s_retries = 0;

            s_inferences++;
            s_dsp_total_ms += result.timing.dsp;
            s_nn_total_ms  += result.timing.classification;

            {
                uint32_t wall = (rt_tick_get() - iter_t0) * 1000 / RT_TICK_PER_SECOND;
                s_flow_consumed += got;
                s_flow_wall_ms  += wall;
                /* 交付率 <95% ⇒ 这一轮麦克风有样本没进到模型眼里 */
                if (wall && (uint64_t)got * 100 < (uint64_t)wall * 16 * 95)
                    s_flow_lossy++;
                if (KWS_SCORE_LOG_INTERVAL && (s_inferences % KWS_SCORE_LOG_INTERVAL) == 0)
                    report_flow(got, wall);
            }

#if KWS_SCORE_LOG_INTERVAL
            if (KWS_SCORE_LOG_INTERVAL && (s_inferences % KWS_SCORE_LOG_INTERVAL) == 0)
                report_scores(&result);
#endif

            /* 唤醒判定：持续性判据——最近 KWS_HIST_LEN 窗里至少 KWS_HIST_HITS 个超阈值。
             * 单窗毛刺（环境音偶发）不算，滤掉大部分误报。 */
            float ww = 0.0f;
            for (int i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++)
                if (strstr(ei_classifier_inferencing_categories[i], KWS_WAKE_WORD_LABEL) != RT_NULL)
                    ww = result.classification[i].value;

            int hb = (int)(ww * 10.0f);
            if (hb > 10) hb = 10;
            if (s_score_hist[hb] < 0xFFFF) s_score_hist[hb]++;
            if (ww > s_score_max) s_score_max = ww;

            s_hist[s_hist_pos % KWS_HIST_LEN] = (ww > s_conf) ? 1 : 0;
            s_hist_pos++;

            {
                int n_hi = 0;
                int n = (s_hist_pos < KWS_HIST_LEN) ? (int)s_hist_pos : KWS_HIST_LEN;
                for (int k = 0; k < n; k++)
                    n_hi += s_hist[k];

                if (n >= KWS_HIST_LEN && n_hi >= s_hist_hits)
                {
                    uint32_t now = rt_tick_get();
                    if ((now - s_last_detection) >= rt_tick_from_millisecond(s_cooldown_ms))
                    {
                        s_last_detection = now;
                        s_detections++;
                        s_last_score = ww;
                        LOG_W("*** @%ums 唤醒词命中: %s (%d%%, %d/%d窗) ***",
                              (unsigned)board_ms(),
                              KWS_WAKE_WORD_LABEL,
                              (int)(ww * 100.0f), n_hi, n);
                        if (s_cb)
                            s_cb(KWS_WAKE_WORD_LABEL, ww);
                        else
                            kws_detected_default(KWS_WAKE_WORD_LABEL, ww);
                        s_hist_pos = 0;      /* 触发后重置历史，避免连续重复触发 */
                        memset(s_hist, 0, sizeof(s_hist));
                    }
                }
            }
        }

        /* 线程侧清理未吐完的采集缓冲（避免与 kws_stop 抢同一块内存） */
        if (s_cap_buf != RT_NULL)
        {
            rt_free(s_cap_buf);
            s_cap_buf = RT_NULL;
        }
        s_cap_len = 0;
        s_cap_pos = 0;
        s_cap_frames = 0;
        s_dump_frames = 0;
        s_eval_windows = 0;
        s_eval_run = 0;
        s_eval_idx = 0;

        /* 关闭麦克风，释放给其他功能（如录音/播放） */
        if (s_audio_dev != RT_NULL)
        {
            rt_device_close(s_audio_dev);
            s_audio_dev = RT_NULL;
            LOG_I("麦克风已关闭");
        }
    }

    LOG_I("KWS 线程退出");
    s_inited = RT_FALSE;
}

/* ---------------- 公共 API ---------------- */

int kws_init(void)
{
    if (s_inited)
        return 0;

    /* 关键修复：本工程 startup 未调用 C++ init_array，EI 全局句柄的构造函数没执行
     * → ei_default_impulse.impulse 为 NULL → 推理返回 -22。
     * 用 placement new 手动构造（连带 state 里的 dsp_handles 分配）。 */
    if (ei_default_impulse.impulse == nullptr)
    {
        new (&ei_default_impulse) ei_impulse_handle_t(&impulse_333519_1);
        LOG_I("EI 句柄已手动构造: impulse=%p", (void *)ei_default_impulse.impulse);
    }

    s_event = rt_event_create("kws_evt", RT_IPC_FLAG_FIFO);
    if (s_event == RT_NULL)
    {
        LOG_E("创建事件失败");
        return -RT_ENOMEM;
    }

    if (s_ring == RT_NULL)
    {
        s_ring = (int16_t *)rt_malloc(EI_CLASSIFIER_RAW_SAMPLE_COUNT * sizeof(int16_t));
        if (s_ring == RT_NULL)
        {
            LOG_E("KWS 环形缓冲分配失败（需 %d 字节）",
                  EI_CLASSIFIER_RAW_SAMPLE_COUNT * (int)sizeof(int16_t));
            rt_event_delete(s_event);
            s_event = RT_NULL;
            return -RT_ENOMEM;
        }
        LOG_I("KWS 环形缓冲 %d 字节（堆）", EI_CLASSIFIER_RAW_SAMPLE_COUNT * (int)sizeof(int16_t));
    }

    s_tid = rt_thread_create("kws", kws_thread, RT_NULL, 8192,
                             KWS_THREAD_PRIORITY, 5);
    if (s_tid == RT_NULL)
    {
        LOG_E("创建线程失败（内存不足？KWS 需约 8192B 栈）");
        rt_event_delete(s_event);
        s_event = RT_NULL;
        return -RT_ENOMEM;
    }

    if (rt_thread_startup(s_tid) != RT_EOK)
    {
        rt_thread_delete(s_tid);
        s_tid = RT_NULL;
        rt_event_delete(s_event);
        s_event = RT_NULL;
        return -RT_ERROR;
    }

    s_inited = RT_TRUE;
    LOG_I("KWS 初始化完成");
    return 0;
}

int kws_deinit(void)
{
    if (!s_inited)
        return 0;
    kws_stop();
    rt_event_send(s_event, KWS_EVENT_EXIT);
    rt_thread_mdelay(100);
    rt_event_delete(s_event);
    s_event = RT_NULL;
    s_tid = RT_NULL;
    s_inited = RT_FALSE;
    LOG_I("KWS 已释放");
    return 0;
}

int kws_start(void)
{
    if (!s_inited && kws_init() != 0)
        return -RT_ERROR;
    if (s_enabled)
        return 0;
    rt_event_recv(s_event, 0xFFFFFFFF, RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                  RT_WAITING_NO, RT_NULL);
    /* 观测：直接打印 EI 句柄指针，验证 handle 是否为空（-22 的根因假设） */
    LOG_I("EI impulse=%p pid=%d nn_in=%d labels=%d arena=%d model=%d",
          (void *)ei_default_impulse.impulse,
          ei_default_impulse.impulse ? (int)ei_default_impulse.impulse->project_id : -1,
          ei_default_impulse.impulse ? (int)ei_default_impulse.impulse->nn_input_frame_size : -1,
          ei_default_impulse.impulse ? (int)ei_default_impulse.impulse->label_count : -1,
          (int)EI_CLASSIFIER_TFLITE_LARGEST_ARENA_SIZE,
          (int)tflite_learn_333519_3_len);
    rt_event_send(s_event, KWS_EVENT_START);
    return 0;
}

int kws_stop(void)
{
    if (!s_inited || !s_enabled)
        return 0;
    s_enabled = RT_FALSE;
    rt_event_send(s_event, KWS_EVENT_STOP);
    /* 等设备由**工作线程自己**关，不在这里 rt_device_close。
     * mic0 的 record pipe 是无超时阻塞读（audio.c 建 pipe 时带 RT_PIPE_FLAG_BLOCK_RD）：
     * 别的线程一 close，_audio_record_stop 就把 PDM 生产者停了，而工作线程可能正好
     * park 在 rt_device_read 的挂起队列上——没人再写数据，它就永远醒不来，STOP 位读不到、
     * 会话退不出、s_cap_buf/s_cap_frames 全泄漏，整个 KWS 服务只能断电救
     * （实测：kws stop 后一次 8 秒采集把板子钉死，之后 kws capture 恒报"上一次还没结束"）。
     * 线程退出会话时一定会关设备（见主线程清理），这里只是有界地等它做完。 */
    for (int i = 0; i < 300 && s_audio_dev != RT_NULL; i++)
        rt_thread_mdelay(10);
    LOG_I("KWS 已停止");
    return 0;
}

rt_bool_t kws_is_enabled(void)
{
    return s_enabled;
}

int kws_info(void)
{
    LOG_I("==== KWS 状态 ====");
    LOG_I("监听: %s", s_enabled ? "运行中" : "已停止");
    LOG_I("唤醒词: \"%s\"（阈值 %d%%，%d/%d 窗确认，冷却 %u ms）",
          KWS_WAKE_WORD_LABEL, (int)(s_conf * 100), s_hist_hits, KWS_HIST_LEN,
          (unsigned)s_cooldown_ms);
    LOG_I("窗口: %dms / hop %dms", KWS_WINDOW_MS, KWS_HOP_MS);
    LOG_I("模型: %s (sr=%dHz, %dms/窗)", EI_CLASSIFIER_PROJECT_NAME,
          EI_CLASSIFIER_FREQUENCY, KWS_WINDOW_MS);
    LOG_I("推理: %u 次，命中: %u 次，唤醒词最近分数: %d%%",
          s_inferences, s_detections, (int)(s_last_score * 100));
    {
        /* 机器可读：PC 侧按这条做阈值扫描（在分布上选阈值，而不是猜） */
        char hs[11 * 7 + 1];
        int o = 0;
        for (int i = 0; i < 11 && o < (int)sizeof(hs) - 8; i++)
            o += rt_snprintf(hs + o, sizeof(hs) - o, "%s%u", i ? "," : "", s_score_hist[i]);
        LOG_I("SCOREHIST 0,10,20,30,40,50,60,70,80,90,100=%s", hs);
        LOG_I("分数分布(每 hop，10%% 一档): 最高 %d%%  >50%% 共 %u 窗",
              (int)(s_score_max * 100.0f),
              (unsigned)(s_score_hist[5] + s_score_hist[6] + s_score_hist[7] +
                         s_score_hist[8] + s_score_hist[9] + s_score_hist[10]));
    }
    if (s_inferences > 0)
        LOG_I("平均耗时: DSP %u ms / NN %u ms",
              s_dsp_total_ms / s_inferences, s_nn_total_ms / s_inferences);
    if (s_flow_wall_ms > 0)
    {
        uint32_t pct = (uint32_t)((uint64_t)s_flow_consumed * 100 /
                                  ((uint64_t)s_flow_wall_ms * 16));
        LOG_I("音频流: 交付率 %d%%（丢样轮 %u / 推理 %u，半帧读 %u）",
              (int)pct, s_flow_lossy, s_inferences, s_raw_short);
    }
    LOG_I("标签:");
    for (int i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++)
        LOG_I("  [%d] %s", i, ei_classifier_inferencing_categories[i]);
    LOG_I("==================");
    return 0;
}

const char *kws_wake_word_label(void)
{
    return KWS_WAKE_WORD_LABEL;
}

void kws_set_callback(kws_detected_cb cb)
{
    s_cb = cb;
}

/* 默认回调：打印日志 + IPC 上报 M33 + 绿灯快闪。
 * 外部可定义同名强符号覆盖（覆盖后这三件事都没了，需自行接上）。
 * EI 类别索引只认当前模型布局：label[1] = xiaomai（无 noise 之外的类别）。 */
rt_weak void kws_detected_default(const char *label, float confidence)
{
    uint8_t label_id = 1;   /* KWS_WAKE_WORD_LABEL 当前固定为类别 1（见 kws.h） */
    uint8_t conf_pct = (uint8_t)(confidence * 100.0f);

    LOG_W("唤醒命中（默认回调）: %s (%d%%)", label, (int)(confidence * 100.0f));

    /* M55 -> M33 唤醒事件（待发槽投递，IPC 线程代写帧，线程安全） */
    feathertalk_ipc_post_kws_event(label_id, conf_pct);

    /* 板载绿灯快闪 1s（main 循环执行翻转） */
    feathertalk_m55_wake_flash();
}

/* ---------------- R0-1 麦克风录音 ---------------- */

int kws_record(const char *path, int seconds)
{
    if (seconds <= 0 || seconds > 60)
    {
        LOG_E("录音时长需在 1~60 秒之间");
        return -RT_EINVAL;
    }

    if (s_enabled)
    {
        LOG_E("请先 kws stop 再录音（麦克风被占用）");
        return -RT_EBUSY;
    }

    rt_device_t dev = rt_device_find(KWS_MIC_DEVICE_NAME);
    if (dev == RT_NULL)
    {
        LOG_E("找不到麦克风 '%s'", KWS_MIC_DEVICE_NAME);
        return -RT_ENOSYS;
    }
    if (rt_device_open(dev, RT_DEVICE_FLAG_RDONLY) != RT_EOK)
    {
        LOG_E("无法打开麦克风");
        return -RT_EIO;
    }

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0)
    {
        LOG_E("无法创建文件 '%s'（SD 卡已挂载？）", path);
        rt_device_close(dev);
        return -RT_EIO;
    }

    LOG_I("开始录音 %d 秒 -> %s ...", seconds, path);
    LOG_I("（对着麦克风说话，下面的 RMS 应明显跳起；全程贴地=麦克风没采到）");
    static int16_t mono[KWS_PDM_MONO_FRAME_SAMPLES];
    int total_samples = seconds * EI_CLASSIFIER_FREQUENCY;
    int collected = 0;
    int frames = 0;
    float rms_peak = 0.0f;
    mic_stream_reset();

    while (collected < total_samples)
    {
        int n = mic_read(dev, mono);
        if (n == 0)
        {
            rt_thread_mdelay(1);
            continue;
        }

        /* 实时 RMS 显示（每 50 帧 ≈ 500ms 打印一次）；统计口径与写入一致，都是单声道 */
        {
            double sumsq = 0.0;
            for (int i = 0; i < n; i++)
                sumsq += (double)mono[i] * mono[i];
            float rms = sqrtf((float)(sumsq / n));
            if (rms > rms_peak) rms_peak = rms;
            if ((frames % 50) == 0)
            {
                int bars = (int)(rms / 200.0f);
                if (bars > 40) bars = 40;
                char meter[48];
                int bi;
                for (bi = 0; bi < bars; bi++) meter[bi] = '#';
                meter[bi] = '\0';
                LOG_I("  电平 %6d |%s", (int)rms, meter);
            }
        }

        if (write(fd, mono, n * (int)sizeof(int16_t)) < 0)
            break;
        collected += n;
        frames++;
    }

    close(fd);
    rt_device_close(dev);
    LOG_I("录音完成: %d 样本 (%d 字节) -> %s", collected, collected * 2, path);
    LOG_I("峰值 RMS = %d（<500 基本是静音/底噪；>2000 说明采到了声音）", (int)rms_peak);
    return 0;
}

/* ---------------- R0-1 免 SD 麦克风实测 ---------------- */

int kws_mic_test(int seconds)
{
    if (seconds <= 0 || seconds > 30)
    {
        LOG_E("实测时长需在 1~30 秒之间");
        return -RT_EINVAL;
    }

    if (s_enabled)
    {
        LOG_E("请先 kws stop 再实测（麦克风被占用）");
        return -RT_EBUSY;
    }

    rt_device_t dev = rt_device_find(KWS_MIC_DEVICE_NAME);
    if (dev == RT_NULL)
    {
        LOG_E("找不到麦克风 '%s'", KWS_MIC_DEVICE_NAME);
        return -RT_ENOSYS;
    }
    if (rt_device_open(dev, RT_DEVICE_FLAG_RDONLY) != RT_EOK)
    {
        LOG_E("无法打开麦克风");
        return -RT_EIO;
    }

    LOG_I("麦克风实测 %d 秒（免 SD，仅统计电平）...", seconds);
    LOG_I("（对着麦克风说话/拍手，下面的电平条应明显跳动）");

    mic_stream_reset();
    static int16_t mono[KWS_PDM_MONO_FRAME_SAMPLES];
    int total = seconds * EI_CLASSIFIER_FREQUENCY;
    int collected = 0, frames = 0, nonzero = 0;
    double sumsq = 0.0;
    int16_t vmin = 32767, vmax = -32768, prev = 0;
    uint32_t zc = 0;
    int peak_rms = 0;

    while (collected < total)
    {
        int n = mic_read(dev, mono);
        if (n == 0)
        {
            rt_thread_mdelay(1);
            continue;
        }
        double fsumsq = 0.0;
        for (int i = 0; i < n; i++)
        {
            int16_t s = mono[i];
            if (s != 0) nonzero++;
            if (s < vmin) vmin = s;
            if (s > vmax) vmax = s;
            if ((prev < 0 && s >= 0) || (prev >= 0 && s < 0)) zc++;
            prev = s;
            fsumsq += (double)s * s;
        }
        sumsq += fsumsq;
        int rms = (int)sqrtf((float)(fsumsq / n));
        if (rms > peak_rms) peak_rms = rms;
        if ((frames % 50) == 0)
        {
            int bars = rms / 200;
            if (bars > 40) bars = 40;
            char meter[48];
            int bi;
            for (bi = 0; bi < bars; bi++) meter[bi] = '#';
            meter[bi] = '\0';
            LOG_I("  电平 %6d |%s", rms, meter);
        }
        collected += n;
        frames++;
    }

    rt_device_close(dev);

    int rms_all = (int)sqrtf((float)(sumsq / (collected > 0 ? collected : 1)));
    int nz_pct  = collected > 0 ? (nonzero * 100 / collected) : 0;
    int zcr_pct = collected > 0 ? (int)((uint32_t)zc * 100 / collected) : 0;
    LOG_I("---- 麦克风实测结果 ----");
    LOG_I("样本 %d 个 / %d 字节；非零样本 %d 个 (%d%%)",
          collected, collected * 2, nonzero, nz_pct);
    LOG_I("电平 RMS：平均 %d / 峰值 %d（<500 静音底噪；>2000 采到声音）",
          rms_all, peak_rms);
    LOG_I("幅度范围：min %d / max %d；过零率 %d%%", vmin, vmax, zcr_pct);
    LOG_I("判定：%s",
          (rms_all > 20 && nz_pct > 5) ? "麦克风工作正常（采到信号）"
                                        : "几乎静音（检查麦克风/环境）");
    return 0;
}

/* ---------------- kws push / kws play：板载喇叭激励端 ---------------- */

static int hexnib(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* 灌一段：kws pushstart <样本数>。缓冲一次分配后长期复用（同一会话里可反复
 * 灌不同片段，每次只留最后一段——RAM 只够一份，而 8s 已覆盖唤醒词/应答语长度）。 */
static int kws_play_prepare(int samples)
{
    if (s_play_running)
    {
        LOG_E("正在回放，先 kws stopplay");
        return -RT_EBUSY;
    }
    if (samples <= 0)
    {
        LOG_E("样本数需 > 0");
        return -RT_EINVAL;
    }
    if (s_play_buf == RT_NULL)
    {
        int cap = KWS_PLAY_MAX_SEC * KWS_PLAY_SR;
        s_play_buf = (int16_t *)rt_malloc(cap * (int)sizeof(int16_t));
        if (s_play_buf == RT_NULL)
        {
            LOG_E("分配回放缓冲失败（需 %d KB）", cap * 2 / 1024);
            return -RT_ENOMEM;
        }
        s_play_cap = cap;
    }
    s_play_len = 0;
    s_play_seq = 0;
    s_play_sum = 0;
    s_play_peak = 0;
    s_play_drop = 0;
    rt_kprintf("PUSHSTART cap=%d want=%d\n", s_play_cap, samples);
    return 0;
}

/* 一行 = 一个序号 + 若干样本（LE 16bit，每样本 4 个 hex 字符）。
 * 只认期望序号：控制台一边收字节一边还要把日志往外吐，RX 环被日志挤爆时就会丢行；
 * 丢了必须让 PC 知道并从期望号重发，否则整段音频会悄悄时间压缩（回放出来的"剂量"
 * 和 PC 以为发的不是同一份，这轮观测就废了）。 */
static int kws_push_chunk(int argc, char **argv)
{
    if (argc < 4)
    {
        rt_kprintf("用法: kws push <序号> <hex>\n");
        return -RT_EINVAL;
    }
    int seq = atoi(argv[2]);
    const char *hex = argv[3];
    int hl = (int)strlen(hex);

    if (s_play_running)
    {
        rt_kprintf("PUSHERR playing exp=%d\n", s_play_seq);
        return -RT_EBUSY;
    }
    if (s_play_buf == RT_NULL || seq != s_play_seq)
    {
        rt_kprintf("PUSHERR seq=%d exp=%d\n", seq, s_play_seq);
        return -RT_EINVAL;
    }
    if ((hl & 3) != 0)
    {
        rt_kprintf("PUSHERR hex_len=%d\n", hl);
        return -RT_EINVAL;
    }

    int n = hl / 4;
    for (int i = 0; i < n; i++)
    {
        int h0 = hexnib(hex[i * 4]), l0 = hexnib(hex[i * 4 + 1]);
        int h1 = hexnib(hex[i * 4 + 2]), l1 = hexnib(hex[i * 4 + 3]);
        if ((h0 | l0 | h1 | l1) < 0)
        {
            rt_kprintf("PUSHERR hex_at=%d\n", i * 4);
            return -RT_EINVAL;
        }
        int16_t v = (int16_t)((unsigned)((h0 * 16 + l0) | ((h1 * 16 + l1) << 8)));
        if (s_play_len < s_play_cap)
        {
            s_play_buf[s_play_len] = v;
            s_play_len++;
            s_play_sum += (uint32_t)(int32_t)v;
            int a = v < 0 ? -v : v;
            if (a > s_play_peak) s_play_peak = a;
        }
        else
        {
            s_play_drop++;
        }
    }
    s_play_seq++;
    /* 每行都回执：实测板端消费速率远低于 115200 的波特率上限（50ms/行全收，20ms/行
     * 一行不进，RX 环直接吃掉行首），PC 只有靠"发一行等一行回执"才能贴着真实速率跑，
     * 定时 sleep 猜不准。回执同时带 len，PC 逐行核 len==已收序号×16，缺字符当场重发。 */
    rt_kprintf("PUSHACK seq=%d len=%d\n", s_play_seq, s_play_len);
    return 0;
}

static int kws_push_stat(void)
{
    rt_kprintf("PUSHSTAT len=%d cap=%d seq=%d drop=%d sum=%u peak=%d running=%d\n",
               s_play_len, s_play_cap, s_play_seq, s_play_drop,
               (unsigned)s_play_sum, s_play_peak, (int)s_play_running);
    return 0;
}

/* 一次放音：优先级 15（高于 kws 的 20，低于放音管线中断），写满即阻塞在管道上，
 * 由驱动按 16kHz 消费——所以线程不需要自己定时，节奏是硬件给的。 */
static void play_once(void)
{
    rt_device_t dev = rt_device_find("sound0");
    ft_audio_status_t st;
    uint32_t rate = KWS_PLAY_SR, bits = 16U, ch = 1U;
    int total = s_play_len;
    int result = 0;
    rt_uint32_t t_begin = 0;

    if (dev == RT_NULL)
    {
        rt_kprintf("PLAY_ERR no_sound0\n");
        goto out;
    }
    if (ft_audio_claim_output(FT_AUDIO_OUTPUT_OWNER_LOCAL_PLAYER) != RT_EOK)
    {
        rt_kprintf("PLAY_ERR busy owner=%d\n", (int)ft_audio_get_output_owner());
        goto out;
    }
    /* 记下改前的格式，放完还原：声路是全局唯一资源，别把别人的档位留在板上 */
    if (ft_audio_get_status(&st) == RT_EOK && st.output_sample_rate != 0U)
    {
        rate = st.output_sample_rate; bits = st.output_sample_bits;
        ch = st.output_channels ? st.output_channels : 1U;
    }
    if (ft_audio_set_output_format(KWS_PLAY_SR, 16U, 1U) != RT_EOK)
    {
        ft_audio_release_output(FT_AUDIO_OUTPUT_OWNER_LOCAL_PLAYER);
        rt_kprintf("PLAY_ERR fmt\n");
        goto out;
    }
    if (rt_device_open(dev, RT_DEVICE_OFLAG_WRONLY) != RT_EOK)
    {
        ft_audio_set_output_format(rate, (uint8_t)bits, (uint8_t)ch);
        ft_audio_release_output(FT_AUDIO_OUTPUT_OWNER_LOCAL_PLAYER);
        rt_kprintf("PLAY_ERR open\n");
        goto out;
    }

    t_begin = board_ms();
    rt_kprintf("PLAY_BEGIN @%ums len=%d loops=%u sr=%d ch=1\n",
               (unsigned)t_begin, total, (unsigned)s_play_loops, KWS_PLAY_SR);
    for (rt_uint32_t L = 0; L < s_play_loops && !s_play_abort; L++)
    {
        int off = 0;
        while (off < total && !s_play_abort)
        {
            int n = total - off;
            if (n > 320) n = 320;               /* 20ms @16kHz */
            rt_ssize_t w = rt_device_write(dev, 0, &s_play_buf[off],
                                           n * (rt_ssize_t)sizeof(int16_t));
            if (w <= 0)
            {
                rt_kprintf("PLAY_ERR write=%d off=%d\n", (int)w, off);
                result = -1;
                break;
            }
            off += (int)(w / (rt_ssize_t)sizeof(int16_t));
        }
        if (result < 0) break;
    }
    rt_device_close(dev);
    ft_audio_set_output_format(rate, (uint8_t)bits, (uint8_t)ch);
    ft_audio_release_output(FT_AUDIO_OUTPUT_OWNER_LOCAL_PLAYER);
    /* 先把"忙"标志清掉再打 PLAY_DONE：PC 看到 PLAY_DONE 就会立刻发下一条命令，
     * 而 rt_kprintf 要占几毫秒把 60 个字符推出去——标志留在这行之后就成了竞态窗口
     * （实测第 3 次 kws play 撞上它，被"上一次回放还没结束"拒了）。 */
    s_play_running = RT_FALSE;
    rt_kprintf("PLAY_DONE @%ums dur=%dms loops=%u samples=%d aborted=%d err=%d\n",
               (unsigned)board_ms(), (int)(board_ms() - t_begin),
               (unsigned)s_play_loops, total, (int)s_play_abort, result);

out:
    s_play_running = RT_FALSE;
}

/* 回放线程**只建一次、永不删除**。之前的写法是每次 `kws play` 都 rt_thread_create，
 * 放完在末尾 rt_thread_delete(rt_thread_self())——在自己的栈帧里把自家栈和 TCB 释放了，
 * 实测放完一遍整机静默死锁：串口一个字节都不出，SWD 侧探针还在，断电重上电也救不回来
 * （2026-10-19 声学回踩第一次放音后即为此卡死）。所以改成常驻线程等请求信号量。 */
static void play_worker(void *arg)
{
    RT_UNUSED(arg);
    for (;;)
    {
        if (rt_sem_take(&s_play_req, RT_WAITING_FOREVER) != RT_EOK)
            continue;
        play_once();
    }
}

static int kws_play(int loops)
{
    if (s_play_running)
    {
        LOG_E("上一次回放还没结束");
        return -RT_EBUSY;
    }
    if (s_play_buf == RT_NULL || s_play_len <= 0)
    {
        LOG_E("回放缓冲是空的（先 kws pushstart/kws push）");
        return -RT_EINVAL;
    }
    if (s_play_drop > 0)
        LOG_W("灌入时被截断了 %d 样本（超出 %d 秒容量）", s_play_drop, KWS_PLAY_MAX_SEC);

    s_play_abort = RT_FALSE;
    s_play_loops = (rt_uint32_t)(loops < 1 ? 1 : loops);
    s_play_running = RT_TRUE;
    if (!s_play_ready)
    {
        rt_sem_init(&s_play_req, "kwsplay", 0, RT_IPC_FLAG_FIFO);
        s_play_tid = rt_thread_create("kwsplay", play_worker, RT_NULL, 2048, 15, 10);
        if (s_play_tid == RT_NULL)
        {
            s_play_running = RT_FALSE;
            LOG_E("回放线程创建失败");
            return -RT_ENOMEM;
        }
        rt_thread_startup(s_play_tid);
        s_play_ready = RT_TRUE;
    }
    return rt_sem_release(&s_play_req);
}

static int kws_stopplay(void)
{
    if (!s_play_running)
    {
        LOG_I("当前没在回放");
        return 0;
    }
    s_play_abort = RT_TRUE;
    return 0;
}

/* 灌数据时把控制台回显关掉（自测台的 push 前置条件）。
 * 开回显时每收 78 字符，板子得发回 78 字符回声 + 提示符，TX 比 RX 还忙
 * （rt_serial_putc 环满即自旋，把 tshell 卡在发送上），128 字节的 RX 环于是溢出，
 * 成百上千行 `kws push` 全变乱码——实测 len 恒 0、一行都进不去。
 * 关掉回显后，链路上限就只剩波特率本身。 */
extern "C" void finsh_set_echo(rt_uint32_t echo);
extern "C" rt_uint32_t finsh_get_echo(void);

static int kws_echo(int argc, char **argv)
{
    if (argc >= 3)
        finsh_set_echo(atoi(argv[2]) ? 1U : 0U);
    rt_kprintf("ECHO %u\n", (unsigned)finsh_get_echo());
    return 0;
}

/* ---------------- MSH 命令 ---------------- */

#ifdef RT_USING_FINSH
#include <finsh.h>

/* 采集/评测共用：将秒数换算成帧数并申请 RAM 缓冲，eval_windows>0 时录完先评再回吐 */
static int cap_start(int seconds, int eval_windows, const char *tag)
{
    if (seconds <= 0 || seconds > 15)
    {
        LOG_E("时长需在 1~15 秒之间（15s 占 480KB 堆）");
        return -RT_EINVAL;
    }
    if (s_cap_frames > 0 || s_cap_buf != RT_NULL || s_eval_run > 0)
    {
        LOG_E("上一次采集/评测/回吐还没结束");
        return -RT_EBUSY;
    }

    int frames = seconds * EI_CLASSIFIER_FREQUENCY / KWS_PDM_MONO_FRAME_SAMPLES;
    int bytes = frames * KWS_PDM_MONO_FRAME_SAMPLES * (int)sizeof(int16_t);
    s_cap_buf = (int16_t *)rt_malloc(bytes);
    if (s_cap_buf == RT_NULL)
    {
        LOG_E("缓冲分配失败（需 %d 字节，用 free 看剩余堆）", bytes);
        return -RT_ENOMEM;
    }
    s_cap_len = 0;
    s_cap_pos = 0;
    s_cap_frames = frames;
    s_eval_windows = eval_windows;
    if (!s_inited && kws_init() != 0)
    {
        rt_free(s_cap_buf);
        s_cap_buf = RT_NULL;
        s_cap_frames = 0;
        s_eval_windows = 0;
        return -RT_ERROR;
    }
    rt_kprintf("%s %d %d\n", tag, seconds, frames);
    rt_event_send(s_event, KWS_EVENT_START);
    return 0;
}

/* kws capture <秒> —— 连续录进 RAM 再回吐（不丢音频）；上限 15s = 480KB 堆 */
int kws_capture(int seconds)
{
    return cap_start(seconds, 0, "KWS_CAPTURE");
}

/* kws eval <秒> —— 同一段 RAM 音频先按整窗重放给推理（EVALW 分数 + EVALF 特征），
 * 再回吐给 PC；秒数即窗数（每窗 1.0s，不重叠）。stage_win>=0 时该窗再走逐级导出。 */
int kws_eval(int seconds, int stage_win)
{
    s_eval_stage_win = stage_win;
    return cap_start(seconds, seconds, "KWS_EVAL");
}

/* kws dsp <mode> [noecho] —— 合成已知信号自检（见 dsp_synth 的模式表）。
 * 不吃麦克风：把整数递推出来的 1 秒信号直接压进 s_ring，逐级打印 DSP 结果，
 * 再（可选）把这段信号按帧回吐，让 PC 逐字节确认两端吃的是同一份输入。 */
static int kws_dsp(int mode, int echo)
{
    if (mode < 0 || mode > KWS_DSP_MODE_MAX)
    {
        LOG_E("自检模式需在 0~%d 之间", KWS_DSP_MODE_MAX);
        return -RT_EINVAL;
    }
    if (s_dsp_mode >= 0)
    {
        LOG_E("上一次自检还没跑完");
        return -RT_EBUSY;
    }
    if (!s_inited && kws_init() != 0)
        return -RT_ERROR;
    s_dsp_echo = echo;
    s_dsp_mode = mode;
    rt_kprintf("KWS_DSP %d %d\n", mode, EI_CLASSIFIER_RAW_SAMPLE_COUNT);
    rt_event_send(s_event, KWS_EVENT_START);
    return 0;
}

/* kws dump <秒> —— 请求 KWS 线程把麦克风原始 mono PCM 以十六进制行流到串口。
 * 注意：必须走线程的读帧路径——自己开麦直接读会拿到无效数据（实测 RMS 顶格）。
 * 实时流受 115200 带宽限（约 18% 占空），要连续音频请用 kws capture。 */
int kws_dump(int seconds)
{
    int frames = seconds * EI_CLASSIFIER_FREQUENCY / KWS_PDM_MONO_FRAME_SAMPLES;
    if (frames <= 0)
        frames = 1;
    s_dump_idx = 0;
    rt_kprintf("KWS_DUMP_BEGIN %d %d 0 0\n", KWS_PDM_MONO_FRAME_SAMPLES, frames);
    s_dump_frames = frames;
    if (!s_inited && kws_init() != 0)
        return -RT_ERROR;
    rt_event_send(s_event, KWS_EVENT_START);
    return 0;
}

/* 线程内调用：把一帧 mono 样本以 hex 行写到串口（一行 = 一个采样帧）。
 * 行首 'K' 后带 4 个 hex 的帧号：串口丢行时 PC 能检出并补位，否则整段音频会
 * 悄悄时间压缩，"板上打分的窗"和"PC 复算的窗"就错位（上一版对账就栽在这里）。 */
static void dump_frame(const int16_t *mono, int n, int idx)
{
    static char line[KWS_PDM_MONO_FRAME_SAMPLES * 4 + 8];
    static const char hx[] = "0123456789abcdef";
    int p = 0;
    line[p++] = 'K';
    line[p++] = hx[(idx >> 12) & 0xF];
    line[p++] = hx[(idx >> 8) & 0xF];
    line[p++] = hx[(idx >> 4) & 0xF];
    line[p++] = hx[idx & 0xF];
    for (int i = 0; i < n; i++)
    {
        uint16_t v = (uint16_t)mono[i];
        line[p++] = hx[(v >> 12) & 0xF];
        line[p++] = hx[(v >> 8) & 0xF];
        line[p++] = hx[(v >> 4) & 0xF];
        line[p++] = hx[v & 0xF];
    }
    line[p++] = '\n';
    line[p] = '\0';
    rt_kprintf("%s", line);
}

static int cmd_kws(int argc, char **argv)
{
    if (argc < 2)
    {
        rt_kprintf("用法: kws <start|stop|info|mic|record|dump|capture|eval|dsp|conf|hits|cool|reset> [参数]\n");
        rt_kprintf("  kws start            开始监听唤醒词\n");
        rt_kprintf("  kws stop             停止监听\n");
        rt_kprintf("  kws info             状态与统计（含每 hop 分数直方图）\n");
        rt_kprintf("  kws conf  [0~100]    读/改唤醒阈值（%%，运行期生效）\n");
        rt_kprintf("  kws hits  [1~3]      读/改确认窗数（最近 3 窗里要几窗超阈值）\n");
        rt_kprintf("  kws cool  [ms]       读/改两次命中的最小间隔\n");
        rt_kprintf("  kws reset            清零分数直方图（换场景前用）\n");
        rt_kprintf("  kws mic [秒]         免SD麦克风实测（默认3秒，输出电平统计）\n");
        rt_kprintf("  kws dump [秒]        麦克风原始PCM以hex行流向串口（实时流会丢帧）\n");
        rt_kprintf("  kws capture [秒]     先连续录进RAM再回吐（不丢音频，1~15秒，默认10）\n");
        rt_kprintf("  kws eval [秒] [窗]   同一段RAM音频：逐窗出板上分数(EVALW)+特征(EVALF)，再回吐；给窗号则该窗逐级导出(DSPE/DSPM/DSPR/DSPC)\n");
        rt_kprintf("  kws dsp [模式] [noecho]  合成信号自检(不吃麦克风)：逐级导出DSP+分数+回吐字节\n");
        rt_kprintf("     模式 0静音 1恒定DC 2白噪声 3等幅1kHz方波 4方波幅度100ms交替 5静音夹20ms突发\n");
        rt_kprintf("  kws record <秒> <路径>  录麦克风 PCM 到 SD 卡（需插卡）\n");
        rt_kprintf("     例: kws record 3 /sdcard/mic_test.pcm\n");
        rt_kprintf("  kws pushstart <样本数> 开一段板载喇叭回放素材（16kHz mono16，容量 %d 秒）\n", KWS_PLAY_MAX_SEC);
        rt_kprintf("  kws push <序号> <hex>  灌一块 PCM（每样本 4 个 hex 字符，≤%d 样本/行）\n", KWS_PUSH_SAMPLES);
        rt_kprintf("  kws pushstat           回放缓冲状态（len/sum/peak/drop，PC 用来核对剂量）\n");
        rt_kprintf("  kws play [遍数]        从板载喇叭(sound0)回放 RAM 里的素材，可与 kws start 同时跑\n");
        rt_kprintf("  kws stopplay           中止回放\n");
        rt_kprintf("  kws echo  <0|1>        关/开控制台回显（灌 push 前必须关，否则 RX 环溢出）\n");
        return -1;
    }

    if (!strcmp(argv[1], "start"))
        return kws_start();
    else if (!strcmp(argv[1], "stop"))
        return kws_stop();
    else if (!strcmp(argv[1], "testevt"))
    {
        /* 软件自测：合成一次唤醒事件，走与真实命中完全相同的上报链路
         * （默认回调 -> IPC 待发槽 -> IPC 线程 -> M33 -> 回执事件）。
         * 用于无人在场时回归验证 M33 侧处理，不必喊话。 */
        LOG_W("[自测] 合成唤醒事件 -> IPC 上报 M33");
        kws_detected_default(KWS_WAKE_WORD_LABEL, 0.99f);
        return 0;
    }
    else if (!strcmp(argv[1], "info"))
        return kws_info();
    else if (!strcmp(argv[1], "mic"))
    {
        int sec = (argc >= 3) ? atoi(argv[2]) : 3;
        return kws_mic_test(sec);
    }
    else if (!strcmp(argv[1], "record"))
    {
        if (argc < 4)
        {
            rt_kprintf("用法: kws record <秒> <路径>\n");
            return -1;
        }
        int sec = atoi(argv[2]);
        return kws_record(argv[3], sec);
    }
    else if (!strcmp(argv[1], "dump"))
    {
        int sec = (argc >= 3) ? atoi(argv[2]) : 3;
        return kws_dump(sec);
    }
    else if (!strcmp(argv[1], "capture"))
    {
        int sec = (argc >= 3) ? atoi(argv[2]) : 10;
        return kws_capture(sec);
    }
    else if (!strcmp(argv[1], "eval"))
    {
        int sec = (argc >= 3) ? atoi(argv[2]) : 5;
        int stage_win = (argc >= 4) ? atoi(argv[3]) : -1;
        return kws_eval(sec, stage_win);
    }
    else if (!strcmp(argv[1], "dsp"))
    {
        int mode = (argc >= 3) ? atoi(argv[2]) : 3;
        int echo = (argc >= 4 && !strcmp(argv[3], "noecho")) ? 0 : 1;
        return kws_dsp(mode, echo);
    }

    else if (!strcmp(argv[1], "conf") || !strcmp(argv[1], "hits") || !strcmp(argv[1], "cool"))
    {
        /* 判决参数运行期可调：一块板一次会话就能扫完"阈值→误触/召回"曲线。
         * 线程只读、shell 写，对齐 4 字节存取自带原子性，不需要加锁。 */
        if (argc >= 3)
        {
            int v = atoi(argv[2]);
            if (!strcmp(argv[1], "conf"))
            {
                if (v < 0 || v > 100) { LOG_E("阈值是 0~100 的百分数"); return -1; }
                s_conf = v / 100.0f;
            }
            else if (!strcmp(argv[1], "hits"))
            {
                if (v < 1 || v > KWS_HIST_LEN) { LOG_E("确认窗数需在 1~%d", KWS_HIST_LEN); return -1; }
                s_hist_hits = v;
            }
            else
            {
                if (v < 0 || v > 60000) { LOG_E("冷却需在 0~60000 ms"); return -1; }
                s_cooldown_ms = (uint32_t)v;
            }
        }
        LOG_I("判决: 阈值 %d%%  确认 %d/%d 窗  冷却 %u ms",
              (int)(s_conf * 100.0f), s_hist_hits, KWS_HIST_LEN, (unsigned)s_cooldown_ms);
        return 0;
    }
    else if (!strcmp(argv[1], "reset"))
    {
        memset(s_score_hist, 0, sizeof(s_score_hist));
        s_score_max = 0.0f;
        LOG_I("分数直方图已清零");
        return 0;
    }
    else if (!strcmp(argv[1], "pushstart"))
    {
        int n = (argc >= 3) ? atoi(argv[2]) : 0;
        return kws_play_prepare(n);
    }
    else if (!strcmp(argv[1], "push"))
        return kws_push_chunk(argc, argv);
    else if (!strcmp(argv[1], "pushstat"))
        return kws_push_stat();
    else if (!strcmp(argv[1], "play"))
    {
        int loops = (argc >= 3) ? atoi(argv[2]) : 1;
        return kws_play(loops);
    }
    else if (!strcmp(argv[1], "stopplay"))
        return kws_stopplay();
    else if (!strcmp(argv[1], "echo"))
        return kws_echo(argc, argv);

    rt_kprintf("未知子命令: %s\n", argv[1]);
    return -1;
}
MSH_CMD_EXPORT_ALIAS(cmd_kws, kws, KWS: start|stop|testevt|info|mic|conf|hits|cool|reset|capture|eval|dsp|pushstart|push|pushstat|play|stopplay|echo);
#endif
