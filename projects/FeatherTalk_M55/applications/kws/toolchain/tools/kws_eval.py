"""kws_eval.py — 板上分数 / 板上 NN 输入特征 / PC python 复刻 三方对账

为什么需要（2026-10-19）：
  同一批板采环境音，板上判定 554 命中/小时，而 PC 复刻链路（training/ei_mfcc.py +
  同一个 int8 tflite）预测 0 命中。两边的"同源"从来只是**声明**，没做过数值对账。
  误触的根因只可能落在三段之一：
      A. DSP 段：板上 MFCC（含 cmvnw）≠ PC 复刻 MFCC
      B. NN 段：同一份特征，板上 NN 输出 ≠ PC tflite 输出（量化/解释差异）
      C. 模型段：两边一致，但分数本来就高 ⇒ 是模型/阈值问题，不是链路 bug
  板上 `kws eval <秒>` 在同一段连续 RAM 音频上，对每一窗依次输出
      EVALW <窗> <千分比分数> err= dsp= nn= ck=<窗校验和> peak=<峰值>  ← 板上完整推理
      EVALF <窗> <units> <rows> <cols> <ck> + units 个 (特征×1000) 整数  ← 板上 NN 真实输入
      再走 hex 回吐把**同一段音频**交给 PC（每行带帧号，丢行可检出并补零对齐）
  校验和先回答"PC 手里这段音频是不是板上打分那段"，再谈差异归属——上一版没有校验和，
  串口丢 7/500 行就让整份对账错位，得出的"不同源"结论不可用。

用法（两段式：串口用 default 环境、TF 推理用 kws 环境，两个环境不合并）:
  <default>/python.exe tools/kws_eval.py 5                          # ① 采
  <kws>/python.exe     tools/kws_eval.py --analyze <stamp|json路径>  # ② 对账（需 tensorflow）
输出: logs/diag/kws_eval_<stamp>.{txt,wav,json}
"""
import argparse
import glob
import json
import os
import re
import sys
import time
import wave
from datetime import datetime

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))          # toolchain/
sys.path.insert(0, os.path.join(ROOT, "training"))
sys.path.insert(0, HERE)
from kws_console import force_utf8                        # noqa: E402
from ei_mfcc import ei_mfcc                                # noqa: E402

force_utf8()

SR = 16000
WIN = 16000                       # = EI_CLASSIFIER_RAW_SAMPLE_COUNT = KWS_WINDOW_MS
MONO_FRAME = 160
NCEP = 13
EVALW_RE = re.compile(r"^EVALW (\d+) (-?\d+) err=(-?\d+) dsp=(-?\d+) nn=(-?\d+) "
                      r"ck=(\d+) peak=(-?\d+)")
EVALF_RE = re.compile(r"^EVALF (\d+) (\d+) (\d+) (\d+) (\d+)")
HEXCH = set("0123456789abcdefABCDEF")
FRAME_RE = re.compile(rb"K([0-9a-fA-F]{4})([0-9a-fA-F]{640})")


class Tee:
    """串口原始流同时留在内存和磁盘：内存那份用来重建被插话截断的帧行，
    磁盘那份出问题时能直接看到是谁插进来的"""

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


def relname(p):
    return os.path.relpath(os.path.abspath(p), ROOT).replace("\\", "/")


def board_cksum(x):
    """复刻固件 cap_cksum：int16 逐个符号扩展后累加，取 uint32 回绕"""
    return int(np.asarray(x, dtype=np.int64).sum() & 0xFFFFFFFF)


def board_peak(x):
    v = np.abs(np.asarray(x, dtype=np.int32))
    return int(v.max()) if v.size else 0


# ---------------- ① 采集（只需 pyserial） ----------------

def hex_run(s):
    """取一行开头的连续 hex 段（帧头的 'K' 记进来），后面跟着的非 hex 一律当插话丢掉。

    原始流实测（kws_eval_*.raw）：一帧行被插断时，前半条记录长这样
        K0005004400a6fff...<我的 hex>...x=1135 rx=3419 err=0 local=0x0000000e peer=0
    后半条是 <剩下的纯 hex>，中间夹的是别的线程的 [FeatherTalk]/[BOND] 日志和它的换行。
    所以"整条记录必须全是 hex"的判据会把前半条丢掉（=真丢帧），只取开头 hex 段才对。
    """
    had_k = s[:1] == "K"
    if had_k:
        s = s[1:]
    n = 0
    while n < len(s) and s[n] in HEXCH:
        n += 1
    # 太短的 hex 段是别的日志行的开头（EVALW 的 'E' 也是 hex），混进来会把帧错位
    if n < (5 if had_k else 8):
        return ""
    return ("K" if had_k else "") + s[:n]


