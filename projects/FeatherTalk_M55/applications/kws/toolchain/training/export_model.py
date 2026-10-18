"""
export_model.py — 把训练好的 int8 TFLite 转成固件可用的头文件

生成与 Edge Impulse 原版**同结构**的 `tflite_learn_333519_3.h`（可直接替换），
并输出集成说明（标签、arena size 等需同步的项）。

用法：
    python export_model.py
输入：out/kws_xiaomai.tflite
输出：out/tflite_learn_333519_3.h  +  out/INTEGRATION.md
"""
import os, json

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")
TFLITE = os.path.join(OUT, "kws_xiaomai.tflite")
HEADER = os.path.join(OUT, "tflite_learn_333519_3.h")
ARENA_SIZE = 32768   # 张量总和上界 21528B + TFLM scratch/结构开销；16384 装不下

HEAD = """/*
 * 本地训练生成（小麦项目 R0-2）— 由 export_model.py 自动生成
 * 替换 Edge Impulse 原版 tflite_learn_333519_3.h
 * 模型：DS-CNN，输入 MFCC(50x13) int8，输出 2 类 int8
 */

#ifndef _EI_CLASSIFIER_TFLITE_LEARN_333519_3_H_
#define _EI_CLASSIFIER_TFLITE_LEARN_333519_3_H_

#define EI_CLASSIFIER_TFLITE_LEARN_333519_3_ARENA_SIZE     __ARENA__
const size_t tflite_learn_333519_3_arena_size = __ARENA__;

#if defined __GNUC__
#define ALIGN(X) __attribute__((aligned(X)))
#elif defined _MSC_VER
#define ALIGN(X) __declspec(align(X))
#elif defined __TASKING__
#define ALIGN(X) __align(X)
#else
#define ALIGN(X)
#endif

#if defined(EI_MODEL_SECTION) && (defined(__GNUC__) || defined(__clang__))
#define STRINGIZE(x) #x
#define STRINGIZE_VALUE_OF(x) STRINGIZE(x)
#define MODEL_SECTION(X) __attribute__((section(STRINGIZE_VALUE_OF(X))))
#else
#define MODEL_SECTION(X)
#endif

MODEL_SECTION(EI_MODEL_SECTION) ALIGN(16) const unsigned char tflite_learn_333519_3[] = {
"""


def main():
    with open(TFLITE, "rb") as f:
        data = f.read()
    n = len(data)

    lines = [HEAD.replace("__ARENA__", str(ARENA_SIZE))]
    for i in range(0, n, 12):
        chunk = data[i:i + 12]
        lines.append("  " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    lines.append("};\n")
    lines.append(f"unsigned int tflite_learn_333519_3_len = {n};\n")
    lines.append("#endif // _EI_CLASSIFIER_TFLITE_LEARN_333519_3_H_\n")

    with open(HEADER, "w") as f:
        f.write("\n".join(lines))
    print(f"已生成: {HEADER}  ({n} 字节模型)")

    # 集成说明
    meta = {}
    mp = os.path.join(OUT, "kws_xiaomai_meta.json")
    if os.path.exists(mp):
        meta = json.load(open(mp))
    doc = f"""# 固件集成说明（自动生成）

模型：`out/kws_xiaomai.tflite`（{n} 字节）→ 头文件 `out/tflite_learn_333519_3.h`

## 需要替换/修改的文件

1. **替换模型头文件**
   `projects/Edgi_Talk_M55_XiaoZhi/edge-impulse/tflite-model/tflite_learn_333519_3.h`
   → 用 `out/tflite_learn_333519_3.h` 覆盖
   （建议先把 edge-impulse 目录拷进 FeatherTalk_M55 解耦，见 WORKFLOW.md）

2. **改标签** `model-parameters/model_variables.h`
   `ei_classifier_inferencing_categories_333519_1[] = {{ "noice", "xiaorui" }}`
   → `{{ "noice", "xiaomai" }}`

3. **改 arena size** `model-parameters/model_metadata.h`
   `EI_CLASSIFIER_TFLITE_LARGEST_ARENA_SIZE` → {ARENA_SIZE}

4. **改 kws.h 标签** `applications/kws/kws.h`
   `KWS_WAKE_WORD_LABEL` → `"xiaomai"`

## 模型量化参数（核对）

- 输入：shape {meta.get('input_shape')}  scale={meta.get('input_scale')}  zp={meta.get('input_zero_point')}
- 输出：shape {meta.get('output_shape')}  scale={meta.get('output_scale')}  zp={meta.get('output_zero_point')}
- 标签：{meta.get('labels')}

> 若输出量化与 EI 运行时硬编码的 (zp=-128, scale=1/256) 不一致，
> 需同步改 `model_variables.h` 的 `ei_fill_result_classification_i8_config_333519_3`
> 的 `zero_point` / `scale` 为上面的输出值。
"""
    with open(os.path.join(OUT, "INTEGRATION.md"), "w") as f:
        f.write(doc)
    print("已生成: out/INTEGRATION.md")


if __name__ == "__main__":
    main()
