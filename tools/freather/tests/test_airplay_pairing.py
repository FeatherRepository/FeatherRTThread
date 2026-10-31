"""Independent native AirPlay 2 transient pairing/encrypted-RTSP test client.
Does not log credentials or claim audio playback. Run against the explicit board IP.
"""
import argparse
import hashlib
import hmac
import ipaddress
import plistlib
import re
import secrets
import socket
import struct
import sys
import time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tools/freather/airplay-test-vendor'))
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
H=lambda b:hashlib.sha512(b).digest()
def integer(n):return n.to_bytes(max(1,(n.bit_length()+7)//8),'big')
def tlv(t,value):
    return b''.join(bytes((t,len(value[i:i+255])))+value[i:i+255] for i in range(0,len(value),255)) if value else bytes((t,0))
def untlv(data):
    out={};pos=0
    while pos<len(data):
        assert pos+2<=len(data);t,n=data[pos:pos+2];pos+=2
        assert n<=len(data)-pos
        out[t]=out.get(t,b'')+data[pos:pos+n];pos+=n
    return out
class Client:
    def __init__(self,board,bind):
        self.sock=socket.create_connection((str(board),7000),timeout=10,source_address=(str(bind),0))
        self.sock.settimeout(30);self.cseq=0;self.encrypted=False;self.tx=0;self.rx=0;self.buffer=bytearray()
    def exact(self,n):
        out=bytearray()
        while len(out)<n:
            b=self.sock.recv(n-len(out))
            if not b:raise EOFError('RTSP connection closed')
            out.extend(b)
        return bytes(out)
    def receive(self):
        while True:
            split=self.buffer.find(b'\r\n\r\n')
            if split>=0:
                head=bytes(self.buffer[:split]);fields={}
                for line in head.split(b'\r\n')[1:]:
                    key,value=line.split(b':',1);fields[key.lower()]=value.strip()
                n=int(fields.get(b'content-length',b'0'))
                if len(self.buffer)>=split+4+n:
                    body=bytes(self.buffer[split+4:split+4+n]);del self.buffer[:split+4+n]
                    return int(head.split(b' ')[1]),fields,body
            if self.encrypted:
                length=self.exact(2);n=int.from_bytes(length,'little');assert n<=1024
                block=self.exact(n+16)
                plain=self.read_cipher.decrypt(bytes(4)+self.rx.to_bytes(8,'little'),block,length)
                self.rx+=1;self.buffer.extend(plain)
            else:self.buffer.extend(self.exact(1))
    def request(self,method,path,body=b'',content_type='application/octet-stream'):
        self.cseq+=1
        packet=(f'{method} {path} RTSP/1.0\r\nCSeq: {self.cseq}\r\nContent-Type: {content_type}\r\nContent-Length: {len(body)}\r\n\r\n').encode()+body
        if self.encrypted:
            for pos in range(0,len(packet),1024):
                part=packet[pos:pos+1024];size=struct.pack('<H',len(part))
                encrypted=self.write_cipher.encrypt(bytes(4)+self.tx.to_bytes(8,'little'),part,size)
                self.tx+=1;self.sock.sendall(size+encrypted)
        else:self.sock.sendall(packet)
        return self.receive()
    def pair(self):
        begin=time.monotonic()
        status,_,body=self.request('POST','/pair-setup',tlv(6,b'\1')+tlv(0,b'\0')+tlv(19,b'\x10'),'application/pairing+tlv8')
        assert status==200;response=untlv(body);assert response[6]==b'\2' and 7 not in response
        salt=response[2];B=int.from_bytes(response[3],'big')
        source=(ROOT/'projects/FeatherTalk_M55/applications/airplay2/vendor/esp/hap/srp.c').read_text()
        prime=re.search(r'static const uint8_t srp_N\[\] = \{(.*?)\};',source,re.S)[1]
        N_bytes=bytes(int(x,16) for x in re.findall(r'0x([0-9A-Fa-f]{2})',prime));N=int.from_bytes(N_bytes,'big');g=5
        a=secrets.randbits(256);A=pow(g,a,N);width=len(N_bytes)
        pad=lambda x:x.to_bytes(width,'big')
        k=int.from_bytes(H(N_bytes+pad(g)),'big');u=int.from_bytes(H(pad(A)+pad(B)),'big')
        x=int.from_bytes(H(salt+H(b'Pair-Setup:3939')),'big')
        S=pow((B-k*pow(g,x,N))%N,a+u*x,N);K=H(integer(S))
        proof=H(bytes(x^y for x,y in zip(H(N_bytes),H(bytes((g,)))))+H(b'Pair-Setup')+integer(int.from_bytes(salt,'big'))+integer(A)+integer(B)+K)
        status,_,body=self.request('POST','/pair-setup',tlv(6,b'\3')+tlv(3,pad(A))+tlv(4,proof),'application/pairing+tlv8')
        response=untlv(body);assert status==200 and 7 not in response and response[6]==b'\4'
        assert hmac.compare_digest(response[4],H(integer(A)+proof+K))
        prk=hmac.new(b'Control-Salt',K,hashlib.sha512).digest()
        derive=lambda info:hmac.new(prk,info+b'\1',hashlib.sha512).digest()[:32]
        self.write_cipher=ChaCha20Poly1305(derive(b'Control-Write-Encryption-Key'))
        self.read_cipher=ChaCha20Poly1305(derive(b'Control-Read-Encryption-Key'))
        self.encrypted=True
        print(f'PASS: native SRP3072 transient pairing, verified server proof ({time.monotonic()-begin:.3f}s)')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--board',type=ipaddress.IPv4Address,required=True);p.add_argument('--bind',type=ipaddress.IPv4Address,required=True);args=p.parse_args()
    c=Client(args.board,args.bind)
    try:
        status,_,_=c.request('OPTIONS','*');assert status==200
        for _ in range(2):
            notification=plistlib.dumps({'type':0,'params':{}},fmt=plistlib.FMT_BINARY)
            status,_,_=c.request('POST','/command',notification,'application/x-apple-binary-plist');assert status==200
        print('PASS: OPTIONS and two pre-pair /command notifications acknowledged')
        status,_,_=c.request('SETUP','/stream');assert status==401
        status,_,_=c.request('RECORD','/stream');assert status==401
        status,_,_=c.request('POST','/command-extra');assert status==401
        status,_,_=c.request('GET','/command');assert status==401
        print('PASS: media, other routes and other methods still require authentication')
        c.pair()
        status,_,body=c.request('GET','/info');assert status==200
        info=plistlib.loads(body);assert info['vv']==2
        print('PASS: encrypted RTSP response authenticated; binary /info is AirPlay 2')
    finally:c.sock.close()
