"""
dataset.py — 读取唤醒词语料 + 数据增强 + 提取 EI-MFCC 特征

正样本：data/wakeword/xiaomai/pos_*.wav（label=1，文本「小麦小麦」）
负样本：data/wakeword/xiaomai/neg_*.wav + nearwords/ + zh/ + en/（label=0）

增强（对每个样本生成多个变体，缓解数据量少）：
  - 随机增益、随机高斯噪声、随机时间平移
输出：X (N,650) float32、y (N,) int 0/1
"""
import os, glob, wave, random
import numpy as np

from ei_mfcc import ei_mfcc

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DATA = os.path.join(ROOT, "data", "wakeword")


def read_wav16k(path):
    w = wave.open(path)
    assert w.getframerate() == 16000 and w.getnchannels() == 1, f"{path} 非 16k mono"
    return np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)


def aug_variants(x, n_aug=4, rng=None):
    """生成增强变体（含原始）"""
    rng = rng or np.random.default_rng()
    out = [x]
    for _ in range(n_aug):
        y = x.astype(np.float32)
        # 随机增益
        y *= rng.uniform(0.6, 1.4)
        # 随机高斯噪声（信噪比 15~35 dB）
        snr_db = rng.uniform(15, 35)
        sig_p = np.mean(y ** 2) + 1e-9
        noise_p = sig_p / (10 ** (snr_db / 10))
        y += rng.normal(0, np.sqrt(noise_p), size=y.shape)
        # 随机时间平移
        shift = int(rng.integers(-800, 800))
        y = np.roll(y, shift)
        out.append(np.clip(y, -32768, 32767).astype(np.int16))
    return out


def collect_files():
    pos, neg = [], []
    pos += sorted(glob.glob(os.path.join(DATA, "xiaomai", "pos_*.wav")))
    neg += sorted(glob.glob(os.path.join(DATA, "xiaomai", "neg_*.wav")))
    neg += sorted(glob.glob(os.path.join(DATA, "nearwords", "*.wav")))
    neg += sorted(glob.glob(os.path.join(DATA, "zh", "*.wav")))
    neg += sorted(glob.glob(os.path.join(DATA, "en", "*.wav")))
    return pos, neg


def build(n_aug=4, seed=0):
    rng = np.random.default_rng(seed)
    pos, neg = collect_files()
    print(f"原始: 正 {len(pos)} / 负 {len(neg)}")
    X, y = [], []
    for p in pos:
        for v in aug_variants(read_wav16k(p), n_aug, rng):
            X.append(ei_mfcc(v)); y.append(1)
    for p in neg:
        for v in aug_variants(read_wav16k(p), max(1, n_aug // 2), rng):
            X.append(ei_mfcc(v)); y.append(0)
    X = np.asarray(X, dtype=np.float32)
    y = np.asarray(y, dtype=np.int32)
    print(f"增强后: 正 {int(y.sum())} / 负 {int((y == 0).sum())}  特征 {X.shape}")
    return X, y


if __name__ == "__main__":
    X, y = build()
    print("X stats: min %.2f max %.2f mean %.3f" % (X.min(), X.max(), X.mean()))
