"""
probe_ctx.py — 架构探针：把判定窗从 1.0s 拉长到 2.0s，能不能把「小麦小麦」和近词分开

**要回答的问题**（10-18 晚记在 PROGRESS 的一条*推断*）：
「小麦小麦」约 1.9s，**装不进 1.0s 窗**；能区别它与「买小麦/小麦白/麦克小麦」的线索是
约 2s 尺度上的**重复节律**，而模型输入只有 650 维 = 1s 窗。若如此，再多真实样本也只是
让模型在"信息本身不在输入里"的特征空间里更用力拟合 —— v10 判负后这个可能性必须单独测。

**做法**：同一批训练文件、同一个网络（只改输入长度）、同一套留出集，比 **1s 窗 vs 2s 窗**：
  主口径 = **单窗级相位完备可分性**：每条样本在 10 个窗相位（50ms 步进，覆盖一个 hop）各滑一遍窗，
           只看"该相位里有没有任一窗分数过阈值"。这条不依赖判定规则，直接问"信息在不在输入里"。
  副口径 = **板上判定语义**（`kws_validate.decide`：阈值 → 2of3 历史环 → 5s 冷却）跑在
           **拼接长流**上（留出集首尾相接 + 0.3s 静音）。

**为什么副口径必须用长流**：单条 TTS 词只有约 1.5s，2.0s 窗下每条文件**只出 1 个窗**，
而 2of3 至少要 2 个过阈值窗才可能命中 —— 按单条测会让 2s 档结构性 0 检出，是假结论。

**口径边界（结论只能在范围内用）**：
  * 用 **float32** Keras 模型直接打分，不是板上 int8 TFLite；本探针回答"特征里有没有这个信息"，
    **不替代 R0-5 验收**，也不给"达标/不达标"的绝对读数。
  * 语料是 TTS 合成 + 现有 7 条真实样本，正样本量远小于 R0-4 目标 → 只能比两种架构的**相对**可分性。
  * 2s 窗上板要重算 `KWS_WINDOW_MS`/hist/冷却语义，本探针**不动固件**。

用法（在 training/ 下）：
  OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 TF_ENABLE_ONEDNN_OPTS=0 python probe_ctx.py
"""
import argparse
import os
import sys

import numpy as np

import tensorflow as tf
from tensorflow.keras import callbacks, layers, models

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))

from ei_mfcc import ei_mfcc                                        # noqa: E402
from dataset_v9 import read_wav16k, _aug                           # noqa: E402
from dataset_v10 import STRIDE, split                              # noqa: E402
from kws_validate import SR, decide                              # noqa: E402

BINS, NCLASS, LABEL = 13, 2, 1        # LABEL=1 = xiaomai（与 meta labels 顺序一致）
HOP = SR // 2                       # 500ms，= 板上 hop
NPHASE = 10                         # 一个 hop 内 10 个相位 = 板上全部可能
GAP = 0.30


def windows(x, win, stride=STRIDE):
    """密相位滑窗；不足一个窗补零（与 dataset_v10.windows 同规则，窗长按架构可变）。"""
    if len(x) < win:
        x = np.pad(x, (0, win - len(x)))
    return [x[s:s + win] for s in range(0, len(x) - win + 1, stride)]


def n_windows(files, win):
    return sum(len(windows(read_wav16k(p), win)) for p in files)


def pad_ratios(files, win):
    """该窗长下每条文件被**数字静音**补齐的比例。2s 档会出现"正样本普遍补齐、
    负样本普遍装满"的捷径特征（板上麦克风流永远不会是数字静音），必须单独量化。"""
    return np.array([max(0.0, (win - min(len(read_wav16k(p)), win)) / win) for p in files])


def shortcut_acc(rp, rn):
    """只用"补齐比例"这一个非学习特征、扫阈值取最好方向的平衡准确率 ——
    它越高，说明该档 2s 窗的成绩里有越多与声学无关的语料结构成分。"""
    best = 0.0
    for th in np.unique(np.concatenate([rp, rn])):
        for sgn in (1, -1):
            acc = 0.5 * (np.mean(sgn * rp > sgn * th) + np.mean(sgn * rn <= sgn * th))
            best = max(best, float(acc))
    return best


