"""Bounded host-to-board raw TCP test for the SDK netutils iperf server.

Not iperf3. Requires an already connected board. Sends synthetic bytes only,
does not change AP credentials, host routing/firewall or storage. Always stops
the board's test server. Optional UI benchmark runs during the transfer.
"""
import argparse
import importlib.util
import ipaddress
import json
from pathlib import Path
import socket
import time

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    "isolation", Path(__file__).with_name("test-wifi-bluetooth-isolation.py"))
isolation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(isolation)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--board", required=True, type=ipaddress.IPv4Address)
    parser.add_argument("--seconds", type=int, choices=range(6, 31), default=12)
    parser.add_argument("--port", type=int, default=5001)
    parser.add_argument("--ui-bench", action="store_true")
    parser.add_argument("--debug-port", type=int)
    parser.add_argument("--log", type=Path, default=ROOT / "tmp/wifi-tcp.jsonl")
    args = parser.parse_args()
    args.log.parent.mkdir(parents=True, exist_ok=True)
    with args.log.open("w", encoding="utf8") as log, \
            isolation.serial.Serial(args.serial, 115200, timeout=.05) as port:
        def record(kind, value):
            line = json.dumps({"kind": kind, "data": value}, ensure_ascii=False)
            print(line, flush=True)
            log.write(line + "\n"); log.flush()

        def read(seconds):
            data = bytearray()
            end = time.monotonic() + seconds
            while time.monotonic() < end:
                data.extend(port.read(port.in_waiting or 1))
            text = data.decode("utf8", "replace")
            if text:
                record("serial", text)
            return text

        def command(text, seconds=1):
            port.write((text + "\r").encode("ascii"))
            return read(seconds)

        debug = None
        started = False
        try:
            state = command("ft_wifi")
            if "busy=0 state=6 error=0" not in state or "ip=" + str(args.board) + " " not in state:
                raise RuntimeError("Board must already be connected with the specified DHCP address")
            if args.debug_port:
                debug = isolation.DebugReader(args.debug_port, "cat1d.cm33",
                                              ROOT / "projects/FeatherTalk_M33/rt-thread.elf")
                baseline = debug.snapshot()
                record("bt-before", baseline)
            response = command("iperf -s -p " + str(args.port))
            if "Please stop iperf firstly" in response:
                raise RuntimeError("An existing iperf test is running; it was not modified")
            started = True
            payload = bytes(range(256)) * 128
            count = 0
            with socket.create_connection((str(args.board), args.port), timeout=5) as sock:
                sock.settimeout(3)
                if args.ui_bench:
                    port.write(b"feather_ui_bench\r")
                begin = time.monotonic()
                while time.monotonic() - begin < args.seconds and count < 32 * 1024 * 1024:
                    sock.sendall(payload)
                    count += len(payload)
                    if port.in_waiting:
                        read(.001)
                sock.shutdown(socket.SHUT_WR)
                # Peer EOF is emitted after its recv loop consumes our stream.
                if sock.recv(1) != b"":
                    raise RuntimeError("Unexpected payload from SDK receive-only server")
                elapsed = time.monotonic() - begin
            record("tcp-host-to-board", {"bytes": count, "seconds": round(elapsed, 3),
                                         "mbps": round(count * 8 / elapsed / 1e6, 3)})
            command("iperf --stop", 4)
            started = False
            final = command("ft_wifi")
            if "busy=0 state=6 error=0" not in final:
                raise RuntimeError("Wi-Fi no longer connected after TCP transfer")
            command("feather_ui_status")
            if debug:
                current = debug.snapshot()
                record("bt-after", current)
                if not (current["command_completes"] > baseline["command_completes"] and
                        current["s_bt_state"] == 2 and current["s_bt_err"] == 0 and
                        all(current[key] == baseline[key] for key in
                            ("hardware_errors", "g_h4_tx_timeout", "g_error_count", "bt_resets", "wifi_resets"))):
                    raise RuntimeError("Bluetooth health changed during TCP test")
            record("COMPLETE", "bounded receive test; not maximum throughput or full-duplex qualification")
        finally:
            if started:
                command("iperf --stop", 4)
            if debug:
                debug.close()


if __name__ == "__main__":
    main()
