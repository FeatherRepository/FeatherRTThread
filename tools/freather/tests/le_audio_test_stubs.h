#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "btstack_config.h"
#include "btstack_event.h"
#include "hci.h"
#include "gap.h"
#include "ble/att_server.h"
#include "ft_gatt.h"
typedef uint8_t rt_uint8_t;
typedef uint16_t rt_uint16_t;
typedef uint32_t rt_uint32_t;
#define RT_NULL NULL
#define rt_kprintf(...) ((void)0)
#define __DMB() ((void)0)
#define FT_ALINK_DCACHE_CLEAN(a,b) ((void)0)
#define FT_ALINK_FLAGS_MODE_LE_AUDIO 1
#define FT_ALINK_BASE 0
#define MSH_CMD_EXPORT_ALIAS(a,b,c)
static struct { uint32_t fmt_rate, fmt_bits, fmt_ch, flags, fmt_gen, volume_percent; } test_ring;
#define FT_ALINK (&test_ring)
static uint8_t produced[256];
static unsigned produced_len;
static unsigned ft_alink_space(void *p) { (void)p; return sizeof(produced); }
static void feathertalk_ipc_send_event(unsigned code) { (void)code; }
static uint32_t ft_audio_produce(const uint8_t *p, uint32_t n) {
    assert(n <= sizeof(produced)); memcpy(produced,p,n); produced_len=n; return n;
}
