"""kws_silence_audit.py — 训练标签里"静音窗"的泄漏审计（2026-10-19）

为什么要这个（板上误触风暴已判到"模型段"，但机制没人证）：
  dataset_v9/v10 的 windows() 把**整份文件**的每个滑窗都打上该文件的标签，且不足 1s 直接
  零补齐。于是一条 1.68s 的 TTS 正样本，除了中间的语音，**开头静音、结尾零补齐**的窗也被
  当成"小麦小麦"喂进训练。模型若学到"低能量/平谱 = 唤醒词"，板上安静房间就会持续高分——
  这正是观测到的现象（板载空场 10s 无洞采集，板上 87.8%、PC 复刻 86.3%）。

口径（全部用 PC 复刻链路 = 同一个 int8 模型 + ei_mfcc，与板上一致已对账）：
  1) 训练集每个窗按 RMS 分桶，看**分数随能量怎么走**，正/负样本分开；
  2) 纯数字静音窗（全零）单独打分；
  3) 板载空场无洞采集（logs/diag/kws_eval_*.wav）逐窗分数 + 它落在哪个桶。

用法（TF 环境，不吃串口，可与板子自测台并行跑）:
  C:/Users/34586/.workbuddy-ai/binaries/python/envs/kws/Scripts/python.exe tools/kws_silence_audit.py
"""
import glob
import os
import re
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "training"))

from dataset_v10 import STRIDE, WIN, split, windows          # noqa: E402
from dataset_v9 import read_wav16k                            # noqa: E402
from kws_validate import TfliteScorer                          # noqa: E402

BUARDS = (0.5, 5.0, 20.0, 80.0, 300.0, 1000.0, 3000.0)


def bucket(rms):
    for i, b in enumerate(BUARDS):
        if rms < b:
            return i
    return len(BUARDS)


def bname(i):
    lo = 0.0 if i == 0 else BUARDS[i - 1]
    hi = float("inf") if i == len(BUARDS) else BUARDS[i]
    return "%.1f~%s" % (lo, ("inf" if hi == float("inf") else "%.0f" % hi))


def main():
    sc = TfliteScorer(os.path.join(ROOT, "training", "out", "kws_xiaomai.tflite"), "xiaomai")
    pos_tr, pos_hold, neg_tr, neg_hold = split()
    rows = {"pos": {}, "neg": {}}
    quiet_pos = {}          # 每份正样本文件里"低能量窗"的条数（标签泄漏落在哪些文件）
    print("训练集逐窗（stride=%dms，与 v10 同口径）" % (STRIDE / 16))
    for tag, files in (("pos", pos_tr), ("neg", neg_tr)):
        tot = 0
        for p in files:
            x = read_wav16k(p)
            for w in windows(x):
                r = float(np.sqrt(np.mean(w.astype(np.float64) ** 2)))
                s = sc.score_window(w)
                rows[tag].setdefault(bucket(r), []).append(s)
                if tag == "pos" and r < 300.0:
                    k = os.path.basename(p)
                    a = quiet_pos.setdefault(k, [0, 0.0])
                    a[0] += 1
                    a[1] = max(a[1], s)
                tot += 1
        print("  %s 文件 %d 份 → 窗 %d 个" % (tag, len(files), tot))
    if quiet_pos:
        print("  低能量(RMS<300)正样本窗出自哪些文件：")
        for k, (n, s) in sorted(quiet_pos.items(), key=lambda kv: -kv[1][0]):
            print("    %-22s %3d 窗  最高分 %.3f" % (k, n, s))

    print("\nRMS 分桶 × 分数（每桶：窗数 / 均分 / ≥0.5 占比 / ≥0.8 占比）")
    print("  %-12s %-22s %-22s" % ("RMS(int16)", "正样本窗", "负样本窗"))
    for i in range(len(BUARDS) + 1):
        cells = []
        for tag in ("pos", "neg"):
            v = rows[tag].get(i)
            if not v:
                cells.append("-")
                continue
            a = np.asarray(v)
            cells.append("%-5d %.3f %4.1f%% %4.1f%%"
                         % (a.size, a.mean(), 100.0 * (a >= 0.5).mean(),
                            100.0 * (a >= 0.8).mean()))
        print("  %-12s %-22s %-22s" % (bname(i), cells[0], cells[1]))

    sil = sc.score_window(np.zeros(WIN, dtype=np.int16))
    print("\n纯数字静音窗（1.0s 全零）分数 = %.3f" % sil)

    d = os.path.join(ROOT, "logs", "diag")
    caps = sorted(glob.glob(os.path.join(d, "kws_eval_*.wav")))
    for p in caps[-1:]:
        x = read_wav16k(p)
        ws = windows(x)
        ss = np.asarray([sc.score_window(w) for w in ws])
        rr = np.asarray([float(np.sqrt(np.mean(w.astype(np.float64) ** 2))) for w in ws])
        print("板载空场无洞采集 %s：%.1fs  %d 窗  RMS 中位 %.0f  分数 中位 %.3f  峰值 %.3f"
              "  ≥0.5 占 %.1f%%  ≥0.8 占 %.1f%%"
              % (os.path.basename(p), x.size / 16000.0, ss.size, np.median(rr),
                 np.median(ss), ss.max(), 100.0 * (ss >= 0.5).mean(),
                 100.0 * (ss >= 0.8).mean()))
        lo = ss[rr < 80]
        hi = ss[rr >= 80]
        if lo.size and hi.size:
            print("    该采集内低能量窗(RMS<80) %d 个均分 %.3f  vs  其余 %d 个均分 %.3f"
                  % (lo.size, lo.mean(), hi.size, hi.mean()))
        idx = np.argsort(-ss)[:12]
        print("    分数最高的 12 窗（t=窗起点秒 RMS peak 分）：")
        for i in sorted(idx):
            w = ws[i]
            print("      %6.2fs  RMS %6.0f  peak %6d  分 %.3f"
                  % (i * STRIDE / 16000.0, rr[i], int(np.abs(w).max()), ss[i]))

    print("\n读法：若'正样本窗'列在低 RMS 桶的分数明显高于'负样本窗'同桶，"
          "说明标签把静音窗喂成了唤醒词正例（泄漏），板上空场高分是训练配方的必然，不是玄学。")


if __name__ == "__main__":
    main()
