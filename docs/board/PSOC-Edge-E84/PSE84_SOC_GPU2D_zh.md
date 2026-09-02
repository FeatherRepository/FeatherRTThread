# PSoC Edge E84 SoC 资源与 GPU2D 技术梳理

更新时间：2026-09-02

适用硬件：Edgi-Talk PSoC Edge E84 开发板

适用器件：`PSE846GPS2DBZC4A`（PSoC Edge E84、EPC2、BGA-220）
适用工程：`FeatherTalk_M33` + `FeatherTalk_M55`

## 1. 文档目的与口径

本文把容易混淆的四个层次分开：

1. **PSoC Edge E8x 系列上限**：数据手册和架构参考手册描述的系列能力。
2. **PSE846GPS2DBZC4A 实际 SKU**：当前芯片真正带有的 CPU、NPU、GFX 和存储资源。
3. **Edgi-Talk 板级资源**：原理图上实际焊接并连到 SoC 的 Flash、RAM、屏幕、无线、音频和接口。
4. **FeatherTalk 当前配置**：链接脚本、RT-Thread、LVGL 9.2/VG-Lite 整帧批处理和显示驱动现在真正启用的路径；FeatherUI 章节保留为 GPU 原生对照实验。

任何“最大支持”都不等于“当前启用”；任何“时钟域频率”也不等于模块核心频率或实时利用率。本文中的“GPU2D”专指 GFXSS 内的 **Vivante GCNano Ultra V 2.5D GPU**，不把显示控制器、MIPI-DSI 和 AXI-DMA混称为 GPU。

## 2. 资料索引与可信度

### 2.1 第一优先级：芯片官方资料

| 资料 | 文档号/版本 | 主要用途 | 本机位置 |
| --- | --- | --- | --- |
| PSoC Edge E8x2/E8x3/E8x5/E8x6 Consumer Datasheet | 002-37630 Rev. *K，2026-03-24 | SKU、容量、频率、电气和外设上限 | `PSOC™ Edge E8/IC-res/DATASHEETS/` |
| PSoC Edge E8x Architecture Reference Manual | 002-38331 Rev. *B，2026-04-01 | 总线、存储、GFXSS、GPU、DC、MIPI、时钟和寄存器 | `PSOC™ Edge E8/IC-res/reference manual/` |
| AN239191 Getting started with graphics | 002-39191 Rev. *A，2025-09-10 | GFXSS、PDL、显示和 VG-Lite 入门 | `PSOC™ Edge E8/IC-res/application notes/` |
| AN237939 High-performance graphics and low-power optimization | 002-37939 Rev. *A，2025-12-09 | 帧缓冲、Cache、低功耗和性能建议 | `PSOC™ Edge E8/IC-res/application notes/` |
| Vivante VGLite Vector Graphics API Reference | 002-39840 Rev. *B，2025-09-24 | VG-Lite 3.0 API、格式、同步、内存和限制 | `PSOC™ Edge E8/IC-res/user guide/` |
| Advanced graphics training manual | Rev. *A，2026-07-16 | LVGL/VG-Lite 集成和官方参考性能 | `official-resources/training/mtb-training-psoc-edge-graphics/Manual/` |

### 2.2 第二优先级：当前板与仓库事实

| 资料 | 用途 |
| --- | --- |
| `PSoc_Edge_Core_Schematic.pdf` | SoC 核心板、SMIF Flash/HyperRAM、无线等连接 |
| `PSoc_Edge_Basic_Schematic.pdf` | 显示、触摸、音频、USB、SD、传感器、扩展口和电源 |
| `projects/libs/TARGET_APP_KIT_PSE84_EVAL_EPC2/` | 具体器件头文件、启动代码和生成配置 |
| `projects/FeatherTalk_M33/board/linker_scripts/link.ld` | M33 当前内存和镜像布局 |
| `projects/FeatherTalk_M55/board/linker_scripts/link.ld` | M55、共享内存、HyperRAM 和 GFX 当前布局 |
| `libraries/HAL_Drivers/drv_lcd.c` | 当前 DC/MIPI/VG-Lite/AXI-DMA/Cache 显示通路 |
| `libraries/Common/board/ports/lvgl/` | LVGL 9.2 显示端口和 VG-Lite draw unit |

原理图和具体器件头文件优先于通用 BSP 的营销说明。仓库中若仍有“160 行 LVGL 缓冲”或“GFXSS GPU 400 MHz”的旧文字，应以本文核对的当前配置为准。

### 2.3 第三优先级：GC355 历史 IP 交付包

`D:/Develop/Edgi-Talk/GC355/` 不是零散示例，而是一套 2010 年前后的 Vivante GC355/OpenVG 1.1 IP 交付资料。当前盘点结果如下：

| 目录 | 文件数/规模 | 主要内容 | 对当前项目的价值 |
| --- | ---: | --- | --- |
| `document/` | 5 个 PDF，约 6.37 MiB | GC355 Data Book、硬件/软件培训、OpenVG 1.1 API 与用户指南 | 理解 Vivante 传统矢量图形流水线、总线、时钟、MMU、命令和 API 语义 |
| `software/` | 291 个文件，约 5.47 MiB | GAL OpenVG 驱动、Linux/WinCE OS 层、SDK、Tiger/功能测试；版本 `2010.1.4` | 研究用户态/内核态分层、命令队列、IRQ、内存映射、同步及驱动移植检查项 |
| `hardware/` | 1001 个文件，约 267.80 MiB | GC355 Verilog RTL、AHB/AXI 测试平台、DV 与 SoC 集成向量 | 研究 Front End、Memory Controller、Tessellator、VG/Imaging/Pixel Engine 及可复现验证方法 |
| `physical/` | 70 个文件，约 0.39 MiB | TSMC 40/65 nm 综合、布局布线、时序和 LEC 脚本 | 仅供旧 IP 物理实现思路参考，不用于判断 PSE84 的实现参数 |

GC355 是面向 OpenVG 1.1 的独立旧核；当前 PSE84 使用的是 **GCNano Ultra V**，当前驱动头识别为 `CHIPID=0x265`、`REVISION=0x1003`、`CID=0x410`、`ECOID=0x1`，软件接口是 VG-Lite 3.0。二者同属 Vivante 图形技术体系，但没有证据表明寄存器、命令编码或 feature 位可直接兼容，因此必须按下表使用：

| GC355 内容 | 参考等级 | 使用方式 |
| --- | --- | --- |
| Host Interface → Memory Controller → Front End → Tessellator/VG/Imaging/Pixel Engine 的数据流 | 高 | 用于建立命令提交、取数、路径细分、像素生成、混合和写回的诊断模型 |
| AHB 控制面、AXI 数据面、异步时钟域、IRQ、命令缓冲和事件完成模型 | 高 | 用于分析“提交后卡死”“总线错误”“等待不返回”和 Cache/内存所有权问题 |
| Blend/Clear/Color transform/Drawing/Image/Mask/Paint/Pixel format 的 SoC 测试分类，以及输入缓冲与结果帧逐项比较方法 | 高 | 可转化为 PSE84 VG-Lite 的最小硬件验证集；测试数据和命令必须重新生成 |
| GAL 用户态/内核态/OS 适配层、寄存器映射、连续显存、IRQ 工作线程和信号同步 | 中 | 作为驱动分层与移植清单；当前 RTOS/VG-Lite 实现不能直接套用 Linux 2.6/WinCE 代码 |
| OpenVG 路径、描边、Paint、渐变、Mask、Scissor、Image filter 和 Blend 语义 | 中 | 用于理解功能意图和构造测试；不能把 OpenVG API 等同于 VG-Lite API |
| GC355 寄存器地址、命令字、MMU 表、像素格式表、复位周期、总线宽度和时钟上限 | 禁止直接继承 | 只能帮助提出问题，最终必须由 PSE84 ARM、当前 VG-Lite 驱动和实机读回确认 |
| GC355 性能、功耗、工艺、面积和物理实现数据 | 不适用 | 不能用于估算 PSE84 GCNano Ultra V 的帧率、功耗、面积或频率 |

