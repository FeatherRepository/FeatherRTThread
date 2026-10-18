#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
kws_record.py -- PC 麦克风唤醒词样本采集工具

用途（Edgi-Talk 本地 AI 项目 / KWS 自测流程第一步）：
  用 PC 麦克风录制唤醒词正样本与负样本（随机语句/环境音），
  生成训练与验收清单 manifest.csv，供 kws_validate.py 验收使用。

采集规范（与嵌入式侧 MFCC 特征对齐，16kHz / mono / 16bit / 每条 1.6s）：

用法示例：
  # 正样本：唤醒词 "你好小智" 录 30 条
  python kws_record.py --wake-word "你好小智" --positive 30 --out data/pos

  # 负样本：随机中文语句/环境音 200 条（建议多人、多环境、远近距离混合）
  python kws_record.py --negative 200 --out data/neg

  # 查看录完的统计
  python kws_record.py --out data

依赖：sounddevice + soundfile + numpy
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
CLIP_SEC = 1.6
CLIP_SAMPLES = int(SAMPLE_RATE * CLIP_SEC)
MAX_TAKE_SEC = 4.0  # 每条最大采集时长（留裁剪余量）


def _dbg(msg):
    print(msg, flush=True)


def record_clip(seconds=MAX_TAKE_SEC):
    """录一段音频，裁剪/补零到固定长度。"""
    audio = sd.rec(int(seconds * SAMPLE_RATE), samplerate=SAMPLE_RATE,
                   channels=1, dtype="int16")
    sd.wait()
    audio = audio.reshape(-1)
    if len(audio) > CLIP_SAMPLES:
        audio = audio[:CLIP_SAMPLES]
    elif len(audio) < CLIP_SAMPLES:
        audio = np.pad(audio, (0, CLIP_SAMPLES - len(audio)))
    return audio


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
    spk = input("  说话人标识（回车=unknown）: ").strip()
    return spk or "unknown"


def collect_positive(out_dir, wake_word, count):
    """正样本：反复念唤醒词。"""
    _dbg(f"\n=== 开始采集正样本：请清晰念出「{wake_word}」 ===")
    _dbg("提示：每条录像前有 3 秒准备与 1 秒倒计时，跟紧节奏。")
    rows = []
    for i in range(1, count + 1):
        speaker = prompt_for_speaker()
        _dbg(f"[{i}/{count}] 准备...")
        time.sleep(3)
        for t in (3, 2, 1):
            _dbg(f"  {t}...")
            time.sleep(1)
        _dbg(f"  录音中（{MAX_TAKE_SEC}s），请念：{wake_word}")
        audio = record_clip()
        ratio = speech_ratio(audio)
        name = f"pos_{i:04d}_{speaker}.wav"
        path = save_clip(audio, out_dir, name)
        if ratio < 0.15:
            _dbg(f"  [警告] {name} 语音能量占比 {ratio:.2f} 偏低，可能录到静音，建议重录（删除该文件即可）")
        rows.append({"file": name, "label": 1, "wake_word": wake_word,
                     "speaker": speaker, "speech_ratio": f"{ratio:.3f}"})
        _dbg(f"  已保存 {path}（语音占比 {ratio:.2f}）")
    return rows


def collect_negative(out_dir, count):
    """负样本：随机语句、闲聊、环境噪声、电视声等。"""
    _dbg("\n=== 开始采集负样本：请说随机的话 / 制造环境音 ===")
    _dbg("提示：尽量贴近真实使用场景——多人对话、电视背景声、不同距离、不同音量，")
    _dbg("      也可故意念与唤醒词相近的音（如「小智」「你好聪明」）来压误报。")
    rows = []
    for i in range(1, count + 1):
        speaker = prompt_for_speaker()
        _dbg(f"[{i}/{count}] 3 秒后录音...")
        time.sleep(3)
        _dbg(f"  录音中（{MAX_TAKE_SEC}s），说任意内容或保持环境音")
        audio = record_clip()
        name = f"neg_{i:04d}_{speaker}.wav"
        path = save_clip(audio, out_dir, name)
        rows.append({"file": name, "label": 0, "wake_word": "",
                     "speaker": speaker, "speech_ratio": "-"})
        _dbg(f"  已保存 {path}")
    return rows


def main():
    ap = argparse.ArgumentParser(description="KWS 唤醒词样本采集工具")
    ap.add_argument("--wake-word", default="你好小智", help="唤醒词文本")
    ap.add_argument("--positive", type=int, default=0, help="正样本条数")
    ap.add_argument("--negative", type=int, default=0, help="负样本条数")
    ap.add_argument("--out", default="kws_data", help="输出根目录")
    args = ap.parse_args()

    if args.positive <= 0 and args.negative <= 0:
        ap.error("--positive 或 --negative 至少指定一个 > 0")

    rows = []
    if args.positive > 0:
        rows += collect_positive(os.path.join(args.out, "pos"),
                                 args.wake_word, args.positive)
    if args.negative > 0:
        rows += collect_negative(os.path.join(args.out, "neg"),
                                 args.negative)

    manifest = os.path.join(args.out, "manifest.csv")
    os.makedirs(args.out, exist_ok=True)
    with open(manifest, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=["file", "label", "wake_word",
                                          "speaker", "speech_ratio"])
        w.writeheader()
        w.writerows(rows)
    _dbg(f"\n完成：共 {len(rows)} 条 -> {manifest}")
    if rows:
        n_pos = sum(1 for r in rows if r["label"] == 1)
        n_neg = len(rows) - n_pos
        _dbg(f"  正样本 {n_pos} 条 / 负样本 {n_neg} 条")
        _dbg(f"  下一步：python kws_validate.py --model <your.onnx> "
             f"--manifest {manifest}")


if __name__ == "__main__":
    sys.exit(main())
