"""kws_acoustic_rig.py —— 声学回踩自测台：喇叭出声 → 板载麦克风听 → 读板上判决

为什么需要它（所有者 2026-10-19 指令：不再靠人录音/喊话取证）：设备的"回复"本身就是喇叭出声，
所以把「播放音频 → 麦克风收 → 板上是否误醒」闭成环路，就能全自动复现
①有人喊唤醒词 ②近词干扰 ③设备自己应答把麦克风灌满 ④环境噪声 四类场景。

两种激励源（`--source`）：
  board（缺省，也是唯一真实的那条）：WAV/合成 PCM 经串口灌进板上 RAM，再由**设备自己的
        喇叭**（sound0→ES8388→MD8002）放出，板载 PDM 麦收。这就是"模型发命令→喇叭回复→
        麦把回复听进去"的机内声路，不需要真人、也不需要目标 USB 线。
  pc：PC 默认输出→空气→板麦。2026-10-19 的 proof 已判这条路在本机**不可信**
        （数字静音对照的抬升和真音一样高），留着只作为对照臂。

两阶段（都要 COM9 独占，跑之前关掉其它串口工具）：

  # 1) 声路证明：同一段 kws mic 时长，分别在"放 1kHz 音/放扫描/放数字静音"时测峰值电平，
  #    与不放音的空场基线对比 —— 真音必须显著抬升、数字静音不得抬升，仪器才成立
  <default>/python.exe tools/kws_acoustic_rig.py --phase proof --source board
  # 2) 场景矩阵：kws start 跑生产判决，逐段灌+放 正样本/近词/应答语/环境音，按板时戳归因命中
  <default>/python.exe tools/kws_acoustic_rig.py --phase matrix --source board --rounds 3
  # 3) 造机内声学训练语料：设备自己放音→自己麦录→逐帧无损回吐落 WAV + manifest（词位置由
  #    包络互相关定，不是文件级标签；串口丢帧处标成洞，dataset_v11 跳过压到洞的窗）
  <default>/python.exe tools/kws_acoustic_rig.py --phase traindata --cap-sec 8 --captures 3 \
      --ambient-captures 24 --volume 69 --gap 2

时间归因：板上每行日志自带 `[  NNN.NNs]` 运行秒，PC 记下行到达时刻，两者最小二乘拟合
board=a·pc+b（残差进报表）。`kws play` 的 PLAY_BEGIN 行给的是**板载放音起点**，
所以板内激励的归因不依赖 PC 时钟，比 pc 臂硬。

灌数据为什么要序号：115200 下每行 973 字符，板子一边收一边还要把日志往外吐，
控制台 RX 环被挤爆时丢字符是会发生的事。固件只认期望序号、乱序就报 PUSHERR，
PC 从期望号重发；最后用 `kws pushstat` 的 len/sum/peak 和 PC 端按同一定义复算的值对账——
**回放出来的确实是要发的那一份**，不靠信任靠构造。

产物：logs/kws_rig_<phase>_<stamp>.{log,json}
"""
import argparse
import ctypes
import datetime
import json
import os
import re
import threading
import time
import wave

import numpy as np
import serial

import sys

# 报表里全是中文和 →、≥ 这类符号。Windows 下 stdout 被重定向时按 GBK 编码，遇到
# GBK 没有的字符直接 UnicodeEncodeError——10-19 那次整场矩阵跑完 21 分钟，只在最后
# 一行汇总上崩掉，JSON 全丢。这里强制 UTF-8，把这一类故障从根上封掉。
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kws_console import force_utf8                   # noqa: E402
force_utf8()
from kws_uart import LineReader                     # noqa: E402
from kws_listen import HIT_RE, INFO_RE, read_info   # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.abspath(os.path.join(HERE, "..", "data"))
LOG_DIR = os.path.abspath(os.path.join(HERE, "..", "logs"))
# 板端时基：固件在放音起点/命中/放音结束这几行打 `@<ms>ms`（RT_TICK_PER_SECOND=1000）。
# ulog 本身不带时间戳，日志里的 `[  x.xxpc]` 前缀是本工具加的 PC 钟——归因一律用板钟。
BOARD_TS = re.compile(r"@(\d+)ms")
SCORE_RE = re.compile(r"top1=(\w+)\((\d+)%\) top2=(\w+)\((\d+)%\)")
LEVEL_RE = re.compile(r"电平\s*(-?\d+)")
MIC_SUMMARY_RE = re.compile(r"电平 RMS：平均 (\d+) / 峰值 (\d+)")
HIST_RE = re.compile(r"SCOREHIST [^=]*=([\d,]+)")
DIST_RE = re.compile(r"最高 (\d+)%\s+>50% 共 (\d+) 窗")
# 固件流量计：消费样本/(墙钟×16) < 100% ⇒ 采集链路在挖洞，模型听到的秒数比墙钟少。
# 误触"次/小时"的分母必须按它校正，否则拿监听墙钟当暴露量会虚降误触率。
FLOW_RE = re.compile(r"累计交付率 (\d+)% 丢失≈(\d+)ms 丢样轮 (\d+)")

# 灌数的瓶颈实测是**板端控制台每行固定开销**：16 样本/行的停止等待只有 392 样本/s
# （≈41ms/行），离 115200 的理论 2400 样本/s 很远。固件已把 FINSH_CMD_SIZE 80→1024、
# RT_SERIAL_RB_BUFSZ 128→2048，这里相应改成 250 样本/行（1000 hex + "kws push 999 " 前缀
# = 1010 字符 ≤ 1023 可用行宽），行数减到 1/15.6，剂量自证口径不变。
# 节奏仍由 PUSHACK 停止等待决定（定时盲发在 20ms/行时会被 RX 环吃掉整行行首）。
PUSH_SAMPLES = 250
BOARD_RE = {
    "start": re.compile(r"PUSHSTART cap=(\d+) want=(\d+)"),
    "ack": re.compile(r"PUSHACK seq=(\d+) len=(\d+)"),
    "stat": re.compile(r"PUSHSTAT len=(\d+) cap=(\d+) seq=(\d+) drop=(\d+) sum=(\d+) peak=(\d+)"),
    "err": re.compile(r"PUSHERR .*?(?:exp=(\d+)|hex_len=(\d+)|hex_at=(\d+))"),
    "begin": re.compile(r"PLAY_BEGIN @(\d+)ms len=(\d+) loops=(\d+) sr=(\d+)"),
    "done": re.compile(r"PLAY_DONE @(\d+)ms dur=(\d+)ms loops=(\d+) samples=(\d+) "
                       r"aborted=(\d+) err=(-?\d+)"),
    "playerr": re.compile(r"PLAY_ERR (\w+)"),
    "echo": re.compile(r"ECHO (\d+)"),
}

winmm = ctypes.windll.winmm


u16, u32, uptr = ctypes.c_uint16, ctypes.c_uint32, ctypes.c_void_p


class WAVEFORMATEX(ctypes.Structure):
    _fields_ = [("wFormatTag", u16), ("nChannels", u16), ("nSamplesPerSec", u32),
                ("nAvgBytesPerSec", u32), ("nBlockAlign", u16), ("wBitsPerSample", u16),
                ("cbSize", u16)]


class WAVEHDR(ctypes.Structure):
    _fields_ = [("lpData", uptr), ("dwBufferLength", u32), ("dwBytesRecorded", u32),
                ("dwUser", uptr), ("dwFlags", u32), ("dwLoops", u32),
                ("lpNext", uptr), ("reserved", uptr)]


WHDR_DONE = 0x0001
CALLBACK_EVENT = 0x00
WAVE_MAPPER = 0xFFFFFFFF
_kernel32 = ctypes.windll.kernel32
_kernel32.CreateEventW.restype = uptr
_kernel32.SetEvent.restype = ctypes.c_int
_kernel32.CloseHandle.restype = ctypes.c_int
_kernel32.WaitForSingleObject.restype = ctypes.c_uint
_wait = _kernel32.WaitForSingleObject
_infinite = 0xFFFFFFFF


def load_pcm(path, sr=16000):
    """任意 WAV → 16k mono int16（本工程的语料本来就是 16k/mono/16bit，这里只做兜底）。"""
    with wave.open(path, "rb") as w:
        n, ch, fr = w.getnframes(), w.getnchannels(), w.getframerate()
        raw = np.frombuffer(w.readframes(n), dtype="<i2")
    x = raw.reshape(-1, ch).mean(axis=1) if ch > 1 else raw.astype(np.float64)
    if fr != sr:
        m = int(x.size * sr / fr)
        t = np.linspace(0.0, x.size * sr / fr / sr, num=m, endpoint=False)
        x = np.interp(t, np.arange(x.size), x)
    return np.clip(x, -32768, 32767).astype("<i2")


