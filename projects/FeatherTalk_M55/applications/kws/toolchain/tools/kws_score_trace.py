"""kws_score_trace.py — 对给定 wav 逐窗打印唤醒词分数（板上同源口径的"分数明细"）

为什么需要：验收报表只给检出/误触的汇总数，看不出"板上报 96% 而 PC 复算 5%"这类
**同一音频两条链路的分歧**。本脚本用与 kws_validate.py 同一个 int8 模型 + 同一个
ei_mfcc 复刻 + 同一套 2of3/冷却判据，把逐窗分数摊开，用来做板vsPC 的对账。

用法:
  python tools/kws_score_trace.py logs/diag/ambient.wav
  python tools/kws_score_trace.py data/real/*.wav --threshold 0.5 --csv out/trace.csv
"""
import argparse
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "training"))
sys.path.insert(0, HERE)

from kws_validate import TfliteScorer, decide, load_wav16k, relname  # noqa: E402

SR = 16000


def trace(x, scorer, win, hop):
    """返回 [(窗起点秒, 分数)]，与板上滑窗一致（不足一窗的尾巴丢弃）"""
    out = []
    for start in range(0, max(0, len(x) - win) + 1, hop):
        out.append((start / SR, scorer.score_window(x[start:start + win])))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("--model", default=os.path.join(ROOT, "training", "out", "kws_xiaomai.tflite"))
    ap.add_argument("--label", default="xiaomai")
    ap.add_argument("--window-sec", type=float, default=1.0)
    ap.add_argument("--hop-sec", type=float, default=0.5)
    ap.add_argument("--threshold", type=float, default=0.50)
    ap.add_argument("--hist-len", type=int, default=3)
    ap.add_argument("--hist-hits", type=int, default=2)
    ap.add_argument("--cooldown-ms", type=int, default=5000)
    ap.add_argument("--csv", default=None, help="把逐窗分数另存 csv（file,t_start_s,score）")
    a = ap.parse_args()

    win, hop = int(a.window_sec * SR), int(a.hop_sec * SR)
    hop_ms = hop * 1000.0 / SR
    scorer = TfliteScorer(a.model, a.label)

    rows = []
    for f in a.files:
        x = load_wav16k(f)
        tr = trace(x, scorer, win, hop)
        if not tr:
            print(f"{relname(f)}: 音频不足一窗，跳过")
            continue
        sc = np.array([s for _, s in tr])
        hits = decide(list(sc), hop_ms, a.threshold, a.hist_len, a.hist_hits, a.cooldown_ms)
        dur = len(x) / SR
        above = (sc > a.threshold).mean() * 100
        print(f"\n== {relname(f)}  {dur:.2f}s  窗数 {len(tr)} ==")
        print(f"   分数 min {sc.min()*100:.1f}% / 中位 {np.median(sc)*100:.1f}% / "
              f"p90 {np.percentile(sc,90)*100:.1f}% / max {sc.max()*100:.1f}%  "
              f"⇒ >{a.threshold:.2f} 的窗占 {above:.1f}%")
        print(f"   板上判据(2of3+{a.cooldown_ms/1000:.0f}s冷却) 命中 {len(hits)} 次 "
              f"= {len(hits)/max(dur,1e-9)*3600:.1f} 次/小时")
        print("   逐窗: " + " ".join(f"{t:.1f}s:{s*100:.0f}%" for t, s in tr[:40])
              + (" …" if len(tr) > 40 else ""))
        for t, s in tr:
            rows.append((relname(f), round(t, 3), round(s, 5)))

    if a.csv:
        os.makedirs(os.path.dirname(os.path.abspath(a.csv)), exist_ok=True)
        with open(a.csv, "w", encoding="utf-8") as fh:
            fh.write("file,t_start_s,score\n")
            for r in rows:
                fh.write("%s,%s,%s\n" % r)
        print(f"\n已存 {a.csv}（{len(rows)} 行）")


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    main()
