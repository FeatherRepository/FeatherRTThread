"""
dataset_v10.py — 密相位滑窗 + 诚实留出集

**为什么必须改训练窗步长**：dataset_v9 的 windows() 以 HOP=8000（500ms）从文件起点步进，
所以训练里出现的窗对齐只有 0/500/1000…ms 这几种；板上说话时刻相对 1.0s 窗是随机的，
验收脚本 --jitter 10 把相位铺到 0~450ms 后，模型在**自己训练过的负样本**上都会误触
（ambient_01 5/10 相位误触）——那些相位从来没进过训练集。
v10 把训练窗步长改成 800 样点 = 50ms，正好是验收 10 个相位的同一套对齐。

**留出集**（绝不参与训练，否则验收数就是自证）：
  负：ambient 5 条留 2 条（02/04）+ 每第 3 条 nearwords
  正：wake 2 条留 1 条（wake_02）+ 每第 3 条 TTS 正样本
`python dataset_v10.py` 会把留出集写成 data/holdout/manifest.csv，供 kws_validate.py --manifest 直接用。
"""
import os, csv, glob

import numpy as np

from ei_mfcc import ei_mfcc
from dataset_v9 import read_wav16k, _aug

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DATA = os.path.join(ROOT, "data", "wakeword")
REAL = os.path.join(ROOT, "data", "real")
HOLD = os.path.join(ROOT, "data", "holdout")

WIN = 16000          # 1s，= KWS_WINDOW_MS
STRIDE = 800         # 50ms = 一个 hop 内的 10 个相位（验收 --jitter 10 同口径）

HOLDOUT_REAL_NEG = ("ambient_02.wav", "ambient_04.wav")
HOLDOUT_REAL_POS = ("wake_02.wav",)
HOLDOUT_EVERY = 3    # TTS 语料按下标每 3 条留 1 条


def _split(files, holdout_names=(), every=0):
    """按文件名/下标规则切成 (训练用, 留出)；留出集合绝不进训练。"""
    keep, hold = [], []
    hl = set(holdout_names)
    for i, p in enumerate(files):
        base = os.path.basename(p)
        if base in hl or (every and i % every == every - 1):
            hold.append(p)
        else:
            keep.append(p)
    return keep, hold


def _g(*parts):
    return sorted(glob.glob(os.path.join(DATA, *parts)))


def split():
    pos_tr, pos_hold, neg_tr, neg_hold = [], [], [], []
    for pat in (("xiaomai", "pos_*.wav"), ("xiaomai_v4", "pos_*.wav")):
        k, h = _split(_g(*pat), (), HOLDOUT_EVERY)
        pos_tr += k; pos_hold += h
    k, h = _split(sorted(glob.glob(os.path.join(REAL, "wake_*.wav"))), HOLDOUT_REAL_POS)
    pos_tr += k; pos_hold += h

    for pat in (("xiaomai", "neg_*.wav"), ("xiaomai_v4", "neg_*.wav"),
                ("nearwords", "*.wav"), ("zh", "*.wav"), ("en", "*.wav")):
        k, h = _split(_g(*pat), (), HOLDOUT_EVERY if pat[0] == "nearwords" else 0)
        neg_tr += k; neg_hold += h
    k, h = _split(sorted(glob.glob(os.path.join(REAL, "ambient_*.wav"))), HOLDOUT_REAL_NEG)
    neg_tr += k; neg_hold += h
    return pos_tr, pos_hold, neg_tr, neg_hold


def windows(x, stride=STRIDE):
    """密相位滑窗：不足 1s 补零，窗起点覆盖每一个 stride。"""
    if len(x) < WIN:
        x = np.pad(x, (0, WIN - len(x)))
    return [x[s:s + WIN] for s in range(0, len(x) - WIN + 1, stride)]


def build(n_aug=1, seed=42, verbose=True):
    """返回 (X, y, grp)。grp 是每条特征所属**源文件**的编号：
    密相位窗之间是近重复样本，按窗随机切验证集会让 EarlyStopping 挑到"背过的文件"，
    所以验证集必须按文件整份留出（train_kws.py 用 grp 做分组划分）。"""
    rng = np.random.default_rng(seed)
    pos_tr, pos_hold, neg_tr, neg_hold = split()
    if verbose:
        print(f"训练文件: 正 {len(pos_tr)} / 负 {len(neg_tr)}    "
              f"留出文件: 正 {len(pos_hold)} / 负 {len(neg_hold)}")
    X, y, g, n_win = [], [], [], 0
    for label, files in ((1, pos_tr), (0, neg_tr)):
        for gi, p in enumerate(files, start=1):
            wins = windows(read_wav16k(p))
            n_win += len(wins)
            for w in wins:
                X.append(ei_mfcc(w)); y.append(label); g.append(gi)
                for _ in range(n_aug):
                    X.append(ei_mfcc(_aug(w, rng))); y.append(label); g.append(gi)
    X = np.asarray(X, dtype=np.float32)
    y = np.asarray(y, dtype=np.int32)
    g = np.asarray(g, dtype=np.int32)
    if verbose:
        print(f"密相位窗 {n_win} 个 → 增强后: 正 {int(y.sum())} / 负 {int((y == 0).sum())}  "
              f"特征 {X.shape}  源文件 {len(np.unique(g))} 个")
    return X, y, g


def write_holdout_manifest():
    """留出集 -> data/holdout/manifest.csv（file 列相对 manifest 目录解析）"""
    _, pos_hold, _, neg_hold = split()
    os.makedirs(HOLD, exist_ok=True)
    texts = {}
    for m in glob.glob(os.path.join(DATA, "*", "manifest.csv")):
        base = os.path.dirname(m)
        with open(m, encoding="utf-8") as f:
            for row in csv.DictReader(f):
                p = row["file"] if os.path.isabs(row["file"]) else os.path.join(base, row["file"])
                texts[os.path.abspath(p)] = row.get("text", "")
    path = os.path.join(HOLD, "manifest.csv")
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["file", "label", "text"])
        for files, lab in ((pos_hold, 1), (neg_hold, 0)):
            for p in files:
                w.writerow([os.path.relpath(p, HOLD).replace("\\", "/"), lab,
                            texts.get(os.path.abspath(p), "")])
    print(f"留出集 manifest: {path}   正 {len(pos_hold)} / 负 {len(neg_hold)}")
    return path


if __name__ == "__main__":
    write_holdout_manifest()
    build()
