"""生成用于校验的『小睿』TTS 音频（16k mono wav），供 validate_mfcc.py 使用。"""
import asyncio, os, sys
import numpy as np
import soundfile as sf
from scipy.signal import resample_poly
import edge_tts

OUT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "data", "wakeword"))


async def synth(text, voice, dst):
    comm = edge_tts.Communicate(text, voice)
    await comm.save(dst)


def to_16k_mono(path_mp3, path_wav):
    data, sr = sf.read(path_mp3, dtype="float32", always_2d=True)
    data = data.mean(axis=1)  # mono
    if sr != 16000:
        from math import gcd
        g = gcd(int(sr), 16000)
        data = resample_poly(data, 16000 // g, int(sr) // g)
    pcm = np.clip(data * 32767.0, -32768, 32767).astype(np.int16)
    sf.write(path_wav, pcm, 16000, subtype="PCM_16")
    return len(pcm) / 16000.0


def main():
    cands = [("小睿", "zh-CN-XiaoxiaoNeural"), ("小睿小睿", "zh-CN-YunxiNeural")]
    for i, (text, voice) in enumerate(cands):
        mp3 = os.path.join(OUT, f"_tmp_xiaorui_{i}.mp3")
        wav = os.path.join(OUT, f"xiaorui_test_{i}.wav")
        asyncio.run(synth(text, voice, mp3))
        dur = to_16k_mono(mp3, wav)
        print(f"生成 {wav}  ({text}, {voice}, {dur:.2f}s)")


if __name__ == "__main__":
    main()
