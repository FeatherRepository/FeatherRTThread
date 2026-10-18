#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
kws_record.py -- PC 麦克风唤醒词样本采集工具

用途（Edgi-Talk 本地 AI 项目 / KWS 自测流程第一步）：
  用 PC 麦克风录制唤醒词正样本与负样本（随机语句/环境音），
  生成训练与验收清单 manifest.csv，供 kws_validate.py 验收使用。

采集规范（与嵌入式侧 MFCC 特征对齐，16kHz / mono / 16bit）：
  **整条原样保存、不截断**。板上是 1.0s 窗 / 0.5s 跳的连续滑窗，词在窗里的位置必须随机；
  截到 1.6s 会让"次/小时"和相位评测双双失真（10-18 验收新口径，见 local_ai/README.md 第五节）。
  正样本 4s（说完就停，别刻意从静音起头）；负样本除短句外还要有**长环境音**（--ambient）。

用法示例：
  # 正样本：唤醒词「小麦小麦」录 100 条（一次问一次说话人，多人多次用 --speaker 区分）
  python kws_record.py --wake-word "小麦小麦" --positive 100 --speaker A --out data/real

  # 负样本：随机语句/闲聊 300 条（含「小麦」的近词务必录进来）
  python kws_record.py --negative 300 --speaker A --out data/real

  # 长环境音：3 段 × 45s（做饭/电视/街噪…），这是"误报次/小时"唯一可信的素材
  python kws_record.py --ambient 45 --ambient-count 3 --out data/real

  # 查看录完的统计
  python kws_record.py --out data/real

依赖：sounddevice + soundfile + numpy
  注意：本机 **sounddevice 只在 `default` 这个 venv 里**（训练/验收用的 `kws` venv 没装），
  录制请用 default 解释器；两个 env 的绝对路径见 local_ai/README.md 第五节。
