#include "sodium.h"
#include "base64.h"
#include <string.h>
int sodium_base642bin(uint8_t *out,size_t cap,const char *in,size_t n,const char *ignore,size_t *used,const char **end,int variant)
{
    char buffer[1024];size_t size=0;
    if(used)*used=0;
    if(n>sizeof(buffer)-4 || (variant!=1 && variant!=3))return -1;
    for(size_t i=0;i<n;++i)if(!ignore || !strchr(ignore,in[i]))buffer[size++]=in[i];
    while(size%4)buffer[size++]='=';
    int r=base64_decode(buffer,size,out,cap);if(r<0)return -1;
    if(used)*used=(size_t)r;if(end)*end=in+n;return 0;
}
