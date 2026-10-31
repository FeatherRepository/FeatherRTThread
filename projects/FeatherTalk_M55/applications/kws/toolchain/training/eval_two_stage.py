#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""eval_two_stage.py — PC 侧仿真"一级门控 → 二级 verifier"两级判决，用于**烧板前调阈值**。

为什么要有它：板上矩阵一轮 40~60 分钟，而两级判决有两个阈值（gate/conf）要配平。
先在 PC 上按固件完全相同的口径跑一遍（1s 窗、hop 500ms、2/3 窗确认、5s 冷却），
把 gate×conf 的网格一次算完，挑好参数再上板验证——避免每换一档阈值就烧一次固件。

口径对齐固件（kws.cpp）：
  - 窗 16000 样本、hop 8000（KWS_WINDOW_MS=1000 / KWS_HOP_MS=500）
  - 一级：run_classifier 的 P(xiaomai)（int8 tflite，输入 MFCC 50×13）
  - 门控：一级 > gate 才跑二级
  - 二级：verifier tflite 同一份特征（独立模型），P(xiaomai) > conf 才算命中窗
  - 持续性：最近 3 窗 ≥2 命中；冷却 5s 内不重复计数

用法：
  python eval_two_stage.py --stage1 out/kws_xiaomai_v31.tflite \
      --verif out/kws_xiaomai_v32v.tflite --gate 0.5 --conf 0.75
  python eval_two_stage.py --sweep        # 网格扫描（gate×conf），只打印汇总表