GC355 Data Book 描述的旧核使用 32-bit AHB 控制接口、64-bit AXI master、命令缓冲取指、虚拟内存、独立 Core/AHB/AXI 时钟域，并提供路径、图像、混合、格式转换等 SoC 测试。这些内容很适合帮助解释 Vivante 类 GPU 的工作方式，但它们不是当前芯片规格。

资料包内的 Data Book、Release Note 和源码头均带有 Vivante 专有/机密声明。当前仓库只记录文件索引和工程结论；在没有明确再分发授权前，**不得把 GC355 PDF、RTL、驱动源码、测试向量或物理脚本复制进公开 GitHub 仓库**。

## 3. SoC 总览

### 3.1 精确器件身份

| 项目 | 当前硬件 |
| --- | --- |
| 完整器件号 | `PSE846GPS2DBZC4A` |
| 产品族 | PSoC Edge E84 Consumer |
| 安全等级 | EPC2 |
| 封装 | BGA-220 |
| Silicon ID | `0xED942115` |
| 双核 | Cortex-M55 + Cortex-M33 |
| AI 加速 | Ethos-U55 + NNLite |
| 图形 | GCNano Ultra V GPU + DC8000Nano + MIPI-DSI |
| 该 SKU 的 SoCMEM | 5120 KiB |

### 3.2 处理器与 AI 资源

| 模块 | 硬件能力 | 当前产品角色 |
| --- | --- | --- |
| Cortex-M55 | 最高 400 MHz；Helium/MVE、DSP、单/双精度 FPU；32 KiB I-Cache、32 KiB D-Cache；256 KiB ITCM、256 KiB DTCM | RT-Thread、LVGL、应用、显示、文件、USB 和音频主控 |
| Cortex-M33 | 最高 200 MHz；DSP、FPU、TrustZone；16 KiB I-Cache | 安全启动、无线控制、跨核服务和低功耗/系统功能 |
| Ethos-U55 | 最高 400 MHz；128 MAC/cycle；INT8 峰值 51.2 GOPS | 硬件存在，FeatherTalk 尚未装入业务 NPU 模型 |
| NNLite | 与 M33 时钟域关联；4 MAC/cycle | 硬件存在，当前产品尚未使用 |

M55 侧主要使用 64-bit AXI4 和 AHB5，M33 侧以 AHB5 为主，双方通过异步桥和 IPC 协作。GPU、显示控制器、DMA、SMIF 和 CPU 都会竞争片上总线与存储带宽，因此 UI 性能不能只看 GPU 峰值。

### 3.3 片上非易失与 RAM

| 资源 | 物理容量 | 说明 |
| --- | ---: | --- |
| Boot ROM | 64 KiB | 固化启动代码，不是用户可写 Flash |
| RRAM | 512 KiB | 安全启动、生命周期和用户 NVM 共用；当前产品镜像不以整块 RRAM 作为普通应用盘 |
| Dedicated OTP | 5120 bytes | 芯片配置和不可逆安全数据，不是普通文件存储 |
| M55 ITCM | 256 KiB | 低延迟指令存储 |
| M55 DTCM | 256 KiB | 低延迟数据、栈和关键对象 |
| M33 subsystem SRAM | 1 MiB | M33 私有代码/数据及跨核保留区域 |
| SoCMEM | 5 MiB | 10 个 512 KiB 物理分区，可做保留/掉电策略；当前由链接脚本重新切分 |
| CAN-FD message RAM | 64 KiB | 外设专用，不计入通用堆 |

常见的“6.5 MiB SRAM”口径为 `5 MiB SoCMEM + 1 MiB M33 SRAM + 512 KiB M55 TCM`。它不表示任一核心能把 6.5 MiB 全部作为一个连续堆使用。

### 3.4 SoC 外设能力

| 分类 | SoC 能力摘要 |
| --- | --- |
| 外部存储 | 2 路 SMIF；每路 32 KiB Cache；支持 Quad/Octal SPI、DDR 和 HyperBus 类器件 |
| SD/eMMC | 2 路 SDHC，覆盖 SD 6.0、SDIO 4.10、eMMC 5.1 能力范围 |
| USB | USB 2.0 High-Speed/Full-Speed PHY，SoC 支持 Host/Device，理论线速 480 Mb/s |
| 网络 | 10/100 Ethernet MAC（MII/RMII，需外部 PHY）；2 路 CAN-FD |
| 串行接口 | 12 个 SCB 资源，可按 UART/I2C/SPI 组合；另有 I3C |
| 定时/PWM | 32 个 TCPWM（8×32-bit、24×16-bit）；2 个 SmartIO |
| 音频 | 2 路 TDM/I2S；PDM/PCM 最多 6 路数字麦克风；Always-on Audio Detection |
| 模拟 | SAR ADC（12–20 bit，最高 5 Msps active）；2×12-bit DAC；4 个运放；比较器 |
| 安全 | CM33 TrustZone、安全启动、EPC2 安全隔离、加密和生命周期管理 |
| DMA | 4 通道高性能 DMA，以及两组 16 通道 DMA 资源 |
| GPIO | BGA-220 SKU 最多 147 个 I/O；实际可用数量受板级复用约束 |

## 4. Edgi-Talk 板级资源

### 4.1 存储与通信

| 板载器件/接口 | 容量或连接 | 当前情况 |
| --- | --- | --- |
| S25FS128S QSPI NOR | 128 Mbit = 16 MiB，SMIF0/XIP0 | M33/M55 XIP、签名槽和末尾 2 MiB `/flash` |
| S70KS1283 HyperRAM | 128 Mbit = 16 MiB，SMIF1/XIP1 | M33 private 2 MiB、M55 private 2 MiB、共享 12 MiB |
| microSD | SDHC1、4-bit、卡检测 | RT-Thread MMC/SD + FAT，挂载 `/sdcard` |
| Wi-Fi/Bluetooth | CYW5551x 系列组合模组；Wi-Fi 走 SDIO，Bluetooth 走 UART/Wake/Reset | 蓝牙 bring-up 已有独立文档；Wi-Fi 仍待完整接入 |
| CAN-FD | TJA1042 类收发器 | 板上通路存在，产品驱动尚未启用 |

无线模组在不同 BSP/原理图资料中出现 `CYW55512`、`CYW55513` 等命名差异。固件选择必须以实板模组丝印、芯片 ID 和已验证的 CYW55500A1 固件组合为最终依据，不能仅由通用 E84 BSP 名称推断。

### 4.2 显示、触摸、音频与传感器

| 资源 | 板级实现 |
| --- | --- |
| LCD | 480×800 RGB565，MIPI-DSI 2 data lanes，TL043WVV02/EK79007AD3 路径 |
| 触摸 | 面板 I2C + INT + RESET；当前驱动识别 ST7102/ST7123 路径 |
| 背光 | AP3019A 类升压驱动 + PWM18；当前 5 kHz |
| Codec | ES8388，支持板载模拟输入/输出路径 |
| 功放/扬声器 | MD8002 类功放和外接/板载扬声器通路 |
| 数字麦克风 | 2 路 PDM MEMS 麦克风 |
| 传感器 | AHT20 温湿度、LSM6DS3TR-C 六轴 IMU |
| USB Type-C | 调试/KitProg3 口与 SoC USB 口分开 |
| 调试 | 板载 KitProg3、SWD/JTAG、trace、调试 UART |
| 扩展 | Raspberry Pi 40-pin、PMOD、3.3 V 电平转换和 CAN 接口 |

SoC 本身支持 USB Host/Device，但当前 SoC USB Type-C 口的 CC 下拉与供电路径按设备/受电端使用最明确。Host 能力不能只靠软件切换，仍需确认 VBUS source、过流保护和 Type-C 角色电路。

## 5. 当前 FeatherTalk 内存与镜像布局

