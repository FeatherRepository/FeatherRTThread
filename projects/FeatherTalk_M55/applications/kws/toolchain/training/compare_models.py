"""compare_models.py — v1/v2/v3 在四类音频上的对比（滑窗 1s 取峰值，与板子行为一致）

四类：真实环境音(要低)、真实唤醒词(要高)、远场合成(要高)、干净合成(要高)
"""
import sys, glob, wave, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from scipy.signal import butter, lfilter
from ei_mfcc import ei_mfcc
import tensorflow as tf


def rd(p):
    w = wave.open(p)
    return np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)


def load(p):
    it = tf.lite.Interpreter(model_path=p); it.allocate_tensors()
    return it, it.get_input_details()[0], it.get_output_details()[0]


def sc(it, i, o, win):
    f = ei_mfcc(win)
    q = np.clip(np.round(f / i['quantization'][0]) + i['quantization'][1], -128, 127)
    it.set_tensor(i['index'], q.astype(np.int8).reshape(i['shape'])); it.invoke()
    y = it.get_tensor(o['index']).astype(np.float32).reshape(-1)
    return float((y[1] - o['quantization'][1]) * o['quantization'][0])


def maxsc(it, i, o, x, hop=8000):
    w = 16000
    xs = [sc(it, i, o, x[s:s + w]) for s in range(0, max(1, len(x) - w + 1), hop)]
    return max(xs) if xs else sc(it, i, o, x[-w:])


def sim(x, g=0.45, fc=2800, rev=0.2, nse=18):
    y = x.astype(np.float32) * g
    b, a = butter(2, fc / 8000.0, btype='low')
    y = lfilter(b, a, y)
    if rev > 0:
        y[100:] += rev * y[:-100]
    if nse > 0:
        rng = np.random.default_rng(7)
        y = y + rng.normal(0, np.sqrt(np.mean(y ** 2) + 1e-9) / (10 ** (nse / 10)), size=y.shape)
    return np.clip(y, -32768, 32767).astype(np.int16)


MODELS = [('v9','out/kws_v9_tmp.tflite'),('v8','out/kws_v8_tmp.tflite'),('v1','out/kws_xiaomai_v1.tflite')]

amb = sorted(glob.glob('../data/real/ambient_*.wav'))
wak = sorted(glob.glob('../data/real/wake_*.wav'))
pos = sorted(glob.glob('../data/wakeword/xiaomai/pos_*.wav'))[:8]

print('%-6s %13s %13s %13s %13s %13s' % ('模型','真实环境音↓','真实唤醒词↑','衰减+噪↑','低通+混响↑','干净↑'))
for tag, p in MODELS:
    it, i, o = load(p)
    a = max(maxsc(it, i, o, rd(f)) for f in amb)
    w = max(maxsc(it, i, o, rd(f)) for f in wak)
    ff = max(maxsc(it, i, o, sim(rd(f))) for f in pos)          # 衰减+低通+混响+噪
    at = max(maxsc(it, i, o, sim(rd(f), rev=0.0, fc=7000, nse=20)) for f in pos)  # 纯衰减+噪
    cl = max(maxsc(it, i, o, rd(f)) for f in pos)
    print('%-6s %11.0f%% %11.0f%% %11.0f%% %11.0f%% %11.0f%%' % (tag, a * 100, w * 100, at * 100, ff * 100, cl * 100))
