"""kws_rate_probe.py — 量 mic0 交付倍率、板上推理节拍、被动误触率与音频交付率

为什么要这个脚本（2026-10-19）：
  ① 声道失配：kws.cpp 用 `#ifndef KWS_ENABLE_STEREO_INPUT_FEED` 决定帧几何，而驱动 drv_pdm.c
     用的是**另一个名字** `ENABLE_STEREO_INPUT_FEED`（rtconfig.h 已定义）。两个开关不同名 ⇒
     固件按"160 样本/帧的单声道"去读驱动实际交付的"L,R 交错 320 样本/帧"。墙钟能直接判：
       kws mic N 循环条件是 collected < N*16000 个"样本"：
         · 若驱动交 16k 单声道 → 跑满 N 秒
         · 若驱动交 32k（16k×2ch 交错）→ 只跑 N/2 秒      ← 判别量
  ② 采集缓冲溢出（第二处缺陷）：record pipe 只有 RT_AUDIO_RECORD_PIPE_SIZE 字节，
     满了是 rt_ringbuffer_put_force（覆盖最旧数据）。立体声 int16@16k = 64B/ms，
     2048B = 32ms < 一次推理 DSP51+NN30=81ms ⇒ 每个 hop 必然挖洞丢音频。
     稳态推导：只要 pipe ≥ 81ms 音频（5184B），消费者平均 drain = 生产 ⇒ 节拍 2.00 次/s
     且不丢；pipe 不足 ⇒ 节拍退化为 1/(1+81/500)=1.72 次/s 量级，丢的比例 = 停顿/周期。
     这条也解释了"板上 599 命中/小时 vs PC 复刻 0 命中"：两条链路吃的不是同一段音频
     （推理路径有洞，kws capture 路径不推理所以没洞）。

用法:
  python tools/kws_rate_probe.py [--mic-sec 5] [--trials 3] [--listen-sec 120] [--port COM9]
输出: 判定 + 数字，打到 stdout（同时落 logs/kws_rate_probe_<stamp>.txt）
"""
import argparse
import os
import re
import sys
import time
from datetime import datetime

import serial

INFO_RE = re.compile(r"推理: (\d+) 次，命中: (\d+) 次")
FLOW_RE = re.compile(r"音频流: 交付率 (\d+)%（丢样轮 (\d+) / 推理 (\d+)，半帧读 (\d+)）")
MIC_BEGIN_RE = re.compile(r"麦克风实测 (\d+) 秒")
MIC_SAMPLE_RE = re.compile(r"样本 (\d+) 个")
MIC_VERDICT_RE = re.compile(r"判定：(.+)")
LEVEL_RE = re.compile(r"电平\s+(\d+)")


def read_info(ser, wait=15.0):
    """发 kws info，取回 {infer, hit, flow_pct, lossy, short}（拿不到推理计数则返回 None）"""
    ser.write(b"\r\nkws info\r\n")
    ser.flush()
    deadline = time.perf_counter() + wait
    res = None
    flow = None
    while time.perf_counter() < deadline:
        line = readline_until(ser, deadline)
        if not line:
            continue
        m = INFO_RE.search(line)
        if m:
            res = (int(m.group(1)), int(m.group(2)))
        fm = FLOW_RE.search(line)
        if fm:
            flow = dict(flow_pct=int(fm.group(1)), lossy=int(fm.group(2)),
                        flow_infer=int(fm.group(3)), short=int(fm.group(4)))
        if res and flow:
            break
    if res is None:
        return None
    base = dict(flow_pct=None, lossy=None, short=None, flow_infer=None)
    base.update(flow or {})
    return dict(infer=res[0], hit=res[1], **base)


def read_counters(ser, wait=12.0):
    """兼容旧调用点：返回 (推理数, 命中数)"""
    d = read_info(ser, wait)
    return None if d is None else (d["infer"], d["hit"])


def readline_until(ser, deadline):
    remaining = deadline - time.perf_counter()
    if remaining <= 0:
        return ""
    ser.timeout = min(0.5, max(0.05, remaining))
    b = ser.readline()
    return b.decode("utf-8", "ignore").strip()


def probe_mic_rate(ser, mic_sec, trials, log):
    """量 kws mic N 的墙钟耗时 —— N/2 秒 ⇒ 驱动交付的是 2 通道交错"""
    out = []
    for i in range(trials):
        # 先把命令打进去，再从"麦克风实测"那行开始计时
        ser.write(f"kws mic {mic_sec}\r\n".encode())
        ser.flush()
        t0 = None
        t1 = None
        samples = None
        levels = []
        deadline = time.perf_counter() + mic_sec * 3 + 10
        while time.perf_counter() < deadline:
            line = readline_until(ser, deadline)
            if not line:
                continue
            if t0 is None and MIC_BEGIN_RE.search(line):
                t0 = time.perf_counter()
            m = LEVEL_RE.search(line)
            if m and t0 is not None:
                levels.append((round(time.perf_counter() - t0, 2), int(m.group(1))))
            sm = MIC_SAMPLE_RE.search(line)
            if sm:
                samples = int(sm.group(1))
            if t0 is not None and MIC_VERDICT_RE.search(line):
                t1 = time.perf_counter()
                break
        if t0 is None or t1 is None:
            log(f"  [试 {i+1}] 没抓到手start/end行（串口被别的输出打断），跳过")
            continue
        wall = t1 - t0
        log(f"  [试 {i+1}] kws mic {mic_sec} 墙钟 {wall:.2f}s  样本 {samples} 个  "
            f"电平条数 {len(levels)}  首末电平时间 {levels[0][0] if levels else '-'}s/{levels[-1][0] if levels else '-'}s")
        out.append((wall, samples))
        time.sleep(1.0)
    return out


