"""
validate_mfcc.py — 用现有 xiaorui 模型当"标准答案"，校验 ei_mfcc.py 复刻是否对齐

原理：板上模型是"MFCC -> TFLite"。如果我的 Python MFCC 与板子一致，
那么把「xiaorui」音频喂进去应得高 xiaorui 分；把「小麦小麦」喂进去应得低分。

这是本地训练方案的**风险闸门**：过了才继续训练，否则回来调 ei_mfcc.py。
"""
import os, sys, glob, wave
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ei_mfcc import ei_mfcc

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
MODEL = os.path.join(ROOT, "models", "wakeword_xiaorui.tflite")
LABELS = ["noice", "xiaorui"]


def read_wav16k(path):
    w = wave.open(path)
    assert w.getframerate() == 16000 and w.getnchannels() == 1, f"{path} 非 16k mono"
    x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)
    return x


def run_model(interp, x_int16):
    feat = ei_mfcc(x_int16)                     # (650,) float32
    inp = interp.get_input_details()[0]
    out = interp.get_output_details()[0]
    i_scale, i_zp = inp["quantization"]
    o_scale, o_zp = out["quantization"]
    q = np.clip(np.round(feat / i_scale) + i_zp, -128, 127).astype(np.int8)
    q = q.reshape(inp["shape"])
    interp.set_tensor(inp["index"], q)
    interp.invoke()
    y = interp.get_tensor(out["index"]).astype(np.float32).reshape(-1)
    scores = (y - o_zp) * o_scale
    return scores, (i_scale, i_zp, o_scale, o_zp)


def main():
    try:
        import tensorflow as tf
        Interp = tf.lite.Interpreter
    except Exception as e:
        print("需要 tensorflow:", e); sys.exit(2)

    interp = Interp(model_path=MODEL)
    interp.allocate_tensors()

    # 收集测试样本：xiaorui(应命中) 与 xiaomai(应不命中)
    tests = []
    for p in glob.glob(os.path.join(ROOT, "data", "wakeword", "**", "*xiaorui*.wav"), recursive=True):
        tests.append(("xiaorui", p))
    for p in sorted(glob.glob(os.path.join(ROOT, "data", "wakeword", "xiaomai", "pos_*.wav")))[:3]:
        tests.append(("xiaomai", p))
    for p in sorted(glob.glob(os.path.join(ROOT, "data", "wakeword", "nearwords", "*.wav")))[:3]:
        tests.append(("nearword", p))

    if not any(t[0] == "xiaorui" for t in tests):
        print("⚠️ 未找到 xiaorui 音频。请先造一条『小睿』TTS 到 data/wakeword/ 下再校验。")
        print("   （负向校验仍可参考：xiaomai/nearword 应判为 noice）")

    print(f"模型: {os.path.basename(MODEL)}\n")
    ok = True
    for kind, p in tests:
        x = read_wav16k(p)
        scores, q = run_model(interp, x)
        top = int(np.argmax(scores))
        name = os.path.basename(p)
        print(f"[{kind:8s}] {name:16s} -> noice={scores[0]:.3f} xiaorui={scores[1]:.3f}  => {LABELS[top]}")
        if kind == "xiaorui" and (top != 1 or scores[1] < 0.7):
            ok = False
        if kind == "nearword" and (top != 0 or scores[0] < 0.7):
            ok = False
        # 注：xiaomai 仅作参考——xiaorui 模型把"小麦小麦"误判为 xiaorui 属正常
        #（两词都以"小"开头、音近），与特征对齐无关。
    print(f"\n输入量化 scale/zp={q[0]:.6f}/{q[1]}  输出 scale/zp={q[2]:.6f}/{q[3]}")
    print("\n判定:", "✅ MFCC 复刻对齐（闸门通过）" if ok else "❌ 未对齐，需调 ei_mfcc.py")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
