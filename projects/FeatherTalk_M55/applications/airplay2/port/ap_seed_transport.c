/* Seed-only application console framing. Uses the existing RT-Thread serial API.
 * Command stays active while receiving, so secret text never enters MSH history.
 * No UART reconfiguration, OS callbacks or debugger access. */
#include <rtthread.h>
#include <string.h>
static int read_exact(rt_device_t dev,uint8_t *out,size_t n,unsigned ms)
{
    size_t used=0;rt_tick_t start=rt_tick_get();
    while(used<n && (rt_tick_t)(rt_tick_get()-start)<rt_tick_from_millisecond(ms)) {
        rt_ssize_t got=rt_device_read(dev,0,out+used,n-used);
        if(got>0 && (size_t)got<=n-used)used+=(size_t)got;else rt_thread_mdelay(1);
    }
    return used==n?0:-1;
}
static int hex(unsigned ch)
{if(ch>='0' && ch<='9')return ch-'0';if(ch>='a' && ch<='f')return ch-'a'+10;return -1;}
int ft_ap_seed_receive(uint8_t out[96])
{
    rt_device_t dev=rt_console_get_device();uint8_t prefix[7],pair[2],end;int result=-1;
    /* The shell/console can leave a line terminator pending. Synchronize on
     * the public marker instead of assuming the next byte starts the frame. */
    result=-10;size_t matched=0;rt_tick_t start=rt_tick_get();
    while(matched<7 && (rt_tick_t)(rt_tick_get()-start)<rt_tick_from_millisecond(10000)){
        uint8_t c;if(read_exact(dev,&c,1,100))continue;
        if(c==(uint8_t)"APSEED:"[matched])++matched;else matched=c=='A'?1:0;
    }
    if(matched!=7)goto done;
    for(unsigned i=0;i<96;++i){
        result=-20-(int)i;if(read_exact(dev,pair,2,3000))goto done;
        int a=hex(pair[0]),b=hex(pair[1]);result=-200-(int)i;if(a<0 || b<0)goto done;out[i]=(uint8_t)((a<<4)|b);
    }
    result=-400;if(read_exact(dev,&end,1,3000))goto done;
    result=-401;if(end!='\r' && end!='\n')goto done;
    result=0;
done:
    memset(prefix,0,sizeof(prefix));memset(pair,0,sizeof(pair));
    if(result){
        memset(out,0,96);rt_tick_t start=rt_tick_get();
        while((rt_tick_t)(rt_tick_get()-start)<rt_tick_from_millisecond(3000)){
            if(rt_device_read(dev,0,&end,1)==1 && end=='\r')break;rt_thread_mdelay(1);
        }
    }
    return result;
}
