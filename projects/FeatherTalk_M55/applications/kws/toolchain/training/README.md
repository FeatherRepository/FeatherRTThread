# 本地训练工作区（R0-2 方案 B）

> 目标：在 PC 上离线训练「小麦小麦」唤醒词模型，导出 int8 TFLite，
> **无缝替换**固件里现有的 `xiaorui` 模型，不改板上任何 DSP/推理代码。

---

## 为什么能无缝替换（关键约束）

板上固件用 Edge Impulse SDK 做「MFCC 特征提取 → TFLite 推理」。
只要我训出的模型满足：
1. **输入** = 与板上**逐位一致**的 MFCC 特征，形状 `[1, 50, 13]`、int8；
2. **输出** = `[1, 2]` int8（类别 noice / xiaomai）；
3. 输出量化 `zero_point=-128, scale=1/256`；

就能直接替换 `tflite-model/tflite_learn_*.h` 里的模型字节数组 + 改标签，其余不动。

所以**最大的技术风险 = MFCC 特征是否逐位对齐**。已通过逆向 EI 源码解决：
`ei_mfcc.py` 逐级复刻了板上的 9 级 MFCC 管线（含 EI 特有的 dc_elimination 与 CMVN）。

## 板上特征提取的确切规格（逆向自 EI 源码）

| 级 | 内容 | 来源 |
|---|---|---|
| 1 | 预加重 `y[n]=x[n]-0.98·x[n-1]` | processing.hpp::preemphasis |
| 2 | 分帧：320 样点/帧、步进 320、共 **50 帧** | processing.hpp::stack_frames (v4) |
| 3 | 每帧**截断到 256**、256 点 FFT、**不加窗**、功率谱=\|FFT\|²/256（129 bins） | numpy.hpp::rfft/power_spectrum |
| 4 | 32 阶 Mel 三角滤波（mel=1127·ln(1+f/700)，末桶 -0.001 修正） | feature.hpp::mfe |
| 5 | log | numpy.hpp::log |
| 6 | DCT-II 正交 | numpy.hpp::dct2 |
| 7 | dc_elimination：第 0 维=log(帧能量) | feature.hpp::mfcc |
| 8 | 取前 13 维 → (50,13) | — |
| 9 | **CMVN**（win=101、对称填充、减均值+除方差） | processing.hpp::cmvnw |

输出 50×13=650，与 `EI_CLASSIFIER_NN_INPUT_FRAME_SIZE=650` 吻合。

---

## 校验方法（用现有模型当"标准答案"）

无法直接比对浮点特征，但可以用**已有的 `xiaorui` 模型当 oracle**：
1. 造一段 "小睿/xiaorui" 的 TTS 音频；
2. 用 `ei_mfcc.py` 算特征 → 喂给 `models/wakeword_xiaorui.tflite`；
3. 若模型对 "xiaorui" 输出**高置信度**（且对 "小麦小麦" 输出低），
   说明我的 MFCC 复刻与板上一致 → 可以放心训练。

这一步是**整个方案的风险闸门**：过了才继续，不过就回来调 `ei_mfcc.py`。

---

## 目录与脚本

| 文件 | 作用 |
|---|---|
| `ei_mfcc.py` | 板上 MFCC 的 Python 复刻（训练特征提取） |
| `validate_mfcc.py` | 用 xiaorui 模型 oracle 校验 MFCC 复刻 —— **已通过对齐闸门** |
| `gen_xiaorui.py` | 生成校验用的「小睿」TTS 音频（供 validate_mfcc.py 用） |
| `dataset.py` … `dataset_v9.py` | 各版数据集管线（语料选择 / 增强 / 滑窗特征提取），**v9 为终版** |
| `train_kws.py` | 训练 DS-CNN + int8 全量化 → `out/kws_xiaomai.tflite` |
| `export_model.py` | 生成固件用的 `out/tflite_learn_333519_3.h` + 集成说明 `out/INTEGRATION.md` |
| `eval_model.py` | 离线评估终版模型（上板前 sanity check） |
| `eval_farfield.py` | 对比不同声学条件下 v1/v2 分数（远场鲁棒性） |
| `compare_models.py` | 多模型 × 四类音频（真实环境音 / 真实唤醒词 / 远场 / 干净）滑窗峰值对比 |

语料与采集工具在隔壁：`../tools/kws_tts_corpus.py`（TTS 造语料）、
`../tools/kws_record.py`（PC 麦采真人样本）、`../tools/kws_validate.py`（验收报表）。

## 环境

Python 3 + tensorflow-cpu / numpy / scipy / soundfile / edge-tts。
Windows 上 TensorFlow 初始化易卡，运行前固定线程数：

```bash
export OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1
```

## 运行（终版 v9 复现顺序）

脚本一律**不接受命令行参数**，语料/输出路径都相对脚本自身目录解析，
在 `training/` 下执行即可：

```bash
python validate_mfcc.py      # 1. 风险闸门（MFCC 对齐校验）
python dataset_v9.py         # 2. 全文件滑窗特征 → 训练/评估张量
python train_kws.py          # 3. DS-CNN + int8 量化 -> out/kws_xiaomai.tflite
python export_model.py       # 4. -> out/tflite_learn_333519_3.h + INTEGRATION.md
python compare_models.py     # 5. 滑窗 eval（口径与板上推理严格一致）
```

---
*建立：2026-10-16 ｜ 最后更新：2026-10-18（v9 定稿、脚本全部落地）。*
