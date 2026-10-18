#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
map_size_report.py -- 从链接图（rtthread.map）做固件体积归因，回答 R0.5-3
「EI SDK 集成固件增量 ≤300KB」到底占了多少。

为什么用链接图而不是比 hex 大小：hex 差量只能给一个总数，看不出这 200 多 KB 是
模型字节、TFLM 解释器、EI DSP 还是 CMSIS 核函数；R1/R2 要不要上更大的模型、
能不能再压，取决于这个分解。

口径说明（重要，别把上界当实测）：
  * 归因按**目标文件路径**分桶；这些桶全部来自 kws/SConscript 拉入的 edge-impulse 副本
    （含 EI 自带的 CMSIS 副本），工程内没有其它模块引用它们，所以按路径归因≈真实增量。
  * flash 计入 .text/.rodata/.ARM.exidx（XIP 段，全部占外部 flash）；
    RAM 计入 .data/.bss（DTCM/ITCM）。
  * gc-sections 已生效，所以这里统计的是**真正链接进去**的部分，不是编译了多少个 .c。

用法：
  python map_size_report.py [..\\..\\..\\..\\rtthread.map]   # 缺省自动定位工程根下的 map
"""
import os
import re
import sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
APP = os.path.abspath(os.path.join(HERE, "..", "..", ".."))      # applications/
PROJ = os.path.abspath(os.path.join(APP, ".."))                  # projects/FeatherTalk_M55/

SEC_RE = re.compile(r'^\s(\.[A-Za-z0-9_.\-]+)\s*$')
FULL_RE = re.compile(r'^\s(\.[A-Za-z0-9_.\-]+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(.+?)\s*$')
CONT_RE = re.compile(r'^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(.+?)\s*$')
FLASH = (".text", ".rodata", ".ARM.exidx", ".gcc_except_table", ".constdata", ".sdata2")
RAM = (".data", ".bss", ".sdata", ".sbss")


def bucket_of(path, sec_name=""):
    p = path.replace("\\", "/")
    if "tflite_learn" in sec_name or "tflite_learn" in p:
        return "模型字节(tflite)"
    if "CMSIS/NN" in p:
        return "CMSIS-NN 核函数(EI 副本)"
    if "CMSIS/DSP" in p:
        return "CMSIS-DSP 核函数(EI 副本)"
    if "CMSIS" in p:
        return "CMSIS 其它(EI 副本)"
    if "edge-impulse-sdk/tensorflow" in p:
        return "TFLM 解释器/算子"
    if "edge-impulse-sdk/dsp" in p:
        return "EI DSP(MFCC/kissfft)"
    if "edge-impulse-sdk/classifier" in p:
        return "EI classifier"
    if "edge-impulse-sdk" in p:
        return "EI SDK 其它(hal/porting)"
    if "/applications/kws/" in p or "applications/kws" in p:
        return "KWS 应用代码"
    if p.startswith("C:/ft/tools") or "libgcc" in p or "libstdc++" in p or "libc.a" in p:
        return "C/C++ 运行时"
    return None


def parse(map_path):
    flash, ram, sections = defaultdict(int), defaultdict(int), []
    pending = None
    region = None            # 只在 Memory Map 区统计；Discarded 区的地址恒为 0

    def record(name, addr, size, obj):
        if not name.startswith(".") or size >= (1 << 24) or ".o" not in obj:
            return
        if name.startswith(".debug"):
            return
        sections.append((name, size, obj))
        b = bucket_of(obj, name)
        if not b:
            return
        if name.startswith(FLASH):
            flash[b] += size
        elif name.startswith(RAM):
            ram[b] += size

    with open(map_path, encoding="utf-8", errors="replace") as f:
        for line in f:
            st = line.strip()
            if st.startswith("Discarded input sections"):
                region = "discarded"
                pending = None
                continue
            if st.startswith("Linker script and memory map") or st.startswith("Memory Map of the image"):
                region = "map"
                pending = None
                continue
            if st.startswith("Cross Reference Table"):
                region = "xref"
                pending = None
                continue
            if region != "map":
                continue
            m = FULL_RE.match(line)                      # 段名/地址/大小/文件 同一行
            if m:
                pending = None
                record(m.group(1), int(m.group(2), 16), int(m.group(3), 16), m.group(4))
                continue
            m2 = CONT_RE.match(line)                     # 续行：地址 大小 文件
            if m2 and pending:
                record(pending, int(m2.group(1), 16), int(m2.group(2), 16), m2.group(3))
                pending = None
                continue
            m3 = SEC_RE.match(line)                      # 单独一行的段名
            pending = m3.group(1) if m3 else None
    return flash, ram, sections


def main():
    argv, pat = [], None
    for a in sys.argv[1:]:
        if a.startswith("--match="):
            pat = a.split("=", 1)[1]
        else:
            argv.append(a)
    ap = argv[0] if argv else os.path.join(PROJ, "rtthread.map")
    if not os.path.exists(ap):
        print(f"找不到链接图: {ap}\n（先在 C:\\ft 下用 SCons 编一次）")
        return 1
    flash, ram, sections = parse(ap)
    kb = lambda n: n / 1024.0
    total_f = sum(flash.values())
    total_r = sum(ram.values())

    print(f"链接图: {ap}")
    print(f"输入段条目 {len(sections)} 条；KWS 相关桶按目标文件路径归因\n")
    print(f"{'分桶':26s} {'Flash/XIP':>12s} {'RAM':>12s}")
    print("-" * 54)
    for b in sorted(flash, key=lambda x: -flash[x]):
        print(f"{b:26s} {kb(flash[b]):9.1f}KB {kb(ram.get(b, 0)):9.1f}KB")
    for b in sorted(ram, key=lambda x: -ram[x]):
        if b not in flash:
            print(f"{b:26s} {0.0:9.1f}KB {kb(ram[b]):9.1f}KB")
    print("-" * 54)
    print(f"{'合计（KWS 集成增量）':24s} {kb(total_f):9.1f}KB {kb(total_r):9.1f}KB")

    gate = 300.0
    print(f"\nR0.5-3 判据 ≤{gate:.0f}KB：{'✓ 达标' if total_f <= gate else '✗ 超'}"
          f"（实测 {total_f/1024.0:.1f}KB）")
    print("口径：这些桶全部来自 kws/SConscript 拉入的 edge-impulse 副本（含 EI 自带的 CMSIS 副本），")
    print("      工程内没有其它模块引用它们，所以按路径归因≈真实增量；唯一需要复核的是")
    print("      「C/C++ 运行时」——kws 是本工程少数 C++ 模块，关掉 KWS 后 libstdc++ 这些成员会整体消失。")
    print("      若要绝对数：同一份代码开/关 KWS 各编一次，比 rtthread.hex 的 .text+.rodata 段总和。")
    print("注：模型换成更大的（如 R1 命令词/R2 ASR）时，主要涨的是「模型字节」与「TFLM 算子」两桶，")
    print("    本脚本可直接复跑对比，无需重新估算。")

    mv = os.path.join(PROJ, "..", "Edgi_Talk_M55_XiaoZhi", "edge-impulse",
                      "model-parameters", "model_metadata.h")
    if os.path.exists(mv):
        txt = open(mv, encoding="utf-8", errors="replace").read()

        def define(k):
            m = re.search(rf"#define\s+{k}\s+\(?\s*(\d+)", txt)
            return int(m.group(1)) if m else 0

        arena, raw = define("EI_CLASSIFIER_TFLITE_LARGEST_ARENA_SIZE"), \
            define("EI_CLASSIFIER_RAW_SAMPLE_COUNT")
        if arena or raw:
            print(f"\n静态图之外的 RAM（运行时 heap，板上 mem 才能看到，归 R0.5-4 画像）：")
            print(f"  TFLM arena {kb(arena):.1f}KB + 原始采样环形缓冲 {kb(raw * 2):.1f}KB"
                  f" = {kb(arena + raw * 2):.1f}KB（EI 内部按 16 字节对齐另计）")

    top = sorted(sections, key=lambda s: -s[1])[:15]
    print("\n最大的 15 个输入段（看体积到底压在哪个符号上）：")
    for name, size, obj in top:
        o = obj.replace("\\", "/")
        o = o.split("edge-impulse-sdk/")[-1] if "edge-impulse-sdk/" in o else \
            o.split("/build/")[-1] if "/build/" in o else os.path.basename(o)
        print(f"  {kb(size):8.1f}KB  {name:28s} {o[:70]}")
    if pat:
        rx = re.compile(pat)
        agg = defaultdict(int)
        for name, size, obj in sections:
            if name.startswith(FLASH) and rx.search(obj.replace("\\", "/")):
                base = os.path.basename(obj.replace("\\", "/"))
                agg[base] += size
        tot = sum(agg.values())
        print(f"\n--match={pat!r} 命中对象（只看真正链接进的 .text/.rodata 等 XIP 段）：")
        for o in sorted(agg, key=lambda x: -agg[x]):
            print(f"  {kb(agg[o]):8.1f}KB  {o}")
        print(f"  {'合计':8s} {kb(tot):8.1f}KB  ← 这些段若全部消失可省的 flash")
    return 0


if __name__ == "__main__":
    sys.exit(main())
