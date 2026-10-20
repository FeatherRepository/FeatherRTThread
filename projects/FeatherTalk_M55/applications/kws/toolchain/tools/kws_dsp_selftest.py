"""kws_dsp_selftest.py — 已知答案自检：合成信号下，板上 EI DSP 逐级 vs PC 逐级复刻

为什么需要（2026-10-19）：
  板上同源对账（kws_eval.py）在**校验和一致的同一段真实音频**上量到：NN 段两端一致
  （Δ≤0.05），而 DSP 输出的 13 个倒谱列**逐列相关系数≈0**。逐列仿射变换不改变相关系数，
  所以差异必然是"帧相关"的——但真实音频同时带来三个混杂因素：麦克风链路本身、串口丢帧
  补零挖洞、以及 PC 侧的补零长度。要定位到具体哪一级，必须让**两端吃逐位相同的输入**。
  于是固件加 `kws dsp <模式>`：输入由纯整数递推合成（无 sin、无浮点），PC 用同一套整数
  运算复现，压进同一个 s_ring，走同一个 run_classifier；再逐级导出
      DSPE 帧能量(log 前) / DSPF mel 线性(mfe 输出) / DSPM log-mel / DSPR cmvnw 前 / DSPC cmvnw 后
  以及几何/FFT 四级探针（把"哪一级错"夹到具体函数）：
      DSPMATH mel 三个函数(log / frequency_to_mel / mel_to_frequency / get_fft_bin_from_hertz)单点实测
      DSPB    mfe 用到的 nf+2 个 mel 边界 bin
      DSPT    预加重后第 0 帧的全部 320 个样点
      DSPP    对那 320 点跑 numpy::power_spectrum 得到的 129 个 bin
  本脚本做五件事：
    1) 先证输入同源（DSP_SYNTH 的 ck/peak + 可选整帧 hex 回吐逐字节核对）
    2) 板上内部自检：DSPC vs EVALF（EVALF 走生产 extract_mfcc_features，两者应逐位同）
    3) T0~T4：预加重 / FFT / mel 几何 / 三角加权 分别对账（fft_probe）
    4) 逐级比对，第一个出结构性分歧的级 = 分歧所在
    5) PC 各级直接取 ei_mfcc() 的中间量（不做任何"修正"，分歧才能归给 ei_mfcc.py）

结构判据：max|Δ| ≤ 0.02 视为一致（float32 / EI 快速 log 近似的噪声远小于此）；
  相关系数 >0.999 = 只是数值差；<0.9 = 结构性分歧（需继续定位）。

已知例外（别再当未定位的分歧）：R/C 两级是 cmvnw 的输出，板上除数是 std+1e-10
  （speechpy/processing.hpp:472）。合成输入里 0（数字静音）与 3（1kHz 等幅方波）会让
  特征列变成**常数列**→ std=0，此时输出 = 上游 1e-5 级浮点差 ÷ 1e-10 = 1e5 级放大，
  板与 PC 谁都不"对"，这个数本身没有物理意义。判据：只要 R 级 max|Δ| 在 1e-5 量级、
  C 级 |值| 到 1e5 量级，就是病态列而非链路分歧（真实语音的列 std >> 1e-5，不受影响）。

用法（default 环境即可：只要 pyserial + numpy，不需要 tensorflow）:
  <default>/python.exe tools/kws_dsp_selftest.py                 # 全 6 个模式，模式 3 回吐字节
  <default>/python.exe tools/kws_dsp_selftest.py --modes 2,3,5
  <default>/python.exe tools/kws_dsp_selftest.py --echo 0,2,3    # 指定哪些模式做逐字节核对
输出: logs/diag/kws_dsp_<stamp>.{json,log} + kws_dsp_m<模式>_*.raw（串口原始流）
"""
import argparse
import json
import math
import os
import re
import sys
import time
from datetime import datetime

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "training"))
sys.path.insert(0, HERE)
from ei_mfcc import (SR, FRAME_LENGTH, FRAME_STRIDE, NUM_CEPSTRAL, NUM_FILTERS,   # noqa: E402
                     FFT_LENGTH, WIN_SIZE, LOW_FREQ, HIGH_FREQ, VERSION,
                     _preemphasis, _frame_offsets, _power_spectrum, _mel_filterbank,
                     _apply_filterbank, _dct2_ortho, _cmvnw)
from kws_uart import LineReader                                     # noqa: E402

WIN = 16000                    # = EI_CLASSIFIER_RAW_SAMPLE_COUNT
MONO_FRAME = 160
# 逐级导出行（DSPE/F/M/R/C）现在是 **float32 位模式**（0..4294967295），无损：1e-10 与 0
# 能分开。只有 EVALF 还是"特征×1000 取整 + 哨兵"的老编码（生产路径，改动代价大、留着做交叉验证）。
NAN_MARK, BIG_POS_MARK, BIG_NEG_MARK = 2000000001, 2000000002, 2000000003

ROW_RE = re.compile(r"^(DSPE|DSPF|DSPM|DSPR|DSPC) (-?\d+) (\d+)((?: [0-9]+)+)$")
META_RE = re.compile(r"^DSP_META (-?\d+) (\d+) (\d+) (\d+) (\d+)")
SYNTH_RE = re.compile(r"^DSP_SYNTH (\d+) (\d+) (\d+) (-?\d+)")
SCORE_RE = re.compile(r"^DSP_SCORE (\d+) (-?\d+) err=(-?\d+) dsp=(-?\d+) nn=(-?\d+) ck=(\d+)")
EVALF_RE = re.compile(r"^EVALF (-?\d+) (\d+) (\d+) (\d+) (\d+)")
# 几何/FFT 探针（固件 mfcc_dump_stages 里 win<100 才打）
BINS_RE = re.compile(r"^DSPB (-?\d+) mb=(\d+) fft=(\d+)((?: \d+)+)$")
PPHDR_RE = re.compile(r"^DSPP_HDR (-?\d+) err=(-?\d+) flen=(\d+) psz=(\d+)")
CHUNK_RE = re.compile(r"^(DSPP|DSPT) (-?\d+) (\d+)((?: [0-9]+)+)$")
MATH_RE = re.compile(r"^DSPMATH((?: [0-9]+)+)$")   # 8 个 float32 位模式
FRAME_RE = re.compile(rb"K([0-9a-fA-F]{4})([0-9a-fA-F]{640})")
HEXCH = set("0123456789abcdefABCDEF")