### 5.1 关键地址

| 区域 | 地址 | 大小 | 当前用途 |
| --- | --- | ---: | --- |
| M55 ITCM | `0x00000000` | 256 KiB | 快速代码 |
| M55 DTCM | `0x20000000` | 256 KiB | 快速数据/栈 |
| RRAM NS alias | `0x02000000` | 512 KiB | 非安全 C-Bus 映射 |
| M33 SRAM NS | `0x24000000` | 1 MiB | M33 子系统内存 |
| SoCMEM NS | `0x26000000` | 5 MiB | M55 code/data、共享和图形区 |
| M55 XIP | `0x60580000` | 8 MiB | M55 签名镜像槽 |
| M55 trailer | `0x60D80000` | 256 KiB | 升级 trailer |
| 用户 Flash 盘 | `0x60E00000` | 2 MiB | FAT `/flash`，固定在 NOR 末尾 |
| M55 private HyperRAM | `0x64200000` | 2 MiB | 专用 section/后续分配 |
| shared HyperRAM | `0x64400000` | 12 MiB | 双核共享；当前前 8 MiB 注册为 `hyperam` heap |

### 5.2 当前 5 MiB SoCMEM 切分

| 链接区 | 地址 | 大小 |
| --- | --- | ---: |
| M55 secondary code | `0x26000000` | 384 KiB |
| M55 data | `0x26060000` | 1408 KiB |
| M33/M55 shared | `0x261C0000` | 256 KiB |
| `gfx_mem` | `0x26200000` | 3 MiB |

这些是当前产品链接分配，不是 SoC 固定硬件边界。更改任何一项必须同时检查 M33/M55 链接脚本、MPU/Cache 属性和跨核 ABI。

## 6. GFXSS 与 GPU2D 架构

### 6.1 三个独立硬件块

GFXSS 由三个职责不同的硬件块组成：

1. **GCNano Ultra V GPU**：把路径、矩形、图像、渐变等绘制进内存目标缓冲。
2. **DC8000Nano Display Controller**：持续从内存读出图层，做合成、色彩处理并生成像素流。
3. **DWC MIPI-DSI Host + D-PHY**：把像素流编码为 MIPI-DSI 信号送往面板。

当前 FeatherTalk 的数据路径如下：

```text
LVGL 9.2 widgets / SVG(A8 path) / images
        |
        +--> LVGL software draw（不适合 GPU 的任务）
        |
        +--> LVGL VG-Lite draw unit
                 |
                 v
          VG-Lite command buffers
                 |
                 v
    GCNano Ultra V GPU  --AXI R/W--> LVGL draw buffer (SoCMEM)
                 |
                 v
     LVGL flush dirty rectangles
                 |
         AXI-DMA >= 8 KiB
         CPU memcpy < 8 KiB / fallback
                 |
                 v
    persistent RGB565 scanout framebuffer (SoCMEM, stride 512)
                 |
                 v
      DC8000Nano AXI read -> DPI pixel stream
                 |
                 v
      MIPI-DSI host / 2-lane D-PHY -> 480x800 panel
```

AXI-DMA 只负责脏矩形搬运，不是 GPU 绘制。DC 只读 scanout，GPU 主要读写 LVGL draw buffer。当前 0°方向下 GPU 不负责整屏旋转。

### 6.2 总线接口

GFXSS 对系统暴露两个主要 AXI master：

- GPU AXI master：读写命令、路径、纹理、像素和目标缓冲。
- DC AXI master：只读图层和光标数据，持续维持显示扫描。

因此 GPU `finish` 完成只表示绘制命令完成，并不代表 LCD 已扫描完该帧；DC 的 frame-done/vblank 同步是另一条状态链。若 CPU、GPU、DMA 和 DC 同时访问同一块缓存内存，Cache 所有权和同步顺序必须明确。

## 7. GCNano Ultra V GPU2D 详细能力

### 7.1 为什么称为 2.5D

GPU 支持带透视校正的二维图像变换，但没有传统 3D GPU 的深度缓冲、顶点着色器、片元着色器、材质和完整三维流水线。它适合 GUI、仪表、矢量图、位图变换和合成，因此官方称为 2.5D。

硬件流水线可概括为：

```text
Host interface / command FIFO
    -> Memory controller
    -> Front end
    -> Tessellation engine
    -> Vector engine / Imaging engine
    -> Pixel engine
    -> Destination memory
```

CPU 构造 display-list/command buffer 后，GPU 可自主取命令并执行；CPU 不需要逐像素参与。

### 7.2 绘制与变换

| 能力 | 支持情况与边界 |
| --- | --- |
| 清屏/矩形填充 | 支持 |
| 图像 blit | 支持缩放、旋转、平移、透视和双线性/点采样 |
| 矢量路径 | 支持直线、二次/三次 Bézier、路径 tessellation 和填充规则 |
| 线性渐变 | 支持 |
| 径向渐变 | 当前硬件 feature 为 0，不支持 |
| Pattern fill | 支持 |
| 透视 | 图像 blit 支持；路径绘制不支持透视 |
| Stroke | 驱动可用，但由软件先把描边转换为填充路径，不是原生硬件 stroke 单元 |
| Mask/Stencil | 当前 feature 为 0 |
| Gaussian blur | 当前 feature 为 0 |
| Global alpha | 当前 feature 为 0；应使用像素 alpha/混合路径 |
| Scissor | 支持 |
| 多路径并行 | 支持 parallel path 和 split path |

### 7.3 混合与抗锯齿

- 支持 8 种 Porter-Duff 类混合操作。
- Porter-Duff 计算假定颜色已做 **premultiplied alpha**。
- 当前硬件不自动 premultiply；带 alpha 的资源最好离线预乘，或由软件明确转换。
- 高质量路径 AA 为 16×，中等为 4×，低质量不做采样 AA。
- `gcFEATURE_VG_QUALITY_8X=0`，不能把不存在的 8×模式写进 UI 设置。

### 7.4 像素格式

当前 GCNano 硬件配置的主要格式如下：

| 类型 | 格式 |
| --- | --- |
| 仅源图支持 | INDEX1、INDEX2、INDEX4、A4、INDEX8 |
| 源/目标均支持 | A8、L8、A2R2G2B2、A1R5G5B5、A4R4G4B4、R5G6B5、A8R8G8B8、X8R8G8B8 |
| YUV/打包格式 | YUY2、YUY2_TILED、NV12_TILED、ANV12_TILED、AYUY2_TILED |
| 通道排列 | ABGR、ARGB、BGRA、RGBA；UV/VU |

关键限制：

- `gcFEATURE_VG_24BIT=0`，GPU 不原生支持紧凑 24-bit RGB888 作为普通 VG 目标格式。
- DC 和 MIPI 能输出 RGB888，不代表 GPU 也具备 RGB888 渲染目标。
- GPU 支持部分 YUV 输出，但对 YUV destination 不支持 alpha blending。
- 当前 UI 使用 RGB565，正好避开 24-bit 目标限制并降低显存/总线带宽。

### 7.5 对齐和内存

| 项目 | 要求/建议 |
| --- | --- |
| VG-Lite external command buffer | 物理地址 64-byte 对齐；大小 128-byte 对齐 |
| Tessellation buffer | 地址 64-byte 对齐 |
| 16-pixel feature | 当前启用，部分宽度/路径处理按 16 pixel 对齐 |
| DC linear framebuffer base/stride | 128-byte 对齐 |
| 当前 RGB565 stride | `512 × 2 = 1024 bytes`，满足 128-byte 对齐 |

在 Cortex-M55 D-Cache 开启时，物理连续不等于 Cache 一致。可选择把 GPU/DC 共享区配置为 non-cacheable，或严格执行 clean/invalidate；不能依赖“同一地址”自动同步。

### 7.6 VG-Lite 软件版本与上下文模型

当前 SDK 源码中的 VG-Lite 为：

