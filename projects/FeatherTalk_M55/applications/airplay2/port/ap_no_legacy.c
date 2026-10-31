#include "rtsp_rsa.h"
/* This port intentionally does not offer classic RAOP RSA authentication. */
int rsa_apple_challenge_response(const char *c,uint32_t ip,const uint8_t mac[6],char *out,size_t n)
{ (void)c;(void)ip;(void)mac;if(n)out[0]=0;return -1; }
int rsa_decrypt_aes_key(const char *c,uint8_t *out,size_t n,size_t *used)
{ (void)c;(void)out;(void)n;*used=0;return -1; }