def run_board(seconds, port, baud, log, rawpath=None):
    """触发板上 eval，收 EVALW/EVALF + 同段音频 hex 回吐（行带帧号，丢行补零对齐）"""
    import serial
    from kws_uart import LineReader
    s = serial.Serial(port, baud, timeout=0.05)
    s.dtr = True
    s.rts = True
    time.sleep(1.0)
    tee = Tee(rawpath)
    lr = LineReader(s, tee=tee)

    def probe(cmd, timeout=6.0):
        """下一条命令并等到 msh 提示符回来 —— 没提示符就是没执行，当场报错而不是静默跑完"""
        s.reset_input_buffer()
        s.write(cmd)
        s.flush()
        got, deadline = [], time.perf_counter() + timeout
        while time.perf_counter() < deadline:
            t = lr.get(0.2)
            if t is None:
                continue
            got.append(t)
            log(f"  << {t[:90]}")
            # 提示符可能被别的线程插成半行（[BOND]/[FeatherTalk] 的日志会和它拼在同一行），
            # 所以只看"这一行里有没有 msh"，不要求行首；命令回显那行先跳过
            if "msh" in t and "kws" not in t:
                return got
        raise RuntimeError(f"命令 {cmd!r} 在 {timeout:.0f}s 内没等到 msh 提示符"
                           f"（收到 {len(got)} 行）—— 板子可能卡在回吐/评测里，先复位再采")

    probe(b"\r\n", 8.0)                      # 只要板子应答（回显 msh />）就证明串口+固件活着
    probe(b"kws info\r\n", 12.0)             # 顺带留下改前的推理速率/交付率读数
    # 这里**不发 kws stop**：kws.cpp:741 kws_stop() 会 rt_device_close 麦克风，
    # 之后线程重新 open 拿不到任何样本（实测：eval 只打到 KWS_EVAL 就再无输出，
    # 10s 的录制永远填不满）。不发 stop 时走的是"设备一直开着"的路径，eval 正常。
    # 顺带记一笔产品缺陷：暂停/恢复唤醒词会把麦打死，需要修驱动或改 stop 语义。
    probe(b"\r\n", 8.0)                      # 再对齐一次提示符：残留半行会把 eval 命令拼坏
    s.write(f"kws eval {seconds}\r\n".encode("ascii"))
    s.flush()

    windows, feats, meta = {}, {}, {"feats": {}}
    cur_win, cur_feat = None, []
    unmatched = []
    expect_frames = 0
    t0 = idle = time.perf_counter()
    nfr = seconds * SR // MONO_FRAME
    cap = seconds + 20 + 60 + nfr * (MONO_FRAME * 4 + 6) / (baud / 10.0)
    # 录制该在 seconds+余量 内出 KWS_EVAL_BEGIN；没有就是板上采集线程空转（设备状态坏了，
    # 且这次采集会把 s_cap_frames 挂住 → 后续 cap_start 一律 EBUSY）。工具自己 reboot 重试一次，
    # 不等人动手。
    eval_seen, restarts, stall_by = False, 0, t0 + seconds + 25
    while time.perf_counter() - t0 < cap:
        t = lr.get(0.2)
        if t is None:
            if not eval_seen and restarts < 2 and time.perf_counter() > stall_by:
                restarts += 1
                log(f"!! {seconds + 25}s 没等到 KWS_EVAL_BEGIN：板上采集线程没拿到样本，"
                    f"复位后重试（第 {restarts} 次）")
                s.write(b"reboot\r\n")
                s.flush()
                s.close()
                time.sleep(12.0)                 # 等 boot + 应用自动起 KWS
                s = serial.Serial(port, baud, timeout=0.05)
                s.dtr = True
                s.rts = True
                lr = LineReader(s, tee=tee)
                time.sleep(1.0)
                s.write(f"kws eval {seconds}\r\n".encode("ascii"))
                s.flush()
                t0 = idle = time.perf_counter()
                stall_by = t0 + seconds + 25
                continue
            if time.perf_counter() - idle > 25:
                log("!! 25s 无输出，提前收")
                break
            continue
        idle = time.perf_counter()
        if cur_win is not None:
            nums = []
            ok = True
            for tok in t.split():
                try:
                    nums.append(int(tok))
                except ValueError:
                    ok = False
                    break
            if ok and nums:
                cur_feat.extend(nums)
                units = meta["feats"][cur_win]["units"]
                if len(cur_feat) >= units:
                    feats[cur_win] = np.array(cur_feat[:units], dtype=np.float32) / 1000.0
                    cur_win, cur_feat = None, []
                continue
        m = EVALW_RE.match(t)
        if m:
            windows[int(m.group(1))] = dict(score=int(m.group(2)) / 1000.0,
                                            err=int(m.group(3)), dsp=int(m.group(4)),
                                            nn=int(m.group(5)), cksum=int(m.group(6)),
                                            peak=int(m.group(7)))
            continue
        m = EVALF_RE.match(t)
        if m:
            cur_win = int(m.group(1))
            meta["feats"][cur_win] = dict(units=int(m.group(2)), rows=int(m.group(3)),
                                          cols=int(m.group(4)), cksum=int(m.group(5)))
            cur_feat = []
            continue
        if t.startswith("KWS_EVAL_BEGIN"):
            eval_seen = True
            log("板上评测中: " + t)
            _, w, nwin, cap_len, ck, pk = t.split()
            meta.update(win=int(w), nwin=int(nwin), cap_len=int(cap_len),
                        cksum=int(ck), peak=int(pk))
            continue
        if t.startswith("EVALF_ERR"):
            log("!! 板上特征导出报错: " + t)
            continue
        if t.startswith("KWS_EVAL_END"):
            log("评测结束，开始回吐音频")
            continue
        if t.startswith("KWS_DUMP_BEGIN"):
            log("音频回吐: " + t)
            f = t.split()
            expect_frames = int(f[2])
            if len(f) > 3:
                meta["dump_cksum"], meta["dump_peak"] = int(f[3]), int(f[4])
            continue
        if t.startswith("KWS_DUMP_END"):
            log("回吐结束（帧行在串口原始流上重建）")
            break
        # 走到这里说明这行谁也不是：不记下来就只剩"静默 146 秒"（上一版就是这么瞎的）
        unmatched.append(f"{time.perf_counter() - t0:6.1f}s {t[:80]!r}")

    if unmatched:
        log(f"  未识别行 {len(unmatched)} 条，前 20 条：")
        for u in unmatched[:20]:
            log("    " + u)

    tee.close()
    s.close()
    # 帧行重建：**在字节流层面**做，不在行层面做。
    # 一帧行 646 字节 > 控制台单次写缓冲（RT_CONSOLEBUF_SIZE），rt_kprintf 会分块写，
    # 而应用线程（drv_pdm 的 [BOND]/[FeatherTalk] tx/rx）在同一个互斥量后面插进来，
    # 于是**一行音频被别人的换行符拦腰截断**（实测 14/1000，行时长 ~56ms 的窗口撞 2%）。
    # 行解析只能看到"短行"，尾部那半截没有 'K' 前缀，静默丢掉 ⇒ 音频时间压缩 ⇒ 对账错位。
    # 所以这里把噪声行整条丢掉、把剩下的 hex 片段拼回流，再按 K+4hex帧号+640hex 逐个取帧。
    pieces = []
    dump_at = tee.data.find(b"KWS_DUMP_BEGIN")
    for rec in (tee.data[dump_at:] if dump_at >= 0 else tee.data).split(b"\n"):
        run = hex_run(rec.strip(b"\r").decode("ascii", "ignore"))
        if run:
            pieces.append(run)
    stream = "".join(pieces).encode("ascii")
    frames, dup = {}, 0
    pos = 0
    while True:
        j = stream.find(b"K", pos)
        if j < 0:
            break
        m = FRAME_RE.match(stream, j)
        if m:
            idx = int(m.group(1), 16)
            if idx in frames:
                dup += 1
            else:
                frames[idx] = np.frombuffer(bytes.fromhex(m.group(2).decode()), dtype=">i2")
            pos = m.end()
        else:
            pos = j + 1
    if not frames:
        raise RuntimeError("没收到音频（串口/固件异常）")
    hi = max(frames)
    lost = [k for k in range(hi + 1) if k not in frames]
    audio = np.zeros((hi + 1) * MONO_FRAME, dtype=np.int16)
    for k, v in frames.items():
        audio[k * MONO_FRAME:(k + 1) * MONO_FRAME] = v
    log(f"帧重建：{len(frames)} 帧取自原始流（重复帧号 {dup}），期望 {expect_frames}")
    if lost:
        log(f"  ⚠ 真丢帧 {len(lost)} 个（{lost[:12]}…），已在原位补零保持窗对齐")
    return windows, feats, meta, audio, lost