| 项目 | 版本 |
| --- | --- |
| API | 3.0.0 |
| Header version | 7 |
| Release | 4.0.107 |
| GCNano CHIPID | `0x265` |
| GCNano REVISION | `0x1003` |
| DC8000Nano driver | 3.0.26 |

VG-Lite 使用一个隐式全局 context。硬件/驱动没有常规多 context 切换能力，应用应由一个命令提交线程串行调用；多个线程不能无锁并发调用 VG-Lite。LVGL 当前由自己的 VG-Lite draw unit 统一提交，其他业务若直接使用 VG-Lite，必须复用同一锁和生命周期。

`vg_lite_init(width, height)` 的 width/height 是 tessellation window，不是显示器注册分辨率：

- 必须是 16 的倍数，最小 16×16。
- 小 window 可省 tessellation 内存，但大目标可能分条/重扫。
- 传入非正数可用于仅 blit 的场景，不创建 tessellation buffer。
- `vg_lite_flush()` 提交后异步返回；`vg_lite_finish()` 提交并等待硬件完成。

## 8. DC8000Nano 显示控制器

### 8.1 图层模型

架构手册用“最多四层 alpha blending”描述合成能力。详细资源应理解为：

- 1 个 memory-backed graphics/video layer；
- overlay0；
- overlay1；
- 1 个 solid-color background；
- 另有独立 hardware cursor。

因此更准确的说法是“3 个内存图层 + 纯色背景参与最多四层合成，外加硬件光标”，而不是四张任意 framebuffer。

当前 FeatherTalk 只启用了 graphics layer；overlay0/overlay1 和 hardware cursor 都未用于 UI。引入 overlay 可减少某些视频/状态层重绘，但会增加带宽、同步和图层管理复杂度。

### 8.2 DC 处理能力

- linear/tiled RGB/YUV 图层读取；
- color key、通道 swizzle 和 alpha 合成；
- graphics layer 与 overlay0 的 YUV-to-RGB；
- gamma SRAM、dither LUT 和格式转换；
- hardware cursor；
- RLAD/RLA/RL 压缩帧缓冲解码；
- display FIFO、DPI/DBI 输出时序。

DC 是固定功能 scanout/compositor，不执行 LVGL 矢量路径。GPU 把结果画进内存，DC 再把内存持续扫到屏上。

### 8.3 RLAD 不是 GPU 纹理压缩

RLAD 位于显示读出链路，用于降低静态 framebuffer 的存储和读带宽：

- `RLAD`、`RLAD_UNIFORM` 为有损模式；
- `RLA`、`RL` 为无损模式；
- 可接收 ARGB4444/1555、RGB565、ARGB8888、RGB888/666/444、GRAY8/6/4 等；
- 解码输出为 ARGB8888；
- 压缩行长度需满足 128-byte 粒度要求；
- 它不能让 VG-Lite 直接把普通压缩纹理当源图绘制。

若未来壁纸或静态背景长期不变，RLAD 可能节省 DC 带宽；动态 LVGL 全屏缓冲未必适合直接切换到 RLAD。

### 8.4 分辨率与接口上限

| 输出路径 | 系列硬件上限 |
| --- | --- |
| Display | 最高 1024×768 @ 60 Hz、24-bit |
| DPI 内部到 DSI | pixel clock 最高约 64 MHz |
| DBI GPIO | 最多 16-bit、约 50 MHz |
| DBI 经 DSI | 约 37.5 MHz |
| MIPI-DSI | 2 lanes，每 lane 最高 1.5 Gb/s |

当前板的实际配置明显低于链路上限：480×800、RGB565、2 lanes × 900 Mb/s、pixel clock 33.984 MHz。

当前 video timing 为 HFP 86、HBP 87、HSYNC 2、VFP 182、VBP 8、VSYNC 2；采用 non-burst sync-pulse video mode。它们是当前面板和驱动的实配值，不应套用到其他 480×800 面板。

## 9. 时钟、复位与低功耗

### 9.1 GFXSS 时钟关系

| 时钟 | 作用 | 频率关系 |
| --- | --- | --- |
| `clk_sys` | GFXSS 寄存器/MMIO 配置 | 最高 100 MHz；模块门控时仍用于控制访问 |
| `CLK_HF1 / clk_hf` | GFXSS 父时钟域 | 100–400 MHz |
| `clk_2d` | GPU2D 核心 | `clk_hf / 2`，即约 50–200 MHz |
| `clk2x_2d` | GPU 内部双口 SRAM | 等于 `clk_hf`，约 100–400 MHz |
| `clk_hf_core` | DC 核心 | `clk_hf / 2`，约 50–200 MHz |

当前 BSP 把 `CLK_HF1` 配为 400 MHz，因此：

- **GFXSS 父时钟域：400 MHz**；
- **GPU2D 核心：200 MHz**；
- **DC 核心：200 MHz**。

现有 System 页若只写“GFXSS GPU/显示 400 MHz”，会让人误以为 GPU core 在 400 MHz。建议 UI 改成“GFXSS HF 400 MHz / GPU2D 200 MHz / DC 200 MHz”。

### 9.2 门控、复位和中断

- GPU disable 会触发核心/高频配置域复位，不支持把它当成普通局部暂停后无条件续跑。
- 进入/退出低功耗必须按 PDL 推荐顺序处理时钟、命令完成、DC 扫描和 DSI ULPM。
- GPU 对外主要是一个 level-high IRQ；总线错误和驱动内部完成事件需要由驱动进一步解析。
- 静止 UI 不代表 DSI video mode 停止传输：当前面板仍连续扫描整帧。

低功耗优化应区分两类目标：减少 CPU/GPU 绘制工作，和减少 DC/DSI 连续扫描功耗。当前 partial render 主要改善前者，不能自动解决后者。

## 10. FeatherTalk 当前 LVGL 整帧 GPU 批处理实现

### 10.1 当前配置与帧流水线（2026-09-01）

`product/edgi-talk` 当前重新启用 `FEATHERTALK_USING_UI_SHELL` 和 LVGL 9.2，关闭实验性的 `FEATHERTALK_USING_GPU_UI`，并启用 `FEATHERTALK_USING_LVGL_GPU_BATCH`。保留 LVGL 已完成的页面、控件、主题、动画和输入逻辑，同时重写刷新、任务调度、VG-Lite 提交和显示端口：

1. LVGL 完成一帧对象遍历和 draw-task 创建；任务立即 evaluate，但不在创建时分散 dispatch。
2. `lv_refr.c` 在整帧任务收集结束后统一 dispatch，所有 GPU primitive 连续追加到同一个 VG-Lite command buffer。
3. 软件任务或资源所有权变化才形成显式同步边界；当前桌面实测没有 SW/resource boundary。
4. 静态字体的 glyph descriptor/kerning 使用跨帧散列表缓存；解码后的 A8 位图进入 384 KiB GPU 可见持久缓存，动态位图才复制到 384 KiB frame transient arena。
5. 唯一一次提交前统一 clean 本帧 transient cache，然后 `vg_lite_finish()` 提交并等待整帧命令链。
6. LVGL FULL render 直接画入两张 512×800 stride 的 RGB565 DC framebuffer；flush 不再做 AXI-DMA/CPU 整帧复制。DC present 改为异步提交，在下一帧开始前才退休上一提交，把 LVGL 线程休眠与部分扫描等待重叠。

因此当前合同是 **一帧一次 GPU submit、一次 complete、一次 DC present**。VG-Lite、LVGL 和 scanout 仍是三个独立阶段；`finish` 完成不代表 DC 已扫描到安全换帧点。

### 10.2 实机性能与分阶段结果

同一 480×800 壁纸桌面上，改造前后的板测结果为：

