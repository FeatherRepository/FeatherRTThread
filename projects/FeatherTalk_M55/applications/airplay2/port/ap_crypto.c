/* Sodium API subset backed by unmodified Monocypher primitives. */
#include "sodium.h"
#include "esp_random.h"
#include <string.h>
int sodium_init(void) { uint8_t seed[32]; int r=ft_ap_random(seed,sizeof(seed)); crypto_wipe(seed,sizeof(seed)); return r; }
int crypto_sign_keypair(uint8_t *pk,uint8_t *sk)
{ uint8_t seed[32]; if(ft_ap_random(seed,32)) return -1; crypto_ed25519_key_pair(sk,pk,seed); crypto_wipe(seed,32); return 0; }
int crypto_box_keypair(uint8_t *pk,uint8_t *sk)
{ if(ft_ap_random(sk,32)) return -1; crypto_x25519_public_key(pk,sk); return 0; }
int crypto_scalarmult(uint8_t *q,const uint8_t *n,const uint8_t *p)
{ uint8_t zero[32]={0}; crypto_x25519(q,n,p); return crypto_verify32(q,zero)==0 ? -1 : 0; }
int crypto_sign_detached(uint8_t *sig,unsigned long long *len,const uint8_t *m,unsigned long long n,const uint8_t *sk)
{ if(n>SIZE_MAX) return -1; crypto_ed25519_sign(sig,sk,m,(size_t)n); if(len) *len=64; return 0; }
int crypto_aead_chacha20poly1305_ietf_encrypt(uint8_t *out,unsigned long long *outlen,const uint8_t *in,unsigned long long n,const uint8_t *ad,unsigned long long adlen,const uint8_t *secret,const uint8_t *nonce,const uint8_t *key)
{
    crypto_aead_ctx ctx; (void)secret;
    if(outlen) *outlen=0;
    if(n>SIZE_MAX-16 || adlen>SIZE_MAX) return -1;
    crypto_aead_init_ietf(&ctx,key,nonce);
    crypto_aead_write(&ctx,out,out+(size_t)n,ad,(size_t)adlen,in,(size_t)n);
    crypto_wipe(&ctx,sizeof(ctx)); if(outlen) *outlen=n+16; return 0;
}
int crypto_aead_chacha20poly1305_ietf_decrypt(uint8_t *out,unsigned long long *outlen,uint8_t *secret,const uint8_t *in,unsigned long long n,const uint8_t *ad,unsigned long long adlen,const uint8_t *nonce,const uint8_t *key)
{
    crypto_aead_ctx ctx; int result; (void)secret;
    if(outlen) *outlen=0;
    if(n<16 || n>SIZE_MAX || adlen>SIZE_MAX) return -1;
    crypto_aead_init_ietf(&ctx,key,nonce);
    result=crypto_aead_read(&ctx,out,in+(size_t)n-16,ad,(size_t)adlen,in,(size_t)n-16);
    crypto_wipe(&ctx,sizeof(ctx));
    if(result) { crypto_wipe(out,(size_t)n-16); return -1; }
    if(outlen) *outlen=n-16; return 0;
}
