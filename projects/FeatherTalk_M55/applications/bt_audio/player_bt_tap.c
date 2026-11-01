#include <rtthread.h>
#include <rthw.h>
#include <string.h>

#include "player_bt_tap.h"

#define FT_PLAYER_BT_TAP_BYTES 32768U

/* The M55 internal data RAM is already fully committed. Keep this producer /
 * consumer buffer in the large SOC-memory heap instead of .bss. */
static uint8_t *s_ring;
static volatile uint32_t s_read;
static volatile uint32_t s_write;
static volatile uint32_t s_used;
static volatile bool s_active;

void ft_player_bt_tap_reset(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    s_read = 0U;
    s_write = 0U;
    s_used = 0U;
    rt_hw_interrupt_enable(level);
}

void ft_player_bt_tap_set_active(bool active)
{
    rt_base_t level;
    if (active && s_ring == RT_NULL)
    {
        s_ring = rt_malloc(FT_PLAYER_BT_TAP_BYTES);
        if (s_ring == RT_NULL) active = false;
    }
    level = rt_hw_interrupt_disable();
    if (s_active != active)
    {
        s_read = 0U;
        s_write = 0U;
        s_used = 0U;
    }
    s_active = active;
    rt_hw_interrupt_enable(level);
}

bool ft_player_bt_tap_active(void)
{
    return s_active;
}

uint32_t ft_player_bt_tap_write(const uint8_t *data, uint32_t length)
{
    rt_base_t level;
    uint32_t free_bytes;
    uint32_t first;
    uint32_t written;

    if (!s_active || s_ring == RT_NULL || data == RT_NULL || length == 0U)
        return 0U;
    level = rt_hw_interrupt_disable();
    free_bytes = FT_PLAYER_BT_TAP_BYTES - s_used;
    if (length > free_bytes) length = free_bytes;
    length &= ~3U;
    written = length;
    first = FT_PLAYER_BT_TAP_BYTES - s_write;
    if (first > length) first = length;
    if (first != 0U)
    {
        memcpy(&s_ring[s_write], data, first);
        s_write = (s_write + first) % FT_PLAYER_BT_TAP_BYTES;
    }
    if (length > first)
    {
        memcpy(s_ring, data + first, length - first);
        s_write = length - first;
    }
    s_used += length;
    rt_hw_interrupt_enable(level);
    return written;
}

uint32_t ft_player_bt_tap_read(uint8_t *data, uint32_t capacity)
{
    rt_base_t level;
    uint32_t length;
    uint32_t first;

    if (s_ring == RT_NULL || data == RT_NULL || capacity == 0U) return 0U;
    level = rt_hw_interrupt_disable();
    length = s_used < capacity ? s_used : capacity;
    length &= ~3U;
    first = FT_PLAYER_BT_TAP_BYTES - s_read;
    if (first > length) first = length;
    if (first != 0U)
    {
        memcpy(data, &s_ring[s_read], first);
        s_read = (s_read + first) % FT_PLAYER_BT_TAP_BYTES;
    }
    if (length > first)
    {
        memcpy(data + first, s_ring, length - first);
        s_read = length - first;
    }
    s_used -= length;
    rt_hw_interrupt_enable(level);
    return length;
}