"""
import os, glob, argparse
import numpy as np
import tensorflow as tf

from ei_mfcc import ei_mfcc
from dataset_v9 import read_wav16k

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
DATA = os.path.join(ROOT, "data")
WIN, HOP = 16000, 8000
HIST_LEN, HIST_HITS, COOLDOWN_MS = 3, 2, 5000


class Net:
    """一个 int8 tflite 模型 + 它自己的量化参数（两个模型参数不同，不能混用）。"""

    def __init__(self, path):
        self.it = tf.lite.Interpreter(model_path=path)
        self.it.allocate_tensors()
        self.i = self.it.get_input_details()[0]
        self.o = self.it.get_output_details()[0]
        self.isc, self.izp = self.i["quantization"]
        self.osc, self.ozp = self.o["quantization"]
        self.shape = self.i["shape"]

    def score(self, feat):
        """feat: (650,) float → P(xiaomai)。"""
        q = np.clip(np.round(feat / self.isc) + self.izp, -128, 127).astype(np.int8)
        self.it.set_tensor(self.i["index"], q.reshape(self.shape))
        self.it.invoke()
        out = self.it.get_tensor(self.o["index"]).astype(np.float32).reshape(-1)
        return float(((out - self.ozp) * self.osc)[1])


def scenarios():
    """(场景名, kind, 文件列表)。pos 用于算召回，neg 用于算误触。"""
    W = os.path.join(DATA, "wakeword")
    S = [
        ("wake_real", "pos", sorted(glob.glob(os.path.join(DATA, "real", "wake_*.wav")))),
        ("wake_tts_v4", "pos", sorted(glob.glob(os.path.join(W, "xiaomai_v4", "pos_*.wav")))[:32]),
        ("wake_tts_v6", "pos", sorted(glob.glob(os.path.join(W, "xiaomai_v6", "pos_*.wav")))[:40]),
        ("nearword", "neg", sorted(glob.glob(os.path.join(W, "nearwords", "*.wav")))),
        ("nearword_sentence", "neg", sorted(glob.glob(os.path.join(W, "nearword_sentences", "*.wav")))),
        ("zh_sentence", "neg", sorted(glob.glob(os.path.join(W, "xiaomai", "neg_*.wav")))),
        ("zh", "neg", sorted(glob.glob(os.path.join(W, "zh", "*.wav")))),
        ("en", "neg", sorted(glob.glob(os.path.join(W, "en", "*.wav")))),
        ("reply", "neg", sorted(glob.glob(os.path.join(DATA, "reply", "*.wav")))),
        ("transient", "neg", sorted(glob.glob(os.path.join(DATA, "transient", "*.wav")))),
        ("ambient", "neg", sorted(glob.glob(os.path.join(DATA, "real", "ambient_*.wav")))),
        # 真实板载负样本（空场/近词/句子）：**只取最新的几轮**，避免把训练语料当评测
        ("boardmic_neg", "neg",
         sorted(glob.glob(os.path.join(DATA, "boardmic", "2026102[34]*", "neg", "*.wav")))[:40]),
    ]
    return [(n, k, fs) for n, k, fs in S if fs]


def window_scores(files, n1, n2):
    """逐窗打分，**按文件分组**返回 [(文件, s1 数组, s2 数组)]。

    为什么按文件分组：板测的 2/3 窗确认跑在连续音频流上，而 PC 侧是"一堆孤立片段"——
    1.7s 的 TTS 只有 2 个窗，2of3 天生过不去（板测用 --min-sec 4.5 连放就是绕这个）。
    所以 PC 侧看两个口径：**窗级**（阈值直接作用的对象）与**文件级**（该文件有没有
    任何一窗过阈值 ≈ 该次发声会不会被唤醒）。2of3 事件数只作参考。
    verifier 全算（PC 算力不是瓶颈），门控在聚合阶段施加。"""
    out = []
    for p in files:
        try:
            x = read_wav16k(p)
        except Exception:
            continue
        if x.size < WIN:
            x = np.pad(x, (0, WIN - x.size))
        a1, a2 = [], []
        for st in range(0, x.size - WIN + 1, HOP):
            f = np.asarray(ei_mfcc(x[st:st + WIN]), dtype=np.float32).reshape(-1)
            a1.append(n1.score(f))
            a2.append(n2.score(f) if n2 is not None else 1.0)
        out.append((os.path.basename(p), np.asarray(a1), np.asarray(a2)))
    return out


def count_hits(s1, s2, gate, conf, single_conf=0.90):
    """按固件口径数命中次数：一级>gate → 二级>conf 记 1，2/3 窗确认 + 5s 冷却。
    返回 (命中次数, 命中窗数)。"""
    n = len(s1)
    hit = ((s1 > gate) & (s2 > conf)) if s2 is not None else (s1 > single_conf)
    hist, pos, hits, last_i = [0] * HIST_LEN, 0, 0, -10 ** 9
    for i in range(n):
        hist[pos % HIST_LEN] = int(hit[i]); pos += 1
        if pos >= HIST_LEN and sum(hist) >= HIST_HITS:
            if (i - last_i) * (HOP / 16.0) >= COOLDOWN_MS:   # hop 500ms → ms
                hits += 1
                last_i = i
    return hits, int(hit.sum())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stage1", default=os.path.join(HERE, "out", "kws_xiaomai_v31.tflite"))
    ap.add_argument("--verif", default=os.path.join(HERE, "out", "kws_xiaomai_v32v.tflite"))
    ap.add_argument("--gate", type=float, default=0.50)
    ap.add_argument("--conf", type=float, default=0.75)
    ap.add_argument("--sweep", action="store_true", help="扫 gate×conf 网格")
    ap.add_argument("--limit", type=int, default=0, help="每场景最多文件数（0=全部）")
    a = ap.parse_args()

    n1 = Net(a.stage1)
    n2 = Net(a.verif) if a.verif and os.path.exists(a.verif) else None
    print("一级: %s  二级: %s" % (os.path.basename(a.stage1),
                                 os.path.basename(a.verif) if n2 else "无（单级）"))

    cache = {}
    for name, kind, files in scenarios():
        if a.limit:
            files = files[:a.limit]
        per = window_scores(files, n1, n2)
        cache[name] = (kind, per)
        s1 = np.concatenate([p[1] for p in per]) if per else np.zeros(1)
        s2 = np.concatenate([p[2] for p in per]) if per else np.zeros(1)
        print("  %-18s %-3s 文件 %3d 窗 %5d | 一级峰 %.2f 二级峰 %.2f"
              % (name, kind, len(per), len(s1), float(s1.max()), float(s2.max())))

    pos = [k for k, v in cache.items() if v[0] == "pos"]
    neg = [k for k, v in cache.items() if v[0] == "neg"]

    def agg(k, gate, conf):
        """返回 (窗数, 命中窗数, 文件数, 命中文件数, 2of3 事件数)。"""
        _, per = cache[k]
        nw = nh = nf = nfh = 0
        s1_all, s2_all = [], []
        for _, s1, s2 in per:
            nf += 1
            hit = (s1 > gate) & (s2 > conf)
            nw += len(s1); nh += int(hit.sum())
            if hit.any():
                nfh += 1
            s1_all.append(s1); s2_all.append(s2)
        ev, _ = count_hits(np.concatenate(s1_all), np.concatenate(s2_all), gate, conf)
        return nw, nh, nf, nfh, ev

    def table(gate, conf):
        return [(k, cache[k][0]) + agg(k, gate, conf) for k in pos + neg]

    if a.sweep:
        print("\n=== gate × conf 网格（命中次数：正=召回，负=误触） ===")
        gates = [0.30, 0.40, 0.50, 0.60, 0.70]
        confs = [0.50, 0.65, 0.75, 0.85, 0.95]
        hdr = "gate\\conf " + " ".join("%6.2f" % c for c in confs)
        print(hdr)
        for g in gates:
            cells = []
            for c in confs:
                rows = table(g, c)
                # 正=文件召回率(%)（该发声有没有任何一窗过阈值）
                pf = sum(r[5] for r in rows if r[1] == "pos")
                pn = sum(r[4] for r in rows if r[1] == "pos")
                nf = sum(r[5] for r in rows if r[1] == "neg")
                nn = sum(r[4] for r in rows if r[1] == "neg")
                cells.append("%3d/%d" % (round(100.0 * pf / max(1, pn)),
                                         round(100.0 * nf / max(1, nn))))
            print("%-9.2f " % g + " ".join("%6s" % x for x in cells))
        print("\n（格式 正样本文件召回%% / 负样本文件误触%%；"
              "正 %d 文件，负 %d 文件）"
              % (sum(r[4] for r in table(gates[0], confs[0]) if r[1] == "pos"),
                 sum(r[4] for r in table(gates[0], confs[0]) if r[1] == "neg")))
        return

    print("\n=== 两级判决：gate=%.2f → conf=%.2f ===" % (a.gate, a.conf))
    print("  %-18s %-3s %6s %8s %8s" % ("场景", "类", "窗数", "命中窗%", "文件命中%"))
    for k, kind, nw, nh, nf, nfh, ev in table(a.gate, a.conf):
        print("  %-18s %-3s %6d %7.1f%% %7.1f%%  (2of3事件 %d)"
              % (k, kind, nw, 100.0 * nh / max(1, nw), 100.0 * nfh / max(1, nf), ev))


if __name__ == "__main__":
    main()