def collect(a, log, outdir):
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    seconds = min(a.seconds, 15)
    log(f"===== 采集 {datetime.now():%F %T}  录 {seconds}s  ({a.port}@{a.baud}) =====")
    rawpath = os.path.join(outdir, f"kws_eval_{stamp}.raw")
    windows, feats, meta, audio, lost = run_board(seconds, a.port, a.baud, log, rawpath)
    log(f"串口原始流 {relname(rawpath)}（行被截时到里面查是谁插进来的）")
    wav_path = os.path.join(outdir, f"kws_eval_{stamp}.wav")
    with wave.open(wav_path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(audio.astype("<h").tobytes())
    log(f"音频已存 {relname(wav_path)}  {len(audio)} 样本 = {len(audio)/SR:.2f}s（名义 {seconds}s）")

    # 采集完整性的第一判据：整段校验和 / 峰值对不对得上
    if "dump_cksum" in meta:
        ok = board_cksum(audio) == meta["dump_cksum"] and board_peak(audio) == meta["dump_peak"]
        log(f"整段校验 板上 ck={meta['dump_cksum']} peak={meta['dump_peak']}  "
            f"PC ck={board_cksum(audio)} peak={board_peak(audio)} → "
            + ("✓ 同一段音频" if ok else "✗ 不一致（补零位会拉低峰值但不改和）"))

    json_path = os.path.join(outdir, f"kws_eval_{stamp}.json")
    with open(json_path, "w", encoding="utf-8") as f:
        json.dump(dict(stamp=stamp, seconds=seconds, meta=meta, lost_frames=lost,
                       board_windows={str(k): v for k, v in windows.items()},
                       feats={str(k): v.tolist() for k, v in feats.items()},
                       wav=os.path.abspath(wav_path), audio_samples=int(len(audio))), f, indent=1)
    log(f"板上原始数字 {relname(json_path)}")
    log(f"\n下一步（需 TF 的环境）：python tools/kws_eval.py --analyze {stamp}")
    return stamp


# ---------------- ② 对账（需 tensorflow） ----------------

def load_wav16k(path):
    with wave.open(path) as w:
        assert w.getnchannels() == 1 and w.getframerate() == SR, f"{path}: 需 16k mono"
        return np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)


