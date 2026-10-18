"""
ei_mfcc.py — Edge Impulse MFCC 特征提取的 Python 逐级复刻
============================================================

目的：在 PC 上算出与 Edgi-Talk 板上 EI SDK **逐位对齐**的 MFCC 特征，
用于本地训练「小麦小麦」唤醒词模型。这样训出的模型可直接替换固件里的
tflite 模型文件，无需改动板上任何 DSP 代码。

参数来源（板上现有模型 333519）：
  edge-impulse/model-parameters/model_variables.h
    ei_dsp_config_mfcc_t: num_cepstral=13, frame_length=0.02, frame_stride=0.02,
                          num_filters=32, fft_length=256, win_size=101,
                          low_frequency=0, high_frequency=0, pre_cof=0.98, pre_shift=1
    implementation_version = 4
  输出：50 帧 × 13 = 650 维（EI_CLASSIFIER_NN_INPUT_FRAME_SIZE=650）

逐级算法（对应 EI 源码）：
  1. 预加重        speechpy/processing.hpp :: preemphasis (cof=0.98, shift=1)
  2. 分帧          speechpy/processing.hpp :: stack_frames (version>=4)
  3. 功率谱        numpy.hpp :: rfft / power_spectrum（截断到 n_fft，**不加窗**）
  4. Mel 滤波      speechpy/feature.hpp :: mfe  + functions.hpp mel 公式
  5. 取 log        numpy.hpp :: log
  6. DCT-II 正交   numpy.hpp :: dct2(DCT_NORMALIZATION_ORTHO)
  7. dc_elimination=true：第 0 维替换为 log(帧能量)
  8. 取前 13 维
  9. CMVN          speechpy/processing.hpp :: cmvnw(win=101, variance_norm=true)
     （EI 特有，非标准 MFCC；漏掉它会明显掉点）

用法：
    from ei_mfcc import ei_mfcc
    feat = ei_mfcc(x_int16)      # x_int16: np.int16, 16000 样点 -> (50,13) float32
"""
import numpy as np

# ---- 板上模型 333519 的确切参数（勿随意改，改了必须同步固件）----
SR              = 16000
FRAME_LENGTH    = 0.02     # 秒
FRAME_STRIDE    = 0.02     # 秒
NUM_CEPSTRAL    = 13
NUM_FILTERS     = 32
FFT_LENGTH      = 256
WIN_SIZE        = 101      # CMVN 滑窗
LOW_FREQ        = 0
HIGH_FREQ       = 0        # 0 -> SR/2
PRE_COF         = 0.98
PRE_SHIFT       = 1
VERSION         = 4

N_FRAMES = 50              # 16000*0.02=320 样点/帧, 步进 320 -> 50 帧


def _hz2mel(f):
    return 1127.0 * np.log(1.0 + f / 700.0)


def _mel2hz(m):
    return 700.0 * (np.exp(m / 1127.0) - 1.0)


def _dct2_ortho(x):
    """DCT-II 正交归一化（等价 scipy.fftpack.dct(type=2, norm='ortho')，纯 numpy 实现）"""
    x = np.asarray(x, dtype=np.float32)
    n_rows, N = x.shape
    n = np.arange(N)
    k = np.arange(N)
    basis = np.cos(np.pi * (2 * n[None, :] + 1) * k[:, None] / (2.0 * N))  # (N,N)
    scale = np.full(N, np.sqrt(2.0 / N), dtype=np.float32)
    scale[0] = np.sqrt(1.0 / N)
    return (x @ basis.T.astype(np.float32)) * scale[None, :]


def _preemphasis(x, cof=PRE_COF, shift=PRE_SHIFT):
    """y[n] = x[n] - cof*x[n-shift]；n<shift 时回绕到信号末尾（对应 EI 的 end_of_signal_buffer）"""
    x = x.astype(np.float32)
    y = np.empty_like(x)
    n = x.shape[0]
    y[shift:] = x[shift:] - cof * x[:-shift]
    # 起始 shift 个样点：回绕到信号尾部
    y[:shift] = x[:shift] - cof * x[n - shift:n]
    return y


def _frame_offsets(n, sr, frame_length, frame_stride, version):
    """复刻 stack_frames (version>=4)：frame_len=ceil(sr*fl), stride=ceil(sr*fs), length=frame_len-stride"""
    def ceil_close(v):
        if v > np.floor(v) and (v - np.floor(v)) < 0.001:
            return float(np.floor(v))
        return float(np.ceil(v))
    frame_len = int(ceil_close(sr * frame_length))
    stride    = int(ceil_close(sr * frame_stride))
    length    = frame_len - stride
    numframes = int(np.floor((n - length) / stride))
    return frame_len, stride, numframes


