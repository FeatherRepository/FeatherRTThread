#include <stdio.h>
#include <stdint.h>
#include "plist.h"
int main(void) {
    char buffer[2048];plist_t p;plist_init(&p,buffer,sizeof(buffer));
    plist_begin(&p);plist_dict_begin(&p);
    plist_dict_int(&p,"min",INT64_MIN);plist_dict_int(&p,"max",INT64_MAX);
    plist_dict_uint(&p,"umax",UINT64_MAX);plist_dict_uint(&p,"zero",0);
    plist_dict_uint(&p,"features",UINT64_C(0x18340401c0a00));
    plist_dict_array_begin(&p,"array");plist_array_int(&p,-1);plist_array_int(&p,0);
    plist_array_int(&p,INT64_MIN);plist_array_end(&p);plist_dict_end(&p);
    size_t n=plist_end(&p);return fwrite(buffer,1,n,stdout)==n?0:1;
}
