"""eval_model.py — 离线评估训练好的 xiaomai 模型（上板前 sanity check）"""
import os, sys, glob, wave
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ei_mfcc import ei_mfcc

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
MODEL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out", "kws_xiaomai.tflite")
LABELS = ["noice", "xiaomai"]


def read16k(p):
    w = wave.open(p)
    return np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)


def main():
    import tensorflow as tf
    it = tf.lite.Interpreter(model_path=MODEL); it.allocate_tensors()
    ide, ode = it.get_input_details()[0], it.get_output_details()[0]
    isc, izp = ide["quantization"]; osc, ozp = ode["quantization"]

    def score(p):
        f = ei_mfcc(read16k(p))
        q = np.clip(np.round(f / isc) + izp, -128, 127).astype(np.int8).reshape(ide["shape"])
        it.set_tensor(ide["index"], q); it.invoke()
        y = it.get_tensor(ode["index"]).astype(np.float32).reshape(-1)
        return (y - ozp) * osc

    groups = [
        ("xiaomai(pos)", sorted(glob.glob(os.path.join(ROOT, "data/wakeword/xiaomai/pos_*.wav")))),
        ("nearword",    sorted(glob.glob(os.path.join(ROOT, "data/wakeword/nearwords/*.wav")))),
        ("xiaorui(他词)", sorted(glob.glob(os.path.join(ROOT, "data/wakeword/xiaorui_test_*.wav")))),
    ]
    for name, files in groups:
        hit = 0
        print(f"\n[{name}]")
        for p in files:
            s = score(p)
            top = int(np.argmax(s))
            if name == "xiaomai(pos)":
                hit += (top == 1)
            if name == "nearword":
                hit += (top == 0)
            print(f"  {os.path.basename(p):16s} noice={s[0]:.3f} xiaomai={s[1]:.3f} => {LABELS[top]}")
        if files:
            print(f"  → {hit}/{len(files)} 符合预期")


if __name__ == "__main__":
    main()
