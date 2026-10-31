/* FeatherTalk-only resource override. Preserve every other SDK lwIP option. */
#ifndef FEATHERTALK_LWIP_RESOURCE_OVERRIDE_H
#define FEATHERTALK_LWIP_RESOURCE_OVERRIDE_H
#include "../../../../../rt-thread/components/net/lwip/port/lwipopts.h"
#undef MEMP_NUM_TCP_PCB_LISTEN
#define MEMP_NUM_TCP_PCB_LISTEN 3 /* RTSP control + event + buffered audio. */
#endif
