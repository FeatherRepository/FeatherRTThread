#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""gen_reply_corpus.py — 生成"设备应答语"负样本（reply 场景语料）

背景：v11.4 板测 reply 误触 97.3/h——设备自己的应答语（"我在/在呢/来啦"）和唤醒词
"小麦小麦"声学距离近，且产品里这些是设备喇叭真实会播的，必须专门拒。
Edge-TTS 间歇性限流，带重试。多音色提多样性。
"""
import os, asyncio, argparse, random, wave, tempfile
import edge_tts

VOICES = ["zh-CN-XiaoxiaoNeural", "zh-CN-YunxiNeural", "zh-CN-XiaoyiNeural",
          "zh-CN-YunjianNeural", "zh-CN-XiaohanNeural", "zh-CN-XiaomoNeural",
          "zh-CN-XiaoxuanNeural", "zh-CN-XiaoruiNeural"]
RATES = ["-15%", "-5%", "+0%", "+10%"]
PITCHES = ["-6Hz", "+0Hz", "+6Hz"]

TEXTS = [
    "我在", "在呢", "来啦", "来啦来啦", "好的", "好呀", "马上", "稍等",
    "收到", "听到啦", "在听的", "在呢在呢", "来喽", "嗯嗯", "明白",
    "你好呀", "你好", "我在我在", "在的", "说吧", "您说", "没问题",
]


def to_wav_16k(mp3, wav):
    """mp3 → 16k 单声道 16bit wav（与语料其余部分同规格）。"""
    import numpy as np, soundfile as sf
    x, sr = sf.read(mp3, dtype="float32", always_2d=False)
    if x.ndim > 1:
        x = x.mean(axis=1)
    if sr != 16000:
        import scipy.signal as ss
        g = 16000 / sr
        n = int(round(len(x) * g))
        x = ss.resample_poly(x, 16000, sr)[:n] if sr != 16000 else x[:n]
    pcm = np.clip(x * 32767, -32768, 32767).astype("<i2")
    w = wave.open(wav, "wb"); w.setnchannels(1); w.setsampwidth(2); w.setframerate(16000)
    w.writeframes(pcm.tobytes()); w.close()


async def _synth(text, voice, rate, pitch, mp3):
    for attempt in range(4):
        try:
            c = edge_tts.Communicate(text, voice, rate=rate, pitch=pitch)
            await c.save(mp3)
            if os.path.getsize(mp3) > 2000:
                return
        except Exception:
            await asyncio.sleep(1.5 * (attempt + 1))
    raise RuntimeError("edge-tts 重试 4 次仍失败: %s %s" % (text, voice))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="../data/reply")
    ap.add_argument("--count", type=int, default=40, help="总条数（在 23 文本 × 音色组合里抽样）")
    ap.add_argument("--seed", type=int, default=20241024)
    a = ap.parse_args()

    os.makedirs(a.out, exist_ok=True)
    existing = len([n for n in os.listdir(a.out) if n.startswith("neg_") and n.endswith(".wav")])
    rng = random.Random(a.seed)
    combos = []
    for t in TEXTS:
        for v in VOICES:
            combos.append((t, v, rng.choice(RATES), rng.choice(PITCHES)))
    rng.shuffle(combos)
    combos = combos[:a.count]

    ok = 0
    with tempfile.TemporaryDirectory() as tmp:
        for i, (t, v, r, p) in enumerate(combos):
            name = "neg_%04d.wav" % (existing + i)
            mp3 = os.path.join(tmp, name + ".mp3")
            try:
                asyncio.run(_synth(t, v, r, p, mp3))
                to_wav_16k(mp3, os.path.join(a.out, name))
                ok += 1
            except Exception as e:
                print("  !! %s 失败: %s" % (name, e))
                continue
            if i % 10 == 0:
                print("  [%d/%d] %s %s %s %s" % (i, len(combos), t, v, r, p))
    print("reply 语料生成完成：新增 %d 条 → %s（现存 %d 条）"
          % (ok, a.out, len([n for n in os.listdir(a.out) if n.startswith("neg_")])))


if __name__ == "__main__":
    main()
