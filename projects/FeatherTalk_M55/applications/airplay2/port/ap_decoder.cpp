#include <new>
#include <stdlib.h>
#include <string.h>
#include "ALACDecoder.h"
#include "ALACBitUtilities.h"
extern "C" {
#include "audio_decoder.h"
#include "esp_log.h"
#include "alac_magic_cookie.h"
#ifdef FT_AP_WITH_AAC
#include "neaacdec.h"
#endif
}
struct audio_decoder { audio_format_t format; ALACDecoder *alac; uint8_t *scratch;
#ifdef FT_AP_WITH_AAC
    NeAACDecHandle aac;
#endif
};
extern "C" audio_decoder_t *audio_decoder_create(const audio_decoder_config_t *c)
{
    if(!c || c->format.channels<1 || c->format.channels>2 || c->format.frame_size<1 || c->format.frame_size>4096 ||
       c->format.max_samples_per_frame>4096 || (c->format.bits_per_sample!=16 && c->format.bits_per_sample!=24)) return NULL;
    audio_decoder_t *d=(audio_decoder_t *)calloc(1,sizeof(*d)); if(!d) return NULL;
    d->format=c->format;
    if(!strcmp(d->format.codec,"ALAC") || !strcmp(d->format.codec,"AppleLossless")) {
        uint8_t cookie[24];
        /* ALACSpecificConfig, big-endian; source parameters from SETUP. */
        memset(cookie,0,sizeof(cookie));
        uint32_t frames=d->format.max_samples_per_frame?d->format.max_samples_per_frame:d->format.frame_size;
        cookie[0]=frames>>24;cookie[1]=frames>>16;cookie[2]=frames>>8;cookie[3]=frames;
        cookie[5]=d->format.bits_per_sample;cookie[6]=40;cookie[7]=10;cookie[8]=14;cookie[9]=d->format.channels;
        cookie[10]=0;cookie[11]=255;
        uint32_t rate=d->format.sample_rate;
        cookie[20]=rate>>24;cookie[21]=rate>>16;cookie[22]=rate>>8;cookie[23]=rate;
        void *object=malloc(sizeof(ALACDecoder));
        if(object) d->alac=new(object) ALACDecoder;
        if(!d->alac || d->alac->Init(cookie,sizeof(cookie))) { audio_decoder_destroy(d);return NULL; }
        d->scratch=(uint8_t *)malloc(frames*d->format.channels*4+64);
        if(!d->scratch) { audio_decoder_destroy(d);return NULL; }
    }
#ifdef FT_AP_WITH_AAC
    else if(!strcmp(d->format.codec,"AAC")) {
        if(d->format.sample_rate!=44100 && d->format.sample_rate!=48000){audio_decoder_destroy(d);return NULL;}
        d->aac=NeAACDecOpen();if(!d->aac){audio_decoder_destroy(d);return NULL;}
        NeAACDecConfigurationPtr config=NeAACDecGetCurrentConfiguration(d->aac);
        config->outputFormat=FAAD_FMT_16BIT;config->defObjectType=LC;
        config->defSampleRate=d->format.sample_rate;config->dontUpSampleImplicitSBR=1;
        if(!NeAACDecSetConfiguration(d->aac,config)){audio_decoder_destroy(d);return NULL;}
        unsigned index=d->format.sample_rate==44100?4:3;
        uint8_t asc[2]={(uint8_t)((2<<3)|(index>>1)),(uint8_t)(((index&1)<<7)|(d->format.channels<<3))};
        unsigned long rate=0;unsigned char channels=0;
        if(NeAACDecInit2(d->aac,asc,sizeof(asc),&rate,&channels) || rate!=(unsigned)d->format.sample_rate || channels!=d->format.channels){audio_decoder_destroy(d);return NULL;}
    }
#endif
    else if(strcmp(d->format.codec,"PCM") && strcmp(d->format.codec,"L16")) {
        ESP_LOGE("decoder","Unsupported codec: %s",d->format.codec);free(d);return NULL;
    }
    return d;
}
extern "C" void audio_decoder_destroy(audio_decoder_t *d)
{ if(d) {
#ifdef FT_AP_WITH_AAC
    if(d->aac)NeAACDecClose(d->aac);
#endif
    if(d->alac) { d->alac->~ALACDecoder();free(d->alac); }free(d->scratch);free(d); } }
extern "C" bool audio_decoder_is_aac(const audio_decoder_t *d) {
#ifdef FT_AP_WITH_AAC
    return d && d->aac;
#else
    (void)d;return false;
#endif
}
extern "C" bool audio_decoder_is_alac(const audio_decoder_t *d) { return d && d->alac; }
extern "C" int audio_decoder_decode(audio_decoder_t *d,const uint8_t *in,size_t n,int16_t *out,size_t cap,audio_decode_info_t *info)
{
    if(!d || !in || !out || !info || n>16384) return -1;
    info->channels=d->format.channels;
#ifdef FT_AP_WITH_AAC
    if(d->aac){
        if(!n || cap<1024)return -1;
        NeAACDecFrameInfo frame={};
        void *pcm=NeAACDecDecode(d->aac,&frame,(unsigned char *)in,(unsigned long)n);
        if(frame.error || frame.bytesconsumed!=n || frame.channels!=d->format.channels ||
           frame.samplerate!=(unsigned)d->format.sample_rate || frame.samples%frame.channels ||
           frame.samples/frame.channels>cap || (frame.samples && !pcm))return -1;
        if(frame.samples)memcpy(out,pcm,frame.samples*sizeof(int16_t));
        return (int)(frame.samples/frame.channels);
    }
#endif
    if(d->alac) {
        uint32_t frames=0, max=d->alac->mConfig.frameLength;
        if(cap<max) return -1;
        BitBuffer bits;BitBufferInit(&bits,(uint8_t *)in,(uint32_t)n);
        if(d->alac->Decode(&bits,d->scratch,max,d->format.channels,&frames)) return -1;
        size_t samples=frames*d->format.channels;
        if(frames>cap) return -1;
        if(d->format.bits_per_sample==16) memcpy(out,d->scratch,samples*2);
        else if(d->format.bits_per_sample==24) for(size_t i=0;i<samples;++i) out[i]=(int16_t)((uint16_t)d->scratch[i*3+1]|((uint16_t)d->scratch[i*3+2]<<8));
        else return -1;
        return (int)frames;
    }
    if(n%(2*d->format.channels) || n/(2*d->format.channels)>cap) return -1;
    for(size_t i=0;i<n/2;++i) out[i]=(int16_t)((in[i*2]<<8)|in[i*2+1]);
    return (int)(n/(2*d->format.channels));
}
