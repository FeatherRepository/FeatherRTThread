/* bt_bond_store.c - M3': 蓝牙 link key 持久化 (M33 侧)
 *
 * 自定义 btstack_link_key_db: 内存表(≤8 条)为运行时真值, 共享块
 * (0x261D4000, 见 bond_shared.h) 为双核传输面, /flash 文件由 M55
 * 托管 (bond_persist.c)。
 *  - btstack init 时 (open) 等待 M55 铺好共享块 (有界), 载入内存表
 *  - put/delete 更新内存表后即序列化到共享块并 seq++ (M55 轮询到
 *    变化即写文件)
 * 时序契约: M33 的 bt_on 流程已有 "等 M55 IPC 上线 ≤10s" 的有界等待;
 * 本库 open 时再做 ≤3s 的 bond magic 等待 (通常 M55 在 BT init 前已
 * 完成铺块, 此等待为兜底)。等待超时按空库继续 (等同现状, 不劣化)。
 */
#include <rtthread.h>
#include <string.h>

#include <feathertalk/bond_shared.h>
#include "bluetooth.h"
#include "classic/btstack_link_key_db.h"
#include "btstack_util.h"

#define FT_BOND_STORE_ENTRIES 8
#define FT_BOND_WAIT_READY_MS 3000

typedef struct
{
    uint8_t used;
    bd_addr_t addr;
    link_key_t key;
    link_key_type_t type;
    uint8_t authenticated;
} bond_entry_t;

static bond_entry_t s_table[FT_BOND_STORE_ENTRIES];
static volatile uint32_t s_bond_seq;
static int s_loaded;

static bond_entry_t *find_entry(bd_addr_t addr)
{
    for (int i = 0; i < FT_BOND_STORE_ENTRIES; i++)
    {
        if (s_table[i].used && memcmp(s_table[i].addr, addr, 6) == 0)
            return &s_table[i];
    }
    return RT_NULL;
}

/* 内存表 -> 共享块 (M33 无 D-cache, cache 宏自动为空) */
static void bond_publish(void)
{
    ft_bond_entry_t entries[FT_BOND_STORE_ENTRIES];
    uint32_t count = 0;
    memset(entries, 0, sizeof(entries));
    for (int i = 0; i < FT_BOND_STORE_ENTRIES; i++)
    {
        if (!s_table[i].used) continue;
        memcpy(entries[count].bd_addr, s_table[i].addr, 6);
        memcpy(entries[count].link_key, s_table[i].key, 16);
        entries[count].key_type = (uint8_t)s_table[i].type;
        entries[count].authenticated = s_table[i].authenticated;
        count++;
    }
    s_bond_seq++;
    ft_bond_serialize(FT_BOND, entries, count, s_bond_seq);
    FT_BOND_DCACHE_CLEAN(FT_BOND_BASE, sizeof(ft_bond_shared_t));
}

/* 共享块 -> 内存表 (M55 铺好后调用) */
static void bond_load_from_shared(void)
{
    FT_BOND_DCACHE_INVALID(FT_BOND_BASE, sizeof(ft_bond_shared_t));
    if (!ft_bond_valid(FT_BOND)) return;
    uint32_t n = FT_BOND->count;
    for (uint32_t i = 0; i < n; i++)
    {
        const ft_bond_entry_t *e = (const ft_bond_entry_t *)&FT_BOND->entries[i];
        bond_entry_t *slot = find_entry(e->bd_addr);
        if (!slot)
        {
            for (int j = 0; j < FT_BOND_STORE_ENTRIES; j++)
            {
                if (!s_table[j].used) { slot = &s_table[j]; break; }
            }
        }
        if (!slot) break;
        slot->used = 1;
        memcpy(slot->addr, e->bd_addr, 6);
        memcpy(slot->key, e->link_key, 16);
        slot->type = (link_key_type_t)e->key_type;
        slot->authenticated = e->authenticated;
    }
    if (n) rt_kprintf("[BOND] loaded %lu link key(s) from shared block\n",
                      (unsigned long)n);
}