MODE_NAME = {0: "数字静音", 1: "恒定 DC(500)", 2: "LCG 白噪声±8000",
             3: "1kHz 等幅方波±6000", 4: "方波 100ms 幅度交替", 5: "孤立 320 样点方波"}
STAGE_NAME = {"E": "帧能量(log前)", "F": "mel 线性(mfe 输出)", "M": "log-mel(np.log)",
              "Mf": "log-mel(板上快速log)", "R": "cmvnw 前(np.log)",
              "Rf": "cmvnw 前(板上快速log)", "C": "cmvnw 后(np.log)",
              "Cf": "cmvnw 后(板上快速log)"}


# ---------------- 合成信号（必须与固件 dsp_synth 逐位一致：只用整数递推） ----------------

def synth(mode, n=WIN):
    out = np.zeros(n, dtype=np.int16)
    x = 0x1234ABCD
    for i in range(n):
        if mode == 1:
            v = 500
        elif mode == 2:
            x = (x * 1103515245 + 12345) & 0xFFFFFFFF
            v = (((x >> 16) & 0x7FFF) * 16000 // 32767) - 8000
        elif mode == 3:
            v = 6000 if (i >> 3) & 1 else -6000
        elif mode == 4:
            amp = 8000 if (i // 1600) & 1 else 500
            v = amp if (i >> 3) & 1 else -amp
        elif mode == 5:
            v = 0
            if 8000 <= i < 8320:
                v = 8000 if (i >> 3) & 1 else -8000
        else:
            v = 0
        out[i] = v
    return out


def board_cksum(x):
    """复刻固件 cap_cksum：int16 符号扩展后累加，取 uint32 回绕"""
    return int(np.asarray(x, dtype=np.int64).sum() & 0xFFFFFFFF)


def board_peak(x):
    v = np.abs(np.asarray(x, dtype=np.int32))
    return int(v.max()) if v.size else 0


# ---------------- PC 逐级复刻（就是 ei_mfcc() 的中间量，一字不改） ----------------

def fast_log(a):
    """EI numpy::log(float) 的向量化复刻（numpy.hpp 里 Norbert Juffa 的快速 log 近似）。

    为什么必须单独复刻这一条：生产链路取对数用的就是它，不是 libm 的 logf；
    而 ei_mfcc.py 用的是 np.log。log-mel 级若只对这一条敏感，结论是"PC 复刻精度更高、
    但和板子不同源"，和"滤波器组/分帧本身偏了"是两个完全不同的问题。
    中间量全程 float32；C 的 fmaf 是单重舍入，这里用 float64 精算后回 float32 近似。
    注意 e 是有符号整型、0xFF800000 掩码保留符号位：a=0 时 e=-1065353216、结果 -88.0297。"""
    x = np.ascontiguousarray(np.asarray(a, dtype=np.float32))
    g = x.view(np.uint32).astype(np.int64)
    e = (g - 0x3F2AAAAB) & 0xFF800000
    e = np.where(e >= 0x80000000, e - 0x100000000, e)      # 掩码结果按 int32 解释
    g = g - e
    i = (e * 1.19209290e-7).astype(np.float32)
    m = g.astype(np.uint32).view(np.float32)
    f = (m - np.float32(1.0)).astype(np.float32)
    s = (f * f).astype(np.float32)

    def fma(A, B, C):
        return (A.astype(np.float64) * B.astype(np.float64) + C.astype(np.float64)
                ).astype(np.float32)

    r = fma(np.full_like(f, np.float32(0.230836749)), f, np.full_like(f, np.float32(-0.279208571)))
    t = fma(np.full_like(f, np.float32(0.331826031)), f, np.full_like(f, np.float32(-0.498910338)))
    r = fma(r, s, t)
    r = fma(r, s, f)
    return fma(i, np.full_like(f, np.float32(0.693147182)), r)


def pc_stages(x):
    xp = _preemphasis(x)
    frame_len, stride, numframes = _frame_offsets(x.shape[0], SR, FRAME_LENGTH,
                                                 FRAME_STRIDE, VERSION)
    bins = _mel_filterbank(SR, NUM_FILTERS, FFT_LENGTH, LOW_FREQ, HIGH_FREQ)
    mel = np.zeros((numframes, NUM_FILTERS), dtype=np.float32)
    en = np.zeros(numframes, dtype=np.float32)
    for fi in range(numframes):
        off = fi * stride
        p = _power_spectrum(xp[off:off + frame_len], FFT_LENGTH)
        en[fi] = p.sum()
        mel[fi] = _apply_filterbank(p, bins, NUM_FILTERS)
    logmel = np.log(np.maximum(mel, 1e-10))
    raw = _dct2_ortho(logmel)[:, :NUM_CEPSTRAL].copy()
    raw[:, 0] = np.log(np.maximum(en, 1e-10))
    cm = _cmvnw(raw, WIN_SIZE, variance_normalization=True)
    # 同一张 mel，只把"取对数"换成板上那条快速 log（zero_handling 语义：只改恰好等于 0 的）
    mel_z = np.where(mel == 0, np.float32(1e-10), mel)
    en_z = np.where(en == 0, np.float32(1e-10), en)
    logmel_f = fast_log(mel_z).astype(np.float32)
    raw_f = _dct2_ortho(logmel_f)[:, :NUM_CEPSTRAL].copy()
    raw_f[:, 0] = fast_log(en_z)
    cm_f = _cmvnw(raw_f, WIN_SIZE, variance_normalization=True)
    return {"E": en, "F": mel, "M": logmel, "Mf": logmel_f,
            "R": raw, "Rf": raw_f, "C": cm, "Cf": cm_f}, numframes


# ---------------- 串口侧 ----------------

def hex_run(s):
    """取行首连续 hex 段（帧头的 'K' 记进来）；太短的当别的日志丢掉——同 kws_eval.py"""
    had_k = s[:1] == "K"
    if had_k:
        s = s[1:]
    n = 0
    while n < len(s) and s[n] in HEXCH:
        n += 1
    if n < (5 if had_k else 8):
        return ""
    return ("K" if had_k else "") + s[:n]


def decode_val(iv):
    if iv == NAN_MARK:
        return float("nan")
    if iv == BIG_POS_MARK:
        return float("inf")
    if iv == BIG_NEG_MARK:
        return float("-inf")
    return iv / 1000.0


ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")


def evalf_tokens(t):
    """EVALF 的数字流跨行累积。控制台写不是原子的：别的线程会把 ANSI 日志插进同一行，
    所以先剥 ANSI，再取**行首连续**的整数 token（尾巴上被插进来的日志丢掉，行首的才是本行数据）。
    返回 (tokens, 是否整行干净)。"""
    s = ANSI_RE.sub("", t).strip()
    out, clean = [], True
    for v in s.split():
        if re.fullmatch(r"-?\d+", v):
            out.append(int(v))
        else:
            clean = False
            break
    return out, clean


def cut(t, tag):
    """从行内任意位置取出以 tag 开头的片段：控制台写不是原子的，
    别的线程的日志（`msh />DSP_SYNTH 4 …`、`[FeatherTalk] …DSP_META …`）会和它拼在同一行。"""
    i = t.find(tag)
    return t[i:] if i >= 0 else None


class Tee:
    """串口原始流同时留内存和磁盘：行被谁截断当场可查"""

    def __init__(self, path):
        self.data = bytearray()
        self.fh = open(path, "wb") if path else None

    def write(self, b):
        self.data.extend(b)
        if self.fh:
            self.fh.write(b)

    def close(self):
        if self.fh:
            self.fh.close()


def rebuild_frames(buf):
    """帧行在**字节流层**重建（行层会被别的线程的换行拦腰截断）——同 kws_eval.py"""
    pieces = []
    at = buf.find(b"KWS_DUMP_BEGIN")
    for rec in (buf[at:] if at >= 0 else buf).split(b"\n"):
        run = hex_run(rec.strip(b"\r").decode("ascii", "ignore"))
        if run:
            pieces.append(run)
    stream = "".join(pieces).encode("ascii")
    frames, pos = {}, 0
    while True:
        j = stream.find(b"K", pos)
        if j < 0:
            break
        m = FRAME_RE.match(stream, j)
        if m:
            frames.setdefault(int(m.group(1), 16),
                              np.frombuffer(bytes.fromhex(m.group(2).decode()), dtype=">i2"))
            pos = m.end()
        else:
            pos = j + 1
    if not frames:
        return None
    hi = max(frames)
    audio = np.zeros((hi + 1) * MONO_FRAME, dtype=np.int16)
    for k, v in frames.items():
        audio[k * MONO_FRAME:(k + 1) * MONO_FRAME] = v
    return dict(audio=audio, frames=frames, nframes=len(frames), hi=hi,
                missing=(hi + 1) - len(frames))


def collect(modes, echo_modes, port, baud, log, rawdir, results=None):
    import serial
    s = serial.Serial(port, baud, timeout=0.05)
    s.dtr = True
    s.rts = True
    time.sleep(1.0)

    results = results if results is not None else {}
    for mode in modes:
        rawpath = os.path.join(rawdir, f"kws_dsp_m{mode}_{int(time.time())}.raw")
        tee = Tee(rawpath)
        lr = LineReader(s, tee=tee)
        s.reset_input_buffer()
        # 不发 kws stop：固件的 stop 会 rt_device_close 麦克风、重开拿不到样本（已证缺陷），
        # 把板子留在坏状态会拖累后续真采。自检分支在主循环每轮都查，正常监听时也能插进去。
        cmd = f"kws dsp {mode}{'' if mode in echo_modes else ' noecho'}\r\n"
        log(f"\n===== 模式 {mode}（{MODE_NAME.get(mode, '?')}）→ {cmd.strip()} =====")
        s.write(cmd.encode("ascii"))
        s.flush()

        res = {"mode": mode, "cksum": None, "peak": None, "score": None, "meta": None,
               "rows": {}, "evalf_meta": None, "evalf": None, "audio": None,
               "bins": None, "pp_hdr": None, "chunks": {}, "math": None,
               "unmatched": [], "dropped": {}}
        feat = None                       # EVALF 每行 13 个，需要跨行累积
        t0 = idle = time.perf_counter()
        cap = 90 if mode in echo_modes else 45
        while time.perf_counter() - t0 < cap:
            t = lr.get(0.2)
            if t is None:
                if time.perf_counter() - idle > 25:
                    log("!! 25s 无输出，提前收（板子可能卡在上一轮回吐里）")
                    break
                continue
            idle = time.perf_counter()

            seg = cut(t, "DSP_SYNTH")
            if seg and res["cksum"] is None:
                m = SYNTH_RE.match(seg)
                if m:
                    res["cksum"], res["peak"] = int(m.group(3)), int(m.group(4))
                    log(f"  输入: {m.group(0)}")
                    continue
            seg = cut(t, "DSP_SCORE")
            if seg:
                m = SCORE_RE.match(seg)
                if m:
                    res["score"] = dict(score=int(m.group(2)) / 1000.0, err=int(m.group(3)),
                                        dsp=int(m.group(4)), nn=int(m.group(5)),
                                        cksum=int(m.group(6)))
                    log(f"  打分: {m.group(0)}")
                    continue
            seg = cut(t, "DSP_META")
            if seg:
                m = META_RE.match(seg)
                if m:
                    res["meta"] = dict(rows=int(m.group(2)), nf=int(m.group(3)),
                                       nc=int(m.group(4)), length=int(m.group(5)))
                    log(f"  尺寸: {m.group(0)}")
                    continue
            # 必须在 DSPM 分支之前：cut() 找子串，"DSPM" 会命中 "DSPMATH"
            seg = cut(t, "DSPMATH")
            if seg:
                m = MATH_RE.match(seg)
                if m:
                    vs = [int(v) for v in m.group(1).split()]
                    if len(vs) >= 8:
                        res["math"] = list(np.array(vs[:12], dtype=np.uint32)
                                           .view(np.float32).astype(np.float64))
                        log("  DSPMATH: " + " ".join("%.6g" % v for v in res["math"]))
                    else:
                        res["dropped"]["MATH"] = res["dropped"].get("MATH", 0) + 1
                    continue
                res["dropped"]["MATH"] = res["dropped"].get("MATH", 0) + 1
                continue

            seg = cut(t, "DSPB")
            if seg:
                m = BINS_RE.match(seg)
                if m:
                    res["bins"] = dict(win=int(m.group(1)), max_bin=int(m.group(2)),
                                       fft=int(m.group(3)),
                                       vals=[int(v) for v in m.group(4).split()])
                    log(f"  边界bin: {m.group(0)}")
                    continue
                res["dropped"]["B"] = res["dropped"].get("B", 0) + 1
                continue

            seg = cut(t, "DSPP_HDR")
            if seg:
                m = PPHDR_RE.match(seg)
                if m:
                    res["pp_hdr"] = dict(win=int(m.group(1)), err=int(m.group(2)),
                                         flen=int(m.group(3)), psz=int(m.group(4)))
                    log(f"  功率谱头: {m.group(0)}")
                    continue

            seg, ctag = cut(t, "DSPP"), "P"
            if seg is None:
                seg, ctag = cut(t, "DSPT"), "T"
            if seg is not None:
                m = CHUNK_RE.match(seg)
                if m:
                    wk, ch = int(m.group(2)), int(m.group(3))
                    nums = [int(v) for v in m.group(4).split()]
                    if len(nums) <= 32 and all(0 <= v <= 0xFFFFFFFF for v in nums):
                        res["chunks"].setdefault((ctag, wk), {})[ch] = nums
                    else:
                        res["dropped"][ctag] = res["dropped"].get(ctag, 0) + 1
                else:
                    res["dropped"][ctag] = res["dropped"].get(ctag, 0) + 1
                continue

            seg, row_tag = None, None
            for tag in ("E", "F", "M", "R", "C"):
                seg = cut(t, "DSP" + tag)
                if seg:
                    row_tag = tag
                    break
            if seg is not None:
                m = ROW_RE.match(seg) if res["meta"] else None
                if m:
                    wk = int(m.group(2))
                    row = int(m.group(3))
                    nums = [int(v) for v in m.group(4).split()]
                    need = {"E": 1, "F": res["meta"]["nf"], "M": res["meta"]["nf"],
                            "R": res["meta"]["nc"], "C": res["meta"]["nc"]}[row_tag]
                    if len(nums) == need and all(0 <= v <= 0xFFFFFFFF for v in nums):
                        res["rows"].setdefault((row_tag, wk), {})[row] = nums
                    else:
                        res["dropped"][row_tag] = res["dropped"].get(row_tag, 0) + 1
                else:
                    res["dropped"][row_tag] = res["dropped"].get(row_tag, 0) + 1
                continue

            seg = cut(t, "EVALF")
            if seg:
                if seg.startswith("EVALF_ERR"):
                    log("!! 板上特征导出报错: " + seg)
                    continue
                m = EVALF_RE.match(seg)
                if m:
                    res["evalf_meta"] = dict(win=int(m.group(1)), units=int(m.group(2)),
                                             rows=int(m.group(3)), cols=int(m.group(4)),
                                             cksum=int(m.group(5)))
                    feat = []
                    log(f"  EVALF: {m.group(0)}")
                    continue

            if feat is not None:
                toks, clean = evalf_tokens(t)
                if clean and toks:
                    feat.extend(toks)
                    if len(feat) >= res["evalf_meta"]["units"]:
                        res["evalf"] = [decode_val(v) for v in feat[:res["evalf_meta"]["units"]]]
                        feat = None
                    continue
                if not clean and toks:      # 被别的线程污染的半行：丢掉并计数，不混进特征
                    res["dropped"]["EVALF"] = res["dropped"].get("EVALF", 0) + 1
                    continue
            if t.startswith("KWS_DUMP_END"):
                if res["audio"] is None:
                    res["audio"] = rebuild_frames(tee.data)
                log("  回吐结束")
                break
            if t.startswith("KWS_DUMP_BEGIN"):
                log("  音频回吐: " + t)
                continue
            if t.startswith("KWS_DSP"):
                log("  " + t)
                continue
            if t.startswith("DSP_ERR"):
                log("!! 板上报错: " + t)
                continue
            res["unmatched"].append(t[:80])

        if res["audio"] is None and mode in echo_modes:
            res["audio"] = rebuild_frames(tee.data)
        tee.close()
        results[mode] = res
        got = ", ".join(f"{t}|{w}:{len(v)}" for (t, w), v in sorted(res["rows"].items()))
        log(f"  收到数据行 {got or '无'}；特征 {len(res['evalf'] or [])} 个")
        ck = ", ".join(f"{t}|{w}:{len(v)}" for (t, w), v in sorted(res["chunks"].items()))
        log(f"  探针: 边界bin={len(res['bins']['vals']) if res['bins'] else 0} 个 "
            f"hdr={res['pp_hdr']} 分块={ck or '无'}")
        if res["dropped"]:
            log(f"  长度不对丢弃的行: {res['dropped']}")
        if res["unmatched"]:
            log(f"  未识别 {len(res['unmatched'])} 条，前 6：")
            for u in res["unmatched"][:6]:
                log(f"    ? {u!r}")
        s.reset_input_buffer()
        s.write(b"\r\n")
        s.flush()
        time.sleep(0.5)
    s.close()
    return results


# ---------------- 逐级比对 ----------------

def grade(bit_eq, n, diff_max, rel_max, corr):
    if n < 8:
        return "样本不足"
    if bit_eq == n:
        return "逐位一致"
    if diff_max <= 0.02 and rel_max <= 1e-4:
        return "仅数值差"
    if corr is not None and corr > 0.999:
        return "弱数值分歧"
    if corr is not None and corr > 0.9:
        return "弱分歧"
    return "结构性分歧"


def cmp_rows(board_rows, pc, name, log):
    """board_rows: {行号: [float32 的 uint32 位模式]}；pc: (rows,cols) 或 (rows,)"""
    pf = np.asarray(pc, dtype=np.float32)
    if pf.ndim == 1:
        pf = pf.reshape(-1, 1)
    pbits = pf.view(np.uint32)
    got = [r for r in sorted(board_rows)
           if r < pf.shape[0] and len(board_rows[r]) == pf.shape[1]]
    if not got:
        log(f"  {name}: 板上没有可用行")
        return dict(rows=0, cells=0, bit_eq=0, verdict="无数据")
    raw = np.array([board_rows[r] for r in got], dtype=np.uint32)
    b = raw.view(np.float32).astype(np.float64)
    p = pf[got].astype(np.float64)
    bit_eq = int((raw == pbits[got]).sum())
    both = np.isfinite(b) & np.isfinite(p)
    n = int(both.sum())
    out = dict(rows=len(got), pc_rows=int(pf.shape[0]), cols=int(pf.shape[1]),
               cells=n, bit_eq=bit_eq, total_cells=int(raw.size),
               nonfinite_board=int((~np.isfinite(b)).sum()),
               nonfinite_pc=int((~np.isfinite(p)).sum()))
    if n == 0:
        out.update(max_abs=None, max_rel=None, corr=None, verdict="无数据")
        log(f"  {name}: 板上非有限 {out['nonfinite_board']} / PC 非有限 {out['nonfinite_pc']}"
            f"，无可比数值")
        return out
    bd, pd = b[both], p[both]
    d = np.abs(bd - pd)
    big = np.abs(pd) > 1e-6
    rel = float((d[big] / np.abs(pd)[big]).max()) if big.any() else 0.0
    corr = None
    if d.size > 1 and np.std(bd) > 1e-9 and np.std(pd) > 1e-9:
        corr = float(np.corrcoef(bd, pd)[0, 1])
    ai = int(np.argmax(d))
    # d 是"按 both 掩码压平后"的一维量，要回到 (行,列) 得先还原掩码位置——
    # 直接拿 ai 去 unravel_index(b.shape) 会错位（掩码不是全 True）
    pos = np.flatnonzero(both.ravel())[ai]
    worst = [int(got[pos // b.shape[1]]), int(pos % b.shape[1]),
             float(bd[ai]), float(pd[ai])]
    out.update(max_abs=float(d[ai]), max_rel=rel, corr=corr,
               board_range=[float(bd.min()), float(bd.max())],
               pc_range=[float(pd.min()), float(pd.max())],
               worst=worst)
    out["verdict"] = grade(bit_eq, n, out["max_abs"], rel, corr)
    log(f"  {name}: 行 {len(got)}/{pf.shape[0]} 单元 {n}/{raw.size} "
        f"逐位相同={bit_eq}({100.0 * bit_eq / raw.size:.0f}%) max|Δ|={out['max_abs']:.4g} "
        f"@行{out['worst'][0]}列{out['worst'][1]} 板上{out['worst'][2]:.6g} "
        f"PC{out['worst'][3]:.6g} max相对={rel:.3g} corr="
        f"{('%.4f' % corr) if corr is not None else 'n/a'} "
        f"范围 板上{[round(v, 4) for v in out['board_range']]}/"
        f"PC{[round(v, 4) for v in out['pc_range']]} → {out['verdict']}")
    return out


def assemble_chunks(chunks, total):
    """{行号: [float32 位模式]} → 长度 total 的一维数组（每行 32 个，行号=偏移/32）
    缺的行填 NaN，越界返回 None"""
    if not chunks:
        return None
    out = np.full(total, np.nan, dtype=np.float64)
    for ch, nums in chunks.items():
        o = ch * 32
        if o + len(nums) > total:
            return None
        out[o:o + len(nums)] = (np.array(nums, dtype=np.uint32)
                                .view(np.float32).astype(np.float64))
    return out


def bin_mapping(board, pc):
    """板上第 i 个功率谱值 = PC 第 j 个 → 把 j 找出来（值唯一时才可靠）。
    用来判定"FFT 本身算错"还是"顺序被换了"：顺序若是 i↔2i、bit-reversal 之类的规律，
    在这张表里一眼能看出来；若是乱序且值集合相同，也是同一类问题。"""
    tol = 1e-6 * max(1.0, float(np.nanmax(np.abs(pc))))
    perm, ambig = [], 0
    for v in board:
        hit = np.flatnonzero(np.abs(pc - v) <= tol)
        if hit.size == 0:
            perm.append(-1)
        else:
            perm.append(int(hit[0]))
            ambig += int(hit.size > 1)
    return perm, ambig


def fft_probe(res, x, log):
    """把"预加重 / FFT / mel 几何 / 滤波组算术"四件事**分别**验一遍（全用板上那帧做输入，
    任何一步分歧都不受上一步影响）：
      T1 板上 DSPT(帧0)  vs PC 的 _preemphasis(x)[0:flen]      → 预加重同不同源
      T2 板上 DSPP       vs PC 对**板上帧**做 DFT 的功率谱      → FFT 同不同源
      T3 板上 DSPB       vs PC 的 _mel_filterbank 边界 bin      → mel 几何同不同源
      T4 板上 DSPF 行0   vs 用板上自己的 bins+DSPP 重算的 mel   → 三角加权算术同不同源
    """
    out = {}
    if res.get("math"):
        exp_v = [2.5200, 2840.2, 781.0, 8000.0, 1200.3, 12.4286, 8000.0, 128.0,
                 2.5200, 0.693147, -0.693147, 3.6352]
        names = ["numpy::log(1+8000/700)", "frequency_to_mel(8000)", "frequency_to_mel(700)",
                 "mel_to_frequency(2840.2)", "mel_to_frequency(1127)", "expf(2840.2/1127)",
                 "700*(expf(2840.2/1127)-1)", "get_fft_bin_from_hertz(256,8000,16000)",
                 "logf(1+8000/700) [libm]", "numpy::log(2)", "numpy::log(0.5)",
                 "numpy::log2(1+8000/700)"]
        log("  T0 mel/log 单点实测（板上 / 期望 / 倍率）：")
        for nm, got, want in zip(names, res["math"], exp_v):
            log(f"     {nm}: {got:.6g} / {want:.6g} / {got / want:.5f}")
        out["T0_math"] = dict(values=[float(v) for v in res["math"]],
                              expect=exp_v, names=names,
                              ratios=[float(v / w) for v, w in zip(res["math"], exp_v)])
    hdr, chunks = res["pp_hdr"], res["chunks"]
    if hdr is None or hdr["err"] != 0:
        log(f"  FFT 探针: 没有 DSPP_HDR 或板上 power_spectrum 报错 → {hdr}")
        return {"skip": "no DSPP_HDR/err"}

    flen, psz = hdr["flen"], hdr["psz"]
    fr_b = assemble_chunks(chunks.get(("T", res["mode"]), {}), flen)
    ps_b = assemble_chunks(chunks.get(("P", res["mode"]), {}), psz)
    if fr_b is None or ps_b is None:
        log("  FFT 探针: DSPT/DSPP 行没收齐（"
            f"T={len(chunks.get(('T', res['mode']), {}))}/{math.ceil(flen / 32)} "
            f"P={len(chunks.get(('P', res['mode']), {}))}/{math.ceil(psz / 32)}）")
        return {"skip": "chunks 不全"}

    # T1 预加重
    xp = _preemphasis(x)
    fr_b32 = np.asarray(fr_b[:flen], dtype=np.float32)
    fr_p32 = np.asarray(xp[:flen], dtype=np.float32)
    fr_p = fr_p32.astype(np.float64)
    d1 = np.abs(fr_b32.astype(np.float64) - fr_p)
    same1 = bool((fr_b32.view(np.uint32) == fr_p32.view(np.uint32)).all())
    first1 = int(np.argmax(d1))
    log(f"  T1 预加重(帧0 {flen} 点): 逐位相同={same1} max|Δ|={d1.max():.6g} "
        f"@{first1} 板上{fr_b[first1]:.6g} PC{fr_p[first1]:.6g} "
        f"（首个不同样点 {next((i for i in range(flen) if d1[i] > 0), None)}）")
    out["T1_preemphasis"] = dict(bit_equal=same1, max_abs=float(d1.max()),
                                 first_diff=next((int(i) for i in range(flen) if d1[i] > 0), None))

    # T2 FFT：拿**板上那帧**在 PC 上做 DFT，和板上 DSPP 比
    ps_p = _power_spectrum(np.asarray(fr_b[:flen], dtype=np.float32), FFT_LENGTH) \
        .astype(np.float64)
    fin = np.isfinite(ps_b)
    d2 = np.abs(ps_b[fin] - ps_p[fin])
    rel2 = float((d2 / np.maximum(np.abs(ps_p[fin]), 1e-30)).max())
    bit2 = int((np.asarray(ps_b, dtype=np.float32).view(np.uint32)
                 == np.asarray(ps_p, dtype=np.float32).view(np.uint32)).sum())
    set_same = bool(np.allclose(np.sort(ps_b[fin]), np.sort(ps_p[fin]),
                                rtol=1e-4, atol=1e-30))
    # 判定顺序很重要：值集合相同 ≠ 顺序错。先按"同一位置比"的相对差定性，
    # 只有位置上也对不上、但排序后对得上，才是真的换了顺序。
    if bit2 == psz:
        verdict2 = "FFT 同源（逐位相同）"
    elif rel2 <= 1e-4:
        verdict2 = f"FFT 同源（{psz - bit2}/{psz} 个 bin 有 {rel2:.2g} 量级的浮点次序差）"
    elif set_same:
        verdict2 = "FFT 把 bin 换了顺序"
    else:
        verdict2 = "FFT 本身不同源"
    log(f"  T2 FFT({psz} bin，输入=板上帧本身): 逐位相同={bit2}/{psz} "
        f"max|Δ|={d2.max():.6g} max相对={rel2:.3g} 值集合相同={set_same} → {verdict2}")
    perm = ambig = None
    if bit2 != psz and rel2 > 1e-4:
        perm, ambig = bin_mapping(ps_b[fin], ps_p[fin])
        log(f"     板上bin→PCbin（前 24）: {perm[:24]}")
        log(f"     匹配不上的 {perm.count(-1)} 个 / 多值歧义 {ambig} 个")
    out["T2_fft"] = dict(bit_equal=bit2, size=int(psz), max_abs=float(d2.max()),
                         max_rel=rel2, same_multiset=set_same, perm=perm, ambiguous=ambig)

    # T3 mel 边界 bin
    if res["bins"] is not None:
        bb = [int(v) for v in res["bins"]["vals"]]
        pb = [int(v) for v in _mel_filterbank(SR, NUM_FILTERS, FFT_LENGTH, LOW_FREQ, HIGH_FREQ)]
        diff = [[i, int(bv), int(pv)] for i, (bv, pv) in enumerate(zip(bb, pb)) if bv != pv]
        log(f"  T3 mel 边界 bin: 板上 {len(bb)} 个 / PC {len(pb)} 个 "
            f"{'全部相同' if not diff else '不同的位置 ' + str(diff[:12])}")
        log(f"     板上: {bb}")
        log(f"     PC : {pb}")
        # 板上那份能否用"mel 端点被线性放大 c 倍"这一个参数完全解释？
        # 能（err=0）就说明这不是舍入差异，而是 mel↔hz 这条曲线本身不同源。
        c, c_err = None, None
        hi_hz = SR / 2 if HIGH_FREQ == 0 else HIGH_FREQ
        mel_end = 1127.0 * math.log(1.0 + hi_hz / 700.0)
        c_pred = None
        for cc in np.arange(1.0, 6.0, 0.0005):
            m = np.linspace(0.0, mel_end * cc, len(bb))
            hz = np.clip(700.0 * (np.exp(m / 1127.0) - 1.0), 0.0, hi_hz)
            hz[-1] -= 0.001
            p = np.floor((FFT_LENGTH + 1) * hz / SR).astype(int)
            e = int(np.abs(p - np.array(bb)).sum())
            if c_err is None or e < c_err:
                c, c_err, c_pred = cc, e, [int(v) for v in p]
        log(f"     单参数拟合：把 mel 端点乘 c={c:.4f} 能重现板上全部 {len(bb)} 个 bin，"
            f"残差={c_err}（0 = 完全由一个比例解释）→ 拟合曲线 {c_pred}")
        out["T3_bins"] = dict(board=bb, pc=pb, diff=diff,
                              scale_c=float(c), fit_err=int(c_err), fit_pred=c_pred)

    # T4 三角加权：用板上自己的 bins + 板上的功率谱，在 PC 上重算第 0 帧 mel
    frow = res["rows"].get(("F", res["mode"]), {}).get(0)
    if res["bins"] is not None and frow is not None and len(frow) == NUM_FILTERS:
        mel_b = (np.array(frow, dtype=np.uint32).view(np.float32)).astype(np.float64)
        mel_c = _apply_filterbank(np.asarray(ps_b[:psz], dtype=np.float32),
                                  np.array(res["bins"]["vals"], dtype=np.int32),
                                  NUM_FILTERS).astype(np.float64)
        d4 = np.abs(mel_b - mel_c)
        log(f"  T4 三角加权(第0帧): 逐位相同="
            f"{int((np.asarray(mel_b, dtype=np.float32).view(np.uint32) == np.asarray(mel_c, dtype=np.float32).view(np.uint32)).sum())}"
            f"/{NUM_FILTERS} max|Δ|={d4.max():.6g} @列{int(np.argmax(d4))} "
            f"板上{mel_b[int(np.argmax(d4))]:.6g} 重算{mel_c[int(np.argmax(d4))]:.6g}")
        out["T4_filterbank"] = dict(max_abs=float(d4.max()),
                                    worst=[int(np.argmax(d4)), float(mel_b[int(np.argmax(d4))]),
                                           float(mel_c[int(np.argmax(d4))])])
    return out


def _pair_rows(board_rows, pc):
    """取"板上行号升序、列数对得上"的那批行，板上按 float32 位模式还原"""
    pf = np.asarray(pc, dtype=np.float32)
    if pf.ndim == 1:
        pf = pf.reshape(-1, 1)
    idx = [r for r in sorted(board_rows)
           if r < pf.shape[0] and len(board_rows[r]) == pf.shape[1]]
    if not idx:
        return None, None, None
    bits = np.array([board_rows[r] for r in idx], dtype=np.uint32)
    return idx, bits.view(np.float32).astype(np.float64), pf[idx].astype(np.float64)


def zero_report(board_rows, pc, log):
    """mel 线性级里"0 有没有被 clamp 成 1e-10"是 log-mel 分歧的头号嫌疑：
    numpy::zero_handling 只改**恰好等于 0** 的单元（feature.hpp 里 mfe 返回前调一次），
    而 PC 侧 _apply_filterbank 会不会产出 0 取决于三角形滤波器的覆盖范围。
    这里把两侧的 0 / 1e-10 / 负数 / 非有限计数摊开——×1000 编码下这些全被压平成 0，
    改成位模式编码正是为了能看见它。"""
    idx, b, p = _pair_rows(board_rows, pc)
    if b is None:
        return None
    z10 = float(np.float32(1e-10))
    out = {}
    for nm, arr in (("board", b), ("pc", p)):
        out[nm] = dict(n=int(arr.size), zeros=int((arr == 0.0).sum()),
                       clamp1e10=int((arr == z10).sum()),
                       negatives=int((arr < 0.0).sum()),
                       nonfinite=int((~np.isfinite(arr)).sum()))
    log(f"  F 零值分布({len(idx)} 行): 板上 0={out['board']['zeros']} "
        f"1e-10={out['board']['clamp1e10']} 负={out['board']['negatives']} "
        f"非有限={out['board']['nonfinite']} / PC 0={out['pc']['zeros']} "
        f"1e-10={out['pc']['clamp1e10']} 负={out['pc']['negatives']} "
        f"非有限={out['pc']['nonfinite']}（共 {out['board']['n']} 个单元）")
    return out


def analyze(results, log):
    report = {}
    for mode, res in sorted(results.items()):
        log(f"\n----- 模式 {mode}（{MODE_NAME.get(mode, '?')}）-----")
        item = {"mode": mode, "name": MODE_NAME.get(mode, "?")}
        x = synth(mode)
        ck, pk = board_cksum(x), board_peak(x)
        same_in = (res["cksum"] == ck and res["peak"] == pk)
        log(f"  输入同源: 板上 ck={res['cksum']} peak={res['peak']} / PC ck={ck} peak={pk}"
            f" → {'✓ 逐位相同' if same_in else '✗ 不同，本级结论作废'}")
        item.update(input_same=bool(same_in), cksum=[res["cksum"], ck], peak=[res["peak"], pk])
        if res["score"]:
            item["score_board"] = res["score"]["score"]
            log(f"  板上分数 = {res['score']['score']:.3f} (err={res['score']['err']} "
                f"dsp={res['score']['dsp']}ms nn={res['score']['nn']}ms)")
        if res["audio"] is not None:
            fr = res["audio"]["frames"]
            bad = [k for k, v in fr.items()
                   if (k + 1) * MONO_FRAME <= WIN and not np.array_equal(
                       v, x[k * MONO_FRAME:(k + 1) * MONO_FRAME])]
            log(f"  字节核对: {len(fr)} 帧（缺 {res['audio']['missing']}），"
                f"与 PC 合成不一致的帧 {len(bad)} → "
                f"{'✓ 收到的每一帧都逐字节相同' if not bad else '✗ 有帧不同，输入不同源'}")
            item["bytes_same"] = (not bad) and res["audio"]["missing"] == 0
        if not same_in or res["meta"] is None:
            item["stages"] = "输入不同源或没有 DSP_META，跳过"
            report[mode] = item
            continue

        pc, nfr = pc_stages(x)
        log(f"  尺寸 板上 rows={res['meta']['rows']} nf={res['meta']['nf']} "
            f"nc={res['meta']['nc']} / PC rows={nfr} nf={NUM_FILTERS} nc={NUM_CEPSTRAL}")
        item["probe"] = fft_probe(res, x, log)
        stages = {}
        for tag in ("E", "F", "M", "Mf", "R", "Rf", "C", "Cf"):
            base = tag[:-1] if tag.endswith("f") else tag
            rows = res["rows"].get((base, mode))
            if not rows:
                if not tag.endswith("f"):
                    log(f"  {tag} {STAGE_NAME[tag]}: 板上没打出这一级")
                continue
            stages[tag] = cmp_rows(rows, pc[tag], f"{tag} {STAGE_NAME[tag]}", log)
        if ("F", mode) in res["rows"]:
            item["zero_clamp"] = zero_report(res["rows"][("F", mode)], pc["F"], log)
        if res["evalf"] and ("C", mode) in res["rows"]:
            crows = res["rows"][("C", mode)]
            c = np.array(np.array([crows[r] for r in sorted(crows)], dtype=np.uint32)
                         .view(np.float32), dtype=np.float64).reshape(-1)
            e = np.asarray(res["evalf"], dtype=np.float64)[:c.size]
            ok = np.isfinite(c) & np.isfinite(e)
            dd = float(np.abs(c[ok] - e[ok]).max()) if ok.sum() else None
            # EVALF 是"特征×1000 取整"的有损编码，量化误差 5e-4；超过 0.02 的单元才算对不上。
            # 用"超容差单元数"而不是 max：别的线程的日志会混进这条流，个别单元被污染
            # 不代表逐级导出没复刻生产路径（模式 2 就整条流都没接上）。
            n_bad = int((np.abs(c[ok] - e[ok]) > 0.02).sum()) if ok.sum() else int(ok.size)
            same = dd is not None and n_bad == 0
            log(f"  内部自检 DSPC vs EVALF: 可比 {int(ok.sum())}/{c.size} "
                f"max|Δ|={('%.4g' % dd) if dd is not None else 'n/a'} 超容差单元={n_bad} → "
                f"{'✓ 逐级导出复刻了生产路径' if same else '✗ 逐级导出与生产路径有 %d 个单元对不上' % n_bad}")
            item["internal"] = dict(cells=int(ok.sum()), max_abs=dd, n_bad=n_bad, same=bool(same))
        item["stages"] = stages
        report[mode] = item

    log("\n===== 判定 =====")
    okv = ("逐位一致", "仅数值差")
    for mode, it in sorted(report.items()):
        st = it.get("stages")
        if not isinstance(st, dict):
            log(f"  模式 {mode}: 无分级数据（输入同源={it.get('input_same')}）")
            continue
        # 每级同时看两版：np.log（ei_mfcc.py 原样）和"板上快速 log"复刻。
        # np.log 版分歧、快速 log 版一致 → 差异只来自 PC 复刻用了更准的对数，链路本身没错；
        # 两版都分歧 → 真结构性分歧，继续往上一级夹。
        parts = []
        for t in ("E", "F", "M", "R", "C"):
            if t not in st:
                continue
            ft = t + "f"
            parts.append(f"{t}: np.log={st[t]['verdict']}"
                         + (f" / 快速log={st[ft]['verdict']}" if ft in st else ""))
        bad = [t for t in ("E", "F", "M", "R", "C")
               if (t + "f" in st and st[t + "f"]["verdict"] not in okv)
               or (t + "f" not in st and t in st and st[t]["verdict"] not in okv)]
        log(f"  模式 {mode}（{it['name']}）:")
        for p in parts:
            log("    " + p)
        log("    → " + (f"第一个分歧级 = {bad[0]}" if bad else "五级全部一致（板 PC 同源）"))
        it["first_divergence"] = bad[0] if bad else None
    return report


def jsonable(v):
    if isinstance(v, float):
        return v if math.isfinite(v) else None
    return v


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modes", default="0,1,2,3,4,5")
    ap.add_argument("--echo", default="3", help="做逐字节回吐核对的模式（每个约 +9s 串口时间）")
    ap.add_argument("--port", default="COM9")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--outdir", default=os.path.join(ROOT, "logs", "diag"))
    ap.add_argument("--reboot", action="store_true", help="跑之前先复位板子")
    a = ap.parse_args()

    os.makedirs(a.outdir, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    logpath = os.path.join(a.outdir, f"kws_dsp_{stamp}.log")
    fh = open(logpath, "w", encoding="utf-8")

    def log(s):
        print(s)
        fh.write(s + "\n")
        fh.flush()

    modes = [int(v) for v in a.modes.split(",") if v != ""]
    echo_modes = {int(v) for v in a.echo.split(",") if v != ""}
    log(f"===== 已知答案自检 {datetime.now():%F %T} 模式 {modes} 回吐 {sorted(echo_modes)} =====")

    if a.reboot:
        import serial
        s = serial.Serial(a.port, a.baud, timeout=0.05)
        s.write(b"reboot\r\n")
        s.flush()
        s.close()
        log("复位中，等 12s…")
        time.sleep(12.0)

    results = {}
    try:
        collect(modes, echo_modes, a.port, a.baud, log, a.outdir, results)
        report = analyze(results, log)
    except Exception:
        import traceback
        log("!! 采集/比对中途异常，已收到的部分照样落盘：\n" + traceback.format_exc())
        report = {"aborted": True}

    board = {}
    for m, res in results.items():
        board[str(m)] = dict(cksum=res["cksum"], peak=res["peak"], score=res["score"],
                             meta=res["meta"], evalf_meta=res["evalf_meta"],
                             evalf=res["evalf"], dropped=res["dropped"],
                             bins=res["bins"], pp_hdr=res["pp_hdr"], math=res["math"],
                             chunks={f"{t}|{w}": {str(r): [jsonable(z) for z in vals]
                                                  for r, vals in sorted(rows.items())}
                                     for (t, w), rows in sorted(res["chunks"].items())},
                             unmatched_n=len(res["unmatched"]),
                             audio_frames=(res["audio"]["nframes"] if res["audio"] else None),
                             rows={f"{t}|{w}": {str(r): [jsonable(z) for z in vals]
                                                for r, vals in sorted(rows.items())}
                                   for (t, w), rows in sorted(res["rows"].items())})
    jpath = os.path.join(a.outdir, f"kws_dsp_{stamp}.json")
    with open(jpath, "w", encoding="utf-8") as f:
        json.dump({"stamp": stamp, "modes": modes, "echo": sorted(echo_modes),
                   "report": report, "board": board}, f, ensure_ascii=False, indent=1)
    rel = lambda p: os.path.relpath(os.path.abspath(p), ROOT).replace("\\", "/")
    log(f"\n结果已存 {rel(jpath)}")
    log(f"日志已存 {rel(logpath)}")
    fh.close()


if __name__ == "__main__":
    main()
