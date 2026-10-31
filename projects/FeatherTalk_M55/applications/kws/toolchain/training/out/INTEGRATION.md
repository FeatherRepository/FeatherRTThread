# 固件集成说明（自动生成）

模型：`out/kws_xiaomai_v26.tflite`（18576 字节）→ 头文件 `out/tflite_learn_333519_3_v26.h`

## 需要替换/修改的文件

1. **替换模型头文件**
   `projects/Edgi_Talk_M55_XiaoZhi/edge-impulse/tflite-model/tflite_learn_333519_3.h`
   → 用 `out/tflite_learn_333519_3_v26.h` 覆盖
   （建议先把 edge-impulse 目录拷进 FeatherTalk_M55 解耦，见 WORKFLOW.md）

2. **改标签** `model-parameters/model_variables.h`
   `ei_classifier_inferencing_categories_333519_1[] = { "noice", "xiaorui" }`
   → `{ "noice", "xiaomai" }`

3. **改 arena size** `model-parameters/model_metadata.h`
   `EI_CLASSIFIER_TFLITE_LARGEST_ARENA_SIZE` → 65536

4. **改 kws.h 标签** `applications/kws/kws.h`
   `KWS_WAKE_WORD_LABEL` → `"xiaomai"`

## 模型量化参数（核对）

- 输入：shape [1, 650]  scale=0.0667961910367012  zp=-23
- 输出：shape [1, 2]  scale=0.00390625  zp=-128
- 标签：['noice', 'xiaomai']

> 若输出量化与 EI 运行时硬编码的 (zp=-128, scale=1/256) 不一致，
> 需同步改 `model_variables.h` 的 `ei_fill_result_classification_i8_config_333519_3`
> 的 `zero_point` / `scale` 为上面的输出值。
