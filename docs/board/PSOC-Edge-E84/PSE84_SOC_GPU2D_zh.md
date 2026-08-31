# PSoC Edge E84 SoC 资源与 GPU2D 技术梳理

更新时间：2026-08-31

适用硬件：Edgi-Talk PSoC Edge E84 开发板

适用器件：`PSE846GPS2DBZC4A`（PSoC Edge E84、EPC2、BGA-220）
适用工程：`FeatherTalk_M33` + `FeatherTalk_M55`

## 1. 文档目的与口径

本文把容易混淆的四个层次分开：

1. **PSoC Edge E8x 系列上限**：数据手册和架构参考手册描述的系列能力。
2. **PSE846GPS2DBZC4A 实际 SKU**：当前芯片真正带有的 CPU、NPU、GFX 和存储资源。
3. **Edgi-Talk 板级资源**：原理图上实际焊接并连到 SoC 的 Flash、RAM、屏幕、无线、音频和接口。
4. **FeatherTalk 当前配置**：链接脚本、RT-Thread、LVGL、VG-Lite 和显示驱动现在真正启用的路径。

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

## 10. FeatherTalk 当前 GPU2D/LVGL 实现

### 10.1 编译配置

| 配置 | 当前值 | 含义 |
| --- | --- | --- |
| LVGL | 9.2.0 | 主 UI 框架 |
| `LV_COLOR_DEPTH` | 16 | RGB565 |
| `LV_USE_DRAW_VG_LITE` | 1 | 开启项目内 VG-Lite draw unit |
| `LV_VG_LITE_USE_GPU_INIT` | 0 | GPU 由 LCD 驱动初始化，避免重复 init |
| `LV_VG_LITE_FLUSH_MAX_COUNT` | 16 | LVGL 提交节奏阈值 |
| VG-Lite box shadow | 关闭 | 阴影走其他路径/降级 |
| gradient cache | 32 | 线性渐变缓存 |
| stroke cache | 32 | 软件 stroke path 缓存 |
| `BSP_LVGL_DRAW_BUF_LINES` | 800 | 0°时每张 draw buffer 为完整 480×800 |
| LVGL render mode | PARTIAL | 即便 draw buffer 是全屏大小，仍按脏区 flush |
| AXI-DMA area copy | 开启 | 大于等于 8 KiB 的脏区尝试 DMA |
| LCD rotation | 0° | 当前不分配额外 rotation scanout buffer |
| `vg_lite_init()` | `vg_lite_init(120, 200)` | 当前 tessellation window 为逻辑宽高各除以 4 |

仓库同时还保留 LVGL 上游名为 `LV_USE_DRAW_VGLITE` 的另一套配置，但当前为 0。真正启用的是项目移植的 `LV_USE_DRAW_VG_LITE`，两者名称相近，排查时不要看错。

### 10.2 当前 `.cy_gpu_buf` 静态预算

0°、480×800、RGB565 下：

| 对象 | 计算 | 字节 |
| --- | --- | ---: |
| LVGL draw buffer 1 | 480×800×2 | 768,000 |
| LVGL draw buffer 2 | 480×800×2 | 768,000 |
| persistent framebuffer | 512×800×2 | 819,200 |
| VG-Lite command buffers | 32 KiB×2 | 65,536 |
| VG-Lite tess buffers | 480×128×2 | 122,880 |
| **合计** |  | **2,543,616 bytes（约 2.426 MiB）** |
| `gfx_mem` 剩余 | 3 MiB - 合计 | **602,112 bytes** |

当编译为 90°/270°时，代码会把 LVGL buffer 自动限制为 384 行，并增加 512×800×2 的 scanout：

- 预计合计约 3,086,336 bytes；
- 3 MiB 区仅余约 59,392 bytes；
- 不是当前配置下立即越界，但余量极低，任何新增 `.cy_gpu_buf` 都可能导致链接失败。

若以后需要旋转与更多图形缓存，应显式选择：减少 LVGL lines、扩大 SoCMEM `gfx_mem`、把非 GPU 热数据移到 HyperRAM，或重构为真正 direct/full framebuffer 交换。不要无审计地继续向 `.cy_gpu_buf` 加静态数组。

### 10.3 当前 flush 与 Cache 所有权

一次典型脏区更新为：

1. LVGL software/VG-Lite 在 `disp_buf1/2` 完成绘制。
2. `disp_flush()` 把脏区传给 `lcd_flush_rgb565_area()`。
3. 面积达到 8 KiB 时，源 draw buffer 和目标 framebuffer 先 clean D-Cache，再由 AXI-DMA 做 2D copy；DMA 完成后 invalidate 目标范围。
4. 小区域或 DMA 不可用时用 CPU `memcpy`。
5. 最后一次 flush 调用 `lcd_present_framebuffer()`，对完整 render framebuffer clean D-Cache。
6. DC 绑定/继续扫描 persistent framebuffer，驱动等待 frame-done 信号。