def probe_infer_rate(ser, listen_sec, log):
    """量监听状态下的推理次数/秒、命中/小时，以及固件自报的音频交付率

    交付率是板上/PC 两条链路能否对上的地基：pipe（record）满时是 put_force
    覆盖最旧数据 ⇒ 模型听到的音频被周期性挖洞，而 kws capture 路径不推理、
    不会挖洞。两者看到的不是同一段音频，PC 复刻算出来的分数自然对不上板子。
    """
    ser.write(b"kws start\r\n")
    ser.flush()
    time.sleep(4.0)
    a = read_info(ser)
    if a is None:
        log("  读不到 kws info 基线计数")
        return None, None
    t0 = time.perf_counter()
    time.sleep(listen_sec)
    b = read_info(ser)
    t1 = time.perf_counter()
    if b is None:
        log("  读不到 kws info 末端计数")
        return None, None
    dt = t1 - t0
    d_inf = b["infer"] - a["infer"]
    d_hit = b["hit"] - a["hit"]
    log(f"  基线 {a} 末端 {b} 间隔 {dt:.2f}s → 推理 {d_inf/dt:.2f} 次/s，命中 {d_hit} 次"
        f"（={d_hit/dt*3600:.1f} 次/小时）")
    if b["flow_pct"] is not None:
        d_loss = (b["lossy"] - a["lossy"]) if a["lossy"] is not None else b["lossy"]
        log(f"  音频交付率 {b['flow_pct']}%（本段丢样轮 {d_loss}/{d_inf}，半帧读 {b['short']}）")
    elif d_inf:
        log(f"  本固件无流量计 ⇒ 只能靠推理节拍判交付：{d_inf/dt:.2f} 次/s vs 名义 2.00 次/s "
            f"= {d_inf/dt/2.0*100:.1f}%")
    return d_inf / dt, d_hit / dt


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mic-sec", type=int, default=5)
    ap.add_argument("--trials", type=int, default=3)
    ap.add_argument("--listen-sec", type=float, default=30)
    ap.add_argument("--port", default="COM9")
    ap.add_argument("--baud", type=int, default=115200)
    args = ap.parse_args()

    logdir = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "logs")
    os.makedirs(logdir, exist_ok=True)
    path = os.path.join(logdir, f"kws_rate_probe_{datetime.now():%Y%m%d_%H%M%S}.txt")
    fh = open(path, "w", encoding="utf-8")

    def log(s):
        print(s)
        fh.write(s + "\n")
        fh.flush()

    ser = serial.Serial(args.port, args.baud, timeout=0.5)
    ser.dtr = True
    ser.rts = True
    time.sleep(1.0)
    ser.reset_input_buffer()
    ser.write(b"\r\n")
    time.sleep(1.0)
    ser.reset_input_buffer()

    log(f"===== mic0 速率探针 {datetime.now():%F %T} ({args.port}@{args.baud}) =====")

    # 让 mic 空闲：kws stop（幂等）
    ser.write(b"kws stop\r\n")
    ser.flush()
    time.sleep(1.5)
    ser.reset_input_buffer()

    mic = probe_mic_rate(ser, args.mic_sec, args.trials, log)
    if mic:
        walls = [w for w, _ in mic]
        avg = sum(walls) / len(walls)
        ratio = args.mic_sec / avg if avg else 0
        log(f"  ⇒ 平均墙钟 {avg:.2f}s（名义 {args.mic_sec}s）→ 交付倍率 {ratio:.2f}×")
        log(f"    单声道假设应 {args.mic_sec:.2f}s（倍率 1.00×）；"
            f"16k×2 交错假设应 {args.mic_sec/2:.2f}s（倍率 2.00×）")

    # 回到监听态量推理节拍
    ser.reset_input_buffer()
    rate, hitrate = probe_infer_rate(ser, args.listen_sec, log)
    if rate:
        log(f"  ⇒ 推理 {rate:.2f} 次/s；交错假设上限 4.00 次/s，单声道假设 2.00 次/s")
    ser.write(b"kws info\r\n")
    ser.flush()
    time.sleep(1.0)
    ser.close()
    log(f"明细已存 {path}")


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    main()
