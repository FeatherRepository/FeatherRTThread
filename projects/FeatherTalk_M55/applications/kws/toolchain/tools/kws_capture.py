"""kws_capture.py — 抓板子麦克风真实音频为 WAV（免 SD，自主采集）

两种模式：
  capture（默认）= 板上先把连续音频录进 RAM，录满后再按帧回吐串口 ⇒ **不丢音频**。
      15s = 480KB 堆，回吐耗时 ≈ 帧数×641B ÷ 11520B/s。
  dump = 老的实时流：16kHz mono int16 = 32KB/s，而 115200 波特带 hex 最多 2.88K 样本/s
      （≈18% 占空）⇒ 拿到的是断续音频，只能看电平，不能当语料。

用法:
  python kws_capture.py <秒> <输出.wav> [--play <要同时播放的wav>] [--mode capture|dump]
  --play：在**开始吐数据那一刻**用 PC 喇叭播放给定 WAV —— 用来采"板麦听到的 TTS 唤醒词"
          （免真人路径：录的是板子自己声学环境里的唤醒词，而非纯净 TTS）
"""
import argparse
import os
import struct
import sys
import time
import wave

import serial

PORT = 'COM9'
BAUD = 115200
MONO_FRAME = 160          # = KWS_PDM_MONO_FRAME_SAMPLES（16kHz mono，10ms）
HEX_LINE_BYTES = MONO_FRAME * 4 + 6   # 'K' + 4 hex 帧号 + 640 hex + 换行


def capture(seconds, out_path, mode='capture', play=None, port=PORT, baud=BAUD, verbose=True):
    s = serial.Serial(port, baud, timeout=0.05)
    from kws_uart import LineReader
    lr = LineReader(s)
    s.dtr = True
    s.rts = True
    time.sleep(1.0)
    s.reset_input_buffer()
    s.write(b'\r\n')
    time.sleep(1.2)
    s.reset_input_buffer()
    s.write(f'kws {mode} {seconds}\r\n'.encode('ascii'))
    s.flush()

    frames_expected = int(seconds * 16000 / MONO_FRAME)
    # 录制时长 + 回吐时长（hex 行 × 波特）+ 余量
    cap = seconds + frames_expected * HEX_LINE_BYTES / (baud / 10.0) + 30
    t0 = time.time()
    idle = t0
    frames = {}
    started = False
    playing = False
    while time.time() - t0 < cap:
        t = lr.get(0.2)
        if t is None:
            if time.perf_counter() - idle > 15:
                if verbose:
                    print('!! 15s 没有任何行，提前收')
                break
            continue
        idle = time.perf_counter()
        if t.startswith('KWS_CAPTURE'):
            if verbose:
                print('板上录制中:', t)
            continue
        if t.startswith('KWS_DUMP_BEGIN'):
            started = True
            idle = time.perf_counter()
            if verbose:
                print('开始回吐:', t, f'（耗时约 {seconds + frames_expected * HEX_LINE_BYTES / (baud / 10.0):.0f}s）')
            if play and not playing:
                import winsound
                winsound.PlaySound(play, winsound.SND_FILENAME | winsound.SND_ASYNC)
                playing = True
            continue
        if t.startswith('KWS_DUMP_END'):
            if verbose:
                print('回吐结束, 收到帧:', len(frames))
            break
        # 帧行 = 'K' + 4 hex 帧号 + 每样本 4 hex（大端）。带帧号才能区分"串口丢行"和"板上真丢音频"
        if started and t[0] == 'K' and len(t) > 8:
            try:
                idx = int(t[1:5], 16)
                b = bytes.fromhex(t[5:])
            except ValueError:
                print(f'  !! 坏行（长 {len(t)}）丢弃')
                continue
            if len(b) != MONO_FRAME * 2:
                print(f'  !! 帧 {idx} 长度 {len(b)} 不对，丢弃')
                continue
            frames[idx] = b

    s.close()
    if playing:
        time.sleep(2.0)
        import winsound
        winsound.PlaySound(None, winsound.SND_PURGE)
    if not frames:
        print('!! 没抓到样本')
        return None
    hi = max(frames)
    lost = [i for i in range(hi + 1) if i not in frames]
    if lost:
        print(f'  ⚠ 串口丢帧 {len(lost)} 个（{lost[:12]}…），原位补零保持时间轴')
    samples = []
    for i in range(hi + 1):
        samples.extend(struct.unpack('>' + 'h' * MONO_FRAME, frames.get(i, b'\0' * (MONO_FRAME * 2))))
    with wave.open(out_path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(16000)
        w.writeframes(struct.pack('<' + 'h' * len(samples), *samples))
    got = len(samples) / 16000.0
    print(f'已存 {out_path}  样本 {len(samples)} 个 = {got:.2f}s（名义 {seconds}s）'
          + ('  ← 时长不足说明丢帧' if got < seconds * 0.9 else ''))
    return out_path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('seconds', type=int)
    ap.add_argument('out')
    ap.add_argument('--mode', default='capture', choices=['capture', 'dump'])
    ap.add_argument('--play', default=None, help='回吐开始时用 PC 喇叭播放的 wav')
    ap.add_argument('--port', default=PORT)
    ap.add_argument('--baud', type=int, default=BAUD)
    a = ap.parse_args()
    if a.mode == 'capture' and a.seconds > 15:
        a.seconds = 15
        print('板上 capture 上限 15s（480KB 堆），已夹到 15')
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    capture(a.seconds, a.out, mode=a.mode, play=a.play, port=a.port, baud=a.baud)


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    main()
