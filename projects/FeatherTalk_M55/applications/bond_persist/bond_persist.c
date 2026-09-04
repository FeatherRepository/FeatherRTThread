/* bond_persist.c - M3': 蓝牙配对持久化 (M55 侧宿主)
 *
 * M33 btstack 的 link key 表经共享块 (0x261D4000, bond_shared.h) 交给
 * 本服务落盘 /flash/bt_bond.bin; 启动时反向铺块 (文件 -> 共享块)。
 *
 * 时序:
 *   1. 等 /flash 可写 (文件系统挂载; 有界重试)
 *   2. 读文件 -> 有效则写入共享块 (clean cache) -> M33 btstack 载入
 *   3. 轮询共享块 seq (2s): 变化且 CRC 有效 -> 写 .tmp + rename 原子落盘
 *
 * 注意: 写共享块前 invalidate / 读前 invalidate (M55 D-cache, 同
 * audio_link.h 模式); 文件缺失/损坏按空库处理 (不劣化现状)。
 */
#include <rtthread.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <feathertalk/bond_shared.h>

#define BOND_FILE      "/flash/bt_bond.bin"
#define BOND_FILE_TMP  "/flash/bt_bond.tmp"
#define BOND_POLL_MS   2000

static rt_uint32_t s_last_seq;      /* 本侧已消费的 seq */

static int bond_read_file(ft_bond_shared_t *out)
{
    int fd = open(BOND_FILE, O_RDONLY | O_BINARY, 0);
    if (fd < 0) return -1;
    int rd = read(fd, out, sizeof(*out));
    close(fd);
    if (rd != (int)sizeof(*out)) return -1;
    if (!ft_bond_valid(out)) return -1;   /* magic/count/crc 校验 */
    return 0;
}

static int bond_write_file(const ft_bond_shared_t *in)
{
    int fd = open(BOND_FILE_TMP, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666);
    if (fd < 0) return -1;
    int wr = write(fd, in, sizeof(*in));
    close(fd);
    if (wr != (int)sizeof(*in)) return -1;
    unlink(BOND_FILE);                    /* FatFS rename 不覆盖已存在目标 */
    if (rename(BOND_FILE_TMP, BOND_FILE) != 0) return -1;
    return 0;
}

static void bond_persist_thread(void *param)
{
    (void)param;
    ft_bond_shared_t local;

    /* 1. 等 /flash 挂载: ENOENT = 文件系统在(仅文件不存在) = 就绪;
     *    其他错误 = 尚未挂载, 继续等 (最长 30s) */
    int ready = 0;
    for (int i = 0; i < 60; i++)
    {
        errno = 0;
        int fd = open(BOND_FILE, O_RDONLY | O_BINARY, 0);
        int err = errno;
        if (i % 4 == 0)
            rt_kprintf("[BOND] wait#%d: fd=%d errno=%d\n", i, fd, err);
        if (fd >= 0) { close(fd); ready = 1; break; }
        /* RT-Thread dfs 返回负 errno (-2 = ENOENT), 两种符号都认 */
        if (err == ENOENT || err == -ENOENT) { ready = 1; break; }
        rt_thread_mdelay(500);
    }
    rt_kprintf("[BOND] wait done, ready=%d\n", ready);
    if (!ready)
    {
        rt_kprintf("[BOND] /flash not ready, persistence disabled\n");
        return;
    }

    /* 2. 读文件 -> 铺共享块 (无文件也铺空有效块: 让 M33 的等待立即通过,
     *    否则 M33 每次冷启动都要吃满超时) */
    if (bond_read_file(&local) == 0)
    {
        memcpy((void *)FT_BOND->entries, local.entries, sizeof(local.entries));
        FT_BOND->count = local.count;
        FT_BOND->crc = local.crc;
        FT_BOND->magic = FT_BOND_MAGIC;
        FT_BOND->seq = local.seq;
        FT_BOND_DCACHE_CLEAN(FT_BOND_BASE, sizeof(ft_bond_shared_t));
        s_last_seq = local.seq;
        rt_kprintf("[BOND] restored %lu link key(s) from flash\n",
                   (unsigned long)local.count);
    }
    else
    {
        memset(&local, 0, sizeof(local));
        local.magic = FT_BOND_MAGIC;
        /* seq 从 0 起: M33 首次发布 seq=1 必然 != 本侧基线 0,
         * 否则两侧行独立计数会撞号 (实测撞在 1 -> 文件永不落盘) */
        local.crc = ft_bond_crc32(local.entries, sizeof(local.entries));
        memcpy((void *)FT_BOND, &local, sizeof(local));
        FT_BOND_DCACHE_CLEAN(FT_BOND_BASE, sizeof(ft_bond_shared_t));
        s_last_seq = 0;
        rt_kprintf("[BOND] no persisted file, published empty bond block (seq 0)\n");
    }

    /* 3. 轮询: M33 更新共享块 (seq 变化且 CRC 有效) -> 原子落盘 */
    rt_uint32_t poll = 0;
    while (1)
    {
        rt_thread_mdelay(BOND_POLL_MS);
        FT_BOND_DCACHE_INVALID(FT_BOND_BASE, sizeof(ft_bond_shared_t));
        poll++;
        if (poll % 5 == 0)
            rt_kprintf("[BOND] poll#%lu: seq=%u last=%u count=%u valid=%d\n",
                       (unsigned long)poll, (unsigned)FT_BOND->seq,
                       (unsigned)s_last_seq, (unsigned)FT_BOND->count,
                       ft_bond_valid(FT_BOND));
        if (!ft_bond_valid(FT_BOND)) continue;
        if (FT_BOND->seq == s_last_seq) continue;

        memcpy(&local, (const void *)FT_BOND, sizeof(local));
        if (bond_write_file(&local) == 0)
        {
            s_last_seq = local.seq;
            rt_kprintf("[BOND] persisted %lu link key(s) (seq %lu)\n",
                       (unsigned long)local.count, (unsigned long)local.seq);
        }
        else
        {
            rt_kprintf("[BOND] flash write failed\n");
        }
    }
}

static int bond_dump(int argc, char **argv)
{
    (void)argc; (void)argv;
    FT_BOND_DCACHE_INVALID(FT_BOND_BASE, sizeof(ft_bond_shared_t));
    rt_kprintf("[BOND] shared: magic=0x%08x seq=%u count=%u crc=0x%08x valid=%d\n",
               FT_BOND->magic, FT_BOND->seq, FT_BOND->count, FT_BOND->crc,
               ft_bond_valid(FT_BOND));
    for (uint32_t i = 0; i < FT_BOND->count && i < FT_BOND_MAX_ENTRIES; i++)
    {
        const ft_bond_entry_t *e = (const ft_bond_entry_t *)&FT_BOND->entries[i];
        rt_kprintf("  [%u] %02x:%02x:%02x:%02x:%02x:%02x type=%u\n", i,
                   e->bd_addr[5], e->bd_addr[4], e->bd_addr[3],
                   e->bd_addr[2], e->bd_addr[1], e->bd_addr[0],
                   e->key_type);
    }
    return 0;
}
MSH_CMD_EXPORT_ALIAS(bond_dump, bond_dump, M3x: dump shared bond block);

static int bond_persist_init(void)
{
    rt_thread_t t = rt_thread_create("bondp", bond_persist_thread, RT_NULL,
                                     2048, 25, 10);
    if (t == RT_NULL) return -1;
    rt_thread_startup(t);
    return 0;
}
INIT_APP_EXPORT(bond_persist_init);