def fit_level(pcm, target_p95=9000.0, peak_cap=32000.0):
    """把激励统一到同一个**声压档**再灌给设备：语料源文件本身差 3 倍以上电平
    （真人板采样本 wake_01 包络 P95=2717、RMS 1820；TTS 词 pos_0002 P95=9235、RMS 4844），
    而机内房间底噪 RMS ~1500 一直在放音时也存在。不拉平就会出现"喇叭确实在响、
    剂量门却把这段判负"——弱激励的相关性被底噪方差稀释（实测 corr 0.17~0.22、抬升 1.26~1.42，
    而同一块板同一音量下 TTS 词能到 corr 0.71~1.00、抬升 2.1~2.6）。

    增益上限用 **|x| 的 99.95 分位**而不是峰值：真人样本里有孤立削顶样点
    （wake_01 峰值就等于 32767），按峰值算会把增益空间一刀切成 0.92（=不能放大）。
    增益 <1 时不动素材（本函数只放大不衰减），返回 (int16 数组, 实际增益)。"""
    x = pcm.astype(np.float64)
    if x.size == 0:
        return pcm, 1.0
    # 源文件长度不一定是 10ms(160 样点) 整数倍（TTS 语料各时长都有）——按 160 对齐截断，
    # 否则 reshape 直接抛 ValueError 把整轮跑批打崩（2026-10-20 夜实测埋葬一轮）。
    x = x[: (x.size // 160) * 160]
    if x.size == 0:
        return pcm, 1.0
    e = np.abs(x).reshape(-1, 160).mean(axis=1)
    p95 = float(np.percentile(e, 95.0))
    if p95 <= 0:
        return pcm, 1.0
    g = target_p95 / p95
    hi = peak_cap / max(1.0, float(np.percentile(np.abs(x), 99.95)))
    g = max(1.0, min(g, hi))
    if g <= 1.0001:
        return pcm, 1.0
    return np.clip(x * g, -32768, 32767).astype("<i2"), round(float(g), 3)


def list_out_devices():
    """枚举 waveOut 输出设备 → [(索引, 名字, 声道数)]（PC+音箱通路选设备用）。"""
    class WAVEOUTCAPS(ctypes.Structure):
        _fields_ = [("wMid", ctypes.c_ushort), ("wPid", ctypes.c_ushort),
                    ("vDriverVersion", ctypes.c_uint),
                    ("szPname", ctypes.c_wchar * 32),
                    ("dwFormats", ctypes.c_uint), ("wChannels", ctypes.c_ushort),
                    ("wReserved1", ctypes.c_ushort), ("dwSupport", ctypes.c_uint)]
    out = []
    try:
        n = winmm.waveOutGetNumDevs()
    except Exception:
        return out
    for i in range(n):
        caps = WAVEOUTCAPS()
        if winmm.waveOutGetDevCapsW(i, ctypes.byref(caps), ctypes.sizeof(caps)) == 0:
            out.append((i, caps.szPname, int(caps.wChannels)))
    return out


def resolve_out_device(spec):
    """--pc-device 解析：空 → (None, "系统默认")；纯数字 → 索引；否则按名字片段匹配。

    名字匹配优先**最长匹配**（"扬声器 (花再 Evo Play)" 同时含"扬声器"和"花再"，
    取最具体的那条），多条同分时取索引最小。找不到就报错——PC 源静默回落到默认设备
    会让整批语料采到错误的物理位置，宁可直接失败。
    """
    devs = list_out_devices()
    if not spec:
        return None, "系统默认"
    s = str(spec).strip()
    if s.isdigit():
        i = int(s)
        for d in devs:
            if d[0] == i:
                return i, d[1]
        raise SystemExit("--pc-device %s 越界（现有 %d 个设备）" % (s, len(devs)))
    hits = [d for d in devs if s.lower() in d[1].lower()]
    if not hits:
        raise SystemExit("--pc-device %r 没匹配到设备。可用：%s"
                         % (s, "; ".join("%d=%s" % (d[0], d[1]) for d in devs)))
    hits.sort(key=lambda d: (-len(d[1]), d[0]))
    return hits[0][0], hits[0][1]


class Player:
    """winmm waveOut 直接吃内存里的 PCM。

    为什么不用 MCI：同一个 WAV 放 C:\\ft\\x.wav 能开、放工程内 119 字符路径就随机报
    304/275，且起播时刻要靠猜。waveOut 没有文件名这层，起播即 waveOutWrite 返回那一刻，
    自测台的时间归因才有硬保证。

    device：waveOut 设备索引（0 起）。None = WAVE_MAPPER（Windows 默认输出设备）。
    **PC+音箱通路必须显式选设备**：默认设备可能是笔记本内置喇叭/HDMI，声音出去了但
    位置和距离全错，语料就废了。`--pc-device` 支持索引或名字片段（见 resolve_out_device）。
    """

    def __init__(self, sr=16000, device=None):
        self.sr = sr
        self.device = device
        self.hwo = uptr()
        self.hdr = None
        self.buf = None
        self.evt = None
        self.open = False

    def to_pcm(self, path):
        return load_pcm(path, self.sr)

    def play(self, path):
        return self.play_pcm(self.to_pcm(path))

    def play_pcm(self, pcm):
        """直接播内存里的 int16 PCM（PC 源采集用，省掉"先写临时 wav 再读回"）。
        起播时刻 = waveOutWrite 返回那一刻，与板载 PLAY_BEGIN 的精度同级。"""
        self.close()
        pcm = np.ascontiguousarray(np.asarray(pcm, dtype="<i2"))
        self.buf = ctypes.create_string_buffer(pcm.tobytes(), pcm.nbytes)
        fmt = WAVEFORMATEX(1, 1, self.sr, self.sr * 2, 2, 16, 0)
        self.evt = _kernel32.CreateEventW(None, True, False, None)
        if self.hwo.value is None:
            dev = WAVE_MAPPER if self.device is None else self.device
            r = winmm.waveOutOpen(ctypes.byref(self.hwo), dev, ctypes.byref(fmt),
                                  self.evt, 0, CALLBACK_EVENT)
            if r != 0:
                raise RuntimeError("waveOutOpen 返回 %d（设备 %s 不可用？）"
                                   % (r, "默认" if self.device is None else self.device))
            self.open = True
        self.hdr = WAVEHDR(ctypes.cast(self.buf, uptr), pcm.nbytes, 0, None, 0, 0, None, None)
        if winmm.waveOutPrepareHeader(self.hwo, ctypes.byref(self.hdr), ctypes.sizeof(self.hdr)):
            raise RuntimeError("waveOutPrepareHeader 失败")
        t = time.perf_counter()
        r = winmm.waveOutWrite(self.hwo, ctypes.byref(self.hdr), ctypes.sizeof(self.hdr))
        if r != 0:
            raise RuntimeError("waveOutWrite 返回 %d" % r)
        return t, pcm.size / float(self.sr)

    def done(self):
        return self.hdr is not None and bool(self.hdr.dwFlags & WHDR_DONE)

    def wait(self, timeout=60.0):
        _wait(self.evt, int(timeout * 1000))

    def close(self):
        if self.hwo and self.open:
            winmm.waveOutReset(self.hwo)
            if self.hdr is not None:
                winmm.waveOutUnprepareHeader(self.hwo, ctypes.byref(self.hdr),
                                             ctypes.sizeof(self.hdr))
        self.hdr = self.buf = None

    def shutdown(self):
        self.close()
        if self.open:
            winmm.waveOutClose(self.hwo)
            self.hwo = uptr()
            self.open = False


def wav_write(path, x, sr=16000, peak=0.9):
    """float[-1,1] → 16bit mono WAV。peak 只做自身归一，不动系统音量（保证可复现）。"""
    x = np.asarray(x, dtype=np.float64)
    m = np.max(np.abs(x)) if x.size else 0.0
    x = (x / m * peak) if m > 0 else x
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes((np.clip(x, -1, 1) * 32767.0).astype("<i2").tobytes())
    return x.size / float(sr)


def clip_sec(path):
    with wave.open(path) as w:
        return w.getnframes() / float(w.getframerate())


def make_assets(out_dir, peak, sr=16000):
    os.makedirs(out_dir, exist_ok=True)
    t = np.arange(0, 2.0, 1.0 / sr)
    out = {}
    out["tone1k"] = os.path.join(out_dir, "rig_tone1k.wav")
    wav_write(out["tone1k"], np.sin(2 * np.pi * 1000 * t), sr, peak)
    out["sweep"] = os.path.join(out_dir, "rig_sweep.wav")
    f0, f1 = 300.0, 3400.0
    k = np.log(f1 / f0) / t[-1]
    wav_write(out["sweep"], np.sin(2 * np.pi * f0 * (np.exp(k * t) - 1) / k), sr, peak)
    out["silence"] = os.path.join(out_dir, "rig_silence.wav")
    wav_write(out["silence"], np.zeros_like(t), sr, 0.0)
    return out


def load_manifest(dir_path):
    p = os.path.join(dir_path, "manifest.csv")
    out = {}
    if os.path.isfile(p):
        with open(p, encoding="utf-8", errors="replace") as f:
            for row in f:
                parts = row.rstrip("\n").split(",")
                if len(parts) >= 3 and parts[0].endswith(".wav"):
                    out[parts[0]] = ",".join(parts[2:3])
    return out


def load_texts():
    """语料文本表，**键是完整路径**。各语料目录都用 pos_0000.wav / neg_0002.wav 这类同名文件，
    按文件名合并必然互相覆盖（实测 neg_0002.wav 在几份清单里有五种不同文本），
    那样写进 manifest 的 text 是错标——正样本会显示成别的语料的句子。"""
    dirs = [os.path.join(DATA, "wakeword", d) for d in ("xiaomai", "nearwords", "zh")]
    dirs += [os.path.join(DATA, "reply"), os.path.join(DATA, "real")]
    out = {}
    for d in dirs:
        for name, txt in load_manifest(d).items():
            out[os.path.normpath(os.path.join(d, name))] = txt
    return out


def dose_transient(x):
    """瞬态素材的剂量判据：**"确实有一次明显事件"**，而不是互相关。

    为什么不能用互相关：敲门/鼠标点击/拍手这类素材是稀疏事件，8s 采集里 95% 是静音，
    互相关被静音段稀释到 0.5 以下（10-21 凌晨实测 20 段只过 1 段，19 段被误判废）。
    判据：至少有一帧（10ms）的均值 ≥ 中位帧的 4 倍且绝对值 ≥ 300 —— 即"有过一次突发"。
    """
    n = (x.size // 160) * 160
    if n == 0:
        return False
    fr = np.abs(x[:n]).reshape(-1, 160).mean(axis=1)
    med = float(np.median(fr)) + 1.0
    pk = float(fr.max())
    return pk >= 4.0 * med and pk >= 300.0


def default_scenarios():
    def files_of(d, pat, limit):
        base = os.path.join(DATA, "wakeword", d)
        if not os.path.isdir(base):
            return []
        return [os.path.join(base, n) for n in sorted(os.listdir(base))
                if n.startswith(pat) and n.endswith(".wav")][:limit]

    def files_in(rel, limit=None):
        """data/ 下任意子目录里的 wav（按名排序截断）。"""
        base = os.path.join(DATA, rel)
        if not os.path.isdir(base):
            return []
        fs = [os.path.join(base, n) for n in sorted(os.listdir(base)) if n.endswith(".wav")]
        return fs[:limit] if limit else fs
    real = os.path.join(DATA, "real")
    reals = (lambda pre: [os.path.join(real, n) for n in sorted(os.listdir(real))
                          if n.startswith(pre) and n.endswith(".wav")]) if os.path.isdir(real) \
        else (lambda pre: [])
    rep = os.path.join(DATA, "reply")          # 设备应答语：机器自己喇叭会播的那些
    reps = [os.path.join(rep, n) for n in sorted(os.listdir(rep)) if n.endswith(".wav")] \
        if os.path.isdir(rep) else []
    return [
        {"name": "wake_real", "kind": "pos", "files": reals("wake_")},
        {"name": "wake_tts", "kind": "pos", "files": files_of("xiaomai", "pos_", 6)},
        # v4 的 32 条多音色 TTS 正样本此前 rig 从未用过——正样本是 v11 的瓶颈（v9/v10 只有
        # 1 条真实正样本进训练），这里补进来：同一句话、不同音色/语速，经板载声路采集。
        {"name": "wake_tts_v4", "kind": "pos", "files": files_of("xiaomai_v4", "pos_", 32)},
        # 对数扫频（实测板载冲激响应用）：kind=neg 只为让 rig 自动播放并录制，
        # 不参与训练；录下来做反卷积就能拿到板子的真实 IR。
        {"name": "sweep", "kind": "neg", "files": files_in("sweep", 1)},
        # 近词源从 8 扩到全部 24：小卖/小迈/买卖/卖麦/小麦/小麦麦/小麦苗…（10-21 凌晨
        # 实测近词是仅次于"起音"的第二大误触源，8 条源只够覆盖 1/3 的词形）
        {"name": "nearword", "kind": "neg", "files": files_of("nearwords", "neg_", 24)},
        # 自然语境近词句子（10-24 新增）：**含「小麦」但整句不是唤醒词**。
        # v30 实测 nearword 203.9/h + zh_sentence 221.5/h，而 v27 只有 2.2/h——差异就是
        # "正样本加到 73 条后边界变松，但模型没见过小麦嵌在自然句里的形态"。
        # 现有 nearwords 是 24 条孤立词，补不了这一块；本场景由 gen_sentence_corpus.py 产出。
        {"name": "nearword_sentence", "kind": "neg",
         "files": files_of("nearword_sentences", "neg_", 80)},
        {"name": "zh_sentence", "kind": "neg", "files": files_of("xiaomai", "neg_", 8)},
        {"name": "reply", "kind": "neg", "files": reps},
        # 真实非语音瞬态（ESC-50 公开库）：敲门/键盘/鼠标/拍手/玻璃碎/脚步/钟表滴答/
        # 门轴/咳嗽/喝水。10-21 凌晨诊断出模型靠"能量起音"判别而不是词内容——原负样本
        # 全是**没有瞬态的安静空场**，这一类是补那块拼图的关键（合成噪声补不动）。
        {"name": "transient", "kind": "neg", "files": files_in("transient"),
         # 稀疏事件：剂量用峰值判据，放大目标调低（fit_level 到 9000 会把底噪一起抬 20 倍）
         "dose": "peak", "stim_target": 3000.0},
        # 环境音**不拉平**：它的作用就是"真实房间声压下的底噪/瞬态"，放大它等于伪造一种
        # 板上不会遇到的噪声强度，而这正是模型误触的那一类，必须保持原生电平。
        {"name": "ambient", "kind": "neg", "files": reals("ambient_"), "level": False},
    ]


def fit_clock(pairs):
    if len(pairs) < 8:
        return None
    t0 = pairs[0][0]
    pc = np.array([p[0] - t0 for p in pairs])
    bd = np.array([p[1] for p in pairs])
    a, b = np.polyfit(pc, bd, 1)
    resid = bd - (a * pc + b)
    return {"t0_pc": t0, "a": float(a), "b": float(b),
            "rms_sec": float(np.sqrt(np.mean(resid ** 2))), "anchors": len(pairs)}


class Monitor(threading.Thread):
    """单独占有串口读句柄：全行落盘 + 抽 命中/分数/电平/板时戳/板计数/分数直方图。"""

    def __init__(self, ser, flog):
        super().__init__(daemon=True)
        self.lr = LineReader(ser, encoding="utf-8")
        self.flog = flog
        self.t0 = time.perf_counter()
        self.lines, self.hits, self.scores, self.anchors, self.levels = [], [], [], [], []
        self.infos, self.hists, self.dists, self.flows = [], [], [], []
        self._stop = threading.Event()

    def stop_now(self):
        self._stop.set()
        self.join(timeout=3)

    def wait_for(self, rx, after_pc, timeout=10.0):
        """等一条 after_pc 之后出现、且匹配 rx 的行。命令回显是小写、板端标记是大写，
        所以模式本身不会撞到发出的命令。timeout=0 也至少扫一遍（当"非阻塞查一下"用）。"""
        t0 = time.perf_counter()
        while True:
            for pc, line in reversed(self.lines[-400:]):
                if pc < after_pc:
                    break
                m = rx.search(line)
                if m:
                    return line, pc, m
            if time.perf_counter() - t0 >= timeout:
                return None, None, None
            time.sleep(0.02)

    def run(self):
        while not self._stop.is_set():
            line = self.lr.get(0.05)
            if line is None:
                continue
            pc = time.perf_counter()
            self.lines.append((pc, line))
            self.flog.write("[%8.2fpc] %s\n" % (pc - self.t0, line))
            self.flog.flush()
            m = BOARD_TS.search(line)
            bt = (float(m.group(1)) / 1000.0) if m else None
            if bt is not None:
                self.anchors.append((pc, bt))
            h = HIT_RE.search(line)
            if h:
                self.hits.append({"pc": pc, "board": bt, "label": h.group(1),
                                  "score": int(h.group(2)),
                                  "confirm": "%s/%s" % (h.group(3), h.group(4)), "raw": line})
            s = SCORE_RE.search(line)
            if s:
                self.scores.append({"pc": pc, "board": bt, "top1": s.group(1),
                                    "p1": int(s.group(2)), "top2": s.group(3),
                                    "p2": int(s.group(4))})
            lv = LEVEL_RE.search(line)
            if lv:
                self.levels.append({"pc": pc, "rms": int(lv.group(1))})
            i = INFO_RE.search(line)
            if i:
                self.infos.append({"pc": pc, "board": bt, "inf": int(i.group(1)),
                                   "hit": int(i.group(2))})
            hb = HIST_RE.search(line)
            if hb:
                self.hists.append({"pc": pc, "buckets": [int(x) for x in
                                                          hb.group(1).rstrip(",").split(",")]})
            dd = DIST_RE.search(line)
            if dd:
                self.dists.append({"pc": pc, "max_pct": int(dd.group(1)),
                                   "over50_windows": int(dd.group(2))})
            fl = FLOW_RE.search(line)
            if fl:
                self.flows.append({"pc": pc, "board": bt, "pct": int(fl.group(1)),
                                   "lost_ms": int(fl.group(2)), "lossy": int(fl.group(3))})


def open_shell(port, baud, first_cmd="kws stop"):
    ser = serial.Serial(port, baud, timeout=1)
    ser.dtr = ser.rts = True
    time.sleep(0.9)
    ser.reset_input_buffer()
    ser.write(b"\r\n")
    time.sleep(0.5)
    ser.read(4096)
    if first_cmd:
        ser.write((first_cmd + "\r\n").encode())
        time.sleep(0.7)
        ser.read(4096)
    return ser


def seg_stats(levels, lo, hi):
    v = [l["rms"] for l in levels if lo <= l["pc"] <= hi]
    return {"n": len(v), "median": int(np.median(v)) if v else 0,
            "peak": int(max(v)) if v else 0}


def pcm_assets(peak, sr=16000, sec=2.0):
    """板载激励的 PCM（16kHz mono int16）。数字静音走的是**同一条放音代码路径**，
    只是 DAC 输出恒定中点——它是"电/机耦合"的对照臂，不是"什么都不做"的对照臂。"""
    t = np.arange(0, sec, 1.0 / sr)
    out = {}
    out["tone1k"] = (np.sin(2 * np.pi * 1000 * t) * 32767 * peak).astype("<i2")
    f0, f1 = 300.0, 3400.0
    k = np.log(f1 / f0) / t[-1]
    out["sweep"] = (np.sin(2 * np.pi * f0 * (np.exp(k * t) - 1) / k) * 32767 * peak).astype("<i2")
    out["silence"] = np.zeros(t.size, dtype="<i2")
    return out


def pcm_sig(x):
    """与固件 kws push 里 cap_cksum 同一定义：int16 符号扩展后按 uint32 累加。"""
    a = np.asarray(x, dtype="<i2").astype(np.int64)
    return {"n": int(a.size), "sum": int(a.sum()) & 0xFFFFFFFF,
            "peak": int(np.abs(a).max()) if a.size else 0}


class Resync(Exception):
    """板上样本数和序号对不上了（丢字符导致某行被截短）：局部补不了，整段重灌。"""


class BoardSource:
    """串口灌 PCM → 板上 RAM → 设备自己的喇叭放。见 kws.cpp 的 push/play 注释。"""

    def __init__(self, mon, ser, log=None):
        self.mon, self.ser, self.log = mon, ser, (log or (lambda *x: None))
        self.loaded = None
        self.stat = {}

    def _echo(self, on):
        """灌数据期间关掉控制台回显（固件 kws echo）。开回显时板子每收 78 字符就要
        原样发回 78 字符 + 提示符，TX 比 RX 还忙（rt_serial_putc 环满即自旋，把 tshell
        卡在发送上），128 字节 RX 环于是溢出，整批 push 变乱码。"""
        t = time.perf_counter()
        self.ser.write(("kws echo %d\r\n" % (1 if on else 0)).encode())
        line, pc, m = self.mon.wait_for(BOARD_RE["echo"], t, 5.0)
        if not m:
            raise RuntimeError("kws echo 无响应（固件太旧，重新编译烧录）")
        return int(m.group(1))

    def _start(self, n):
        t = time.perf_counter()
        self.ser.write(("kws pushstart %d\r\n" % n).encode())
        line, pc, m = self.mon.wait_for(BOARD_RE["start"], t, 8.0)
        if not m:
            raise RuntimeError("pushstart 无响应（固件是否含 kws push？）")
        cap = int(m.group(1))
        if n > cap:
            raise RuntimeError("素材 %d 样本 > 板上容量 %d" % (n, cap))
        return cap

    def _push_once(self, n):
        """停止等待：发一行 → 等这一行的回执 → 才发下一行。
        为什么不能定时盲发：实测板端消费速率远低于波特率上限，50ms/行 100% 收进去，
        20ms/行 **一行都进不去**（128 字节 RX 环被吃满，行首 "kws push NN" 整段丢失，
        只剩尾巴被当成乱命令 `…hex…: command not found`）。回执节奏由板子自己给，
        PC 不用猜速率。每行还核 len==已收样本数，被截短的行当场重灌整段。"""
        hexs = np.asarray(self.pcm, dtype="<i2").tobytes().hex()
        pos, seq, resend, errfix = 0, 0, 0, 0
        while pos < n:
            cnt = min(PUSH_SAMPLES, n - pos)
            want = pos + cnt
            self.ser.write(("kws push %d %s\r\n" % (seq, hexs[pos * 4:want * 4])).encode())
            t0 = time.perf_counter()
            ack = err = None
            while ack is None and err is None:
                if time.perf_counter() - t0 > 3.0:
                    break
                _, _, ack = self.mon.wait_for(BOARD_RE["ack"], t0, 0.2)
                if ack:
                    break
                _, _, err = self.mon.wait_for(BOARD_RE["err"], t0, 0.2)
            if err is not None:
                if err.lastindex != 1:
                    raise Resync("板端拒收该行（hex 非法）: %s" % err.group(0))
                exp = int(err.group(1))
                if "playing" in err.group(0):
                    raise Resync("板上还在放音（上一段没等完 PLAY_DONE）")
                if exp > seq:                      # 我重发过，板子其实早收了 → 对齐到期望值
                    self.log("    第 %d 行板子已收（期望 %d）→ 对齐续发" % (seq, exp))
                else:
                    self.log("    板上只到 %d 行（我发到 %d）→ 从 %d 行补发" % (exp, seq, exp))
                errfix += 1
                if errfix > 20:
                    raise Resync("序号错位补救 20 次仍不收敛（串口链路不稳）")
                seq, pos = exp, exp * PUSH_SAMPLES
                continue
            if ack is None:
                if resend > 8:
                    raise Resync("第 %d 行连发 8 次无回执（串口链路不稳）" % seq)
                resend += 1          # 行首被吃掉：同一序号原样重发，板上期望值没变
                continue
            sq, ln = int(ack.group(1)), int(ack.group(2))
            if ln != min(want, n):
                raise Resync("第 %d 行后板上 len=%d（应为 %d）→ 有行被截短"
                             % (sq, ln, min(want, n)))
            pos, seq = want, seq + 1
        return seq, resend + errfix

    def push(self, name, pcm, attempts=3):
        sig = pcm_sig(pcm)
        self.pcm = pcm
        t_send = time.perf_counter()
        self._echo(False)
        try:
            self._start(sig["n"])
            last = None
            for k in range(attempts):
                try:
                    seq, retries = self._push_once(sig["n"])
                    self.stat = self._verify(name, sig, seq, retries,
                                             round(time.perf_counter() - t_send, 1))
                    break
                except Resync as e:
                    last = e
                    self.log("  整段重灌（第 %d/%d 次）：%s" % (k + 2, attempts, e))
                    self._start(sig["n"])
            else:
                raise RuntimeError("灌入失败：%s" % last)
        finally:
            try:
                self._echo(True)
            except Exception as e:
                self.log("  回显恢复失败（不影响本轮数据）：%s" % e)
        self.loaded = name
        self.log("  灌入 %s：%d 样本 / %.2fs 音频，%d 行(续发%d)，用时 %.1fs，校验和 %u 一致"
                 % (name, sig["n"], sig["n"] / 16000.0, seq, retries, self.stat["sec"],
                    sig["sum"]))
        return self.stat

    def _verify(self, name, sig, seq, retries, sec):
        t = time.perf_counter()
        self.ser.write(b"kws pushstat\r\n")
        line, pc, m = self.mon.wait_for(BOARD_RE["stat"], t, 15.0)
        if not m:
            raise RuntimeError("pushstat 无响应")
        st = dict(zip(("len", "cap", "seq", "drop", "sum", "peak", "running"),
                      [int(g) for g in m.groups()]))
        out = {"clip": name, "want": sig, "board": st, "lines": seq, "resync": retries,
               "sec": sec}
        if (st["len"], st["sum"], st["peak"], st["drop"]) != \
           (sig["n"], sig["sum"], sig["peak"], 0):
            raise Resync("剂量对不上（板上 %s vs PC %s）\n  %s" % (st, sig, line))
        return out

    def play(self, loops=1, tries=4):
        """返回 (放音起点板时戳, PC 时戳, 素材秒数)。起点用固件 PLAY_BEGIN 里的 `@ms`，
        和"唤醒词命中"那行是同一把板端尺子——归因不需要 PC/板时钟拟合。
        PLAY_ERR busy（上一遍刚结束还没腾出标志）重试即可，其它错误直接失败。"""
        for k in range(tries):
            t_send = time.perf_counter()
            self.ser.write(("kws play %d\r\n" % loops).encode())
            line, pc, m = self.mon.wait_for(BOARD_RE["begin"], t_send, 8.0)
            if m:
                bt = BOARD_TS.search(line)
                if bt is None:
                    raise RuntimeError("PLAY_BEGIN 不带板端时基 @ms（固件太旧，重编重烧）")
                return float(bt.group(1)) / 1000.0, pc, int(m.group(2)) / 16000.0 * loops
            eline, _, _ = self.mon.wait_for(BOARD_RE["playerr"], t_send, 0.0)
            if eline and "busy" not in eline:
                raise RuntimeError("放音被拒：%s" % eline)
            self.log("  板端正忙，0.4s 后重试放音（第 %d/%d 次）" % (k + 1, tries))
            time.sleep(0.4)
        raise RuntimeError("放音没起来（重试 %d 次仍无 PLAY_BEGIN）" % tries)

    def wait_done(self, after_pc, timeout=120.0):
        """等 PLAY_DONE，返回板端实测放音秒数（管道欠载时它比标称值长，归因窗口用它更准）。"""
        line, pc, m = self.mon.wait_for(BOARD_RE["done"], after_pc - 0.5, timeout)
        return (int(m.group(2)) / 1000.0) if m else None


class PcSource:
    """PC 声卡 → 外接音箱 → 空气 → 板载麦。与 BoardSource 接口对齐，可直接替换。

    与板载源的差异（都是"更简单"的方向）：
      - 不走串口灌数：没有板上 RAM 容量上限、没有 push 回执/乱码/序号错位风险；
      - 起播由 waveOutWrite 返回时刻决定，精度与板载 PLAY_BEGIN 同级；
      - **但声压不受脚本控制**：板载源能改 `feather_audio_volume`，PC 源只能靠系统音量。
        所以跑之前必须把 Windows 音量固定在某一档并记下来，跑完不要动——否则同一批
        语料里混进不同声压，域会脏。

    ⚠ 前置闸门：`--phase proof --source pc` 必须先通过。
      该臂历史上被判过不可信（"数字静音对照和真音一样高"），机制是 PC 与板子共地串扰
      或声卡底噪——那种情况下采到的"空气声"其实是电耦合，语料全废。
      判定看 proof 输出：真实出声抬升 ≥3× 且 数字静音对照 <2×。
    """

    def __init__(self, log=None, device=None):
        self.log = log or (lambda *x: None)
        self.player = Player(device=device)
        self.pcm = None
        self.name = None

    def push(self, name, pcm, attempts=1):
        """与 BoardSource.push 同签名；PC 源只是把 PCM 留在内存里。"""
        self.pcm = np.ascontiguousarray(np.asarray(pcm, dtype="<i2"))
        self.name = name
        return {"clip": name, "want": {"n": int(self.pcm.size)}, "board": {},
                "lines": 0, "resync": 0, "sec": self.pcm.size / 16000.0}

    def play(self, loops=1, tries=2):
        """返回 (板端时戳, PC 时戳, 素材秒数)。PC 源没有板端时基，第一项给 0.0。"""
        if self.pcm is None:
            raise RuntimeError("PC 源没有素材（先 push 再 play）")
        data = np.tile(self.pcm, loops) if loops > 1 else self.pcm
        for k in range(tries):
            try:
                t, dur = self.player.play_pcm(data)
                return 0.0, t, dur
            except RuntimeError as e:
                if k == tries - 1:
                    raise
                self.log("  PC 放音失败（%s），0.4s 后重试" % e)
                time.sleep(0.4)

    def wait_done(self, after_pc, timeout=120.0):
        """PC 播放是同步的（waveOutWrite 已把整段交给驱动），没有等价的 PLAY_DONE。"""
        return None

    def shutdown(self):
        self.player.shutdown()


def mic_run(mon, ser, sec):
    """跑一条 kws mic（它占住控制台线程，所以期间发不进任何命令），取逐帧电平统计。"""
    t0 = time.perf_counter()
    ser.write(("kws mic %d\r\n" % sec).encode())
    line, pc, m = mon.wait_for(MIC_SUMMARY_RE, t0, sec + 25.0)
    return seg_stats(mon.levels, t0 + 0.4, t0 + sec + 2.5), (list(m.groups()) if m else None)


def phase_proof_board(a, flog, ser, mon):
    """机内声路证明：同一段 kws mic 时长，分别在"放 1kHz / 放扫描 / 放数字静音"下取峰值电平，
    与不放音的空场基线对比。真音必须显著抬升（听得到），数字静音不得抬升（抬了就是电/机耦合
    或共地串扰，不是声学，后面的召回率就不能信）。"""
    def say(s):
        print(s)
        flog.write(s + "\n")
        flog.flush()

    src = BoardSource(mon, ser, log=say)
    assets = pcm_assets(a.gain, sec=a.clip_sec)
    vol_re = re.compile(r"sound0 volume=(\d+)")   # 带等号才撞得上回执，撞不到命令回显
    t = time.perf_counter()
    ser.write(b"feather_audio_volume\r\n")
    line, pc, m = mon.wait_for(vol_re, t, 5.0)
    vol0 = int(m.group(1)) if m else None
    t = time.perf_counter()
    ser.write(("feather_audio_volume %d\r\n" % a.volume).encode())
    line, pc, m = mon.wait_for(vol_re, t, 5.0)
    say("喇叭音量: %s → %s（跑完还原到 %s）"
        % (vol0, m.group(1) if m else "?", vol0))

    loops = int(np.ceil((a.sec + 2.0) / a.clip_sec))
    say("计划：基线×2 + %s，每条 mic %ds，放音 %d 遍(%.1fs) 罩住整条 mic"
        % ("/".join(n for n, _ in PROOF_ORDER), a.sec, loops, loops * a.clip_sec))
    base_a, _ = mic_run(mon, ser, a.sec)
    runs = [{"seg": "baseline_a", "kind": "off", **base_a}]
    pushes = []
    for name, kind in PROOF_ORDER:
        key = "tone1k" if name == "tone_again" else name
        src.push(name, assets[key])
        pushes.append(dict(src.stat))
        onset, pcb, span = src.play(loops)
        st, summ = mic_run(mon, ser, a.sec)
        src.wait_done(pcb, timeout=span + 30.0)
        runs.append({"seg": name, "kind": kind, "onset_board": onset, "loops": loops,
                     "play_span": round(span, 1), "board_summary": summ, **st})
        say("  %-11s %s 段内 RMS 中位 %6d 峰值 %6d（%d 帧）"
            % (name, "音" if kind == "tone" else "静音对照", st["median"], st["peak"], st["n"]))
    base_b, _ = mic_run(mon, ser, a.sec)
    runs.append({"seg": "baseline_b", "kind": "off", **base_b})
    if vol0 is not None and a.volume != vol0:
        t = time.perf_counter()
        ser.write(("feather_audio_volume %d\r\n" % vol0).encode())
        mon.wait_for(vol_re, t, 5.0)

    denom = max(base_a["peak"], base_b["peak"], 1)
    for r in runs:
        r["lift_vs_base"] = round(r["peak"] / denom, 2)
    say("  基线峰值 A %d / B %d（取大者做分母，抬升算得偏保守）"
        % (base_a["peak"], base_b["peak"]))
    tones = [r["lift_vs_base"] for r in runs if r["kind"] == "tone"]
    ctrls = [r["lift_vs_base"] for r in runs if r["kind"] == "ctrl"]
    acoustic = max(tones or [0])
    ctrl = max(ctrls or [0])
    ok = acoustic >= 3.0 and ctrl < 2.0
    say("  ---- 判定：真实出声抬升 %.1f×，数字静音对照 %.1f× → 机内声路%s"
        % (acoustic, ctrl, "成立（可跑矩阵）" if ok else "不成立"))
    return {"source": "board", "volume": a.volume, "volume_before": vol0,
            "baseline": base_a, "baseline_b": base_b,
            "segments": runs, "pushes": pushes, "acoustic_lift": acoustic,
            "silence_control_lift": ctrl, "path_ok": bool(ok), "loops": loops}


PROOF_ORDER = (("tone1k", "tone"), ("sweep", "tone"), ("silence", "ctrl"),
               ("tone_again", "tone"))


def phase_proof_pc(a, flog, ser, mon):
    """对照臂：PC 默认输出→空气→板麦。本机已判不可信（数字静音对照和真音一样高），
    留着只为在换 PC/换喇叭时重新判一次外部声路能不能用。"""
    assets = make_assets(os.path.join(LOG_DIR, "rig_assets"), a.gain)
    ser.write(b"feather_audio_status\r\n")
    time.sleep(1.2)
    audio_status = "\n".join(l for _, l in mon.lines[-40:] if "sound0" in l or "mic0" in l)
    player = Player(device=a.pc_dev_index)
    dur = a.sec + 6
    ser.write(("kws mic %d\r\n" % dur).encode())
    time.sleep(1.2)
    t0 = mon.t0
    marks = []
    for name, when in (("tone1k", 2.0), ("sweep", 5.5), ("silence", 9.0), ("tone_again", 12.0)):
        key = "tone1k" if name == "tone_again" else name
        time.sleep(max(0.0, (t0 + when) - time.perf_counter()))
        st, d = player.play(assets[key])
        marks.append({"seg": name, "rel": when, "pc_start": st, "pc_end": st + d})
    time.sleep(max(0.0, (t0 + dur + 1.5) - time.perf_counter()))
    player.shutdown()

    baseline = seg_stats(mon.levels, t0 + 0.5, t0 + 1.9)
    rows = []
    for m in marks:
        s = seg_stats(mon.levels, m["pc_start"] + 0.2, m["pc_end"] + 0.4)
        lift = s["peak"] / max(baseline["peak"], 1)
        rows.append({**m, **s, "lift_vs_base": round(lift, 2)})
        print("  %-10s 段内 RMS 中位 %6d 峰值 %6d（%d 帧）→ 相对空场基线峰值 %5.1f×" %
              (m["seg"], s["median"], s["peak"], s["n"], lift))
    acoustic = max([r["lift_vs_base"] for r in rows if r["seg"] != "silence"] or [0])
    ctrl = [r for r in rows if r["seg"] == "silence"]
    ctrl_lift = ctrl[0]["lift_vs_base"] if ctrl else 0
    ok = acoustic >= 3.0 and ctrl_lift < 2.0
    print("  ---- 判定：真实出声抬升 %.1f×，数字静音对照 %.1f× → 声路%s" %
          (acoustic, ctrl_lift, "成立（可跑矩阵）" if ok else "不成立"))
    return {"source": "pc", "baseline": baseline, "segments": rows,
            "levels_n": len(mon.levels), "acoustic_lift": acoustic,
            "silence_control_lift": ctrl_lift, "path_ok": bool(ok),
            "audio_status": audio_status}


def phase_proof(a, flog):
    ser = open_shell(a.port, a.baud)
    mon = Monitor(ser, flog)
    mon.start()
    try:
        out = phase_proof_board(a, flog, ser, mon) if a.source == "board" \
            else phase_proof_pc(a, flog, ser, mon)
    finally:
        mon.stop_now()
        ser.close()
    tail = [l for _, l in mon.lines if "判定" in l or "电平 RMS" in l]
    out["board_summary_lines"] = tail[-8:]
    return out


def pick_scenarios(a):
    scenarios = default_scenarios()
    if a.reply_dir and os.path.isdir(a.reply_dir):
        fs = [os.path.join(a.reply_dir, n) for n in sorted(os.listdir(a.reply_dir))
              if n.endswith(".wav")]
        if fs:
            hit = [s for s in scenarios if s["name"] == "reply"]
            if hit:
                hit[0]["files"] = fs
            else:
                scenarios.insert(4, {"name": "reply", "kind": "neg", "files": fs})
    return [s for s in scenarios if s["files"]]


def union_len(intervals):
    total, cur = 0.0, None
    for lo, hi in sorted(intervals):
        if cur is None or lo > cur[1]:
            total += hi - lo
            cur = (lo, hi)
        else:
            cur = (cur[0], max(cur[1], hi))
            total += 0
    return total


def phase_matrix_board(a, flog):
    """生产判决路径（kws start）+ 机内放音：每段素材先灌进 RAM 再由设备自己的喇叭放一遍。
    归因用的是 PLAY_BEGIN 的**板载时戳**，不是 PC 时钟——这是这台仪器比 pc 臂硬的地方。
    灌数据那十几秒是真静音，正好用来量空场误触底噪。"""
    def say(s):
        print(s)
        flog.write(s + "\n")
        flog.flush()

    ser = open_shell(a.port, a.baud)
    scenarios = pick_scenarios(a)
    if not scenarios:
        print("!! data/ 下没有可用场景 WAV")
        ser.close()
        return {}
    texts = load_texts()

    mon = Monitor(ser, flog)
    mon.start()
    src = BoardSource(mon, ser, log=say)
    vol_re = re.compile(r"sound0 volume=(\d+)")
    t = time.perf_counter()
    ser.write(("feather_audio_volume %d\r\n" % a.volume).encode())
    mon.wait_for(vol_re, t, 5.0)
    t = time.perf_counter()
    ser.write(b"kws reset\r\n")
    mon.wait_for(DIST_RE, t, 5.0)
    ser.write(b"kws start\r\n")
    time.sleep(2.0)
    t = time.perf_counter()
    ser.write(b"kws info\r\n")
    mon.wait_for(INFO_RE, t, 8.0)
    base = mon.infos[-1] if mon.infos else None
    cap = int(a.max_sec * 16000)

    plan = "  ".join("%s×%d(%s)" % (s["name"], a.rounds, s["kind"]) for s in scenarios)
    say("计划：%s   每段之间真静音 %.1fs（灌数据的时间）" % (plan, a.gap))
    rng = np.random.default_rng(a.seed)
    events = []
    for sc in scenarios:
        for r in range(a.rounds):
            path = sc["files"][int(rng.integers(0, len(sc["files"])))]
            clip = os.path.basename(path)
            pcm = load_pcm(path)
            if sc.get("level", True):        # 与 traindata 同档，训练/验收条件才一致；环境音保持原生
                pcm, _g = fit_level(pcm, a.stim_target, a.stim_peak_cap)
            raw_n = pcm.size
            min_n = int(a.min_sec * 16000)
            gap = np.zeros(int(a.loop_gap * 16000), dtype=pcm.dtype)
            tiled, reps, tot = [], 0, 0
            while raw_n < min_n and tot + raw_n + gap.size <= cap:
                tiled += [pcm, gap]
                tot += raw_n + gap.size
                reps += 1
            if reps:
                pcm = np.concatenate(tiled)      # 短素材连放几次，中间留 loop_gap 秒真静音
            trunc = pcm.size > cap
            if trunc:
                pcm = pcm[:cap]
            plays = reps if reps else 1
            tag = "%s#%d" % (sc["name"], r)
            try:
                src.push(tag, pcm)
                onset, pcb, span = src.play(1)
            except RuntimeError as e:
                say("  !! %s 放弃：%s" % (tag, e))
                continue
            ev = {"tag": tag, "scenario": sc["name"], "kind": sc["kind"], "clip": clip,
                  "text": texts.get(os.path.normpath(path), ""), "board_onset": onset, "span": round(span, 2),
                  "pc_start": pcb, "truncated": bool(trunc), "repeats": plays,
                  "push": dict(src.stat),
                  "push_sec": round(src.stat.get("sec", 0.0), 1), "hits": []}
            events.append(ev)
            note = "×%d 连放" % plays if plays > 1 else ("（截断）" if trunc else "")
            say("  %-11s r%-2d %-20s %.2fs  板载放音起点 %8.2fs %s"
                % (sc["name"], r, clip, span, onset if onset is not None else -1, note))
            dur = src.wait_done(pcb, timeout=span + 30.0)
            if dur:
                ev["span"] = round(dur, 2)      # 板端实测时长，欠载拖长也算进归因窗口
            time.sleep(max(0.5, a.gap))

    t = time.perf_counter()
    ser.write(b"kws info\r\n")
    mon.wait_for(DIST_RE, t, 15.0)
    end = mon.infos[-1] if mon.infos else None
    ser.write(b"kws stop\r\n")
    time.sleep(1.0)
    mon.stop_now()
    ser.close()

    for e in events:
        if e["board_onset"] is None:
            continue
        e["w0"] = e["board_onset"] - 0.3
        e["w1"] = e["board_onset"] + e["span"] + a.slack
    timed = [e for e in events if e["board_onset"] is not None]
    for h in mon.hits:
        bt = h["board"]
        cands = [e for e in timed if e["w0"] <= bt <= e["w1"]] if bt is not None else []
        if not cands:
            h["owner"] = "quiet"
        elif len(cands) == 1:
            h["owner"] = cands[0]["scenario"]
            cands[0]["hits"].append(h)
        else:
            near = min(cands, key=lambda e: abs(bt - (e["w0"] + e["w1"]) / 2.0))
            h["owner"] = near["scenario"]
            h["overlap"] = True
            near["hits"].append(h)
    quiet_hits = [h for h in mon.hits if h["owner"] == "quiet"]

    t_lo = min([e["w0"] for e in timed] or [0]) - 5.0
    t_hi = max([e["w1"] for e in timed] or [0]) + 5.0
    listen_sec = (end["board"] - base["board"]) if (end and base and end["board"]
                                                    and base["board"]) else None
    # `kws info` 那行没有 @ms（只有放音/命中行有），拿不到板钟时**用板自己的推理计数当时钟**：
    # 生产路径每 hop(0.5s) 恰好一次推理，推理增量×0.5s 就是"板子自认为监听了多久"。
    d_inf0 = (end["inf"] - base["inf"]) if (end and base) else None
    clock_src = "板钟"
    if not listen_sec and d_inf0:
        listen_sec, clock_src = d_inf0 * 0.5, "推理计数×0.5s"
    onwindow_sec = union_len([(max(e["w0"], t_lo), min(e["w1"], t_hi)) for e in timed])
    quiet_sec = max(0.0, (listen_sec or 0.0) - onwindow_sec)

    rows = []
    for name in [s["name"] for s in scenarios]:
        es = [e for e in timed if e["scenario"] == name]
        if not es:
            continue
        kind = es[0]["kind"]
        hits = sum(len(e["hits"]) for e in es)
        sec = sum(e["span"] + a.slack for e in es)
        r = {"scenario": name, "kind": kind, "plays": len(es), "audio_sec": round(sec, 1),
             "hits": hits, "max_score": max([x["score"] for e in es for x in e["hits"]] or [0]),
             "hit_plays": sum(1 for e in es if e["hits"]),
             "clips": [{"clip": e["clip"], "text": e["text"], "sec": e["span"],
                        "hits": [x["score"] for x in e["hits"]]} for e in es]}
        r["recall"] = round(r["hit_plays"] / r["plays"], 3) if kind == "pos" else None
        r["fa_per_hour"] = round(hits / sec * 3600, 1) if sec else 0.0
        rows.append(r)
        say("  %-11s %s 播%d次/%5.1fs窗 命中%2d 峰%3d%%  %s" %
            (name, "正" if kind == "pos" else "负", r["plays"], r["audio_sec"], hits,
             r["max_score"], ("召回 %.0f%%" % (100 * r["recall"])) if r["recall"] is not None
             else ("误触 %.1f 次/小时" % r["fa_per_hour"])))
    q_rate = (len(quiet_hits) / quiet_sec * 3600) if quiet_sec > 1 else None
    say("  ---- 空场（暴露 %.0fs，含灌数据时间）命中 %d → %s 次/小时" %
        (quiet_sec, len(quiet_hits), ("%.1f" % q_rate) if q_rate is not None else "样本不足"))
    if listen_sec:
        say("  ---- 监听总时长 %.0fs（时钟源：%s）  出声窗合计 %.0fs" %
            (listen_sec, clock_src, onwindow_sec))
    # 两个暴露量别混：**墙钟**（产品口径的"每小时"）与**模型真实消费到的音频秒数**
    # = 推理增量 × hop(0.5s)。采集挖洞时后者更小，用它算的误触率才是"板子听到多少判了多少"。
    d_inf = end["inf"] - base["inf"] if end and base else None
    d_hit = end["hit"] - base["hit"] if end and base else None
    heard = round(d_inf * 0.5, 1) if d_inf else None
    flow = mon.flows[-1]["pct"] if mon.flows else None
    quiet_heard = max(0.0, (heard or 0.0) - onwindow_sec) if heard else None
    q_heard = (len(quiet_hits) / quiet_heard * 3600) if (quiet_heard and quiet_heard > 1) else None
    say("  ---- 板计数：推理 +%s ⇒ 模型消费 ≈%s 秒音频  命中 +%s（日志命中 %d 条）"
        "（交付率 %s%%）" %
        (d_inf, heard, d_hit, len(mon.hits), flow if flow is not None else "未报"))
    if q_heard is not None:
        say("  ---- 空场按消费秒重算：暴露 %.0fs 命中 %d → %.1f 次/小时" %
            (quiet_heard, len(quiet_hits), q_heard))
    if mon.dists:
        say("  ---- 分数直方图 %s" % mon.hists[-1]["buckets"] if mon.hists else "")
        say("  ---- 分布：最高 %d%%，>50%% 共 %u 窗（占推理 %s）" %
            (mon.dists[-1]["max_pct"], mon.dists[-1]["over50_windows"],
             "%.1f%%" % (100.0 * mon.dists[-1]["over50_windows"] / d_inf) if d_inf else "n/a"))

    strip = lambda e: {k: v for k, v in e.items() if k not in ("hits", "push")}
    return {"source": "board", "volume": a.volume, "rows": rows,
            "events": [strip(e) | {"hits": [h["score"] for h in e["hits"]]} for e in events],
            "pushes": [e["push"] for e in events],
            "quiet_sec": round(quiet_sec, 1), "quiet_hits": len(quiet_hits),
            "quiet_fa_per_hour": round(q_rate, 1) if q_rate is not None else None,
            "board_flow_pct": flow, "heard_sec": heard,
            "quiet_heard_sec": round(quiet_heard, 1) if quiet_heard else None,
            "clock_src": clock_src,
            "quiet_fa_per_hour_heard": round(q_heard, 1) if q_heard is not None else None,
            "listen_sec": round(listen_sec, 1) if listen_sec else None,
            "hits": [{k: v for k, v in h.items() if k != "raw"} for h in mon.hits],
            "scores": mon.scores[-600:],
            "score_hist": mon.hists[-1] if mon.hists else None,
            "score_dist": mon.dists[-1] if mon.dists else None,
            "base_info": base, "end_info": end,
            "board_delta": {"inferences": d_inf, "hits": d_hit},
            "scenarios": [s["name"] for s in scenarios]}


def phase_matrix(a, flog):
    if a.source == "board":
        return phase_matrix_board(a, flog)
    ser = open_shell(a.port, a.baud)
    scenarios = pick_scenarios(a)
    if not scenarios:
        print("!! data/ 下没有可用场景 WAV")
        ser.close()
        return {}
    texts = load_texts()

    base = read_info(ser)[0]
    ser.write(b"kws start\r\n")
    time.sleep(1.5)
    base = read_info(ser)[0]
    mon = Monitor(ser, flog)
    mon.start()

    player = Player(device=a.pc_dev_index)
    rng = np.random.default_rng(a.seed)
    events, quiet = [], []
    print("计划：%s" % "  ".join("%s×%d(%s)" % (s["name"], a.rounds, s["kind"]) for s in scenarios))
    for sc in scenarios:
        for r in range(a.rounds):
            path = sc["files"][int(rng.integers(0, len(sc["files"])))]
            st, d = player.play(path)
            events.append({"scenario": sc["name"], "kind": sc["kind"],
                           "clip": os.path.basename(path),
                           "text": texts.get(os.path.normpath(path), ""),
                           "pc_start": st, "pc_end": st + d})
            print("  %-11s r%d %-16s %.2fs  %s" % (sc["name"], r, events[-1]["clip"], d,
                  datetime.datetime.fromtimestamp(time.time()).strftime("%H:%M:%S")))
            time.sleep(max(0.3, st + d + a.gap - time.perf_counter()))
        player.close()
        q0 = time.perf_counter()
        time.sleep(a.quiet)
        quiet.append({"pc_start": q0, "pc_end": time.perf_counter()})
    player.close()
    time.sleep(a.slack + 1.0)
    mon.stop_now()
    end = read_info(ser)[0]
    ser.write(b"kws stop\r\n")
    ser.close()

    clock = fit_clock(mon.anchors)
    if clock:
        def to_board(pc):
            return clock["a"] * (pc - clock["t0_pc"]) + clock["b"]
        print("时钟映射 board=%.5f·(pc-t0)+%.3f  锚点%d 残差RMS=%.3fs" %
              (clock["a"], clock["b"], clock["anchors"], clock["rms_sec"]))
    else:
        print("!! 锚点不足(%d)，退回用 PC 相对时刻归因" % len(mon.anchors))
        def to_board(pc):
            return pc - mon.t0

    for grp in (events, quiet):
        for it in grp:
            it["board_start"] = to_board(it["pc_start"])
            it["board_end"] = to_board(it["pc_end"])
            it["hits"] = []
    for h in mon.hits:
        bt = h["board"] if h["board"] is not None else to_board(h["pc"])
        h["board"] = bt
        h["owner"] = "gap"
        for e in events:
            if e["board_start"] - 0.3 <= bt <= e["board_end"] + a.slack:
                h["owner"] = e["scenario"]
                e["hits"].append(h)
                break
        if h["owner"] == "gap":
            for i, q in enumerate(quiet):
                if q["board_start"] <= bt <= q["board_end"]:
                    h["owner"] = "quiet"
                    q["hits"].append(h)
                    break

    rows = []
    for e in events:
        r = next((x for x in rows if x["scenario"] == e["scenario"]), None)
        if r is None:
            r = {"scenario": e["scenario"], "kind": e["kind"], "plays": 0, "audio_sec": 0.0,
                 "hits": 0, "hit_plays": 0, "max_score": 0, "clips": []}
            rows.append(r)
        r["plays"] += 1
        r["audio_sec"] += e["pc_end"] - e["pc_start"]
        r["hits"] += len(e["hits"])
        r["hit_plays"] += 1 if e["hits"] else 0
        r["max_score"] = max([r["max_score"]] + [x["score"] for x in e["hits"]])
        r["clips"].append({"clip": e["clip"], "text": e["text"],
                           "hits": len(e["hits"]),
                           "scores": [x["score"] for x in e["hits"]]})
    rows.sort(key=lambda x: x["scenario"])
    for r in rows:
        r["audio_sec"] = round(r["audio_sec"], 2)
        r["fa_per_hour"] = round(r["hits"] / r["audio_sec"] * 3600, 1) if r["audio_sec"] else 0.0
        r["recall"] = round(r["hit_plays"] / r["plays"], 3) if r["kind"] == "pos" else None
        print("  %-11s %s 播%d次/%5.1fs 命中%2d 峰%3d%%  %s" %
              (r["scenario"], "正" if r["kind"] == "pos" else "负", r["plays"], r["audio_sec"],
               r["hits"], r["max_score"],
               ("召回 %.0f%%" % (100 * r["recall"])) if r["recall"] is not None
               else ("误触 %.1f 次/小时" % r["fa_per_hour"])))
    q_hits = sum(len(q["hits"]) for q in quiet)
    q_sec = sum(q["pc_end"] - q["pc_start"] for q in quiet)
    gap_hits = [h for h in mon.hits if h["owner"] == "gap"]
    print("  ---- 出声间隙(%.0fs) 命中 %d → %.1f 次/小时；计划外时段命中 %d；总命中 %d" %
          (q_sec, q_hits, (q_hits / q_sec * 3600) if q_sec else 0, len(gap_hits), len(mon.hits)))
    d_inf = end[0] - base[0] if base and end else None
    d_hit = end[1] - base[1] if base and end else None
    print("  ---- 板计数：推理 +%s  命中 +%s（日志命中 %d 条）" % (d_inf, d_hit, len(mon.hits)))
    strip = lambda it: {k: v for k, v in it.items() if k != "hits"}
    return {"clock": clock, "rows": rows,
            "events": [strip(e) | {"hits": [h["score"] for h in e["hits"]]} for e in events],
            "quiet": [strip(q) | {"hits": len(q["hits"])} for q in quiet],
            "quiet_hits": q_hits, "gap_hits": len(gap_hits), "hits": mon.hits,
            "scores": mon.scores[-400:],
            "board_delta": {"inferences": d_inf, "hits": d_hit},
            "base_info": list(base) if base else None, "end_info": list(end) if end else None,
            "scenarios": [s["name"] for s in scenarios]}


CAP_RE = re.compile(r"KWS_CAPTURE (\d+) (\d+)")
DUMP_BEGIN_RE = re.compile(r"KWS_DUMP_BEGIN (\d+) (\d+) (\d+) (-?\d+)")
DUMP_END_RE = re.compile(r"KWS_DUMP_END")
# 麦克风停流标记（固件看门狗）：采集/回吐/推理任一条路径连续 1.5s 读不到样本就会打这条
STALL_RE = re.compile(r"KWS_(?:CAPTURE|DUMP|MIC)_STALL")
# 帧行 = K + 4 位十六进制帧号 + 640 位十六进制样本（160 个 int16，每样本 4 hex、大端）。
# 解析不再用"整行必须 640 hex"的正则：那样会把被别的线程日志插断的帧整条记成洞，
# 改由 parse_frame_rec + stitch_frames 处理（见各自 docstring）。
HEXCH = "0123456789abcdefABCDEF"


def hex_head_len(s):
    n = 0
    while n < len(s) and s[n] in HEXCH:
        n += 1
    return n


def parse_frame_rec(s):
    """把串口一条记录分成三类：('head', 帧号, hex) / ('tail', None, hex) / None。

    为什么要分三类：645 字符一行的帧回吐**不保证原子**——板上 rt_kprintf 是分块塞进 TX 环的，
    别的线程（[FeatherTalk]/[BOND]）在块与块之间插一嘴，一条帧行就断成
        K0052fa9dfac6…（前 511 个 hex）
        cda01dc02b4011…（剩下的尾巴，纯 hex）
    按"整行必须 640 hex"的判据，这类帧全记成洞；实测 8s 采集 600 帧里断 8 帧。尾巴本身不带
    帧号，所以只能靠"紧跟在没拼满的 head 之后"来认（见 stitch_frames）。
    """
    s = s.strip()
    if not s:
        return None
    if s[0] == "K" and len(s) >= 5 and all(c in HEXCH for c in s[1:5]):
        n = hex_head_len(s[5:])
        return ("head", int(s[1:5], 16), s[5:5 + n])
    n = hex_head_len(s)
    if n >= 8 and n == len(s):
        return ("tail", None, s)
    return None


def stitch_frames(records):
    """记录流 → ({帧号: 160 个 int16}, 未拼回头的帧号列表)。

    字节序：固件 dump_frame 按 **nibble 高位在前**打印每个 int16（v>>12,v>>8,v>>4,v&0xF），
    一个样本 4 个 hex = 2 字节大端 ⇒ 必须用 ">i2" 解释；写成 "<i2" 是字节交换，
    0x00B5(181) 变 0xB500(-18944)，重建出来的"板载音频"是一片满幅乱码（实测峰值 32768，
    而板上头部自报峰值只有 4043）。kws_eval.py 一直用的就是大端。

    拼接只认两条硬规则，宁缺勿错：尾巴必须接在**还没拼满的 head** 后面；一旦下一条 head 来了
    就丢掉未完成的 head（宁可少一帧，也不能把别人的字节当成这帧的样本喂进训练集）。
    """
    out, pend, dropped = {}, None, []
    for kind, idx, hx in records:
        if kind == "head":
            if pend is not None and len(pend[1]) < 640:
                dropped.append(pend[0])
            pend = [idx, hx]
            if len(hx) >= 640:
                out[idx] = np.frombuffer(bytes.fromhex(hx[:640]), dtype=">i2")
                pend = None
        elif pend is not None and len(pend[1]) < 640:
            pend[1] += hx
            if len(pend[1]) >= 640:
                out[pend[0]] = np.frombuffer(bytes.fromhex(pend[1][:640]), dtype=">i2")
                pend = None
    if pend is not None and len(pend[1]) < 640:
        dropped.append(pend[0])
    return out, dropped


def wav_write_i16(path, x):
    """原样落 int16，不做峰值归一（归一会把板载真实增益抹掉，训练就失去意义）。"""
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(16000)
        w.writeframes(np.asarray(x, dtype="<i2").tobytes())
    return x.size / 16000.0


def env_dose(stim, mic, hop=160, max_lag_sec=2.0):
    """激励包络 ↔ 采集包络的归一互相关：一次回答两个问题——
       ① 声音是否真的经喇叭→空气→麦这条**声学**路进来（相关系数，不是靠时钟推断）；
       ② 词落在缓冲哪里（lag）。`kws capture` 的起点没有 @ms，只有从音频本身推才靠得住。

       lag 搜索上限取 2.0s：10-19 实测本回路延迟 0.14s（wake_01 单放，corr 0.765，
       max_lag 3/6/9 三档同峰），起录→放音之间只有命令往返。上限放宽到 6~9s 不会
       多找回真峰，只会让远处的假峰有机会赢——lag 直接决定正样本窗的标签位置，
       选错就是整段语料的标签错位。同峰（差 ≤0.01）时取更早的那个，同理。

       corr 的口径要讲清：分子是**两边都按整段标准化后的包络**的点积除以长度，激励侧
       单位方差、采集侧只是全局标准化后截出来的一段（局部方差不等于 1），所以它是
       "标准化包络协方差"而非严格的 [0,1] 相关系数——实测能到 1.004。用它做**有无**
       剂量判据（阈值 0.30，数字静音对照远低于此）成立，当百分比解读不成立。"""
    def env(v):
        a = np.abs(np.asarray(v, dtype=np.float64))
        n = a.size // hop
        return a[:n * hop].reshape(n, hop).mean(axis=1)

    es, em = env(stim), env(mic)
    def z(v):
        v = v - v.mean()
        s = float(v.std())
        return v / s if s > 0 else v
    es, em = z(es), z(em)
    if es.size < 20 or em.size <= es.size:
        return 0.0, None
    best, lag = -9.0, 0
    hi = min(int(max_lag_sec * 16000 / hop), em.size - es.size)
    for k in range(hi + 1):
        a = em[k:k + es.size]
        c = float(np.dot(es, a) / es.size)
        if c > best + 0.01:
            best, lag = c, k
    return round(best, 3), round(lag * hop / 16000.0, 3)


def rep_split(mic, lag_sec, clip_sec, reps, loop_gap, hop=160):
    """按 tiling 布局把采集切成"词所在段"和"段间真静音"，比值证明抬升是声学的：
       电/机耦合会把静音段一起抬起来（ratio≈1），真放音必须让静音段显著低于发声段。"""
    e = np.abs(np.asarray(mic, dtype=np.float64)).reshape(-1, hop).mean(axis=1)
    sp, gl = [], []
    step = clip_sec + loop_gap
    for i in range(int(reps)):
        a = int((lag_sec + i * step) * 16000 / hop)
        b = int((lag_sec + i * step + clip_sec) * 16000 / hop)
        g0, g1 = b, int((lag_sec + (i + 1) * step) * 16000 / hop)
        if 0 <= a < b <= e.size:
            sp.append(e[a:b].mean() if b > a else 0.0)
        if 0 <= g0 < g1 <= e.size and g1 > g0:
            gl.append(e[g0:g1].mean())
    if not sp:
        return None
    s_mean = float(np.mean(sp))
    g_mean = float(np.mean(gl)) if gl else 0.0
    return {"speech_env": round(s_mean, 1), "gap_env": round(g_mean, 1),
            "lift": round(s_mean / g_mean, 2) if g_mean > 0 else None,
            "n_speech": len(sp), "n_gap": len(gl)}


def quiet_floor(mic, lag_sec, clip_sec, reps, loop_gap, hop=160):
    """放音段以外那部分包络的 P90 = 这段采集期间的**房间底噪**估计。
       剂量门只能说"激励没被找回来"，说不清是喇叭没响还是房间在响，这一列补的就是后者：
       10-19 中午同一段 wake_01 连采两次，c00 尾段底噪 P90≈1000 / corr 0.93，
       c01 全程底噪≈2000（房间声压抬了一倍，且重录第 1 次仍如此，持续 >70s）/ corr 0.19。"""
    e = np.abs(np.asarray(mic, dtype=np.float64)).reshape(-1, hop).mean(axis=1)
    step = clip_sec + loop_gap
    keep = np.ones(e.size, dtype=bool)
    for i in range(int(reps)):
        a = int((lag_sec + i * step) * 16000 / hop)
        b = int((lag_sec + i * step + clip_sec) * 16000 / hop)
        keep[max(0, a):min(e.size, b)] = False
    rest = e[keep]
    return round(float(np.percentile(rest, 90.0)), 1) if rest.size else None


def frame_holes(vf):
    """把逐帧有效位压缩成 (起始帧, 连续缺失帧) 列表，供报表与 dataset_v11 跳过用。"""
    out, i, n = [], 0, len(vf)
    while i < n:
        if vf[i]:
            i += 1
            continue
        j = i
        while j < n and not vf[j]:
            j += 1
        out.append((i, j - i))
        i = j
    return out


class MicRecorder:
    """`kws capture <秒>`：板上连续录进 RAM（期间不推理），录满后逐帧 hex 回吐。
    帧号由板子自己编 ⇒ 缺号就是串口丢了一帧（160 样本 = 10ms）。丢的帧**标成洞记账**，
    绝不拿补零当真音频用：训练时压到洞的窗整块丢掉（上一版对账就栽在"补零没人知道"）。"""

    def __init__(self, mon, ser, log=None):
        self.mon, self.ser, self.log = mon, ser, (log or (lambda *x: None))

    def capture(self, sec, attempts=2):
        last = None
        for k in range(attempts):
            try:
                want, pc = self.arm(sec)
                return self.collect(want, pc, sec)
            except RuntimeError as e:
                last = e
                self.log("  采集作废重试（第 %d/%d 次）：%s" % (k + 2, attempts, e))
                time.sleep(1.0)
        raise RuntimeError("采集失败：%s" % last)

    def arm(self, sec):
        """发 `kws capture` 并确认板子已进入录音态（KWS_CAPTURE 回执）。
        它**不阻塞到录满**：调用方要在这之后立刻放音，词才落在缓冲里（放音晚于起录就漏头）。"""
        t = time.perf_counter()
        self.ser.write(("kws capture %d\r\n" % sec).encode())
        line, pc, m = self.mon.wait_for(CAP_RE, t, 10.0)
        if not m:
            raise RuntimeError("kws capture 没起来（无 KWS_CAPTURE 回执）")
        return int(m.group(2)), pc

    def collect(self, want, pc, sec):
        """等录满 → 逐帧收 hex → 按帧号重建（缺号记成洞）。"""
        mon = self.mon
        # 板上现在会在麦克风停流时打 KWS_CAPTURE_STALL/KWS_DUMP_STALL/KWS_MIC_STALL 并
        # 干净收尾（以前是无限等数据，串口再也等不到回吐，只能断电）。这里见到标记就立刻
        # 失败，别把 30 秒超时当"板子卡了"误判成采集链路问题。
        m = None
        t0 = time.perf_counter()
        while time.perf_counter() - t0 < sec + 30.0:
            _, _, m = mon.wait_for(DUMP_BEGIN_RE, pc, 1.0)
            if m:
                break
            sl, _, sm = mon.wait_for(STALL_RE, pc, 0.0)
            if sm:
                raise RuntimeError("板上麦克风停流：%s（这条采集作废，设备冷启动后重试）"
                                   % sl.strip())
        if not m:
            raise RuntimeError("录满 %d 帧没等到 KWS_DUMP_BEGIN" % want)
        fs, frames, ck, peak = (int(g) for g in m.groups())
        if fs != 160 or frames != want:
            raise RuntimeError("回吐头对不上（fs=%d frames=%d want=%d）" % (fs, frames, want))
        recs, i, done = [], len(mon.lines) - 1, False
        deadline = time.perf_counter() + frames * 0.09 + 30.0
        while True:
            while i < len(mon.lines):
                line = mon.lines[i][1]
                i += 1
                if DUMP_END_RE.search(line):
                    done = True
                    break
                pr = parse_frame_rec(line)
                if pr:
                    recs.append(pr)
            if done or time.perf_counter() > deadline:
                break
            time.sleep(0.05)
        got, dropped = stitch_frames(recs)
        if not got:
            raise RuntimeError("回吐没收到任何帧行")
        if not done:
            raise RuntimeError("回吐超时（收到 %d/%d 帧，串口或板子卡了）" % (len(got), frames))
        base = min(got)
        vf = np.zeros(frames, dtype=bool)
        x = np.zeros(frames * fs, dtype=np.int16)
        for k, arr in got.items():
            j = (k - base) % 65536
            if 0 <= j < frames:
                vf[j] = True
                x[j * fs:(j + 1) * fs] = arr
        holes = frame_holes(vf)
        # vf 是"帧级"掩码（长度 = frames），x 是样本级：直接 x[vf] 会拿 600 个布尔去索引
        # 96000 个样本，当场 IndexError（smoke 实测踩到）。先按帧 reshape 再取有效帧。
        x32 = x.astype(np.int32)
        pk = int(np.abs(x32.reshape(frames, fs)[vf]).max()) if vf.any() else 0
        # 板上 cap_cksum 是"整块缓冲的 int16 求和（模 2^32）"，PC 用同一套整数运算复算：
        # 相等 ⇒ 这一段**一字节没丢**（洞=0 且拼接没错），是采集链路无损的自证，不是估计。
        pc_ck = int(np.uint32(np.sum(x32, dtype=np.int64) & 0xFFFFFFFF))
        lost = sum(h[1] for h in holes)
        sat = int((np.abs(x32) >= 32767).sum())
        st = {"frames": frames, "got": int(vf.sum()), "hole_runs": len(holes),
              "hole_frames": lost, "hole_ms": lost * 10, "board_cksum": ck,
              "pc_cksum": pc_ck, "ck_ok": pc_ck == (ck & 0xFFFFFFFF),
              "dropped_heads": dropped,
              "board_peak": peak, "pc_peak": pk, "peak_ok": pk == peak,
              "saturation": sat / float(max(1, x.size)), "vf": vf}
        return x, holes, st


def phase_traindata(a, flog):
    """造"机内声学"训练语料：设备自己的喇叭出声 → 板载麦录音 → 无损回吐落盘 + manifest。

    为什么标签要从这里改口径（v9/v10 的泄漏实证见 tools/kws_silence_audit.py）：老配方把
    **整份文件**的每个滑窗都打上该文件的标签，于是 1.7s TTS 正样本的开头静音、结尾零补齐
    都成了"小麦小麦"正例——审计实测这类低能量正样本窗（RMS<300）模型给到 0.88~0.996 分。
    这里不再用"文件级标签"，而是用互相关把每一次放音在缓冲里的位置钉出来（lag/reps/gap 进
    manifest），dataset_v11 只把与词重叠的窗记正例，其余（含段间静音、房间底噪）一律负例。
    """
    def say(s):
        print(s)
        flog.write(s + "\n")
        flog.flush()

    ser = open_shell(a.port, a.baud)
    want = [n for n in a.scenarios.split(",") if n] if a.scenarios else None
    scenarios = [s for s in pick_scenarios(a) if (not want or s["name"] in want)]
    if not scenarios:
        say("!! data/ 下没有可用场景 WAV")
        ser.close()
        return {}
    texts = load_texts()

    mon = Monitor(ser, flog)
    mon.start()
    pc_src = (a.source == "pc")
    src = PcSource(log=say, device=a.pc_dev_index) if pc_src else BoardSource(mon, ser, log=say)
    rec = MicRecorder(mon, ser, log=say)
    vol_re = re.compile(r"sound0 volume=(\d+)")
    vol0 = None
    if pc_src:
        say("音源 = PC 声卡 + 外接音箱（板载喇叭音量不参与）")
        say("  ⚠ 系统音量必须固定在某一档且全程不动，否则同一批语料会混进不同声压")
        if not a.distance:
            say("  ⚠ 未给 --distance：距离不写进 manifest，事后无法按距离分层复现")
    else:
        t = time.perf_counter()
        ser.write(b"feather_audio_volume\r\n")
        _l, _p, _m = mon.wait_for(vol_re, t, 5.0)
        vol0 = int(_m.group(1)) if _m else None
        t = time.perf_counter()
        ser.write(("feather_audio_volume %d\r\n" % a.volume).encode())
        mon.wait_for(vol_re, t, 5.0)
        if vol0 is not None:
            say("喇叭音量: %s → %d（跑完还原到 %s）" % (vol0, a.volume, vol0))

    out_root = os.path.join(DATA, a.out_dir)
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    if a.source == "pc":
        stamp += "_pc"
        if a.distance:
            stamp += "_d%g" % a.distance
    out_dir = os.path.join(out_root, stamp)
    for d in ("pos", "neg", "fail"):
        os.makedirs(os.path.join(out_dir, d), exist_ok=True)
    man_path = os.path.join(out_dir, "manifest.csv")
    cols = ["file", "label", "scenario", "clip", "text", "sec", "reps", "lag_sec",
            "clip_sec", "loop_gap", "push_gain", "corr", "lift", "floor_p90",
            "hole_runs", "hole_ms",
            "hole_spans", "peak_ok", "ck_ok", "saturation", "drop_heads",
            # 剂量门结论必须落进 manifest：DictWriter 用 extrasaction="ignore"，
            # 不在 cols 里就静默丢——10-20 夜合并脚本因此退回 corr/lift 判据，
            # 把 12 段空场负样本（corr=0 无激励）全判出局。
            "dose_ok",
            # 10-24 新增：音源类型与距离必须进 manifest。PC 音箱与板载喇叭是两条不同的
            # 声路，混在一起训练就等于把两个域塞进一份语料；距离同理（0.3m 与 2m 的
            # 直达/混响比例差一个量级）。
            "source", "distance"]
    rows = []

    per = a.cap_sec * 9.0 + a.gap + 6.0      # 录 + 回吐(实测约 8×实时) + 间隔 + 灌数摊销
    scenarios = scenarios + [{"name": "room", "kind": "neg", "files": []}]
    est = sum((a.ambient_captures if not s["files"] else a.captures) for s in scenarios) * per
    say("计划：%s   每次采集 %ds（录 %ds + 回吐约 %ds）预计 %.0f 分钟"
        % ("  ".join("%s×%d(%s)" % (s["name"], a.ambient_captures if not s["files"]
                                    else a.captures, s["kind"]) for s in scenarios),
           a.cap_sec, a.cap_sec, int(a.cap_sec * 8), est / 60.0))

    rng = np.random.default_rng(a.seed)
    cap = int(a.max_sec * 16000)
    keep = int((a.cap_sec - 2.0 - a.play_delay) * 16000)   # 留 2s + 放音延迟：命令往返 + 尾巴余量
    loaded = None
    cur = None
    for sc in scenarios:
        n = a.ambient_captures if not sc["files"] else a.captures
        if not sc["files"]:
            # 空场/房间场景必须**什么都不放**：cur 是上一个场景缓存的素材，不清零就会
            # 在"负样本"里放进真词，还会拿它算剂量相关性——负样本被自己污染，等于把 v10
            # 的泄漏换个地方再犯一遍。
            cur = None
            loaded = None
        for c in range(n):
            clip = None
            if sc["files"]:
                path = sc["files"][int(rng.integers(0, len(sc["files"])))]
                clip = os.path.basename(path)
                if loaded != clip:
                    pcm0 = load_pcm(path)
                    if sc.get("level", True):
                        pcm0, gain = fit_level(pcm0, sc.get("stim_target", a.stim_target), a.stim_peak_cap)
                    else:
                        gain = 1.0
                    gap = np.zeros(int(a.loop_gap * 16000), dtype=pcm0.dtype)
                    tiled, reps, tot = [], 0, 0
                    while tot + pcm0.size + gap.size <= min(cap, keep):
                        tiled += [pcm0, gap]
                        tot += pcm0.size + gap.size
                        reps += 1
                    if reps == 0:
                        say("  !! %s 太长（%.2fs），该场景改用截断素材" % (clip, pcm0.size / 16000.0))
                        pcm = pcm0[:min(pcm0.size, keep)]
                        cur = (pcm, 1, pcm.size / 16000.0, gain)
                        loaded = clip
                        try:
                            src.push(clip, pcm)
                        except RuntimeError as e:
                            say("  !! %s 灌入失败，跳过该场景：%s" % (clip, e))
                            loaded = None
                            break
                    else:
                        pcm = np.concatenate(tiled)
                        try:
                            src.push(clip, pcm)
                        except RuntimeError as e:
                            say("  !! %s 灌入失败，跳过该场景：%s" % (clip, e))
                            loaded = None
                            break
                        loaded = clip
                        cur = (pcm, reps, pcm0.size / 16000.0, gain)
            stim, reps, clip_sec, gain = (cur[0], cur[1], cur[2], cur[3]) if cur \
                else (None, 0, 0.0, 1.0)
            text = texts.get(os.path.normpath(path), "") if clip else ""
            kind = sc["kind"]
            x = None
            for att in range(a.dose_retries + 1):
                try:
                    want, pc_arm = rec.arm(a.cap_sec)
                    if stim is not None:
                        # 起录之后**先等 play_delay 再放音**：实测板载 mic 管线真正出流比
                        # `kws capture` 回显晚 0.05~0.25s，立刻放音时词头被"放音早于起录"吃掉
                        # ~180ms → 互相关从 0.93 崩到 0.19（lift 仍 2.7，看着像"响了但认不出"），
                        # 而 lag 是正样本窗标签的位置依据，词头掉在缓冲外时标签整段错位。
                        time.sleep(a.play_delay)
                        src.play(1)
                    x, holes, st = rec.collect(want, pc_arm, a.cap_sec)
                except RuntimeError as e:
                    say("  !! 采集失败：%s" % e)
                    loaded = None
                    x = None
                    time.sleep(2.0)
                    break
                corr, lag = (0.0, 0.0) if stim is None else env_dose(
                    stim, x, max_lag_sec=a.max_lag_sec)
                sp = None if stim is None or lag is None else rep_split(
                    x, lag, clip_sec, reps, a.loop_gap)
                lift = sp["lift"] if sp else None
                floor = quiet_floor(x, lag or 0.0, clip_sec, reps, a.loop_gap)
                if sc.get("dose") == "peak":
                    # 稀疏瞬态：互相关被静音稀释，改用"确实有过一次突发"的峰值判据
                    dose_ok = dose_transient(x)
                else:
                    dose_ok = (stim is None) or (corr >= a.corr_min
                                                 and (lift or 1.0) >= a.lift_min)
                if dose_ok or att == a.dose_retries:
                    break
                # 不达标就**同一段素材重录**，不重灌：一轮采集 ~2min，直接丢进 fail/ 等于
                # 把已付的灌数成本一起扔了（smoke 实测 4 段有 1 段抬升 1.55 卡在 1.6 门限下）。
                # 退避随次数加长：实测房间吵起来能持续 >70s，等 2s 再录必然还是同一场噪声。
                say("  !! 剂量不达标（corr=%.2f 抬升=%s 底噪=%s），重录第 %d 次"
                    % (corr, ("%.2f" % lift) if lift else "-",
                       ("%.0f" % floor) if floor else "-", att + 1))
                time.sleep(max(0.5, a.gap) + 15.0 * (att + 1))
            if x is None:
                continue
            sub = kind if dose_ok else "fail"
            fn = "%s_%s_c%02d.wav" % (sc["name"], (os.path.splitext(clip)[0] if clip
                                                   else "room"), c)
            fp = os.path.join(out_dir, sub, fn)
            sec = wav_write_i16(fp, x)
            rows.append({"file": os.path.relpath(fp, out_dir).replace("\\", "/"),
                         "label": 1 if kind == "pos" else 0, "scenario": sc["name"],
                         "clip": clip or "", "text": text,
                         "sec": round(sec, 2), "reps": reps if dose_ok else 0,
                         "lag_sec": lag if lag is not None else "",
                         "clip_sec": round(clip_sec, 3), "loop_gap": a.loop_gap,
                         "push_gain": gain,
                         "corr": corr, "lift": lift, "floor_p90": floor, "hole_runs": st["hole_runs"],
                         "hole_ms": st["hole_ms"], "peak_ok": st["peak_ok"],
                         "ck_ok": st["ck_ok"], "saturation": round(st["saturation"], 6),
                         "drop_heads": len(st["dropped_heads"]),
                         "hole_spans": ",".join("%d-%d" % (h[0], h[0] + h[1] - 1)
                                                 for h in holes),
                         "dose_ok": dose_ok,
                         "source": a.source,
                         "distance": (a.distance if a.distance else "")})
            say("  %-11s c%-2d %-16s %.1fs  剂量 corr=%.2f 抬升=%s 底噪=%s 增益%.2f× 洞 %d 段/%dms  "
                "削顶 %.2f%%  校验 %s  存 %s%s"
                % (sc["name"], c, clip or "（空场）", sec, corr,
                   ("%.1f×" % lift) if lift else "-",
                   ("%.0f" % floor) if floor else "-", gain, st["hole_runs"], st["hole_ms"],
                   st["saturation"] * 100.0,
                   "无损" if st["ck_ok"] else "有洞",
                   sub, "" if dose_ok else "  ← 剂量不达标，不进训练集"))
            time.sleep(max(0.5, a.gap))

    if vol0 is not None and a.volume != vol0:
        t = time.perf_counter()
        ser.write(("feather_audio_volume %d\r\n" % vol0).encode())
        mon.wait_for(vol_re, t, 5.0)
        say("喇叭音量已还原: %s" % vol0)
    if pc_src:
        src.shutdown()
    ser.write(b"kws stop\r\n")
    time.sleep(0.5)
    mon.stop_now()
    ser.close()

    import csv
    with open(man_path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=cols, extrasaction="ignore")
        w.writeheader()
        for r in rows:
            w.writerow(r)
    ok = [r for r in rows if r["dose_ok"]]
    pos = [r for r in ok if r["label"] == 1]
    neg = [r for r in ok if r["label"] == 0]
    say("  ---- 语料 %s：可用 %d 段（正 %d / 负 %d，正样本内放音共 %d 次）"
        "  弃 %d 段  洞 %d 段/%d ms  逐字节无损 %d/%d 段  削顶样本 %.3f%%"
        % (out_dir, len(ok), len(pos), len(neg), sum(r["reps"] for r in pos),
           len(rows) - len(ok), sum(r["hole_runs"] for r in ok),
           sum(r["hole_ms"] for r in ok),
           sum(1 for r in rows if r["ck_ok"]), len(rows),
           100.0 * sum(r["saturation"] * r["sec"] for r in ok)
           / max(0.01, sum(r["sec"] for r in ok))))
    return {"out_dir": out_dir, "manifest": man_path, "rows": rows,
            "pos_sec": round(sum(r["sec"] for r in pos), 1),
            "neg_sec": round(sum(r["sec"] for r in neg), 1),
            "pos_utterances": int(sum(r["reps"] for r in pos)),
            "volume": a.volume, "cap_sec": a.cap_sec,
            "scenarios": [s["name"] for s in scenarios]}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--phase", choices=["proof", "matrix", "traindata"], default="proof")
    ap.add_argument("--port", default="COM9")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--rounds", type=int, default=3, help="矩阵阶段每场景播放次数")
    ap.add_argument("--gap", type=float, default=8.0, help="两次播放之间的静默间隔（秒）")
    ap.add_argument("--quiet", type=float, default=20.0, help="场景之间的整段静默（秒）")
    ap.add_argument("--slack", type=float, default=3.5,
                    help="命中晚于 clip 结束多久仍归因到该场景（滚动窗 + 2/3 窗确认）")
    ap.add_argument("--sec", type=int, default=10, help="proof 阶段每条 kws mic 观测秒数")
    ap.add_argument("--gain", type=float, default=0.9, help="激励波形目标峰值（不动系统音量）")
    ap.add_argument("--source", choices=["board", "pc"], default="board",
                    help="board=设备自己的喇叭（机内声路，唯一真实的那条）；pc=外部对照臂")
    ap.add_argument("--volume", type=int, default=100, help="board 源的 sound0 音量 0~100（跑完还原）")
    ap.add_argument("--max-sec", type=float, default=7.5,
                    help="board 源单段素材上限秒数（须小于固件 KWS_PLAY_MAX_SEC）")
    ap.add_argument("--clip-sec", type=float, default=2.0, help="proof 激励片段时长（秒）")
    ap.add_argument("--min-sec", type=float, default=4.5,
                    help="board 矩阵单段素材不足这个秒数时连放补齐：1.7s 的短词最多只占 2~3 个"
                         "滑窗，2of3 确认天然过不去，测出来的 0%% 召回是仪器假象不是模型缺陷")
    ap.add_argument("--loop-gap", type=float, default=0.8, help="连放两次之间插的真静音秒数")
    ap.add_argument("--seed", type=int, default=20261019)
    ap.add_argument("--reply-dir", default="", help="应答语 WAV 目录（kws_tts_corpus.py 产物）")
    ap.add_argument("--scenarios", default="",
                    help="traindata 只跑这些场景（逗号分隔，缺省全跑）")
    ap.add_argument("--cap-sec", type=int, default=8,
                    help="traindata 单次采集秒数（固件上限 15s=480KB 堆；回吐按 ~8×实时计）")
    ap.add_argument("--captures", type=int, default=3, help="traindata 每场景采集次数")
    ap.add_argument("--ambient-captures", type=int, default=12,
                    help="traindata 空场（不出声）采集次数：负样本的主力，房间瞬态才是风暴现场")
    ap.add_argument("--stim-target", type=float, default=9000.0,
                    help="激励包络 P95 目标档（TTS 词实测 9235，真人板采样本只有 2717）；"
                         "语料源电平差 3 倍以上，不拉平就会被房间底噪稀释掉剂量读数")
    ap.add_argument("--stim-peak-cap", type=float, default=32000.0,
                    help="放大后 |x| 的 99.95 分位上限（不用峰值：真人样本有孤立削顶样点）")
    ap.add_argument("--corr-min", type=float, default=0.30,
                    help="剂量自证下限：激励包络与采集包络的互相关，低于它这段不能当「被麦听到」")
    ap.add_argument("--lift-min", type=float, default=1.25,
                    help="发声段/段间静音 包络比下限：≈1 说明是电/机耦合不是声学。"
                         "定标实测：数字静音对照 0.48×、真实声路 1.19~2.6×（越短的 TTS 词抬升越低，"
                         "因为段间只剩房间底噪）；声学路本身已由 proof 的 1kHz 正弦 8.0× / 静音 0.48× "
                         "A/B 证明，这里只留 2.6× 余量挡住耦合，剂量有无主要看 corr")
    ap.add_argument("--play-delay", type=float, default=0.6,
                    help="traindata 起录到放音之间的等待秒数。板载 mic 管线出流比 `kws capture` 回显晚 "
                         "0.05~0.25s，立刻放音会把词头留在缓冲外（实测差 180ms 时 corr 0.93→0.19、"
                         "lag 从 0.14s 假移到 0.02s），而 lag 就是正样本窗标签的位置")
    ap.add_argument("--dose-retries", type=int, default=2,
                    help="traindata 剂量不达标时用同一段素材重录几次（不重灌，采集约 2min/次）")
    ap.add_argument("--max-lag-sec", type=float, default=2.0,
                    help="traindata 剂量互相关允许的最大回路延迟（喇叭→空气→麦→板内缓冲）。"
                         "实测本回路延迟 0.14s（同一段在 max_lag 3/6/9s 下都收敛到同一个峰），"
                         "窗口放宽只会让搜索摊薄、把无关段的相关抬上来，故收紧到 2s")
    ap.add_argument("--out-dir", default=os.path.join("boardmic"),
                    help="traindata 语料根目录（相对 toolchain/data）")
    ap.add_argument("--distance", type=float, default=0.0,
                    help="音源到板载麦的距离（米）。只记录进 manifest/目录名，不改变行为；"
                         "PC 源建议必填，否则事后无法按距离分层复现")
    ap.add_argument("--note", default="")
    ap.add_argument("--pc-device", default="",
                    help="PC 源的输出设备：索引或名字片段（如 0 / 花再 / Evo）。"
                         "留空 = Windows 默认设备。PC+外接音箱通路必须显式指定，"
                         "否则声音可能从笔记本内置喇叭/HDMI 出去，采到的语料位置全错")
    a = ap.parse_args()

    # PC 源：先解析输出设备并打印——日志里必须留下"声音到底从哪个设备出去"的证据
    a.pc_dev_index, a.pc_dev_name = None, "系统默认"
    if a.source == "pc":
        a.pc_dev_index, a.pc_dev_name = resolve_out_device(a.pc_device)

    os.makedirs(LOG_DIR, exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    log_path = os.path.join(LOG_DIR, "kws_rig_%s_%s.log" % (a.phase, stamp))
    sum_path = log_path[:-4] + ".json"
    with open(log_path, "w", encoding="utf-8") as flog:
        flog.write("===== 声学回踩 %s 阶段 %s =====\n端口 %s@%d  环境: %s\n" %
                   (a.phase, datetime.datetime.now(), a.port, a.baud, a.note or "（未填）"))
        if a.source == "pc":
            flog.write("PC 输出设备: [%s] %s\n" %
                       ("默认" if a.pc_dev_index is None else a.pc_dev_index, a.pc_dev_name))
        t_open = time.perf_counter()
        if a.phase == "proof":
            out = phase_proof(a, flog)
        elif a.phase == "traindata":
            out = phase_traindata(a, flog)
        else:
            out = phase_matrix(a, flog)
    out.update({"phase": a.phase, "note": a.note, "port": "%s@%d" % (a.port, a.baud),
                "elapsed_sec": round(time.perf_counter() - t_open, 1), "log_path": log_path})
    with open(sum_path, "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=1)
    print("留档 %s\n汇总 %s" % (log_path, sum_path))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
