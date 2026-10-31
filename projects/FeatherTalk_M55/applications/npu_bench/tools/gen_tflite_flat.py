#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
R0.5-2 生成 NPU 带宽基准模型（无 TF 依赖：手写 flatbuffer 建 int8 FC 图）

为什么不用 TF：tensorflow-cpu 在托管 python3.13 上卡死 1 小时下不动；
int8 FC 的图足够简单（一个 FULLY_CONNECTED），直接用 tflite schema 拼。

模型：输入 [1,n] int8 -> FULLY_CONNECTED -> [1,n] int8，权重 [n,n] int8 = n*n 字节。
权重用**随机数据**（不能全 0：U55 权重解码器有 0 游程压缩，全 0 会让命令流
几乎不读权重 → 带宽测出来是假的）。

产物：out/npu_fc_int8.tflite
"""

import argparse
import os

import flatbuffers
import numpy as np
import tflite as tf

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(HERE, "out")


def off_vector(builder, items):
    """构造 offset 向量（items 已按正序给出）"""
    builder.StartVector(4, len(items), 4)
    for it in reversed(items):
        builder.PrependUOffsetTRelative(it)
    return builder.EndVector()


def int_vector(builder, items):
    """构造 int32 向量（用于 Operator 的输入/输出张量索引）"""
    builder.StartVector(4, len(items), 4)
    for v in reversed(items):
        builder.PrependInt32(v)
    return builder.EndVector()


def make_quant(builder, scale, zp):
    """per-tensor 量化参数：scale/zero_point 各 1 元素"""
    s = builder.CreateNumpyVector(np.array([scale], dtype=np.float32))
    z = builder.CreateNumpyVector(np.array([zp], dtype=np.int32))
    tf.QuantizationParametersStart(builder)
    tf.QuantizationParametersAddScale(builder, s)
    tf.QuantizationParametersAddZeroPoint(builder, z)
    return tf.QuantizationParametersEnd(builder)


def add_tensor(builder, name, shape, ttype, buffer_idx, quant):
    name_off = builder.CreateString(name)
    shape_off = off_vector(builder, [])  # 先占位，下面真填
    raise SystemExit("unused")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=64)
    ap.add_argument("--out", default=os.path.join(OUT_DIR, "npu_fc_int8.tflite"))
    args = ap.parse_args()

    n = args.n
    rng = np.random.default_rng(20261019)
    weights = rng.integers(-128, 128, size=(n, n), dtype=np.int8).tobytes()
    bias = rng.integers(-100, 100, size=(n,)).astype(np.int32).tobytes()

    fb = flatbuffers.Builder(1024 * 1024)

    # ============ 1. buffers（子表，最先建） ============
    # [0] 空, [1] 输入(空), [2] 权重, [3] bias, [4] 输出(空)
    bufs = []
    for payload in (None, None, weights, bias, None):
        if payload is None:
            tf.BufferStart(fb)
            bufs.append(tf.BufferEnd(fb))
        else:
            data = fb.CreateByteVector(payload)
            tf.BufferStart(fb)
            tf.BufferAddData(fb, data)
            bufs.append(tf.BufferEnd(fb))

    # ============ 2. 量化参数 ============
    q_in = make_quant(fb, 0.05, 0)
    q_w = make_quant(fb, 0.02, 0)
    q_b = make_quant(fb, 0.05 * 0.02, 0)
    q_out = make_quant(fb, 0.1, 0)

    # ============ 3. tensors（引用 buffer/quant） ============
    def mk_tensor(name, shape, ttype, buf_idx, quant):
        name_off = fb.CreateString(name)
        # shape 向量
        fb.StartVector(4, len(shape), 4)
        for d in reversed(shape):
            fb.PrependInt32(d)
        shape_off = fb.EndVector()
        tf.TensorStart(fb)
        tf.TensorAddShape(fb, shape_off)
        tf.TensorAddType(fb, ttype)
        tf.TensorAddBuffer(fb, buf_idx)
        tf.TensorAddQuantization(fb, quant)
        tf.TensorAddName(fb, name_off)
        return tf.TensorEnd(fb)

    t_in = mk_tensor("x", [1, n], tf.TensorType.INT8, 1, q_in)
    t_w = mk_tensor("w", [n, n], tf.TensorType.INT8, 2, q_w)
    t_b = mk_tensor("b", [n], tf.TensorType.INT32, 3, q_b)
    t_out = mk_tensor("y", [1, n], tf.TensorType.INT8, 4, q_out)

    # ============ 4. FullyConnectedOptions（fused_activation=NONE=0） ============
    tf.FullyConnectedOptionsStart(fb)
    fc_opts = tf.FullyConnectedOptionsEnd(fb)

    # ============ 5. Operator ============
    # 注意：Operator.inputs/outputs 是 int32 索引向量（不是 offset 向量！）
    op_in_vec = off_vector(fb, [t_in, t_w, t_b]) if False else int_vector(fb, [0, 1, 2])
    op_out_vec = int_vector(fb, [3])
    tf.OperatorStart(fb)
    tf.OperatorAddOpcodeIndex(fb, 0)
    tf.OperatorAddInputs(fb, op_in_vec)
    tf.OperatorAddOutputs(fb, op_out_vec)
    tf.OperatorAddBuiltinOptions(fb, fc_opts)
    tf.OperatorAddBuiltinOptionsType(fb, tf.BuiltinOptions.FullyConnectedOptions)
    op = tf.OperatorEnd(fb)

    # ============ 6. OperatorCode（FULLY_CONNECTED = 9） ============
    tf.OperatorCodeStart(fb)
    tf.OperatorCodeAddBuiltinCode(fb, tf.BuiltinOperator.FULLY_CONNECTED)
    tf.OperatorCodeAddDeprecatedBuiltinCode(fb, 9)
    tf.OperatorCodeAddVersion(fb, 1)
    opcode = tf.OperatorCodeEnd(fb)

    # ============ 7. SubGraph ============
    # 注意：tensors 是 offset 向量；inputs/outputs 是 int32 索引向量
    tensors_vec = off_vector(fb, [t_in, t_w, t_b, t_out])
    inputs_vec = int_vector(fb, [0])
    outputs_vec = int_vector(fb, [3])
    ops_vec = off_vector(fb, [op])
    tf.SubGraphStart(fb)
    tf.SubGraphAddTensors(fb, tensors_vec)
    tf.SubGraphAddInputs(fb, inputs_vec)
    tf.SubGraphAddOutputs(fb, outputs_vec)
    tf.SubGraphAddOperators(fb, ops_vec)
    subgraph = tf.SubGraphEnd(fb)

    # ============ 8. Model ============
    buffers_vec = off_vector(fb, bufs)
    opcodes_vec = off_vector(fb, [opcode])
    subgraphs_vec = off_vector(fb, [subgraph])
    desc = fb.CreateString("npu_bw_bench")

    tf.ModelStart(fb)
    tf.ModelAddVersion(fb, 3)
    tf.ModelAddOperatorCodes(fb, opcodes_vec)
    tf.ModelAddSubgraphs(fb, subgraphs_vec)
    tf.ModelAddDescription(fb, desc)
    tf.ModelAddBuffers(fb, buffers_vec)
    model = tf.ModelEnd(fb)
    fb.Finish(model, b"TFL3")

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "wb") as fp:
        fp.write(fb.Output())
    print("[gen] wrote %s (%d bytes, n=%d, weights=%d bytes)"
          % (args.out, os.path.getsize(args.out), n, n * n))


if __name__ == "__main__":
    main()
