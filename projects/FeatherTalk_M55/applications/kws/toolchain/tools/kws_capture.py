"""kws_capture.py — 用 kws dump 抓板子麦克风真实音频为 WAV（免 SD，自主采集）

用法:
  python kws_capture.py <秒> <输出.wav> [--play <要同时播放的wav>]
"""
import serial, time, sys, wave, struct, threading, winsound

PORT = 'COM9'


def capture(seconds, out_path, play=None):
    s = serial.Serial(PORT, 115200, timeout=1)
    s.dtr = True; s.rts = True
    time.sleep(1.0)
    s.reset_input_buffer()
    s.write(b'\r\n'); time.sleep(1.2)
    s.write(f'kws dump {seconds}\r\n'.encode())

    samples = []
    started = False
    t0 = time.time()
    while time.time() - t0 < seconds + 25:
        line = s.readline()
        if not line:
            continue
        t = line.decode('ascii', 'ignore').strip()
        if t.startswith('KWS_DUMP_BEGIN'):
            started = True
            print('capture begin:', t)
            if play:
                winsound.PlaySound(play, winsound.SND_FILENAME | winsound.SND_ASYNC)
            continue
        if t.startswith('KWS_DUMP_END'):
            print('capture end, frames:', len(samples))
            break
        if started and len(t) > 4 and t[0] == 'K':
            try:
                b = bytes.fromhex(t[1:])
            except ValueError:
                continue
            # 固件按大端（最高位 nibble 先）输出 hex，故这里按大端解
            samples.extend(struct.unpack('>' + 'h' * (len(b) // 2), b))

    s.close()
    if not samples:
        print('!! 没抓到样本')
        return None
    with wave.open(out_path, 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(16000)
        w.writeframes(struct.pack('<' + 'h' * len(samples), *samples))
    print(f'saved {out_path}: {len(samples)} samples = {len(samples)/16000:.2f}s')
    return out_path


if __name__ == '__main__':
    sec = int(sys.argv[1]); out = sys.argv[2]
    play = None
    if '--play' in sys.argv:
        play = sys.argv[sys.argv.index('--play') + 1]
    capture(sec, out, play)
