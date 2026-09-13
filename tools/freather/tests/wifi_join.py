"""Join an explicitly named AP; never log the command or its password echo."""
import argparse
import getpass
import re
import sys
import time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "serial-monitor/vendor"))
import serial
p = argparse.ArgumentParser()
p.add_argument("--ssid", required=True)
p.add_argument("--serial", default="COM17")
p.add_argument("--sdk", action="store_true")
p.add_argument("--password-stdin", action="store_true", help="Read credential from redirected stdin")
a = p.parse_args()
key = sys.stdin.readline().rstrip("\r\n") if a.password_stdin else getpass.getpass("AP password: ")
if not a.ssid.replace("_","").replace("-","").isalnum() or not key.isalnum():
    raise ValueError("This console helper accepts alphanumeric credentials only")
with serial.Serial(a.serial, 115200, timeout=.1) as port:
    port.write((("wifi join " if a.sdk else "ft_wifi join ") + a.ssid + " " + key + "\r").encode())
    # Discard all join echo, including asynchronous interleaving.
    end = time.monotonic() + 4
    initial = bytearray()
    while time.monotonic() < end:
        initial.extend(port.read(port.in_waiting or 1))
    # Retain only SDK status lines, never the shell command/credential echo.
    for line in initial.decode("utf8", "replace").splitlines():
        if "join" not in line and ("WLAN.mgnt" in line or "W/whd" in line or "E/whd" in line):
            print(line.replace(key, "<redacted>"), flush=True)
    for attempt in range(25):
        port.write(b"ifconfig\r" if a.sdk else b"ft_wifi\r")
        response = bytearray()
        end = time.monotonic()+1
        while time.monotonic() < end:
            response.extend(port.read(port.in_waiting or 1))
        text = response.decode("utf8","replace").replace(key,"<redacted>")
        print(text, flush=True)
        if (a.sdk and re.search(r"ip address:\s*192\.168\.0\.[1-9][0-9]*", text)) or (not a.sdk and "busy=0 state=6 error=0" in text):
            break
        if "busy=0 state=7" in text:
            raise RuntimeError("Wi-Fi join failed")
    else:
        raise TimeoutError("No connected state")
