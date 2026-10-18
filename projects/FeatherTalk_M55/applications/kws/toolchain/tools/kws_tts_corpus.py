#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
kws_tts_corpus.py -- 唤醒词测试语料自动生成器（TTS 合成，无需真人录音）

用途：
  在拿到真人录音之前，用 TTS 快速生成唤醒词测试语料，
  用于跑通/验证「特征提取 -> 模型推理 -> 验收报表」全链路。
  ⚠️ 局限：TTS 语音是"干净合成音"，与真实麦克风环境差异大，
     结果只能作为**管线正确性验证**，不能替代真人录音的准确率验收。

用法：
  # 生成中文唤醒词正样本（多语速/多音色，模拟说话人差异）
  python kws_tts_corpus.py --wake-word "你好小智" --pos-count 20 ^
      --voices zh-CN-XiaoxiaoNeural,zh-CN-YunxiNeural --out tts_corpus/pos

  # 生成负样本（随机中文语句，含近音干扰词）
  python kws_tts_corpus.py --neg-file neg_sentences.txt --out tts_corpus/neg

  # 英文（用于验证 openWakeWord 预训练模型）
  python kws_tts_corpus.py --wake-word "Hey Jarvis" --pos-count 10 ^
      --voices en-US-JennyNeural --lang en --out tts_corpus/pos_en

输出：16kHz / mono / 16bit WAV（与嵌入式 KWS 特征约定一致）+ manifest.csv
依赖：edge-tts（在线合成，免密钥）、soundfile、numpy
"""

import argparse
import asyncio
import csv
import os
import sys

import numpy as np
import soundfile as sf

SR = 16000

# 预设负样本语句（中文）：日常语句 + 与唤醒词近音干扰
NEG_ZH = [
    "今天天气怎么样", "帮我查一下明天的日程", "这个功能怎么用啊",
    "你好聪明啊", "小智障又出问题了", "你好好想想再说",
    "把音量调小一点", "刚才那句话是什么意思", "我们一起去看电影吧",
    "这个项目的进度怎么样了", "冰箱里还有什么吃的", "帮我把灯关掉",
    "最近有什么好看的剧", "他说的好像不太对", "我明天要去出差",
    "记得提醒我买牛奶", "这个价格有点贵", "先把方案发我看看",
    "外面下雨了吗", "孩子放学了没",
]

NEG_EN = [
    "what is the weather today", "turn the volume down please",
    "hey there how are you", "that sounds like a great idea",
    "remind me to buy some milk", "the project is almost finished",
    "can you check my calendar", "let me think about it again",
    "this is a very long sentence for testing purposes",
    "open the door and close the window",
]


def _resample(x, sr_in, sr_out=SR):
    if sr_in == sr_out:
        return x
    n_out = int(round(len(x) * sr_out / sr_in))
    idx = np.linspace(0, len(x) - 1, n_out)
    return np.interp(idx, np.arange(len(x)), x)


def _to_wav_16k(src_path, dst_path):
    """mp3/wav -> 16kHz mono PCM16 wav。"""
    data, sr = sf.read(src_path, dtype="float32", always_2d=False)
    if data.ndim > 1:
        data = data.mean(axis=1)
    data = _resample(data, sr)
    peak = float(np.max(np.abs(data))) or 1.0
    data = data / peak * 0.95
    sf.write(dst_path, (data * 32767).astype(np.int16), SR, subtype="PCM_16")


async def _synth(text, voice, rate, dst_mp3):
    import edge_tts
    comm = edge_tts.Communicate(text, voice, rate=rate)
    await comm.save(dst_mp3)


def synth_one(text, voice, rate, tmp_dir, out_dir, name):
    mp3 = os.path.join(tmp_dir, name + ".mp3")
    wav = os.path.join(out_dir, name + ".wav")
    asyncio.run(_synth(text, voice, rate, mp3))
    _to_wav_16k(mp3, wav)
    return wav


def main():
    ap = argparse.ArgumentParser(description="KWS TTS 测试语料生成器")
    ap.add_argument("--wake-word", default="你好小智")
    ap.add_argument("--pos-count", type=int, default=10)
    ap.add_argument("--voices", default="zh-CN-XiaoxiaoNeural,zh-CN-YunxiNeural")
    ap.add_argument("--lang", default="zh", choices=["zh", "en"])
    ap.add_argument("--neg-file", help="负样本语句文件（每行一句）")
    ap.add_argument("--out", required=True, help="输出目录")
    args = ap.parse_args()

    tmp_dir = os.path.join(args.out, "_tmp")
    os.makedirs(args.out, exist_ok=True)
    os.makedirs(tmp_dir, exist_ok=True)

    voices = [v.strip() for v in args.voices.split(",") if v.strip()]
    rates = ["+0%", "-10%", "+10%"]  # 多语速，模拟不同说话节奏
    rows = []

    # 正样本
    for i in range(args.pos_count):
        voice = voices[i % len(voices)]
        rate = rates[i % len(rates)]
        name = f"pos_{i:04d}"
        try:
            path = synth_one(args.wake_word, voice, rate, tmp_dir,
                             args.out, name)
            rows.append({"file": name + ".wav", "label": 1,
                         "text": args.wake_word, "voice": voice, "rate": rate})
            print(f"[pos {i + 1}/{args.pos_count}] {name} ({voice} {rate})")
        except Exception as e:
            print(f"[pos {i + 1}] 失败: {type(e).__name__}: {e}")

    # 负样本
    neg_list = []
    if args.neg_file:
        with open(args.neg_file, encoding="utf-8") as f:
            neg_list = [ln.strip() for ln in f if ln.strip()]
    else:
        neg_list = NEG_ZH if args.lang == "zh" else NEG_EN

    for i, sent in enumerate(neg_list):
        voice = voices[i % len(voices)]
        name = f"neg_{i:04d}"
        try:
            path = synth_one(sent, voice, "+0%", tmp_dir, args.out, name)
            rows.append({"file": name + ".wav", "label": 0, "text": sent,
                         "voice": voice, "rate": "+0%"})
            print(f"[neg {i + 1}/{len(neg_list)}] {name} <- {sent[:20]}")
        except Exception as e:
            print(f"[neg {i + 1}] 失败: {type(e).__name__}: {e}")

    manifest = os.path.join(args.out, "manifest.csv")
    with open(manifest, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=["file", "label", "text",
                                          "voice", "rate"])
        w.writeheader()
        w.writerows(rows)
    n_pos = sum(1 for r in rows if r["label"] == 1)
    print(f"\n完成：正 {n_pos} / 负 {len(rows) - n_pos} -> {manifest}")


if __name__ == "__main__":
    sys.exit(main())
