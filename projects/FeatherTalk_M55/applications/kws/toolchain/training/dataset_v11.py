"""dataset_v11.py — 逐窗标签：把 v9/v10 的"文件级标签泄漏"从配方里根除

**为什么必须改**（实证见 tools/kws_silence_audit.py 报表 logs/diag/silence_audit2.txt）：
  v9/v10 的 windows() 把整份文件的每个滑窗都打上该文件的标签，还要给不足 1s 的部分补零。
  于是一条 1.68s 的 TTS 正样本里，开头静音、结尾补零的窗也被当成「小麦小麦」正例喂进训练。
  审计实测这类低能量正样本窗（RMS 20~300）在 v9 模型上拿 0.879~0.996 分——板上安静房间
  持续高分（空场 10s 无洞采集 18.8% 窗 ≥0.5）与这个方向完全一致。

**v11 的标签口径**：
  · 机内声学语料（rig 的 traindata 阶段产物，data/boardmic/<stamp>/manifest.csv）：
    词的位置由「激励包络 ↔ 采集包络互相关」定出（lag/reps/loop_gap 在 manifest 里），
    只有与某次放音重叠 ≥0.5s 的窗记正例；重叠 0~0.5s 的**骑窗**整块丢弃（半截词既不是
    唤醒词也不是噪声，硬归任一类都是造假）；同一次采集里词与词之间的静音、房间底噪一律
    记负例——静音第一次被显式教成 noice，而不是顺着文件标签变成 xiaomai。
  · 串口丢帧的位置（hole_spans，帧号）标出来，压到洞的窗直接丢：补零不是音频。
  · 旧语料（TTS/板采）只走 VAD 逐窗：正样本文件里的**有声窗**才当正例，静音窗归负例。
  · 留出集按**整份源采集**切（绝不参与训练），并把词区/非词区剪成独立短文件写成
    data/holdout_v11/manifest.csv，让 kws_validate.py 用文件级口径也能吃。

用法：
  <kws>/python.exe dataset_v11.py                  # 打印构成并写留出集
  KWS_VERSION=v11 <kws>/python.exe train_kws.py    # 训练（train_kws 按版本挑本模块）
"""
import csv
import glob
import os

import numpy as np

from ei_mfcc import ei_mfcc
from dataset_v9 import read_wav16k, _aug

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DATA = os.path.join(ROOT, "data")
LEGACY = os.path.join(DATA, "wakeword")
REAL = os.path.join(DATA, "real")
HOLD = os.path.join(DATA, "holdout_v11")

# 响度归一化目标（0=关闭）。开启时必须同步改板端 kws.cpp 的 DSP 前处理。
NORM_LEVEL = float(os.environ.get("KWS_NORM_LEVEL", "0") or 0)

SR = 16000
WIN = 16000
STRIDE = 800                 # 50ms = 验收 --jitter 10 的同一套窗相位
FRAME = 160                  # 回吐帧 = 10ms
POS_OVERLAP = 0.5 * WIN      # 与词重叠半个窗以上才算正例
VAD_FRAC = 0.15              # 有声判据：帧 RMS > 该文件 p95 的 15%


def newest_corpus():
    """缺省取 data/boardmic 下最新一次的语料；KWS_CORPUS 可指到具体 manifest.csv 或其目录。"""
    p = os.environ.get("KWS_CORPUS", "")
    if p:
        return p if p.endswith(".csv") else os.path.join(p, "manifest.csv")
    cands = sorted(glob.glob(os.path.join(DATA, "boardmic", "*", "manifest.csv")))
    return cands[-1] if cands else ""


def parse_holes(spec, n_frames):
    """manifest 的 hole_spans（'83-84,910-911' 帧号闭区间）→ 样本级无效位掩码。"""
    bad = np.zeros(n_frames, dtype=bool)
    for part in (spec or "").split(","):
        if "-" not in part:
            continue
        a, b = part.split("-")
        bad[int(a):int(b) + 1] = True
    return bad


