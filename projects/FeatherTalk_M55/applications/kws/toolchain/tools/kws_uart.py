"""kws_uart.py — 串口"按整行读"的读取器（避免把一条长行读成两半）

为什么需要：pyserial 的 `readline()` 带短 timeout 时，会在超时点把一条长行截断返回。
板上回吐的帧行有 645 字节（'K' + 4 hex 帧号 + 160×4 hex），截断在 PC 侧表现为
"帧长度不对/坏行"，之前 16/1000 的"串口丢帧"里有相当一部分其实是这种假丢帧。
语料采集和板上/PC 对账都要求音频完整，所以统一走这里的缓冲读取。

用法：
    lr = LineReader(ser)
    line = lr.get(0.2)      # 有整行就返回（不含换行），没有就返回 None（最多等 0.2s）
"""
import time


class LineReader:
    def __init__(self, ser, encoding='ascii', tee=None):
        self.ser = ser
        self.buf = bytearray()
        self.encoding = encoding
        self.tee = tee            # 传文件对象：把收到的原始字节同时落一份，行被谁截断当场可查

    def get(self, wait=0.2):
        """返回下一条完整行；wait 秒内没有则返回 None（已收到的半行留在缓冲里）"""
        deadline = time.perf_counter() + wait
        while True:
            i = self.buf.find(b'\n')
            if i >= 0:
                line = bytes(self.buf[:i])
                del self.buf[:i + 1]
                s = line.decode(self.encoding, 'ignore').strip()
                if s:
                    return s
                continue
            if time.perf_counter() > deadline:
                return None
            n = getattr(self.ser, 'in_waiting', 0)
            if n:
                chunk = self.ser.read(min(n, 4096))
            else:
                chunk = self.ser.read(1)
            if chunk:
                self.buf.extend(chunk)
                if self.tee is not None:
                    self.tee.write(chunk)
