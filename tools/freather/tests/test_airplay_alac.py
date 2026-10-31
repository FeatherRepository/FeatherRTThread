"""Build an independent host encoder and exercise the production decoder adapter."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[3]
app=root/'projects/FeatherTalk_M55/applications/airplay2'
codec=root.parent/'airplay2-resources/alac/codec'
work=root/'tmp/airplay-alac-host';work.mkdir(parents=True,exist_ok=True)
objects=[]
for name in ['ag_enc.c','ag_dec.c','dp_enc.c','dp_dec.c','matrix_enc.c','matrix_dec.c','ALACBitUtilities.c','EndianPortable.c']:
    obj=work/(name+'.o');objects.append(str(obj))
    source=app/'vendor/alac'/name if name=='EndianPortable.c' else codec/name
    subprocess.run(['gcc','-O2','-I'+str(codec),'-c',str(source),'-o',str(obj)],check=True)
exe=work/'encode.exe'
subprocess.run(['g++','-O2','-Wno-multichar','-static-libgcc','-static-libstdc++','-Drt_kprintf=printf','-include','stdio.h',
    '-I'+str(codec),'-I'+str(app/'port'),'-I'+str(app/'vendor/esp'),'-I'+str(app/'vendor/esp/audio'),
    '-I'+str(Path(__file__).parent/'airplay_stubs'),str(codec/'ALACEncoder.cpp'),str(codec/'ALACDecoder.cpp'),
    str(app/'port/ap_decoder.cpp'),str(Path(__file__).with_name('make_airplay_alac.cpp')),*objects,'-o',str(exe)],check=True)
subprocess.run([str(exe),str(root/'tmp/airplay-alac-packets.bin')],check=True)
