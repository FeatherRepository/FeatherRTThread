"""Dual-core independent on/off regression. No AP join, pairing or core halt.

Flash matching M33/M55, attach OpenOCD without reset, then use --serial COM17.
Requires idle Bluetooth. Restores both radios ON at exit (best effort).
"""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import sys
import time
import subprocess

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("isolation", Path(__file__).with_name("test-wifi-bluetooth-isolation.py"))
isolation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(isolation)
serial = isolation.serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--cycles", type=int, default=3, choices=range(1, 11))
    parser.add_argument("--log", type=Path, default=ROOT / "tmp/radio-lifecycle.jsonl")
    args = parser.parse_args()
    args.log.parent.mkdir(parents=True, exist_ok=True)
    debug = isolation.DebugReader(6666, "cat1d.cm33", ROOT / "projects/FeatherTalk_M33/rt-thread.elf")
    checks = 0
    with args.log.open("w", encoding="utf8") as log, serial.Serial(args.serial, 115200, timeout=.1) as port:
        def record(kind, data):
            line = json.dumps({"kind": kind, "data": data})
            print(line, flush=True); log.write(line + "\n"); log.flush()

        def check(test, name):
            nonlocal checks
            record("PASS" if test else "FAIL", name)
            if not test: raise AssertionError(name)
            checks += 1

        def read(seconds, pattern=None):
            data = ""; until = time.monotonic() + seconds
            while time.monotonic() < until:
                data += port.read(port.in_waiting or 1).decode("utf8", "replace")
                if pattern and re.search(pattern, data): return data
            if pattern: raise TimeoutError(pattern)
            return data

        def command(command, seconds=.7, pattern=None):
            port.write((command + "\r").encode("ascii"))
            return read(seconds, pattern)

        def snap():
            state = debug.snapshot()
            state["worker"] = debug.words("s_loop_thread")[0]
            state["pending"] = debug.words("s_request_pending")[0]
            # symbol is a uint32_t, not a target-size enum or bool.
            state["uart_baud"] = debug.words("s_cur_baud")[0]
            counters = debug.words("g_bt_coex_diag", 7)
            state["stops"], state["stack_inits"] = counters[5:7]
            return state

        def invariant(state, name):
            record(name, state)
            check(state["worker"] == initial["worker"], name + ": single persistent BT worker")
            check(state["stack_inits"] == initial["stack_inits"] == 1, name + ": profiles/handlers initialized once")
            check(state["supply_on"] == 1, name + ": shared supply retained")
            check(state["wifi_resets"] == initial["wifi_resets"], name + ": WLAN never reset")
            check(state["hardware_errors"] == initial["hardware_errors"] == 0, name + ": no HCI hardware error")
            check(state["g_h4_tx_timeout"] == initial["g_h4_tx_timeout"] == 0, name + ": no UART TX timeout")
            check(state["g_error_count"] == initial["g_error_count"], name + ": no new IPC errors")
            check(state["s_bt_err"] == 0, name + ": no BT service error")

        def wait_bt(on, timeout=35):
            until = time.monotonic() + timeout
            while time.monotonic() < until:
                state = snap()
                if state["s_bt_state"] == (2 if on else 0) and not state["pending"]:
                    status = command("bt_status")
                    if re.search(r"enabled\s*:\s*" + ("yes" if on else "no"), status) and "result=ok" in status:
                        check(state["bt_state"] == (3 if on else 0), "BT resource state matches service")
                        check(state["uart_baud"] == (3000000 if on else 0), "UART 3M when on, deinitialized when off")
                        return state
                read(.2)
            record("timeout-snapshot", snap())
            raise TimeoutError("Bluetooth did not settle: " + str(on))

        def bt(on):
            command("bt_on" if on else "bt_off", .2)
            return wait_bt(on)

        def wifi(on):
            command("ft_wifi on" if on else "ft_wifi off", 20, r"\[wifi\] operation=[12] result=0")
            status = command("ft_wifi", 1)
            check(("enabled=%d busy=0" % on) in status, "Wi-Fi target applied")

        def scan():
            command("ft_wifi scan", 25, r"\[wifi\] operation=3 result=0")
            status = command("ft_wifi", 1)
            count = re.search(r"networks=(\d+)", status)
            check(count is not None and int(count[1]) > 0, "Wi-Fi real passive scan results")

        def live_bt(previous):
            until = time.monotonic() + 7
            while time.monotonic() < until:
                state = snap()
                if state["advertising_acks"] > previous["advertising_acks"]: return state
                read(.3)
            raise AssertionError("Bluetooth HCI replies stalled")

        read(.5)
        initial = snap(); record("baseline", initial)
        check(initial["s_bt_state"] == 2 and not initial["s_gatt_connected"] and
              not initial["s_classic_connected"], "idle BT ready (no connected-peer test)")
        check("ready=1 enabled=1 busy=0" in command("ft_wifi"), "Wi-Fi ready")
        try:
            for cycle in range(args.cycles):
                off = bt(False)
                invariant(off, f"cycle-{cycle}-BT-off-WLAN-on")
                scan()
                quiet = snap()
                check(quiet["command_completes"] == off["command_completes"], "BT remains silent during WLAN scan")
                check(bt(False)["bt_resets"] == off["bt_resets"], "repeated BT off is idempotent")
                wifi(False)
                invariant(snap(), f"cycle-{cycle}-both-off")
                on = bt(True)
                check(on["bt_resets"] == off["bt_resets"] + 1, "one BT reset per new start")
                check(on["wifi_state"] == 4, "BT start preserves WLAN OFF")
                check(bt(True)["bt_resets"] == on["bt_resets"], "repeated BT on is idempotent")
                wifi(True); scan()
                active = live_bt(on)
                check(active["bt_resets"] == on["bt_resets"], "WLAN enable/scan does not reset BT")
                invariant(active, f"cycle-{cycle}-both-on")

            bt(False)
            command("bt_on", .7)  # Cancel while the raw HCD loader owns the UART.
            check(snap()["s_bt_state"] == 1, "BT start in progress before cancellation")
            cancelled = bt(False)
            invariant(cancelled, "cancel-during-HCD")
            scan()
            restored = bt(True)
            invariant(restored, "restart-after-cancel")
            before = bt(False)
            command("bt_on", .3)
            check(snap()["s_bt_state"] == 1, "BT starts asynchronously")
            command("bt_on", .2)
            scan()  # WHD operations progress while M33 downloads HCD.
            concurrent = wait_bt(True)
            check(concurrent["bt_resets"] == before["bt_resets"] + 1, "duplicate on during HCD does not restart")
            invariant(concurrent, "WLAN-scan-during-BT-start")
            for request in range(16):
                command("bt_off" if request % 2 == 0 else "bt_on", .04)
            invariant(bt(False), "rapid-alternation-final-off")
            invariant(bt(True), "rapid-alternation-final-on")

            # Read the actual LVGL label/state, not just the IPC model. Resolve
            # member offsets from the matching ELF; no hardcoded UI ABI/size.
            elf = ROOT / "projects/FeatherTalk_M55/rt-thread.elf"
            gdb = isolation.TOOLS / "arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi/bin/arm-none-eabi-gdb.exe"
            expressions = ("(unsigned long)&((lv_label_t*)0)->text",
                           "(unsigned long)&((lv_obj_t*)0)->state", "(unsigned long)LV_STATE_DISABLED")
            layout = subprocess.check_output([str(gdb), "-nx", "-batch", str(elf)] +
                       [arg for expr in expressions for arg in ("-ex", "p /d " + expr)], text=True)
            label_offset, state_offset, disabled_mask = map(int, re.findall(r"\$\d+ = (\d+)", layout))
            ui = isolation.DebugReader(6666, "cat1d.cm55", elf)
            try:
                def ui_state():
                    label = ui.words("s_settings_radio_status")[0]
                    button = ui.words("s_settings_radio_button")[0]
                    check(label != 0 and button != 0, "Bluetooth settings objects exist")
                    pointer = ui.words_at(label + label_offset, 1)[0]
                    value = bytearray()
                    for offset in range(0, 160, 8):
                        chunk = bytes(int(word, 0) for word in ui.command(
                            f"cat1d.cm55 read_memory 0x{pointer + offset:x} 8 8").split())
                        value.extend(chunk.split(b"\0", 1)[0])
                        if b"\0" in chunk: break
                    state = int(ui.command(f"cat1d.cm55 read_memory 0x{button + state_offset:x} 16 1"), 0)
                    return value.decode("utf8"), bool(state & disabled_mask)

                command("feather_ui_scene 12", 2)
                label, disabled = ui_state()
                check(("开启" in label or "Radio: On" in label) and not disabled, "settings shows enabled radio")
                bt(False); read(1)
                label, disabled = ui_state()
                check(("关闭" in label or "Radio: Off" in label) and not disabled, "settings refreshes OFF without reopening")
                command("bt_on", 1)
                label, disabled = ui_state()
                check(("处理中" in label or "Working..." in label) and disabled, "settings shows pending and disables only its action")
                wait_bt(True); read(1)
                label, disabled = ui_state()
                check(("开启" in label or "Radio: On" in label) and not disabled, "settings refreshes READY and re-enables action")
                command("feather_ui_scene 0", 1)
                check(ui.words("s_settings_radio_status")[0] == 0 and ui.words("s_settings_radio_button")[0] == 0,
                      "leaving settings releases tracked UI objects")
            finally:
                ui.close()
            check(restored["g_tx_count"] > initial["g_tx_count"] and restored["g_rx_count"] > initial["g_rx_count"],
                  "bidirectional IPC remains live through all transitions")
            record("COMPLETE", {"checks": checks, "cycles": args.cycles, "joined_networks": 0, "halted_cores": 0})
        finally:
            try:
                command("ft_wifi on", 5)
                command("bt_on", 1)
            finally:
                debug.close()


if __name__ == "__main__":
    main()
