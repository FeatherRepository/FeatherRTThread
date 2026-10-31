#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""解码 Vela 裸 NPU 命令流（剥 COP 壳后的），定位 byte 288 处的命令。

用法：python decode_cmd.py [npz路径]
"""
import os
import struct
import sys

import numpy as np
from ethosu.vela.ethos_u55_regs.ethos_u55_regs import cmd0, cmd1
from ethosu.vela.register_command_stream_generator import CmdMode

HERE = os.path.dirname(os.path.abspath(__file__))


def strip_cop_wrapper(cmd):
    w = struct.unpack("<%dI" % (len(cmd) // 4), bytes(cmd))
    assert w[0] == 0x31504F43, "cmd_data 头不是 COP1 魔数：0x%08x" % w[0]
    i = 1
    while i < len(w):
        x = w[i]
        action = x & 0xFF
        length_words = (x >> 16) & 0xFFFF
        if action == 2:      # COMMAND_STREAM
            payload_off = 4 * (i + 1)
            payload_len = length_words * 4
            return bytes(cmd)[payload_off:payload_off + payload_len]
        elif action == 1:    # OPTIMIZER_CONFIG
            i += 1 + 2
        elif action == 5:    # NOP
            i += 1
        else:
            raise SystemExit("未知 driver_action=%u @w%d" % (action, i))
    raise SystemExit("COP 流里没有 COMMAND_STREAM 记录")


def cmd_name(code):
    """区分 cmd0（无载荷单字）与 cmd1（0x4000 标志 + 32位偏移载荷双字）"""
    mode = code & CmdMode.Mask.value
    try:
        if mode == CmdMode.Payload32.value:
            return cmd1(code & CmdMode.CmdOpMask.value).name
        return cmd0(code & CmdMode.CmdOpMask.value).name
    except ValueError:
        return "UNKNOWN(0x%x)" % code


def main():
    npz = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "out", "vela32", "npu_fc32_vela.npz")
    z = np.load(npz, allow_pickle=True)
    cop = z["cmd_data"].item() if z["cmd_data"].shape == () else bytes(z["cmd_data"].flatten())
    raw = strip_cop_wrapper(cop)
    print("[decode] 命令流 %d 字节 = %d 字" % (len(raw), len(raw) // 4))
    print("[decode] 实测 QREAD=288 = 第 %d 字（0基）" % (288 // 4))

    # 按 emitter 的命令边界切分（cmd1 是双字）
    w = struct.unpack("<%dI" % (len(raw) // 4), raw)
    i = 0
    idx = 0
    while i < len(w):
        x = w[i]
        code = x & 0xFFFF
        param = x >> 16
        mode = code & CmdMode.Mask.value
        name = cmd_name(code)
        if mode == CmdMode.Payload32.value:
            off = w[i + 1] if i + 1 < len(w) else 0
            note = " -> offset=0x%08x" % off
            i += 2
        else:
            note = ""
            i += 1
        mark = "  <<< NPU 停在这里" if idx == 72 else ""
        if "REGION" in name:
            note += "  [BASEP index=%d]" % param
        print("cmd%02d @+%3d: 0x%08x  param=0x%04x  %-26s%s%s"
              % (idx, idx * 4 if False else 0, x, param, name, note, mark))
        idx += 1
        if idx > 90:
            break


if __name__ == "__main__":
    main()
