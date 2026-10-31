#include "hap.h"
#include "srp.h"
#include "sodium.h"
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
static uint8_t pk[32],sk[64];
static int initialized;
esp_err_t hap_init(void)
{
    int fd, n; uint8_t saved[96];
    if(initialized) return ESP_OK;
    if(sodium_init()<0) return ESP_FAIL;
    fd=open("/flash/.airplay2.key",O_RDONLY,0);
    if(fd>=0) {
        n=read(fd,saved,sizeof(saved)); close(fd);
        if(n!=sizeof(saved)) return ESP_FAIL;
        uint8_t seed[32],check_sk[64],check_pk[32];
        memcpy(seed,saved+32,32); crypto_ed25519_key_pair(check_sk,check_pk,seed);
        if(crypto_verify32(check_pk,saved) || crypto_verify64(check_sk,saved+32)) {
            crypto_wipe(saved,sizeof(saved)); crypto_wipe(check_sk,64); return ESP_FAIL;
        }
        crypto_wipe(check_sk,64); memcpy(pk,saved,32); memcpy(sk,saved+32,64);
    } else {
        if(crypto_sign_keypair(pk,sk)) return ESP_FAIL;
        memcpy(saved,pk,32); memcpy(saved+32,sk,64);
        fd=open("/flash/.airplay2.key",O_WRONLY|O_CREAT|O_EXCL,0600);
        if(fd<0) { crypto_wipe(sk,64); crypto_wipe(saved,96); return ESP_FAIL; }
        n=write(fd,saved,sizeof(saved)); close(fd);
        if(n!=sizeof(saved)) { crypto_wipe(sk,64); crypto_wipe(saved,96); return ESP_FAIL; }
    }
    crypto_wipe(saved,sizeof(saved)); initialized=1; return ESP_OK;
}
const uint8_t *hap_get_public_key(void) { return pk; }
hap_session_t *hap_session_create(void)
{
    if(!initialized) return NULL;
    hap_session_t *s=calloc(1,sizeof(*s)); if(!s) return NULL;
    memcpy(s->device_public_key,pk,32); memcpy(s->device_secret_key,sk,64);
    if(crypto_box_keypair(s->session_public_key,s->session_secret_key)) { crypto_wipe(s,sizeof(*s)); free(s); return NULL; }
    return s;
}
void hap_session_free(hap_session_t *s)
{ if(s) { if(s->srp) srp_session_free(s->srp); crypto_wipe(s,sizeof(*s)); free(s); } }
