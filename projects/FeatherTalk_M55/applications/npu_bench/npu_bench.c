/*
 * R0.5-2 Ethos-U55 NPU 通路实测（R3 LLM 生死闸门）
 *
 * 背景：R0.5-1 实测 CPU 读共享 HyperRAM(0x64400000) 只有 ~20MB/s，"LLM 权重常驻
 * 共享区 + CPU 全量流读"架构已判否（13MB int4 → 1.5 tok/s）。本测试回答最后一个
 * 问题：**NPU（Ethos-U55）有自己的 AXI 读通路，带宽是否高于 CPU 的 20MB/s？**
 * 若显著高于 → matmul 委托 NPU，R3 复活；若同级 → R3 降级为小模型方案。
 *
 * 实现路径（刻意最轻，避开三处坑）：
 *   1. **直接操作 NPU 寄存器**（基址 0x44600000），不链 DEEPCRAFT 中间件/TFLM
 *      —— 避免与 EI 的 TFLM 符号冲突、避免 IRQ/信号量集成（轮询 STATUS 完成位）；
 *   2. **权重不占 Flash**：权重内容对带宽测试无意义，板上按模式现场生成
 *      （NPU 照样要读完每个权重字节——测的就是这个）；只有 Vela 命令流（几十 KB）
 *      以 C 数组驻留 Flash；
 *   3. DCache 维护：写权重后 SCB_CleanDCache_by_Addr 再启动 NPU。
 *
 * 寄存器与初始化值来自 PDL COMPONENT_U55（ethosu55_interface.h / ethosu_dev_axi_init）。
 *
 * 用法：
 *   npu_id                      # 读 ID/CONFIG(MACS)/STATUS，确认 NPU 活着且通电
 *   npu_bench m55               # 权重放 M55 专区 0x64240000（NPU 读 HyperRAM 快路径）
 *   npu_bench hram              # 权重放共享 HyperRAM 0x64C00000（R3 生死数：CPU 仅 20MB/s）
 *   npu_bench flash             # 权重用固件 const 数组 XIP
 *   npu_bench shared / dtcm     # 小模型（<=216KB / <=1.4KB）放 SoC SRAM / DTCM
 */

#include <rtthread.h>
#include <string.h>
#include "board.h"
#include "cy_sysclk.h"
#include "system_edge.h"

/* ---------------- NPU 寄存器（偏移见 PDL ethosu55_interface.h） ---------------- */
#define NPU_BASE              0x44600000UL
#define NPU_REG(off)          (*(volatile uint32_t *)(NPU_BASE + (off)))
#define NPU_ID                NPU_REG(0x00)
#define NPU_STATUS            NPU_REG(0x04)
#define NPU_CMD               NPU_REG(0x08)
#define NPU_RESET             NPU_REG(0x0C)
#define NPU_QBASE             NPU_REG(0x10)
#define NPU_QREAD             NPU_REG(0x18)
#define NPU_QCONFIG           NPU_REG(0x1C)
#define NPU_QSIZE             NPU_REG(0x20)
#define NPU_PROT              NPU_REG(0x24)
#define NPU_CONFIG            NPU_REG(0x28)
#define NPU_REGIONCFG         NPU_REG(0x3C)
#define NPU_AXI_LIMIT0        NPU_REG(0x40)
#define NPU_AXI_LIMIT1        NPU_REG(0x44)
#define NPU_AXI_LIMIT2        NPU_REG(0x48)
#define NPU_AXI_LIMIT3        NPU_REG(0x4C)
/* BASEP 步进 8 字节！basep_r 是 uint32_t word[2]（64 位寄存器，U55 只用低 32 位）。
 * 教训 10-21：原来写成 4 字节步进，BASEP(1) 的 scratch 基址落进了 0x84（BASEP[0]
 * 的高字，被硬件忽略），真正的 BASEP[1] 保持 0 → IFM 从 0x20 读 → bus abort
 * （STATUS=0xffff1006, faulting_channel=1=IFM, QREAD=288）。换任何内存布局都
 * 一样失败，因为跟内存位置根本无关。 */
