/* audio_link.h - FeatherTalk A0 跨核音频数据面 (M33 <-> M55)
 *
 * 共享 SRAM 环形缓冲 + IPC 门铃。布局与规则见
 * worklog/topics/A0-跨核音频通道设计.md：
 *  - 区域: m33_m55_shared (0x261C0000, 双核同址, 256KB)
 *  - wr 仅 producer 写, rd 仅 consumer 写, 各占独立 32B cache line
 *  - 数据面 cache 维护: 写后 CleanDCache / 读前 InvalidateDCache
 *    (__DCACHE_PRESENT 守卫; M33 无 D-cache 自动为空)
 */
#ifndef FEATHERTALK_AUDIO_LINK_H
#define FEATHERTALK_AUDIO_LINK_H

#include <stdint.h>
#include <string.h>
#include <cy_syslib.h>   /* __DMB / SCB_* (PDL, 双核工程均有) */

#ifdef __cplusplus
extern "C" {
#endif

#define FT_ALINK_MAGIC      0x46544155UL   /* 'FTAU' */
#define FT_ALINK_BASE       0x261C0000UL   /* m33_m55_shared 起始 (双核链接脚本同址) */
#define FT_ALINK_RING_BYTES 32768U         /* 8 x 4096B ≈ 85ms @48k/16/2ch */

/* M5: 反向 ring (M55 producer -> M33 consumer, A2DP Source 用)。
 * 控制块 128B + 数据 32768B = 32896B, 32B 对齐; 两 ring 同构, 仅方向相反。
 * 初始化纪律与正向一致: M33 启动早期一次铺好, M55 等 magic。 */
#define FT_ALINK2_BASE      (FT_ALINK_BASE + 32896UL)

/* cache line (32B) 对齐的控制块; wr/rd 分居独立 line 防伪共享 */
typedef struct
{
    volatile uint32_t magic;            /* FT_ALINK_MAGIC */
    volatile uint32_t capacity;         /* = FT_ALINK_RING_BYTES */
    volatile uint32_t fmt_rate;         /* 当前流格式: 采样率 */
    volatile uint32_t fmt_bits;         /* 位深 */
    volatile uint32_t fmt_ch;           /* 声道数 */
    volatile uint32_t fmt_gen;          /* 格式代际: 变化即 +1 (UAC 同款语义) */
    volatile uint32_t flags;            /* 预留 */
    volatile uint32_t volume_percent;   /* M4b: AVRCP 绝对音量 0-100, 0xFF=不变 (M33 写, M55 读; 原名 reserved0) */
    uint8_t  _pad0[32 - 8 * 4];

    volatile uint32_t wr;               /* 写偏移 (字节, 单调不回绕; 取模用) */
    uint8_t  _pad1[32 - 4];

    volatile uint32_t rd;               /* 读偏移 (字节, 单调) */
    uint8_t  _pad2[32 - 4];

    volatile uint32_t seq;              /* producer 块计数 */
    volatile uint32_t stats_overrun;    /* producer: 空间不足丢弃的字节数 */
    volatile uint32_t stats_doorbell;   /* producer: 门铃发送计数 */
    volatile uint32_t reserved1;
    uint8_t  _pad3[32 - 4 * 4];

    uint8_t  data[FT_ALINK_RING_BYTES];
} ft_audio_link_t;

#define FT_ALINK    ((ft_audio_link_t *)FT_ALINK_BASE)
#define FT_ALINK2   ((ft_audio_link_t *)FT_ALINK2_BASE)   /* M5: M55->M33 */

#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1)
#define FT_ALINK_DCACHE_CLEAN(addr, size)     SCB_CleanDCache_by_Addr((uint32_t *)(addr), (int32_t)(size))
#define FT_ALINK_DCACHE_INVALID(addr, size)   SCB_InvalidateDCache_by_Addr((uint32_t *)(addr), (int32_t)(size))
#else
#define FT_ALINK_DCACHE_CLEAN(addr, size)     do { (void)(addr); (void)(size); } while (0)
#define FT_ALINK_DCACHE_INVALID(addr, size)   do { (void)(addr); (void)(size); } while (0)
#endif

/* 双 32B 对齐向上取整 (cache 维护要求地址/长度 32B 对齐) */
#define FT_ALINK_ALIGN_DOWN(v)  ((v) & ~31U)
#define FT_ALINK_ALIGN_UP(v)    (((v) + 31U) & ~31U)

/* producer 初始化 (仅 M33 启动早期调用一次; ring2 同此纪律) */
static inline void ft_alink_init_at(ft_audio_link_t *r)
{
    memset((void *)r, 0, sizeof(*r));
    r->capacity = FT_ALINK_RING_BYTES;
    r->volume_percent = 0xFFU;   /* 0xFF=无变化哨兵: 0 会被 M55 当"音量 0"应用导致静音 */
    __DMB();
    r->magic = FT_ALINK_MAGIC;
    FT_ALINK_DCACHE_CLEAN((uint32_t *)r, 128);
}

static inline void ft_alink_init(void)
{
    ft_alink_init_at(FT_ALINK);
    ft_alink_init_at(FT_ALINK2);   /* M5: 反向 ring 一并铺好 */
}

static inline int ft_alink_ready_at(const ft_audio_link_t *r)
{
    FT_ALINK_DCACHE_INVALID((uint32_t)r, 32);
    return r->magic == FT_ALINK_MAGIC;
}

static inline int ft_alink_ready(void)
{
    return ft_alink_ready_at(FT_ALINK);
}

/* 可读/可写字节数 (wr/rd 单调递增, 差值即占用) */
static inline uint32_t ft_alink_used(const ft_audio_link_t *r)
{
    FT_ALINK_DCACHE_INVALID((uint32_t)&r->wr, 32);
    FT_ALINK_DCACHE_INVALID((uint32_t)&r->rd, 32);
    return r->wr - r->rd;
}

static inline uint32_t ft_alink_space(const ft_audio_link_t *r)
{
    return r->capacity - ft_alink_used(r);
}

/* producer: 写入 (允许部分写), 返回写入字节数; 空间 0 时返回 0 由调用方决定重试 */
static inline uint32_t ft_alink_write(ft_audio_link_t *r, const uint8_t *data, uint32_t len)
{
    uint32_t space = ft_alink_space(r);
    uint32_t off, first;
    uint8_t *base;

    if (len > space) len = space;
    if (len == 0U) return 0U;

    off = r->wr % r->capacity;
    base = (uint8_t *)r->data;
    first = r->capacity - off;
    if (first > len) first = len;

    memcpy(base + off, data, first);
    FT_ALINK_DCACHE_CLEAN(base + FT_ALINK_ALIGN_DOWN(off),
                          FT_ALINK_ALIGN_UP(first + (off & 31U)));
    if (first < len)
    {
        memcpy(base, data + first, len - first);
        FT_ALINK_DCACHE_CLEAN(base, FT_ALINK_ALIGN_UP(len - first));
    }

    __DMB();
    r->wr += len;
    FT_ALINK_DCACHE_CLEAN((uint32_t)&r->wr, 32);
    return len;
}

/* consumer: 读取 (允许部分读), 返回读取字节数 */
static inline uint32_t ft_alink_read(ft_audio_link_t *r, uint8_t *out, uint32_t len)
{
    uint32_t used = ft_alink_used(r);
    uint32_t off, first;
    uint8_t *base;

    if (len > used) len = used;
    if (len == 0U) return 0U;

    off = r->rd % r->capacity;
    base = (uint8_t *)r->data;
    first = r->capacity - off;
    if (first > len) first = len;

    FT_ALINK_DCACHE_INVALID(base + FT_ALINK_ALIGN_DOWN(off),
                            FT_ALINK_ALIGN_UP(first + (off & 31U)));
    memcpy(out, base + off, first);
    if (first < len)
    {
        FT_ALINK_DCACHE_INVALID(base, FT_ALINK_ALIGN_UP(len - first));
        memcpy(out + first, base, len - first);
    }

    __DMB();
    r->rd += len;
    FT_ALINK_DCACHE_CLEAN((uint32_t)&r->rd, 32);
    return len;
}

#ifdef __cplusplus
}
#endif

#endif /* FEATHERTALK_AUDIO_LINK_H */
