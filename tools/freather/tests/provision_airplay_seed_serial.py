"""Application-only seed provisioning over USB console; no OS/driver changes.
Secret is consumed inside the running command, not entered as a shell argument.
"""
import argparse
import hashlib
import os
import re
from pathlib import Path
import sys
import time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'serial-monitor/vendor'))
import serial
p=argparse.ArgumentParser();p.add_argument('--serial',default='COM17');a=p.parse_args()
with serial.Serial(a.serial,115200,timeout=.05) as port:
    port.write(b'ft_ap_seed\r');prefix=bytearray();end=time.monotonic()+10
    while time.monotonic()<end:
        prefix.extend(port.read(port.in_waiting or 1))
        if b'already provisioned' in prefix:
            print('Application seed already provisioned; unchanged.');sys.exit(0)
        if b'seed receiver ready' in prefix:break
    else:raise TimeoutError('Application seed receiver did not start')
    secret=bytearray(os.urandom(64))
    payload=secret+hashlib.sha512(secret).digest()[:16]+b'APSEED01'+bytes(8)
    frame=bytearray(b'APSEED:'+payload.hex().encode()+b'\r')
    try:
        for offset in range(len(frame)):
            port.write(frame[offset:offset+1]);time.sleep(.02)
        result=bytearray();end=time.monotonic()+18
        while time.monotonic()<end:result.extend(port.read(port.in_waiting or 1))
        if b'seed provision result=0' not in result:
            status=re.search(rb'seed (?:transfer failed=-?\d+|input rejected|provision result=-?\d+)',result)
            if status:print(status[0].decode('ascii'))
            raise RuntimeError('Device did not confirm durable seed import')
        print('PASS: host CSPRNG seed durably imported over application console; secret omitted.')
    finally:
        secret[:]=bytes(len(secret));payload[:]=bytes(len(payload));frame[:]=bytes(len(frame))
