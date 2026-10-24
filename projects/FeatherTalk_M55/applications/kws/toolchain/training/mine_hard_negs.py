#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""mine_hard_negs.py — 用一级模型在负样本音频上挖"高分误触窗"，供 verifier 重点学。

原理（两级 verifier 的数据基础）：
  一级模型（宽召回）在负样本上打高分的那些 1s 窗，就是它真正的失效点。
  把这些窗原样存成 wav，verifier 训练时当负例重点学——直接在一级的决策边界
  失效处下功夫，而不是和几百条合成负样本混在一起被稀释。

用法：
  python mine_hard_negs.py --model out/kws_xiaomai_v31.tflite --thresh 0.30 --per-src 80
  python mine_hard_negs.py --model out/kws_xiaomai_v32a.tflite --thresh 0.25 --per-src 120

源清单（全部负样本，holdout_v11 除外——那是评测集，不进来）：
  wakeword/nearwords, nearword_sentences, xiaomai/neg_*, zh, en, reply, transient,
  real/ambient_*, boardmic/*/neg/*.wav（真实板载负样本）
"""
import os, glob, csv, argparse
import numpy as np
import tensorflow as tf

from ei_mfcc import ei_mfcc
from dataset_v9 import read_wav16k

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
DATA = os.path.join(ROOT, "data")
WIN = 16000
STRIDE = 4000          # 0.25s 滑步：误触窗定位准一点（一级 hop 是 500ms，取更密没错）


def sources():
    """(源名, 文件列表) —— 每个源单独配额，防止某一类（如 transient）霸榜。"""
    S = []
    W = os.path.join(DATA, "wakeword")
    S.append(("nearwords", sorted(glob.glob(os.path.join(W, "nearwords", "*.wav")))))
    S.append(("nearword_sentences", sorted(glob.glob(os.path.join(W, "nearword_sentences", "*.wav")))))
    S.append(("zh_sentence", sorted(glob.glob(os.path.join(W, "xiaomai", "neg_*.wav")))))
    S.append(("zh", sorted(glob.glob(os.path.join(W, "zh", "*.wav")))))
    S.append(("en", sorted(glob.glob(os.path.join(W, "en", "*.wav")))))
    S.append(("reply", sorted(glob.glob(os.path.join(DATA, "reply", "*.wav")))))
    S.append(("transient", sorted(glob.glob(os.path.join(DATA, "transient", "*.wav")))))
    S.append(("ambient", sorted(glob.glob(os.path.join(DATA, "real", "ambient_*.wav")))))
    S.append(("boardmic_neg", sorted(glob.glob(os.path.join(DATA, "boardmic", "*", "neg", "*.wav")))))
    return [(n, fs) for n, fs in S if fs]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", required=True, help="一级模型 tflite（如 out/kws_xiaomai_v31.tflite）")
    ap.add_argument("--thresh", type=float, default=0.30, help="保留窗的分数下限")
    ap.add_argument("--per-src", type=int, default=80, help="每个源最多保留多少窗（按分数降序）")
    ap.add_argument("--out", default=os.path.join(DATA, "hard_negs"), help="输出目录")
    ap.add_argument("--stride", type=int, default=STRIDE)
    a = ap.parse_args()

    interp = tf.lite.Interpreter(model_path=a.model)
    interp.allocate_tensors()
    ide = interp.get_input_details()[0]
    ode = interp.get_output_details()[0]
    iscale, izp = ide["quantization"][0], ide["quantization"][1]
    oscale, ozp = ode["quantization"][0], ode["quantization"][1]
    shape = ide["shape"]
    print("模型 %s  输入 shape=%s  int8(scale=%.3g, zp=%d)"
          % (a.model, shape, iscale, izp))

    os.makedirs(a.out, exist_ok=True)
    for old in glob.glob(os.path.join(a.out, "*.wav")):
        os.remove(old)                       # 全量重挖，不叠加

    rows = []
    for name, files in sources():
        found = []
        for p in files:
            try:
                x = read_wav16k(p)
            except Exception:
                continue
            if x.size < WIN:
                continue
            for s in range(0, x.size - WIN + 1, a.stride):
                f = np.asarray(ei_mfcc(x[s:s + WIN]), dtype=np.float32).reshape(1, -1)
                q = np.clip(np.round(f / iscale) + izp, -128, 127).astype(np.int8).reshape(shape)
                interp.set_tensor(ide["index"], q)
                interp.invoke()
                o = interp.get_tensor(ode["index"]).astype(np.float32).reshape(-1)
                sc = float(((o - ozp) * oscale)[1])     # P(xiaomai)
                if sc >= a.thresh:
                    found.append((sc, p, s))
        found.sort(key=lambda t: -t[0])
        kept = found[:a.per_src]
        for sc, p, s in kept:
            rows.append({"source": name, "src_file": os.path.relpath(p, ROOT),
                         "pos_sec": round(s / 16000.0, 2), "score": round(sc, 3)})
        print("  %-18s 文件 %3d → 高分窗 %4d → 保留 %3d（最高 %.2f）"
              % (name, len(files), len(found), len(kept), kept[0][0] if kept else 0))

    # 落盘：每条难负窗一个 wav（16000 样本，负例）
    import wave
    for i, r in enumerate(rows):
        x = read_wav16k(os.path.join(ROOT, r["src_file"]))
        s = int(round(r["pos_sec"] * 16000))
        w = x[s:s + WIN]
        if w.size < WIN:                      # 理论不会发生（扫描时已判过）
            continue
        fn = "hard_%04d.wav" % i
        pcm = w.astype("<i2")
        wf = wave.open(os.path.join(a.out, fn), "wb")
        wf.setnchannels(1); wf.setsampwidth(2); wf.setframerate(16000)
        wf.writeframes(pcm.tobytes()); wf.close()
        r["file"] = fn

    man = os.path.join(a.out, "manifest.csv")
    with open(man, "w", newline="", encoding="utf-8") as f:
        wcsv = csv.DictWriter(f, fieldnames=["file", "source", "src_file", "pos_sec", "score"])
        wcsv.writeheader()
        for r in rows:
            wcsv.writerow(r)
    print("难负样本挖掘完成：%d 条 → %s（阈值 %.2f）" % (len(rows), a.out, a.thresh))


if __name__ == "__main__":
    main()
