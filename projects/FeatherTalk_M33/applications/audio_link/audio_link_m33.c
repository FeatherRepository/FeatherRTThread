/* audio_link_m33.c - A0 跨核音频数据面 M33 侧 (producer)
 *
 * 职责: 启动早期初始化共享 ring (ft_alink_init), 提供 producer 写接口
 * (后续 M4b 的 A2DP media handler 直接调 ft_audio_produce),
 * 以及 msh 回环自测命令 ft_audio_test。
 *
 * 门铃经 feathertalk_ipc_send_audio_db 登记, 由 IPC 线程统一发出
 * (pipe 唯一写者原则)。
 */
#include <rtthread.h>
#include <stdlib.h>
#include <string.h>

#include <feathertalk/audio_link.h>
#include "ipc/feathertalk_ipc.h"

/* 测试图案: 与绝对位置绑定的确定性字节 (M55 侧同函数校验) */
static rt_uint8_t ft_audio_pattern(rt_uint32_t pos)
{
    return (rt_uint8_t)(((pos * 2654435761u) >> 24) ^ (pos & 0xFFu));
}

/* producer 写入口 (M4b 起由 A2DP 路径调用): 返回写入字节数 */
rt_uint32_t ft_audio_produce(const rt_uint8_t *data, rt_uint32_t len)
{
    rt_uint32_t written = ft_alink_write(FT_ALINK, data, len);

    if (written > 0U)
    {
        FT_ALINK->seq++;
        feathertalk_ipc_send_audio_db(FT_ALINK->wr, 0U);
        FT_ALINK->stats_doorbell++;
    }
    return written;
}

static int ft_alink_init_early(void)
{
    ft_alink_init();
    return 0;
}
/* M33 先启并监督 M55: 在应用初始化阶段铺好 ring, M55 侧等 magic */
INIT_APP_EXPORT(ft_alink_init_early);

/* ---- msh 自测: ft_audio_test [blocks] [chunk] ---- */
static int ft_audio_test(int argc, char **argv)
{
    static rt_uint8_t buf[4096];
    rt_uint32_t blocks = 64;
    rt_uint32_t chunk = 4096;
    rt_uint32_t interval_ms = 0;   /* 块间限速: 模拟实时流 (48k/16/2ch 4096B≈21.3ms) */
    rt_uint32_t total, written_total = 0;
    rt_uint32_t tick_start;
    rt_uint32_t pos0;

    if (!ft_alink_ready())
    {
        rt_kprintf("ft_audio_test: ring not ready\n");
        return -1;
    }
    if (argc > 1) blocks = (rt_uint32_t)strtoul(argv[1], RT_NULL, 0);
    if (argc > 2) chunk = (rt_uint32_t)strtoul(argv[2], RT_NULL, 0);
    if (argc > 3) interval_ms = (rt_uint32_t)strtoul(argv[3], RT_NULL, 0);
    if (chunk == 0U || chunk > sizeof(buf)) chunk = sizeof(buf);

    total = blocks * chunk;
    pos0 = FT_ALINK->wr;   /* 图案与绝对位置绑定 */
    rt_kprintf("ft_audio_test: %lu blocks x %lu B = %lu B, pos0=%lu\n",
               (unsigned long)blocks, (unsigned long)chunk,
               (unsigned long)total, (unsigned long)pos0);

    tick_start = rt_tick_get();
    for (rt_uint32_t b = 0; b < blocks; b++)
    {
        for (rt_uint32_t i = 0; i < chunk; i++)
        {
            buf[i] = ft_audio_pattern(pos0 + b * chunk + i);
        }
        rt_uint32_t off = 0;
        while (off < chunk)
        {
            rt_uint32_t w = ft_alink_write(FT_ALINK, buf + off, chunk - off);
            if (w == 0U)
            {
                FT_ALINK->stats_overrun += chunk - off;
                rt_kprintf("ft_audio_test: ring full at block %lu (+%lu B dropped)\n",
                           (unsigned long)b, (unsigned long)(chunk - off));
                break;
            }
            off += w;
        }
        FT_ALINK->seq++;
        feathertalk_ipc_send_audio_db(FT_ALINK->wr, 0U);
        FT_ALINK->stats_doorbell++;
        written_total += (off == chunk) ? chunk : off;
        if (off != chunk) break;
        if (interval_ms > 0U)
        {
            rt_thread_mdelay(interval_ms);
        }
    }
    rt_uint32_t elapsed_ms = rt_tick_get() - tick_start;
    rt_kprintf("ft_audio_test: wrote %lu B in %lu ms (%lu KB/s)\n",
               (unsigned long)written_total, (unsigned long)elapsed_ms,
               elapsed_ms ? (unsigned long)(written_total / elapsed_ms) : 0UL);
    return 0;
}
MSH_CMD_EXPORT_ALIAS(ft_audio_test, ft_audio_test,
                     A0: write test pattern into cross-core audio ring);