def _power_spectrum(frame, fft_length):
    """复刻 numpy::power_spectrum：截断到 n_fft，不加窗，|rfft|^2 / n_fft"""
    src = frame
    if src.shape[0] > fft_length:
        src = src[:fft_length]
    spec = np.fft.rfft(src, n=fft_length)          # 未归一化 DFT，129 bins
    return (spec.real ** 2 + spec.imag ** 2) / float(fft_length)


def _mel_filterbank(sr, num_filters, fft_length, low, high):
    """复刻 feature::mfe 的 Mel 三角滤波（含 EI 对最后一个 bin 的 -0.001 修正）"""
    if high == 0:
        high = sr // 2
    max_bin = fft_length                            # version>=4
    mels = np.linspace(_hz2mel(float(low)), _hz2mel(float(high)), num_filters + 2)
    hertz = _mel2hz(mels)
    hertz = np.clip(hertz, low, high)
    hertz[-1] -= 0.001                              # EI 修正 speechpy 的桶误差
    bins = np.floor((max_bin + 1) * hertz / sr).astype(int)   # get_fft_bin_from_hertz: (fft_size+1)
    return bins


def _apply_filterbank(power, bins, num_filters):
    """复刻 mfe 里的三角加权求和（middle 权重=1，左右线性插值）"""
    out = np.zeros(num_filters, dtype=np.float32)
    for i in range(num_filters):
        left, middle, right = int(bins[i]), int(bins[i + 1]), int(bins[i + 2])
        val = power[middle]
        for b in range(left + 1, right):
            if b < middle:
                val += ((b - left) / (middle - left)) * power[b]
            elif b > middle:
                val += ((right - b) / (right - middle)) * power[b]
        out[i] = val
    return out


def _cmvnw(features, win_size, variance_normalization=True):
    """复刻 cmvnw：对称填充滑窗，减均值（并按方差归一化）"""
    pad = (win_size - 1) // 2
    rows, cols = features.shape

    def sym_pad(fm):
        return np.pad(fm, ((pad, pad), (0, 0)), mode="symmetric")

    # 1) 减均值
    padded = sym_pad(features)
    out = features.copy()
    for ix in range(rows):
        win = padded[ix:ix + win_size]
        out[ix] = features[ix] - win.mean(axis=0)

    # 2) 方差归一化
    if variance_normalization:
        padded2 = sym_pad(out)
        for ix in range(rows):
            win = padded2[ix:ix + win_size]
            var = win.var(axis=0)
            out[ix] = out[ix] / np.sqrt(var + 1e-10)
    return out


def ei_mfcc(x_int16, flatten=True):
    """
    输入：x_int16  长度>=16000 的 np.int16 音频（16kHz mono）
    输出：flatten=True -> (650,) float32；否则 (50,13) float32
    """
    x = np.asarray(x_int16, dtype=np.float32)
    if x.shape[0] < SR:
        x = np.pad(x, (0, SR - x.shape[0]))
    x = x[:SR]

    # 1. 预加重
    xp = _preemphasis(x)

    # 2. 分帧
    frame_len, stride, numframes = _frame_offsets(x.shape[0], SR, FRAME_LENGTH, FRAME_STRIDE, VERSION)

    bins = _mel_filterbank(SR, NUM_FILTERS, FFT_LENGTH, LOW_FREQ, HIGH_FREQ)

    feats = np.zeros((numframes, NUM_FILTERS), dtype=np.float32)
    energies = np.zeros((numframes,), dtype=np.float32)

    for fi in range(numframes):
        off = fi * stride
        frame = xp[off:off + frame_len]
        power = _power_spectrum(frame, FFT_LENGTH)
        energies[fi] = power.sum()
        feats[fi] = _apply_filterbank(power, bins, NUM_FILTERS)

    # 5. log（先做零值保护，对应 EI zero_handling）
    feats = np.log(np.maximum(feats, 1e-10))

    # 6. DCT-II 正交（取前 num_cepstral 维）
    mfcc = _dct2_ortho(feats)[:, :NUM_CEPSTRAL]

    # 7. dc_elimination：第 0 维 = log(帧能量)
    mfcc[:, 0] = np.log(np.maximum(energies, 1e-10))

    # 9. CMVN
    mfcc = _cmvnw(mfcc, WIN_SIZE, variance_normalization=True)

    return mfcc.reshape(-1).astype(np.float32) if flatten else mfcc.astype(np.float32)


if __name__ == "__main__":
    import sys, wave
    path = sys.argv[1] if len(sys.argv) > 1 else None
    if not path:
        print("用法: python ei_mfcc.py <wav16k-mono>")
        sys.exit(1)
    w = wave.open(path)
    data = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)
    f = ei_mfcc(data)
    print("shape:", f.shape, "dtype:", f.dtype)
    print("前 5 维:", np.round(f[:5], 3))
    print("min/max/mean:", float(f.min()), float(f.max()), float(f.mean()))
