/* Port adaptation of upstream API; see ../LICENSE. Bounded TLV implementation. */
#include <string.h>
#include "tlv8.h"
#include "ft_airplay_protocol.h"
void tlv8_encoder_init(tlv8_encoder_t *e,uint8_t *b,size_t cap){e->buffer=b;e->capacity=cap;e->size=0;}
bool tlv8_encode(tlv8_encoder_t *e,uint8_t t,const uint8_t *v,size_t n)
{size_t wrote=0;if(!e || e->size>e->capacity)return false;
 if(ft_ap_tlv_put(e->buffer+e->size,e->capacity-e->size,t,v,n,&wrote)!=FT_AP_OK)return false;
 e->size+=wrote;return true;}
bool tlv8_encode_byte(tlv8_encoder_t *e,uint8_t t,uint8_t v){return tlv8_encode(e,t,&v,1);}
size_t tlv8_encoder_size(const tlv8_encoder_t *e){return e->size;}
const uint8_t *tlv8_find(const uint8_t *d,size_t n,uint8_t t,size_t *length)
{
    const uint8_t *found=NULL;size_t pos=0,first=0,previous=0;int state=0;
    *length=0;
    while(pos<n){
        if(n-pos<2 || d[pos+1]>n-pos-2)return NULL;
        size_t size=d[pos+1];
        if(d[pos]==t){
            if(state==2 || (state==1 && previous!=255))return NULL;
            if(!found){found=d+pos+2;first=size;}
            previous=size;state=1;
        }else if(state==1)state=2;
        pos+=size+2;
    }
    if(found)*length=first;
    return found;
}
bool tlv8_decode_concat(const uint8_t *d,size_t n,uint8_t t,uint8_t *out,size_t cap,size_t *used)
{return ft_ap_tlv_get(d,n,t,out,cap,used)==FT_AP_OK;}