| 阶段 | GPU 任务路由 | submit/frame | collect | encode | finish/GPU wait | GPU 核心 busy | 整帧表现 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 原分散 LVGL 路径 | 约 67% | 约 12 | 未拆分 | 未拆分 | 多次 | 未测 | 明显卡顿 |
| 整帧批处理 + direct scanout，热代码仍在 XIP | 100% | 1.02 | 22.35 ms | 84.82 ms | 11.01 ms | 未测 | 约 121 ms/frame |
| 同步触摸和同步 present 的早期 ITCM 版本 | 100% | 1.00 | 8.12 ms | 23.00 ms | 10.71 ms | 18.17% | 17.42 FPS，57.40 ms/frame |
| 当前异步触摸、文字缓存、异步 present，60 帧基准 | 100% | 1.00 | **7.39–7.43 ms** | **19.37–19.49 ms** | **10.77–10.78 ms** | **23.66–24.99%** | **22.70–23.98 FPS** |
| 字体/SVG 离线原生化 + scissor 状态单链 | 100% | **1.00** | **6.286 ms** | **6.875 ms** | **11.235 ms** | **29.13%** | **26.22 FPS** |
| Stroke CALL 隔离 + 当前完整桌面三轮回归 | 100% | **1.00** | **9.54–9.60 ms** | **9.81–9.98 ms** | **10.88–10.90 ms** | **27.70–27.87%** | **25.97–26.16 FPS** |

当前 19.49 ms 编码阶段的 primitive 分解为：Label 12.59 ms、Image 4.56 ms、Fill 0.51 ms、Border 0.07 ms，其余接近 0。Label 内部约 9.68 ms 是 107 次 glyph VG-Lite 命令生成，布局/字体查找已降至 2.91 ms；本次基准的 descriptor cache 为 17,452 hit/50 miss，A8 bitmap cache 为 6,431 hit/10 miss、仅使用 12,736 B。GPU 核心执行约 10.77 ms/帧，不是当前最大耗时。

### 10.2.1 LVGL Vector 图标实装

FeatherTalk 已不再把 GPU 的 path 能力停留在“硬件支持但业务未使用”。受约束的单色 SVG
现在在主机构建时生成通用 shape/point 表；M55 首次使用时分别建立 fill path 和保持原始
中心线的 stroke path。当前 50 个 SVG 图标全部走
`LV_DRAW_TASK_TYPE_VECTOR -> vg_lite_draw()`；24/32/48 px A8 只作为 path 创建失败时的
防御性回退，SVG/XML 不进入设备运行时。

首轮实测的 10–12 submit/frame 不是 path 太大，也不是 224 KiB command buffer 溢出。
运行期每次提交约 4 KiB，并且所有显式 batch boundary 都为 0。真正原因是官方 Vector 后端
每条 path 都重写相同 scissor；VG-Lite 4.0.107 将 render target 标为 dirty，后续
`set_render_target()` 因此先提交旧命令。适配层现在复用 draw task 已安装的相同 clip，只有
真正更窄的 vector sub-clip 才改硬件 scissor。

修复后的最终镜像 60 帧桌面基准为：60 render / 60 submit、25.74 FPS、collect 9.721 ms、
encode 15.552 ms、finish 10.808 ms、GPU busy 27.10%。其中 Vector encode 为 2.366 ms/frame；
修复前同一矢量图标版本约为 12.4 ms/frame 且 10 submit/frame。这个结果说明 path 可以
进入产品单链，但必须把状态切换也纳入批处理审计，不能只统计 `lv_vg_lite_flush()` 次数。

继续扩展到 50/50 全矢量后，第一版手工 stroke 展开的 60 帧桌面基准为 23.90 FPS，
Vector 4.809 ms/frame。它虽然保持 60 render / 60 submit，却把相邻线段拆成重叠的矩形
与圆点；相反轮廓绕向在 NONZERO fill 下抵消，造成可见断口，因此不是可交付实现。
最终版本保留 SVG 中心线并使用 VG-Lite 原生 cap/join tessellation，同时修复 stroke cache
的 path type、线宽比较和一次性工作链表生命周期。最终 60 帧为 60 render / 60 submit、
26.08 FPS、collect 10.181 ms、encode 12.881 ms、finish 10.676 ms、GPU busy 27.32%，
Vector 2.390 ms/frame；software draw、batch boundary 和 overflow 仍全部为 0。
该版本同时修正 Vector 后端颜色值由错误的
`0xAABBGGRR` 为 VG-Lite 规定的 `0xAARRGGBB`，显存回读确认强调色 `#0078d7` 不再交换
红蓝通道。

### 10.2.2 字体与 SVG 的离线 VG-Lite 原生资源

当前 50 个 SVG 在主机侧直接编译成 `VG_LITE_FP32` fill/stroke 命令流和各自精确边界；
Noto Sans SC 的 7,586 个字形直接编译成 5,924,806 字节 canonical `VG_LITE_S16`
命令流。四档字号共用字形轮廓，运行时只设置矩阵和颜色。两类资源都通过只读 wrapper
引用 XIP 常量，不执行 SVG/XML/TTF 解析，不复制命令，不再走 LVGL path 到 VG-Lite path
的逐操作翻译。旧四档 A8 中文字体源不再参与产品编译。

实板重新验证后确认：PSE84 的 CALL/RETURN 本身可用。旧版上传器只按 64 位对齐复制原始
fill/font 路径；当路径长度恰好对齐且末尾是 `CLOSE` 时，DATA 后立即出现 `RETURN`。`CLOSE` 只闭合
当前轮廓，不是流终止符，GC265 因而继续把 `RETURN` 当路径数据并停滞。非对齐路径之所以
偶尔正常，是零填充碰巧充当了 `END`。当前驱动扩大 DATA payload，在被上传路径的
`RETURN` 前无条件追加 32 位 `END`，并 clean 完整上传块 D-Cache；
`LV_VG_LITE_USE_PATH_UPLOAD=1` 只表示允许经过明确 opt-in 的离线 SVG/字体原生 fill 路径
持久上传。LVGL 临时转换的运行时路径保持内联；它们的生命周期短于 GPU command stream，
不能仅因被标为 immutable 就自动转成 CALL。

验证不是停在 API 返回值：真正以 8 字节对齐 `CLOSE` 结束的内联反例会在约 5 秒超时并
报告 GPU idle `0x7ffffffe`；同一字节流经修复后的上传区变为
`CLOSE, END, padding, RETURN`，3 ms 完成且 1,200 像素校验和为 `0x9cc8104d`。另有真实
1,156 字节 SVG 同帧 100 次 CALL、跨 100 次独立提交复用，以及完整桌面 60/60 帧测试，
均无停滞。非 FreeRTOS HAL 的有限等待也已修正，`vg_lite_finish()` 异常不再无限自旋。

必须区分“CALL 收到完成 IRQ”和“输出像素正确”。后续 framebuffer A/B 发现，
`vg_lite_update_stroke()` 生成的最终 stroke stream 若独立 upload/CALL，虽然 finish 成功，
System、Media 等图标仍会丢失或破坏部分描边；相同 stream 内联到主 command buffer 时画面
正确。因此产品当前为 `LV_VG_LITE_USE_PATH_UPLOAD=1`、
`LV_VG_LITE_USE_STROKE_UPLOAD=0`：fill/字体使用持久 CALL，stroke 由 GPU 内联执行。
这仍是同一条整帧 GPU 命令链，最新三轮基准均为 60 帧/60 submit，没有增加软件绘制或
同步边界。在逐像素等价测试通过前，不得重新启用 stroke CALL。

全矢量后真实子裁剪会改变 scissor。官方驱动原先把纯 `scissor_dirty` 当成 render-target
变化并先 flush，使一帧被拆成 5 个 job。当前驱动只对 target/mirror/gamma/flexa 变化同步；
scissor 通过有序 `0x0A13` 状态写留在同一链。最终 60 帧为 60 submits，encode 从
16.901 ms/frame 降至 6.875 ms/frame；Vector 从 11.801 ms 降至 1.687 ms。字体 descriptor
缓存为 11,926/8，70 glyph/frame 的命令编码为 3.366 ms，布局/查找为 0.648 ms。

