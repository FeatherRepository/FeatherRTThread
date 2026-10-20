# npu_bench — Ethos-U55 NPU 裸寄存器带宽基准（R0.5-2）

直寄存器路径 + Vela raw 命令流，不依赖 BSP 驱动与 DEEPCRAFT 例程。用途：测 NPU
**权重流读带宽**——R3（本地 LLM）架构的生死数：权重常驻共享 HyperRAM 时每 token
要流读多少毫秒。

## 结论（2026-10-20 晚，20 轮均）

| 权重位置 | NPU 带宽 | CPU 同位置（R0.5-1） |
|---|---|---|
| M55 专区 0x64240000 | 88.4 MB/s | 79 MB/s |
| **共享 HyperRAM 0x64C00000** | **89.4 MB/s** | **20 MB/s** |
| Flash XIP | 8.8 MB/s | — |
| B128 burst（0x64C00000） | 90.4 MB/s | — |

~90MB/s 是这条 AXI 通路上限（128-bit burst 无增益）。**NPU 的 AXI 主口在共享区比
CPU 快 4.5 倍**——10-19 晚"CPU 读共享区 20MB/s 判否"否掉的是 CPU 主控方案，
NPU 方案复活：13MB int4 全量纯权重流读 ≈146ms/token（仅 NPU 可委托层）。

## 板上用法（M55 控制台 = uart2 = COM9）

```
npu_bench hram          # 共享 HyperRAM 0x64C00000，默认 B64 burst
npu_bench hram b128     # 同地址 + AXI_LIMIT max_beats=1（128-bit）
npu_bench m55           # M55 专区 0x64240000
npu_bench flash         # SoC XIP Flash
npu_bench hram_safe     # 0x64C00000 之外的 4MB 已映射未分配区（0x64C00000 之外的对照）
```

## 踩过的坑（改代码前先读）

1. **BASEP 步进是 8 字节**：`basep_r` 是 64 位寄存器（word[2]），U55 只用低 32 位、
   高字忽略。按 4 字节步进写，第 2 个 basep（scratch）会落进 0x84（BASEP[0] 高字）
   被硬件忽略 → BASEP[1]=0 → IFM 从 0x20 读 → bus abort。10-20 白天数小时的
   "访问任何内存都 abort"就是这个，**不是 BSP MPC/AXI 权限问题**。
2. **STATUS bit5 `cmd_end_reached` 是粘滞位**，只在写 QBASE/QSIZE 时清。不在每次
   invoke 前重写基址，轮询会立刻看到上轮的结果位空转（假象：20 次 OK 只花 1ms、
   23240MB/s）。
3. CMD 寄存器：bit0 transition_to_running，**bit2 clock_q_enable / bit3 power_q_enable
   要在软复位后置位**（与官方 ethosu_request_power 对齐）。
4. Vela npz 的 `cmd_data` 是**驱动动作流**（COP 包装），只有 `COMMAND_STREAM` 记录
   指向的载荷才是裸命令流——QBASE 必须指向它（emit_model_c.py 的 strip_cop_wrapper）。

## 重建模型数据（npu_model.c 不入库，6.4MB）

```
python tools/gen_tflite_flat.py          # 建 int8 FC(1024) 图 → tools/out/npu_fc1024.tflite
# Vela 编译（需 pip 环境有 ethosu-vela）：
python -m ethosu.vela --accelerator-config ethos-u55-128 \
    --system-config Ethos-U55 --memory-mode Shared_Memory \
    --output-dir tools/out/vela1024 tools/out/npu_fc1024.tflite
python tools/emit_model_c.py             # 剥 COP 壳 → npu_model.c
```

`npu_model.c` 与 `tools/out/` 均已加 .gitignore（可再生产物）。调试命令流解码用
`tools/decode_cmd.py`（用 Vela 自己的 cmd0/cmd1 枚举解mask 裸流）。
