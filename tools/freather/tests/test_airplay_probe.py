"""Exercise only the explicit development probe; NOT an AirPlay playback test."""
import argparse
import ipaddress
import re
import socket
import sys
import time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "serial-monitor/vendor"))
import serial
p = argparse.ArgumentParser()
p.add_argument("--board", type=ipaddress.IPv4Address, required=True)
p.add_argument("--bind", type=ipaddress.IPv4Address, required=True)
p.add_argument("--serial", default="COM17")
a = p.parse_args()
with serial.Serial(a.serial, 115200, timeout=.05) as port:
    def command(text, seconds=1):
        port.write((text+"\r").encode("ascii"))
        data = bytearray(); end = time.monotonic()+seconds
        while time.monotonic() < end:
            data.extend(port.read(port.in_waiting or 1))
        result = data.decode("utf8", "replace")
        print(result, flush=True)
        return result
    def connect():
        conn = socket.create_connection((str(a.board), 7000), timeout=2,
                                        source_address=(str(a.bind), 0))
        conn.settimeout(2)
        return conn
    def replies(conn, count):
        data = bytearray()
        while data.count(b"\r\n\r\n") < count:
            block = conn.recv(4096)
            if not block: break
            data.extend(block)
        return bytes(data)
    wifi = command("ft_wifi")
    assert "busy=0 state=6 error=0" in wifi and "ip="+str(a.board)+" " in wifi, "Recheck board DHCP address"
    status = command("ft_airplay")
    assert "playable=0 advertised=0 probe=0" in status, "Existing probe was not modified"
    owner = re.search(r"audio-owner=(\d+)", status)[1]
    try:
        command("ft_airplay probe")
        with connect() as conn:
            packet = b"OPTIONS * RTSP/1.0\r\ncSeQ: 41\r\nContent-Length: 3\r\n\r\n\0\1x"
            conn.sendall(packet[:17]); time.sleep(.05)
            conn.sendall(packet[17:]+b"OPTIONS * RTSP/1.0\r\nCSeq: 42\r\n\r\n")
            result = replies(conn, 2)
            assert result.count(b"200 OK") == 2 and b"CSeq: 41" in result and b"CSeq: 42" in result
        with connect() as conn:
            conn.sendall(b"POST /pair-setup RTSP/1.0\r\nCSeq: 43\r\nContent-Length: 3\r\n\r\n\6\1\1")
            assert b"501 Not Implemented" in replies(conn, 1)
        with connect() as conn:
            conn.sendall(b"POST /pair-setup RTSP/1.0\r\nCSeq: 44\r\nContent-Length: -1\r\n\r\n")
            assert b"400 Bad Request" in replies(conn, 1)
        print("PASS: board RTSP fragmentation/pipeline, unsupported pairing rejected, malformed length rejected")
        command("list thread", .5)
    finally:
        command("ft_airplay stop", 2)
        status = command("ft_airplay")
        assert "playable=0 advertised=0 probe=0" in status
        assert re.search(r"audio-owner=(\d+)", status)[1] == owner
        print("PASS: stopped; audio owner unchanged; no discovery or playback claimed")
