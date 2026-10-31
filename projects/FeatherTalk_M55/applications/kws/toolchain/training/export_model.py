"""
export_model.py — 把训练好的 int8 TFLite 转成固件可用的头文件

生成与 Edge Impulse 原版**同结构**的 `tflite_learn_333519_3.h`（可直接替换），
并输出集成说明（标签、arena size 等需同步的项）。

用法：
    python export_model.py            # 板上v9产物：out/kws_xiaomai.tflite → tflite_learn_333519_3.h
    python export_model.py v11        # 版本化：out/kws_xiaomai_v11.tflite → tflite_learn_333519_3_v11.h
    python export_model.py v32v --name kws_verif   # verifier：符号改名（与一级模型头文件共存）
输出：out/tflite_learn_333519_3.h（或版本化名字）  +  out/INTEGRATION.md
"""
import os, json, sys, argparse

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")
# 可选版本参数：python export_model.py v11 → 读 out/kws_xiaomai_v11.tflite，写版本化头文件。
# 不给参数时保持老行为（out/kws_xiaomai.tflite → tflite_learn_333519_3.h），那是板上引用的文件。
_ap = argparse.ArgumentParser()
_ap.add_argument("ver", nargs="?", default="")
_ap.add_argument("--name", default="", help="模型符号名（默认 tflite_learn_333519_3；verifier 用 kws_verif）")
_args = _ap.parse_args()
VERSION = _args.ver.lstrip("v")
SUF = ("_v" + VERSION) if VERSION else ""
SYM = _args.name or "tflite_learn_333519_3"          # 头文件里的数组/长度符号基名
TFLITE = os.path.join(OUT, "kws_xiaomai%s.tflite" % SUF)
META = os.path.join(OUT, "kws_xiaomai%s_meta.json" % SUF)
# --name 给定（verifier 等第二模型）时用固定文件名 out/<name>.h——固件 include 的是
# 稳定名字，版本号只进 git 留档（手动 cp 一份 *_vNN.h）。不给 --name 时保持老行为
# （版本化后缀，兼容既有一键替换流程）。
HEADER = os.path.join(OUT, ("%s.h" % SYM) if _args.name else "%s%s.h" % (SYM, SUF))
# TFLM arena：张量总和 ~21.3KB + scratch/结构开销。gapmax 池化（GAP+GlobalMax 拼接）
# 实测在 32768 下 AllocateTensors() failed——CONCATENATION/REDUCE_MAX 的 scratch 更多，
# 49152 起跑通（10-21 凌晨实测）。环境变量 KWS_ARENA_SIZE 可覆盖。
ARENA_SIZE = int(os.environ.get("KWS_ARENA_SIZE", "49152"))

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


def head_for(sym):
    """按符号基名生成头文件头（verifier 与一级模型符号不重名才能同时链进固件）。"""
    guard = "_EI_%s_H_" % sym.upper()
    return (HEAD
            .replace("_EI_CLASSIFIER_TFLITE_LEARN_333519_3_H_", guard)
            .replace("EI_CLASSIFIER_TFLITE_LEARN_333519_3_ARENA_SIZE", "%s_ARENA_SIZE" % sym.upper())
            .replace("tflite_learn_333519_3_arena_size", "%s_arena_size" % sym)
            .replace("tflite_learn_333519_3[]", "%s[]" % sym))


def main():
    with open(TFLITE, "rb") as f:
        data = f.read()
    n = len(data)

    lines = [head_for(SYM).replace("__ARENA__", str(ARENA_SIZE))]
    for i in range(0, n, 12):
        chunk = data[i:i + 12]
        lines.append("  " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    lines.append("};\n")
    lines.append(f"unsigned int {SYM}_len = {n};\n")
    lines.append("#endif // _EI_%s_H_\n" % SYM.upper())

    with open(HEADER, "w") as f:
        f.write("\n".join(lines))
    print(f"已生成: {HEADER}  ({n} 字节模型)")

    # 集成说明
    meta = {}
    mp = META
    if os.path.exists(mp):
        meta = json.load(open(mp))
    doc = f"""# 固件集成说明（自动生成）

模型：`out/kws_xiaomai{SUF}.tflite`（{n} 字节）→ 头文件 `out/tflite_learn_333519_3{SUF}.h`

## 需要替换/修改的文件

1. **替换模型头文件**
   `projects/Edgi_Talk_M55_XiaoZhi/edge-impulse/tflite-model/tflite_learn_333519_3.h`
   → 用 `out/tflite_learn_333519_3{SUF}.h` 覆盖
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
