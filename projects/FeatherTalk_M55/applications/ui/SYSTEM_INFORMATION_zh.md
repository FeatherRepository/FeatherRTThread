# FeatherTalk 系统信息口径

本文说明 System 应用显示的硬件信息、容量与实时占用来自哪里。目标是把
“芯片物理资源”“当前双核链接布局”“M55 当前运行状态”和“尚不可观测的
资源”分开，避免把分配容量误写成已经使用，也避免为未启用的驱动伪造状态。

## 页面呈现

System 页面采用“先概览、再按需展开”的层级，不把本文全部技术字段一次性
铺在屏幕上：

- 顶部四张摘要卡只显示片外存储、片外 RAM、片上 RAM 和处理器；480×800
  竖屏为 2×2，宽屏/横屏可在一行显示四张。
- “设备规格”默认展开，保留 SoC、双核、NPU/GFX、系统、显示和触摸等最常查
  信息。
- “存储与内存”“接口与外设”“运行状态”默认折叠，点击标题后才展示完整值。
- 折叠只改变信息层级，不删除采集项；下述容量、占用、时钟和不可用状态仍由
  原来的运行期数据源更新。

## 处理器与时钟

| 项目 | 当前配置 | System 页取值方式 |
| --- | ---: | --- |
| SoC | PSE846GPS2DBZC4A / PSoC Edge E84 | 产品板级配置 |
| Cortex-M55 | 400 MHz | 运行期 `SystemCoreClock` |
| Cortex-M33 时钟域 | 200 MHz | 运行期 `Cy_SysClk_ClkHfGetFrequency(0)`；M33 负载来自 IPC |
| Ethos-U55 NPU 时钟域 | 400 MHz | 运行期读取 NPU 外设所属 HF 时钟 |
| GFXSS GPU/显示时钟域 | 400 MHz | 运行期读取 GFXSS HF 时钟 |
| RT-Thread tick | 1000 Hz | `RT_TICK_PER_SECOND` |

System 页还显示 M55 指令/数据 Cache 的实际启用状态。频率表示当前时钟域
频率，不等于该单元每一时刻都在执行任务。M33 自己的堆、栈和 CPU 使用率
不能从 M55 地址空间直接推断；已有 ABI 4 IPC 能显示 M33 上报的在线状态、
uptime、CPU、heap 和线程数。

## 片上存储与 RAM

芯片物理资源由生成的 PSE84 设备头文件定义：

| 资源 | 物理容量 | 当前产品用途或可观测范围 |
| --- | ---: | --- |
| Boot ROM | 64 KiB | 平台启动 ROM，不计入产品固件可写空间 |
| RRAM | 512 KiB | 其中用户可寻址窗口为 296 KiB programmable + 32 KiB user NVM；产品 M33/M55 镜像当前不链接到这里 |
| M55 ITCM | 256 KiB | 指令 TCM |
| M55 DTCM | 256 KiB | System 页用链接符号显示静态数据占用；默认主栈另保留 4 KiB |
| M33 subsystem SRAM | 1 MiB | M33 私有代码/数据与双方小块分配区；M55 不伪报其实时使用量 |
| SoC memory | 5 MiB | M55 secondary code 384 KiB、data 1,408 KiB、双核共享 256 KiB、GFX 3 MiB |

因此当前地址图中的通用片上 RAM 合计 6.50 MiB，包括 M55 的 512 KiB TCM、
1 MiB M33 SRAM 和 5 MiB SoC memory。CAN-FD 还带有 64 KiB 专用 message RAM，
它属于外设本地存储，不计入通用堆；当前产品没有启用 CAN-FD 驱动。

M55 内部堆的 total/current/peak 来自 RT-Thread `rt_memory_info()`，不是
链接文件估算。GFX 占用由 `.cy_gpu_buf` 的链接边界得到，当前主要包含
480 x 800 RGB565 双缓冲和 512 像素 scanout stride 所需的静态显示缓冲。

## 片外 Flash

板载 S25FS128S QSPI NOR 物理容量为 16 MiB，映射到 SMIF0/XIP0。当前产品
链接布局如下：

| 区域 | 容量 | 说明 |
| --- | ---: | --- |
| 启动/安全区 | 1 MiB | ROMBoot、RRAMBoot、L1/安全资产等平台布局 |
| M33 Secure | 2 MiB + 256 KiB trailer | 安全镜像槽及升级 trailer |
| M33 Non-secure | 2 MiB + 256 KiB trailer | `0x60340000` 起的 M33 产品镜像槽 |
| M55 | 8 MiB + 256 KiB trailer | `0x60580000` 起的 M55 XIP 镜像槽 |
| 当前未分配 | 2.25 MiB | 物理 Flash 中未被上述链接窗口占用的尾部容量 |

