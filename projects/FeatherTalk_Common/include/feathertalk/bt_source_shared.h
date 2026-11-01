/* bt_source_shared.h - M33 A2DP Source 扫描与连接状态快照
 *
 * 位置: 0x261D4400, 紧接 bond 1KB 共享块之后的独立 512B 块。
 * 所有权:
 *   - M33 bk btstack 是唯一写方，每次状态变化用 seqlock 包裹整份快照。
 *   - M55 UI 只读；读取前 invalidate D-Cache，首尾 sequence 一致才采用。
 *   - 地址和名称不是指针，跨核生命期由本块自身保证。
 */
#ifndef FEATHERTALK_BT_SOURCE_SHARED_H
#define FEATHERTALK_BT_SOURCE_SHARED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FT_BT_SOURCE_BASE       0x261D4400UL
#define FT_BT_SOURCE_MAGIC      0x46544253UL   /* 'FTBS' */
#define FT_BT_SOURCE_VERSION    1U
#define FT_BT_SOURCE_BYTES      512U
#define FT_BT_SOURCE_MAX_DEVICES 8U
#define FT_BT_SOURCE_NAME_LEN   24U

#define FT_BT_SOURCE_FLAG_AUDIO        (1U << 0)
#define FT_BT_SOURCE_FLAG_CONNECTED    (1U << 1)

typedef enum
{
    FT_BT_SOURCE_STATE_CLOSED = 0,
    FT_BT_SOURCE_STATE_CONNECTING,
    FT_BT_SOURCE_STATE_OPEN,
    FT_BT_SOURCE_STATE_PLAYING,
    FT_BT_SOURCE_STATE_ERROR
} ft_bt_source_state_t;

typedef struct
{
    uint8_t addr[6];
    uint8_t flags;
    uint8_t name_len;
    char name[FT_BT_SOURCE_NAME_LEN];
} ft_bt_source_device_t;

typedef struct
{
    uint32_t magic;
    uint32_t sequence;
    uint32_t version;
    uint32_t scan_active;
    uint32_t state;
    uint32_t count;
    uint8_t peer[6];
    uint8_t peer_flags;
    uint8_t reserved;
    ft_bt_source_device_t devices[FT_BT_SOURCE_MAX_DEVICES];
} ft_bt_source_shared_t;

#define FT_BT_SOURCE ((volatile ft_bt_source_shared_t *)FT_BT_SOURCE_BASE)

#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1)
#define FT_BT_SOURCE_DCACHE_INVALID(addr, size) \
    SCB_InvalidateDCache_by_Addr((uint32_t *)(addr), (int32_t)(size))
#else
#define FT_BT_SOURCE_DCACHE_INVALID(addr, size)
#endif

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(ft_bt_source_device_t) == 32U,
               "bt source device entry must stay fixed for cross-core ABI");
_Static_assert(sizeof(ft_bt_source_shared_t) <= FT_BT_SOURCE_BYTES,
               "bt source snapshot must fit its reserved shared block");
#endif

#ifdef __cplusplus
}
#endif

#endif /* FEATHERTALK_BT_SOURCE_SHARED_H */