#define NPU_BASEP(i)          NPU_REG(0x80 + 8 * (i))

/* STATUS 位（ethosu55_interface.h status_r） */
#define ST_BUS_STATUS         (1UL << 2)
#define ST_RESET_STATUS       (1UL << 3)
#define ST_CMD_PARSE_ERROR    (1UL << 4)
#define ST_CMD_END_REACHED    (1UL << 5)
#define ST_WD_FAULT           (1UL << 7)
#define ST_ECC_FAULT          (1UL << 8)
#define ST_ERR_MASK           (ST_BUS_STATUS | ST_CMD_PARSE_ERROR | ST_WD_FAULT | ST_ECC_FAULT)

/* CMD 位 */
#define CMD_TRANSITION_RUN    (1UL << 0)    /* transition_to_running_state */
#define CMD_CLEAR_IRQ         (1UL << 1)    /* clear_irq */
#define CMD_CLOCK_Q_ENABLE    (1UL << 2)    /* clock_q_enable：使能 NPU 时钟 q 接口 */
#define CMD_POWER_Q_ENABLE    (1UL << 3)    /* power_q_enable：使能 NPU 电源 q 接口 */
#define NPU_CMD_PWR_CLK_MASK  (CMD_CLOCK_Q_ENABLE | CMD_POWER_Q_ENABLE)   /* 0xC：保留电源/时钟位的掩码（驱动同款） */

/* 初始化值（严格对齐 PDL ethosu_dev_init 的官方默认，见 ethosu_config.h） */
#define NPU_QCONFIG_INIT           2UL
/* REGIONCFG: 每个 2-bit 字段选 AXI 口。官方默认 region0=3(AXI3 权重)、region1=0(AXI0)、
 * region2..7=1(AXI1)。10-20 修正：原来写成 0x5557 把 region1 错配到 AXI1。 */
#define NPU_REGIONCFG_VENDOR       0x5553UL
#define NPU_REGIONCFG_ALL_AXI0     0x0000UL    /* 全部走 AXI0（若 SoC 只接了 AXI0 这就是解） */
#define NPU_REGIONCFG_ALL_AXI1     0x5555UL    /* 全部走 AXI1 */
#define NPU_REGIONCFG_INIT         NPU_REGIONCFG_VENDOR
/* AXI_LIMIT: bits[1:0]=max_beats(B64=0/B128=1/B256=2), [7:4]=memtype,
 * [20:16]=max_outstanding_read-1, [27:24]=max_outstanding_write-1。
 * 官方默认 reads=32→31, writes=16→15, max_beats=B64。
 * 10-20 修正：原来错写成 <<12/<<20（落进保留位），导致 reads 实际=17、writes=1。
 * 10-21：argv[3]=b128 扫 max_beats=B128 看 burst 宽度能否再提带宽（两区都卡
 * ~89MB/s，疑似 HyperRAM 通路到顶，但值得一试）。 */
static rt_uint32_t g_axi_max_beats = 0UL;   /* B64；b128 → 1(B128) */
#define NPU_AXI_LIMIT_VAL          ((31UL << 16) | (15UL << 24) | g_axi_max_beats)

/* ---------------- U55 PPU 上电（教训：不使能 PPU 读 NPU 寄存器直接 bus fault） ---------- */
/* U55_Type: MXU55_PPU @ 0x44600000+0x2000；PWPR.PWR_POLICY 见 ppu_v1.h，ON = 8 */
#define PPU_U55_BASE       (NPU_BASE + 0x2000UL)
#define PPU_U55_PWPR       (*(volatile uint32_t *)(PPU_U55_BASE + 0x00))
#define PPU_U55_UNLK       (*(volatile uint32_t *)(PPU_U55_BASE + 0x1C))
#define PPU_U55_PWSR       (*(volatile uint32_t *)(PPU_U55_BASE + 0x08))
#define PPU_V1_MODE_ON     8UL