System 页中的 M55 “used”不是 HEX 文件大小，而是链接器导出的
`__m55_image_start__` 到 `__m55_image_end__` 实际占用跨度，包含 1 KiB
MCUboot header。M33 各槽在 M55 页只报告分配容量；若未来需要显示 M33
镜像的精确字节数，应由 M33 或安全启动元数据通过 IPC 上报，不能用槽容量
冒充已用容量。

## 片外 HyperRAM

板载 S70KS1283 HyperRAM 物理容量为 16 MiB，映射到 SMIF1/XIP1：

| 地址窗口 | 容量 | 所属 |
| --- | ---: | --- |
| `0x64000000` 起 | 2 MiB | M33 private |
| `0x64200000` 起 | 2 MiB | M55 private |
| `0x64400000` 起 | 12 MiB | M33/M55 shared |

当前 HyperRAM 驱动把共享窗口前 8 MiB 注册为独立 RT-Thread `hyperam`
memheap，所以 System 页可以实时显示它的 total/current/peak。共享窗口余下
4 MiB 仍在地址图中，但没有绑定到通用堆；2 MiB M55 private 窗口也保留给
显式 section/后续专用分配，不应计算为当前空闲 heap。

## 通信与显示链路

| 链路 | 当前配置 |
| --- | --- |
| SMIF0 Flash | 200 MHz 输入时钟，x4 Quad-SDR XIP |
| SMIF1 HyperRAM | 399 MHz 输入时钟，x8 DDR（器件为 16-bit HyperRAM） |
| MIPI-DSI | 2 lanes × 900 Mb/s，RGB565，33.984 MHz pixel clock |
| UART2 / MSH | 115200 baud，8-N-1 |
| I2C0 | 硬件控制器，100 kHz |
| I2C1 | RT-Thread software I2C，连接 ST7102/ST7123 触摸控制器 |
| M33/M55 IPC | 片上 IPC pipe，ABI 4，固定 16-byte frame；不存在外部 baud |
| LCD 背光 | PWM18，5 kHz；UI 0..100% 映射到硬件 50..100% duty |
| LCD 搬运 | AXI-DMA，面积达到 8 KiB 后启用 |

SMIF 输入时钟不应直接解释成等量有效吞吐；协议宽度、DDR/SDR、等待周期、
Cache 命中与总线仲裁都会影响实际带宽。MIPI 的 900 Mb/s 是每 lane 速率，
不是 CPU 时钟。

## 外设状态口径

System 页运行期遍历 RT-Thread `RT_Object_Class_Device`，显示实际已注册设备
数和名称。当前产品路径启用 GPIO、IPC、UART2、I2C0、software I2C1、触摸、
LCD/GFX/MIPI、PWM18、HyperRAM 与显示 AXI-DMA。

当前产品已经启用 SDHC1 4-bit、RT-Thread MMC/SD、DFS 与 Elm-FatFS；System
页从 `/sdcard` 的真实挂载状态和 `statfs` 结果显示容量/可用空间。开发板硬件
或 SDK 中存在但当前产品配置没有启用的主要能力包括 CYW55513
Wi-Fi/Bluetooth、ES8388 audio、USB、CAN-FD、I3C、PDM 和 TDM。Wi-Fi、
Bluetooth、RTC、电池和 USB 只有在其所有者驱动真实上报后才显示在线/
使用状态；当前必须明确显示 unavailable。

## 数据来源与维护规则

- 物理芯片容量：`projects/libs/TARGET_APP_KIT_PSE84_EVAL_EPC2/cy_device_headers_ns.h`
- RRAM 用户窗口：`projects/libs/TARGET_APP_KIT_PSE84_EVAL_EPC2/config/GeneratedSource/device_mem.json`
- M55/M33 内存布局：各项目 `board/linker_scripts/link.ld`
- Flash/HyperRAM 器件容量与 SMIF 时钟：`cycfg_qspi_memslot.*`
- MIPI 配置：板级 GFX/MIPI generated source 与显示驱动
- 产品驱动开关：`projects/FeatherTalk_M55/rtconfig.h`
- 运行期采集：`feathertalk_ui_platform.c`

任何板级容量、链接槽、时钟或驱动开关变化，都必须同步更新 System 页、
本文和板端自动测试。对另一个核心或未注册驱动无法观测的数据，应写明
“不可观测/未启用”，不能用静态默认值冒充实时使用量。
