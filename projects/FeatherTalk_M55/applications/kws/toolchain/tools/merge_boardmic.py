"""merge_boardmic.py — 把多轮 rig traindata 跑批合并成一份语料，供 dataset_v11 使用

为什么需要它：dataset_v11.new_corpus() 缺省只取 data/boardmic 下**最新一次** manifest，
而 v11 的语料要多轮积累（每轮 8s 一条，一次跑批 ~58 条）。合并时统一做三件事：
  ① 跳过 fail/ 目录的段与 dose_ok=False 的段（剂量门没过＝激励没进去/没认回来，
     不是"没有声学耦合"就是没能用，两种都不能进训练）；
  ② 老格式 manifest（没有 dose_ok 列）按 rig 自己的门限补判（corr>=0.30 且 lift>=1.25）；
  ③ 复制 wav 时加轮次前缀，避免不同轮的同名文件互相覆盖（三个语料目录都用
     pos_0000.wav 这种名字，按文件名合并必然串味——kws_record 已经踩过一次）。

用法：
  python merge_boardmic.py                     # 合并 data/boardmic/* 全部轮次
  python merge_boardmic.py 20261019_123423 ...  # 只合并指定轮次
产物：data/boardmic_v11/{pos,neg}/... + manifest.csv（dataset_v11 用 KWS_CORPUS 指进来）
"""
import csv
import glob
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.abspath(os.path.join(HERE, "..", "data"))
SRC_ROOT = os.path.join(DATA, "boardmic")
DST = os.path.join(DATA, "boardmic_v11")

CORR_MIN = 0.30
LIFT_MIN = 1.25


def row_gate(row):
    """返回 (ok, why)。dose_ok 列存在时以它为准；否则用 rig 的剂量门限补判。

    无激励的负样本（room 空场：clip 为空、reps=0、corr=0）必须放行——剂量门的作用是
    验证"激励确实进了麦"，没有激励就无从验证，而空场底噪正是误触治理最需要的负样本。
    老格式 manifest 没有 dose_ok 列时若按 corr>=0.30 判，会把 12 段空场全判出局。
    """
    f = (row.get("file") or "")
    if f.replace("\\", "/").startswith("fail/"):
        return False, "fail-dir"
    d = row.get("dose_ok")
    if d is not None and d != "":
        return (str(d).strip().lower() in ("1", "true", "yes")), "dose_ok"
    try:
        reps = int(row.get("reps") or 0)
        clip = (row.get("clip") or "").strip()
    except ValueError:
        return False, "unparseable"
    if not clip and reps == 0:
        return True, "no-stimulus-neg"      # 空场负样本：无激励可验，直接收
    try:
        corr = float(row.get("corr") or 0)
        lift = float(row.get("lift") or 0)
    except ValueError:
        return False, "unparseable"
    if reps <= 0:
        return False, "no-dose"
    return (corr >= CORR_MIN and lift >= LIFT_MIN), "corr/lift"


def main():
    args = sys.argv[1:]
    if args:
        runs = [os.path.join(SRC_ROOT, a) for a in args]
    else:
        runs = sorted(glob.glob(os.path.join(SRC_ROOT, "*")))
    runs = [r for r in runs if os.path.exists(os.path.join(r, "manifest.csv"))]
    if not runs:
        print("没有可合并的轮次")
        return 1

    if os.path.isdir(DST):
        for old in glob.glob(os.path.join(DST, "*.wav")) + [os.path.join(DST, "manifest.csv")]:
            os.remove(old)
    rows = []
    stats = {"kept": 0, "skip": 0}
    for run in runs:
        stamp = os.path.basename(run)
        man = os.path.join(run, "manifest.csv")
        with open(man, encoding="utf-8-sig", newline="") as f:
            for row in csv.DictReader(f):
                ok, why = row_gate(row)
                if not ok:
                    stats["skip"] += 1
                    continue
                src = os.path.join(run, row["file"].replace("\\", "/"))
                if not os.path.exists(src):
                    stats["skip"] += 1
                    continue
                lab = int(row["label"])
                sub = "pos" if lab == 1 else "neg"
                os.makedirs(os.path.join(DST, sub), exist_ok=True)
                # 轮次前缀：stamp__scenario__orig —— 唯一且可溯源
                fn = "%s__%s__%s" % (stamp, row.get("scenario", "?"),
                                     os.path.basename(row["file"]))
                dst = os.path.join(DST, sub, fn)
                shutil.copyfile(src, dst)
                row = dict(row)
                row["file"] = "%s/%s" % (sub, fn)
                rows.append(row)
                stats["kept"] += 1

    cols = ["file", "label", "scenario", "clip", "text", "sec", "reps", "lag_sec",
            "clip_sec", "loop_gap", "push_gain", "corr", "lift", "floor_p90",
            "hole_runs", "hole_ms", "hole_spans", "peak_ok", "ck_ok", "saturation",
            "drop_heads", "dose_ok"]
    seen = set()
    cols = [c for c in cols if not (c in seen or seen.add(c))]
    cols = [c for c in cols if any(c in r for r in rows)]
    with open(os.path.join(DST, "manifest.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=cols, extrasaction="ignore")
        w.writeheader()
        for r in rows:
            w.writerow(r)

    npos = sum(1 for r in rows if int(r["label"]) == 1)
    nneg = len(rows) - npos
    sec = sum(float(r.get("sec") or 0) for r in rows)
    print("合并 %d 轮 → %s：保留 %d 段（正 %d / 负 %d）跳过 %d 段，总时长 %.1fs"
          % (len(runs), DST, stats["kept"], npos, nneg, stats["skip"], sec))
    print("正样本放音总次数: %d" % sum(int(r.get("reps") or 0) for r in rows if int(r["label"]) == 1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
