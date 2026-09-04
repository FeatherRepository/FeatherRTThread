"""Authorized STA association/DHCP/ping regression; credentials stay in memory.

Supply --network SSID:CHANNEL for each authorized AP. Password comes from a
hidden prompt or stdin, never argv, a file, or the JSONL log. RX is buffered
before redaction, including serial command echo across read boundaries.
The entire join echo is discarded through its following status marker, so
asynchronous firmware logs cannot expose fragments inserted into that echo.
Optional OpenOCD reads the running M33 to verify live Bluetooth HCI progress.
Does not configure the host network, format storage, pair, or reset a core.
"""
import argparse
import getpass
import importlib.util
import json
from pathlib import Path
import re
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    "isolation", Path(__file__).with_name("test-wifi-bluetooth-isolation.py"))
isolation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(isolation)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--network", action="append", required=True)
    parser.add_argument("--country", required=True)
    parser.add_argument("--cycles", type=int, choices=range(1, 6), default=2)
    parser.add_argument("--password-stdin", action="store_true")
    parser.add_argument("--debug-port", type=int)
    parser.add_argument("--log", type=Path, default=ROOT / "tmp/wifi-ap.jsonl")
    args = parser.parse_args()
    networks = []
    for network in args.network:
        ssid, channel = network.rsplit(":", 1)
        if not re.fullmatch(r"[\w.-]{1,32}", ssid, flags=re.ASCII):
            parser.error("This shell test supports simple ASCII SSIDs only")
        networks.append((ssid, int(channel)))
    password = sys.stdin.readline().rstrip("\r\n") if args.password_stdin else getpass.getpass("AP password: ")
    if not re.fullmatch(r"[A-Za-z0-9._@!+-]{8,63}", password):
        parser.error("Use the board UI for keys requiring shell quoting")
    debug = None
    checks = 0
    args.log.parent.mkdir(parents=True, exist_ok=True)
    try:
        if args.debug_port:
            debug = isolation.DebugReader(args.debug_port, "cat1d.cm33",
                                          ROOT / "projects/FeatherTalk_M33/rt-thread.elf")
        with args.log.open("w", encoding="utf8") as log, \
                isolation.serial.Serial(args.serial, 115200, timeout=.1) as port:
            pending_rx = ""
            awaiting_join_status = False
            def record(kind, data):
                # Redact BEFORE encoding, output, or persistent logging.
                safe = json.dumps(data, ensure_ascii=False).replace(password, "<redacted>")
                line = json.dumps({"kind": kind, "data": json.loads(safe)}, ensure_ascii=False)
                print(line, flush=True)
                log.write(line + "\n"); log.flush()

            def read(seconds):
                data = bytearray()
                end = time.monotonic() + seconds
                while time.monotonic() < end:
                    data.extend(port.read(port.in_waiting or 1))
                return data.decode("utf8", "replace")

            def command(command, seconds=1):
                nonlocal pending_rx, awaiting_join_status
                if command.startswith("ft_wifi join "):
                    awaiting_join_status = True
                port.write((command + "\r").encode("ascii"))
                response = read(seconds)
                pending_rx += response
                if awaiting_join_status:
                    # The status header is emitted only after MSH has parsed
                    # the complete join command. Discard its echo, including
                    # fragments split by unrelated asynchronous log lines.
                    marker = pending_rx.find("[wifi] rc=")
                    if marker < 0:
                        return response
                    pending_rx = pending_rx[marker:]
                    awaiting_join_status = False
                # Never emit a partial echoed key, even across timed reads.
                boundary = max(pending_rx.rfind("\n"), pending_rx.rfind("\r"))
                if boundary >= 0:
                    record("serial", pending_rx[:boundary + 1])
                    pending_rx = pending_rx[boundary + 1:]
                return response

            def check(condition, message):
                nonlocal checks
                record("PASS" if condition else "FAIL", message)
                if not condition:
                    raise RuntimeError(message)
                checks += 1

            def state_wait(predicate, seconds=50, fail_on_error=False):
                end = time.monotonic() + seconds
                while time.monotonic() < end:
                    response = command("ft_wifi", 1.5)
                    if fail_on_error and "busy=0 state=7" in response:
                        error = re.search(r"\berror=(-?\d+)", response)
                        raise RuntimeError("Wi-Fi operation failed: " + (error[1] if error else "unknown"))
                    if predicate(response):
                        return response
                raise TimeoutError("Wi-Fi state deadline exceeded; see redacted serial log")

            def idle(response):
                return "busy=0" in response

            read(.4)
            baseline = command("ft_wifi")
            check("country=" + args.country in baseline, "correct country configuration")
            check("ready=1 enabled=1 busy=0" in baseline, "radio initialized and idle")
            if debug:
                start = debug.snapshot()
                record("bt-baseline", start)
                check(start["s_bt_state"] == 2, "Bluetooth running before AP tests")
            for cycle in range(args.cycles):
                if cycle:
                    command("ft_wifi off")
                    off = state_wait(idle, 20, fail_on_error=True)
                    check("enabled=0 busy=0 state=1 error=0" in off and
                          "ssid= ip= gw=" in off, "connected Wi-Fi OFF clears association and address")
                    rejected = command("ft_wifi scan")
                    check(bool(re.search(r"rc=-\d+ .*enabled=0 busy=0", rejected)), "scan rejected while OFF")
                    command("ft_wifi on")
                    on = state_wait(idle, 20, fail_on_error=True)
                    check("enabled=1 busy=0 state=2 error=0" in on, "Wi-Fi ON returns idle without auto-join")
                for ssid, channel in networks:
                    record("phase", {"cycle": cycle + 1, "ssid": ssid, "channel": channel})
                    command("ft_wifi disconnect")
                    state_wait(idle, 15)
                    visible = False
                    # Passive scanning can miss a beacon. Retry only scanning,
                    # never silently retry a failed authentication or ping.
                    for attempt in range(1, 4):
                        command("ft_wifi scan")
                        scan = state_wait(idle, 30)
                        visible = bool(re.search(r"\d+: " + re.escape(ssid) + r" ch=" + str(channel) + r"\b", scan))
                        record("scan-attempt", {"attempt": attempt, "visible": visible})
                        if visible:
                            break
                    check(visible, "authorized SSID/channel visible")
                    before = time.monotonic()
                    command("ft_wifi join " + ssid + " " + password)
                    status = state_wait(lambda s: "busy=0 state=6 error=0" in s and "ssid=" + ssid + " ip=" in s,
                                        fail_on_error=True)
                    record("connection-seconds", round(time.monotonic() - before, 2))
                    ip = re.search(r"\bip=([\d.]+) gw=([\d.]+)", status)
                    check(ip is not None and ip[1] != "0.0.0.0" and ip[2] != "0.0.0.0", "DHCP address and gateway ready")
                    info = command("wifi status")
                    check("Channel: " + str(channel) in info, "WLAN connected information matches band")
                    command("ifconfig")
                    command("ft_wifi scan")
                    scanned = state_wait(idle, 30, fail_on_error=True)
                    check("state=6 error=0" in scanned and "ssid=" + ssid + " ip=" in scanned,
                          "passive scan while connected preserves association and DHCP")
                    ping = command("ping " + ip[2], 9)
                    replies = re.findall(r"time=(\d+) ms", ping)
                    check(len(replies) == 4, "gateway ICMP 4/4 replies")
                    record("gateway-ping-ms", [int(x) for x in replies])
                    command("bt_status")
                    command("ft_radio")
                    if debug:
                        current = debug.snapshot()
                        record("bt-current", current)
                        check(current["command_completes"] > start["command_completes"], "live Bluetooth HCI progresses")
                        check(current["s_bt_state"] == 2 and current["s_bt_err"] == 0 and
                              current["hardware_errors"] == start["hardware_errors"] and
                              current["g_h4_tx_timeout"] == start["g_h4_tx_timeout"], "Bluetooth remains healthy")
                        check(current["bt_resets"] == start["bt_resets"] and
                              current["wifi_resets"] == start["wifi_resets"], "no controller resets during AP switching")
                        start = current
            command("feather_ui_scene 11")
            command("feather_ui_status")
            record("COMPLETE", {"checks": checks, "cycles": args.cycles, "networks": len(networks)})
    finally:
        if debug:
            debug.close()
        password = ""


if __name__ == "__main__":
    main()
