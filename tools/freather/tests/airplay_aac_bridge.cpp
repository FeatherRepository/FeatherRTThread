#include <cstring>
extern "C" {
#include "audio_decoder.h"
void ap_log(const char *,const char *,const char *,...) {}
__declspec(dllexport) void *aac_test_create(int rate) {
    audio_decoder_config_t c={};std::strcpy(c.format.codec,"AAC");
    c.format.sample_rate=rate;c.format.channels=2;c.format.bits_per_sample=16;
    c.format.frame_size=c.format.max_samples_per_frame=1024;
    return audio_decoder_create(&c);
}
__declspec(dllexport) int aac_test_decode(void *d,const unsigned char *p,unsigned n,short *out,unsigned cap) {
    audio_decode_info_t info={};return audio_decoder_decode((audio_decoder_t *)d,p,n,out,cap,&info);
}
__declspec(dllexport) void aac_test_close(void *d) { audio_decoder_destroy((audio_decoder_t *)d); }
}
