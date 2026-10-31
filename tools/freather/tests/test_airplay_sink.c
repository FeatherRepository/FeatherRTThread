#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rtdevice.h"
#include "ft_airplay_sink.h"
#include "feathertalk_audio.h"
struct test_device { int unused; } device;
static ft_audio_output_owner_t owner;
static int fail_open, fail_close, formats, writes;
int ap_test_init(void);
int rt_mutex_init(struct rt_mutex *m, const char *n, int p) { (void)n; (void)p; m->locked=0; return 0; }
int rt_mutex_take(struct rt_mutex *m, int t) { (void)t; assert(!m->locked); m->locked=1; return 0; }
int rt_mutex_release(struct rt_mutex *m) { assert(m->locked); m->locked=0; return 0; }
rt_device_t rt_device_find(const char *n) { assert(!strcmp(n,"sound0")); return &device; }
int rt_device_open(rt_device_t d, int f) { (void)d; (void)f; return fail_open ? -RT_EIO : 0; }
int rt_device_close(rt_device_t d) { (void)d; return fail_close ? -RT_EIO : 0; }
size_t rt_device_write(rt_device_t d, int p, const void *b, size_t n)
{ (void)d; (void)p; (void)b; ++writes; return n; }
bool ft_audio_output_format_supported(uint32_t r, uint8_t b, uint8_t c)
{ return r==48000 && b==16 && c==2; }
int ft_audio_set_output_format(uint32_t r, uint8_t b, uint8_t c)
{ assert(ft_audio_output_format_supported(r,b,c)); ++formats; return 0; }
int ft_audio_claim_output(ft_audio_output_owner_t v)
{ if (owner && owner!=v) return -RT_EBUSY; owner=v; return 0; }
void ft_audio_release_output(ft_audio_output_owner_t v) { if (owner==v) owner=0; }
int main(void)
{
    char pcm[16]={0};
    assert(!ap_test_init());
    owner=FT_AUDIO_OUTPUT_OWNER_BT_LE_AUDIO;
    assert(ft_ap_sink_open(48000,16,2)==-RT_EBUSY && !formats);
    assert(!ft_ap_sink_close() && owner==FT_AUDIO_OUTPUT_OWNER_BT_LE_AUDIO);
    owner=0;
    assert(ft_ap_sink_open(44100,16,2)==-RT_EINVAL && !formats);
    fail_open=1;
    assert(ft_ap_sink_open(48000,16,2)==-RT_EIO && owner==0);
    fail_open=0;
    assert(!ft_ap_sink_open(48000,16,2) && owner==FT_AUDIO_OUTPUT_OWNER_AIRPLAY2);
    assert(ft_ap_sink_open(48000,16,2)==-RT_EBUSY);
    assert(ft_ap_sink_write(pcm,3)==-RT_EINVAL && !writes);
    assert(ft_ap_sink_write(pcm,sizeof(pcm))==sizeof(pcm) && writes==1);
    fail_close=1;
    assert(ft_ap_sink_close()==-RT_EIO && owner==FT_AUDIO_OUTPUT_OWNER_AIRPLAY2);
    fail_close=0;
    assert(!ft_ap_sink_close() && owner==0);
    assert(ft_ap_sink_write(pcm,sizeof(pcm))==-RT_ERROR);
    puts("PASS: PCM adapter format checks, LE Audio busy rejection, failure cleanup, frame alignment");
    return 0;
}
