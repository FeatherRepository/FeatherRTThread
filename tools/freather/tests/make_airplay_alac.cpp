#include <cstdio>
#include <cstring>
#include <cmath>
#include "ALACEncoder.h"
extern "C" {
#include "audio_decoder.h"
void ap_log(const char *,const char *,const char *format,...) { std::fprintf(stderr,"decoder: %s\n",format); }
}
int main(int argc,char **argv)
{
    if(argc!=2)return 1;
    AudioFormatDescription input={},output={};
    input.mSampleRate=44100;input.mFormatID='lpcm';input.mFormatFlags=12;
    input.mBytesPerPacket=4;input.mFramesPerPacket=1;input.mBytesPerFrame=4;input.mChannelsPerFrame=2;input.mBitsPerChannel=16;
    output.mSampleRate=44100;output.mFormatID='alac';output.mFormatFlags=1;output.mFramesPerPacket=352;output.mChannelsPerFrame=2;
    ALACEncoder encoder;encoder.SetFrameSize(352);if(encoder.InitializeEncoder(output))return 2;
    audio_decoder_config_t config={};strcpy(config.format.codec,"ALAC");config.format.sample_rate=44100;
    config.format.channels=2;config.format.bits_per_sample=16;config.format.frame_size=352;config.format.max_samples_per_frame=352;
    audio_decoder_t *decoder=audio_decoder_create(&config);if(!decoder)return 3;
    FILE *f=fopen(argv[1],"wb");if(!f)return 4;
    for(unsigned frame=0;frame<512;++frame){
        int16_t pcm[704],decoded[8192];unsigned char packet[8192];
        for(unsigned i=0;i<352;++i)pcm[i*2]=pcm[i*2+1]=(int16_t)(2048*sin(6.283185307179586*1000*(frame*352+i)/44100));
        int32_t bytes=sizeof(pcm);if(encoder.Encode(input,output,(unsigned char *)pcm,packet,&bytes))return 5;
        audio_decode_info_t info;int samples=audio_decoder_decode(decoder,packet,bytes,decoded,4096,&info);
        if(samples!=352 || info.channels!=2 || memcmp(pcm,decoded,sizeof(pcm)))return 6;
        unsigned char length[2]={(unsigned char)bytes,(unsigned char)(bytes>>8)};
        if(fwrite(length,1,2,f)!=2 || fwrite(packet,1,bytes,f)!=(unsigned)bytes)return 7;
    }
    fclose(f);audio_decoder_destroy(decoder);
    puts("PASS: 512 compressed ALAC packets roundtrip through production decoder adapter, exact stereo PCM");
    return 0;
}
