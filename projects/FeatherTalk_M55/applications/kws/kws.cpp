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

#include <fcntl.h>
#include <sys/unistd.h>
#include <math.h>
#include <new>

/* Edge Impulse SDK */
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "edge-impulse-sdk/dsp/numpy.hpp"
#include "model-parameters/model_metadata.h"

#define DBG_TAG "kws"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

/* ---------------- 配置 ---------------- */

#ifndef KWS_MIC_DEVICE_NAME
#define KWS_MIC_DEVICE_NAME "mic0"
#endif

/* PDM 驱动帧：默认 mono，160 样本/帧（10ms @16kHz） */
#ifndef KWS_ENABLE_STEREO_INPUT_FEED
#define KWS_PDM_FRAME_SAMPLES   160
#define KWS_PDM_MONO_FRAME_SAMPLES KWS_PDM_FRAME_SAMPLES
#define KWS_PDM_IS_STEREO 0
#else
#define KWS_PDM_FRAME_SAMPLES   320
#define KWS_PDM_MONO_FRAME_SAMPLES (KWS_PDM_FRAME_SAMPLES / 2)
#define KWS_PDM_IS_STEREO 1
#endif
#define KWS_PDM_FRAME_SIZE (KWS_PDM_FRAME_SAMPLES * sizeof(int16_t))

/* 每多少次推理打印一次分数（0 = 关闭） */
#ifndef KWS_SCORE_LOG_INTERVAL
#define KWS_SCORE_LOG_INTERVAL 20
#endif

/* ---------------- 状态 ---------------- */

static rt_device_t   s_audio_dev = RT_NULL;
static int16_t      *s_ring = RT_NULL;   /* 从堆分配（放 m55_data_secondary，避免占用内部 RAM） */
static volatile rt_bool_t s_enabled = RT_FALSE;
static rt_bool_t     s_inited = RT_FALSE;
static rt_thread_t   s_tid = RT_NULL;
static uint32_t      s_last_detection = 0;
static uint8_t       s_hist[KWS_HIST_LEN];   /* 唤醒词分数历史（持续性判据） */
static uint8_t       s_hist_pos = 0;
static volatile int  s_dump_frames = 0;      /* >0 时线程进入转储模式：把麦克风样本流到串口 */
static rt_event_t    s_event = RT_NULL;
static kws_detected_cb s_cb = RT_NULL;

/* 统计 */
static uint32_t s_inferences = 0;
static uint32_t s_detections = 0;
static uint32_t s_dsp_total_ms = 0;
static uint32_t s_nn_total_ms = 0;
static float    s_last_score = 0.0f;
static int      s_retries = 0;

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

/* 从 mic0 读一帧到 raw；若给 mono 缓冲区则做解交错拷贝，返回 mono 样本数；0 = 无数据 */
static int mic_read(int16_t *raw, int16_t *mono)
{
    rt_size_t rd = rt_device_read(s_audio_dev, 0, raw, KWS_PDM_FRAME_SIZE);
    if (rd == 0)
        return 0;
    int total = (int)(rd / sizeof(int16_t));
#if KWS_PDM_IS_STEREO
    /* 交错立体声取右声道（奇序号） */
    int m = total / 2;
    for (int i = 0; i < m; i++)
        mono[i] = raw[i * 2 + 1];
#else
    int m = total;
    for (int i = 0; i < m; i++)
        mono[i] = raw[i];
#endif
    return m;
}

static int mic_read_frame(int16_t *frame)
{
    static int16_t mono_buf[KWS_PDM_MONO_FRAME_SAMPLES];
    int m = mic_read(frame, mono_buf);
    if (m > 0)
        ring_push(mono_buf, m);
    return m;
}

/* ---------------- 主线程 ---------------- */
static void dump_frame(const int16_t *mono, int n);   /* 前置声明：线程转储用 */