def word_spans(row, limit):
    """一次采集里每次放音在缓冲中的样本区间。lag 由互相关从音频本身推出（不是时钟）。"""
    try:
        lag = float(row["lag_sec"])
        clip = float(row["clip_sec"])
        gap = float(row["loop_gap"])
        reps = int(row["reps"] or 0)
    except (KeyError, TypeError, ValueError):
        return []
    out = []
    for i in range(max(0, reps)):
        a = int(round((lag + i * (clip + gap)) * SR))
        b = int(round((lag + i * (clip + gap) + clip) * SR))
        if a < limit and b > a:
            out.append((a, min(b, limit)))
    return out


def vad_spans(x):
    """旧语料的逐窗有声判定：帧 RMS 双阈值 + 合并短间隔 + 丢短段。"""
    a = np.abs(np.asarray(x, dtype=np.float64))
    n = (a.size // FRAME) * FRAME
    f = a[:n].reshape(-1, FRAME).std(axis=1)
    if f.size == 0:
        return []
    hi = float(np.percentile(f, 95))
    thr = max(VAD_FRAC * hi, 1.0)
    on = f > thr
    spans, i = [], 0
    while i < len(on):
        if not on[i]:
            i += 1
            continue
        j = i
        while j < len(on) and on[j]:
            j += 1
        if j - i >= int(0.3 * SR / FRAME):
            if spans and (i - spans[-1][1]) * FRAME < int(0.12 * SR):
                spans[-1] = (spans[-1][0], j * FRAME)
            else:
                spans.append((i * FRAME, j * FRAME))
        i = j
    return spans


def _overlap(a0, a1, spans):
    """窗 [a0,a1) 与词区间列表的最大重叠样本数（不叠 = 0）。"""
    if not spans:
        return 0.0
    return max(0.0, max(min(a1, b1) - max(a0, b0) for b0, b1 in spans))


def label_windows(x, spans, bad_frames=None, tight=True):
    """一段音频 → [(窗起点, 标签)]；tight=True 时丢弃"骑窗"与压到丢帧洞的窗。"""
    n = len(x)
    if n < WIN:
        return ([] if tight else ([(0, 1 if spans else 0)])), None
    out = []
    for s in range(0, n - WIN + 1, STRIDE):
        e = s + WIN
        if bad_frames is not None and bad_frames[s // FRAME:e // FRAME + 1].any():
            continue                        # 压到串口丢帧：补零不是音频，整窗丢
        ovl = _overlap(s, e, spans)
        if not tight:
            out.append((s, 1 if spans else 0))
            continue
        if ovl >= POS_OVERLAP:
            out.append((s, 1))
        elif ovl == 0:
            out.append((s, 0))              # 词之间/词外的静音与底噪 = 负例（泄漏修复点）
        # 0 < ovl < 半个窗：骑窗，半截词，两类都不算 → 丢
    return out, None


def load_corpus(manifest=None):
    """→ [(wav路径, 标签, 词区间列表, 无效帧掩码, 源文件号, 说明)]，按整份采集给 grp。

    负样本里剔除**含完整唤醒词**的近词（如"小麦小麦吗"）：用户说这句话时本就是在叫设备，
    模型命中它不算误触。留着会让验收把"正确行为"记成误报，掩盖真正的误触。
    """
    manifest = manifest or newest_corpus()
    items = []
    if not manifest or not os.path.exists(manifest):
        return items
    base = os.path.dirname(manifest)
    with open(manifest, encoding="utf-8-sig", newline="") as f:
        for gi, row in enumerate(csv.DictReader(f), start=1):
            p = os.path.join(base, row["file"])
            if not os.path.exists(p):
                continue
            lab = int(row["label"])
            if lab == 0 and "小麦小麦" in (row.get("text") or ""):
                continue                     # 含完整唤醒词的"负样本"不是负样本
            x = read_wav16k(p)
            spans = word_spans(row, x.size) if lab == 1 else []
            bad = parse_holes(row.get("hole_spans", ""), x.size // FRAME + 1)
            items.append({"path": p, "x": x, "spans": spans, "bad": bad, "grp": gi,
                          "kind": "corpus", "name": os.path.basename(row["file"])})
    return items


def load_legacy(pos_vad=True):
    """旧语料：正样本走 VAD 只留有声窗，负样本整份负例（与 v10 同一批文件，口径不同）。"""
    items = []
    g = 0
    for pat in (("xiaomai", "pos_*.wav"), ("xiaomai_v4", "pos_*.wav")):
        for p in sorted(glob.glob(os.path.join(LEGACY, *pat))):
            g += 1
            x = read_wav16k(p)
            items.append({"path": p, "x": x, "spans": vad_spans(x) if pos_vad else [(0, x.size)],
                          "bad": None, "grp": 50000 + g, "kind": "legacy_pos",
                          "name": os.path.basename(p)})
    for p in sorted(glob.glob(os.path.join(REAL, "wake_*.wav"))):
        g += 1
        x = read_wav16k(p)
        items.append({"path": p, "x": x, "spans": vad_spans(x), "bad": None,
                      "grp": 50000 + g, "kind": "legacy_pos", "name": os.path.basename(p)})
    neg = []
    for pat in (("xiaomai", "neg_*.wav"), ("xiaomai_v4", "neg_*.wav"), ("nearwords", "*.wav"),
                ("zh", "*.wav"), ("en", "*.wav")):
        neg += sorted(glob.glob(os.path.join(LEGACY, *pat)))
    neg += sorted(glob.glob(os.path.join(REAL, "ambient_*.wav")))
    for i, p in enumerate(neg):
        items.append({"path": p, "x": read_wav16k(p), "spans": [], "bad": None,
                      "grp": 60000 + i, "kind": "legacy_neg", "name": os.path.basename(p)})
    return items


def holdout_groups(items, every=4):
    """留出**整份源采集**：机内语料里每 every 份留 1 份（正负都按同一规则），
    这样验收看到的词位置、房间噪声、麦路径增益都是训练没见过的组合。"""
    hold = set()
    pos_i = [it["grp"] for it in items if it["spans"]]
    neg_i = [it["grp"] for it in items if not it["spans"]]
    for lst in (pos_i, neg_i):
        for g in lst[::every]:
            hold.add(g)
    return hold


def norm_level(w, target=2000.0):
    """把窗内 RMS 归一化到固定目标，彻底抹掉响度信息。

    为什么必须做：机内语料的正样本全是音量 100 的响素材、空场负样本很安静，模型于是
    学会"响 = 唤醒词"。实测证据（10-20 夜）：同一段 ambient 原始 0.52 分 → 放大 8 倍
    0.88 分 → 经喇叭重录 0.996 分；而"持续大声噪声"（ambient_05，p10 底噪 1828 vs 其他
    段 700~800）即使重训后仍拿 0.88 分。宽增益增强只是缓解，归一化才是根治。
    **板端 kws.cpp 必须做同一变换**，否则训练/推理特征分布不一致。
    """
    x = np.asarray(w, dtype=np.float32)
    rms = float(np.sqrt(np.mean(x * x))) if x.size else 0.0
    if rms < 1.0:
        return w                      # 数字静音不放大，避免把量化噪声抬成信号
    return np.clip(x * (target / rms), -32768, 32767).astype(np.int16)


def synth_steady_noise(rng, n, sr=SR):
    """合成"持续稳态噪声"窗：白噪 / 粉噪 / 低频嗡嗡 / 带通噪声，电平覆盖安静到很响。

    为什么必须合成：10-21 凌晨实测，最大的**真误触**源就是"持续大声噪声"——ambient_05
    （底噪 p10=1828，其余环境段只有 700~800）被模型打了 0.88~0.996 分，而真语料里这类
    样本极少（25 段空场 + 6 段环境音里只有 1 段是稳态大声）。模型尾部 GAP 池化对时间
    结构不敏感，稳态噪声在时间上平坦，正好钻这个空子。
    """
    out = []
    kinds = rng.integers(0, 4, size=n)
    for k in kinds:
        w = rng.normal(0, 1, sr).astype(np.float32)
        if k == 1:                       # 粉噪（1/f）：累积白噪去均值
            w = np.cumsum(w); w = w - w.mean()
        elif k == 2:                     # 低频嗡嗡（风扇/压缩机）：50Hz 基频+谐波
            t = np.arange(sr) / sr
            w = (0.6 * np.sin(2 * np.pi * 50 * t) + 0.25 * np.sin(2 * np.pi * 100 * t)
                 + 0.15 * np.sin(2 * np.pi * 150 * t) + 0.1 * w)
        elif k == 3:                     # 带通噪声（电视/混响底噪）
            w = np.diff(np.concatenate([[0.0], w]))
            w = np.convolve(w, np.ones(8) / 8.0, mode="same")
        w = w - w.mean()
        rms = float(np.sqrt(np.mean(w * w))) + 1e-9
        out.append(np.clip(w * (rng.uniform(400.0, 8000.0) / rms), -32768, 32767).astype(np.int16))
    return out


def synth_onset_negatives(rng, n, sr=SR):
    """合成"静音→突发声响"窗：随机位置的短促爆发（噪声/音调/敲击），前后是安静。

    为什么必须补这一类（10-21 凌晨实测的决定性证据）：`ambient_02.wav` 原始逐窗打分
    全是 0.00，但**循环 3 遍、中间插 0.8s 静音后**分数飙到 0.65 / 0.95 / 1.00。
    即模型学到的是"**静音→突发**"这个转换，而不是"小麦小麦"——因为训练负样本几乎
    全是**没有突发的安静空场**，正样本全是"安静→词"，于是"有动静"就等价于唤醒词。
    现实里这意味着关门、碰碗、椅子响都可能触发。
    """
    out = []
    for _ in range(n):
        w = rng.normal(0, 0.02, sr).astype(np.float32)        # 极低底噪
        ln = int(rng.uniform(0.08, 0.9) * sr)                  # 爆发长度 80~900ms
        st = int(rng.integers(0, max(1, sr - ln)))
        kind = int(rng.integers(0, 3))
        if kind == 0:                                          # 宽带噪声爆发（碰撞/摩擦）
            burst = rng.normal(0, 1, ln)
        elif kind == 1:                                        # 短音调（门铃/提示音/敲击）
            t = np.arange(ln) / sr
            f0 = rng.uniform(180, 1600)
            burst = np.sin(2 * np.pi * f0 * t) * np.exp(-t * rng.uniform(3, 12))
        else:                                                  # 衰减冲击（敲桌子/关门）
            burst = rng.normal(0, 1, ln) * np.exp(-np.arange(ln) / (ln * rng.uniform(0.1, 0.5)))
        burst = burst / (np.sqrt(np.mean(burst ** 2)) + 1e-9) * rng.uniform(800, 9000)
        w[st:st + ln] += burst.astype(np.float32)
        out.append(np.clip(w, -32768, 32767).astype(np.int16))
    return out


def build(n_aug=1, seed=42, verbose=True, legacy=None, holdout=True):
    """返回 (X, y, grp)，与 train_kws.py 的 v10 接口一致。"""
    corpus = load_corpus()
    if not corpus:
        raise SystemExit("没有机内声学语料：先跑 rig 的 --phase traindata，或用 KWS_CORPUS 指定 manifest.csv")
    items = corpus + (load_legacy() if legacy is None or legacy else [])
    hold = holdout_groups(items) if holdout else set()
    rng = np.random.default_rng(seed)
    X, y, g = [], [], []
    stat = {"pos_win": 0, "neg_win": 0, "drop_ride": 0, "drop_hole": 0, "hold_win": 0}
    for it in items:
        x = it["x"]
        wins = label_windows(x, it["spans"], it["bad"], tight=True)[0]
        if not wins:
            continue
        if it["bad"] is not None:
            stat["drop_hole"] += int(it["bad"][:x.size // FRAME + 1].sum())
        for s, lab in wins:
            if hold and it["grp"] in hold:
                stat["hold_win"] += 1
                continue
            w = x[s:s + WIN]
            stat["pos_win" if lab else "neg_win"] += 1
            if NORM_LEVEL > 0:         # 默认关闭：板端未做归一化时训练也不能做，否则特征错配
                w = norm_level(w, NORM_LEVEL)
            X.append(ei_mfcc(w)); y.append(lab); g.append(it["grp"])
            for _ in range(n_aug):
                # 宽增益（0.25~3.0）在归一化之上再叠加电平抖动，防模型对残余电平敏感
                X.append(ei_mfcc(_aug(w, rng, gain=(0.25, 3.0))))
                y.append(lab); g.append(it["grp"])
    # 合成负样本（独立组号 70000/71000，不进留出集——是"补充负样本"不是"评测集"）
    n_synth = int(os.environ.get("KWS_SYNTH_NEG", "600") or 0)
    if n_synth > 0:
        for w in synth_steady_noise(rng, n_synth):
            stat["neg_win"] += 1
            X.append(ei_mfcc(w)); y.append(0); g.append(70000)
            for _ in range(n_aug):
                X.append(ei_mfcc(_aug(w, rng, gain=(0.25, 3.0))))
                y.append(0); g.append(70000)
    n_onset = int(os.environ.get("KWS_SYNTH_ONSET", "1000") or 0)
    if n_onset > 0:
        for w in synth_onset_negatives(rng, n_onset):
            stat["neg_win"] += 1
            X.append(ei_mfcc(w)); y.append(0); g.append(71000)
            for _ in range(n_aug):
                X.append(ei_mfcc(_aug(w, rng, gain=(0.25, 3.0))))
                y.append(0); g.append(71000)
    X = np.asarray(X, dtype=np.float32)
    y = np.asarray(y, dtype=np.int32)
    g = np.asarray(g, dtype=np.int32)
    if verbose:
        print("语料 %d 份 + 旧语料 %d 份 + 合成稳态噪声 %d / 起音 %d 窗；"
              "进训练 正窗 %d / 负窗 %d（留出窗 %d 未进训练）"
              % (len(corpus), len(items) - len(corpus), n_synth, n_onset,
                 stat["pos_win"], stat["neg_win"], stat["hold_win"]))
        print("正样本占比 %.1f%%  源文件 %d 个  特征 %s"
              % (100.0 * y.mean(), len(np.unique(g)), X.shape))
    return X, y, g


def export_holdout(out_dir=HOLD):
    """把留出的整份采集剪成短文件：词区（±0.15s）= 正例文件，词间静音/底噪区 = 负例文件。
    剪出来的正例不带整窗补零，验收的"单条"读数才不会被静音窗稀释。"""
    corpus = load_corpus()
    if not corpus:
        print("没有语料可剪")
        return None
    hold = holdout_groups(corpus)
    items = [it for it in corpus if it["grp"] in hold]
    os.makedirs(out_dir, exist_ok=True)
    for old in glob.glob(os.path.join(out_dir, "*.wav")):
        os.remove(old)
    rows, np_, nn = [], 0, 0
    pad = int(0.15 * SR)
    for it in items:
        x, name = it["x"], os.path.splitext(it["name"])[0]
        spans = it["spans"]
        for i, (a, b) in enumerate(spans):
            s, e = max(0, a - pad), min(x.size, b + pad)
            fn = "pos_%s_w%d.wav" % (name, i)
            _write(os.path.join(out_dir, fn), x[s:e])
            rows.append((fn, 1, "词区 %s %d-%d" % (name, a, b)))
            np_ += 1
        # 非词区（且不含丢帧）剪成 ~2.5s 的负例块
        free = [(0, spans[0][0] if spans else x.size)] if not spans else []
        if spans:
            free = [(spans[i][1], spans[i + 1][0] if i + 1 < len(spans) else x.size)
                    for i in range(len(spans))]
        for j, (a, b) in enumerate(free):
            k = a
            ci = 0
            while b - k >= int(2.5 * SR):
                # 文件名必须带**块序号 ci**：此前用区间序号 j，同一区间内的块互相覆盖，
                # 一段 8s 空场只落盘最后 2.5s 一块——而混淆音往往在别处，验收因此漏检
                # （10-20 夜"零误触"假象的真因：manifest 47 条负样本、磁盘上只有 18 个文件，
                #  kws_validate 静默跳过缺失文件）。
                fn = "neg_%s_q%d_%d.wav" % (name, j, ci)
                _write(os.path.join(out_dir, fn), x[k:k + int(2.5 * SR)])
                rows.append((fn, 0, "词外 %s %d-%d" % (name, k, k + int(2.5 * SR))))
                nn += 1
                k += int(2.5 * SR)
                ci += 1
    man = os.path.join(out_dir, "manifest.csv")
    with open(man, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["file", "label", "text"])
        for fn, lab, note in rows:
            w.writerow([fn, lab, note])
    print("留出集 %s：正 %d / 负 %d（来自 %d 份整采）" % (man, np_, nn, len(items)))
    return man


def _write(path, x):
    import wave
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(np.asarray(x, dtype="<i2").tobytes())


if __name__ == "__main__":
    export_holdout()
    build(legacy=True)