static void npu_power_on(void)
{
    rt_uint32_t i;

    /* 官方使能序列（system_edge.c: Cy_SysEnableU55）：
     * 1) Cy_SysClk_PeriGroupSlaveInit —— MXU550 peri 路由 + 自动使能 CLK_HF1
     * 2) cy_pd_ppu_set_power_mode(PPU ON)  3) cy_pd_pdcm_set_dependency(PD 依赖)
     * 只做 1) 不够：U550 块读访问仍 bus fault（PPU/PD 未使能）。
     * 教训 10-19：手写 PPU PWPR 无效，必须走完整序列尤其是 PDCM 依赖。 */
    Cy_SysEnableU55(true);

    /* 等 PPU 状态反映 ON（PWSR 低 4 位 = 模式，ON=8） */
    for (i = 0; i < 1000; i++)
    {
        if ((PPU_U55_PWSR & 0xFUL) == PPU_V1_MODE_ON)
        {
            return;
        }
        rt_thread_mdelay(1);
    }
    rt_kprintf("[npu] U55 上电超时: PWSR=0x%08x（NPU 仍不可访问）\n", PPU_U55_PWSR);
}

/* ---------------- Vela 模型数据（generated: tools/emit_model_c.py 勿手改） ---------------- */
extern const unsigned char npu_cmd_stream[];
extern const unsigned int  npu_cmd_stream_len;
extern const unsigned char npu_weight_blob[];
extern const unsigned int  npu_weights_len;    /* Vela 权重块字节数（非 n*n） */
extern const unsigned int  npu_scratch_len;    /* Vela 报告的 scratch 字节数 */
extern const unsigned int  npu_input_len;      /* 输入/输出张量字节数 */

/* BASEP 索引（Vela 内存规划，按生成脚本输出核对） */
#ifndef NPU_BASEP_WEIGHTS
#define NPU_BASEP_WEIGHTS   0
#endif
#ifndef NPU_BASEP_SCRATCH
#define NPU_BASEP_SCRATCH   1
#endif
#ifndef NPU_BASEP_INPUT
#define NPU_BASEP_INPUT     2
#endif
#ifndef NPU_BASEP_OUTPUT
#define NPU_BASEP_OUTPUT    3
#endif

/* M55 专区摆放（WiFi lwIP 池在 0x64200000 起 ~0x25900，往后 +256KB 避让） */
#define HRAM_M55_BASE       0x64200000UL
#define HRAM_M55_SAFE       (HRAM_M55_BASE + 0x40000UL)

/* 共享 HyperRAM 12MB 映射里，M55 动态堆只配了前 8MB（0x64400000..0x64BFFFFF），
 * 0x64C00000 起 4MB 已映射但未分配 —— R0.5-2 测 NPU 读共享区带宽就放这。
 * （R3 生死数：CPU 读共享区仅 20MB/s，NPU 的 AXI 通路是否更快？） */
#define HRAM_SHARED_SAFE    0x64C00000UL
#define HRAM_SHARED_SAFE_SZ (4UL * 1024 * 1024)

/* ---- 内存布局（教训 10-20）----
 * NPU 的 AXI 主**读不到 M55 的 DTCM**（实测 STATUS bit11=IFM 通道 bus abort），
 * 所以命令流/scratch 不能放 0x20000000 的核内 TCM，必须放 SoC SRAM。
 * SoC SRAM：0x26060000 起 1.375MB；已用 ~85KB，其余为 .heap 段（实测可写）。 */
/* 教训 10-20：0x26080000 起是 .heap 段但**有活数据**（写入后系统崩），不可借用。
 * 改用 m33_m55_shared 尾部空闲段：0x261C8000..0x26200000 = 224KB。 */
