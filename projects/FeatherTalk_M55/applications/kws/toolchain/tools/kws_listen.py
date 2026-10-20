"""kws_listen.py — 板上连续监听留档 + 误触统计（R0 待办 P0 的取数工具）

两种用法：
  1) **被动听**（缺省，不碰板上状态）：板子已经在 `kws start`，本脚本只读串口并计时。
     python tools/kws_listen.py --sec 3600 --tag ambient
  2) 自己拉起监听：`--start` 才发 `kws start`（跑完发 `kws stop`）。
     python tools/kws_listen.py --sec 600 --start

为什么两端都要 `kws info`：固件自己累计 `推理 N 次 / 命中 N 次`，两端读数之差是**板上权威计数**，
比我从日志里正则数命中行可靠（命中行受日志等级与打印密度影响）。逐窗分数行只是辅助：
`KWS_SCORE_LOG_INTERVAL=20` ⇒ 每 20 次推理打一行，即每 10s 一个采样，用来看待测分布。

口径边界（写在报表里，别把这里当验收）：
  * 「命中」只有在**这段时间确实没有人喊唤醒词**时才是误触。脚本不做判断，只在日志头记录
    起止时刻与环境说明（`--note`），由读数的人自己确认这段是空场。
  * 板与 PC 之间只有 USB 数据连接，噪声是房间/PC 风扇的真实环境音（不是数字静音）。
"""
import argparse
import datetime
import json
import os
import re
import sys
import time

import serial

from kws_console import force_utf8
force_utf8()

HERE = os.path.dirname(os.path.abspath(__file__))
LOG_DIR = os.path.abspath(os.path.join(HERE, "..", "logs"))

HIT_RE = re.compile(r"唤醒词命中: (\w+) \((\d+)%, (\d+)/(\d+)窗\)")
SCORE_RE = re.compile(r"\[kws\] #(\d+) dsp=(\d+)ms nn=(\d+)ms top1=(\w+)\((\d+)%\) top2=(\w+)\((\d+)%\)")
INFO_RE = re.compile(r"推理: (\d+) 次，命中: (\d+) 次")


def read_info(ser, wait=2.5):
    """发 `kws info` 并返回 (推理, 命中)；拿不到返回 None。"""
    ser.write(b"kws info\r\n")
    buf, t0 = "", time.time()
    while time.time() - t0 < wait:
        raw = ser.read(512)
        if raw:
            buf += raw.decode("utf-8", "replace")
    m = INFO_RE.search(buf)
    return (int(m.group(1)), int(m.group(2))) if m else None, buf


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sec", type=int, default=3600, help="监听时长（秒），缺省 1h")
    ap.add_argument("--port", default="COM9")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--tag", default="run", help="产物文件名后缀")
    ap.add_argument("--start", action="store_true", help="由本脚本发 kws start（否则只被动听）")
    ap.add_argument("--note", default="", help="环境说明，进报表（如：房间空场、PC 风扇低速）")
    ap.add_argument("--volume", type=int, default=-1,
                    help="soak 前把 sound0 音量设成该值（-1=不动）；用来把喇叭底噪从空场误触的候选根因里摘出去")
    a = ap.parse_args()

    os.makedirs(LOG_DIR, exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    log_path = os.path.join(LOG_DIR, f"kws_listen_{a.tag}_{stamp}.log")
    sum_path = os.path.join(LOG_DIR, f"kws_listen_{a.tag}_{stamp}.json")

    ser = serial.Serial(a.port, a.baud, timeout=1)
    ser.dtr = True
    ser.rts = True
    time.sleep(1.0)
    ser.reset_input_buffer()
    ser.write(b"\r\n")
    time.sleep(0.8)

    if a.volume >= 0:
        ser.write(("feather_audio_volume %d\r\n" % a.volume).encode("ascii"))
        time.sleep(1.0)
    base, _ = read_info(ser)
    if a.start:
        ser.write(b"kws start\r\n")
        time.sleep(1.5)
        base, _ = read_info(ser)          # 启动后重新取基线，才把这段与历史分开

    t_start = time.time()
    hits, scores, lines = [], [], 0
    with open(log_path, "w", encoding="utf-8") as f:
        head = (f"===== 监听留档 {datetime.datetime.now()} =====\n"
                f"时长目标 {a.sec}s  端口 {a.port}@{a.baud}  主动 start={a.start}  "
                f"sound0 音量={a.volume if a.volume >= 0 else '（不动）'}\n"
                f"基线 kws info（推理,命中）= {base}\n"
                f"环境说明: {a.note or '（未填）'}\n"
                f"注: 命中行只有在确认无人喊唤醒词的时段里才等于误触\n")
        f.write(head)
        f.flush()
        while time.time() - t_start < a.sec:
            raw = ser.readline()
            if not raw:
                continue
            txt = raw.decode("utf-8", "replace").rstrip()
            lines += 1
            el = time.time() - t_start
            f.write(f"[{el:8.2f}s] {txt}\n")
            f.flush()
            m = HIT_RE.search(txt)
            if m:
                hits.append((round(el, 2), m.group(1), int(m.group(2)),
                             f"{m.group(3)}/{m.group(4)}"))
            s = SCORE_RE.search(txt)
            if s:
                scores.append((round(el, 2), int(s.group(1)), int(s.group(2)), int(s.group(3)),
                               s.group(4), int(s.group(5)), s.group(6), int(s.group(7))))

    dur = time.time() - t_start
    if a.start:
        ser.write(b"kws stop\r\n")
        time.sleep(0.8)
    end, _ = read_info(ser)
    ser.close()

    d_inf = d_hit = None
    if base and end and all(isinstance(x, tuple) for x in (base, end)):
        d_inf, d_hit = end[0] - base[0], end[1] - base[1]
    summary = {
        "tag": a.tag, "note": a.note, "port": a.port,
        "started": str(datetime.datetime.fromtimestamp(t_start)),
        "duration_sec": round(dur, 1),
        "log_lines": lines,
        "baseline_info": list(base) if isinstance(base, tuple) else None,
        "end_info": list(end) if isinstance(end, tuple) else None,
        "delta_inferences": d_inf, "delta_hits": d_hit,
        "hits_parsed_from_log": len(hits),
        "hits_per_hour_by_log": round(len(hits) / dur * 3600, 1) if dur else None,
        "hits_per_hour_by_counter": round(d_hit / dur * 3600, 1) if d_hit is not None and dur else None,
        "hit_events": hits,
        "score_samples": scores[:2000],
        "log_path": log_path,
    }
    with open(sum_path, "w", encoding="utf-8") as f:
        json.dump(summary, f, ensure_ascii=False, indent=1)

    print(f"时长 {dur:.0f}s  基线 {base}  末端 {end}")
    if d_inf:
        print(f"板计数增量：推理 {d_inf} 次（应≈{dur*2:.0f}，hop 500ms）、命中 {d_hit} 次"
              f" → {summary['hits_per_hour_by_counter']} 次/小时")
    print(f"日志命中行 {len(hits)} 条 → {summary['hits_per_hour_by_log']} 次/小时")
    print(f"留档 {log_path}\n汇总 {sum_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
