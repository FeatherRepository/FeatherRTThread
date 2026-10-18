"""
dataset_v7.py — 多样语料 + 仅真实环境音负样本（不用真实唤醒词当正样本）

在 dataset.py 基础上新增：
  - 远场模拟：随机衰减 + 低通(高频倾斜) + 梳状混响
  - 多语速（0.85x ~ 1.15x）
  - 加噪（SNR 5~40dB）
  - 随机增益 / 时间平移

目标：把之前真人实测中"轻喊/远喊"只给 25~49% 的那一档整体抬上来。
"""
import os, glob, wave
import numpy as np
from scipy.signal import butter, lfilter, resample_poly

from ei_mfcc import ei_mfcc

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DATA = os.path.join(ROOT, "data", "wakeword")


def read_wav16k(path):
    w = wave.open(path)
    assert w.getframerate() == 16000 and w.getnchannels() == 1, f"{path} 非 16k mono"
    return np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)


def _farfield(x, rng):
    """远场模拟：只做距离衰减 + 轻噪（不做低通/混响）。
    实测：加低通会让模型把"低频为主的环境音"误学成唤醒词（v2/v3/v4 真实环境音 97% 误报）。"""
    return x.astype(np.float32) * rng.uniform(0.20, 1.0)


def _speed(y, rng):
    r = rng.uniform(0.85, 1.15)
    n = int(round(len(y) / r))
    if n < 200:
        return y
    out = resample_poly(y, n, len(y))
    return out[:16000] if len(out) >= 16000 else np.pad(out, (0, 16000 - len(out)))


def aug_variants(x, n_aug, rng, hard=True):
    out = [x]
    for _ in range(n_aug):
        y = x.astype(np.float32)
        # v6：不做频谱类远场增强（低通/混响会把环境音误学成唤醒词，实测 v2-v5 真实环境音 97~100% 误报）
        if False and hard and rng.random() < 0.35:
            y = _farfield(y, rng)
        y = y * rng.uniform(0.5, 1.3)                  # 增益
        if rng.random() < 0.8:
            snr = rng.uniform(5, 40)
            p = np.mean(y ** 2) + 1e-9
            y = y + rng.normal(0, np.sqrt(p / 10 ** (snr / 10)), size=y.shape)
        if hard and rng.random() < 0.6:
            y = _speed(y, rng)                         # 语速
        if rng.random() < 0.5:
            y = np.roll(y, int(rng.integers(-800, 800)))
        out.append(np.clip(y, -32768, 32767).astype(np.int16))
    return out


REAL = os.path.join(ROOT, "data", "real")

def collect_files():
    pos, neg = [], []
    pos += sorted(glob.glob(os.path.join(DATA, "xiaomai", "pos_*.wav")))
    neg += sorted(glob.glob(os.path.join(DATA, "xiaomai", "neg_*.wav")))
    neg += sorted(glob.glob(os.path.join(DATA, "nearwords", "*.wav")))
    neg += sorted(glob.glob(os.path.join(DATA, "zh", "*.wav")))
    neg += sorted(glob.glob(os.path.join(DATA, "en", "*.wav")))
    # 多样语料（v4）：5 音色 TTS 正样本 + 普通语句负样本
    pos += sorted(glob.glob(os.path.join(DATA, "xiaomai_v4", "pos_*.wav")))
    neg += sorted(glob.glob(os.path.join(DATA, "xiaomai_v4", "neg_*.wav")))
    # 真实板载音频（kws dump 抓的）：唤醒词当正样本，环境音当负样本
    neg += sorted(glob.glob(os.path.join(REAL, "ambient_*.wav")))
    return pos, neg


def build(n_aug=10, seed=42, verbose=True):
    rng = np.random.default_rng(seed)
    pos, neg = collect_files()
    if verbose:
        print(f"原始: 正 {len(pos)} / 负 {len(neg)}")
    X, y = [], []
    for p in pos:
        for v in aug_variants(read_wav16k(p), n_aug, rng):
            X.append(ei_mfcc(v)); y.append(1)
    for p in neg:
        k = 12 if ("ambient_" in os.path.basename(p)) else max(2, n_aug // 2)
        for v in aug_variants(read_wav16k(p), k, rng):
            X.append(ei_mfcc(v)); y.append(0)
    X = np.asarray(X, dtype=np.float32)
    y = np.asarray(y, dtype=np.int32)
    if verbose:
        print(f"增强后: 正 {int(y.sum())} / 负 {int((y == 0).sum())}  特征 {X.shape}")
    return X, y


if __name__ == "__main__":
    X, y = build()
    print("X stats: min %.2f max %.2f mean %.3f" % (X.min(), X.max(), X.mean()))
