"""pyOCD attach hook: inspect allocation metadata only; ELF must match board."""
from pathlib import Path
import collections
import re
import subprocess
ROOT = Path(__file__).resolve().parents[3]
patch = ROOT / "tools/freather/pyocd_pse84_debug_patch.py"
exec(compile(patch.read_text(encoding="utf8"), str(patch), "exec"))

def did_connect(board):
    nm = ROOT / "tools/freather/arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi/bin/arm-none-eabi-nm.exe"
    symbols = subprocess.check_output([str(nm), str(ROOT / "projects/FeatherTalk_M55/rt-thread.elf")], text=True)
    def sym(name):
        return int(re.search(r"(?m)^([0-9a-f]+) \w " + name + r"$", symbols)[1], 16)
    start, limit = sym("__HeapBase"), sym("__HeapLimit")
    ap = board.target.dp.aps[APv2Address(0xf0006000)]
    pointer = start
    hist = collections.Counter()
    free = used = 0
    for _ in range(4096):
        if not start <= pointer <= limit - 24:
            raise RuntimeError("Heap pointer outside validated region")
        magic, pool, nxt, prev, nextfree, prevfree = ap.read_memory_block32(pointer, 6)
        if magic & 0xfffffffe != 0x1ea01ea0:
            raise RuntimeError("Invalid allocation header")
        if nxt == start:
            break
        if nxt <= pointer or nxt > limit-24:
            raise RuntimeError("Invalid next block")
        size = nxt - pointer - 24
        if magic & 1:
            hist[size] += 1
            used += size
        else:
            free += size
        pointer = nxt
    print("HEAP_METADATA used", used, "free", free, "blocks", sum(hist.values()))
    print("LARGEST_TOTAL_ALLOCATIONS", sorted(hist.items(), key=lambda x:x[0]*x[1], reverse=True)[:20])
