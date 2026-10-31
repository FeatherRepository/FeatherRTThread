/* HOST TEST ONLY. Never part of the firmware SConscript. */
#include <stddef.h>
int ft_ap_random(void *out,size_t n){unsigned char *p=out;for(size_t i=0;i<n;++i)p[i]=(unsigned char)i;return 0;}
