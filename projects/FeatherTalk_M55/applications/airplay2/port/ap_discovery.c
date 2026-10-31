/* Application-level mDNS/DNS-SD over the existing RT-Thread BSD socket API.
 * No lwIP options, netif client-data slots or platform changes required. */
#include <rtthread.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "hap.h"
#include "wifi.h"
#include "ft_airplay_features.h"
#define DNS_CAP 1500
typedef struct {
    int fd,stop; size_t used; int overflow;
    uint8_t out[DNS_CAP],in[DNS_CAP];
    char id[18],pk[65],raop[96];
    struct sockaddr_in group;
} dns_context;
static dns_context *volatile current;
static const char *host="feathertalk-airplay.local";
static const char *air="_airplay._tcp.local",*raop="_raop._tcp.local";
static const char *air_instance="FeatherTalk AirPlay._airplay._tcp.local";
static void put(dns_context *c,const void *p,size_t n)
{ if(n>DNS_CAP-c->used){c->overflow=1;return;}memcpy(c->out+c->used,p,n);c->used+=n; }
static void word(dns_context *c,unsigned n){uint8_t b[2]={(uint8_t)(n>>8),(uint8_t)n};put(c,b,2);}
static void dword(dns_context *c,uint32_t n){word(c,n>>16);word(c,n);}
static void name(dns_context *c,const char *s)
{
    while(*s){const char *end=strchr(s,'.');size_t n=end?(size_t)(end-s):strlen(s);
        if(!n || n>63){c->overflow=1;return;}uint8_t b=(uint8_t)n;put(c,&b,1);put(c,s,n);
        if(!end)break;s=end+1;}
    uint8_t zero=0;put(c,&zero,1);
}
static size_t rr(dns_context *c,const char *n,int type,int unique,uint32_t ttl)
{ name(c,n);word(c,type);word(c,unique?0x8001:1);dword(c,ttl);size_t p=c->used;word(c,0);return p; }
static void end_rr(dns_context *c,size_t p)
{ if(c->overflow || p+2>c->used)return;size_t n=c->used-p-2;c->out[p]=(uint8_t)(n>>8);c->out[p+1]=(uint8_t)n; }
static void txt(dns_context *c,const char *key,const char *value)
{char item[128];int n=snprintf(item,sizeof(item),"%s=%s",key,value);if(n<0 || n>=sizeof(item)){c->overflow=1;return;}uint8_t size=(uint8_t)n;put(c,&size,1);put(c,item,n);}
static void airplay_txt(dns_context *c)
{
    txt(c,"deviceid",c->id);txt(c,"features",FT_AP_FEATURES_TXT);txt(c,"flags","0x4");
    txt(c,"model","AudioAccessory5,1");txt(c,"pk",c->pk);txt(c,"srcvers","377.40.00");
    txt(c,"vv","2");txt(c,"acl","0");txt(c,"pi","00000000-0000-0000-0000-000000000000");
    txt(c,"protovers","1.1");
}
size_t ft_ap_airplay_txt(uint8_t *out,size_t cap)
{
    dns_context *c=rt_calloc(1,sizeof(*c));if(!c)return 0;
    wifi_get_mac_str(c->id,sizeof(c->id));const uint8_t *pk=hap_get_public_key();
    for(int i=0;i<32;++i)snprintf(c->pk+i*2,3,"%02x",pk[i]);
    airplay_txt(c);size_t n=c->used;
    if(c->overflow || n>cap)n=0;else memcpy(out,c->out,n);
    rt_free(c);return n;
}
static size_t build(dns_context *c,uint32_t ttl,uint16_t id)
{
    char ip[16];if(wifi_get_ip_str(ip,sizeof(ip))!=0)return 0;
    c->used=0;c->overflow=0;word(c,id);word(c,0x8400);word(c,0);word(c,9);word(c,0);word(c,0);
    size_t p=rr(c,"_services._dns-sd._udp.local",12,0,ttl);name(c,air);end_rr(c,p);
    p=rr(c,"_services._dns-sd._udp.local",12,0,ttl);name(c,raop);end_rr(c,p);
    p=rr(c,air,12,0,ttl);name(c,air_instance);end_rr(c,p);
    p=rr(c,raop,12,0,ttl);name(c,c->raop);end_rr(c,p);
    p=rr(c,air_instance,33,1,ttl);word(c,0);word(c,0);word(c,7000);name(c,host);end_rr(c,p);
    p=rr(c,c->raop,33,1,ttl);word(c,0);word(c,0);word(c,7000);name(c,host);end_rr(c,p);
    p=rr(c,air_instance,16,1,ttl);
    airplay_txt(c);end_rr(c,p);
    p=rr(c,c->raop,16,1,ttl);
#ifdef FT_AP_WITH_AAC
    txt(c,"cn","0,1,2");
#else
    txt(c,"cn","0,1");
#endif
    txt(c,"ch","2");txt(c,"ss","16");txt(c,"sr","44100");txt(c,"et","5");txt(c,"ft",FT_AP_FEATURES_TXT);
    txt(c,"pk",c->pk);txt(c,"am","AudioAccessory5,1");txt(c,"vs","377.40.00");txt(c,"vv","2");
    txt(c,"tp","UDP");txt(c,"sf","0x4");txt(c,"md","0,2");txt(c,"vn","65537");txt(c,"txtvers","1");end_rr(c,p);
    p=rr(c,host,1,1,ttl);uint32_t address=inet_addr(ip);put(c,&address,4);end_rr(c,p);
    return c->overflow?0:c->used;
}
static int read_name(const uint8_t *p,size_t n,size_t *offset,char out[256])
{
    size_t at=*offset,end=0,used=0;unsigned jumps=0;
    while(at<n && jumps++<128){
        unsigned size=p[at++];
        if((size&0xc0)==0xc0){if(at>=n)return -1;if(!end)end=at+1;at=((size&63)<<8)|p[at];continue;}
        if(size&0xc0 || size>n-at || size>63)return -1;
        if(!size){*offset=end?end:at;out[used]=0;return 0;}
        if(used){if(used>=254)return -1;out[used++]='.';}
        if(size>255-used)return -1;memcpy(out+used,p+at,size);used+=size;at+=size;
    }
    return -1;
}
static int interested(dns_context *c,size_t n)
{
    if(n<12 || c->in[2]&0x80)return 0;
    unsigned q=((unsigned)c->in[4]<<8)|c->in[5];if(q>16)return 0;size_t pos=12;int match=0;
    for(unsigned i=0;i<q;++i){char query[256];if(read_name(c->in,n,&pos,query) || n-pos<4)return 0;
        unsigned type=((unsigned)c->in[pos]<<8)|c->in[pos+1];pos+=4;
        if(type!=1 && type!=12 && type!=16 && type!=33 && type!=255)continue;
        if(!strcasecmp(query,host)||!strcasecmp(query,air)||!strcasecmp(query,raop)||!strcasecmp(query,air_instance)||
           !strcasecmp(query,c->raop)||!strcasecmp(query,"_services._dns-sd._udp.local"))match=1;}
    return match;
}
static void announce(dns_context *c,uint32_t ttl)
{size_t n=build(c,ttl,0);if(n)sendto(c->fd,c->out,n,0,(struct sockaddr *)&c->group,sizeof(c->group));}
static void worker(void *arg)
{
    dns_context *c=arg;rt_tick_t last=rt_tick_get(),reply=last-1000;announce(c,120);
    while(!c->stop){fd_set fds;struct timeval tv={0,200000};FD_ZERO(&fds);FD_SET(c->fd,&fds);
        int ready=select(c->fd+1,&fds,NULL,NULL,&tv);
        if(ready>0){struct sockaddr_in peer;socklen_t len=sizeof(peer);int n=recvfrom(c->fd,c->in,sizeof(c->in),0,(struct sockaddr *)&peer,&len);
            if(n>0 && interested(c,(size_t)n) && (rt_tick_t)(rt_tick_get()-reply)>=rt_tick_from_millisecond(100)){
                reply=rt_tick_get();uint16_t id=((uint16_t)c->in[0]<<8)|c->in[1];size_t size=build(c,120,peer.sin_port==htons(5353)?0:id);
                struct sockaddr_in *to=peer.sin_port==htons(5353)?&c->group:&peer;
                if(size)sendto(c->fd,c->out,size,0,(struct sockaddr *)to,sizeof(*to));}}
        if((rt_tick_t)(rt_tick_get()-last)>=rt_tick_from_millisecond(30000)){announce(c,120);last=rt_tick_get();}
    }
    announce(c,0);closesocket(c->fd);rt_free(c);current=NULL;
}
int ft_ap_discovery(int enable)
{
    if(!enable){if(current){current->stop=1;for(int i=0;i<100 && current;++i)rt_thread_mdelay(10);}return current?-1:0;}
    if(current)return current->stop?-1:0;
    dns_context *c=rt_calloc(1,sizeof(*c));if(!c)return -1;
    wifi_get_mac_str(c->id,sizeof(c->id));char compact[13];int k=0;
    for(int i=0;c->id[i] && k<12;++i)if(c->id[i]!=':')compact[k++]=c->id[i];compact[k]=0;
    snprintf(c->raop,sizeof(c->raop),"%s@FeatherTalk AirPlay._raop._tcp.local",compact);
    const uint8_t *pk=hap_get_public_key();for(int i=0;i<32;++i)snprintf(c->pk+i*2,3,"%02x",pk[i]);
    c->fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(c->fd<0){rt_free(c);return -1;}
    struct sockaddr_in local={0};local.sin_family=AF_INET;local.sin_port=htons(5353);
    c->group.sin_family=AF_INET;c->group.sin_port=htons(5353);c->group.sin_addr.s_addr=inet_addr("224.0.0.251");
    struct ip_mreq membership;membership.imr_multiaddr=c->group.sin_addr;membership.imr_interface.s_addr=INADDR_ANY;
    unsigned char ttl=255;
    if(bind(c->fd,(struct sockaddr *)&local,sizeof(local)) ||
       setsockopt(c->fd,IPPROTO_IP,IP_ADD_MEMBERSHIP,&membership,sizeof(membership)) ||
       setsockopt(c->fd,IPPROTO_IP,IP_MULTICAST_TTL,&ttl,sizeof(ttl)) || !build(c,120,0)){
        closesocket(c->fd);rt_free(c);return -1;}
    rt_thread_t t=rt_thread_create("ap_mdns",worker,c,8192,18,10);
    if(!t){closesocket(c->fd);rt_free(c);return -1;}
    current=c;rt_thread_startup(t);return 0;
}
