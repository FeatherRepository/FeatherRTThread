#ifndef KWS_H
#define KWS_H

#include <rtthread.h>

/* TFLM arena 的实际情况（2026-10-23 查证）：
 * EI SDK 的 arena 有两条路——定义了 EI_CLASSIFIER_ALLOCATION_STATIC 才是静态数组
 * （可用 DEFINE_SECTION(EI_TENSOR_ARENA_LOCATION) 指定段）；本项目**没定义**它，
 * 所以走 ei_aligned_calloc(16, arena_size)，从 RT-Thread 堆分配。
 * 而 link.ld 里 .heap 横跨整个 m55_data_secondary（1.4MB SOC SRAM，快），
 * 远大于内部 RAM 的 256KB（.bss 已占 244KB）。
 * ⇒ **模型能变大的真正杠杆就是把模型头的 arena_size 调大**（KWS_ARENA_SIZE），
 *    而不是把模型压成 18.6KB。内部 RAM 从来不是瓶颈，堆才是可用的那块。*/

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * KWS 唤醒词模块（小麦项目 R0）
 * 引擎：Edge Impulse TFLite + CMSIS-NN/DSP，运行于 CM55
 * 音频：PDM 数字麦克风（mic0，16kHz mono）
 * ============================================================ */

/* 唤醒词标签：必须与 Edge Impulse 训练时的 label 一致
 * 仓库现有模型为 "xiaorui"；用「小麦小麦」重新训练导出后改为 "小麦小麦" */
#ifndef KWS_WAKE_WORD_LABEL
#define KWS_WAKE_WORD_LABEL   "xiaomai"
#endif

/* 推理窗口 1000ms，滑窗步进 500ms（延迟与漏检的折中） */
#ifndef KWS_WINDOW_MS
#define KWS_WINDOW_MS         1000
#endif
#ifndef KWS_HOP_MS
#define KWS_HOP_MS            500
#endif

/* 置信度阈值与触发策略 */
#ifndef KWS_CONFIDENCE
/* v11.4 定档（2026-10-21，holdout_v11 9正/74负，gapmax）：
 * t=0.90：单条口径检出 100% / 误报 0.00 次/h。
 * 注：v17（2026-10-23）上板实测 TTS 召回 0%/真人 33%（比 v11.4 的 100%/75% 差），
 * 已回滚到 v11.4；v17 的问题见 local_ai/reports/v17_breakthrough 后续记录。 */
#define KWS_CONFIDENCE        0.90f
#endif
/* 触发冷却（同一唤醒词两次命中的最小间隔；5s = 常见 KWS 做法） */
#ifndef KWS_COOLDOWN_MS
#define KWS_COOLDOWN_MS       5000
#endif
/* 持续性判据：最近 N 个窗口里至少 M 个超阈值才算真唤醒（滤掉单窗毛刺误报）。
 * hop=500ms，所以 N=3 ≈ 1.5s 的观测窗——正好覆盖"小麦小麦"4 个音节的时长。 */
#ifndef KWS_HIST_LEN
#define KWS_HIST_LEN          3
#endif
#ifndef KWS_HIST_HITS
#define KWS_HIST_HITS         2
#endif

/* ============================================================
 * 二级 verifier（两级唤醒判定，2026-10-24 加入）
 *
 * 背景：v11.4→v31 十七个版本全部落在同一条"召回 vs 精确"权衡曲线上——
 * 加正样本召回升、误触跟着涨；压负样本误触降、召回归零。单级模型做不到两头都要。
 * v27~v31 证明容量（18.6KB→93KB）不是根因，根因是**单级决策边界**：
 * nearword_sentence（"小麦麦真好吃"）/ reply（"我在"）/ transient 的分数分布
 * 与正样本大面积重叠（v31 挖出 169 个 1s 窗，一级打分最高到 1.00）。
 *
 * 方案（OpenWakeWord 的标准做法）：一级宽召回 + 二级收紧。
 *   一级 = 现役大模型，**低门控**（KWS_VERIF_GATE，只求不漏）；
 *   二级 = 独立 TFLM 实例（kws_verif.h，同一份 1s MFCC 特征，复用同一环形缓冲），
 *          专门在"一级高分误触窗"（mine_hard_negs.py 产物）上训练过，
 *          **高阈值**（KWS_VERIF_CONF）才放行。
 *   两级都过 → 记 1 个命中窗，仍走 KWS_HIST_HITS 持续性判据。
 *
 * 代价：只有一级过门控的 hop 才跑二级（DSP 重提取 ~17ms + NN ~60ms），
 * 负样本占多数的场景几乎不额外花 CPU。verifier 初始化失败时自动退化为单级
 * （ww > KWS_CONFIDENCE），行为回落到 v31。
 * ============================================================ */
