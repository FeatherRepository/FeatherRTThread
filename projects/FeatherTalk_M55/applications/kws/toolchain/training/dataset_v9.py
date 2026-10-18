"""
dataset_v9.py — 滑窗 + 真实唤醒词正样本 + 真实环境音负样本（两侧全窗口覆盖）

关键修复：训练时**也用滑窗**（1s 窗 / 0.5s 跳），与板子行为完全一致。
原来每段只用前 16000 样本 → 模型只在那一片上学过，板子滑到别处就没学过（实测误报 96%）。

语料：多样 TTS 正样本 + 普通语句负样本 + nearwords/zh/en + **真实板载环境音（负样本）**
v9 起恢复用"真实唤醒词"当正样本（配合滑窗后，两侧样本都全窗口覆盖，可真正分离）
"""
import os, glob, wave
import numpy as np

from ei_mfcc import ei_mfcc

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DATA = os.path.join(ROOT, "data", "wakeword")
REAL = os.path.join(ROOT, "data", "real")
WIN = 16000        # 1 秒窗
HOP = 8000         # 0.5 秒跳（与 KWS_HOP_MS=500 一致）


def read_wav16k(path):
    w = wave.open(path)
    assert w.getframerate() == 16000 and w.getnchannels() == 1, f"{path} 非 16k mono"
    return np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)


def windows(x):
    """把一段音频切成 1s 窗（不足则补零），返回窗口列表"""
    if len(x) < WIN:
        x = np.pad(x, (0, WIN - len(x)))
    out = []
    for s in range(0, len(x) - WIN + 1, HOP):
        out.append(x[s:s + WIN])
    if (len(x) - WIN) % HOP:                     # 尾部也来一窗
        out.append(x[-WIN:])
    # 去掉重复的尾部窗
    if len(out) > 1 and np.array_equal(out[-1], out[-2]):
        out.pop()
    return out


def _aug(w, rng):
    """轻增强：增益 + 噪声 + 语速 + 平移（不做频谱改变）"""
    y = w.astype(np.float32) * rng.uniform(0.5, 1.3)
    if rng.random() < 0.8:
        snr = rng.uniform(8, 35)
        p = np.mean(y ** 2) + 1e-9
        y = y + rng.normal(0, np.sqrt(p / 10 ** (snr / 10)), size=y.shape)
    if rng.random() < 0.4:
        r = rng.uniform(0.9, 1.1)
        n = int(round(len(y) / r))
        idx = np.arange(n) * r
        idx = idx[idx < len(y) - 1]
        y = np.interp(idx, np.arange(len(y)), y)
    if rng.random() < 0.4:
        y = np.roll(y, int(rng.integers(-1600, 1600)))
    return np.clip(y, -32768, 32767).astype(np.int16)


def collect():
    pos, neg = [], []
    pos += sorted(glob.glob(os.path.join(DATA, "xiaomai", "pos_*.wav")))
    pos += sorted(glob.glob(os.path.join(DATA, "xiaomai_v4", "pos_*.wav")))
    pos += sorted(glob.glob(os.path.join(REAL, "wake_*.wav")))   # 真实声学路径唤醒词
    neg += sorted(glob.glob(os.path.join(DATA, "xiaomai", "neg_*.wav")))
    neg += sorted(glob.glob(os.path.join(DATA, "xiaomai_v4", "neg_*.wav")))
    neg += sorted(glob.glob(os.path.join(DATA, "nearwords", "*.wav")))
    neg += sorted(glob.glob(os.path.join(DATA, "zh", "*.wav")))
    neg += sorted(glob.glob(os.path.join(DATA, "en", "*.wav")))
    neg += sorted(glob.glob(os.path.join(REAL, "ambient_*.wav")))
    return pos, neg


def build(n_aug=2, seed=42, verbose=True):
    rng = np.random.default_rng(seed)
    pos, neg = collect()
    if verbose:
        print(f"原始文件: 正 {len(pos)} / 负 {len(neg)}")
    X, y = [], []
    n_win = 0
    for label, files in ((1, pos), (0, neg)):
        for p in files:
            wins = windows(read_wav16k(p))
            n_win += len(wins)
            for w in wins:
                X.append(ei_mfcc(w)); y.append(label)
                for _ in range(n_aug):
                    X.append(ei_mfcc(_aug(w, rng))); y.append(label)
    X = np.asarray(X, dtype=np.float32)
    y = np.asarray(y, dtype=np.int32)
    if verbose:
        print(f"滑窗 {n_win} 个 → 增强后: 正 {int(y.sum())} / 负 {int((y == 0).sum())}  特征 {X.shape}")
    return X, y


if __name__ == "__main__":
    build()