static void kws_thread(void *param)
{
    int hop_samples = KWS_HOP_MS * EI_CLASSIFIER_FREQUENCY / 1000;

    LOG_I("KWS 启动: model=%s sr=%d win=%dms hop=%dms 阈值=%d%% 标签=\"%s\"",
          EI_CLASSIFIER_PROJECT_NAME, EI_CLASSIFIER_FREQUENCY,
          KWS_WINDOW_MS, KWS_HOP_MS, (int)(KWS_CONFIDENCE * 100), KWS_WAKE_WORD_LABEL);
    for (int i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++)
        LOG_I("  label[%d] = %s", i, ei_classifier_inferencing_categories[i]);

    signal_t signal;
    signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
    signal.get_data = &signal_get_data;

    int16_t frame[KWS_PDM_FRAME_SAMPLES];

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
        s_enabled = RT_TRUE;
        LOG_I("开始监听唤醒词...");

        while (s_enabled)
        {
            /* 停止检查 */
            if (rt_event_recv(s_event, KWS_EVENT_STOP,
                              RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                              RT_WAITING_NO, &ev) == RT_EOK)
            {
                s_enabled = RT_FALSE;
                break;
            }

            /* 转储模式：把麦克风 mono 样本流到串口（不推理，复用已验证的读帧路径） */
            if (s_dump_frames > 0)
            {
                static int16_t mbuf[KWS_PDM_MONO_FRAME_SAMPLES];
                int n = mic_read(frame, mbuf);
                if (n > 0)
                {
                    dump_frame(mbuf, n);
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

            /* 读帧累积 hop 个样本 */
            int got = 0;
            while (got < hop_samples && s_enabled)
            {
                int n = mic_read_frame(frame);
                if (n == 0)
                {
                    rt_thread_mdelay(1);
                    continue;
                }
                /* 逐帧直接进环形缓冲（省掉 16KB hop_buf，避免占用内部 RAM/栈） */
                ring_push(frame, n);
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

            s_hist[s_hist_pos % KWS_HIST_LEN] = (ww > KWS_CONFIDENCE) ? 1 : 0;
            s_hist_pos++;

            {
                int n_hi = 0;
                int n = (s_hist_pos < KWS_HIST_LEN) ? (int)s_hist_pos : KWS_HIST_LEN;
                for (int k = 0; k < n; k++)
                    n_hi += s_hist[k];

                if (n >= KWS_HIST_LEN && n_hi >= KWS_HIST_HITS)
                {
                    uint32_t now = rt_tick_get();
                    if ((now - s_last_detection) >= rt_tick_from_millisecond(KWS_COOLDOWN_MS))
                    {
                        s_last_detection = now;
                        s_detections++;
                        s_last_score = ww;
                        LOG_W("*** 唤醒词命中: %s (%d%%, %d/%d窗) ***",
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
    rt_thread_mdelay(60);
    if (s_audio_dev != RT_NULL)
    {
        rt_device_close(s_audio_dev);
        s_audio_dev = RT_NULL;
    }
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
    LOG_I("唤醒词: \"%s\"（阈值 %d%%）", KWS_WAKE_WORD_LABEL, (int)(KWS_CONFIDENCE * 100));
    LOG_I("窗口: %dms / hop %dms", KWS_WINDOW_MS, KWS_HOP_MS);
    LOG_I("模型: %s (sr=%dHz, %dms/窗)", EI_CLASSIFIER_PROJECT_NAME,
          EI_CLASSIFIER_FREQUENCY, KWS_WINDOW_MS);
    LOG_I("推理: %u 次，命中: %u 次，唤醒词最近分数: %d%%",
          s_inferences, s_detections, (int)(s_last_score * 100));
    if (s_inferences > 0)
        LOG_I("平均耗时: DSP %u ms / NN %u ms",
              s_dsp_total_ms / s_inferences, s_nn_total_ms / s_inferences);
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

/* 默认回调：打印日志。外部可定义同名强符号覆盖。 */
rt_weak void kws_detected_default(const char *label, float confidence)
{
    LOG_W("唤醒命中（默认回调）: %s (%d%%)", label, (int)(confidence * 100.0f));
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
    int16_t frame[KWS_PDM_FRAME_SAMPLES];
    int total_samples = seconds * EI_CLASSIFIER_FREQUENCY;
    int collected = 0;
    int frames = 0;
    float rms_peak = 0.0f;

    while (collected < total_samples)
    {
        rt_size_t rd = rt_device_read(dev, 0, frame, KWS_PDM_FRAME_SIZE);
        if (rd == 0)
        {
            rt_thread_mdelay(1);
            continue;
        }
        int n = (int)(rd / sizeof(int16_t));

        /* 实时 RMS 显示（每 50 帧 ≈ 500ms 打印一次） */
        {
            double sumsq = 0.0;
            for (int i = 0; i < n; i++)
                sumsq += (double)frame[i] * frame[i];
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

#if KWS_PDM_IS_STEREO
        for (int i = 0; i < n / 2; i++)
        {
            int16_t mono = frame[i * 2 + 1];
            if (write(fd, &mono, sizeof(mono)) < 0)
                break;
        }
        collected += n / 2;
#else
        if (write(fd, frame, rd) < 0)
            break;
        collected += n;
#endif
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

    int16_t frame[KWS_PDM_FRAME_SAMPLES];
    int total = seconds * EI_CLASSIFIER_FREQUENCY;
    int collected = 0, frames = 0, nonzero = 0;
    double sumsq = 0.0;
    int16_t vmin = 32767, vmax = -32768, prev = 0;
    uint32_t zc = 0;
    int peak_rms = 0;

    while (collected < total)
    {
        rt_size_t rd = rt_device_read(dev, 0, frame, KWS_PDM_FRAME_SIZE);
        if (rd == 0)
        {
            rt_thread_mdelay(1);
            continue;
        }
        int n = (int)(rd / sizeof(int16_t));
        double fsumsq = 0.0;
        for (int i = 0; i < n; i++)
        {
            int16_t s = frame[i];
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

/* ---------------- MSH 命令 ---------------- */

#ifdef RT_USING_FINSH
#include <finsh.h>

/* kws dump <秒> —— 请求 KWS 线程把麦克风原始 mono PCM 以十六进制行流到串口。
 * 注意：必须走线程的读帧路径——自己开麦直接读会拿到无效数据（实测 RMS 顶格）。 */
int kws_dump(int seconds)
{
    int frames = seconds * EI_CLASSIFIER_FREQUENCY / KWS_PDM_MONO_FRAME_SAMPLES;
    if (frames <= 0)
        frames = 1;
    rt_kprintf("KWS_DUMP_BEGIN %d %d\n", KWS_PDM_MONO_FRAME_SAMPLES, frames);
    s_dump_frames = frames;
    if (!s_inited && kws_init() != 0)
        return -RT_ERROR;
    rt_event_send(s_event, KWS_EVENT_START);
    return 0;
}

/* 线程内调用：把一帧 mono 样本以 hex 行写到串口（一行 = 一个采样帧） */
static void dump_frame(const int16_t *mono, int n)
{
    static char line[KWS_PDM_MONO_FRAME_SAMPLES * 4 + 2];
    static const char hx[] = "0123456789abcdef";
    int p = 0;
    line[p++] = 'K';
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
        rt_kprintf("用法: kws <start|stop|info|mic|record|dump> [参数]\n");
        rt_kprintf("  kws start            开始监听唤醒词\n");
        rt_kprintf("  kws stop             停止监听\n");
        rt_kprintf("  kws info             状态与统计\n");
        rt_kprintf("  kws mic [秒]         免SD麦克风实测（默认3秒，输出电平统计）\n");
        rt_kprintf("  kws dump [秒]        麦克风原始PCM以hex行流向串口（PC侧采集真实样本用）\n");
        rt_kprintf("  kws record <秒> <路径>  录麦克风 PCM 到 SD 卡（需插卡）\n");
        rt_kprintf("     例: kws record 3 /sdcard/mic_test.pcm\n");
        return -1;
    }

    if (!strcmp(argv[1], "start"))
        return kws_start();
    else if (!strcmp(argv[1], "stop"))
        return kws_stop();
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

    rt_kprintf("未知子命令: %s\n", argv[1]);
    return -1;
}
MSH_CMD_EXPORT_ALIAS(cmd_kws, kws, KWS: start|stop|info|mic|record|dump);
#endif
