#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
把 Vela npz 产物转成固件用的 npu_model.c：
  cmd_data    -> npu_cmd_stream[]（命令流，328B）
  weight_data -> npu_weight_blob[]（Vela 重编码的权重块，只能来自 Vela）
  scratch_size/input_shape -> 尺寸常量
产物拷到 applications/npu_bench/ 覆盖 npu_model_stub.c 的位置（同名 npu_model.c）
"""

import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(HERE, "out")


def strip_cop_wrapper(cmd):
    """Vela raw 产物 npz 的 cmd_data 是 **COP 驱动动作流**，不是裸 NPU 命令流。

    结构（按 DRIVER_ACTION_e）：
      w0            "COP1" 魔数（驱动仅校验用）
      w1            OPTIMIZER_CONFIG 记录（固定占 3 字）
      w4..w6        NOP x3
      w7            COMMAND_STREAM 记录（cmd=2, length=命令流字数）
      w8 起         真正的 NPU 命令流  ← QBASE 必须指到这里
    教训 10-20：直接把整个 COP 流喂 QBASE → STATUS.cmd_parse_error + QREAD=0
    （NPU 把 "COP1" 魔数当成命令字，第一个命令就崩）。
    """
    import struct
    w = struct.unpack("<%dI" % (len(cmd) // 4), bytes(cmd))
    if w[0] != 0x31504F43:
        raise SystemExit("[emit] cmd_data 头不是 COP1 魔数：0x%08x" % w[0])
    i = 1
    while i < len(w):
        x = w[i]
        action = x & 0xFF
        length_words = (x >> 16) & 0xFFFF
        if action == 2:      # COMMAND_STREAM
            payload_off = 4 * (i + 1)
            payload_len = length_words * 4
            if payload_off + payload_len > len(cmd):
                raise SystemExit("[emit] COMMAND_STREAM 载荷越界")
            return bytes(cmd)[payload_off:payload_off + payload_len]
        elif action == 1:    # OPTIMIZER_CONFIG：固定 3 字
            i += 1 + 2
        elif action == 5:    # NOP
            i += 1
        else:
            raise SystemExit("[emit] 未知 driver_action_command=%u @w%d" % (action, i))
    raise SystemExit("[emit] COP 流里没有 COMMAND_STREAM 记录")


def emit(npz_path):
    z = np.load(npz_path, allow_pickle=True)
    cop = z["cmd_data"].item() if z["cmd_data"].shape == () else bytes(z["cmd_data"].flatten())
    cmd = strip_cop_wrapper(cop)
    wblob = z["weight_data"].item() if z["weight_data"].shape == () else bytes(z["weight_data"].flatten())
    scratch = int(z["scratch_size"].item())
    ishape = [int(v) for v in z["input_shape"].flatten()]
    in_bytes = 1
    for v in ishape:
        in_bytes *= v
    oshape = [int(v) for v in z["output_shape"].flatten()]
    out_bytes = 1
    for v in oshape:
        out_bytes *= v
    in_bytes = max(in_bytes, out_bytes)   # 输入输出同尺寸，取大

    def c_array(name, data, per=16):
        lines = ["const unsigned char %s[] = {" % name]
        for i in range(0, len(data), per):
            chunk = data[i:i + per]
            lines.append("    " + ",".join("0x%02x" % b for b in chunk) + ",")
        lines.append("};")
        return "\n".join(lines)

    src = []
    src.append("/*")
    src.append(" * NPU 带宽基准模型（gen_tflite_flat.py + Vela 自动生成，勿手改）")
    src.append(" * 图：int8 FullyConnected 1024x1024（Ethos-U55-128 编译，100% NPU 算子）")
    src.append(" * 命令流 %d B / 权重块 %d B / scratch %d B / 输入输出 %d B"
               % (len(cmd), len(wblob), scratch, in_bytes))
    src.append(" */")
    src.append("")
    src.append(c_array("npu_cmd_stream", cmd))
    src.append("const unsigned int  npu_cmd_stream_len = %d;" % len(cmd))
    src.append("")
    src.append(c_array("npu_weight_blob", wblob))
    src.append("const unsigned int  npu_weights_len = %d;" % len(wblob))
    src.append("const unsigned int  npu_scratch_len = %d;" % scratch)
    src.append("const unsigned int  npu_input_len = %d;" % in_bytes)
    src.append("")

    out_path = os.path.join(os.path.dirname(HERE), "npu_model.c")
    with open(out_path, "w") as fp:
        fp.write("\n".join(src))
    print("[emit] wrote %s (%d bytes text)" % (out_path, os.path.getsize(out_path)))


if __name__ == "__main__":
    npz = sys.argv[1] if len(sys.argv) > 1 else os.path.join(OUT_DIR, "vela1024", "npu_fc1024_vela.npz")
    emit(npz)
