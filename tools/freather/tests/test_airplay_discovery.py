"""Check real DNS-SD response and TCP listener; explicit board/interface only."""
import argparse
import socket
import struct
p=argparse.ArgumentParser();p.add_argument('--board',required=True);p.add_argument('--bind',required=True);a=p.parse_args()
query=b''.join(bytes([len(s)])+s for s in b'_airplay._tcp.local'.split(b'.'))+b'\0'
with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sock:
    sock.bind((a.bind,0));sock.settimeout(4)
    sock.sendto(struct.pack('!6H',0xA102,0,1,0,0,0)+query+struct.pack('!2H',12,1),(a.board,5353))
    data,peer=sock.recvfrom(4096)
    assert peer[0]==a.board and data[:2]==b'\xa1\x02' and data[2]&0x80
    assert b'FeatherTalk AirPlay' in data and b'vv=2' in data
    assert socket.inet_aton(a.board) in data
    print('PASS: live AirPlay 2 DNS-SD response, device name and IPv4 address')
with socket.create_connection((a.board,7000),timeout=4,source_address=(a.bind,0)) as sock:
    sock.sendall(b'OPTIONS * RTSP/1.0\r\nCSeq: 1\r\nContent-Length: 0\r\n\r\n')
    assert sock.recv(2048).startswith(b'RTSP/1.0 200 ')
    print('PASS: RTSP listener responds without manually enabling AirPlay')
