"""板载声路模拟（board_sim）：不需要真人再采集，自己造"板载喇叭→麦"路径的正样本。

思路（开源 KWS 的域随机化做法）：
1. 从已有板载采集（data/boardmic/*）估出板子的**频率响应 H(f)**（喇叭+麦的着色）
   和**底噪 RMS**（floor_p90 / 静音段）。
2. 用 pyroomacoustics 的 ShoeBox 图像声源法生成**随机 RIR**（随机房间/位置/RT60）。
3. 把干净 TTS 正样本 → 卷积 RIR → 施加 H(f) → 加底噪 → 增益/削波 → 得到"板载路径"音频。

用法：KWS_BOARD_SIM=<每干净正样本生成几条>（0=关）。
"""
import os
import csv as _csv
import numpy as np
from scipy.signal import fftconvolve

FS = 16000


def _src_path(clip, src_roots):
    for root in src_roots:
        p = os.path.join(root, clip)
        if os.path.exists(p):
            return p
    return None


def estimate_board_profile(capture_dirs, src_roots, verbose=False):
    """从板载采集估频响 H(f)（长度 FS/2+1 的幅度谱比，已平滑归一化）和底噪 RMS。"""
    from dataset_v9 import read_wav16k
    ratios, floors = [], []
    for d in capture_dirs:
        man = os.path.join(d, "manifest.csv")
        if not os.path.exists(man):
            continue
        for r in _csv.DictReader(open(man, encoding="utf-8-sig")):
            if r.get("label") != "1" or not r.get("clip"):
                continue
            fp = os.path.join(d, r.get("file", ""))
            if not os.path.exists(fp):
                continue
            src = _src_path(r["clip"], src_roots)
            if src is None:
                continue
            try:
                rec = read_wav16k(fp).astype(float)
                clean = read_wav16k(src).astype(float)
            except Exception:
                continue
            lag = float(r.get("lag_sec") or 0.0)
            dur = float(r.get("clip_sec") or 1.5)
            a, b = int(lag * FS), int((lag + dur) * FS)
            seg = rec[a:b]
            n = min(len(seg), len(clean))
            if n < int(0.5 * FS):
                continue
            w = np.hanning(n)
            S = np.abs(np.fft.rfft(clean[:n] * w))
            R = np.abs(np.fft.rfft(seg[:n] * w))
            m = S > 1e3
            if m.sum() < 32:
                continue
            ratio = np.zeros_like(S)
            ratio[m] = R[m] / S[m]
            # 归一化到固定长度 FS//2+1（不同片段 n 不同）
            target = FS // 2 + 1
            if len(ratio) != target:
                ratio = np.interp(np.linspace(0, 1, target),
                                  np.linspace(0, 1, len(ratio)), ratio)
            ratios.append(ratio)
            if r.get("floor_p90"):
                floors.append(float(r["floor_p90"]))
    if ratios:
        H = np.median(np.vstack(ratios), axis=0)
        H = np.convolve(H, np.ones(9) / 9, mode="same")
        med = np.median(H)
        if med > 0:
            H = H / med
    else:
        H = np.ones(FS // 2 + 1)
    prof = {
        "H": H.astype(np.float32),
        "noise_rms": float(np.median(floors)) if floors else 300.0,
        "n_seg": len(ratios),
        "ir": None,
    }
    # 优先用"实测 IR"（rig 播对数扫频 → 反卷积得到，含喇叭+房间+麦的完整传递函数）。
    # 有它就不再用随机 RIR + 估计频响——实测的准得多。
    ir_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "board_ir.npy")
    if os.path.exists(ir_path):
        try:
            prof["ir"] = np.load(ir_path).astype(np.float64)
        except Exception:
            prof["ir"] = None
    return prof