/* ---- btstack_link_key_db 接口 ---- */
static void db_open(void)
{
    /* 等共享块就绪 (M55 启动早期铺好; 通常此刻已就绪) */
    for (int i = 0; i < FT_BOND_WAIT_READY_MS / 50 && !ft_bond_valid(FT_BOND); i++)
        rt_thread_mdelay(50);
    bond_load_from_shared();
    s_bond_seq = FT_BOND->seq;
}

static void db_set_local_bd_addr(bd_addr_t addr) { (void)addr; }
static void db_close(void) {}

static int db_get_link_key(bd_addr_t addr, link_key_t key, link_key_type_t *type)
{
    bond_entry_t *e = find_entry(addr);
    if (!e) return 0;
    memcpy(key, e->key, 16);
    *type = e->type;
    return 1;
}

static void db_put_link_key(bd_addr_t addr, link_key_t key, link_key_type_t type)
{
    bond_entry_t *e = find_entry(addr);
    if (!e)
    {
        for (int i = 0; i < FT_BOND_STORE_ENTRIES; i++)
        {
            if (!s_table[i].used) { e = &s_table[i]; break; }
        }
        if (!e)
        {
            /* 满: 挤掉第 0 条 (最旧, 简单 LRU 后续再优化) */
            e = &s_table[0];
        }
    }
    e->used = 1;
    memcpy(e->addr, addr, 6);
    memcpy(e->key, key, 16);
    e->type = type;
    e->authenticated = 1;
    bond_publish();
}

static void db_delete_link_key(bd_addr_t addr)
{
    bond_entry_t *e = find_entry(addr);
    if (!e) return;
    memset(e, 0, sizeof(*e));
    bond_publish();
}

/* ---- 迭代器 (btstack 要求; 允许遍历中删除, 这里删除走表内标记) ---- */
static int s_iter_index;

static int db_iterator_init(btstack_link_key_iterator_t *it)
{
    (void)it;
    s_iter_index = 0;
    return 1;
}

static int db_iterator_next(btstack_link_key_iterator_t *it, bd_addr_t addr,
                            link_key_t key, link_key_type_t *type)
{
    (void)it;
    while (s_iter_index < FT_BOND_STORE_ENTRIES)
    {
        bond_entry_t *e = &s_table[s_iter_index++];
        if (e->used)
        {
            memcpy(addr, e->addr, 6);
            memcpy(key, e->key, 16);
            *type = e->type;
            return 1;
        }
    }
    return 0;
}

static void db_iterator_free(btstack_link_key_iterator_t *it) { (void)it; }

static const btstack_link_key_db_t s_bond_db = {
    .open = db_open,
    .set_local_bd_addr = db_set_local_bd_addr,
    .close = db_close,
    .get_link_key = db_get_link_key,
    .put_link_key = db_put_link_key,
    .delete_link_key = db_delete_link_key,
    .iterator_init = db_iterator_init,
    .iterator_get_next = db_iterator_next,
    .iterator_done = db_iterator_free,
};

/* bt_main.c: 替换 btstack_link_key_db_memory_instance() */
const btstack_link_key_db_t * bt_bond_store_instance(void)
{
    return &s_bond_db;
}

/* bt 启动流程调用: 等 M55 铺好 bond 块 (有界)。M55 无文件时发布空有效块,
 * 正常数百毫秒内通过; 超时按空库继续 (等同旧行为, 不劣化) */
void bt_bond_store_wait_ready(uint32_t timeout_ms)
{
    for (uint32_t waited = 0; waited < timeout_ms; waited += 50)
    {
        FT_BOND_DCACHE_INVALID(FT_BOND_BASE, sizeof(ft_bond_shared_t));
        if (ft_bond_valid(FT_BOND)) return;
        rt_thread_mdelay(50);
    }
    rt_kprintf("[BOND] shared block not ready in %lu ms, starting empty\n",
               (unsigned long)timeout_ms);
}

/* ---- M8.x: LE bond 持久化 ----
 * le_device_db (RAM) 为运行时真值; LE 段 (共享块 + M55 文件) 为传输面。
 * 载入: bt 启动时 (M55 铺块后) 把 LE 段灌回 le_device_db;
 * 发布: SM 配对完成/身份创建后把 le_device_db 镜像出去。
 * 结果: 重烧/重启后手机旧 bond 直接可用, 加密链路免重配 (双模根治项) */