这里的 **100% GPU 任务路由** 与 **约 24–25% GPU 核心 busy** 是两个不同指标：前者表示本场景没有回退到软件绘制，后者由 GPU 提交到完成中断之间的实际忙时除以 60 帧基准墙钟时间得到。静态桌面不连续重绘时，累计平均 busy 会更低；这不代表 GPU 路径失效。

串口可执行 `feather_ui_bench`，它在 LVGL 线程中串行触发 60 次全屏重绘，并输出真实 render 数、FPS、collect/encode/finish、submit 数和各 primitive 编码耗时；`feather_ui_status` 用于读取累计统计、扫描等待和超时。

### 10.2.3 Tile 矩阵直绘与 Layer/Border 假热点闭环

旧 Tile 选中动画由 LVGL 先创建中间 Layer，在 Layer 内画完 Tile，再把中间纹理缩放合成到
主 framebuffer。编辑态基准只有 12.58 FPS、约 10 submits/frame；统计看起来是 Layer
14.738 ms/frame、Border 13.786 ms/frame。逐调用取证确认 Border 几何本身并没有消耗
13 ms：它恰好是 render-target 切换后的第一个任务，计时包含前一个 Layer 的
`vg_lite_finish()`；Layer 时间也主要是同一同步边界。因此这两个数是等待归属，不是 CPU
在软件画边框。

`LV_DRAW_TRANSFORM_USE_MATRIX=1` 后，纯缩放、全不透明且无复杂裁剪的对象把 LVGL 任务矩阵
直接乘入 VG-Lite 全局矩阵。Tile 的 fill、border、label、SVG 和四角 Chevron 直接画入主
framebuffer，不再生成中间纹理。首轮编辑态从 12.58 提升到 23.92 FPS，10 降为
1 submit/frame，Layer 归零、Border 降至 0.187 ms/frame。最终正式固件实测：

| 场景 | FPS | collect | encode | finish | submit | Layer | Border |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 静态桌面 | 25.78 | 13.009 ms | 11.061 ms | 10.921 ms | 1/frame | 0 | 0.063 ms |
| Tile 编辑动画 | 23.21 | 14.118 ms | 12.256 ms | 11.146 ms | 1/frame | 0 | 0.185 ms |

OpenOCD 在不 reset/halt 的情况下回读两张 512-stride RGB565 scanout framebuffer；A/B 只有
686 像素（0.18%）差异，包围盒 `x=20..307, y=96..207`，全部位于正在缩放的选中 Tile，
状态栏、导航栏和交界区域逐像素一致。Tile 本体、文字、SVG、边框和四个内嵌箭头的矩阵及
层次顺序一致，没有用“只缩放外框”换取性能。

也验证过更激进的 render-target 依赖链：先让 GPU 写中间 Layer，不等待就切回主目标，并把
刚才的 Layer 当纹理源继续编码。当前 PSE84/GC265 在该 read-after-write 链上真实停滞，
因此带透明度、滤镜或复杂裁剪的通用 Layer 仍保留安全 `finish`。这类跨目标依赖不能与同一
render target 上可排序的 fill/border/vector 命令混为一谈；在没有硬件 fence/依赖证明前，
不得为了追求“一条链”删除该边界。

路径上传也采用同样的证据边界。256 KiB 上传预留消除了原先的
`VG_LITE_OUT_OF_MEMORY`，但把全部运行时转换路径自动上传后，全量页面压力仍可在通知/页面
生命周期切换中停滞。产品最终只允许 `attach_native()` 的离线字体/SVG fill 使用 CALL，
运行时转换路径内联，stroke CALL 继续关闭。该组合完整自动测试为 351 PASS / 1 FAIL / 163
actions、100,520 ms，无 OOM、无 GPU hang；唯一失败是测试环境中 M33 无线驱动能力不可用，
与图形链无关。证据日志为 `COM17-direct-matrix-autotest-native-upload-only.log`、
`COM17-direct-matrix-release-wallpaper-static.log` 和
`COM17-direct-matrix-release-edit-bench.log`。

### 10.2.4 GPU/CPU 跨帧流水线

此前每帧末尾立即执行 `vg_lite_finish()`，M55 在 GPU 完成约 10.9--11.1 ms 的整帧命令链时
同步等待。虽然已经做到一帧一次 submit，CPU 的下一帧对象遍历、样式解析、文字布局和命令
编码仍不能与 GPU 并行，静态桌面和 Tile 编辑态分别只有 25.78 和 23.21 FPS。

当前实现改为两个资源槽和 VG-Lite 的两个交替 command buffer：帧 N 只 `flush` 提交，帧 N+1
立即在另一个槽中收集、解析资源并编码；到下一次提交边界才回收帧 N。时序关系为：

```text
CPU:  collect/encode N+1 | retire N | submit N+1
GPU:       execute N     |  END IRQ | execute N+1
DC :                       commit N | scanout N
```

- 解码图片、gradient 和 transient arena 按帧槽隔离；GPU 完成前不会释放上一帧引用的内存。
- `vg_lite_wait()` 只等待已经提交的 command buffer，不会误提交 CPU 正在编码的备用 buffer。
- GFX END 中断释放 RT-Thread semaphore；等待时 M55 线程睡眠，可把 CPU 时间让给音频、USB、
  文件系统等后续任务，不再以 1 us 轮询占满内核。
- 高优先级 scanout worker 在匹配的 GPU END 后才把 framebuffer 交给 DC，避免显示未完成表面；
  DC `DISP0` 中断确认旧表面已消费后才允许再次使用。
- 基准在计时前、最后一帧后各 drain 一次，确保 N 个 render 对应 N 个已完成 GPU job，动画
  场景不会把基准外的飞行帧误算成第 61 个 job。

当前 GC265 只有一个执行队列，因此“可串行”是把同一帧 fill/border/label/vector 等全部按依赖
顺序放在一个 GPU command chain 中；“重叠”是让 CPU 准备下一帧与 GPU 执行上一帧并行。
这不等于同时向硬件提交两个相互独立的 job，也不会跨越已证实不安全的 render-target
read-after-write 边界。

重新上电后的实板全屏数据如下；两种场景均为 GPU 路由 100%、60 render / 60 submit /
60 completed jobs，software/resource/explicit boundary 为 0：

| 场景 | FPS | collect | encode | 帧末残余等待 | GPU busy |
| --- | ---: | ---: | ---: | ---: | ---: |
| 静态桌面（3 轮） | 50.84--51.32 | 约 7.33 ms | 6.54--6.74 ms | GPU 0.217--0.220 ms；scanout 4.70--4.95 ms | 54.2--54.7% |
| Tile 编辑压力（8 轮/480 帧） | 50.50--51.59 | 8.04--8.06 ms | 7.55--7.78 ms | GPU 0.220--0.223 ms；scanout 2.73--3.30 ms | 55.1--56.3% |

`finish` 统计现在表示流水线回收阶段的残余阻塞，不再等于 GPU 的完整执行时长。GPU 每帧约
10.9 ms 的实际工作绝大部分已经藏在下一帧 CPU 准备阶段后面；当前主瓶颈转为 CPU 的
collect+encode（约 14--15.8 ms）以及 60 Hz video-mode 扫描相位，而不是 GPU completion
等待。静态桌面相对旧 25.78 FPS 提升约 97%，编辑态相对旧 23.21 FPS 提升约 118%。

调试阶段曾捕获一次“GPU 寄存器已 idle、UI 软件仍停留”的尾帧异常。等待现在限定 100 ms，
超时后复核 core interrupt/idle；若硬件已完成但 wrapper IRQ 丢失，则补发同一完成序列，若
硬件仍忙则保留错误而不伪造成功。加入阶段号后连续 8 轮编辑压力均结束于 `stage=0`，无
scanout timeout。证据日志为 `COM17-gpu-pipeline-final-static-3x.log` 和
`COM17-gpu-pipeline-stage-stress.log`。