def random_rir(rng, rt60_range=(0.08, 0.45)):
    """pyroomacoustics ShoeBox 图像声源法生成随机 RIR（随机房间/位置/RT60）。"""
    from pyroomacoustics import ShoeBox
    from pyroomacoustics.parameters import Material
    L = [rng.uniform(2.0, 6.0), rng.uniform(2.0, 6.0), rng.uniform(2.2, 3.2)]
    rt60 = rng.uniform(*rt60_range)
    V = L[0] * L[1] * L[2]
    S = 2 * (L[0] * L[1] + L[0] * L[2] + L[1] * L[2])
    # Sabine 公式反推平均吸声系数：RT60 = 0.161*V / (S*alpha)
    alpha = float(np.clip(0.161 * V / (max(rt60, 0.02) * S), 0.05, 0.9))
    room = ShoeBox(L, fs=FS, materials=Material(energy_absorption=alpha),
                   max_order=int(rng.integers(6, 22)))
    try:
        room.add_source([rng.uniform(0.15, L[0] - 0.15),
                         rng.uniform(0.15, L[1] - 0.15),
                         rng.uniform(0.25, 1.1)])
        room.add_microphone([rng.uniform(0.15, L[0] - 0.15),
                             rng.uniform(0.15, L[1] - 0.15),
                             rng.uniform(0.25, 1.1)])
        room.compute_rir()
        ir = np.asarray(room.rir[0][0], dtype=float)
    except Exception:
        ir = np.zeros(1)
    if ir.size < 2:
        ir = np.array([1.0])
    ir = ir[:int(0.55 * FS)]
    if np.abs(ir).max() > 0:
        ir = ir[np.argmax(np.abs(ir)):]
        ir = ir / np.abs(ir).max() * rng.uniform(0.35, 1.0)
    return ir


def simulate_board(x, profile, rng, use_esc_noise=True):
    """把干净音频 x(int16) 模拟成板载路径音频。"""
    xf = x.astype(np.float64)
    if xf.size < FS // 2:
        return x
    _ir = profile.get("ir")
    if _ir is not None and len(_ir) > 1:
        # 实测 IR（板子真实传递函数）：直接卷积，只做轻度随机化——保真优先
        y = fftconvolve(xf, _ir * rng.uniform(0.6, 1.4))
        if rng.random() < 0.5:                     # 偶尔再叠一层随机混响，增房间多样性
            y = fftconvolve(y, random_rir(rng, (0.05, 0.22)))
        if rng.random() < 0.25:                    # 偶尔轻微时移（不同摆放距离）
            sh = int(rng.integers(0, int(0.02 * FS)))
            y = np.concatenate([np.zeros(sh), y])
    else:
        # 退回：随机 RIR + 估计频响
        ir = random_rir(rng)
        y = fftconvolve(xf, ir)[:xf.size + ir.size]
        # 施加板子频响 H(f)
        n = 1 << int(np.ceil(np.log2(max(len(y), 1024))))
        Y = np.fft.rfft(y, n)
        H = np.asarray(profile["H"], dtype=float)
        if len(H) != len(Y):
            H = np.interp(np.linspace(0, 1, len(Y)), np.linspace(0, 1, len(H)), H)
        y = np.fft.irfft(Y * H, n)[:len(y)]
    # 加噪（底噪白噪 + 可选 ESC-50 瞬态）
    if rng.random() < 0.9:
        scale = rng.uniform(0.25, 3.0)
        y = y + rng.normal(0.0, profile["noise_rms"] * scale, len(y))
    if use_esc_noise and rng.random() < 0.35:
        try:
            import glob as _glob
            fs_ = sorted(_glob.glob(os.path.join(os.path.dirname(__file__), "..", "data", "transient", "*.wav")))
            if fs_:
                from dataset_v9 import read_wav16k as _rw
                nz = _rw(fs_[int(rng.integers(0, len(fs_)))]).astype(float)
                if nz.size > 0:
                    st = int(rng.integers(0, max(1, len(y) - nz.size)))
                    g = rng.uniform(0.05, 0.4)
                    y[st:st + nz.size] += nz * g
        except Exception:
            pass
    # 增益到目标峰值 + 轻微削波
    peak = np.abs(y).max()
    if peak > 0:
        target = rng.uniform(9000.0, 30000.0)
        y = y * (target / peak)
    return np.clip(y, -32768, 32767).astype(np.int16)
