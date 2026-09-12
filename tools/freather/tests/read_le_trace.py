"""Decode bounded, key-redacted M33 protocol trace through the debug probe."""
import re
import struct
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
TOOLS = ROOT / "tools/freather"
nm = TOOLS / "arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi/bin/arm-none-eabi-nm.exe"
symbols = subprocess.check_output([str(nm), "-n", str(ROOT / "projects/FeatherTalk_M33/rt-thread.elf")], text=True)
def address(name):
    return int(re.search(r"(?m)^([0-9a-f]+) \w " + name + r"$", symbols)[1], 16)

count_addr, trace_addr = address("g_le_trace_count"), address("g_le_trace")
diag_addr = address("g_le_connect_diag")
commands = ["core 2", f"read32 {count_addr:#x} 4",
            f"read32 {trace_addr:#x} 4096", f"read32 {diag_addr:#x} 32",
            f"read32 {count_addr:#x} 4"]
args = [str(TOOLS / "pyocd/pyocd.exe"), "commander", "--connect", "attach",
        "--uid", "0D141868022E2400", "--target", "cortex_m",
        "--script", str(TOOLS / "pyocd_pse84_debug_patch.py"), "--frequency", "1000000"]
for command in commands:
    args += ["--command", command]
result = subprocess.run(args, capture_output=True, text=True, timeout=30)
if result.returncode:
    raise RuntimeError(result.stdout + result.stderr)
memory = {}
counts = []
for line in result.stdout.splitlines():
    match = re.match(r"^([0-9a-fA-F]{8}):\s+((?:[0-9a-fA-F]{8}\s*)+)", line)
    if not match:
        continue
    start = int(match[1], 16)
    words = match[2].split()
    if start == count_addr:
        counts.append(int(words[0], 16))
    for i, word in enumerate(words):
        memory[start + i * 4] = struct.pack("<I", int(word, 16))
if len(counts) != 2 or counts[0] != counts[1]:
    raise RuntimeError("Trace changed during read; retry after the connection attempt finishes.")
count = counts[0]
print("TRACE_COUNT", count)
names = ["connections", "disconnect_callbacks", "disconnect_reason", "pair_completions",
         "pair_status", "pair_reason", "last_dynamic_att", "last_write_opcode"]
for i, name in enumerate(names):
    print(name, hex(struct.unpack("<I", memory[diag_addr + i * 4])[0]))
for index in range(max(0, count - 64), count):
    start = trace_addr + index % 64 * 64
    row = b"".join(memory[start + i * 4] for i in range(16))
    ms, kind, incoming, length, captured, data = struct.unpack("<IBBBB56s", row)
    print(index, ms, "RX" if incoming else "TX", "type", kind,
          "len", length, data[:captured].hex())
