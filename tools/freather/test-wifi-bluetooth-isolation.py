"""Idle-radio coexistence regression; never joins an AP or resets a controller.

Flash BOTH matching images first. Attach official OpenOCD without reset (Tcl
port 6666). The script reads M33 SRAM through its debug AP while it RUNS; no
halt, HCI probe, pairing, or M33 serial connection is required. Requires idle
Bluetooth (the existing advertising keepalive supplies real HCI replies).
Wi-Fi is restored to enabled in a finally block. Raw SSIDs are not logged.
"""
import argparse
import json
from pathlib import Path
import re
import socket
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools/freather"
sys.path.insert(0, str(TOOLS / "serial-monitor/vendor"))
import serial


class DebugReader:
    def __init__(self, port, target, elf):
        nm = TOOLS / "arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi/bin/arm-none-eabi-nm.exe"
        output = subprocess.check_output([str(nm), "--defined-only", str(elf)], text=True)
        self.symbols = {name: int(address, 16) for address, name in
                        re.findall(r"^([0-9a-fA-F]+)\s+\S\s+(\S+)$", output, re.M)}
        self.target = target
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=5)
        if self.command(target + " curstate").strip() != "running":
            raise RuntimeError("M33 must already be running; test will not resume/reset it")

    def command(self, command):
        self.socket.sendall(command.encode("ascii") + b"\x1a")
        data = bytearray()
        while b"\x1a" not in data:
            part = self.socket.recv(4096)
            if not part:
                raise RuntimeError("OpenOCD disconnected")
            data.extend(part)
        return data.split(b"\x1a", 1)[0].decode("ascii")

    def words(self, symbol, count=1):
        return self.words_at(self.symbols[symbol], count)

    def words_at(self, address, count):
        text = self.command(f"{self.target} read_memory 0x{address:x} 32 {count}")
        values = [int(token, 0) for token in text.strip().split()]
        if len(values) != count:
            raise RuntimeError("Invalid OpenOCD memory response: " + text)
        return values

    def snapshot(self):
        values = self.words("g_bt_coex_diag", 5)
        snapshot = dict(zip(("starts", "command_completes", "advertising_acks",
                             "hardware_errors", "last_complete_ms"), values))
        for name in ("s_bt_state", "s_bt_err", "s_gatt_connected",
                     "s_classic_connected", "g_h4_tx_timeout",
                     "g_tx_count", "g_rx_count", "g_error_count"):
            snapshot[name] = self.words(name)[0]
        shared = self.words_at(0x240FFF40, 24)
        if shared[0] != 0x46545244 or shared[1] != 1:
            raise RuntimeError("Matching dual-core radio-manager firmware must be flashed first")
        snapshot["supply_on"] = shared[2]
        snapshot["wifi_state"] = shared[11]
        snapshot["wifi_resets"] = shared[12]
        snapshot["bt_state"] = shared[19]
        snapshot["bt_resets"] = shared[20]
        return snapshot

    def close(self):
        self.socket.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True, help="M55 console, e.g. COM17")
    parser.add_argument("--tcl-port", type=int, default=6666)
    parser.add_argument("--target", default="cat1d.cm33")
    parser.add_argument("--elf", type=Path, default=ROOT / "projects/FeatherTalk_M33/rt-thread.elf")
    parser.add_argument("--cycles", type=int, choices=range(1, 11), default=3)
    parser.add_argument("--log", type=Path, default=ROOT / "tmp/wifi-bt-isolation.jsonl")
    args = parser.parse_args()
    args.log.parent.mkdir(parents=True, exist_ok=True)
    debug = DebugReader(args.tcl_port, args.target, args.elf)
    try:
        with args.log.open("w", encoding="utf-8") as log, \
                serial.Serial(args.serial, 115200, timeout=0.1) as port:
            checks = 0

            def record(kind, data):
                line = json.dumps({"kind": kind, "data": data}, ensure_ascii=False)
                log.write(line + "\n")
                log.flush()
                print(line, flush=True)

            def check(condition, message):
                nonlocal checks
                if not condition:
                    record("FAIL", message)
                    raise AssertionError(message)
                checks += 1
                record("PASS", message)

            def read(seconds, pattern=None):
                result = ""
                deadline = time.monotonic() + seconds
                while time.monotonic() < deadline:
                    result += port.read(port.in_waiting or 1).decode("utf-8", "replace")
                    if pattern and re.search(pattern, result):
                        return result
                if pattern:
                    raise RuntimeError("Missing console response: " + pattern)
                return result

            def command(text, seconds=2, pattern=None):
                port.write((text + "\r").encode("ascii"))
                return read(seconds, pattern)

            def wifi_status(expected):
                deadline = time.monotonic() + 6
                while True:
                    status = command("ft_wifi", 0.7)
                    if expected in status or time.monotonic() >= deadline:
                        # Keep network names/passwords out of regression logs.
                        record("wlan-status", re.findall(r"\[wifi\] rc=[^\r\n]*", status))
                        return status

            def progress(label, previous):
                # Existing advertising keepalive interval is 3 s. Allow two
                # periods, without sending ANY additional HCI traffic.
                deadline = time.monotonic() + 8
                while True:
                    current = debug.snapshot()
                    if current["advertising_acks"] > previous["advertising_acks"]:
                        break
                    if time.monotonic() >= deadline:
                        break
                    read(0.4)
                record(label, current)
                check(current["starts"] == initial["starts"], label + ": no BT restart")
                check(current["s_bt_state"] == 2 and current["s_bt_err"] == 0,
                      label + ": BT working/error=0")
                check(current["advertising_acks"] > previous["advertising_acks"],
                      label + ": fresh successful controller HCI reply")
                check(current["hardware_errors"] == initial["hardware_errors"] == 0,
                      label + ": no hardware errors")
                check(current["g_h4_tx_timeout"] == initial["g_h4_tx_timeout"] == 0,
                      label + ": no UART TX timeout")
                check(current["supply_on"] == 1 and current["bt_state"] == 3,
                      label + ": shared supply retained, BT resource READY")
                check(current["bt_resets"] == initial["bt_resets"] == 1 and
                      current["wifi_resets"] == initial["wifi_resets"] == 1,
                      label + ": neither domain reset by WLAN logical switch")
                check(current["g_tx_count"] > previous["g_tx_count"] and
                      current["g_rx_count"] > previous["g_rx_count"] and
                      current["g_error_count"] == initial["g_error_count"],
                      label + ": bidirectional IPC progressing without NEW errors")
                status = command("bt_status", 1)
                age = re.search(r"age=(\d+)ms", status)
                check(age is not None and int(age[1]) < 3000 and
                      "host enabled; not a live HCI probe" in status,
                      label + ": M55 receives fresh Bluetooth status")
                return current

            read(0.5)
            initial = debug.snapshot()
            record("baseline", initial)
            check(initial["s_bt_state"] == 2 and initial["starts"] == 1,
                  "one successful BT startup")
            check(not initial["s_gatt_connected"] and not initial["s_classic_connected"],
                  "idle Bluetooth; this is NOT a connected audio/GATT stress test")
            status = command("ft_wifi")
            check("ready=1 enabled=1 busy=0" in status, "WLAN initially ready")
            guard = command("ft_radio_test")
            check("checks=12 failures=0" in guard, "12 cross-owner/invalid-argument guards")
            previous = progress("baseline-live", initial)
            try:
                for cycle in range(args.cycles):
                    command("ft_wifi off", 15, r"\[wifi\] operation=2 result=0")
                    status = wifi_status("enabled=0 busy=0 state=1 error=0")
                    check("enabled=0 busy=0 state=1 error=0" in status, "WLAN off")
                    previous = progress(f"cycle-{cycle}-off", previous)
                    rejected = command("ft_wifi scan")
                    check(bool(re.search(r"rc=-\d+ .*enabled=0 busy=0", rejected)),
                          "scan safely rejected while WLAN off")
                    previous = progress(f"cycle-{cycle}-rejected", previous)
                    command("ft_wifi on", 15, r"\[wifi\] operation=1 result=0")
                    status = wifi_status("enabled=1 busy=0 state=2 error=0")
                    check("enabled=1 busy=0 state=2 error=0" in status, "WLAN on")
                    previous = progress(f"cycle-{cycle}-on", previous)
                    command("ft_wifi scan", 25, r"\[wifi\] operation=3 result=0")
                    status = wifi_status("busy=0 state=2 error=0")
                    check("busy=0 state=2 error=0" in status, "passive scan completed")
                    count = re.search(r"networks=(\d+)", status)
                    check(count is not None and int(count[1]) > 0, "real scan results")
                    previous = progress(f"cycle-{cycle}-scan", previous)
                record("COMPLETE", {"checks": checks, "cycles": args.cycles,
                                    "joined_networks": 0, "halted_cores": 0})
            finally:
                # Best effort; do not replace a useful failure with cleanup error.
                try:
                    command("ft_wifi on", 5)
                except (OSError, RuntimeError) as error:
                    record("restore-warning", str(error))
    finally:
        debug.close()


if __name__ == "__main__":
    main()
