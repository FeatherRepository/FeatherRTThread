#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""make_merged_corpus.py — 把多次 rig 采集合并成一个训练语料 manifest。

背景（10-22 排查）：`dataset_v11.newest_corpus()` 只取 data/boardmic 下**最新一次**采集，
而最新一次（20261021_024142）只有 16 条**近词负样本**、零正样本 —— 于是 v11 的
"机内声学语料"其实一条 rig 正样本都没用上，正样本全靠 TTS + 7 条 real/wake_*.wav。
真正的小麦小麦-era 采集散落在 4 个目录里（共 ~102 文件、~18 正样本含 6 条 wake_real），
一直没被训练用到。本脚本把它们合起来。

用法：
  python make_merged_corpus.py --out data/boardmic_merged_v13 \
      --include 20261020_215243 20261021_012058 20261021_024142
  # 留出（不进训练）：
  python make_merged_corpus.py --out data/boardmic_holdout_v13 \
      --include 20261020_230020

产出 <out>/manifest.csv，其中 file 列写成 `../<stamp>/<原file>`（相对 out 目录），
这样 dataset_v11.load_corpus() 用 os.path.join(base, file) 即可读到原文件，无需复制 wav。
"""
import argparse, csv, os, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BM = os.path.join(ROOT, "data", "boardmic")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True, help="输出目录（相对 toolchain 或绝对）")
    ap.add_argument("--include", nargs="+", required=True, help="要合并的采集目录名（stamp）")
    args = ap.parse_args()

    out = args.out if os.path.isabs(args.out) else os.path.join(ROOT, args.out)
    os.makedirs(out, exist_ok=True)

    rows, fields = [], []
    for stamp in args.include:
        man = os.path.join(BM, stamp, "manifest.csv")
        if not os.path.exists(man):
            print("跳过（无 manifest）：", stamp)
            continue
        with open(man, encoding="utf-8-sig", newline="") as f:
            rd = csv.DictReader(f)
            for c in (rd.fieldnames or []):
                if c not in fields:
                    fields.append(c)          # 各次采集列不完全一致（如 dose_ok），取并集
            for r in rd:
                r = dict(r)
                r["file"] = "../boardmic/%s/%s" % (stamp, r["file"].replace("\\", "/"))
                rows.append(r)
        print("合并 %s：%d 行" % (stamp, sum(1 for _ in open(man, encoding='utf-8-sig')) - 1))

    dst = os.path.join(out, "manifest.csv")
    with open(dst, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fields, restval="", extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)

    npos = sum(1 for r in rows if r.get("label") == "1")
    print("→ %s  共 %d 行，正 %d / 负 %d" % (dst, len(rows), npos, len(rows) - npos))


if __name__ == "__main__":
    main()
