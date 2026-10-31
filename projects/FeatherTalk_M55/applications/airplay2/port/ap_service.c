#include <rtthread.h>
#include <string.h>
#include "hap.h"
#include "rtsp_server.h"
#include "audio_receiver.h"
#include "audio_output.h"
#include "wifi.h"
#include "ptp_clock.h"
#include "ntp_clock.h"
extern int ft_ap_discovery(int enable);
extern volatile uint32_t ap_pcm_frames,ap_pcm_write_errors,ap_pcm_underflows;
static volatile int service_state,service_error;
/* One application worker owns all lifecycle calls. Console commands only change
 * intent, so disconnect, manual off and startup cannot race teardown. */
static volatile int service_enabled=1;
static void stop_service(void)
{
    service_state=3;
    ft_ap_discovery(0);rtsp_server_stop();audio_receiver_stop();
    audio_output_stop();ptp_clock_stop();ntp_clock_stop();
    service_state=0;
}
static int start_service(void)
{
    service_state=1;
    int r=hap_init();
    if(!r)r=audio_receiver_init();
    if(!r)audio_receiver_set_playing(false); /* Discovery must not claim sound0. */
    if(!r)r=audio_output_init();
    if(!r)r=rtsp_server_start();
    if(!r)audio_output_start();
    if(!r)r=ft_ap_discovery(1);
    service_error=r;
    if(r)stop_service();
    else service_state=2;
    rt_kprintf("[AP] service state=%d error=%d\n",service_state,service_error);
    return r;
}
static void service_worker(void *unused)
{
    char active_ip[48]={0},ip[48];int retry_ticks=0;
    (void)unused;
    rt_kprintf("[AP] default enabled; waiting for Wi-Fi/IP\n");
    for(;;){
        int ready=wifi_is_connected() && wifi_get_ip_str(ip,sizeof(ip))==0;
        if(service_state==2 && (!service_enabled || !ready || strcmp(ip,active_ip))){
            stop_service();active_ip[0]=0;retry_ticks=0;
        }
        if(!service_enabled || !ready)retry_ticks=0;
        else if(service_state==0 && retry_ticks==0){
            if(start_service()==0){rt_strncpy(active_ip,ip,sizeof(active_ip)-1);}
            else retry_ticks=60; /* Retry safely after provisioning or transient failure. */
        }
        if(retry_ticks>0)--retry_ticks;
        rt_thread_mdelay(500);
    }
}
static int service_init(void)
{
    rt_thread_t t=rt_thread_create("ap_service",service_worker,NULL,16384,18,10);
    if(!t){service_error=-RT_ENOMEM;return service_error;}
    return rt_thread_startup(t);
}
INIT_APP_EXPORT(service_init);
int ft_ap_service_running(void){return service_state;}
static void ft_airplay2(int argc,char **argv)
{
    if(argc==2 && !strcmp(argv[1],"on")){
        service_enabled=1;
    }else if(argc==2 && !strcmp(argv[1],"off")){
        service_enabled=0;
    }
    audio_stats_t stats;audio_receiver_get_stats(&stats);
    rt_kprintf("[AP] enabled=%d state=%d error=%d packets=%u decoded=%u decrypt-errors=%u pcm-frames=%u write-errors=%u empty-reads=%u\n",
        service_enabled,service_state,service_error,stats.packets_received,stats.packets_decoded,stats.decrypt_errors,ap_pcm_frames,ap_pcm_write_errors,ap_pcm_underflows);
}
MSH_CMD_EXPORT(ft_airplay2, Experimental AirPlay 2 receiver on / off / status);
MSH_CMD_EXPORT_ALIAS(ft_airplay2,ft_airplay,AirPlay 2 receiver on / off / status);
