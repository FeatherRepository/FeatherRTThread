/* bond_shared.h - M3': 蓝牙配对(link key)持久化共享块
 *
 * 位置: m33_m55_shared 区 0x261D4000 (ring2 之后的大片空闲, 1KB 块,
 * 双核同址)。所有权模型 (同 smif_guard/radio_shared 模式):
 *   - M55 是持久化宿主: 启动时读 /flash/bt_bond.bin 写入共享块;
 *     轮询 seq 变化 -> 变化即写回文件 (tmp + rename 原子化)
 *   - M33 btstack link key 库是运行时真值: put/delete -> 更新表 ->
 *     序列化到共享块 -> seq++
 *   - M33 启动早期等 magic 出现 (有界), 把共享块条目载入内存表
 * 缓存: M55 访问前后 Clean/Invalidate D-Cache (M33 无 D-cache 自动为空,
 * 与 audio_link.h 同款处理)。
 */
#ifndef FEATHERTALK_BOND_SHARED_H
#define FEATHERTALK_BOND_SHARED_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FT_BOND_BASE        0x261D4000UL
#define FT_BOND_MAGIC       0x46544244UL   /* 'FTBD' */
#define FT_BOND_MAX_ENTRIES 8
#define FT_BOND_ENTRY_SIZE  32

typedef struct
{
    uint8_t bd_addr[6];
    uint8_t link_key[16];
    uint8_t key_type;      /* btstack link_key_type_t */
    uint8_t authenticated;
    uint8_t reserved[FT_BOND_ENTRY_SIZE - 6 - 16 - 2];
} ft_bond_entry_t;

typedef struct
{
    uint32_t magic;
    uint32_t seq;                       /* 写方每改一次 +1 */
    uint32_t count;                     /* 有效条目数 */
    uint32_t crc;                       /* entries 的 CRC32 */
    ft_bond_entry_t entries[FT_BOND_MAX_ENTRIES];
} ft_bond_shared_t;

#define FT_BOND ((volatile ft_bond_shared_t *)FT_BOND_BASE)

#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1)
#define FT_BOND_DCACHE_CLEAN(addr, size)   SCB_CleanDCache_by_Addr((uint32_t *)(addr), (int32_t)(size))
#define FT_BOND_DCACHE_INVALID(addr, size) SCB_InvalidateDCache_by_Addr((uint32_t *)(addr), (int32_t)(size))
#else
#define FT_BOND_DCACHE_CLEAN(addr, size)
#define FT_BOND_DCACHE_INVALID(addr, size)
#endif

/* CRC32 (无表位翻转实现, 初始化成本为零; 条目仅 ~256B, 逐位足够) */
static inline uint32_t ft_bond_crc32(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFU;
    for (uint32_t i = 0; i < len; i++)
    {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return crc ^ 0xFFFFFFFFU;
}

/* 序列化内存表 -> 共享块 (写方调用; M55 侧需随后 clean cache) */
static inline void ft_bond_serialize(volatile ft_bond_shared_t *dst,
                                     const ft_bond_entry_t *entries, uint32_t count,
                                     uint32_t seq)
{
    memset((void *)dst->entries, 0, sizeof(dst->entries));
    if (count > FT_BOND_MAX_ENTRIES) count = FT_BOND_MAX_ENTRIES;
    if (count) memcpy((void *)dst->entries, entries, count * sizeof(ft_bond_entry_t));
    dst->count = count;
    dst->crc = ft_bond_crc32(dst->entries, sizeof(dst->entries));
    dst->seq = seq;
    dst->magic = FT_BOND_MAGIC;
}

/* 校验共享块有效性 (读方调用) */
static inline int ft_bond_valid(const volatile ft_bond_shared_t *src)
{
    if (src->magic != FT_BOND_MAGIC) return 0;
    if (src->count > FT_BOND_MAX_ENTRIES) return 0;
    return ft_bond_crc32(src->entries, sizeof(src->entries)) == src->crc;
}

#ifdef __cplusplus
}
#endif

#endif /* FEATHERTALK_BOND_SHARED_H */
