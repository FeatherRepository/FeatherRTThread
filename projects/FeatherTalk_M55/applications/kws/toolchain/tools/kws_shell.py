"""kws_shell.py — 往板上 MSH 发命令并把回显收进 logs/（诊断用的一次性通道）

用法:
  python tools/kws_shell.py --cmd "kws mic 5" [--wait 12] [--port COM9]
  python tools/kws_shell.py --cmd "kws info" --cmd "kws stop" --wait 3
"""
import argparse
import os
import sys
import time
from datetime import datetime

import serial


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cmd", action="append", required=True, dest="cmds")
    ap.add_argument("--wait", type=float, default=8.0, help="最后一条命令后的收流秒数")
    ap.add_argument("--port", default="COM9")
    ap.add_argument("--baud", type=int, default=115200)
    args = ap.parse_args()

    logdir = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "logs")
    os.makedirs(logdir, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    path = os.path.join(logdir, f"kws_shell_{stamp}.log")

    ser = serial.Serial(args.port, args.baud, timeout=0.2)
    ser.dtr = True
    ser.rts = True
    time.sleep(0.5)
    ser.reset_input_buffer()
    ser.write(b"\r\n")

    buf = bytearray()

    def drain(t):
        t0 = time.perf_counter()
        while time.perf_counter() - t0 < t:
            d = ser.read(4096)
            if d:
                buf.extend(d)

    for c in args.cmds:
        ser.write(f"{c}\r\n".encode("utf-8"))
        ser.flush()
        drain(max(1.5, args.wait / max(1, len(args.cmds))))
    drain(args.wait)
    ser.close()

    text = buf.decode("utf-8", "replace")
    open(path, "w", encoding="utf-8").write(text)
    print(text)
    print(f"\n[已存 {path}]")


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    main()