当前做法优先保证显示一致性，但每个 present 都 clean 完整 819,200-byte render buffer，是状态栏下拉、动画和高频小脏区时的潜在性能成本。后续可以在严格证明 DC/Cache 边界正确后，把最终 clean 收敛为累计 dirty rectangles 或使用 non-cacheable scanout。

CPU fallback copy 后没有单独 invalidate，因为写入由 CPU 自己完成；最终 full clean 负责把目标 framebuffer 写回给 DC。DMA 路径则必须先处理源和目标的脏 Cache line，避免 DMA 与 CPU cache line 互相覆盖。

### 10.4 当前没有使用的硬件能力

- DC overlay0/overlay1；
- hardware cursor；
- RLAD scanout compression；
- DSI command mode/真正面板局部刷新；
- 90°/270°整帧 VG-Lite 旋转（当前 rotation=0）；
- GPU radial gradient、blur、mask、stencil 等本就不存在的 feature；
- GPU 利用率硬件计数器和逐任务耗时采集。

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
| P0 | 每次 present clean 完整 scanout/render buffer | 动画和通知面板可能产生明显带宽与延迟成本 |
| P1 | 0°使用两张全屏 draw buffer，却仍为 PARTIAL + copy-to-scanout | 占 1.5 MB 且仍有一次搬运，需和 direct/full 模式做 A/B |
| P1 | 90°/270°的 3 MiB 图形区只余约 58 KiB | 后续资源增长极易链接失败 |
| P1 | UI FPS 不是 GPU 利用率 | 难以定位 GPU、Cache、DMA、DC 或 DSI 谁是瓶颈 |
| P1 | DSI video mode 持续整帧扫描 | LVGL partial 不能直接降低面板链路功耗 |
| P2 | 历史文档仍可能写 160-line buffer | 与当前 `BSP_LVGL_DRAW_BUF_LINES=800` 不一致 |
| P2 | GPU 不原生支持 RGB888 目标 | 未来 24-bit UI 需要重新评估格式、带宽和 DC 转换 |
| P2 | GC355 历史资料与当前 GCNano Ultra V 混用 | 可能误用旧寄存器、命令格式、MMU、频率、像素格式或性能结论 |

## 13. 建议的下一阶段

1. **修正信息口径**：System 页显示 `GFXSS HF 400 / GPU2D 200 / DC 200 MHz`，并标明这是时钟而非利用率。
2. **加入图形诊断**：统计 GPU/SW draw task、flush/finish、DMA/CPU copy、dirty 面积、Cache clean 和 frame-done。
3. **做三组缓冲 A/B**：160 行 partial、320/384 行 partial、当前 800 行 partial；记录 FPS、CPU、峰值 heap 和交互延迟。
4. **评估 direct/full 模式**：如果能安全地让 LVGL 输出成为 DC framebuffer，可去掉一次 copy；必须先解决 DC 扫描期间撕裂和双缓冲交换。
5. **优化 Cache**：先量化 full clean 成本，再决定 dirty-range clean 或 non-cacheable scanout，不能只凭视觉现象删 Cache 操作。
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

当前 PSE84 的图形能力不是“只有一个 2D 加速器”，而是一条完整的 **GPU2D 渲染 + DC 多层扫描合成 + MIPI-DSI 输出**链路。FeatherTalk 已经真正启用 VG-Lite 加速 LVGL，并用 AXI-DMA 搬运较大脏区；但当前仍是 RGB565 单 DC 图层、DSI video mode、partial render 复制到固定 scanout 的实现。

眼下最大的优化空间不是继续堆叠动画 API，而是量化并收敛 **全屏 Cache clean、双全屏 draw buffer、脏区 copy 和 frame-done 等待**。同时应保持一个明确边界：GPU core 当前是 200 MHz，DC 和 MIPI 是独立模块，AXI-DMA 也不是 GPU。只有把这几条路径分别测量，后续帧率、功耗和脏块问题才能被稳定定位。

GC355 完整交付包为这一过程提供了非常有价值的 Vivante 内部架构和验证方法参照，尤其适合反推命令缓冲、总线访问、IRQ 完成、路径细分和像素写回的故障链；但现芯片能力、寄存器和性能结论始终以 PSE84 当前官方资料、当前驱动 feature 表和实机验证为准。
