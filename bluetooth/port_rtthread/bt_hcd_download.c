/* bt_hcd_download.c - CYW55500A1 HCD 固件下载 (阻塞式, btstack 启动前执行)
 *
 * 协议移植自: reference/btstack-integration/COMPONENT_HCI-UART/common/cybt_prm.c
 * (Infineon 官方实现) — HCD 记录格式: [命令16LE][长度8][参数...], 逐条以厂商
 * 命令发送并等待 Command Complete (status=0); 末条 0xFC4E 启动固件, 之后延时
 * 250ms。CYW55500A1 无需 MiniDriver (直载 HCD)。
 *
 * 固件数据来自官方 bt-fw-ifx-cyw55500a1 子模块的
 * COMPONENT_wlbga_iPA_sLNA_ANT0_LHL_XTAL_IN/btfw.c (板选天线变体,
 * 与 AIROC 参考实现同一份, 519 条记录)。下载在 3M 自动波特率链路
 * 建立后执行 (见 bt_main.c 的 S0-S3 时序)。
 *
 * 本实现为阻塞式独立 H4 微分帧器 (btstack 运行前, 无并发)。
 */
#include <rtthread.h>
#include <string.h>
#include <stdint.h>

/* 官方固件数组 (btfw.c) */
extern const char brcm_patch_version[];
extern const uint8_t brcm_patchram_buf[];
extern const int brcm_patch_ram_length;

/* 来自 hal_uart_dma_rtthread.c 的裸接口 */
extern int  bt_uart_raw_open(uint32_t baud);
extern int  bt_uart_raw_write(const uint8_t *buf, uint16_t len);
extern int  bt_uart_raw_read(uint8_t *buf, uint16_t len, uint32_t timeout_ms);
extern void feathertalk_ipc_send_event(rt_uint32_t code);

#define HCD_CMD_WRITE_RAM        0xFC4C
#define HCD_CMD_LAUNCH_RAM       0xFC4E
#define HCD_LAUNCH_DELAY_MS      250
#define HCD_CMD_TIMEOUT_MS       3000

static uint32_t s_download_ms;

/* 读一个 HCI 事件; 匹配 Command Complete(0x0E) 的 opcode, 返回 status (-1=超时/错帧) */
static int wait_command_complete(uint16_t opcode)
{
    /* 计时用起点差值判断, 不用 deadline 回绕比较 (原实现条件写反,
     * 循环体从不执行, 每次调用都瞬时返回 -1 —— M1-M3 "模组无应答"
     * 的众多表象里就埋着这个 bug) */
    rt_tick_t start = rt_tick_get();
    uint8_t hdr = 0;

    while ((rt_tick_get() - start) < rt_tick_from_millisecond(HCD_CMD_TIMEOUT_MS))
    {
        int n = bt_uart_raw_read(&hdr, 1, 20);
        if (n <= 0) continue;
        if (hdr != 0x04)
        {
            continue;   /* 非 H4 事件帧: 丢弃 */
        }
        uint8_t code = 0, elen = 0;
        if (bt_uart_raw_read(&code, 1, 50) <= 0) continue;
        if (bt_uart_raw_read(&elen, 1, 50) <= 0) continue;

        uint8_t params[260];
        if (elen > 250) elen = 250;
        uint32_t total = 0;
        while (total < elen)
        {
            int r = bt_uart_raw_read(params + total, elen - total, 50);
            if (r <= 0) break;
            total += (uint32_t)r;
        }
        if (total != elen) continue;

        if (code == 0x0E && elen >= 4)
        {
            /* Command Complete: [num_pkts][opcode_lo][opcode_hi][status]... */
            uint16_t opc = params[1] | (params[2] << 8);
            uint8_t status = params[3];
            if (opc == opcode)
            {
                return status;
            }
        }
    }
    return -1;
}

static int send_hci_command(uint16_t opcode, uint8_t plen, const uint8_t *params)
{
    uint8_t hdr[4] = { 0x01,                      /* H4 command */
                       (uint8_t)(opcode & 0xFF),
                       (uint8_t)(opcode >> 8),
                       plen };
    bt_uart_raw_write(hdr, 4);
    if (plen)
    {
        bt_uart_raw_write(params, plen);
    }
    return wait_command_complete(opcode);
}

int bt_hcd_download_run(void)
{
    rt_tick_t t0 = rt_tick_get();

    rt_kprintf("[BT-hcd] download %s (%d bytes)\n",
               brcm_patch_version, brcm_patch_ram_length);

    /* 逐条发送 HCD 记录 (518 x WriteRAM + 1 x LaunchRAM);
       初始 HCI_Reset 由调用方在 3M 链路建立后显式完成 (见 bt_main.c) */
    uint32_t off = 0;
    uint32_t count = 0;
    while (off + 3 <= (uint32_t)brcm_patch_ram_length)
    {
        uint16_t cmd = brcm_patchram_buf[off] | (brcm_patchram_buf[off + 1] << 8);
        uint8_t  plen = brcm_patchram_buf[off + 2];
        int status;
        if (off + 3 + (uint32_t)plen > (uint32_t)brcm_patch_ram_length)
        {
            rt_kprintf("[BT-hcd] record overflow @%lu\n", (unsigned long)off);
            return -2;
        }
        status = send_hci_command(cmd, plen, &brcm_patchram_buf[off + 3]);
        if (status != 0)
        {
            rt_kprintf("[BT-hcd] record %lu (op %04x) failed status=%d\n",
                       (unsigned long)count, cmd, status);
            {
                extern volatile int s_bt_err;
                s_bt_err = 30 + (int)((count > 50) ? 50 : count);
                feathertalk_ipc_send_event(1000u + (rt_uint32_t)(count % 500));
                feathertalk_ipc_send_event(2000u + (rt_uint32_t)cmd);
                feathertalk_ipc_send_event(3000u + (rt_uint32_t)(status & 0xFF));
            }
            return -3;
        }
        if (cmd == HCD_CMD_LAUNCH_RAM)
        {
            rt_thread_mdelay(HCD_LAUNCH_DELAY_MS);
        }
        off += 3 + (uint32_t)plen;
        count++;
        if ((count % 128) == 0)
        {
            rt_kprintf("[BT-hcd] progress %lu records\n", (unsigned long)count);
        }
    }

    /* ticks → ms (原写法 rt_tick_from_millisecond(差值) 方向相反,
     * 仅在 tick=1000Hz 时数值碰巧相等) */
    s_download_ms = (uint32_t)(((rt_tick_get() - t0) * 1000U) / RT_TICK_PER_SECOND);
    rt_kprintf("[BT-hcd] download done: %lu records, %lu ms\n",
               (unsigned long)count, (unsigned long)s_download_ms);
    feathertalk_ipc_send_event(1100u + (rt_uint32_t)count);
    return 0;
}

/* 发送单条 HCI 命令并等待 CC (供 bt_main 的 Reset/波特率切换复用) */
int bt_hcd_send_raw_command(uint16_t opcode, uint8_t plen, const uint8_t *params)
{
    return send_hci_command(opcode, plen, params);
}