"""

import argparse
import csv
import os
import sys
import time

import numpy as np
import sounddevice as sd
import soundfile as sf

SAMPLE_RATE = 16000
CLIP_SEC = 4.0       # 正/负短句采集时长；整条保存，不截断（词在 1.0s 窗里的位置要随机）


def _dbg(msg):
    print(msg, flush=True)


def record_clip(seconds=CLIP_SEC):
    """录一段音频，原样返回（不裁剪、不补零）。"""
    audio = sd.rec(int(seconds * SAMPLE_RATE), samplerate=SAMPLE_RATE,
                   channels=1, dtype="int16")
    sd.wait()
    return audio.reshape(-1)


def speech_ratio(audio_int16):
    """粗估语音占比（能量中位数以上阈值），用于剔除空录。"""
    x = audio_int16.astype(np.float32) / 32768.0
    frame = 512
    n = len(x) // frame
    if n == 0:
        return 0.0
    rms = np.sqrt(np.mean(x[:n * frame].reshape(n, frame) ** 2, axis=1) + 1e-12)
    thr = max(1e-4, np.median(rms) * 1.5)
    return float(np.mean(rms > thr))


def save_clip(audio, out_dir, name):
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, name)
    sf.write(path, audio, SAMPLE_RATE, subtype="PCM_16")
    return path


def prompt_for_speaker():
    try:
        spk = input("  说话人标识（回车=unknown）: ").strip()
    except EOFError:
        return "unknown"
    return spk or "unknown"


def collect_positive(out_dir, wake_word, count, speaker):
    """正样本：反复念唤醒词。"""
    _dbg(f"\n=== 开始采集正样本：请清晰念出「{wake_word}」 ===")
    _dbg("提示：每条录像前有 3 秒准备与 1 秒倒计时，跟紧节奏。")
    _dbg("      念完就停，**不要刻意从静音起头**（板上窗位置随机，刻意对齐会让指标虚高）。")
    rows = []
    for i in range(1, count + 1):
        _dbg(f"[{i}/{count}] 准备...")
        time.sleep(3)
        for t in (3, 2, 1):
            _dbg(f"  {t}...")
            time.sleep(1)
        _dbg(f"  录音中（{CLIP_SEC}s），请念：{wake_word}")
        audio = record_clip()
        ratio = speech_ratio(audio)
        name = f"pos_{i:04d}_{speaker}.wav"
        path = save_clip(audio, out_dir, name)
        if ratio < 0.15:
            _dbg(f"  [警告] {name} 语音能量占比 {ratio:.2f} 偏低，可能录到静音，建议重录（删除该文件即可）")
        _dbg(f"  已保存 {path}（语音占比 {ratio:.2f}，{len(audio)/SAMPLE_RATE:.1f}s）")
        rows.append({"file": f"pos/{name}", "label": 1, "wake_word": wake_word,
                     "speaker": speaker, "speech_ratio": f"{ratio:.3f}",
                     "text": wake_word})
    return rows


def collect_negative(out_dir, count, speaker):
    """负样本：随机语句、闲聊、环境噪声、电视声等。"""
    _dbg("\n=== 开始采集负样本：请说随机的话 / 制造环境音 ===")
    _dbg("提示：尽量贴近真实使用场景——多人对话、电视背景声、不同距离、不同音量，")
    _dbg("      **务必念与唤醒词相近的音**（小麦 / 买小麦 / 小麦白 / 麦克小麦 / 你好小麦…）：")
    _dbg("      10-18 验收里的误触全部来自这类近词串。")
    rows = []
    for i in range(1, count + 1):
        _dbg(f"[{i}/{count}] 3 秒后录音...")
        time.sleep(3)
        _dbg(f"  录音中（{CLIP_SEC}s），说任意内容或保持环境音")
        audio = record_clip()
        name = f"neg_{i:04d}_{speaker}.wav"
        path = save_clip(audio, out_dir, name)
        rows.append({"file": f"neg/{name}", "label": 0, "wake_word": "",
                     "speaker": speaker, "speech_ratio": f"{speech_ratio(audio):.3f}",
                     "text": ""})
        _dbg(f"  已保存 {path}（{len(audio)/SAMPLE_RATE:.1f}s）")
    return rows


def collect_ambient(out_dir, seconds, count):
    """长环境音：整段不切。"误报 ≤1 次/小时"只能由长素材统计出来，短句是外推。"""
    _dbg(f"\n=== 长环境音采集：{count} 段 × {seconds}s（说话/电视/街噪都行，别刻意安静） ===")
    rows = []
    for i in range(1, count + 1):
        _dbg(f"[{i}/{count}] 5 秒后开始，请让环境自然运转…")
        time.sleep(5)
        _dbg(f"  录音中 {seconds}s")
        audio = record_clip(seconds)
        name = f"ambient_{i:04d}.wav"
        path = save_clip(audio, out_dir, name)
        rows.append({"file": f"neg/{name}", "label": 0, "wake_word": "",
                     "speaker": "ambient", "speech_ratio": f"{speech_ratio(audio):.3f}",
                     "text": ""})
        _dbg(f"  已保存 {path}（{len(audio)/SAMPLE_RATE:.1f}s）")
    return rows


FIELDS = ["file", "label", "wake_word", "speaker", "speech_ratio", "text"]


def write_manifest(path, new_rows):
    """合并写：R0-4 要分多次录制（正/负/环境音不同场次），按 file 去重覆盖、保留旧条目。"""
    merged = {}
    if os.path.exists(path):
        with open(path, newline="", encoding="utf-8") as f:
            for row in csv.DictReader(f):
                if row.get("file"):
                    merged[row["file"]] = {k: row.get(k, "") for k in FIELDS}
    for row in new_rows:
        merged[row["file"]] = {k: row.get(k, "") for k in FIELDS}
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS)
        w.writeheader()
        w.writerows(merged[k] for k in sorted(merged))
    return len(merged)


def main():
    ap = argparse.ArgumentParser(description="KWS 唤醒词样本采集工具")
    ap.add_argument("--wake-word", default="小麦小麦", help="唤醒词文本")
    ap.add_argument("--positive", type=int, default=0, help="正样本条数")
    ap.add_argument("--negative", type=int, default=0, help="负样本条数（短句/闲聊/近词）")
    ap.add_argument("--ambient", type=int, default=0, help="长环境音每段秒数（如 45）")
    ap.add_argument("--ambient-count", type=int, default=3, help="长环境音段数")
    ap.add_argument("--speaker", default=None,
                    help="说话人标识（仅正/负样本需要；不给则交互询问一次，纯环境音不询问）")
    ap.add_argument("--out", default="kws_data", help="输出根目录")
    args = ap.parse_args()

    if args.positive <= 0 and args.negative <= 0 and args.ambient <= 0:
        ap.error("--positive / --negative / --ambient 至少指定一个 > 0")

    speaker = args.speaker or (prompt_for_speaker()
                               if args.positive > 0 or args.negative > 0 else "ambient")
    rows = []
    if args.positive > 0:
        rows += collect_positive(os.path.join(args.out, "pos"),
                                 args.wake_word, args.positive, speaker)
    if args.negative > 0:
        rows += collect_negative(os.path.join(args.out, "neg"),
                                 args.negative, speaker)
    if args.ambient > 0:
        rows += collect_ambient(os.path.join(args.out, "neg"),
                                args.ambient, args.ambient_count)

    manifest = os.path.join(args.out, "manifest.csv")
    total = write_manifest(manifest, rows)
    _dbg(f"\n完成：本次 {len(rows)} 条，清单累计 {total} 条 -> {manifest}")
    if rows:
        n_pos = sum(1 for r in rows if r["label"] == 1)
        n_neg = len(rows) - n_pos
        _dbg(f"  正样本 {n_pos} 条 / 负样本 {n_neg} 条")
        _dbg(f"  下一步（板上同源验收，kws venv 解释器）：\n"
             f"    python tools/kws_validate.py --tag real --manifest {manifest} "
             f"--concat --jitter 10 --out <归档仓>/local_ai/reports/r0-5_<日期>")


if __name__ == "__main__":
    sys.exit(main())