#include "ble/le_device_db.h"

volatile uint32_t g_le_bond_diag[4]; /* [0]=imported [1]=published [2]=db_count [3]=load_status */

void ft_le_bond_load(void)
{
    /* 有界等待 M55 铺 LE 段 (与经典段同批发布; 通常数百毫秒内就绪) */
    for (int i = 0; i < 40; i++)
    {
        FT_BOND_DCACHE_INVALID((uint32_t)FT_BOND_LE, sizeof(ft_bond_le_shared_t));
        if (ft_bond_le_valid(FT_BOND_LE)) break;
        rt_thread_mdelay(50);
    }
    FT_BOND_DCACHE_INVALID((uint32_t)FT_BOND_LE, sizeof(ft_bond_le_shared_t));
    if (!ft_bond_le_valid(FT_BOND_LE))
    {
        g_le_bond_diag[3] = 1;   /* 段无效 (M55 未铺/版本不符): 空库继续 */
        rt_kprintf("[BOND] LE section invalid/absent, empty LE db\n");
        return;
    }
    uint32_t imported = 0;
    for (uint32_t i = 0; i < FT_BOND_LE->count; i++)
    {
        const volatile ft_bond_le_entry_t *e = &FT_BOND_LE->entries[i];
        if (!e->in_use) continue;
        sm_key_t irk, ltk;
        uint8_t rand8[8];
        for (int k = 0; k < 16; k++) { irk[k] = e->irk[k]; ltk[k] = e->ltk[k]; }
        for (int k = 0; k < 8; k++) rand8[k] = e->rand[k];
        int idx = le_device_db_add(e->addr_type, (const uint8_t *)e->addr, irk);
        if (idx < 0) continue;
        if (e->key_size > 0)
        {
            le_device_db_encryption_set(idx, e->ediv, rand8, ltk,
                                        e->key_size, e->authenticated, 0,
                                        e->secure_connection);
        }
        imported++;
    }
    g_le_bond_diag[0] = imported;
    rt_kprintf("[BOND] LE db restored %lu entr(y/ies) from shared block\n",
               (unsigned long)imported);
}

void ft_le_bond_publish(void)
{
    ft_bond_le_entry_t table[FT_BOND_LE_MAX_ENTRIES];
    memset(table, 0, sizeof(table));
    int n = le_device_db_count();
    if (n > FT_BOND_LE_MAX_ENTRIES) n = FT_BOND_LE_MAX_ENTRIES;
    uint32_t count = 0;
    for (int i = 0; i < n; i++)
    {
        int addr_type, key_size, authenticated, authorized, secure_connection;
        uint16_t ediv;
        uint8_t rand8[8];
        sm_key_t irk, ltk;
        bd_addr_t addr;
        le_device_db_info(i, &addr_type, addr, irk);
        le_device_db_encryption_get(i, &ediv, rand8, ltk, &key_size,
                                    &authenticated, &authorized, &secure_connection);
        ft_bond_le_entry_t *e = &table[count++];
        e->in_use = 1;
        e->addr_type = (uint8_t)addr_type;
        memcpy(e->addr, addr, 6);
        memcpy(e->irk, irk, 16);
        memcpy(e->ltk, ltk, 16);
        e->ediv = ediv;
        memcpy(e->rand, rand8, 8);
        e->key_size = (int8_t)key_size;
        e->authenticated = (uint8_t)authenticated;
        e->secure_connection = (uint8_t)secure_connection;
    }
    static uint32_t s_le_seq;
    s_le_seq++;
    ft_bond_le_serialize(FT_BOND_LE, table, count, s_le_seq);
    FT_BOND_DCACHE_CLEAN((uint32_t)FT_BOND_LE, sizeof(ft_bond_le_shared_t));
    g_le_bond_diag[1]++;
    g_le_bond_diag[2] = (uint32_t)n;
    rt_kprintf("[BOND] LE db published %lu entr(y/ies) (le_seq %lu)\n",
               (unsigned long)count, (unsigned long)s_le_seq);
}