def score_from_features(scorer, feat):
    """给定浮点特征 → 同一个 int8 模型 → 分数（隔离 NN 段）"""
    iscale, izp, oscale, ozp = scorer.q
    q = np.clip(np.round(feat / iscale) + izp, -128, 127).astype(np.int8)
    scorer.it.set_tensor(scorer.inp["index"], q.reshape(scorer.inp["shape"]))
    scorer.it.invoke()
    y = scorer.it.get_tensor(scorer.out["index"]).astype(np.float32).reshape(-1)
    return float((y[scorer.idx] - ozp) * oscale)


def feat_diff(a, b):
    m = min(len(a), len(b))
    d = np.abs(np.asarray(a[:m], dtype=np.float32) - np.asarray(b[:m], dtype=np.float32))
    wi = int(np.argmax(d))
    return dict(max_abs=float(d.max()), mean_abs=float(d.mean()),
                per_cepstral_mean=[float(d[k:m:13].mean()) for k in range(min(13, m))],
                worst_idx=wi, worst_a=float(a[wi]), worst_b=float(b[wi]),
                rel_l1=float(d.sum() / max(1e-6, np.abs(b[:m]).sum())),
                corr=float(np.corrcoef(np.asarray(a[:m], dtype=np.float64),
                                       np.asarray(b[:m], dtype=np.float64))[0, 1]))


