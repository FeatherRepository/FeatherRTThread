"""
train_kws.py — 训练「小麦小麦」唤醒词 DS-CNN 并导出 int8 TFLite

网络：DS-CNN（深度可分离卷积），输入 (50,13,1)，输出 2 类。
量化：全 int8（输入/输出 int8），以匹配固件 EI 运行时的推理路径。

产出：training/out/kws_xiaomai_<ver>.tflite（+ 量化参数 json，供 export_model.py 用）
      版本号由环境变量 KWS_VERSION 决定，默认 v10；只有显式 KWS_VERSION=v9 才写板上产物
      kws_xiaomai.tflite —— 10-18 曾差点用 v10 覆盖掉固件引用的 v9。
"""
import os, json
import numpy as np

import tensorflow as tf
from tensorflow.keras import layers, models, callbacks

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
os.makedirs(OUT, exist_ok=True)

VERSION = os.environ.get("KWS_VERSION", "v10")
# 数据集口径随版本走：v11 的标签是**逐窗**的（词区才算正例，词间静音归负例），
# v10 及以前是整份文件一个标签——审计证明那会把 TTS 的静音垫也教成唤醒词。
if VERSION in ("v11", "v12", "v13", "v14", "v15", "v17", "v18", "v19", "v20", "v21", "v22", "v23", "v24", "v25", "v26"):
    from dataset_v11 import build
else:
    from dataset_v10 import build
MODEL_NAME = ("kws_xiaomai.tflite" if VERSION == "v9"
              else f"kws_xiaomai_{VERSION}.tflite")
META_NAME = MODEL_NAME[: -len(".tflite")] + "_meta.json"

FRAMES, BINS, NCLASS = 50, 13, 2


def make_model():
    # 输入张量必须是 [1,650]（与 EI 运行时喂入的特征形状一致；4D 会导致推理报错）
    inp = layers.Input(shape=(FRAMES * BINS,), name="mfcc")
    x = layers.Reshape((FRAMES, BINS, 1))(inp)
    # 路线 A：MixConv（多核深度卷积并行拼接，microWakeWord 风格）。KWS_ARCH=mixconv 启用。
    if os.environ.get("KWS_ARCH", "dscnn").lower() == "mixconv":
        x = layers.Conv2D(16, (3, 3), strides=2, padding="same", use_bias=False)(x)
        x = layers.BatchNormalization()(x); x = layers.ReLU()(x)
        for filters, kernels in ((32, ((3, 3), (5, 5))), (64, ((3, 3),))):
            outs = []
            for k in kernels:
                y = layers.DepthwiseConv2D(k, padding="same", use_bias=False)(x)
                y = layers.BatchNormalization()(y); y = layers.ReLU()(y)
                outs.append(y)
            x = layers.Concatenate()(outs) if len(outs) > 1 else outs[0]
            x = layers.Conv2D(filters, (1, 1), use_bias=False)(x)
            x = layers.BatchNormalization()(x); x = layers.ReLU()(x)
            x = layers.MaxPool2D((2, 2))(x)
        x = layers.GlobalAveragePooling2D()(x)
        x = layers.Dropout(0.3)(x)
        return models.Model(inp, layers.Dense(NCLASS, activation="softmax", name="scores")(x))
    # 首层 stride=2：把最大激活张量压到 25x7x16=2800，使 TFLM arena 能放进 M55 内部 RAM
    x = layers.Conv2D(16, (3, 3), strides=2, padding="same", use_bias=False)(x)
    x = layers.BatchNormalization()(x); x = layers.ReLU()(x)
    x = layers.SeparableConv2D(32, (3, 3), padding="same", use_bias=False)(x)
    x = layers.BatchNormalization()(x); x = layers.ReLU()(x)
    x = layers.MaxPool2D((2, 2))(x)
    x = layers.SeparableConv2D(64, (3, 3), padding="same", use_bias=False)(x)
    x = layers.BatchNormalization()(x); x = layers.ReLU()(x)
    x = layers.MaxPool2D((2, 2))(x)
    # 池化方式可选（KWS_POOL）：
    #   gap（默认，v9/v10/v11 现役）= GlobalAveragePooling —— 对时间结构不敏感，
    #      "持续稳态噪声"在时间上平坦，正好钻这个空子（10-21 凌晨 ambient_05 拿 0.88~0.996）
    #   flat = 保留时间轴 flatten（6x1x64=384 → Dense），让模型能学"能量随时间怎么变"
    #   gapmax = GAP 与 GlobalMax 拼接（128 维），显式给"峰值 vs 平均"两个统计量
    pool = os.environ.get("KWS_POOL", "gap").lower()
    if pool == "flat":
        x = layers.Flatten()(x)
    elif pool == "gapmax":
        x = layers.Concatenate()([layers.GlobalAveragePooling2D()(x),
                                  layers.GlobalMaxPooling2D()(x)])
    else:
        x = layers.GlobalAveragePooling2D()(x)
    x = layers.Dropout(0.3)(x)
    out = layers.Dense(NCLASS, activation="softmax", name="scores")(x)
    return models.Model(inp, out)


