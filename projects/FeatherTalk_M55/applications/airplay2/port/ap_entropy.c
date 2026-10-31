/* Prototype entropy provisioning. No fixed seeds, weak RNG, or security-policy changes.
 * Seeded by host CSPRNG through application console once; state rotates BEFORE output.
 * Physical-debug/flash rollback resistance is NOT claimed. */
#include <rtthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
extern int ft_ap_seed_receive(uint8_t out[96]);
#include "mbedtls/hmac_drbg.h"
#include "monocypher-ed25519.h"
static struct rt_mutex lock;
static mbedtls_hmac_drbg_context drbg;
static int initialized;
typedef struct { uint8_t magic[8]; uint64_t generation; uint8_t seed[64]; uint8_t digest[64]; } seed_record;
static const char *slots[2]={"/flash/.airplay2/rng0","/flash/.airplay2/rng1"};
static int entropy_init(void) { mbedtls_hmac_drbg_init(&drbg); return rt_mutex_init(&lock,"ap_rng",RT_IPC_FLAG_PRIO); }
INIT_APP_EXPORT(entropy_init);
static int read_slot(int index,seed_record *r)
{
    int fd=open(slots[index],O_RDONLY,0), n; uint8_t digest[64];
    if(fd<0) return -1;
    n=read(fd,r,sizeof(*r));close(fd);
    if(n!=sizeof(*r) || memcmp(r->magic,"AP2RNG01",8)) return -1;
    crypto_sha512(digest,(uint8_t *)r,80);
    return crypto_verify64(digest,r->digest);
}
static int write_slot(int index,seed_record *r)
{
    seed_record check; int n,fd;
    memcpy(r->magic,"AP2RNG01",8);crypto_sha512(r->digest,(uint8_t *)r,80);
    fd=open(slots[index],O_WRONLY|O_CREAT|O_TRUNC,0600);
    if(fd<0) return -1;
    n=write(fd,r,sizeof(*r)); int synced=fsync(fd); int closed=close(fd);
    if(n!=sizeof(*r) || synced || closed || read_slot(index,&check)) return -1;
    int same=memcmp(r,&check,sizeof(check));crypto_wipe(&check,sizeof(check));return same;
}
int ft_ap_drbg_random(void *out,size_t n)
{
    int result=-1;
    rt_mutex_take(&lock,RT_WAITING_FOREVER);
    if(!initialized) {
        seed_record a,b,next; int va=read_slot(0,&a)==0, vb=read_slot(1,&b)==0;
        if(!va && !vb) { rt_kprintf("[AP] secure seed provisioning required (ft_ap_seed)\n"); goto wipe; }
        seed_record *old=va && (!vb || a.generation>b.generation)?&a:&b;
        if(old->generation==UINT64_MAX) goto wipe;
        result=mbedtls_hmac_drbg_seed_buf(&drbg,mbedtls_md_info_from_type(MBEDTLS_MD_SHA512),old->seed,64);
        if(result) goto wipe;
        memset(&next,0,sizeof(next));next.generation=old->generation+1;
        result=mbedtls_hmac_drbg_random(&drbg,next.seed,64);
        if(!result) result=write_slot((int)(next.generation&1),&next);
        crypto_wipe(&next,sizeof(next));
        if(!result) initialized=1;
wipe:
        crypto_wipe(&a,sizeof(a));crypto_wipe(&b,sizeof(b));
        if(!initialized) { mbedtls_hmac_drbg_free(&drbg);mbedtls_hmac_drbg_init(&drbg);goto done; }
    }
    result=mbedtls_hmac_drbg_random(&drbg,out,n);
done:
    if(result && n) crypto_wipe(out,n);
    rt_mutex_release(&lock);return result;
}
static void ft_ap_seed(void)
{
    seed_record a,b,r;uint8_t digest[64];int result=-1;uint8_t payload[96];
    memset(payload,0,sizeof(payload));
    rt_mutex_take(&lock,RT_WAITING_FOREVER);
    if(read_slot(0,&a)==0 || read_slot(1,&b)==0 || initialized) {
        rt_kprintf("[AP] seed already provisioned; unchanged\n");goto done;
    }
    rt_kprintf("[AP] seed receiver ready\n");
    result=ft_ap_seed_receive(payload);
    if(result) {rt_kprintf("[AP] seed transfer failed=%d\n",result);goto done;}
    crypto_sha512(digest,payload,64);
    if(memcmp(payload+80,"APSEED01",8) || crypto_verify16(digest,payload+64)) {
        rt_kprintf("[AP] seed input rejected\n");goto done;
    }
    mkdir("/flash/.airplay2",0700);
    memset(&r,0,sizeof(r));r.generation=1;memcpy(r.seed,payload,64);
    result=write_slot(1,&r);
    rt_kprintf("[AP] seed provision result=%d (secret not logged)\n",result);
done:
    crypto_wipe(&a,sizeof(a));crypto_wipe(&b,sizeof(b));crypto_wipe(&r,sizeof(r));
    crypto_wipe(payload,sizeof(payload));crypto_wipe(digest,sizeof(digest));
    rt_mutex_release(&lock);
}
MSH_CMD_EXPORT(ft_ap_seed, Application seed provisioning via existing RT-Thread console API);
