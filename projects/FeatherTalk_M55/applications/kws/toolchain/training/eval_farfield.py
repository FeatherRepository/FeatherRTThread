"""eval_farfield.py — 对比 v1/v2 模型在不同声学条件下的分数（自主验证远场鲁棒性）"""
import os, sys, glob, wave
import numpy as np
import tensorflow as tf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ei_mfcc import ei_mfcc
from dataset_v2 import _farfield

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
MODELS = {
    "v1(干净TTS训练)": os.path.join(os.path.dirname(os.path.abspath(__file__)), "out", "kws_xiaomai_v1.tflite"),
    "v2(远场增强训练)": os.path.join(os.path.dirname(os.path.abspath(__file__)), "out", "kws_xiaomai.tflite"),
}


def read16k(p):
    w = wave.open(p)
    return np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)


def load(mod_path):
    it = tf.lite.Interpreter(model_path=mod_path); it.allocate_tensors()
    return it, it.get_input_details()[0], it.get_output_details()[0]


def score(it, ide, ode, wav_int16):
    f = ei_mfcc(wav_int16)
    q = np.clip(np.round(f / ide["quantization"][0]) + ide["quantization"][1], -128, 127)
    it.set_tensor(ide["index"], q.astype(np.int8).reshape(ide["shape"])); it.invoke()
    y = it.get_tensor(ode["index"]).astype(np.float32).reshape(-1)
    return float((y[1] - ode["quantization"][1]) * ode["quantization"][0])


def simulate(x, level):
    """level: clean / mid / far —— 固定参数的远场模拟（保证对比可复现）"""
    if level == "clean":
        return x
    rng = np.random.default_rng(12345)
    gain = 1.0 if level == "mid" else 0.45
    fc = 4000 if level == "mid" else 2800
    from scipy.signal import butter, lfilter
    y = x.astype(np.float32) * gain
    b, a = butter(2, fc / 8000.0, btype="low")
    y = lfilter(b, a, y)
    if level == "far":
        y[100:] += 0.2 * y[:-100]
        y = y + rng.normal(0, np.sqrt(np.mean(y**2) + 1e-9) / (10 ** (18 / 10)), size=y.shape)
    elif level == "mid":
        y = y + rng.normal(0, np.sqrt(np.mean(y**2) + 1e-9) / (10 ** (25 / 10)), size=y.shape)
    return np.clip(y, -32768, 32767).astype(np.int16)


def main():
    files = sorted(glob.glob(os.path.join(ROOT, "data/wakeword/xiaomai/pos_*.wav")))
    interp = {k: load(p) for k, p in MODELS.items()}
    print(f"{'条件':<10}" + "".join(f"{k:>22}" for k in MODELS))
    for level in ["clean", "mid", "far"]:
        row = f"{level:<10}"
        for k in MODELS:
            it, ide, ode = interp[k]
            ss = [score(it, ide, ode, simulate(read16k(f), level)) for f in files]
            row += f"{np.mean(ss)*100:>17.1f}%"
        print(row)


if __name__ == "__main__":
    main()