def main():
    X, y, grp = build(n_aug=2, seed=42)             # (N,650) 直接作为输入

    # 验证集按**源文件**整份留出：密相位窗互为近重复，按窗随机切会让 EarlyStopping
    # 挑到"背过的文件"，选出来的权重在没听过的音频上没有代表性。
    rng = np.random.default_rng(0)
    files = np.unique(grp); rng.shuffle(files)
    va_files = set(files[:max(2, int(len(files) * 0.2))].tolist())
    m = np.isin(grp, list(va_files))
    Xtr, ytr = X[~m], y[~m]
    Xva, yva = X[m], y[m]
    print(f"训练 {len(Xtr)} / 验证 {len(Xva)}（验证留出 {len(va_files)}/{len(files)} 个源文件）")

    model = make_model()
    model.compile(optimizer=tf.keras.optimizers.Adam(1e-3),
                  loss="sparse_categorical_crossentropy", metrics=["accuracy"])
    model.summary()

    cw = {0: len(ytr) / (2 * max(1, (ytr == 0).sum())),
          1: len(ytr) / (2 * max(1, (ytr == 1).sum()))}
    # 负类权重倍率（KWS_NEG_MULT，默认 1）：>1 让模型更保守、压误触（v19 用）。
    _nm = float(os.environ.get("KWS_NEG_MULT", "1") or 1)
    if _nm != 1.0:
        cw[0] *= _nm
        print("负类权重 ×%.2f → cw=%s" % (_nm, cw))
    model.fit(Xtr, ytr, validation_data=(Xva, yva), epochs=int(os.environ.get("KWS_EPOCHS", "60")), batch_size=32,
              class_weight=cw, verbose=2,
              callbacks=[callbacks.EarlyStopping(patience=12, restore_best_weights=True)])

    # ---- 诊断：float 模型对真实样本打分（区分"训练塌缩" vs "int8 转换塌缩"）----
    try:
        import glob as _glob
        from ei_mfcc import ei_mfcc as _mfcc
        from dataset_v9 import read_wav16k as _read
        _real = os.path.join(os.path.dirname(OUT), "..", "data", "real")
        _fs = (sorted(_glob.glob(os.path.join(_real, "wake_*.wav")))[:2]
               + sorted(_glob.glob(os.path.join(_real, "ambient_*.wav")))[:2])
        print("[float-diag] 真实样本 float 打分（xiaomai 概率）：")
        for _p in _fs:
            _x = _read(_p); _best = 0.0
            for _s in range(0, max(1, len(_x) - 16000 + 1), 8000):
                _f = np.asarray(_mfcc(_x[_s:_s + 16000])).reshape(1, -1)
                _best = max(_best, float(model.predict(_f, verbose=0)[0][1]))
            print("   %-28s float=%.3f" % (os.path.basename(_p), _best))
        model.save(os.path.join(OUT, "kws_xiaomai_%s_float.keras" % VERSION))
    except Exception as _e:
        print("[float-diag] skip:", type(_e).__name__, _e)

    # 全 int8 量化
    def rep():
        for i in range(len(Xtr)):
            yield [Xtr[i:i + 1].astype(np.float32)]

    conv = tf.lite.TFLiteConverter.from_keras_model(model)
    conv.optimizations = [tf.lite.Optimize.DEFAULT]
    conv.representative_dataset = rep
    conv.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    conv.inference_input_type = tf.int8
    conv.inference_output_type = tf.int8
    tfl = conv.convert()

    path = os.path.join(OUT, MODEL_NAME)
    with open(path, "wb") as f:
        f.write(tfl)
    print("模型已保存:", path, len(tfl), "字节")

    # 记录量化参数（供 export_model.py 生成固件配置）
    interp = tf.lite.Interpreter(model_content=tfl); interp.allocate_tensors()
    ide = interp.get_input_details()[0]; ode = interp.get_output_details()[0]
    meta = {
        "input_shape": [int(x) for x in ide["shape"]], "input_scale": float(ide["quantization"][0]),
        "input_zero_point": int(ide["quantization"][1]),
        "output_shape": [int(x) for x in ode["shape"]], "output_scale": float(ode["quantization"][0]),
        "output_zero_point": int(ode["quantization"][1]),
        "labels": ["noice", "xiaomai"],
    }
    with open(os.path.join(OUT, META_NAME), "w") as f:
        json.dump(meta, f, indent=2, ensure_ascii=False)
    print("量化参数:", meta)

    # 量化后验证集准确率
    iscale, izp = meta["input_scale"], meta["input_zero_point"]
    oscale, ozp = meta["output_scale"], meta["output_zero_point"]
    correct = 0
    for i in range(len(Xva)):
        q = np.clip(np.round(Xva[i] / iscale) + izp, -128, 127).astype(np.int8).reshape(ide["shape"])
        interp.set_tensor(ide["index"], q)
        interp.invoke()
        out = interp.get_tensor(ode["index"]).astype(np.float32).reshape(-1)
        pred = int(np.argmax((out - ozp) * oscale))
        correct += int(pred == yva[i])
    print(f"量化后验证集准确率: {correct}/{len(Xva)} = {correct / len(Xva):.3f}")

    # ---- 诊断：int8 模型对同一批真实样本打分（与 float-diag 对照）----
    try:
        import glob as _glob
        from ei_mfcc import ei_mfcc as _mfcc
        from dataset_v9 import read_wav16k as _read
        _real = os.path.join(os.path.dirname(OUT), "..", "data", "real")
        _fs = (sorted(_glob.glob(os.path.join(_real, "wake_*.wav")))[:2]
               + sorted(_glob.glob(os.path.join(_real, "ambient_*.wav")))[:2])
        print("[int8-diag] 真实样本 int8 打分（xiaomai 概率）：")
        for _p in _fs:
            _x = _read(_p); _best = 0.0
            for _s in range(0, max(1, len(_x) - 16000 + 1), 8000):
                _f = np.asarray(_mfcc(_x[_s:_s + 16000]))
                _q = np.clip(np.round(_f / iscale) + izp, -128, 127).astype(np.int8).reshape(ide["shape"])
                interp.set_tensor(ide["index"], _q); interp.invoke()
                _o = interp.get_tensor(ode["index"]).astype(np.float32).reshape(-1)
                _best = max(_best, float(((_o - ozp) * oscale)[1]))
            print("   %-28s int8=%.3f" % (os.path.basename(_p), _best))
    except Exception as _e:
        print("[int8-diag] skip:", type(_e).__name__, _e)


if __name__ == "__main__":
    main()
