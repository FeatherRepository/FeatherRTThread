#include <rtthread.h>
#include "sodium.h"
#include "esp_random.h"
#include "srp.h"
static void crypto_probe_worker(void *unused)
{
    uint8_t scratch[32],digest[64]; (void)unused;
    crypto_hash_sha512(digest,(const uint8_t *)"abc",3);
    if(digest[0]!=0xdd || digest[63]!=0x9f) { rt_kprintf("[AP] SHA512 FAILED\n"); return; }
    int result=ft_ap_random(scratch,32); crypto_wipe(scratch,32);
    rt_kprintf("[AP] entropy result=%d (bytes not logged)\n",result);
    if(result) return;
    srp_session_t *s=srp_session_create();
    if(!s) return;
    rt_tick_t start=rt_tick_get();
    result=srp_start(s,"Pair-Setup","3939");
    rt_kprintf("[AP] SRP3072 result=%d duration=%u ticks\n",result,(unsigned)(rt_tick_get()-start));
    srp_session_free(s);
}
static void ft_ap_crypto(void)
{
    rt_thread_t thread=rt_thread_create("ap_crypto",crypto_probe_worker,NULL,16384,18,10);
    if(thread) rt_thread_startup(thread);
}
MSH_CMD_EXPORT(ft_ap_crypto, Verify crypto primitives and SE entropy without printing secrets);
