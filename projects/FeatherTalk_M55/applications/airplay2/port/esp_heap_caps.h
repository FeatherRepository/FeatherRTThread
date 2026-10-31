#pragma once
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
#define MALLOC_CAP_DMA 8
void *heap_caps_malloc(size_t n, unsigned caps);
void *heap_caps_calloc(size_t n, size_t size, unsigned caps);
void *heap_caps_realloc(void *p, size_t n, unsigned caps);
void heap_caps_free(void *p);
size_t heap_caps_get_free_size(unsigned caps);
size_t heap_caps_get_largest_free_block(unsigned caps);
