"""Validate both HTTP/XML and RTSP/binary /info from the actual firmware."""
import argparse,socket,plistlib,struct,itertools
p=argparse.ArgumentParser();p.add_argument('--board',required=True);p.add_argument('--bind',required=True);a=p.parse_args()
results=[]
for proto,qualified in itertools.product(('HTTP/1.1','RTSP/1.0'),(False,True)):
    with socket.create_connection((a.board,7000),timeout=5,source_address=(a.bind,0)) as s:
        payload=plistlib.dumps({'qualifier':['txtAirPlay']},fmt=plistlib.FMT_BINARY) if qualified else b''
        s.sendall(f'GET /info {proto}\r\nCSeq: 1\r\nContent-Type: application/x-apple-binary-plist\r\nContent-Length: {len(payload)}\r\n\r\n'.encode()+payload);data=b''
        while b'\r\n\r\n' not in data:
            b=s.recv(4096);assert b;data+=b;assert len(data)<8192
        h,body=data.split(b'\r\n\r\n',1)
        assert h.split(b'\r\n')[0].split()[1]==b'200'
        fields=dict(line.lower().split(b':',1) for line in h.split(b'\r\n')[1:])
        size=int(fields[b'content-length']);assert 0<size<8192
        while len(body)<size:
            b=s.recv(4096);assert b;body+=b
        info=plistlib.loads(body);assert info['vv']==2 and info['features']>2**32
        assert info['features']&(1<<40) and info['features']&(1<<20)
        assert len(info['pk'])==32
        raw=info['txtAirPlay'];pos=0;txt={}
        while pos<len(raw):
            n=raw[pos];pos+=1;assert pos+n<=len(raw)
            key,value=raw[pos:pos+n].split(b'=',1);txt[key.decode()]=value.decode();pos+=n
        lo,hi=(int(x,16) for x in txt['features'].split(','))
        assert info['features']==(hi<<32)|lo
        assert info['deviceID']==info['deviceid']==txt['deviceid']
        assert info['pk'].hex()==txt['pk'] and info['pi']==txt['pi']
        assert info['protocolVersion']==txt['protovers'] and info['sourceVersion']==txt['srcvers']
        results.append(info)
        print(f'PASS: {proto} /info qualified={qualified}; TXT, 64-bit capabilities and identity valid')
for key in ('features','vv','pk','deviceid','name','audioFormats','txtAirPlay'):
    assert all(results[0][key]==info[key] for info in results),key
print('PASS: HTTP/XML and RTSP/binary capability fields agree')
query=b''.join(bytes([len(s)])+s for s in b'_airplay._tcp.local'.split(b'.'))+b'\0'
with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:
    s.bind((a.bind,0));s.settimeout(2)
    for attempt in range(3):
        s.sendto(struct.pack('!6H',0xA104,0,1,0,0,0)+query+struct.pack('!2H',12,1),(a.board,5353))
        try:data,peer=s.recvfrom(4096);break
        except socket.timeout:
            if attempt==2:raise
    assert peer[0]==a.board
    # The application emits an uncompressed DNS response with no question section.
    assert struct.unpack_from('!H',data,4)[0]==0
    count=struct.unpack_from('!H',data,6)[0];pos=12;records=[]
    for _ in range(count):
        while data[pos]:
            n=data[pos];assert n<64;pos+=n+1
        pos+=1;kind,_,_,n=struct.unpack_from('!HHIH',data,pos);pos+=10
        if kind==16:records.append(data[pos:pos+n])
        pos+=n;assert pos<=len(data)
    assert results[0]['txtAirPlay'] in records
print('PASS: /info txtAirPlay is byte-identical to live mDNS TXT RDATA')
