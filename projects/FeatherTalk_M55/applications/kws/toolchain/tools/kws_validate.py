#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
kws_validate.py -- KWS 唤醒词验收工具（**板上同源口径**）

为什么必须同源：本项目 v2~v8 的离线指标虚高、真机误触，根因就是离线口径 ≠ 板上口径。
本脚本的每一项都对着固件 `applications/kws/kws.cpp` / `kws.h` 实现：
  特征   = training/ei_mfcc.py（板上 EI 9 级 MFCC 的逐级复刻，13 维 + CMVN）
  推理   = training/out/*.tflite 的 int8 模型（量化参数取自模型自身，非浮点近似）
  滑窗   = KWS_WINDOW_MS 1000 / KWS_HOP_MS 500
  判定   = 单窗分 > 阈值(0.50) 记 1，最近 KWS_HIST_LEN=3 窗内 >= KWS_HIST_HITS=2 次才命中
  冷却   = 命中后 KWS_COOLDOWN_MS=5000 内不再计（且清空历史）
所以这里算出的检出率/误报率可以直接对照板上行为，不需要再"打折"。

判定口径：每个文件独立当作一路流（按 hist_len-1 个零分窗预热，等价于板上"已经跑过静音窗"）。
`--concat` 额外把同组样本首尾拼成**一条连续语流**再测一次：短促样本单独测时，只有一窗超阈值
的近词会被 2of3 判据全部挡掉，看着像 0 误报；真机连续说话时前后两个词会落进同一个历史环，
`--concat` 测的才是这个真实风险。

`--jitter N` 再把每条样本在 N 个**窗相位**（前置静音 0, hop/N, 2*hop/N …）下各测一遍：单条评测
的词正好从窗边界开始，是最理想相位；板上说话时刻随机，相位抖动下的**期望检出率**才可当验收数。

用法：
  # 真实板采样本（wake_*.wav 作正、ambient_*.wav 作负）
  python kws_validate.py --tag real --concat --jitter 10 \
      --pos ../data/real --pos-include 'wake_*' --neg-include 'ambient_*' --neg ../data/real

  # TTS 语料（用 manifest.csv 的正/负划分）
  python kws_validate.py --tag tts --concat --jitter 10 \
      --manifest ../data/wakeword/xiaomai/manifest.csv \
      --manifest ../data/wakeword/nearwords/manifest.csv

模型缺省 = training/out/kws_xiaomai.tflite（v9 定稿）；推理只做一遍，阈值扫描复用逐窗分数。
依赖：numpy + tensorflow-cpu（先 export OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1）
"""

import argparse
import csv
import glob
import os
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))          # toolchain/
sys.path.insert(0, os.path.join(ROOT, "training"))
from ei_mfcc import ei_mfcc                                # noqa: E402

SR = 16000


def load_wav16k(path):
    with wave.open(path) as w:
        if w.getnchannels() != 1 or w.getsampwidth() != 2:
            raise ValueError(f"{path}: 需 16bit 单声道")
        if w.getframerate() != SR:
            raise ValueError(f"{path}: 采样率 {w.getframerate()} != {SR}")
        return np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)


class TfliteScorer:
    """板上同一个 int8 模型的 PC 复刻：一窗音频 -> 唤醒词概率。"""

    def __init__(self, model_path, label):
        import json
        import tensorflow as tf
        self.it = tf.lite.Interpreter(model_path=model_path)
        self.it.allocate_tensors()
        self.inp = self.it.get_input_details()[0]
        self.out = self.it.get_output_details()[0]
        self.q = tuple(self.inp["quantization"]) + tuple(self.out["quantization"])
        meta = os.path.join(ROOT, "training", "out", "kws_xiaomai_meta.json")
        labels = json.load(open(meta, encoding="utf-8"))["labels"] if os.path.exists(meta) else []
        if label not in labels:
            raise ValueError(f"标签 {label} 不在模型类别 {labels} 中（--label 用模型里的 ASCII 名）")
        self.idx = labels.index(label)

    def score_window(self, samples):
        iscale, izp, oscale, ozp = self.q
        feats = ei_mfcc(samples)
        q = np.clip(np.round(feats / iscale) + izp, -128, 127).astype(np.int8)
        self.it.set_tensor(self.inp["index"], q.reshape(self.inp["shape"]))
        self.it.invoke()
        y = self.it.get_tensor(self.out["index"]).astype(np.float32).reshape(-1)
        return float((y[self.idx] - ozp) * oscale)


def relname(p):
    return os.path.relpath(os.path.abspath(p), ROOT).replace("\\", "/")


def score_files(files, scorer, win, hop):
    """每个文件滑窗打一遍分（推理只这一遍），返回 [(相对名, 时长s, 逐窗分数)]。"""
    out = []
    for p in files:
        audio = load_wav16k(p)
        scores = [scorer.score_window(audio[s:s + win])
                  for s in range(0, max(0, len(audio) - win + 1), hop)]
        out.append((relname(p), len(audio) / SR, scores))
        print(f"  [{len(out):3d}] {relname(p):42s} "
              f"{len(scores):3d} 窗  峰值 {max(scores)*100 if scores else 0:3.0f}%")
    return out


def decide(scores, hop_ms, threshold, hist_len, hist_hits, cooldown_ms):
    """固件 kws.cpp 的持续性判定 + 冷却，返回命中窗下标（相对文件起点的真实窗号）。

    预热：板上是**连续麦克风流**，喊唤醒词之前必然已经流过若干静音窗、历史环早已填满；
    把每个文件当"冷启动的第 0 窗"会让 <hist_len 窗的短促词永远不可能命中，与真机行为不符
    （真机 10-17/10-18 的真人命中就是 2/3 窗）。因此按 hist_len-1 个零分窗预热。
    """
    prime = hist_len - 1
    hist = [0] * hist_len
    hist_pos, last, hits = 0, -10 ** 9, []
    for i, sc in enumerate([0.0] * prime + scores):
        hist[hist_pos % hist_len] = 1 if sc > threshold else 0
        hist_pos += 1
        n = min(hist_pos, hist_len)
        if n >= hist_len and sum(hist[:n]) >= hist_hits:
            if (i - last) * hop_ms >= cooldown_ms:
                last = i
                hits.append(i - prime)
                hist_pos, hist = 0, [0] * hist_len
    return hits


def concat_tier(files, scorer, win, hop, gap_sec):
    """把一批短样本首尾相接（间隔 gap_sec 静音）拼成一条连续流，滑窗打一遍分。

    返回 (逐窗分数, [(样本起始样点, 样本长度, 相对名)], 有效总时长s)。
    静音段是保守近似：板上真机两个词之间是环境音（实测峰值 24%），比数字静音更容易误触。
    """
    gap = int(round(gap_sec * SR))
    chunks, marks, pos = [], [], 0
    for p in files:
        a = load_wav16k(p)
        marks.append((pos, len(a), relname(p)))
        chunks.append(a)
        chunks.append(np.zeros(gap, dtype=np.int16))
        pos += len(a) + gap
    stream = np.concatenate(chunks)
    scores = [scorer.score_window(stream[s:s + win])
              for s in range(0, max(0, len(stream) - win + 1), hop)]
    print(f"  连续流：{len(files)} 条拼接 -> {len(stream)/SR:.1f}s，{len(scores)} 窗")
    return scores, marks, (len(stream) - gap) / SR


def attribute(hits, marks, hop, win):
    """命中窗 -> 归属到重叠最多的那条样本（跨边界的窗同时混进了前后两个词，如实标注占比）。"""
    rows = []
    for i in hits:
        a, b = i * hop, i * hop + win
        name, best = "（静音间隔）", 0
        for s, d, n in marks:
            ov = min(b, s + d) - max(a, s)
            if ov > best:
                name, best = n, ov
        rows.append({"index": i, "time_s": a / SR, "name": name, "overlap": round(best / win, 2)})
    return rows


def jitter_tier(files, scorer, win, hop, offset_samples):
    """同一条样本在 N 种**窗相位**下各打一遍分（前置 0, hop/N, 2*hop/N … 静音）。

    为什么必须测：板上是连续滑窗，说话时刻相对 1000ms 窗是随机的；单条评测里词正好从
    窗边界开始，是最理想相位。相位平移只需覆盖一个 hop（再大就是同一批窗的循环），
    所以 N 个相位 = 对板上全部可能窗对齐的完整采样。
    返回 [(相对名, 时长, [每个相位的逐窗分数])]；分数缓存下来供阈值扫描复用，不再推理。
    """
    per = []
    for p in files:
        a = load_wav16k(p)
        phases = []
        for off in offset_samples:
            x = np.concatenate([np.zeros(off, dtype=np.int16), a])
            phases.append([scorer.score_window(x[s:s + win])
                           for s in range(0, max(0, len(x) - win + 1), hop)])
        per.append((relname(p), len(a) / SR, phases))
        print(f"  {relname(p):42s} "
              f"峰值 {max(max(x) for x in phases)*100:3.0f}%  "
              f"{len(phases)} 个相位")
    return per


def phase_stats(per, th, hop_ms, args):
    """相位完备口径：返回 (命中相位比例均值, 有任一相位命中的样本比例, 每条命中相位数)。"""
    if not per:
        return 0.0, 0.0, []
    counts = [sum(1 for sc in phases
                  if decide(sc, hop_ms, th, args.hist_len, args.hist_hits, args.cooldown_ms))
              for _, _, phases in per]
    nph = max(1, len(per[0][2]))
    return (float(np.mean([c / float(nph) for c in counts])),
            float(np.mean([1.0 if c else 0.0 for c in counts])),
            counts)


def main():
    ap = argparse.ArgumentParser(description="KWS 验收（板上同源口径）")
    ap.add_argument("--model", default=os.path.join(ROOT, "training", "out", "kws_xiaomai.tflite"))
    ap.add_argument("--label", default="xiaomai")
    ap.add_argument("--pos", help="正样本目录（*.wav）")
    ap.add_argument("--neg", help="负样本目录（*.wav）")
    ap.add_argument("--pos-include", default="*", help="正目录内的文件名通配（同目录混合语料时用）")
    ap.add_argument("--neg-include", default="*", help="负目录内的文件名通配")
    ap.add_argument("--manifest", action="append", help="manifest.csv（file,label 列），可多次")
    ap.add_argument("--out", default=os.path.join(ROOT, "reports"))
    ap.add_argument("--tag", default="run")
    ap.add_argument("--threshold", type=float, default=0.50, help="= kws.h KWS_CONFIDENCE")
    ap.add_argument("--window-sec", type=float, default=1.0, help="= KWS_WINDOW_MS")
    ap.add_argument("--hop-sec", type=float, default=0.5, help="= KWS_HOP_MS")
    ap.add_argument("--hist-len", type=int, default=3, help="= KWS_HIST_LEN")
    ap.add_argument("--hist-hits", type=int, default=2, help="= KWS_HIST_HITS")
    ap.add_argument("--cooldown-ms", type=int, default=5000, help="= KWS_COOLDOWN_MS")
    ap.add_argument("--concat", action="store_true",
                    help="额外把同组样本拼成一条连续语流再测一次（短样本单独测不出 2of3 连续误触）")
    ap.add_argument("--concat-gap", type=float, default=0.30, help="拼接时样本之间的静音秒数")
    ap.add_argument("--jitter", type=int, default=0,
                    help="在 N 个窗相位（前置静音 0, hop/N, …）各测一遍，给出真机随机相位下的检出/误触")
    args = ap.parse_args()

    pos_files, neg_files, texts = [], [], {}
    if args.pos:
        pos_files += sorted(glob.glob(os.path.join(args.pos, args.pos_include + ".wav")))
    if args.neg:
        neg_files += sorted(glob.glob(os.path.join(args.neg, args.neg_include + ".wav")))
    for m in args.manifest or []:
        base = os.path.dirname(os.path.abspath(m))
        with open(m, encoding="utf-8") as f:
            for row in csv.DictReader(f):
                p = row["file"] if os.path.isabs(row["file"]) else os.path.join(base, row["file"])
                if os.path.exists(p):
                    dst = pos_files if row["label"] == "1" else neg_files
                    dst.append(p)
                    texts[os.path.abspath(p)] = row.get("text", "")
    if not pos_files or not neg_files:
        ap.error("正/负样本为空（--pos/--neg 或 --manifest）")

    win, hop = int(args.window_sec * SR), int(args.hop_sec * SR)
    hop_ms = hop * 1000.0 / SR
    scorer = TfliteScorer(args.model, args.label)

    lines = []
    def echo(s=""):
        print(s)
        lines.append(s)

    echo("========== KWS 验收报表（板上同源口径） ==========")
    echo(f"模型     : {os.path.relpath(args.model, ROOT)}   标签={args.label}")
    echo(f"判定口径 : 窗 {win/SR*1000:.0f}ms / 跳 {hop/SR*1000:.0f}ms / 阈值 {args.threshold:.2f} / "
          f"{args.hist_hits}of{args.hist_len} 窗 / 冷却 {args.cooldown_ms}ms")
    echo(f"特征/推理: training/ei_mfcc.py + int8 TFLite（与固件同源）")
    echo("")
    echo("正样本逐条：")
    pos = score_files(pos_files, scorer, win, hop)
    echo("负样本逐条：")
    neg = score_files(neg_files, scorer, win, hop)

    hits_pos = [decide(s, hop_ms, args.threshold, args.hist_len, args.hist_hits, args.cooldown_ms)
                for _, _, s in pos]
    hits_neg = [decide(s, hop_ms, args.threshold, args.hist_len, args.hist_hits, args.cooldown_ms)
                for _, _, s in neg]
    p_detect = sum(1 for h in hits_pos if h)
    n_fp = sum(len(h) for h in hits_neg)
    neg_hours = sum(d for _, d, _ in neg) / 3600.0
    recall = p_detect / max(1, len(pos))
    far_h = n_fp / neg_hours if neg_hours > 0 else 0.0
    neg_peaks = [max(s) for _, _, s in neg if s] or [0.0]
    pos_peaks = [max(s) for _, _, s in pos if s] or [0.0]

    echo("")
    echo(f"正样本 {len(pos)} 条 / {sum(d for _, d, _ in pos):.1f}s    "
         f"负样本 {len(neg)} 条 / {neg_hours*3600:.1f}s（{neg_hours:.4f} 小时）")
    echo(f"检出率       = {p_detect}/{len(pos)} = {recall*100:.1f}%")
    echo(f"误报         = {n_fp} 次 / {neg_hours:.4f}h = {far_h:.2f} 次/小时")
    echo(f"负样本峰值分 = {max(neg_peaks)*100:.0f}%（阈值 {args.threshold*100:.0f}%）")
    echo(f"正样本峰值分 = 中位 {np.median(pos_peaks)*100:.0f}% / 最低 {min(pos_peaks)*100:.0f}%")
    lat = [h[0] * hop / SR for h in hits_pos if h]
    if lat:
        echo(f"首次命中延迟 = 中位 {np.median(lat):.2f}s / 最差 {max(lat):.2f}s（从窗起点算）")

    risk = sorted(((max(s) if s else 0.0, n, d) for n, d, s in neg), reverse=True)[:5]
    n_hi_windows = sum(1 for _, _, s in neg for x in s if x > args.threshold)
    echo("\n高分负样本 TOP5（误触风险源，按板上同源口径）：")
    for sc_, name, dur in risk:
        echo(f"  {sc_*100:5.1f}%  {name:42s} {dur:5.1f}s  "
             f"{texts.get(os.path.abspath(os.path.join(ROOT, name)), '')[:22]}")
    if n_hi_windows:
        echo(f"  ⚠ {n_hi_windows} 个负样本窗超阈值——单窗不触发（需 2of3），但连续说话/近词即可能误触")

    verdict_extra = ""
    concat_rows = []
    if args.concat:
        echo(f"\n---- 连续语流模式（--concat，样本间隔 {args.concat_gap:.2f}s 静音）----")
        echo("单条测不出的风险：相邻两个词落进同一个 3 窗历史环 -> 近词串/连续说话是否误触")
        cs, mk, dur = concat_tier(neg_files, scorer, win, hop, args.concat_gap)
        ch = decide(cs, hop_ms, args.threshold, args.hist_len, args.hist_hits, args.cooldown_ms)
        far_concat = len(ch) / (dur / 3600.0) if dur > 0 else 0.0
        echo(f"负样本连续流 {dur:.1f}s（{dur/3600:.4f}h）  误触 {len(ch)} 次 = {far_concat:.2f} 次/小时")
        for r in attribute(ch, mk, hop, win):
            echo(f"    t={r['time_s']:7.2f}s  窗#{r['index']:4d}  归属 {r['name']}"
                 f"（窗内重叠 {r['overlap']*100:.0f}%）  "
                 f"{texts.get(os.path.abspath(os.path.join(ROOT, r['name'])), '')[:22]}")
            concat_rows.append(["negative_stream", r["time_s"], r["index"], r["name"], r["overlap"]])
        if not ch:
            echo("    （连续流内无命中）")
        cs2, mk2, dur2 = concat_tier(pos_files, scorer, win, hop, args.concat_gap)
        ch2 = decide(cs2, hop_ms, args.threshold, args.hist_len, args.hist_hits, args.cooldown_ms)
        det_concat = {r["name"] for r in attribute(ch2, mk2, hop, win)}
        rc_concat = len(det_concat) / max(1, len(pos_files))
        echo(f"正样本连续流 {dur2:.1f}s  检出 {len(det_concat)}/{len(pos_files)} = {rc_concat*100:.1f}%")
        for r in attribute(ch2, mk2, hop, win):
            concat_rows.append(["positive_stream", r["time_s"], r["index"], r["name"], r["overlap"]])

        echo(f"\n两种口径对比（取差的那个下结论）：")
        echo(f"  检出率  单条={recall*100:5.1f}%   连续流={rc_concat*100:5.1f}%")
        echo(f"  误报/时 单条={far_h:7.2f}    连续流={far_concat:7.2f}")
        recall, far_h = min(recall, rc_concat), max(far_h, far_concat)
        verdict_extra = "（已取 单条/连续流 两种口径中较差者）"

    jitter_rows = []
    jpos = jneg = None
    if args.jitter:
        nph = max(1, args.jitter)
        offs = [i * hop // nph for i in range(nph)]
        echo(f"\n---- 窗相位完备模式（--jitter {nph}：一个 hop 内 {nph} 个相位，"
             f"步进 {hop/nph/SR*1000:.0f}ms）----")
        echo("板上是连续滑窗，说话时刻相对 1000ms 窗随机；平移超过一个 hop 只是同一批窗的循环，"
             f"故 {nph} 个相位已覆盖板上全部可能。单条评测的词从窗边界起 = 最理想相位，不能当检出率")
        jpos = jitter_tier(pos_files, scorer, win, hop, offs)
        jneg = jitter_tier(neg_files, scorer, win, hop, offs)
        exp_recall, any_recall, cnt_p = phase_stats(jpos, args.threshold, hop_ms, args)
        exp_fp, any_fp, cnt_n = phase_stats(jneg, args.threshold, hop_ms, args)
        echo(f"\n正样本  随机相位期望检出 = {exp_recall*100:.1f}%   "
             f"任一相位命中 = {any_recall*100:.1f}%   全相位命中 = "
             f"{sum(1 for c in cnt_p if c >= nph)/max(1,len(cnt_p))*100:.1f}%")
        echo("最难唤醒的样本（命中相位数最少）：")
        for c, (n, d, _) in sorted(zip(cnt_p, jpos))[:5]:
            echo(f"  {c:2d}/{nph}  {n:42s}  {texts.get(os.path.abspath(os.path.join(ROOT, n)), '')[:22]}")
        echo(f"负样本  {sum(1 for c in cnt_n if c)}/{len(cnt_n)} 条在至少一个相位下误触   "
             f"随机相位期望误触 = {exp_fp*100:.1f}%")
        for c, (n, d, _) in sorted(zip(cnt_n, jneg), key=lambda x: -x[0])[:10]:
            if not c:
                break
            echo(f"  {c:2d}/{nph}  {n:42s}  {texts.get(os.path.abspath(os.path.join(ROOT, n)), '')[:22]}")
        echo("→ 相位完备口径下「零误触」是硬指标：只要有一条负样本在某相位误触，"
             "板上连续语流迟早会撞上那个相位")
        for c, (n, d, ph) in zip(cnt_p, jpos):
            jitter_rows.append(["positive", n, d, c, len(ph)])
        for c, (n, d, ph) in zip(cnt_n, jneg):
            jitter_rows.append(["negative", n, d, c, len(ph)])
        recall = min(recall, exp_recall)
        verdict_extra += " + 相位完备期望检出"

    grid = np.arange(0.05, 0.951, 0.05)
    phase_sweep, zero_fp, phase_feas = [], [], []
    if args.jitter:
        for th in grid:
            rc, _, _ = phase_stats(jpos, th, hop_ms, args)
            fph, fany, _ = phase_stats(jneg, th, hop_ms, args)
            phase_sweep.append((float(th), rc, fany, fph))
        zero_fp = [r for r in phase_sweep if r[2] == 0.0]
        phase_feas = [r for r in zero_fp if r[1] >= 0.90]

    verdict = "PASS" if (recall >= 0.90 and far_h <= 1.0) else "FAIL"
    if args.jitter and not phase_feas:
        verdict = "FAIL"
    echo(f"\n合格线（检出>=90% @ 误报<=1次/小时"
         f"{' + 相位完备零误触工作点' if args.jitter else ''}）: {verdict} {verdict_extra}")
    if args.jitter and not phase_feas:
        echo(f"   判定失败原因：阈值扫描里**没有一个**阈值能同时做到「负样本全相位零误触」与"
             f"「期望检出>=90%」（见下面相位完备表）")
    if neg_hours < 1.0:
        echo(f"⚠ 负样本总时长 {neg_hours*3600:.0f}s < 1 小时，\"次/小时\"是外推值，统计上不足以下结论；"
             f"R0-4 的 300 条负样本补齐前，本报表只证明验收链路可用 + 给出当前读数。")
    if len(pos) < 100:
        echo(f"⚠ 正样本 {len(pos)} 条 << 目标 100 条，检出率置信区间很宽。")

    sweep_rows = []
    for th in grid:
        d = sum(1 for _, _, s in pos
                if decide(s, hop_ms, th, args.hist_len, args.hist_hits, args.cooldown_ms))
        f_ = sum(len(decide(s, hop_ms, th, args.hist_len, args.hist_hits, args.cooldown_ms))
                 for _, _, s in neg)
        sweep_rows.append((float(th), d / max(1, len(pos)), f_ / neg_hours if neg_hours else 0.0))
    # 可行工作点 = 误报<=1次/小时 且 检出>=90%；在其中取**阈值最大**的那个，
    # 因为阈值越低离噪声越近、真机漂移时最先失守（同 recall 下高阈值更鲁棒）。
    feas = [r for r in sweep_rows if r[2] <= 1.0 and r[1] >= 0.90]
    best = max(feas, key=lambda r: r[0]) if feas else None
    echo("\n阈值扫描（判定含 2of3 持续性 + 5s 冷却，与板上一致；单条口径）：")
    for th, rc, fh in sweep_rows:
        mark = ""
        if abs(th - args.threshold) < 1e-6:
            mark += "  <= 当前固件阈值"
        if best and abs(th - best[0]) < 1e-6:
            mark += "  <= 建议工作点"
        echo(f"  t={th:.2f}  检出={rc*100:5.1f}%  误报={fh:7.2f}/h{mark}")
    if best:
        echo(f"→ 建议阈值 {best[0]:.2f}（满足 误报<=1次/小时 且 检出>=90% 中最高者，"
             f"检出 {best[1]*100:.1f}% / 误报 {best[2]:.2f}次每小时）")
        if args.jitter:
            echo("   （这条只是**单条口径**：它只看词正好落在窗边界的那一种相位，板上不可复现；"
                 "工作点以下面「相位完备」扫描为准）")
    else:
        echo("→ 无可行阈值：在当前样本上找不到同时满足 检出>=90% 与 误报<=1次/小时 的工作点")

    if args.jitter:
        echo("\n阈值扫描（**相位完备**：每个阈值都在一个 hop 内的全部窗相位上判定，"
             "板上真实可达的就是这个口径）：")
        if zero_fp:
            bestp = max(zero_fp, key=lambda r: r[1])
        else:
            low = min(r[2] for r in phase_sweep)
            bestp = max([r for r in phase_sweep if abs(r[2] - low) < 1e-9], key=lambda r: r[1])
        for th, rc, fany, fph in phase_sweep:
            mark = ""
            if abs(th - args.threshold) < 1e-6:
                mark += "  <= 当前固件阈值"
            if abs(th - bestp[0]) < 1e-6:
                mark += "  <= 相位完备最优点"
            echo(f"  t={th:.2f}  期望检出={rc*100:5.1f}%  有误触的负样本比例={fany*100:5.1f}%  "
                 f"期望误触相位={fph*100:4.1f}%{mark}")
        echo(f"→ 相位完备最优点 t={bestp[0]:.2f}：期望检出 {bestp[1]*100:.1f}%，"
             f"有误触负样本比例 {bestp[2]*100:.1f}%")
        if not zero_fp:
            echo("  ✗ 结论：**0.05~0.95 没有任何阈值能把负样本相位误触降到 0**——"
                 "调阈值救不了，必须重训：把真实环境音、含「小麦」的近词串按多相位切片进负样本")
        elif not phase_feas:
            echo("  ✗ 零误触阈值存在，但其期望检出 < 90%：正样本需覆盖更多说话人/距离/语速，"
                 "或重做判定窗参数（hop / hist / 冷却）")
        else:
            echo("  ✓ 存在同时满足「相位零误触 + 期望检出>=90%」的工作点，可提交板上复测")

    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, f"detail_{args.tag}.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["file", "kind", "duration_s", "windows", "max_score",
                    "detections", "first_hit_s", "text"])
        for kind, tier, hitlist in (("positive", pos, hits_pos), ("negative", neg, hits_neg)):
            for (name, dur, scores), h in zip(tier, hitlist):
                w.writerow([name, kind, round(dur, 2), len(scores),
                            round(max(scores), 4) if scores else "", len(h),
                            round(h[0] * hop / SR, 2) if h else "",
                            texts.get(os.path.abspath(os.path.join(ROOT, name)), "")])
    with open(os.path.join(args.out, f"sweep_{args.tag}.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["threshold", "recall", "far_per_hour"])
        for th, rc, fh in sweep_rows:
            w.writerow([f"{th:.2f}", f"{rc:.3f}", f"{fh:.2f}"])
    if args.jitter:
        with open(os.path.join(args.out, f"sweep_phase_{args.tag}.csv"), "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(["threshold", "phase_recall", "neg_any_phase_hit_frac", "neg_mean_phase_hit_frac"])
            for th, rc, fany, fph in phase_sweep:
                w.writerow([f"{th:.2f}", f"{rc:.3f}", f"{fany:.3f}", f"{fph:.4f}"])
        with open(os.path.join(args.out, f"jitter_{args.tag}.csv"), "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(["kind", "file", "duration_s", "phase_hits", "phase_total"])
            w.writerows(jitter_rows)
    if args.concat:
        with open(os.path.join(args.out, f"concat_{args.tag}.csv"), "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(["stream", "hit_time_s", "hit_window", "source_file", "window_overlap"])
            for row in concat_rows:
                w.writerow([row[0], f"{row[1]:.2f}", row[2], row[3], row[4]])
    echo(f"\n报表: {os.path.abspath(args.out)}  明细: detail_{args.tag}.csv  判定: {verdict}")
    echo("==================================================")
    with open(os.path.join(args.out, f"report_{args.tag}.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return 0 if verdict == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