#ifndef KWS_VERIF_ENABLE
#define KWS_VERIF_ENABLE      1
#endif
/* 一级门控（宽召回）：低于它不跑二级、直接记 0。
 * v31 板测分数带：真人峰 61~90、难负样本峰最高 100 —— 门控取 0.50 让大部分
 * 正样本窗都能进二级，由二级做精度。运行期可用 `kws verif gate <0~100>` 改。 */
#ifndef KWS_VERIF_GATE
#define KWS_VERIF_GATE        0.50f
#endif
/* 二级确认阈值（精度）：二级是"在难负样本上训练过的收紧器"，
 * 0.75 起试，按板测误触/召回再调。运行期 `kws verif conf <0~100>` 改。 */
#ifndef KWS_VERIF_CONF
#define KWS_VERIF_CONF        0.75f
#endif
/* 二级 arena：tensor 总字节实测 ~119KB（大模型），256KB 含 scratch 足够。
 * 从同一块 1.4MB 堆分配（一级已占 512KB）。 */
#define KWS_VERIF_ARENA_BYTES (256 * 1024)
/* 二级门控命中时打印一行 gate/verif 分数（调参期开，平时关省串口） */
#ifndef KWS_VERIF_LOG
#define KWS_VERIF_LOG         1
#endif

/* KWS 线程优先级。
 * 2026-10-23 修正：原为 20（与 msh shell 同级）。rig 灌数据时 shell 要解析几百行 hex，
 * 与 KWS 线程同优先级互相抢，导致音频缓冲溢出——实测大模型（NN 61ms）交付率从 58% 崩到 19%，
 * 召回测量被严重削弱。这里提到 shell 之上（串口接收是中断驱动的，不会丢数据，只是解析变慢）。
 * RT-Thread 里数字越小优先级越高；msh shell 默认 20，故取 10。 */
#ifndef KWS_THREAD_PRIORITY
#define KWS_THREAD_PRIORITY   10
#endif

/* 开机自启：1 = main() 里 IPC/UI 就绪后自动 kws_start()（无需手动敲命令）。
 * 关掉（-DKWS_AUTOSTART=0）则恢复手动 `kws start`，适合调试/占用麦克风冲突场景。 */
#ifndef KWS_AUTOSTART
#define KWS_AUTOSTART         1
#endif

/* 检测回调（可在外部覆盖） */
typedef void (*kws_detected_cb)(const char *label, float confidence);
rt_weak void kws_detected_default(const char *label, float confidence);
void kws_set_callback(kws_detected_cb cb);

/* 生命周期 */
int kws_init(void);
int kws_deinit(void);

/* 控制 */
int kws_start(void);
int kws_stop(void);
rt_bool_t kws_is_enabled(void);

/* 信息与统计 */
int  kws_info(void);      /* 打印状态/统计/模型信息，返回 0 */
const char *kws_wake_word_label(void);

/* R0-1 麦克风验证：录制原始 PCM 到 SD 卡
 * path 例："/sdcard/mic_test.pcm"（16kHz / mono / int16 原始流）
 * 返回 0 = 成功；负数 = 错误码 */
int kws_record(const char *path, int seconds);

/* R0-1 免 SD 麦克风实测：采集 seconds 秒到内存，仅输出电平统计
 * （不需要 SD 卡；用于证明 PDM 麦克风确实在工作）
 * 返回 0 = 成功；负数 = 错误码 */
int kws_mic_test(int seconds);

#ifdef __cplusplus
}
#endif

#endif /* KWS_H */
