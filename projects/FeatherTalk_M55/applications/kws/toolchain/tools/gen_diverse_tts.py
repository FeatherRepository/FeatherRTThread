"""多音色/多语速/多音高 TTS 正样本生成器（OpenWakeWord 配方的关键：说话人多样性）。

OpenWakeWord 的经验：wakeword 模型的鲁棒性主要来自"同一句话、很多不同嗓音"。
Edge-TTS 有 8 个中文音色，再叠 rate/pitch 变化 → 几百条不同"说话人"的正样本。
配合 board_sim 的实测 IR 卷积，就能在不录真人的前提下把正样本多样性做够。

用法：python gen_diverse_tts.py --count 240 --out ../data/wakeword/xiaomai_v6
"""
import os
import asyncio
import time
import argparse
import tempfile
import numpy as np
import soundfile as sf

SR = 16000
VOICES = [
    "zh-CN-XiaoxiaoNeural", "zh-CN-YunxiNeural", "zh-CN-XiaoyiNeural",
    "zh-CN-YunjianNeural", "zh-CN-XiaohanNeural", "zh-CN-XiaomoNeural",
    "zh-CN-XiaoxuanNeural", "zh-CN-XiaoruiNeural",
]
RATES = ["-25%", "-15%", "-5%", "+0%", "+8%", "+18%", "+28%"]
PITCHES = ["-12Hz", "-6Hz", "+0Hz", "+6Hz", "+12Hz"]


def _to_wav_16k(mp3, wav):
    import librosa
    data, _ = librosa.load(mp3, sr=SR, mono=True)
    peak = np.abs(data).max()
    if peak > 0:
        data = data / peak * 0.9
    sf.write(wav, (data * 32767).astype(np.int16), SR, subtype="PCM_16")


async def _synth(text, voice, rate, pitch, dst):
    import edge_tts
    comm = edge_tts.Communicate(text, voice, rate=rate, pitch=pitch)
    await comm.save(dst)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wake-word", default="小麦小麦")
    ap.add_argument("--count", type=int, default=240)
    ap.add_argument("--out", required=True)
    ap.add_argument("--seed", type=int, default=20261023)
    a = ap.parse_args()

    os.makedirs(a.out, exist_ok=True)
    tmp = tempfile.mkdtemp()
    rng = np.random.default_rng(a.seed)
    n_ok = 0
    for i in range(a.count):
        v = VOICES[int(rng.integers(0, len(VOICES)))]
        r = RATES[int(rng.integers(0, len(RATES)))]
        p = PITCHES[int(rng.integers(0, len(PITCHES)))]
        name = "pos_%03d.wav" % i
        mp3 = os.path.join(tmp, name + ".mp3")
        ok_i = False
        for attempt in range(3):          # Edge-TTS 偶发 "No audio was received"（限流），重试
            try:
                asyncio.run(_synth(a.wake_word, v, r, p, mp3))
                _to_wav_16k(mp3, os.path.join(a.out, name))
                ok_i = True
                break
            except Exception as e:
                if attempt == 2:
                    print("  !! %s 失败: %s" % (name, e))
                else:
                    time.sleep(1.5)
        if ok_i:
            n_ok += 1
        if i % 40 == 0:
            print("  [%d/%d] %s %s %s" % (i, a.count, v, r, p))
    print("生成 %d/%d 条 → %s" % (n_ok, a.count, a.out))


if __name__ == "__main__":
    main()