def analyze(a, log, outdir):
    src = a.analyze
    if not src.endswith(".json"):
        cand = glob.glob(os.path.join(outdir, f"kws_eval_{src.replace('kws_eval_', '')}*.json")) \
            or glob.glob(os.path.join(outdir, f"*{src}*.json"))
        if not cand:
            raise SystemExit(f"找不到 {src} 对应的 json（{relname(outdir)}）")
        src = max(cand, key=os.path.getmtime)
    data = json.load(open(src, encoding="utf-8"))
    wav = data.get("wav")
    if not wav or not os.path.exists(wav):
        wav = os.path.join(outdir, f"kws_eval_{data['stamp']}.wav")
    audio = load_wav16k(wav)
    meta = data["meta"]
    win = int(meta.get("win", WIN))
    windows = {int(k): v for k, v in data["board_windows"].items()}
    feats = {int(k): np.array(v, dtype=np.float32) for k, v in data["feats"].items()}

    log(f"===== 三方对账 {datetime.now():%F %T} =====")
    log(f"数据    : {relname(src)}")
    log(f"音频    : {relname(wav)}  {len(audio)} 样本 = {len(audio)/SR:.2f}s（板上录 {data['seconds']}s）")
    log(f"板上窗长 : {win} / 评了 {meta.get('nwin')} 窗 / RAM {meta.get('cap_len')} 样本 / "
        f"丢帧补零 {len(data.get('lost_frames', []))} 个")
    log(f"模型    : {relname(a.model)}  标签={a.label}  阈值=0.50")

    from kws_validate import TfliteScorer
    scorer = TfliteScorer(a.model, a.label)

    nwin = len(audio) // win
    log("\n---- 0. 窗对齐校验（板上 ck vs PC 在同一样本位置重算）----")
    aligned = []
    for i in range(nwin):
        seg = audio[i * win:(i + 1) * win]
        b = windows.get(i)
        if not b:
            continue
        pc_ck, pc_pk = board_cksum(seg), board_peak(seg)
        okc, okp = pc_ck == b["cksum"], pc_pk == b["peak"]
        aligned.append(okc)
        log(f"  窗 {i}: ck 板上 {b['cksum']:>11} / PC {pc_ck:>11} {'✓' if okc else '✗'}   "
            f"peak 板上 {b['peak']:>6} / PC {pc_pk:>6} {'✓' if okp else '✗'}"
            + ("   ← 丢帧补零处，分数不可比" if not okc else ""))
    if not all(aligned):
        log("  ⚠ 有窗对不上：这些窗的分数/特征差异里混有串口丢帧，不能单独归因给链路")

    log("\n---- 逐窗分数 ----")
    log(f"{'窗':>3} {'板上':>8} {'PC端到端':>9} {'Δ':>7} {'PC(板上特征)':>12} {'ck':>3} {'dsp':>5} {'nn':>5}  备注")
    rows = []
    for i in range(nwin):
        seg = audio[i * win:(i + 1) * win]
        pc = scorer.score_window(seg)
        b = windows.get(i)
        bd = b["score"] if b else None
        okc = (b and board_cksum(seg) == b["cksum"])
        units = (win // 320) * NCEP
        pcf = score_from_features(scorer, feats[i]) if (i in feats and feats[i].size == units) else None
        note = []
        if bd is not None and bd > 0.5:
            note.append("板>阈")
        if pc > 0.5:
            note.append("PC>阈")
        if pcf is not None and pcf > 0.5:
            note.append("PC(板特征)>阈")
        if not okc:
            note.append("未对齐")
        rows.append(dict(idx=i, board=bd, pc=pc, pc_from_feat=pcf, ck_ok=bool(okc)))
        log(f"{i:>3} "
            + (f"{bd:8.3f}" if bd is not None else f"{'-':>8}")
            + f" {pc:9.3f}"
            + (f" {bd-pc:+7.3f}" if bd is not None else f"{'':>7}")
            + (f" {pcf:12.3f}" if pcf is not None else f"{'-':>12}")
            + f" {'✓' if okc else '✗':>3}"
            + f" {windows.get(i,{}).get('dsp','-'):>5} {windows.get(i,{}).get('nn','-'):>5}"
            + "  " + ",".join(note))

    log("\n---- A. DSP 段：板上 NN 真实输入 vs PC ei_mfcc（同窗同音频）----")
    for i, f in sorted(feats.items()):
        info = meta.get("feats", {}).get(str(i), {})
        pc = ei_mfcc(audio[i * win:(i + 1) * win], flatten=True, sample_limit=win).astype(np.float32)
        log(f"  窗 {i}: 板上 rows={info.get('rows')} cols={info.get('cols')} units={info.get('units')}"
            f"（收到 {len(f)} 个浮点）；PC ei_mfcc = {pc.shape[0]} 个")
        if len(f) != pc.shape[0]:
            log("        ✗ 维度不一致 —— 本身就是 DSP 段差异的硬证据")
            continue
        d = feat_diff(f, pc)
        log(f"        最大绝对差 {d['max_abs']:.4f}（下标 {d['worst_idx']}：板上 {d['worst_a']:.4f} / PC {d['worst_b']:.4f}）"
            f"  平均 {d['mean_abs']:.4f}  相对差 L1 {d['rel_l1']*100:.2f}%  相关系数 {d['corr']:.3f}")
        log("        逐 cepstral 平均差：" + "  ".join(f"c{k}={v:.4f}" for k, v in enumerate(d["per_cepstral_mean"])))
        log(f"        → {'判定同源' if d['rel_l1'] < 0.02 else '判定不同源（差异量级足以翻转分数）'}")
        if rows and i < len(rows):
            rows[i]["feat"] = d

    log("\n---- B. NN 段：特征固定为板上那份时，板上分数 vs PC 分数 ----")
    paired = [r for r in rows if r["board"] is not None and r["pc_from_feat"] is not None]
    if paired:
        for r in paired:
            log(f"  窗 {r['idx']}: 板上 {r['board']:.3f}  vs  PC(板上特征) {r['pc_from_feat']:.3f}  "
                f"Δ {r['board']-r['pc_from_feat']:+.3f}{'（该窗未对齐，仅参考）' if not r['ck_ok'] else ''}")
    else:
        log("  （无 EVALF 窗，跳过）")

    log("\n---- C. 模型段：PC 端到端在板采环境音上的读数 ----")
    allb = [r["board"] for r in rows if r["board"] is not None]
    allpc = [r["pc"] for r in rows]
    if allpc:
        log(f"  PC ：{len(allpc)} 窗  峰值 {max(allpc)*100:.1f}%  超阈 {sum(1 for x in allpc if x > 0.5)} 窗")
    if allb:
        log(f"  板上：{len(allb)} 窗  峰值 {max(allb)*100:.1f}%  超阈 {sum(1 for x in allb if x > 0.5)} 窗")
    log("  音频能量核对：板上窗峰值 peak 见上表；若 peak≈0 或分数在静音窗仍然很高，"
        "先查采集通道是不是拿到了一条静音/直流的数据")
    log("\n判定规则：0 段不对齐 → 先修串口回吐，别谈归因；A 大 → 差异在 DSP；"
        "A 小而 B 大 → NN/量化解释；A、B 都小而 C 高 → 模型本身在这段音频上就给高分")

    out = os.path.join(outdir, f"kws_eval_{data['stamp']}_recon.json")
    with open(out, "w", encoding="utf-8") as f:
        json.dump(dict(stamp=data["stamp"], rows=rows, meta=meta,
                       feats={k: None for k in feats}), f, indent=1)
    log(f"对账数字: {relname(out)}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("seconds", nargs="?", type=int, default=5)
    ap.add_argument("--port", default="COM9")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--model", default=os.path.join(ROOT, "training", "out", "kws_xiaomai.tflite"))
    ap.add_argument("--label", default="xiaomai")
    ap.add_argument("--analyze", default=None, help="只做事后对账（json 名或 stamp），不占串口")
    ap.add_argument("--outdir", default=os.path.join(ROOT, "logs", "diag"))
    a = ap.parse_args()

    os.makedirs(a.outdir, exist_ok=True)

    def mklog(p):
        fh = open(p, "w", encoding="utf-8")

        def log(s=""):
            print(s)
            fh.write(str(s) + "\n")
            fh.flush()
        return log, fh

    if a.analyze:
        log, fh = mklog(os.path.join(a.outdir, f"kws_eval_analyze_{datetime.now():%Y%m%d_%H%M%S}.txt"))
        analyze(a, log, a.outdir)
    else:
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        log, fh = mklog(os.path.join(a.outdir, f"kws_eval_{stamp}.txt"))
        collect(a, log, a.outdir)
    fh.close()


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")
    os.environ.setdefault("OMP_NUM_THREADS", "1")
    os.environ.setdefault("TF_ENABLE_ONEDNN_OPTS", "0")
    main()
