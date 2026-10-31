"""Independent FFmpeg AAC encoder -> production FAAD2/RT-Thread decoder adapter.
No device changes; generates a quiet sine fixture and verifies nonzero bounded PCM.
"""
from pathlib import Path
import concurrent.futures,ctypes,math,struct,subprocess,sys
from fractions import Fraction
root=Path(__file__).resolve().parents[3];app=root/'projects/FeatherTalk_M55/applications/airplay2'
sys.path.insert(0,str(root/'tools/freather/airplay-test-vendor'))
import av
work=root/'tmp/airplay-aac-host';work.mkdir(exist_ok=True,parents=True)
inc=['-I'+str(app/x) for x in ['vendor/faad2/include','vendor/faad2/libfaad','vendor/alac','vendor/esp/audio','vendor/esp','port']]
inc+=['-I'+str(Path(__file__).parent/'airplay_stubs')]
flags=['-O2','-Wno-multichar','-DFT_AP_WITH_AAC','-DLC_ONLY_DECODER','-DDISABLE_SBR','-DHAVE_INTTYPES_H=1','-DHAVE_STRING_H=1','-DHAVE_STRINGS_H=1','-DHAVE_MEMCPY=1','-DHAVE_LRINTF=1','-DPACKAGE_VERSION="test"']
sources=list((app/'vendor/faad2/libfaad').glob('*.c'))+list((app/'vendor/alac').glob('*.c'))+list((app/'vendor/alac').glob('*.cpp'))+[app/'port/ap_decoder.cpp',Path(__file__).with_name('airplay_aac_bridge.cpp')]
def compile_source(item):
    index,source=item;obj=work/f'{index}.o'
    subprocess.run(['g++' if source.suffix=='.cpp' else 'gcc',*flags,*inc,'-c',str(source),'-o',str(obj)],check=True,capture_output=True)
    return str(obj)
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:objects=list(pool.map(compile_source,enumerate(sources)))
dll=work/'aac_test.dll';subprocess.run(['g++','-shared','-static-libgcc','-static-libstdc++',*objects,'-o',str(dll)],check=True)
lib=ctypes.CDLL(str(dll));lib.aac_test_create.argtypes=[ctypes.c_int];lib.aac_test_create.restype=ctypes.c_void_p
lib.aac_test_decode.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p,ctypes.c_uint]
lib.aac_test_close.argtypes=[ctypes.c_void_p]
for rate in (44100,48000):
    enc=av.CodecContext.create('aac','w');enc.sample_rate=rate;enc.layout='stereo';enc.format='fltp';enc.bit_rate=128000;enc.open()
    packets=[]
    for block in range(64):
        frame=av.AudioFrame(format='fltp',layout='stereo',samples=1024);frame.sample_rate=rate;frame.pts=block*1024;frame.time_base=Fraction(1,rate)
        samples=struct.pack('<1024f',*(.05*math.sin(2*math.pi*1000*(block*1024+i)/rate) for i in range(1024)))
        for plane in frame.planes:plane.update(samples)
        packets.extend(bytes(p) for p in enc.encode(frame))
    packets.extend(bytes(p) for p in enc.encode(None))
    d=lib.aac_test_create(rate);assert d
    out=(ctypes.c_short*(4096*2))();count=0;energy=0
    try:
        assert lib.aac_test_decode(d,packets[0],len(packets[0]),out,100)==-1
        for p in packets:
            n=lib.aac_test_decode(d,p,len(p),out,4096);assert 0<=n<=1024,n
            count+=n;energy+=sum(int(out[i])**2 for i in range(n*2))
        assert count>=63*1024 and energy>1000000,(count,energy)
    finally:lib.aac_test_close(d)
    (root/f'tmp/airplay-aac-{rate}.bin').write_bytes(b''.join(struct.pack('<H',len(p))+p for p in packets))
    print(f'PASS AAC-LC {rate}Hz stereo: {len(packets)} raw packets, {count} PCM frames, nonzero samples, undersized output rejected')
assert not lib.aac_test_create(96000)