def make_dataset(win, n_aug, seed):
    pos_tr, _, neg_tr, _ = split()
    rng = np.random.default_rng(seed)
    X, y, g = [], [], []
    for label, files in ((1, pos_tr), (0, neg_tr)):
        for gi, p in enumerate(files, start=1):
            for w in windows(read_wav16k(p), win):
                X.append(ei_mfcc(w, sample_limit=win)); y.append(label); g.append(gi)
                for _ in range(n_aug):
                    X.append(ei_mfcc(_aug(w, rng), sample_limit=win)); y.append(label); g.append(gi)
    return (np.asarray(X, np.float32), np.asarray(y, np.int32), np.asarray(g, np.int32))


def make_model(n_frames):
    """**板上 v9 网络原样**，只把输入帧数换成 50（1s）或 100（2s）——两档只差窗长，
    否则架构差异会污染对照（train_kws.py:30-46）。"""
    inp = layers.Input(shape=(n_frames * BINS,), name="float_input")
    x = layers.Reshape((n_frames, BINS, 1))(inp)
    x = layers.Conv2D(16, (3, 3), strides=2, padding="same", use_bias=False)(x)
    x = layers.BatchNormalization()(x); x = layers.ReLU()(x)
    x = layers.SeparableConv2D(32, (3, 3), padding="same", use_bias=False)(x)
    x = layers.BatchNormalization()(x); x = layers.ReLU()(x)
    x = layers.MaxPool2D((2, 2))(x)
    x = layers.SeparableConv2D(64, (3, 3), padding="same", use_bias=False)(x)
    x = layers.BatchNormalization()(x); x = layers.ReLU()(x)
    x = layers.MaxPool2D((2, 2))(x)
    x = layers.GlobalAveragePooling2D()(x)
    x = layers.Dropout(0.3)(x)
    out = layers.Dense(NCLASS, activation="softmax", name="scores")(x)
    m = models.Model(inp, out)
    # 必须用关键字：Keras 3 的 compile 第 3 个位置参数是 loss_weights，
    # 把 ["accuracy"] 传进去会让字符串进 dtype 提升 → ValueError: dtype='string'
    m.compile(optimizer=tf.keras.optimizers.Adam(1e-3),
              loss="sparse_categorical_crossentropy", metrics=["accuracy"])
    return m


def train(win, tag, n_aug, epochs):
    X, y, grp = make_dataset(win, n_aug, seed=42)
    n_frames = X.shape[1] // BINS
    pos_tr, _, neg_tr, _ = split()
    rp, rn = pad_ratios(pos_tr, win), pad_ratios(neg_tr, win)
    rng = np.random.default_rng(0)
    files = np.unique(grp); rng.shuffle(files)
    va_files = set(files[:max(2, int(len(files) * 0.2))].tolist())
    m = np.isin(grp, list(va_files))
    Xtr, ytr, Xva, yva = X[~m], y[~m], X[m], y[m]
    print(f"[{tag}] 窗 {win * 1000 // SR}ms  输入 {n_frames}帧×{BINS}={X.shape[1]}维   "
          f"增强倍率 {n_aug + 1}x  窗样本 {len(X)}（训 {len(Xtr)} / 验 {len(Xva)}，"
          f"验证文件 {len(va_files)} 个，按文件整份留出）")
    print(f"[{tag}] 数字静音补齐比例 中位数：正样本 {np.median(rp)*100:.1f}%  负样本 {np.median(rn)*100:.1f}%"
          f"  →「只看补齐比例」的平衡准确率 {shortcut_acc(rp, rn)*100:.1f}%"
          f"（50%＝无捷径；越高则该档成绩越要打折）")
    net = make_model(n_frames)
    cw = {0: len(ytr) / (2 * max(1, int((ytr == 0).sum()))),
          1: len(ytr) / (2 * max(1, int((ytr == 1).sum())))}
    net.fit(Xtr, ytr, validation_data=(Xva, yva), epochs=epochs, batch_size=32, class_weight=cw,
            verbose=0, callbacks=[callbacks.EarlyStopping(patience=10, restore_best_weights=True)])
    va_acc = float(np.mean(net.predict(Xva, verbose=0).argmax(1) == yva)) if len(Xva) else float("nan")
    print(f"[{tag}] float 模型按文件留出的验证准确率 = {va_acc*100:.1f}%")
    return net, va_acc