### 10.2.5 全场景实板压力基线（2026-09-02）

新增 `feather_ui_scene <id>`，可重复建立 17 个路由页面和 10 个覆盖层/交互状态；主机脚本
`tools/freather/benchmark-ui-scenes.cmd` 对每个状态执行 60 个全屏帧，采集 CPU collect、
GPU 命令 encode、流水线残余 wait、GPU IRQ busy、各 primitive 和 submit/job 数。基准期间
暂停 M55 IPC 周期日志，避免串口输出交错破坏记录；独立看门狗只在连续 1 秒没有新帧时报告
`stage/slot/present`，正常测量不打印。逐帧触发仍使用 LVGL 的一次性 async timer，确保下一帧
发生在下一轮 `lv_timer_handler()`，不在 `RENDER_READY` 的无效区清零窗口内直接重入。

Infineon OpenOCD 显式 reset/run 并等待 UI 完成启动后，27 个场景共 1,620 个全屏帧全部完成，
每个场景均为 60 render / 60 batch，GPU job 与 submit 数严格相等，scanout timeout 为 0。
以下 `CPU` 为 collect+encode；`finish` 是双槽流水线已经重叠后仍暴露在帧尾的残余等待，并非
GPU 完整执行时间。

| 场景 | FPS | CPU collect+encode | finish | GPU busy | submit/frame | 首要压力 |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| Home | 51.15 | 7.41 + 6.69 ms | 4.29 ms | 54.66% | 1 | 均衡 |
| Search | 51.28 | 7.69 + 7.10 ms | 3.95 ms | 64.04% | 1 | 均衡 |
| System | 24.84 | 13.19 + 25.57 ms | 0.14 ms | 37.25% | 1 | 379 glyph/frame，Label 20.51 ms |
| Settings | 33.65 | 12.35 + 16.14 ms | 0.36 ms | 48.47% | 1 | Label 11.47 ms |
| Media | 51.72 | 5.56 + 5.78 ms | 6.68 ms | 52.02% | 1 | 扫描相位 |
| Recorder | 26.08 | 8.16 + 28.32 ms | 0.09 ms | 27.17% | **3** | 批次拆分、Label 9.40 ms |
| Gallery | 50.80 | 7.37 + 9.52 ms | 1.96 ms | 55.41% | 1 | Label 6.49 ms |
| Files | 50.84 | 6.05 + 8.45 ms | 4.22 ms | 54.51% | 1 | 扫描相位 |
| About | 50.89 | 3.54 + 8.19 ms | 7.03 ms | 52.17% | 1 | GPU/扫描相位 |
| Settings / Display | 50.84 | 4.48 + 8.29 ms | 5.83 ms | 53.76% | 1 | GPU/扫描相位 |
| Settings / Audio | 32.39 | 12.13 + 17.42 ms | 0.20 ms | 39.56% | 1 | 198 glyph/frame |
| Settings / Wi-Fi | 50.89 | 4.07 + 8.41 ms | 6.14 ms | 54.82% | 1 | GPU/扫描相位 |
| Settings / Bluetooth | 51.67 | 4.03 + 6.83 ms | 6.91 ms | 53.58% | 1 | GPU/扫描相位 |
| Settings / Storage | 37.90 | 10.96 + 13.73 ms | 0.63 ms | 43.01% | 1 | CPU 页面遍历与文字 |
| Settings / USB | 31.00 | 10.82 + 19.03 ms | 0.16 ms | 38.39% | 1 | 229 glyph/frame，Label 13.89 ms |
| Settings / Time & Language | 48.07 | 6.98 + 12.23 ms | 0.74 ms | 54.13% | 1 | 161 glyph/frame |
| Settings / Personalization | 38.53 | 9.62 + 13.84 ms | 1.70 ms | 42.64% | 1 | Vector 3.18 ms |
| All Apps | 51.59 | 6.58 + 6.83 ms | 4.95 ms | 54.91% | 1 | 均衡 |
| Shade open | 33.07 | 13.08 + 15.80 ms | 0.65 ms | 50.37% | 1 | Vector 5.35 ms、Label 7.67 ms |
| Shade mid-drag | 51.36 | 8.26 + 9.09 ms | 1.44 ms | **70.32%** | 1 | 本组最高 GPU 压力 |
| Search + keyboard | 24.27 | 16.38 + 23.83 ms | 0.14 ms | 34.83% | 1 | 键盘对象/Fill 与文字 |
| Settings + keyboard | **16.03** | **22.99 + 38.07 ms** | 0.14 ms | 26.07% | 1 | 全组最差，229 glyph/frame |
| Tile edit | 51.32 | 8.29 + 8.67 ms | 1.78 ms | 56.17% | 1 | 已消除中间 Layer |
| Gallery viewer | 25.92 | 6.25 + 7.02 ms | **23.97 ms** | 56.44% | 1 | 大图 GPU 执行/scanout 等待 |
| Files action menu | 24.35 | 11.22 + 28.96 ms | 0.11 ms | 36.88% | **14** | Layer 11.79 ms、批次拆分 |
| Media playing | **51.81** | 5.55 + 5.77 ms | 6.63 ms | 52.04% | 1 | 本组最高 FPS |
| Alert | 25.95 | 9.65 + 27.94 ms | 0.09 ms | 36.56% | **13** | Layer 11.42 ms、批次拆分 |

27 场景算术平均为 40.30 FPS：14 个场景达到 48 FPS 以上，6 个在 30--48 FPS，7 个低于
30 FPS。结论不是“GPU 不够快”：最差的 Settings+keyboard 中 GPU 仅忙 26.07%，CPU 每帧
准备 61.06 ms；System、Search+keyboard、USB/Audio 设置页也都是 CPU collect/Label 编码
占主导。Gallery viewer 是明确例外，CPU 仅 13.26 ms，残余 finish 达 23.97 ms，应沿大图
blit、framebuffer 依赖与 scanout 相位继续查。Recorder、Files action、Alert 分别为 3、14、
13 submit/frame，违背目标的一帧一次提交，是下一轮最高优先级的批次合并对象。

完整机器可读数据和原始串口证据为
`tools/freather/logs/COM17-ui-scene-benchmark-clean-final-20260902.csv` 与同名 `.log`。
该套件测量的是每个稳定视觉状态的连续全屏重绘；真实手指拖动的 input-to-present 延迟、页面
切换动画逐帧曲线仍应作为独立动态基准补充，不能用这里的静态状态 FPS 代替。

### 10.3 ITCM 与图形内存预算

外部 SMIF XIP 上执行大量短函数曾是 CPU 编码慢的主要原因。产品链接脚本现在只把每帧必经路径放入 M55 ITCM：VG-Lite command encoder、LVGL refresh/draw/object/style/字体解析热路径；页面初始化和冷业务代码仍留在 XIP。

| 区域 | 当前 ELF | 容量 | 余量 |
| --- | ---: | ---: | ---: |
| `.app_code_itcm` | `0x36480` = 222,336 B | 256 KiB | 39,808 B |
| `.cy_gpu_buf` | `0x2ee000` = 3,072,000 B | 3 MiB | 73,728 B |

`.cy_gpu_buf` 包含双 direct-scanout framebuffer、VG-Lite 命令/细分相关内存、256 KiB
path-upload 预留、两个各 192 KiB 的 frame transient arena，以及 192 KiB 持久 glyph bitmap
arena。`lv_gpu_batch.o` 合计占 576 KiB，显示/VG-Lite 区占 2,424 KiB；当前只余 72 KiB。
每次修改 framebuffer 数量、stride、命令缓冲或缓存大小时都必须重新检查 ELF section，
不能只看 C 数组的名义大小。

### 10.4 Cache、IRQ 与未使用能力