#define SOC_CMD_ADDR        0x261C8000UL   /* 命令流 328B */
#define SOC_SCRATCH_ADDR    0x261C9000UL   /* scratch 2KB */
#define SOC_WEIGHTS_ADDR    0x261CA000UL   /* 权重块（<= 216KB） */

/* m33_m55_shared（0x261C0000, 256KB）：音频环占前 32KB，0x261C8000 起 ~224KB 可借用 */
#define SHARED_SRAM_SAFE    0x261CA000UL  /* 不得与 cmd(0x261C8000) 重叠 */

/* DTCM 静态区（唯一被 NPU 成功读取的区域）：命令流+scratch+小权重（<=8KB） */
static rt_uint8_t s_dtcm_cmd[512] __attribute__((aligned(16)));
static rt_uint8_t s_dtcm_scratch[512] __attribute__((aligned(16)));
static rt_uint8_t s_dtcm_weights[4096] __attribute__((aligned(16)));

#define BENCH_RUNS          20
#define BENCH_POLL_TIMEOUT  2000    /* ms，单次 invoke 轮询上限 */

static volatile int npu_busy_flag;   /* 仅防 msh 重入 */
static rt_uint32_t g_regioncfg = NPU_REGIONCFG_INIT;   /* REGIONCFG 扫描开关 */


/* NPU 硬性要求：QBASE / BASEP 必须 16 字节对齐（不对齐 → STATUS cmd_parse_error）。
 * rt_malloc 只保证 8 字节，这里手动对齐（多申请 16 字节，泄漏可忽略，基准用）。 */
static rt_uint8_t *alloc16(rt_uint32_t bytes, rt_uint8_t **raw_out)
{
    rt_uint8_t *p = (rt_uint8_t *)rt_malloc(bytes + 16);
    if (p == RT_NULL)
    {
        *raw_out = RT_NULL;
        return RT_NULL;
    }
    *raw_out = p;   /* 释放时必须用原始指针，否则 memheap 断言失败 */
    return (rt_uint8_t *)(((rt_uint32_t)p + 15UL) & ~15UL);
}

static void npu_regs_init(void)
{
    NPU_QCONFIG   = NPU_QCONFIG_INIT;
    NPU_REGIONCFG = g_regioncfg;
    NPU_AXI_LIMIT0 = NPU_AXI_LIMIT_VAL;
    NPU_AXI_LIMIT1 = NPU_AXI_LIMIT_VAL;
    NPU_AXI_LIMIT2 = NPU_AXI_LIMIT_VAL;
    NPU_AXI_LIMIT3 = NPU_AXI_LIMIT_VAL;
}

/* 软复位（驱动 ethosu_dev_soft_reset 同款）：
 * RESET.pending_CPL=1(Privileged) / pending_CSL=1(NonSecure) —— 与 DEEPCRAFT 默认
 * (SECURITY_ENABLE=0, PRIVILEGE_ENABLE=1) 一致；复位后必须重设 AXI/QCONFIG。
 * 不软复位直接跑 → STATUS.cmd_parse_error 粘滞（实测 0x11/0x12）。
 *
 * 10-21 关键修复：复位后必须补写 CMD 的 clock_q_enable/power_q_enable（官方
 * ethosu_request_power 流程）。缺失时 NPU 数据通路（IFM/OFM/权重 DMA）不动作：
 * 实测 STATUS=0xffff1006（bus_status=1, faulting_channel=1=IFM），命令流能解析到
 * NPU_OP_CONV（QREAD=288/296）但 IFM DMA 一发起就总线中止，scratch 回读全 0。
 * 只读寄存器/解析命令不需要这两个位，跑数据要 —— 纯粹的 "缺一步官方序列"。 */
