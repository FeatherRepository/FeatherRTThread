#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
typedef struct { rt_tick_t tick;char stage[48];int value; } ap_diag_entry;
static ap_diag_entry *ap_history;
static unsigned ap_history_count;
/* Callers pass protocol labels only; copied so bounded route labels are safe. */
void ap_diag_record(const char *stage,int value)
{
    if(!ap_history){
        ap_diag_entry *candidate=rt_calloc(32,sizeof(*candidate));if(!candidate)return;
        rt_base_t init_mask=rt_hw_interrupt_disable();
        if(!ap_history){ap_history=candidate;candidate=NULL;}
        rt_hw_interrupt_enable(init_mask);if(candidate)rt_free(candidate);
    }
    rt_base_t mask=rt_hw_interrupt_disable();
    ap_diag_entry *entry=&ap_history[ap_history_count%32];
    entry->tick=rt_tick_get();entry->value=value;
    rt_strncpy(entry->stage,stage,sizeof(entry->stage)-1);entry->stage[sizeof(entry->stage)-1]=0;
    ++ap_history_count;rt_hw_interrupt_enable(mask);
}
static void ft_ap_trace(void)
{
    ap_diag_entry copy[32];unsigned total,n;
    rt_base_t mask=rt_hw_interrupt_disable();total=ap_history_count;n=total<32?total:32;
    for(unsigned i=0;i<n;++i)copy[i]=ap_history[(total-n+i)%32];
    rt_hw_interrupt_enable(mask);
    rt_kprintf("[AP trace] total=%u retained=%u\n",total,n);
    for(unsigned i=0;i<n;++i)rt_kprintf("[AP trace] tick=%u stage=%s value=%d\n",(unsigned)copy[i].tick,copy[i].stage,copy[i].value);
}
MSH_CMD_EXPORT(ft_ap_trace, Read retained AirPlay protocol stages without secret data);
/* rt_vsnprintf does not consume every libc length modifier. Never feed it
 * unsupported varargs: a following %s could otherwise dereference an integer.
 * Extended-format diagnostics retain their literal format, without arguments. */
void ap_log(const char *level,const char *tag,const char *fmt,...)
{
    int supported=1;
    for(const char *p=fmt;*p && supported;++p){
        if(*p!='%')continue;
        ++p;if(*p=='%')continue;
        while(*p && strchr("-+ #0.123456789",*p))++p;
        if(!*p || !strchr("diuoxXcsp",*p))supported=0;
    }
    char text[256];
    if(supported){va_list ap;va_start(ap,fmt);rt_vsnprintf(text,sizeof(text),fmt,ap);va_end(ap);}
    else rt_snprintf(text,sizeof(text),"[format unsupported] %s",fmt);
    rt_kprintf("[AP:%s:%s] %s\n",level,tag,text);
}
#include <FreeRTOS.h>
#include <task.h>
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
extern struct rt_memheap *drv_hyperam_get_memheap(void);
const char *esp_err_to_name(int e) { return e ? "platform error" : "OK"; }
void *heap_caps_malloc(size_t n,unsigned caps)
{
    struct rt_memheap *heap=drv_hyperam_get_memheap();
    if(n==0) n=1;
    if ((n>=4096 || (caps&MALLOC_CAP_SPIRAM)) && heap && heap->pool_size)
        return rt_memheap_alloc(heap,n);
    return rt_malloc(n);
}
void *heap_caps_calloc(size_t n,size_t size,unsigned caps)
{ void *p; if(size && n>SIZE_MAX/size) return NULL; p=heap_caps_malloc(n*size,caps); if(p) memset(p,0,n*size); return p; }
void *heap_caps_realloc(void *p,size_t n,unsigned caps) { (void)caps; return p?rt_realloc(p,n):heap_caps_malloc(n,caps); }
void heap_caps_free(void *p) { rt_free(p); }
size_t heap_caps_get_free_size(unsigned caps) { struct rt_memheap *h=drv_hyperam_get_memheap(); (void)caps; return h?h->available_size:0; }
size_t heap_caps_get_largest_free_block(unsigned caps)
{
    struct rt_memheap *h=drv_hyperam_get_memheap(); struct rt_memheap_item *p; size_t largest=0; (void)caps;
    if(!h || rt_sem_take(&h->lock,RT_WAITING_FOREVER)!=RT_EOK) return 0;
    for(p=h->free_list->next_free;p!=h->free_list;p=p->next_free) {
        size_t n=(uint8_t *)p->next-(uint8_t *)p-RT_ALIGN(sizeof(*p),RT_ALIGN_SIZE);
        if(n>largest) largest=n;
    }
    rt_sem_release(&h->lock); return largest;
}
void *ap_malloc(size_t n) { return heap_caps_malloc(n,0); }
void *ap_calloc(size_t n,size_t s) { return heap_caps_calloc(n,s,0); }
void *ap_realloc(void *p,size_t n) { return heap_caps_realloc(p,n,0); }
void ap_free(void *p) { heap_caps_free(p); }
BaseType_t ap_task_create(TaskFunction_t fn,const char *name,uint32_t bytes,void *arg,UBaseType_t priority,TaskHandle_t *handle)
{
    if(bytes<16384) bytes=16384;
    priority+=10; if(priority>24) priority=24;
    return xTaskCreate(fn,name,(bytes+sizeof(StackType_t)-1)/sizeof(StackType_t),arg,priority,handle);
}
int64_t esp_timer_get_time(void)
{
    /* RT-Thread clock only. Millisecond granularity; no direct timer registers. */
    rt_base_t level=rt_hw_interrupt_disable();
    uint32_t tick=rt_tick_get();
    static uint32_t previous; static uint64_t epoch;
    if(tick<previous) epoch+=(UINT64_C(1)<<32);
    previous=tick;
    uint64_t us=(epoch+tick)*1000000/RT_TICK_PER_SECOND;
    rt_hw_interrupt_enable(level); return (int64_t)us;
}
int ft_ap_random(void *out,size_t size)
{
    extern int ft_ap_drbg_random(void *,size_t);
    return ft_ap_drbg_random(out,size);
}
void esp_fill_random(void *out,size_t n)
{
    /* Legacy void API is not used by key generation; never fall back to rand(). */
    if(ft_ap_random(out,n)) { memset(out,0,n); RT_ASSERT(0); }
}