- VG-Lite 仅在唯一提交前 clean 本帧 transient 范围；持久 glyph 只在首次写入时 clean，不在每个 glyph/blit 上执行全 Cache 维护。
- DC 驱动显式打开 `DISP0` frame interrupt；present semaphore 只在确有等待者时释放，不积累陈旧 token。异步 present 后累计线程阻塞等待平均约 1–3 ms、最大 19 ms、0 timeout。
- 两张 framebuffer 直接作为 LVGL draw buffer 和 DC scanout buffer，不再存在旧的 persistent framebuffer copy 路径。
- 当前仍未使用 DC overlay、hardware cursor、RLAD、DSI command-mode 局部刷新和 90°/270° GPU 整帧旋转。
- `FEATHERTALK_USING_GPU_UI`/`libraries/FeatherUI` 保留为 GPU 原生实验与对照实现，不是当前产品默认 UI。

## 11. 性能数据应如何理解

官方 advanced graphics training 在 Infineon E84 参考板、LVGL music 示例上给出的参考结果为：

| 场景 | GPU 开 | GPU 关 |
| --- | --- | --- |
| 音乐暂停 | 53–60 FPS，约 10% CPU | 约 50 FPS，约 17% CPU |
| 音乐播放 | 40–50 FPS，约 30–40% CPU | 20–25 FPS，约 77–90% CPU |

这只能证明 VG-Lite 对同类 LVGL 负载有效，**不是 Edgi-Talk/FeatherTalk 的实测数据**。本产品的壁纸、透明层、字体、dirty copy、全缓冲 clean 和 DSI video mode 都会改变结果。

当前 UI 的 FPS 监视器反映 LVGL 的刷新节奏，不等于 GPU utilization；CPU usage 也不能说明 GPU 是否饱和。严谨测量至少需要：

- LVGL draw task 的 GPU/SW 分派数；
- 每帧 GPU flush/finish 次数与等待时间；
- dirty 像素数和 DMA/CPU copy 字节数；
- full-cache-clean 时间；
- frame-done 等待时间；
- GFXSS IRQ/AXI error；
- GPU on/off 的同场景 A/B 测试和功耗测量。

## 12. 已确认的风险与技术债

| 优先级 | 问题 | 影响 |
| --- | --- | --- |
| P0 | System 页把 GFXSS 父时钟 400 MHz 写成 GPU/显示时钟 | 误判 GPU 实际频率，应拆为 HF/GPU/DC |
| P0 | 静态文字仍约 3.62–3.79 ms/frame（当前回归场景） | 字形已使用持久 CALL，但约 69–70 glyph/frame 仍产生逐字形状态/矩阵/CALL 编码；应评估稳定文本 compound path/layer cache |
| P0 | Stroke upload/CALL 完成但像素不等价 | 当前强制内联且画面正确；启用前必须建立 framebuffer 逐像素 A/B 与多图标回归 |
| 已闭环 | Stroke 最终轮廓复用中心线 bounds，导致矩形裁剪 | 最终 bounds 扩张半线宽 + 1 px AA guard；8 个图标 ROI 的 >1 px 拓扑差异由 413 降为 0 |
| P1 | M55 ITCM 仅余 39,808 B | 继续迁移热代码前必须用分型统计证明收益，不能把整套 UI 冷代码搬入 ITCM |
| P1 | FULL render 每个动画帧都重绘 480×800 | 保证单链和无脏块，但静态/局部动画仍有减少任务数量的空间 |
| P1 | 当前 busy 是基于提交/完成 IRQ 的时间占比 | 已可区分任务路由与硬件 busy，但还没有 GPU 内部计数器/AXI 带宽占用 |
| P1 | DSI video mode 持续整帧扫描 | LVGL partial 不能直接降低面板链路功耗 |
| P2 | 历史文档仍可能写 160-line buffer | 与当前 `BSP_LVGL_DRAW_BUF_LINES=800` 不一致 |
| P2 | GPU 不原生支持 RGB888 目标 | 未来 24-bit UI 需要重新评估格式、带宽和 DC 转换 |
| P2 | GC355 历史资料与当前 GCNano Ultra V 混用 | 可能误用旧寄存器、命令格式、MMU、频率、像素格式或性能结论 |

## 13. 建议的下一阶段

1. **修正信息口径**：System 页显示 `GFXSS HF 400 / GPU2D 200 / DC 200 MHz`，并标明这是时钟而非利用率。
2. **整段文字缓存**：glyph descriptor 与原生轮廓缓存已落地；下一步优先评估稳定字符串 compound path，必要时才缓存 A8/RGB565 layer，失效受字体、文本、颜色/opa、布局和 DPI 控制。
3. **静态层复用**：把壁纸、状态栏和未变化 Tile 组合为 retained layer，动画帧只重放变化节点，最后仍合成到同一个 GPU command chain。
4. **交互基准**：为通知栏拖动、Tile 缩放/移动、页面切换和相册切图各建立固定 `feather_ui_bench` 变体，记录 input-to-present 延迟而不只看静态 FPS。
5. **继续按 primitive 取证**：Label 和 Image 之外的图元当前不足 1 ms/frame，不应再投入无收益的通用微优化。
6. **再考虑 overlay/RLAD**：只有视频层、静态壁纸或功耗数据证明值得时再启用，避免把固定功能图层变成新的复杂源。
7. **建立 FeatherTalk 基线**：GPU on/off、壁纸 on/off、通知面板拖动、Tile 编辑动画和相册切图均跑固定自动化脚本并保存帧率/耗时。
8. **提炼 GC355 验证方法**：把旧包的 Blend、Image、Mask、Paint、Pixel format、命令完成和错误中断测试思想重建为 PSE84 VG-Lite 自测，不复制旧命令流。

## 14. 快速核对清单

- [ ] 器件号是否仍为 `PSE846GPS2DBZC4A`。
- [ ] `CLK_HF1` 是否仍为 400 MHz，GPU/DC 是否仍为二分频。
- [ ] `gfx_mem` 是否仍为 `0x26200000`、3 MiB。
- [ ] LVGL 是否仍启用 `LV_USE_DRAW_VG_LITE=1`。
- [ ] draw buffer lines 是否仍为 800，rotation 是否仍为 0°。
- [ ] DC 是否只启用 graphics layer，RGB565 stride 是否仍为 512。
- [ ] DSI 是否仍为 2×900 Mb/s、pixel clock 33.984 MHz。
- [ ] AXI-DMA threshold 是否仍为 8 KiB。
- [ ] 修改 Cache、buffer 或 rotation 后是否重新核对 map 文件中的 `.cy_gpu_buf`。
- [ ] UI FPS 是否明确标注为 LVGL 帧率，而不是 GPU 利用率。
- [ ] 引用 GC355 资料时是否明确标成“历史架构参考”，并用 PSE84 当前资料/实机结果复核。

## 15. 结论

当前 PSE84 的图形能力不是“只有一个 2D 加速器”，而是一条完整的 **GPU2D 渲染 + DC 多层扫描合成 + MIPI-DSI 输出**链路。FeatherTalk 当前保留 LVGL 完整功能层，并把底层改为整帧任务收集、单次 GPU 提交和双 RGB565 framebuffer 直接 scanout；FeatherUI 则保留为 GPU 原生对照实现。

眼下最大的优化空间是减少官方 VG-Lite API 的逐图元 CPU 编码成本，并继续保持 `1 submit / 1 complete / frame`；不能以恢复逐命令 flush、CPU 软件画像素或整帧 copy 换取表面功能进度。同时应保持一个明确边界：GPU core 当前是 200 MHz，DC 和 MIPI 是独立模块，AXI-DMA 也不是 GPU。只有把 collect、encode、GPU execute、present 和输入 I/O 分别测量，后续帧率、功耗和脏块问题才能被稳定定位。

GC355 完整交付包为这一过程提供了非常有价值的 Vivante 内部架构和验证方法参照，尤其适合反推命令缓冲、总线访问、IRQ 完成、路径细分和像素写回的故障链；但现芯片能力、寄存器和性能结论始终以 PSE84 当前官方资料、当前驱动 feature 表和实机验证为准。