static int npu_soft_reset(void)
{
    rt_uint32_t i;

    NPU_RESET = 0x3;   /* bit0 pending_CPL=1, bit1 pending_CSL=1 */
    for (i = 0; i < 100000; i++)
    {
        if ((NPU_STATUS & ST_RESET_STATUS) == 0)
        {
            break;
        }
    }
    if (NPU_STATUS & ST_RESET_STATUS)
    {
        rt_kprintf("[npu] 软复位超时: STATUS=0x%08x\n", NPU_STATUS);
        return -1;
    }
    npu_regs_init();
    /* 官方 ethosu_request_power：软复位后显式开 NPU 时钟/电源请求 */
    NPU_CMD = CMD_CLOCK_Q_ENABLE | CMD_POWER_Q_ENABLE;
    return 0;
}

/* STATUS 故障通道解码：bits[15:12]=faulting_channel（读）：0=Cmd 1=IFM 2=Weights
 * 3=Scale+Bias 4=Mem2Mem 8=OFM 9=Mem2Mem；bit11=faulting_interface */
static const char *npu_fault_channel(rt_uint32_t st)
{
    rt_uint32_t ch = (st >> 12) & 0xF;
    if (!(st & ST_BUS_STATUS))
    {
        return "";
    }
    switch (ch)
    {
    case 0: return " (fault: Cmd 通道)";
    case 1: return " (fault: IFM 输入通道)";
    case 2: return " (fault: Weights 权重通道)";
    case 3: return " (fault: Scale+Bias 通道)";
    case 8: return " (fault: OFM 输出通道)";
    default: return " (fault: 未知通道)";
    }
}

/* 每次 invoke 的基址（官方驱动 run_command_stream 也是每次全写一遍） */
static rt_uint8_t *g_cmd_buf, *g_w_buf, *g_sc_buf;

/* 启动一次并轮询完成；返回 0=成功，负=错误码。
 * 每次都要重写 QBASE/QSIZE/BASEP —— STATUS.cmd_end_reached 粘滞到下次写 QBASE/QSIZE
 * 才清（官方驱动 run_command_stream 也是每次 invoke 全写一遍）。漏写 → 第 2 轮起
 * 轮询立刻看到上次的 cmd_end_reached 空转返回（实测 20 轮 1ms 假象，10-21）。 */
static int npu_run_once(void)
{
    rt_uint32_t t0 = rt_tick_get_millisecond();

    NPU_QBASE = (uint32_t)g_cmd_buf;
    NPU_QSIZE = npu_cmd_stream_len;
    NPU_BASEP(NPU_BASEP_WEIGHTS) = (uint32_t)g_w_buf;
    NPU_BASEP(NPU_BASEP_SCRATCH) = (uint32_t)g_sc_buf;
    NPU_CMD = (NPU_CMD & NPU_CMD_PWR_CLK_MASK) | CMD_TRANSITION_RUN;

    for (;;)
    {
        rt_uint32_t st = NPU_STATUS;
        if (st & ST_ERR_MASK)
        {
            rt_kprintf("[npu] STATUS 错误: 0x%08x QREAD=%u QBASE=0x%08x QSIZE=%u CMD=0x%x%s\n",
                       st, NPU_QREAD, NPU_QBASE, NPU_QSIZE, NPU_CMD,
                       npu_fault_channel(st));
            return -1;
        }
        if (st & ST_CMD_END_REACHED)
        {
            return 0;
        }
        if ((rt_tick_get_millisecond() - t0) > BENCH_POLL_TIMEOUT)
        {
            rt_kprintf("[npu] 超时: STATUS=0x%08x QREAD=%u\n", NPU_STATUS, NPU_QREAD);
            return -2;
        }
    }
}

static void fill_pattern(rt_uint8_t *p, rt_uint32_t n)
{
    /* 权重块只能来自 Vela（自有编码，含 scale/bias），不能现场生成。
     * 本函数已废——留个空实现防误调。 */
    (void)p; (void)n;
}

