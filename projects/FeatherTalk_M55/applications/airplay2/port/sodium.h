#pragma once
#include <stddef.h>
#include <stdint.h>
#include "monocypher.h"
#include "monocypher-ed25519.h"
typedef crypto_sha512_ctx crypto_hash_sha512_state;
typedef crypto_sha512_hmac_ctx crypto_auth_hmacsha512_state;
#define crypto_auth_hmacsha512_BYTES 64
#define crypto_sign_BYTES 64
#define crypto_aead_chacha20poly1305_ietf_ABYTES 16
#define sodium_base64_VARIANT_ORIGINAL 1
#define sodium_base64_VARIANT_ORIGINAL_NO_PADDING 3
int sodium_base642bin(uint8_t *,size_t,const char *,size_t,const char *,size_t *,const char **,int);
#define sodium_memzero crypto_wipe
static inline int crypto_hash_sha512_init(crypto_hash_sha512_state *s) { crypto_sha512_init(s); return 0; }
static inline int crypto_hash_sha512_update(crypto_hash_sha512_state *s, const uint8_t *m, size_t n) { crypto_sha512_update(s,m,n); return 0; }
static inline int crypto_hash_sha512_final(crypto_hash_sha512_state *s, uint8_t *h) { crypto_sha512_final(s,h); return 0; }
static inline int crypto_hash_sha512(uint8_t *h, const uint8_t *m, size_t n) { crypto_sha512(h,m,n); return 0; }
static inline int crypto_auth_hmacsha512_init(crypto_auth_hmacsha512_state *s,const uint8_t *k,size_t n) { crypto_sha512_hmac_init(s,k,n); return 0; }
static inline int crypto_auth_hmacsha512_update(crypto_auth_hmacsha512_state *s,const uint8_t *m,size_t n) { crypto_sha512_hmac_update(s,m,n); return 0; }
static inline int crypto_auth_hmacsha512_final(crypto_auth_hmacsha512_state *s,uint8_t *h) { crypto_sha512_hmac_final(s,h); return 0; }
int sodium_init(void);
int crypto_sign_keypair(uint8_t *pk, uint8_t *sk);
int crypto_box_keypair(uint8_t *pk, uint8_t *sk);
int crypto_scalarmult(uint8_t *q, const uint8_t *n, const uint8_t *p);
int crypto_sign_detached(uint8_t *sig, unsigned long long *len, const uint8_t *m, unsigned long long n, const uint8_t *sk);
int crypto_aead_chacha20poly1305_ietf_encrypt(uint8_t *out, unsigned long long *outlen, const uint8_t *in, unsigned long long n, const uint8_t *ad, unsigned long long adlen, const uint8_t *secret, const uint8_t *nonce, const uint8_t *key);
int crypto_aead_chacha20poly1305_ietf_decrypt(uint8_t *out, unsigned long long *outlen, uint8_t *secret, const uint8_t *in, unsigned long long n, const uint8_t *ad, unsigned long long adlen, const uint8_t *nonce, const uint8_t *key);
