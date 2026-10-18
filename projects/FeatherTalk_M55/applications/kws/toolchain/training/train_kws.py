"""
train_kws.py — 训练「小麦小麦」唤醒词 DS-CNN 并导出 int8 TFLite

网络：DS-CNN（深度可分离卷积），输入 (50,13,1)，输出 2 类。
量化：全 int8（输入/输出 int8），以匹配固件 EI 运行时的推理路径。

产出：training/out/kws_xiaomai.tflite  (+ 量化参数 json，供 export_model.py 用)
"""
import os, json
import numpy as np

import tensorflow as tf
from tensorflow.keras import layers, models, callbacks

from dataset_v9 import build   # v9：滑窗+真实唤醒词正样本

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
os.makedirs(OUT, exist_ok=True)

FRAMES, BINS, NCLASS = 50, 13, 2


def make_model():
    # 输入张量必须是 [1,650]（与 EI 运行时喂入的特征形状一致；4D 会导致推理报错）
    inp = layers.Input(shape=(FRAMES * BINS,), name="mfcc")
    x = layers.Reshape((FRAMES, BINS, 1))(inp)
    # 首层 stride=2：把最大激活张量压到 25x7x16=2800，使 TFLM arena 能放进 M55 内部 RAM
    x = layers.Conv2D(16, (3, 3), strides=2, padding="same", use_bias=False)(x)
    x = layers.BatchNormalization()(x); x = layers.ReLU()(x)
    x = layers.SeparableConv2D(32, (3, 3), padding="same", use_bias=False)(x)
    x = layers.BatchNormalization()(x); x = layers.ReLU()(x)
    x = layers.MaxPool2D((2, 2))(x)
    x = layers.SeparableConv2D(64, (3, 3), padding="same", use_bias=False)(x)
    x = layers.BatchNormalization()(x); x = layers.ReLU()(x)
    x = layers.MaxPool2D((2, 2))(x)
    x = layers.GlobalAveragePooling2D()(x)
    x = layers.Dropout(0.3)(x)
    out = layers.Dense(NCLASS, activation="softmax", name="scores")(x)
    return models.Model(inp, out)


def main():
    X, y = build(n_aug=10, seed=42)                # (N,650) 直接作为输入

    # 打乱 + 划分
    rng = np.random.default_rng(0)
    idx = rng.permutation(len(X)); X, y = X[idx], y[idx]
    n_val = max(1, int(len(X) * 0.2))
    Xtr, ytr = X[n_val:], y[n_val:]
    Xva, yva = X[:n_val], y[:n_val]
    print(f"训练 {len(Xtr)} / 验证 {len(Xva)}")

    model = make_model()
    model.compile(optimizer=tf.keras.optimizers.Adam(1e-3),
                  loss="sparse_categorical_crossentropy", metrics=["accuracy"])
    model.summary()

    cw = {0: len(ytr) / (2 * max(1, (ytr == 0).sum())),
          1: len(ytr) / (2 * max(1, (ytr == 1).sum()))}
    model.fit(Xtr, ytr, validation_data=(Xva, yva), epochs=60, batch_size=32,
              class_weight=cw, verbose=2,
              callbacks=[callbacks.EarlyStopping(patience=12, restore_best_weights=True)])

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

    path = os.path.join(OUT, "kws_xiaomai.tflite")
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
    with open(os.path.join(OUT, "kws_xiaomai_meta.json"), "w") as f:
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


if __name__ == "__main__":
    main()
