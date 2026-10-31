#include <rtthread.h>
#include <string.h>
#include "audio_output.h"
#include "audio_receiver.h"
#include "audio_resample.h"
#include "rtsp_server.h"
#include "ft_airplay_sink.h"
static volatile int running,stop_requested,source_rate=44100,flush_requested;
volatile uint32_t ap_pcm_frames,ap_pcm_write_errors,ap_pcm_underflows;
static void playback(void *arg)
{
    int16_t *input=rt_calloc(1024*2,sizeof(int16_t)),*output=rt_calloc(2048*2,sizeof(int16_t));
    int opened=0,rate=0;(void)arg;
    if(!input || !output) { ++ap_pcm_write_errors;goto done; }
    while(!stop_requested) {
        if(!audio_receiver_is_playing()) {
            if(opened) { if(ft_ap_sink_close()==RT_EOK)opened=0; }
            rt_thread_mdelay(10);continue;
        }
        if(!opened) {
            if(ft_ap_sink_open(48000,16,2)!=RT_EOK) { rt_thread_mdelay(20);continue; }
            opened=1;
        }
        if(rate!=source_rate) {
            rate=source_rate;
            if(!audio_resample_init(rate,48000,2)) { ++ap_pcm_write_errors;break; }
        }
        if(flush_requested) { flush_requested=0;audio_resample_reset(); }
        size_t frames=audio_receiver_read(input,353),out_frames;
        int16_t *pcm=input;
        if(frames) {
            out_frames=frames;
            if(audio_resample_is_active()) { out_frames=audio_resample_process(input,frames,output,2048);pcm=output; }
            int32_t gain=airplay_get_volume_q15(); if(gain<0)gain=0;if(gain>32768)gain=32768;
            for(size_t i=0;i<out_frames*2;++i) pcm[i]=(int16_t)(((int32_t)pcm[i]*gain)>>15);
        } else {
            ++ap_pcm_underflows;out_frames=480;pcm=output;memset(output,0,out_frames*4);
        }
        size_t bytes=out_frames*4,offset=0;
        while(offset<bytes && !stop_requested) {
            size_t chunk=bytes-offset; if(chunk>4096)chunk=4096;
            int n=ft_ap_sink_write((uint8_t *)pcm+offset,chunk);
            if(n<=0 || (unsigned)n>chunk || n%4) { ++ap_pcm_write_errors;stop_requested=1;break; }
            offset+=(size_t)n;
        }
        if(frames) ap_pcm_frames+=(uint32_t)(offset/4);
    }
done:
    if(opened) ft_ap_sink_close();
    if(input)rt_free(input);if(output)rt_free(output);
    audio_resample_destroy();running=0;
}
esp_err_t audio_output_init(void) { return ESP_OK; }
void audio_output_start(void)
{
    if(running)return;
    stop_requested=0;running=1;
    rt_thread_t thread=rt_thread_create("ap_play",playback,NULL,16384,12,10);
    if(thread)rt_thread_startup(thread);else{running=0;++ap_pcm_write_errors;}
}
void audio_output_stop(void) { stop_requested=1;for(int i=0;i<200 && running;++i)rt_thread_mdelay(10); }
void audio_output_flush(void) { flush_requested=1; }
esp_err_t audio_output_write(const void *data,size_t bytes,TickType_t wait)
{ (void)wait;return ft_ap_sink_write(data,bytes)==(int)bytes?ESP_OK:ESP_FAIL; }
void audio_output_set_sample_rate(uint32_t rate) { (void)rate; /* Physical output stays 48kHz. */ }
void audio_output_set_source_rate(int rate) { source_rate=rate; }
uint32_t audio_output_get_hardware_latency_us(void)
{ return 4096U*1000000U/(48000U*4U); /* sound0 double buffer, model not measured cursor */ }