static int npu_bench(int argc, char **argv)
{
    /* 模型数据缺失（未跑生成链，stub 的 len=0）时直接提示，不让 NPU 空转 */
    if (npu_cmd_stream_len == 0 || npu_weights_len == 0)
    {
        rt_kprintf("[npu] 无模型数据：先跑 tools/gen_tflite_flat.py + Vela + "
                   "tools/emit_model_c.py 生成 npu_model.c（见 README.md）\n");
        return -1;
    }
    rt_bool_t use_m55_region = RT_FALSE;
    rt_bool_t use_soc_region = RT_FALSE;
    rt_bool_t use_shared_region = RT_FALSE;
    rt_bool_t use_dtcm_region = RT_FALSE;
    rt_bool_t use_flash_region = RT_FALSE;
    rt_bool_t use_hram_region = RT_FALSE;
    rt_uint8_t *cmd_buf, *w_buf, *sc_buf, *in_buf, *out_buf;
    rt_uint8_t *cmd_raw = RT_NULL, *w_raw = RT_NULL, *sc_raw = RT_NULL;
    int run;
    int fail = 0;

    if (argc >= 2 && !strcmp(argv[1], "m55"))
    {
        use_m55_region = RT_TRUE;
    }
    else if (argc >= 2 && !strcmp(argv[1], "soc"))
    {
        use_soc_region = RT_TRUE;
    }
    else if (argc >= 2 && !strcmp(argv[1], "shared"))
    {
        use_shared_region = RT_TRUE;
    }
    else if (argc >= 2 && !strcmp(argv[1], "dtcm"))
    {
        use_dtcm_region = RT_TRUE;
    }
    else if (argc >= 2 && !strcmp(argv[1], "flash"))
    {
        use_flash_region = RT_TRUE;   /* 权重直接用固件里的 const 数组（XIP Flash） */
    }
    else if (argc >= 2 && !strcmp(argv[1], "hram"))
    {
        use_hram_region = RT_TRUE;    /* 权重放共享 HyperRAM 未分配区（0x64C00000） */
    }

    /* argv[2]：REGIONCFG 扫描（vendor|axi0|axi1） */
    if (argc >= 3)
    {
        if (!strcmp(argv[2], "axi0"))
        {
            g_regioncfg = NPU_REGIONCFG_ALL_AXI0;
        }
        else if (!strcmp(argv[2], "axi1"))
        {
            g_regioncfg = NPU_REGIONCFG_ALL_AXI1;
        }
        else if (!strcmp(argv[2], "b128"))
        {
            g_axi_max_beats = 1UL;   /* AXI burst 宽度 B64→B128 */
        }
        else
        {
            g_regioncfg = NPU_REGIONCFG_VENDOR;
        }
    }

    if (npu_busy_flag)
    {
        rt_kprintf("[npu] 上一次还没结束\n");
        return -1;
    }
    npu_busy_flag = 1;
    npu_power_on();

    /* ---- 布置缓冲区：命令流/scratch 固定放 SoC SRAM（NPU 读不到 M55 DTCM，
     *      实测 STATUS bit11=IFM 通道 bus abort）；输入/输出在 scratch 内部 ---- */
    cmd_buf = (rt_uint8_t *)SOC_CMD_ADDR;
    sc_buf  = (rt_uint8_t *)SOC_SCRATCH_ADDR;
    if (use_dtcm_region)
    {
        if (npu_cmd_stream_len > sizeof(s_dtcm_cmd) ||
            npu_scratch_len > sizeof(s_dtcm_scratch) ||
            npu_weights_len > sizeof(s_dtcm_weights))
        {
            rt_kprintf("[npu] DTCM 模式只支持小模型（cmd<=512, scratch<=1K, weights<=8K）\n");
            npu_busy_flag = 0;
            return -1;
        }
        cmd_buf = s_dtcm_cmd;
        sc_buf  = s_dtcm_scratch;
    }
    in_buf  = RT_NULL;   /* 输入/输出在 scratch 内（output@+0、input@+1024） */
    out_buf = RT_NULL;

    if (use_flash_region)
    {
        w_buf = (rt_uint8_t *)npu_weight_blob;   /* Flash XIP，不拷贝 */
    }
    else if (use_hram_region)
    {
        if (npu_weights_len > HRAM_SHARED_SAFE_SZ)
        {
            rt_kprintf("[npu] 权重块 %u 字节 > 共享区未分配区 %lu 字节\n",
                       npu_weights_len, HRAM_SHARED_SAFE_SZ);
            npu_busy_flag = 0;
            return -1;
        }
        w_buf = (rt_uint8_t *)HRAM_SHARED_SAFE;
    }
    else if (use_dtcm_region)
    {
        w_buf = s_dtcm_weights;
    }
    else if (use_m55_region)
    {
        /* WiFi lwIP 池在 0x64200000 起 ~0x25900；从 +256KB 起放，可用 2MB-256KB */
        if (npu_weights_len > (2UL * 1024 * 1024 - 0x40000))
        {
            rt_kprintf("[npu] 权重块 %u 字节 > M55 专区可用 %lu 字节\n",
                       npu_weights_len, 2UL * 1024 * 1024 - 0x40000);
            npu_busy_flag = 0;
            return -1;
        }
        w_buf = (rt_uint8_t *)HRAM_M55_SAFE;
    }
    else if (use_shared_region)
    {
        if (npu_weights_len > 0x00038000UL)   /* 0x261C8000..0x26200000 = 224KB */
        {
            rt_kprintf("[npu] 权重块 %u 字节 > 共享 SRAM 借用区 224KB\n", npu_weights_len);
            npu_busy_flag = 0;
            return -1;
        }
        w_buf = (rt_uint8_t *)SHARED_SRAM_SAFE;
    }
    else
    {
        /* 默认：m33_m55_shared 空闲段（0x261CA000..0x26200000 ≈ 216KB 可用） */
        if (npu_weights_len > 0x00036000UL)
        {
            rt_kprintf("[npu] 权重块 %u 字节 > 共享 SRAM 借用区 216KB\n", npu_weights_len);
            npu_busy_flag = 0;
            return -1;
        }
        w_buf = (rt_uint8_t *)SOC_WEIGHTS_ADDR;
    }

    rt_kprintf("[npu] 权重块 %u 字节放 %s（0x%08x），命令流 %u 字节，scratch %u 字节\n",
               npu_weights_len,
               use_flash_region ? "Flash XIP" :
               use_dtcm_region ? "DTCM(静态)" :
               use_m55_region ? "M55专区(0x64240000)" :
               use_hram_region ? "共享HyperRAM(0x64C00000)" :
               use_shared_region ? "共享SRAM(0x261C8000)" : "共享SRAM(0x261CA000)",
               (rt_uint32_t)w_buf, npu_cmd_stream_len, npu_scratch_len);

    /* ---- 填数据 + DCache 维护（NPU 是总线主，读的是内存不是缓存） ---- */
    rt_memcpy(cmd_buf, npu_cmd_stream, npu_cmd_stream_len);
    if (!use_flash_region)
    {
        rt_memcpy(w_buf, npu_weight_blob, npu_weights_len);
        SCB_CleanDCache_by_Addr((uint32_t *)w_buf, (int32_t)npu_weights_len);
    }
    rt_memset(sc_buf, 0, npu_scratch_len);   /* 输出@+0、输入@+1024，全 0 即可 */
    SCB_CleanDCache_by_Addr((uint32_t *)cmd_buf, (int32_t)npu_cmd_stream_len);
    SCB_CleanDCache_by_Addr((uint32_t *)sc_buf, (int32_t)npu_scratch_len);

    /* ---- 软复位 + 寄存器初始化；基址存全局，由 npu_run_once 每次 invoke 写入 ---- */
    if (npu_soft_reset() != 0)
    {
        rt_kprintf("[npu] 软复位失败，放弃\n");
        npu_busy_flag = 0;
        return -1;
    }
    g_cmd_buf = cmd_buf;
    g_w_buf   = w_buf;
    g_sc_buf  = sc_buf;
    rt_kprintf("[npu] PROT=0x%08x(exp 0x3) QCONFIG=0x%08x REGIONCFG=0x%08x CMD=0x%x(exp 0xc)\n",
               NPU_PROT, NPU_QCONFIG, NPU_REGIONCFG, NPU_CMD);
    rt_kprintf("[npu] QBASE=0x%08x BASEP0(w)=0x%08x BASEP1(sc)=0x%08x\n",
               (rt_uint32_t)cmd_buf, (rt_uint32_t)w_buf, (rt_uint32_t)sc_buf);

    /* ---- 跑 N 次 ---- */
    {
        rt_uint32_t t0, t1;
        rt_uint32_t ok = 0;
        rt_uint32_t total_bytes = npu_weights_len;

        /* 第一次单独跑：验证链路通（不计时） */
        run = npu_run_once();
        if (run != 0)
        {
            rt_kprintf("[npu] 首次 invoke 失败（链路/命令流/基址有问题）\n");
            fail = 1;
        }
        else
        {
            rt_kprintf("[npu] 首次 invoke OK，开始计时 %d 轮...\n", BENCH_RUNS);
            t0 = rt_tick_get_millisecond();
            for (run = 0; run < BENCH_RUNS; run++)
            {
                if (npu_run_once() != 0)
                {
                    fail = 1;
                    break;
                }
                ok++;
            }
            t1 = rt_tick_get_millisecond();

            if (ok > 0)
            {
                rt_uint32_t ms = t1 - t0;
                rt_uint32_t kbps;   /* KB/s */
                if (ms == 0) ms = 1;
                kbps = (ok * (total_bytes / 1024) * 1000) / ms;
                rt_kprintf("[npu] %u 次 OK，%u ms（均 %u.%03u ms/次）\n",
                           ok, ms, ms / ok, (ms * 1000 / ok) % 1000);
                rt_kprintf("[npu] 权重流读带宽 = %u.%03u MB/s（%u 字节权重 x %u 次 / %u ms）\n",
                           kbps / 1000, kbps % 1000, npu_weights_len, ok, ms);
                rt_kprintf("[npu] 对比基线：CPU 读共享区 20MB/s / 专区 79MB/s\n");
            }
        }
    }

    /* ---- 清理 ---- */
    NPU_QBASE = 0;
    NPU_QSIZE = 0;
    /* 固定地址布局，无需释放。回读 scratch 判断 NPU 到底写没写进去： */
    {
        volatile rt_uint8_t *sp = sc_buf;
        rt_uint32_t k;
        rt_kprintf("[npu] scratch 回读（输出@+0）:");
        for (k = 0; k < 16; k++)
        {
            rt_kprintf(" %02x", sp[k]);
        }
        rt_kprintf("\n");
    }
    npu_busy_flag = 0;
    return fail ? -1 : 0;
}

static int npu_id(int argc, char **argv)
{
    (void)argc; (void)argv;
    npu_power_on();
    rt_kprintf("PPU PWSR     = 0x%08x (mode=%u)\n", PPU_U55_PWSR, (unsigned)(PPU_U55_PWSR & 0xF));
    rt_kprintf("NPU ID       = 0x%08x\n", NPU_ID);
    rt_kprintf("NPU CONFIG   = 0x%08x (MACS_PER_CC=%u)\n",
               NPU_CONFIG, (unsigned)(NPU_CONFIG & 0xF));
    rt_kprintf("NPU STATUS   = 0x%08x\n", NPU_STATUS);
    rt_kprintf("NPU QCONFIG  = 0x%08x\n", NPU_QCONFIG);
    return 0;
}
MSH_CMD_EXPORT(npu_id, Ethos-U55 ID/CONFIG/STATUS probe);
MSH_CMD_EXPORT(npu_bench, Ethos-U55 weight-stream bandwidth bench (R0.5-2));
