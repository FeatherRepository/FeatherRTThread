"""Bounded SDK iperf TCP uplink sink bound to an explicit wired IPv4."""
import argparse
import json
import select
import socket
import sys
import time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "serial-monitor/vendor"))
import serial
p=argparse.ArgumentParser()
p.add_argument("--bind", required=True)
p.add_argument("--board", required=True)
p.add_argument("--serial", default="COM17")
p.add_argument("--seconds", type=int, default=15)
p.add_argument("--port", type=int, default=5001)
a=p.parse_args()
with socket.socket() as server, serial.Serial(a.serial,115200,timeout=.05) as port:
    server.bind((a.bind,a.port)); server.listen(1); server.settimeout(12)
    port.write(f"iperf -c {a.bind} -p {a.port}\r".encode())
    try:
        conn,peer=server.accept()
        with conn:
            if peer[0]!=a.board: raise RuntimeError("Unexpected peer")
            print("PEER",peer,"LOCAL",conn.getsockname(),flush=True)
            begin=time.monotonic(); count=0
            while time.monotonic()-begin<a.seconds:
                if select.select([conn],[],[],.05)[0]:
                    data=conn.recv(65536)
                    if not data: break
                    count+=len(data)
                if port.in_waiting:
                    print(port.read(port.in_waiting).decode("utf8","replace"),end="",flush=True)
            elapsed=time.monotonic()-begin
            print(json.dumps({"direction":"board-to-wired-host","bytes":count,
                              "seconds":round(elapsed,3),"mbps":round(count*8/elapsed/1e6,3)}),flush=True)
    finally:
        port.write(b"iperf --stop\r")
        end=time.monotonic()+3
        while time.monotonic()<end:
            print(port.read(port.in_waiting or 1).decode("utf8","replace"),end="",flush=True)
