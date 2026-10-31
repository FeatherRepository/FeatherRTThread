"""Cross-check production crypto adapters against independent cryptography/OpenSSL."""
import ctypes as C
from pathlib import Path
import subprocess
import sys
root=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(root/'tools/freather/airplay-test-vendor'))
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
from cryptography.hazmat.primitives.asymmetric import ed25519,x25519
from cryptography.hazmat.primitives.serialization import Encoding,PublicFormat
src=root/'projects/FeatherTalk_M55/applications/airplay2'
dll=root/'tmp/airplay-crypto-test.dll'
subprocess.run(['gcc','-shared','-O2','-static-libgcc','-I'+str(src/'port'),'-I'+str(src/'vendor/monocypher'),
    str(src/'port/ap_crypto.c'),str(src/'vendor/monocypher/monocypher.c'),
    str(src/'vendor/monocypher/monocypher-ed25519.c'),str(Path(__file__).with_name('airplay_crypto_rng_mock.c')),
    '-o',str(dll)],check=True)
lib=C.CDLL(str(dll));ptr=C.c_void_p;ull=C.c_ulonglong
enc=lib.crypto_aead_chacha20poly1305_ietf_encrypt
enc.argtypes=[ptr,C.POINTER(ull),ptr,ull,ptr,ull,ptr,ptr,ptr]
dec=lib.crypto_aead_chacha20poly1305_ietf_decrypt
dec.argtypes=[ptr,C.POINTER(ull),ptr,ptr,ull,ptr,ull,ptr,ptr]
key=bytes(range(32));nonce=bytes(range(12));aad=b'header-authenticated'
for size in (0,1,15,16,17,352,1024):
    message=bytes(i%256 for i in range(size));cipher=C.create_string_buffer(size+16);n=ull()
    assert enc(cipher,C.byref(n),message,size,aad,len(aad),None,nonce,key)==0
    expected=ChaCha20Poly1305(key).encrypt(nonce,message,aad)
    assert cipher.raw==expected and n.value==len(expected)
    plain=C.create_string_buffer(size+1)
    assert dec(plain,C.byref(n),None,expected,len(expected),aad,len(aad),nonce,key)==0
    assert n.value==size and plain.raw[:size]==message
    bad=expected[:-1]+bytes([expected[-1]^1]);plain.raw=b'x'*(size+1)
    assert dec(plain,C.byref(n),None,bad,len(bad),aad,len(aad),nonce,key)==-1
    assert n.value==0 and plain.raw[:size]==bytes(size)
pk=C.create_string_buffer(32);sk=C.create_string_buffer(64);sig=C.create_string_buffer(64);length=ull()
lib.crypto_sign_keypair(pk,sk)
expected=ed25519.Ed25519PrivateKey.from_private_bytes(bytes(range(32)))
assert pk.raw==expected.public_key().public_bytes(Encoding.Raw,PublicFormat.Raw)
lib.crypto_sign_detached.argtypes=[ptr,C.POINTER(ull),ptr,ull,ptr]
assert lib.crypto_sign_detached(sig,C.byref(length),b'paired-device',13,sk)==0
expected.public_key().verify(sig.raw,b'paired-device')
lib.crypto_box_keypair(pk,sk)
expected=x25519.X25519PrivateKey.from_private_bytes(bytes(range(32)))
assert pk.raw==expected.public_key().public_bytes(Encoding.Raw,PublicFormat.Raw)
print('PASS: ChaCha20-Poly1305 cross-check, tag rejection/wipe, Ed25519 and X25519 interoperability')
