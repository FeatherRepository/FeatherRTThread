#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
kws_validate.py -- KWS 唤醒词模型验收工具（PC 端）

用途（Edgi-Talk 本地 AI 项目 / KWS 自测流程第二步）：
  对训练好的唤醒词模型做量化验收——检出率（Recall）、
  误报率（FAR/hour）、最优阈值建议，输出逐条明细与汇总报表。

特征约定（与嵌入式侧对齐，改参数需同步改嵌入式代码）：
  16kHz / mono / 25ms 帧长 / 10ms 帧移 / 40 MFCC + log-energy

用法：
  # 无模型冒烟测试（合成音频，验证整条管线是否工作）
  python kws_validate.py --smoke

  # 真实验收（模型为 ONNX，正/负样本目录含 manifest.csv）
  python kws_validate.py --model kws_model.onnx ^
      --pos kws_data/pos --neg kws_data/neg --out report/

  # 也支持直接给 manifest.csv
  python kws_validate.py --model kws_model.onnx --manifest kws_data/manifest.csv

依赖：numpy + soundfile（+ onnxruntime，仅 --model 模式需要）
"""

import argparse
import csv
import glob
import os
import sys

import numpy as np
import soundfile as sf

# ---------------- 特征提取（MFCC，纯 numpy，无 librosa） ----------------

SR = 16000
FRAME_LEN = int(0.025 * SR)   # 25ms
FRAME_STEP = int(0.010 * SR)  # 10ms
N_MELS = 40
N_FFT = 512


def _mel_filterbank(sr=SR, n_fft=N_FFT, n_mels=N_MELS, fmin=20.0, fmax=7600.0):
    def hz2mel(f):
        return 2595.0 * np.log10(1.0 + f / 700.0)

    def mel2hz(m):
        return 700.0 * (10.0 ** (m / 2595.0) - 1.0)

    mels = np.linspace(hz2mel(fmin), hz2mel(fmax), n_mels + 2)
    hz = mel2hz(mels)
    bins = np.floor((n_fft + 1) * hz / sr).astype(int)
    fb = np.zeros((n_mels, n_fft // 2 + 1))
    for i in range(n_mels):
        a, b, c = bins[i], bins[i + 1], bins[i + 2]
        for k in range(a, b):
            fb[i, k] = (k - a) / max(1, (b - a))
        for k in range(b, c):
            fb[i, k] = (c - k) / max(1, (c - b))
    return fb


_MEL_FB = _mel_filterbank()
_DCT = np.cos(np.pi / N_MELS *
              (np.arange(N_MELS) + 0.5)[:, None] *
              np.arange(N_MELS)[None, :])


def extract_mfcc(audio, sr=SR, num_frames=None):
    """音频(int16 已转 float) -> [T, 40] MFCC（含 log-energy 于第 0 维）。"""
    if sr != SR:
        raise ValueError(f"只支持 {SR}Hz，收到 {sr}")
    x = audio.astype(np.float32) / 32768.0
    if len(x) < FRAME_LEN:
        x = np.pad(x, (0, FRAME_LEN - len(x)))
    num = (len(x) - FRAME_LEN) // FRAME_STEP + 1
    idx = np.arange(FRAME_LEN)[None, :] + \
        np.arange(num)[:, None] * FRAME_STEP
    frames = x[idx] * np.hamming(FRAME_LEN)
    spec = np.abs(np.fft.rfft(frames, n=N_FFT, axis=1))
    mel = spec @ _MEL_FB.T
    logmel = np.log(mel + 1e-6)
    mfcc = logmel @ _DCT.T
    energy = np.log(np.sum(frames ** 2, axis=1) + 1e-12)[:, None]
    feats = np.concatenate([energy, mfcc], axis=1).astype(np.float32)
    if num_frames is not None:
        if len(feats) >= num_frames:
            feats = feats[:num_frames]
        else:
            feats = np.pad(feats, ((0, num_frames - len(feats)), (0, 0)))
    return feats


# ---------------- 模型推理抽象 ----------------

class OnnxScorer:
    """把 ONNX 模型包成 滑窗打分器。输入约定 batch=1。"""

    def __init__(self, path):
        import onnxruntime as ort
        self.sess = ort.InferenceSession(path, providers=["CPUExecutionProvider"])
        self.inp = self.sess.get_inputs()[0]
        self.in_name = self.inp.name
        shape = self.inp.shape
        # 期望 [N, T, F] 或 [N, T*F]
        self.expect_flat = len(shape) == 2
        self.num_frames = None
        for s in shape:
            if isinstance(s, int) and s > 1:
                self.num_frames = s if self.expect_flat else s

    def score(self, feats):
        """feats: [T, F] -> float"""
        x = feats[None, ...]
        if self.expect_flat:
            x = x.reshape(1, -1)
        out = self.sess.run(None, {self.in_name: x})[0]
        return float(np.ravel(out)[0])


class EnergySmokeScorer:
    """冒烟打分器：无真实模型时，用 MFCC 能量差模拟『正样本得分更高』。"""

    def score(self, feats):
        e = feats[:, 0]
        speech = float(np.mean(e > np.median(e) - 0.5))
        return speech * (1.0 + float(e.std()))


# ---------------- 验收逻辑 ----------------

def load_wav(path):
    audio, sr = sf.read(path, dtype="int16", always_2d=False)
    if audio.ndim > 1:
        audio = audio.mean(axis=1).astype(np.int16)
    return audio, sr


def evaluate(pos_files, neg_files, scorer, num_frames,
             window_sec, hop_sec, far_duration_sec, out_dir):
    win = int(window_sec * SR)
    hop = int(hop_sec * SR)

    def slide_scores(paths):
        all_scores, detail = [], []
        for p in paths:
            audio, sr = load_wav(p)
            if sr != SR:
                raise ValueError(f"{p} 采样率 {sr} != {SR}")
            best = -1e9
            n_win = 0
            for start in range(0, max(1, len(audio) - win + 1), hop):
                seg = audio[start:start + win]
                feats = extract_mfcc(seg, num_frames=num_frames)
                s = scorer.score(feats)
                best = max(best, s)
                n_win += 1
            all_scores.append(best)
            detail.append((os.path.basename(p), best, n_win))
        return np.array(all_scores), detail

    pos_scores, pos_detail = slide_scores(pos_files)
    neg_scores, neg_detail = slide_scores(neg_files)

    # 阈值扫描
    lo, hi = min(pos_scores.min(), neg_scores.min()), \
        max(pos_scores.max(), neg_scores.max())
    grid = np.linspace(lo, hi, 201)
    rows = []
    for th in grid:
        tp = int((pos_scores >= th).sum())
        fp = int((neg_scores >= th).sum())
        rec = tp / max(1, len(pos_scores))
        far_per_h = fp / max(1e-9, far_duration_sec / 3600.0)
        prec = tp / max(1, tp + fp)
        f1 = 2 * prec * rec / max(1e-9, prec + rec)
        rows.append((float(th), rec, far_per_h, prec, f1))
    rows.sort(key=lambda r: -r[4])
    best_th, best_rec, best_far, best_prec, best_f1 = rows[0]

    # FAR <= 1/h 的最高检出率阈值
    ok = [r for r in rows if r[2] <= 1.0]
    if ok:
        ok.sort(key=lambda r: -r[1])
        th_strict, rec_strict, far_strict = ok[0][0], ok[0][1], ok[0][2]
    else:
        th_strict, rec_strict, far_strict = best_th, best_rec, best_far

    # 报表
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "detail.csv"), "w", newline="",
              encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["file", "score", "windows", "kind"])
        for n, s, c in pos_detail:
            w.writerow([n, f"{s:.4f}", c, "positive"])
        for n, s, c in neg_detail:
            w.writerow([n, f"{s:.4f}", c, "negative"])

    total_neg_h = far_duration_sec / 3600.0
    print("\n================ KWS 验收报表 ================")
    print(f"正样本: {len(pos_scores)} 条   负样本: {len(neg_scores)} 条 "
          f"(约 {total_neg_h:.2f} 小时音频)")
    print(f"\n[阈值-最佳 F1]  t={best_th:.4f}")
    print(f"  检出率={best_rec * 100:.1f}%  误报={best_far:.2f} 次/小时  "
          f"精确率={best_prec * 100:.1f}%  F1={best_f1:.3f}")
    print(f"\n[阈值-误报约束 <=1次/小时]  t={th_strict:.4f}")
    print(f"  检出率={rec_strict * 100:.1f}%  误报={far_strict:.2f} 次/小时")
    print("\n建议判据（合格线）：")
    print(f"  - 检出率 >= 90%  @  误报 <= 1 次/小时  -> "
          f"{'PASS' if rec_strict >= 0.9 and far_strict <= 1.0 else 'FAIL'}")
    print(f"  - 明细: {os.path.join(out_dir, 'detail.csv')}")
    print("=============================================")
    return best_th, rec_strict, far_strict


def make_smoke_data(neg_count=12):
    """合成冒烟音频：正=带静音段的双音，负=噪声。"""
    rng = np.random.default_rng(42)
    pos, neg = [], []
    t = np.arange(int(1.6 * SR)) / SR
    for _ in range(6):
        seg = (12000 * np.sin(2 * np.pi * 440 * t) *
               (t > 0.4) * (t < 1.2)).astype(np.int16)
        pos.append(seg)
    for _ in range(neg_count):
        neg.append((rng.standard_normal(int(2.0 * SR)) * 1500).astype(np.int16))
    return pos, neg


def main():
    ap = argparse.ArgumentParser(description="KWS 模型验收工具")
    ap.add_argument("--model", help="ONNX 模型文件（缺省=冒烟模式）")
    ap.add_argument("--smoke", action="store_true", help="合成音频冒烟测试")
    ap.add_argument("--pos", help="正样本目录（*.wav）")
    ap.add_argument("--neg", help="负样本目录（*.wav）")
    ap.add_argument("--manifest", help="manifest.csv（file,label 列）")
    ap.add_argument("--out", default="kws_report", help="报表输出目录")
    ap.add_argument("--window-sec", type=float, default=1.6)
    ap.add_argument("--hop-sec", type=float, default=0.4)
    ap.add_argument("--num-frames", type=int, default=None,
                    help="模型要求的帧数（缺省=不裁剪）")
    args = ap.parse_args()

    scorer = OnnxScorer(args.model) if args.model else EnergySmokeScorer()
    if args.smoke or not (args.pos or args.neg or args.manifest):
        print("[冒烟模式] 无模型/样本，使用合成音频验证管线...")
        pos_audio, neg_audio = make_smoke_data()
        tmp = os.path.join(args.out, "_smoke_wav")
        os.makedirs(tmp, exist_ok=True)
        pos_files = [sf.write(os.path.join(tmp, f"s_pos_{i}.wav"), a, SR)
                     or os.path.join(tmp, f"s_pos_{i}.wav")
                     for i, a in enumerate(pos_audio)]
        neg_files = [sf.write(os.path.join(tmp, f"s_neg_{i}.wav"), a, SR)
                     or os.path.join(tmp, f"s_neg_{i}.wav")
                     for i, a in enumerate(neg_audio)]
        pos_dur = len(pos_audio) * 1.6
        neg_dur = len(neg_audio) * 2.0
    else:
        pos_files = sorted(glob.glob(os.path.join(args.pos, "*.wav"))) \
            if args.pos else []
        neg_files = sorted(glob.glob(os.path.join(args.neg, "*.wav"))) \
            if args.neg else []
        if args.manifest:
            with open(args.manifest, encoding="utf-8") as f:
                for row in csv.DictReader(f):
                    p = row["file"]
                    if not os.path.isabs(p):
                        p = os.path.join(os.path.dirname(args.manifest), p)
                    (pos_files if row["label"] == "1" else neg_files).append(p)
        if not pos_files or not neg_files:
            ap.error("正/负样本为空，无法验收")
        pos_dur = len(pos_files) * args.window_sec
        neg_dur = len(neg_files) * 2.0

    evaluate(pos_files, neg_files, scorer, args.num_frames,
             args.window_sec, args.hop_sec, neg_dur, args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
