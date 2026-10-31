"""Native AP2 encrypted ALAC smoke sender. Quiet test tone, explicit target only.
Confirm device decoded/PCM/error counters separately; sending is not proof of sound.
"""
import argparse
import os
import plistlib
import socket
import struct
import time
from test_airplay_pairing import Client, ROOT, ChaCha20Poly1305

p=argparse.ArgumentParser()
p.add_argument('--board',required=True);p.add_argument('--bind',required=True)
p.add_argument('--codec',choices=['alac','aac'],default='alac')
p.add_argument('--buffered',action='store_true')
a=p.parse_args()
c=Client(a.board,a.bind)
udp=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);udp.bind((a.bind,0))
event=None;media=None
def request(method,obj):
    status,_,body=c.request(method,'/stream',plistlib.dumps(obj,fmt=plistlib.FMT_BINARY),'application/x-apple-binary-plist')
    assert status==200, (method,status)
    return plistlib.loads(body) if body else {}
try:
    c.pair()
    initial=request('SETUP',{'timingProtocol':'PTP','name':'Local validation'})
    assert initial['eventPort']>0
    event=socket.create_connection((a.board,initial['eventPort']),timeout=5,source_address=(a.bind,0))
    key=os.urandom(32)
    aac=a.codec=='aac';spf=1024 if aac else 352
    reply=request('SETUP',{'streams':[{'type':103 if a.buffered else 96,'ct':4 if aac else 2,'sr':44100,'spf':spf,
        'audioFormat':1<<(22 if aac else 18),'shk':key,'controlPort':udp.getsockname()[1]}]})
    port=reply['streams'][0]['dataPort']
    if a.buffered:media=socket.create_connection((a.board,port),timeout=5,source_address=(a.bind,0))
    if media:print('PASS: control, event and buffered-audio TCP connections coexist',flush=True)
    status,_,_=c.request('SET_PARAMETER','/stream',b'volume: -12.0\r\n','text/parameters');assert status==200
    status,_,_=c.request('RECORD','/stream');assert status==200
    packets=(ROOT/('tmp/airplay-aac-44100.bin' if aac else 'tmp/airplay-alac-packets.bin')).read_bytes()
    if aac:packets*=4
    cipher=ChaCha20Poly1305(key);pos=0;seq=0;start=time.monotonic()
    while pos<len(packets):
        size=struct.unpack_from('<H',packets,pos)[0];pos+=2
        payload=packets[pos:pos+size];pos+=size;assert len(payload)==size
        header=struct.pack('!BBHII',0x80,0x60,seq&65535,seq*spf,0x41503201)
        tail=seq.to_bytes(8,'little')
        data=header+cipher.encrypt(bytes(4)+tail,payload,header[4:12])+tail
        delay=start+seq*spf/44100-time.monotonic()
        if delay>0:time.sleep(delay)
        if media:media.sendall(struct.pack('!H',len(data)+2)+data)
        else:udp.sendto(data,(a.board,port))
        seq+=1
    time.sleep(1)
    print(f'SENT: {seq} encrypted {a.codec.upper()} packets; check device PCM counters, not just packet transmission.')
finally:
    if c.encrypted:
        try:c.request('TEARDOWN','/stream')
        except (OSError,EOFError):pass
    if media:media.close()
    if event:event.close()
    c.sock.close();udp.close()