class KerasScorer:
    """与 TfliteScorer 同接口：一窗样本 -> xiaomai 概率（float 路径）。"""

    def __init__(self, net, win):
        self.net, self.win = net, win

    def score_window(self, samples):
        s = np.asarray(samples)
        if len(s) < self.win:
            s = np.pad(s, (0, self.win - len(s)))
        f = np.asarray([ei_mfcc(s, sample_limit=self.win)], np.float32)
        return float(self.net.predict(f, verbose=0)[0][LABEL])


def phase_traces(net, files, win):
    """每条文件在 NPHASE 个窗相位上的逐窗分数。返回 [(名, 时长s, [每相位分数列表])]"""
    offs = [i * HOP // NPHASE for i in range(NPHASE)]
    per = []
    for p in files:
        a = read_wav16k(p)
        phases = []
        for off in offs:
            x = np.concatenate([np.zeros(off, np.int16), a])
            if len(x) < win:
                x = np.pad(x, (0, win - len(x)))
            ws = [x[s:s + win] for s in range(0, len(x) - win + 1, HOP)]
            phases.append(score_list(net, ws, win))
        per.append((os.path.basename(p), len(a) / SR, phases))
    return per


def score_list(net, ws, win):
    if not ws:
        return []
    F = np.asarray([ei_mfcc(w, sample_limit=win) for w in ws], np.float32)
    return list(net.predict(F, verbose=0)[:, LABEL])


def window_level(pos_per, neg_per, grid):
    """主口径：单窗级、相位完备。每相位判据 = 该相位有没有任一窗过阈值。"""
    rows = []
    for th in grid:
        per_phase_pos = []
        for ph in range(NPHASE):
            hits = [1.0 if any(s > th for s in p[2][ph]) else 0.0 for p in pos_per]
            per_phase_pos.append(np.mean(hits) if hits else 0.0)
        per_phase_neg = []
        for ph in range(NPHASE):
            hits = [1.0 if any(s > th for s in p[2][ph]) else 0.0 for p in neg_per]
            per_phase_neg.append(np.mean(hits) if hits else 0.0)
        neg_any = np.mean([1.0 if any(any(s > th for s in ph) for ph in p[2]) else 0.0
                           for p in neg_per])
        rows.append((float(th), float(np.mean(per_phase_pos)), float(np.mean(per_phase_neg)),
                     float(neg_any)))
    return rows


def stream_level(scorer, files, win):
    """副口径：拼成长流跑板上 decide()，在 NPHASE 个相位上各跑一遍。"""
    offs = [i * HOP // NPHASE for i in range(NPHASE)]
    return [concat_tier_sc(scorer, files, win, off)[:3] for off in offs]


def concat_tier_sc(scorer, files, win, off):
    gap = int(round(GAP * SR))
    chunks, marks, pos = [], [], 0
    for p in files:
        a = read_wav16k(p)
        if off:
            a = np.concatenate([np.zeros(off, np.int16), a])
        marks.append((pos, len(a), os.path.basename(p)))
        chunks.append(a)
        chunks.append(np.zeros(gap, np.int16))
        pos += len(a) + gap
    stream = np.concatenate(chunks)
    ws = [stream[s:s + win] for s in range(0, max(0, len(stream) - win + 1), HOP)]
    return score_list(scorer.net, ws, win), marks, len(stream) / SR


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--aug", type=int, default=1)
    ap.add_argument("--epochs", type=int, default=40)
    a = ap.parse_args()

    pos_tr, pos_hold, neg_tr, neg_hold = split()
    print("===== 架构探针：1s 窗 vs 2s 窗（同数据、同网络、同留出集；float 打分）=====")
    print(f"文件数：训练 正{len(pos_tr)}/负{len(neg_tr)}    留出 正{len(pos_hold)}/负{len(neg_hold)}")

    # 两档窗长不同 → 同一批文件滑出的窗数差很多（2s 档少）。为免"样本量少所以差"
    # 被读成"架构差"，按**训练行数**拉平：给窗数少的那档加大增强倍率，并打印实际倍率。
    base = {w: n_windows(pos_tr + neg_tr, w) for w in (SR, 2 * SR)}
    target = base[SR] * (a.aug + 1)
    aug = {w: max(a.aug, min(8, round(target / base[w] - 1))) for w in base}
    print(f"裸窗数：1s {base[SR]}  2s {base[2*SR]}  → 训练行数拉平到约 {target}"
          f"，增强倍率 1s {aug[SR]+1}x / 2s {aug[2*SR]+1}x")

    grid = np.arange(0.05, 0.951, 0.05)
    out = {}
    for win, tag in ((SR, "1s窗(现固件)"), (2 * SR, "2s窗(探针)")):
        net, va_acc = train(win, tag, aug[win], a.epochs)
        pp = phase_traces(net, pos_hold, win)
        npv = phase_traces(net, neg_hold, win)
        rows = window_level(pp, npv, grid)
        out[tag] = dict(va=va_acc, rows=rows)

        print(f"\n---- {tag}：主口径＝单窗级相位完备 ----")
        for th, rc, fph, fany in rows:
            mark = "  <= 负样本全相位全窗不过" if fany == 0.0 else ""
            print(f"  t={th:.2f}  期望检出={rc*100:5.1f}%  期望有误触负样本比例/相位={fph*100:5.1f}%  "
                  f"任一相位误触的负样本={fany*100:5.1f}%{mark}")
        z = [r for r in rows if r[3] == 0.0]
        if z:
            b = max(z, key=lambda r: r[1])
            print(f"  → 零误触可达的最高期望检出 = {b[1]*100:.1f}%（t={b[0]:.2f}）")
        else:
            low = min(r[3] for r in rows)
            b = max([r for r in rows if r[3] == low], key=lambda r: r[1])
            print(f"  → **无任何阈值能把任一相位误触的负样本降到 0**；最低为 {low*100:.1f}%（t={b[0]:.2f}，"
                  f"此时期望检出 {b[1]*100:.1f}%）")

        sc = KerasScorer(net, win)
        tr = stream_level(sc, pos_hold, win)
        trn = stream_level(sc, neg_hold, win)
        det, fph = [], []
        for th in (0.30, 0.50, 0.70, 0.90):
            d = np.mean([len(decide(s, HOP * 1000.0 / SR, th, 3, 2, 5000)) for s, _, _ in tr])
            n = np.mean([len(decide(s, HOP * 1000.0 / SR, th, 3, 2, 5000)) for s, _, _ in trn])
            det.append((th, d)); fph.append((th, n))
        print(f"  副口径＝板上 2of3+5s 冷却跑在拼接长流（每相位平均命中窗数）：")
        print("    正流 " + "  ".join(f"t={t:.2f}:{d:4.2f}" for t, d in det) +
              "   负流 " + "  ".join(f"t={t:.2f}:{n:4.2f}" for t, n in fph))

    k1, k2 = "1s窗(现固件)", "2s窗(探针)"
    print("\n========== 对照结论（主口径：单窗级、相位完备）==========")
    print(f"{'指标':30s} {k1:>14s} {k2:>14s}")
    z1 = max([r[1] for r in out[k1]['rows'] if r[3] == 0.0], default=-1.0)
    z2 = max([r[1] for r in out[k2]['rows'] if r[3] == 0.0], default=-1.0)
    f1 = min(r[3] for r in out[k1]['rows'])
    f2 = min(r[3] for r in out[k2]['rows'])
    print(f"{'float 按文件留出验证准确率':26s} {out[k1]['va']*100:13.1f}% {out[k2]['va']*100:13.1f}%")
    print(f"{'可达到的最低误触负样本比例':25s} {f1*100:13.1f}% {f2*100:13.1f}%")
    print(f"{'零误触下最高期望检出':23s} "
          f"{('%.1f%%' % (z1*100)) if z1 >= 0 else '不存在':>14s} "
          f"{('%.1f%%' % (z2*100)) if z2 >= 0 else '不存在':>14s}")
    print("\n判读：2s 窗若把『最低误触负样本比例』明显压低、或在零误触下给出更高检出，")
    print("      说明重复节律这条线索**确实需要 >1s 上下文**，架构要在补数据之前先定；")
    print("      若两档差不多，则架构不是卡点，回到『真实样本量』这条结论。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
