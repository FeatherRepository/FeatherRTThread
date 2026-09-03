/* audio_link_m55.c - A0 跨核音频数据面 M55 侧 (consumer)
 *
 * 职责: 等 M33 铺好 ring (magic) -> 门铃唤醒/50ms 兜底轮询 drain ->
 * A0 阶段逐字节校验测试图案并统计 (M4b 起此处换成 SBC 解码 -> sound0)。
 *
 * 线程约定沿用音频源 worker: 优先级 12 (低于 sound_thread 的 6),
 * drain 在任务上下文, 门铃处理只投递事件 (ISR 不做重活)。
 */
#include <rtthread.h>
#include <string.h>

#include <feathertalk/audio_link.h>
#include "ft_sbc_decode.h"   /* A0-2: fmt_gen 非 0 时切换到 SBC 帧解码 */

#define FT_ALINK_EVT_DBELL   0x01U
#define FT_ALINK_CHUNK       4096U
#define FT_ALINK_POLL_MS     50U

static struct rt_event  s_alink_event;
static rt_thread_t      s_alink_thread;
static rt_bool_t        s_alink_ready;
static rt_uint32_t      s_last_fmt_gen;   /* ring 流格式代际 (0 = 图案校验模式) */

/* 统计 (msh ft_audio_stats 可读) */
static rt_uint32_t s_stat_bytes;
static rt_uint32_t s_stat_blocks;
static rt_uint32_t s_stat_errors;
static rt_uint32_t s_stat_resync;
static rt_uint32_t s_stat_poll_wake;
static rt_uint32_t s_stat_doorbell_wake;
static rt_tick_t   s_first_wake_tick;
static rt_tick_t   s_last_wake_tick;

/* 与 M33 侧同一图案函数: 按绝对位置校验 */
static rt_uint8_t ft_audio_pattern(rt_uint32_t pos)
{
    return (rt_uint8_t)(((pos * 2654435761u) >> 24) ^ (pos & 0xFFu));
}

/* M55 IPC 接收循环的门铃钩子 (MSG_AUDIO_DB) */
void feathertalk_audio_doorbell(void)
{
    rt_event_send(&s_alink_event, FT_ALINK_EVT_DBELL);
}

static void ft_alink_consume(void)
{
    static rt_uint8_t buf[FT_ALINK_CHUNK];
    ft_audio_link_t *r = FT_ALINK;
    rt_uint32_t got;

    while ((got = ft_alink_read(r, buf, FT_ALINK_CHUNK)) > 0U)
    {
        rt_uint32_t base = r->rd - got;   /* 本次读块的绝对起始位置 */

        if (s_last_fmt_gen != 0U)
        {
            /* A0-2: SBC 流模式, 帧化字节流送解码器 */
            ft_sbc_feed(buf, got);
        }
        else
        {
            for (rt_uint32_t i = 0; i < got; i++)
            {
                if (buf[i] != ft_audio_pattern(base + i))
                {
                    s_stat_errors++;
                }
            }
        }
        s_stat_blocks++;
        s_stat_bytes += got;
        s_last_wake_tick = rt_tick_get();
    }
}

static void ft_alink_thread_entry(void *parameter)
{
    rt_event_init(&s_alink_event, "ftal", RT_IPC_FLAG_PRIO);
    (void)parameter;

    /* 等 M33 铺好 ring */
    while (!ft_alink_ready())
    {
        rt_thread_mdelay(100);
    }
    /* 初次同步: 从当前写位置开始 (启动前的残留不算数) */
    FT_ALINK->rd = FT_ALINK->wr;
    s_alink_ready = RT_TRUE;
    rt_kprintf("[A0] audio link consumer ready (ring %lu B @0x%08lx)\n",
               (unsigned long)FT_ALINK_RING_BYTES, (unsigned long)FT_ALINK_BASE);

    while (1)
    {
        rt_uint32_t recvd = 0;
        rt_err_t rc = rt_event_recv(&s_alink_event, FT_ALINK_EVT_DBELL,
                                    RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                                    rt_tick_from_millisecond(FT_ALINK_POLL_MS),
                                    &recvd);
        if (rc == RT_EOK)
        {
            s_stat_doorbell_wake++;
        }
        else
        {
            s_stat_poll_wake++;
        }

        if (!ft_alink_ready())
        {
            continue;
        }
        /* M33 复位重建 ring (wr 归零) -> 重新同步 */
        if (FT_ALINK->wr < FT_ALINK->rd)
        {
            FT_ALINK->rd = FT_ALINK->wr;
            s_stat_resync++;
            rt_kprintf("[A0] ring re-created, resync\n");
        }
        if (s_first_wake_tick == 0 && ft_alink_used(FT_ALINK) > 0U)
        {
            s_first_wake_tick = rt_tick_get();
        }
        /* A0-2/M4b: 流格式代际变化 -> fmt_rate 非 0 开流 (SBC 解码+sound0),
         * fmt_rate 为 0 则流结束 (M33 侧停流/断连), 关 sound0 释放 */
        FT_ALINK_DCACHE_INVALID(FT_ALINK_BASE, 32);
        if (FT_ALINK->fmt_gen != s_last_fmt_gen)
        {
            s_last_fmt_gen = FT_ALINK->fmt_gen;
            if ((s_last_fmt_gen != 0U) && (FT_ALINK->fmt_rate != 0U))
            {
                ft_sbc_stream_begin();
            }
            else if (s_last_fmt_gen != 0U)
            {
                ft_sbc_stream_end();
            }
        }
        ft_alink_consume();
    }
}

static int ft_alink_consumer_init(void)
{
    s_alink_thread = rt_thread_create("ft_alink", ft_alink_thread_entry, RT_NULL,
                                      2048, 12, 10);
    if (s_alink_thread != RT_NULL)
    {
        rt_thread_startup(s_alink_thread);
        return 0;
    }
    return -1;
}
INIT_APP_EXPORT(ft_alink_consumer_init);

static int ft_audio_stats(int argc, char **argv)
{
    ft_audio_link_t *r = FT_ALINK;
    rt_uint32_t elapsed_ms = 0;

    (void)argc; (void)argv;
    if (s_last_wake_tick > s_first_wake_tick && s_first_wake_tick != 0)
    {
        elapsed_ms = (rt_uint32_t)(s_last_wake_tick - s_first_wake_tick);
    }
    rt_kprintf("[A0] ready=%d ring used=%lu/%lu B\n",
               (int)s_alink_ready, (unsigned long)ft_alink_used(r),
               (unsigned long)r->capacity);
    rt_kprintf("[A0] m55: blocks=%lu bytes=%lu errors=%lu resync=%lu\n",
               (unsigned long)s_stat_blocks, (unsigned long)s_stat_bytes,
               (unsigned long)s_stat_errors, (unsigned long)s_stat_resync);
    rt_kprintf("[A0] wake: doorbell=%lu poll=%lu, span=%lu ms -> %lu KB/s\n",
               (unsigned long)s_stat_doorbell_wake, (unsigned long)s_stat_poll_wake,
               (unsigned long)elapsed_ms,
               elapsed_ms ? (unsigned long)(s_stat_bytes / elapsed_ms) : 0UL);
    rt_kprintf("[A0] m33: wr=%lu seq=%lu overrun=%lu doorbell=%lu\n",
               (unsigned long)r->wr, (unsigned long)r->seq,
               (unsigned long)r->stats_overrun, (unsigned long)r->stats_doorbell);
    return 0;
}
MSH_CMD_EXPORT_ALIAS(ft_audio_stats, ft_audio_stats,
                     A0: show cross-core audio link stats);
