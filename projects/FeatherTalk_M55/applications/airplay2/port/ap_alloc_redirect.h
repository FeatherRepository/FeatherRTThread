#pragma once
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <stddef.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/select.h>
#define MBEDTLS_CONFIG_FILE "ap_mbedtls_config.h"
#include "sdkconfig.h"
#ifdef __cplusplus
#include <stdlib.h>
#include <new>
extern "C" {
#endif
void *ap_malloc(size_t);
void *ap_calloc(size_t,size_t);
void *ap_realloc(void *,size_t);
void ap_free(void *);
#ifdef __cplusplus
}
#endif
#define malloc ap_malloc
#define calloc ap_calloc
#define realloc ap_realloc
#define free ap_free
