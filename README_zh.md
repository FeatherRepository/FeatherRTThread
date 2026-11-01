# FeatherTalk — 基于 PSoC Edge E84 的双核智能音频终端

**中文** | [**English**](./README.md)

> 在 RT-Thread 上把 PSoC Edge E84 的 Cortex-M55 / Cortex-M33 双核、LVGL + VG-Lite、蓝牙音频、USB、Wi-Fi、本地语音唤醒和 NPU 基准整合成一套可构建、可烧录、可诊断的综合 Demo。

![Edgi-Talk](docs/figures/board_large.jpg)

## 提交材料速览

| 提交材料 | 本文对应位置 | 当前状态 |
| --- | --- | --- |
| 项目概述 | 第 1 节 | 已整理 |
| 功能介绍 | 第 2 节 | 已整理 |
| 硬件框架说明 | 第 3 节 | 已整理 |
| 软件框架说明 | 第 4 节 | 已整理 |
| 开源软件代码 | 本仓库；目录见第 5 节 | 待整理提交链接或归档包 |
| 演示视频 | 第 10 节（含建议脚本） | 待录制 |

本仓库同时包含两层内容：

1. **Edgi-Talk 官方 BSP/SDK**：PSoC Edge E84 的 RT-Thread 板级支持、驱动和独立外设示例。
2. **FeatherTalk 产品工程**：在同一 SDK 上开发的双核综合应用，覆盖 UI、音频、USB、Wi-Fi、经典蓝牙、LE Audio、Auracast、本地唤醒词、NPU 基准和 AirPlay2 研究原型。

当前产品分支为 `product/edgi-talk`。FeatherTalk 不是另一个单核示例，而是 **`FeatherTalk_M33` 与 `FeatherTalk_M55` 两个配套工程及其共享模块**。

---

## 1. 项目概述

### 1.1 背景

端侧智能音频设备通常要同时处理触摸 UI、音频播放与录音、蓝牙连接、USB 音频、文件系统、无线网络和本地 AI。PSoC Edge E84 提供了 Cortex-M55、Cortex-M33、Ethos-U55、GCNano Ultra V GPU、DC8000Nano 显示控制器和丰富的外设，但官方资料更多是单个外设或单个中间件的示例，缺少一个把这些能力组织成完整产品形态的参考工程。

FeatherTalk 的目标不是再写一个单个外设或单个协议栈的演示，而是回答三个工程问题：

- 双核如何分工，才能让实时音频、无线协议栈和复杂 UI 互不拖累？
- 显示、音频、USB、存储和无线如何共享有限的 SRAM、HyperRAM、DMA 和总线带宽？
- 这些能力如何在同一个可重复构建、烧录和诊断的固件中协同工作？

### 1.2 项目定位

**一个可运行的双核综合 Demo，同时是一套面向产品化的架构验证平台。**

- 对评审：可以直接看到完整 UI、音频、蓝牙、USB、Wi-Fi、KWS 和 NPU 基准。
- 对开发：提供双核启动、IPC、音频仲裁、无线资源管理、构建烧录和测试工具链的参考实现。
- 对后续产品：边界清晰，实验性功能（AirPlay2、FPGA/Smart I/O 音频桥、自研底板）不会被误认为已经量产的交付物。

### 1.3 主要创新点

1. **双核 RT-Thread 产品化**：M33 负责启动、蓝牙、无线资源与跨核服务，M55 负责 UI、音频、USB、存储和 Wi-Fi；两核通过版本化 IPC 和共享 SRAM 协作。
2. **LVGL 9.2 + VG-Lite 整帧批处理**：LVGL draw task 按帧收集后编码为 VG-Lite 命令链，双 RGB565 framebuffer 同时承担渲染和 DC scanout，减少整帧复制。
3. **跨核音频数据面**：双向共享 SRAM ring + doorbell，承载 SBC/LC3 帧、格式和状态；`sound0` 通过 owner 机制仲裁本地播放、UAC、A2DP Sink、LE Audio 和 AirPlay2。
4. **A2DP Source 音源可选**：编码器可从 USB 主机 UAC tap 或本地播放器 tap 二选一，不再固定绑定 USB；本地播放器到蓝牙耳机走独立 `player_bt_tap`，不占用 `sound0`。
5. **无线共存管理**：Wi-Fi 与蓝牙各自拥有 claim、状态、复位和错误计数，普通功能开关不会切断另一功能或整组公共电源。
6. **共享 Flash 保护**：M55 擦写用户 Flash 时通过跨核守卫避免 M33 继续执行同一 SMIF 设备上的旧缓存代码。
7. **完整工具链**：双核构建、签名合并、OpenOCD 烧录校验、串口、UI 场景/GPU 基准、无线与音频测试均可重复执行。

### 1.4 主要成果

- 一套可启动、可交互、可烧录的 M33/M55 综合固件。
- 21 个 UI 页面、32 个可重复基准场景，覆盖桌面、搜索、设置、媒体、录音、文件、图库、蓝牙等。
- 本地音乐播放器支持 WAV/MP3、3D 封面流、专辑图、扬声器/蓝牙输出切换。
- 经典蓝牙 A2DP Sink/Source、LE Audio 单播、Auracast 广播源、配对持久化和扫描/连接 UI。
- 双向 USB UAC、USB MSC、USB 到蓝牙转发。
- Wi-Fi 扫描/连接、TCP 诊断和 AirPlay2 研究原型。
- 本地“小麦小麦”唤醒词全链路和 Ethos-U55 带宽基准工具。
- 可重复的构建、烧录、串口和测试脚本。

---

## 2. 功能介绍

### 2.1 桌面与系统交互

- 480×800 触摸屏，自适应中英文界面。
- 桌面、应用列表、通知栏、全局搜索、设置和多级子页面。
- 主题色、壁纸、时间格式、时区、语言和亮度等偏好持久化。
- 系统信息页展示双核、内存、存储、显示链路、无线和 IPC 状态。
- 提供 `feather_ui_scene`、`feather_ui_status`、`feather_ui_bench` 等串口诊断/基准命令。

### 2.2 音乐播放器

- 从 SD 卡或内置 Flash 选择文件夹，扫描直属 WAV/MP3 形成播放列表。
- MP3 使用 minimp3 解码；WAV 支持 8/16/24/32-bit PCM 和常见采样率。
- 3D Cover Flow 封面流：中心专辑放大、两侧透视、倒影和渐变遮罩，由 VG-Lite 2.5D 加速绘制。
- 支持专辑图 sidecar 文件；播放、暂停、上一曲/下一曲、进度、音量和文件夹循环。
- 输出设备可选 **板载扬声器** 或 **蓝牙耳机**；蓝牙 Source 音源可选 **USB 音频** 或 **本地播放器**。
- 媒体页与蓝牙设置页的音源选择双向同步；未连接耳机时切换蓝牙输出会给出提示。
- 蓝牙输出使用独立的 `player_bt_tap` 32 KB 环形缓冲，PCM 统一重采样到 48 kHz/16-bit/双声道再交给 SBC 编码。

### 2.3 录音、文件与图库

- PDM 数字麦克风录音，生成 WAV 文件，保存到 SD 卡或内置 Flash。
- 文件管理器浏览 `/sdcard` 与 `/flash`，支持进入目录、复制、粘贴、重命名、删除和格式化。
- 图库浏览 SD/Flash 图片，支持全屏预览和设为壁纸。
- 存储页显示容量、挂载状态和格式化操作，格式化前有明确的破坏性提示。

### 2.4 蓝牙音频

- 经典蓝牙：可发现、SSP 配对、link key 持久化、重连和 AVRCP。
- A2DP Sink：板子作为音箱接收手机推流。
- A2DP Source：板子把所选音源发送到蓝牙耳机；支持本地文件或 USB 音频。
- 扫描结果以固定高度可滚动列表展示，包含设备名、MAC 和连接标记；扫描后自动补查远端名称。
- LE Audio：BAP Unicast Server、双 CIS、LC3、PACS/ASCS/CAS/TMAP/VCS 和 LE bond 持久化。
- Auracast：BIG/BIS 广播源、周期广播、启停与单播互斥。
- A2DP 角色、LE Audio 角色和音频通道（双模/仅经典/仅 LE）在 UI 中可切换，互斥关系有明确提示。

### 2.5 USB 音频与存储

- USB UAC 扬声器：主机音频进入 M55，经 `sound0` 输出到板载 Codec/功放。
- USB UAC 麦克风：板载 PDM 麦克风音频上行到 USB 主机。
- 动态采样率/位深/声道协商，以及主机与板端时钟漂移校准。
- USB MSC：把 SD 卡或内置 Flash 用户盘暴露给 PC。
- USB 到蓝牙转发：UAC 音频可直接转发到 A2DP Source 耳机。

### 2.6 Wi-Fi 与 AirPlay2

- CYW5551x Wi-Fi 经 SDIO 接入 M55，WHD + lwIP 提供扫描、连接和 TCP/IP。
- Wi-Fi 与蓝牙独立启停、独立 claim、独立错误计数；普通开关不切断另一功能。
- AirPlay2 研究原型：mDNS、RTSP/HTTP、HAP transient 配对、会话/RTP 加密、ALAC/AAC-LC、type96/type103 和 `sound0` 输出。
- AirPlay2 在未连接 AP 时保持等待，不抢占音频设备；真实 iPhone 连续播放和 PTP 同步仍待验收。

### 2.7 本地语音唤醒与 NPU

- “小麦小麦”自训唤醒词模型，PDM 麦克风滑窗推理，开机自启，M33 事件联动和绿灯指示。
- 提供采集、训练、验证工具和两级 verifier；模型仍在迭代，召回率/误触率按模型版本单独验收。
- Ethos-U55 裸寄存器命令流、HyperRAM/Flash 权重读带宽基准和模型生成工具，用于评估 NPU 数据通路，不代表本地大模型推理已经完成。

### 2.8 开发与诊断

- M55 产品 Shell：`feather_m55_status`、`bt_status`、`ft_wifi`、`ft_airplay`、`feather_ui_status`、`ft_audio_stats`、`kws` 等。
- 音频/蓝牙专项命令：`feather_player`、`feather_bt_source`、`ft_sbc_enc_stats`。
- M33 UART5 诊断终端：`feather_status`、`feather_ping` 和底层蓝牙诊断。
- UI 场景切换、GPU/刷新/scanout/堆统计和 60 帧全屏基准。
- 双核构建、签名合并、OpenOCD 烧录校验、串口监控和主机侧测试脚本。

### 2.9 功能状态表

| 领域 | 已开发内容 | 当前状态与边界 |
| --- | --- | --- |
| 双核基础 | 双核启动、版本化 IPC、心跳、跨核音频 ring、共享 Flash 和无线资源管理 | 已整合；当前镜像启动和 IPC 基础检查通过 |
| 产品 UI | 中英文 Shell、滑动导航、本地播放、录音、文件、图库、存储、设置和蓝牙页面 | 已整合；UI 初始化、GPU 和导航检查通过，完整交互仍需随版本回归 |
| GPU 与显示 | VG-Lite 批处理、双 framebuffer direct scanout、背光控制与性能基准 | 已有专项板测；当前基础状态无溢出和 scanout 超时 |
| 本地音频与 USB | 本地播放（扬声器/蓝牙输出切换）、蓝牙 Source 音源可选、录音、双向 UAC、动态速率/时钟校准、USB MSC、USB 到蓝牙转发 | 功能已开发并有专项测试记录；仍需外部 USB 主机和试听回归 |
| 经典蓝牙 | BTstack、可发现、SSP 配对、link key 持久化、A2DP Sink/Source、音源选择、角色切换、重连和 AVRCP | USB 转发已有端到端出声记录；本地播放器到 A2DP Source 已完成编码、协商和输出切换路径，真实耳机复测待完成 |
| LE Audio | BAP Unicast Server、双 CIS、LC3、PACS/ASCS/CAS/TMAP/VCS、LE bond 持久化 | 已有真实手机连接与出声记录；长时连续性、更多手机和完整合规性未宣称完成 |
| Auracast | BIG/BIS 广播源、周期广播、启停与单播互斥 | BIG/BIS 数据面已打通；综合镜像完成两轮控制启停，接收端试听仍待验收 |
| Wi-Fi 与共存 | 双频扫描/连接、独立无线生命周期、性能优化、TCP/内存诊断 | 有双频短测和性能记录；当前镜像实扫通过，长时吞吐及音频并发仍待回归 |
| AirPlay2 | mDNS、RTSP/HTTP、HAP transient 配对、会话/RTP 加密、ALAC/AAC-LC、type96/type103、`sound0` 输出 | 研究原型；协议和模拟网络音频测试通过，真实 iPhone 连续播放、PTP 同步和长时间验收未完成 |
| 本地唤醒词 KWS | “小麦小麦”自训模型、PDM 麦克风、滑窗推理、开机自启、M33 事件联动、绿灯指示、采集/训练/验证工具和两级 verifier | 已有上板全链路验证；模型仍在迭代，最新模型的真人召回率与误触率须按版本单独验收 |
| Ethos-U55 NPU | 裸寄存器命令流、HyperRAM/Flash 权重读带宽基准和模型生成工具 | 研究与性能测量工具，不代表本地大模型推理功能已经完成 |
| 构建与诊断 | 双核构建、签名合并、OpenOCD 烧录校验、串口、PyOCD 辅助路径、UI/无线/音频测试 | 已整合并用于当前版本构建和上板验证 |
| 自研硬件 | Core 扩展架构、接口分配、USB+PMU 子板和设计约束 | 需求与设计文档阶段，不等同于硬件已投产或验证 |
| SPI 转 I2S | FPGA 音频桥设计资料；SoC SPI + Smart I/O 官方路线分析 | 未纳入当前综合固件；Smart I/O 驱动尚未开始开发 |

---

## 3. 硬件框架

### 3.1 板卡组成

当前验证平台是 Edgi-Talk PSoC Edge E84 开发板，采用“核心板 + 底板”形态：

- **核心板 Edgi-Talk_Core**：PSoC Edge E84（BGA-220）、QSPI NOR、HyperRAM、PDM/模拟音频前端、1.8 V 电源和板对板连接器。
- **底板**：BQ25606 充电、ES8388 Codec、MD8002 功放、CYW5551x Wi-Fi/BT 模组、MIPI-DSI 显示、SD 卡、CAN、树莓派 40-pin 扩展和 KitProg3 调试器。
- 核心板与底板通过 **2× DF40C 80-pin** 板对板连接器连接。
- 调试使用板载 KitProg3（SWD/JTAG、调试 UART），产品串口默认 115200 8N1。

### 3.2 SoC 资源

| 模块 | 硬件能力 | FeatherTalk 中的角色 |
| --- | --- | --- |
| Cortex-M55 | 最高 400 MHz；Helium/MVE、DSP、FPU；32 KiB I-Cache、32 KiB D-Cache；256 KiB ITCM、256 KiB DTCM | RT-Thread、LVGL、应用、显示、文件、USB 和音频主控 |
| Cortex-M33 | 最高 200 MHz；DSP、FPU、TrustZone；16 KiB I-Cache | 安全启动、蓝牙 Host、无线资源管理、M55 启动和跨核服务 |
| Ethos-U55 | 最高 400 MHz；128 MAC/cycle；INT8 峰值 51.2 GOPS | 硬件存在；当前用于 NPU 带宽基准，尚未装载业务模型 |
| NNLite | 与 M33 时钟域关联；4 MAC/cycle | 当前产品未使用 |
| GPU | GCNano Ultra V，VG-Lite 3.0 接口 | LVGL 2.5D 绘制、封面流、图形合成 |
| 显示控制器 | DC8000Nano + MIPI-DSI | 双 framebuffer direct scanout 到 480×800 面板 |

器件完整型号为 `PSE846GPS2DBZC4A`（PSoC Edge E84 Consumer，EPC2，BGA-220）。

### 3.3 存储与内存

| 资源 | 容量 | 用途 |
| --- | ---: | --- |
| Boot ROM | 64 KiB | 固化启动代码 |
| RRAM | 512 KiB | 安全启动、生命周期和用户 NVM 共用 |
| M55 ITCM / DTCM | 256 KiB / 256 KiB | 低延迟代码、数据和栈 |
| M33 subsystem SRAM | 1 MiB | M33 私有代码/数据及跨核保留区 |
| SoCMEM | 5 MiB | 双核共享、GPU/显示、音频 ring 和堆，由链接脚本切分 |
| QSPI NOR | 128 Mbit（16 MiB） | M33/M55 XIP、签名槽和末尾 2 MiB `/flash` 用户盘 |
| HyperRAM | 128 Mbit（16 MiB） | M33/M55 私有区和 12 MiB 共享区 |
| microSD | 板载 SDHC1，4-bit | FAT 文件系统，挂载 `/sdcard` |

### 3.4 外设与接口

| 资源 | 板级实现 |
| --- | --- |
| LCD | 480×800 RGB565，MIPI-DSI 2 data lanes |
| 触摸 | 面板 I2C + INT + RESET，ST7102/ST7123 路径 |
| 背光 | AP3019A 类升压 + PWM18，当前 5 kHz |
| Codec | ES8388，模拟输入/输出 |
| 功放/扬声器 | MD8002 类 2 W AB 类功放 + 板载/外接扬声器 |
| 数字麦克风 | 2 路 PDM MEMS |
| Wi-Fi/Bluetooth | CYW5551x 组合模组；Wi-Fi SDIO，蓝牙 UART + Wake/Reset |
| USB | USB 2.0 HS/FS PHY；产品使用 UAC + MSC |
| SD | SDHC1，4-bit，卡检测 |
| CAN | TJA1042 类收发器，产品驱动尚未启用 |
| 传感器 | AHT20 温湿度、LSM6DS3TR-C 六轴 IMU |
| 扩展 | Raspberry Pi 40-pin、PMOD、3.3 V 电平转换 |
| 调试 | 板载 KitProg3、SWD/JTAG、trace、调试 UART |

> 无线模组在不同 BSP/原理图资料中可能出现 `CYW55512`、`CYW55513` 等命名差异。固件选择以实板丝印、芯片 ID 和已验证的 CYW55500A1 固件组合为准。

### 3.5 硬件数据通路

```text
                        +---------------------------------------------+
                        |          PSoC Edge E84 (BGA-220)            |
                        |  Cortex-M55 400 MHz | Cortex-M33 200 MHz  |
                        |  Ethos-U55 | GCNano Ultra V | DC8000Nano   |
                        +----+-------------+-------------+------------+
                             |             |             |
                        MIPI-DSI       I2S/TDM       SDIO / UART
                             |             |             |
                       480x800 LCD    ES8388 Codec   CYW5551x Wi-Fi/BT
                                       + MD8002 amp
                             |             |             |
                        Touch I2C      Speaker       Wi-Fi / Classic BT
                                                       LE Audio / Auracast

                        QSPI NOR 16 MiB  +  HyperRAM 16 MiB  +  microSD
```

- **显示**：LVGL 渲染到 RGB565 framebuffer → DC8000Nano 读取 → MIPI-DSI → 480×800 面板。
- **音频输出**：本地文件/UAC/A2DP Sink/LE Audio/AirPlay2 → `sound0` → I2S/TDM → ES8388 → MD8002 → 扬声器。
- **音频上行**：PDM 麦克风 → M55 录音/UAC 麦克风 → USB 主机或 WAV 文件。
- **蓝牙音频**：M33 BTstack ↔ HCI UART ↔ CYW5551x；A2DP/LE Audio 帧通过共享 SRAM ring 与 M55 交换。
- **Wi-Fi**：M55 WHD + lwIP ↔ SDIO ↔ CYW5551x。

### 3.6 自研扩展硬件（设计阶段）

仓库中的 `docs/board/PSOC-Edge-E84/` 还包含 Core 扩展架构、接口分配、USB+PMU 子板和自研底板设计约束。这些内容是后续硬件迭代的设计输入，**不代表对应自研板已经打样、测试或量产**。

---

## 4. 软件框架

### 4.1 软件分层

```text
+-----------------------------------------------------------------------+
| 应用层   FeatherTalk Shell / Media / Settings / Files / Gallery /     |
|          Recorder / Bluetooth UI / System pages                       |
+-----------------------------------------------------------------------+
| 服务层   音频仲裁 / USB UAC+MSC / 存储 / Wi-Fi / BT 服务 / KWS /      |
|          AirPlay2 / 偏好设置与持久化                                  |
+-----------------------------------------------------------------------+
| 中间件   LVGL 9.2 + VG-Lite / RT-Thread 5.0.2 / BTstack / lwIP + WHD /|
|          minimp3 / FAAD2 / ALAC / mbedTLS 等                          |
+-----------------------------------------------------------------------+
| 驱动层   LCD/DSI / I2S/TDM / PDM / SDIO / SDHC / USB / IPC / GPIO /   |
|          PWM / I2C / ADC                                              |
+-----------------------------------------------------------------------+
| 硬件     PSoC Edge E84 + CYW5551x + ES8388 + MD8002 + LCD + SD + ...  |
+-----------------------------------------------------------------------+
```

### 4.2 双核职责

| 模块 | M33 控制核 | M55 应用核 |
| --- | --- | --- |
| 系统 | 板级初始化、启动 M55、版本与状态监督 | 产品应用、界面和主要业务服务 |
| 无线 | BTstack、经典蓝牙、BLE、LE Audio、Auracast、HCI UART | WHD、lwIP、Wi-Fi 扫描/连接、AirPlay2 |
| 音频 | A2DP/ISO 数据收发、跨核音频生产或消费 | SBC/LC3/AAC/ALAC 处理、播放、录音、`sound0` 输出 |
| 用户功能 | 蓝牙状态与控制命令 | LVGL、USB UAC/MSC、文件系统、图库、设置和持久化 |
| 共享资源 | Radio Manager 的蓝牙所有者 | Radio Manager 的 Wi-Fi 所有者 |

### 4.3 启动链与镜像关系

```text
Secure M33
  安全启动、签名镜像与启动策略
        |
        v
M33 Non-Secure / RT-Thread
  板级初始化 -> 启动 M55 -> 蓝牙 Host / Radio Manager / IPC
        |
        +---------------- IPC Pipe + 共享 SRAM ----------------+
        |                                                      |
        v                                                      v
M55 / RT-Thread                                      CYW5551x 无线模组
  LVGL / Wi-Fi / USB / 存储 / 音频 / AirPlay2         Wi-Fi: M55 WHD + SDIO
                                                     蓝牙: M33 BTstack + HCI UART
```

需要更新双核固件时，**先烧写 M55，再烧写包含 Secure M33 的签名 M33 合并镜像**。最后一次 M33 复位会完成启动链并释放 M55。

### 4.4 IPC 与共享内存

- **IPC ABI**：固定宽度消息、HELLO/HEARTBEAT、系统状态、快速控制、音频事件和蓝牙 Source 快照；不兼容变更必须提升 ABI 版本。
- **跨核音频 ring**：`FT_ALINK`（M33 → M55）和 `FT_ALINK2`（M55 → M33）位于共享 SRAM，生产者/消费者各自维护 `wr`/`rd`，配合 cache clean/invalidate 和 doorbell。
- **蓝牙 Source 快照**：M33 用 seqlock 发布扫描结果、连接状态、peer 地址和名称，M55 UI 只读。
- **Radio Manager**：Wi-Fi 与蓝牙分别持有 claim；启动、关闭、复位和错误计数互不覆盖。

### 4.5 音频架构

**输出仲裁**：本地扬声器播放、UAC、A2DP Sink、LE Audio 和 AirPlay2 通过 owner 机制竞争 `sound0`，避免多个音源无约束同时输出。

```text
本地文件 ──► player ──┬─► sound0 ──► I2S/TDM ──► ES8388 ──► 扬声器
                     └─► player_bt_tap ──► SBC 编码 ──► ring2 ──► M33 ──► A2DP 耳机

USB 主机 ──► UAC ring ─┬─► sound0 ──► 扬声器
                       └─► UAC tap ──► SBC 编码 ──► ring2 ──► M33 ──► A2DP 耳机

手机/平板 ──► M33 A2DP Sink ──► ring1 ──► SBC 解码 ──► sound0 ──► 扬声器
手机/平板 ──► M33 LE Audio ──► ring1 ──► LC3 解码 ──► sound0 ──► 扬声器
AirPlay2 ──► 网络/RTP ──► AAC/ALAC 解码 ──► sound0 ──► 扬声器
```

**A2DP Source 音源选择**：

- 编码线程从 `UAC tap` 或 `player_bt_tap` 二选一，不再默认绑定 USB。
- 媒体页“蓝牙耳机”与蓝牙设置页“音源: 本地”互相同步；选择“板载扬声器”则回到 USB 音源。
- USB 转发只静音 UAC 的本地 `sound0` 输出，编码线程本身不占用 `sound0`。
- 编码器固定 48 kHz / Joint Stereo / 16 blocks / 8 subbands / Loudness，bitpool 跟随耳机协商值。

### 4.6 显示与图形管线

当前产品界面是 M55 上的 LVGL 9.2 FeatherTalk Shell：

- LVGL draw task 整帧收集后编码为 VG-Lite 命令链，正常路径接近每帧一次 GPU 提交。
- 双 RGB565 framebuffer 同时用于 LVGL 渲染和 DC scanout，避免额外的整帧复制。
- VG-Lite、刷新、字体等热代码放入 M55 ITCM，冷页面和业务代码留在外部 XIP。
- 提供 GPU、刷新、scanout、堆和页面状态诊断；`feather_ui_bench` 可运行 60 帧全屏基准。

`libraries/FeatherUI` 和 `applications/gpu_ui` 保留作 GPU 原生实验与对照，不是当前默认产品 Shell。

### 4.7 无线与协议栈

- **经典蓝牙 / BLE / LE Audio / Auracast**：M33 上的 BTstack 产品适配，HCI 通过 UART 与 CYW5551x 通信。
- **Wi-Fi**：M55 上的 WHD + lwIP，经 SDIO 与同一模组通信。
- **共存**：Wi-Fi 与蓝牙独立 claim、独立复位和错误计数；A2DP、LE Audio 单播和 Auracast 之间按音频路径互斥。
- **配对持久化**：经典蓝牙 link key 和 LE bond 保存到用户 Flash，重启后可恢复。

### 4.8 存储与文件

- `/flash`：QSPI NOR 末尾 2 MiB 用户盘，掉电保存。
- `/sdcard`：microSD，FAT 文件系统，支持热插拔检测。
- 文件操作：目录浏览、复制/粘贴、重命名、删除、格式化。
- USB MSC 可将上述用户盘暴露给 PC；M55 擦写时通过跨核守卫保护 M33 的 XIP 执行。

### 4.9 本地语音唤醒与 AI

- PDM 麦克风采集 → 滑窗特征 → “小麦小麦”模型推理 → M33 事件/LED。
- 提供采集、训练、验证工具和两级 verifier，模型与阈值可按版本替换。
- Ethos-U55 基准使用裸寄存器命令流，测量 HyperRAM/Flash 权重读取和 NPU 数据通路带宽。

### 4.10 构建与工具链

- `tools/freather/build-demo.cmd`：双核构建、签名和合并。
- `tools/freather/flash-feathertalk.cmd`：M55 → M33 顺序烧录、校验和复位。
- `tools/freather/serial-monitor.cmd`：串口监控和脚本化命令。
- `tools/freather/tests/`：AirPlay2、LC3、UI 导航、无线生命周期、Wi-Fi TCP 和音频测试。
- `tools/freather/benchmark-ui-scenes.py`：21 个页面/32 个场景的 UI 与 GPU 基准。

---

## 5. 工程目录

| 目录 | 作用 |
| --- | --- |
| `projects/Edgi_Talk_*` | 官方独立示例：驱动、IPC、USB、LVGL、Wi-Fi、DEEPCRAFT 等 |
| `projects/FeatherTalk_M33` | FeatherTalk 控制核工程，负责蓝牙、板级控制、M55 启动和 IPC |
| `projects/FeatherTalk_M55` | FeatherTalk 应用核工程，负责 UI、Wi-Fi、USB、存储、音频和 AirPlay2 |
| `projects/FeatherTalk_Common` | 双核共享的 IPC ABI、版本信息、音频 ring 和无线资源管理器 |
| `bluetooth/port_rtthread` | M33 上的 BTstack 产品适配、经典蓝牙和 LE Audio 服务 |
| `bluetooth/reference/btstack` | BTstack 子模块 |
| `libraries/HAL_Drivers` | 板级外设驱动，包括 I2S、IPC、LCD、PDM、SPI 等 |
| `libraries/FeatherUI` | GPU 原生 UI 实验与公共 UI 组件；当前产品界面使用 M55 的 LVGL Shell |
| `tools/freather` | 产品构建、签名、烧录、串口、测试和诊断工具 |
| `docs` | 板级设计、无线、音频、AirPlay2 和性能验证记录 |
| `rt-thread` | RT-Thread 内核及组件 |

官方示例仍可按原 SDK 方式使用。FeatherTalk 的产品配置、构建脚本和应用代码主要集中在上述 `FeatherTalk*`、`bluetooth/port_rtthread` 和 `tools/freather` 目录，不需要用综合工程替换官方示例。

---

## 6. 开发状态与进度

状态基线：**2026-11-01，`product/edgi-talk`**。

当前阶段是 **“综合 Demo 已整合一轮，进入真实外部设备兼容性与长时间稳定性验收”**，不是所有功能已经完成产品认证。

### 6.1 已完成的主要阶段

1. 建立双核运行、IPC、构建烧录和产品工具链。
2. 建立 LVGL 产品 Shell、存储、图库、播放、录音和 GPU 优化路径。
3. 打通经典蓝牙配对、A2DP Sink、A2DP Source 与配对持久化。
4. 建立 LE Audio 单播、LC3 管线、VCS 和 Auracast 广播源。
5. 完成 Wi-Fi/蓝牙独立资源管理、双模音频通道控制和 Wi-Fi 性能优化。
6. 移入 AirPlay2 协议、加密、解码和输出适配，形成可测试的研究原型。
7. 建立本地 KWS 全链路、训练与板测工具，并完成 Ethos-U55 权重带宽基准。
8. 将上述模块纳入同一套 M33/M55 综合固件，完成双核构建、签名、烧录和基础上板检查。

### 6.2 最近一轮（2026-11-01）

- 媒体播放器新增板载扬声器/蓝牙耳机输出切换，并打通本地文件到 A2DP Source 的独立 `player_bt_tap` 通路。
- 蓝牙设置页新增扫描结果列表、已连接设备信息和“音源: USB/本地”切换。
- A2DP Source 能力集与 M55 编码器对齐，SBC bitpool 改为跟随耳机协商值，针对 max bitpool 不一致可能导致耳机停止消费的问题。
- 补充经典蓝牙远端名称查询，修正 MAC 地址显示字节序。
- M33/M55 构建通过，OpenOCD 烧录校验通过；媒体页、蓝牙设置页和播放/音源切换的串口等价路径已回归。
- 真实耳机 A2DP 出声与长时播放仍需复测；当前环境缺少稳定的可发现经典耳机。

### 6.3 阶段性上板记录（AirPlay2 综合镜像，KWS 合入前）

- M55/M33 均完成 OpenOCD 写入和校验。
- 双核 IPC 收发正常，应用错误计数为零。
- UI 已初始化，GPU 路径运行，未观察到 overflow 或 scanout timeout。
- 蓝牙可用，Wi-Fi 就绪且实际扫描成功。
- Auracast 两轮控制启停完成，跨核音频 ring 就绪且错误计数为零。
- AirPlay2 服务默认启用；未连接 AP 时保持等待网络状态，不抢占音频设备。

这些结果属于基础集成检查，不代替下列外部设备验收。

### 6.4 下一阶段优先级

1. 使用真实手机/耳机回归 A2DP、LE Audio、音量控制、断线重连和长时间播放。
2. 使用真实 iOS/macOS 发送端完成 AirPlay2 配对、连续音乐、PTP 同步和断线恢复。
3. 回归 USB UAC/MSC、本地播放、录音、图库和存储持久化。
4. 进行 Wi-Fi 已连接条件下的蓝牙音频并发、吞吐、丢包和长时稳定性测试。
5. 在功能冻结后形成明确的发布配置和完整验收清单；实验性的 FPGA/Smart I/O 音频桥不阻塞当前综合 Demo 基线。

---

## 7. 构建、烧录与运行

### 7.1 环境准备

推荐在 Windows PowerShell 中使用仓库内 `tools/freather` 工具链：

- Arm GNU Toolchain 13.3.Rel1
- SCons 4.10.1 与随附 Python
- Infineon Customized OpenOCD
- KitProg3 调试器和 115200 8N1 串口

首次获取仓库后执行：

```powershell
git lfs install
git lfs pull --include="tools/freather/**"
git submodule update --init --recursive
```

官方独立示例仍支持 RT-Thread Studio 2.2.9 或更高版本；FeatherTalk 推荐使用下面的可重复命令行流程。

### 7.2 构建综合 Demo

在仓库根目录执行：

```powershell
.\tools\freather\build-demo.cmd FeatherTalk_M55 -Clean
.\tools\freather\build-demo.cmd FeatherTalk_M33 -Clean
```

最终烧录产物：

```text
projects/FeatherTalk_M55/rtthread.hex
projects/FeatherTalk_M33/build/rtthread.hex
```

M33 根目录下其他同名中间 HEX 不能替代 `build/rtthread.hex` 中的签名合并镜像。

### 7.3 烧录综合 Demo

```powershell
.\tools\freather\flash-feathertalk.cmd
```

脚本自动按 **M55 → M33** 顺序擦写、校验并复位。只更新固件分区，不应把该操作理解为格式化末尾用户数据卷。

### 7.4 串口检查

```powershell
.\tools\freather\serial-monitor.cmd --list
.\tools\freather\serial-monitor.cmd --port COM17 --baud 115200
```

M55 产品终端常用命令：

```text
feather_m55_status
bt_status
ft_wifi
ft_wifi scan
ft_airplay
feather_ui_status
ft_audio_stats
kws info
kws testevt
bt_bcast_start
bt_bcast_stop
```

音频/蓝牙专项调试：

```text
feather_bt_source status
feather_bt_source input usb|player
feather_bt_source scan
feather_player status
feather_player scan
feather_player output speaker|bt
feather_player play <index>
ft_sbc_enc_stats
```

M33 UART5 诊断终端可使用 `feather_status`、`feather_ping` 和底层蓝牙诊断命令。Wi-Fi 不保存密码、不会因 AirPlay2 自动加入 AP；AirPlay2 配对前还需按其移植文档完成随机种子配置。

### 7.5 快速体验路径

1. 开机进入桌面，左右滑动查看应用列表、通知栏和设置。
2. 打开“音乐”，选择 SD 卡文件夹，播放 WAV/MP3，观察 3D 封面流、专辑图和进度。
3. 进入“设置 > 蓝牙”，选择 A2DP SOURCE，扫描并连接耳机；在媒体页选择“蓝牙耳机”，或把蓝牙页音源切到“本地”。
4. 连接 USB 主机，验证 UAC 扬声器/麦克风、USB MSC 和 USB 到蓝牙转发。
5. 在“设置 > Wi-Fi”扫描/连接 AP，用 `ft_wifi`、`ft_airplay` 查看网络和 AirPlay2 状态。
6. 说“小麦小麦”验证本地唤醒；运行 `feather_ui_bench` 查看 GPU/刷新基准。

---

## 8. 测试与验证

### 8.1 主机侧测试

以下测试使用仓库随附 Python，覆盖实际生产 C/C++ 源码或其适配层：

```powershell
.\tools\freather\serial-monitor\python\python.exe tools/freather/tests/test_airplay_protocol.py
.\tools\freather\serial-monitor\python\python.exe tools/freather/tests/test_airplay_crypto.py
.\tools\freather\serial-monitor\python\python.exe tools/freather/tests/test_airplay_alac.py
.\tools\freather\serial-monitor\python\python.exe tools/freather/tests/test_airplay_aac.py
.\tools\freather\serial-monitor\python\python.exe tools/freather/tests/test_lc3_watermark.py
.\tools\freather\serial-monitor\python\python.exe tools/freather/check-ui-navigation-map.py
```

LE Audio 线协议测试的编译命令见 `docs/LE_AUDIO_REPAIR_20260913.md`。无线生命周期、Wi-Fi TCP 和 AirPlay2 实机脚本位于 `tools/freather` 与 `tools/freather/tests`；运行前应阅读各自参数和硬件前提。

### 8.2 板级检查

| 类型 | 内容 | 入口 |
| --- | --- | --- |
| 构建/烧录 | M33/M55 双核构建、签名合并、OpenOCD 写入与校验 | `build-demo.cmd`、`flash-feathertalk.cmd` |
| 启动/IPC | 双核启动、IPC 心跳、错误计数、共享内存 | 串口 `feather_m55_status`、`bt_status` |
| UI/GPU | 21 页面、32 场景、60 帧基准、scanout/堆统计 | `feather_ui_scene`、`feather_ui_bench`、`benchmark-ui-scenes.py` |
| 音频 | 本地播放、暂停/继续/停止、输出切换、SBC 统计 | `feather_player`、`ft_sbc_enc_stats` |
| 蓝牙 | 扫描、配对、A2DP Sink/Source、LE Audio、Auracast | `feather_bt_source`、`bt_bcast_start/stop` |
| 无线 | Wi-Fi 扫描/连接、TCP、蓝牙生命周期 | `ft_wifi`、`test-wifi-tcp.py`、`test-radio-lifecycle.py` |
| 存储 | SD/Flash 挂载、文件操作、格式化、USB MSC | UI 文件页/存储页 |
| KWS/NPU | 唤醒词全链路、事件联动、NPU 带宽 | `kws`、`npu_bench` |

主机单元测试、网络模拟包测试和基础启动检查都不能替代真实发送端、耳机、USB 主机和长时间播放验收。

---

## 9. 开源软件与第三方组件

FeatherTalk 建立在多个开源项目和厂商 SDK 之上。下表用于提交材料中的组件盘点，具体许可条款以各上游仓库为准。

| 组件 | 用途 | 许可/说明 |
| --- | --- | --- |
| RT-Thread | RTOS、驱动框架、FinSH | Apache-2.0 |
| LVGL | 产品 UI、控件、动画 | MIT |
| BTstack | 经典蓝牙、BLE、LE Audio、A2DP/AVDTP/AVRCP | BlueKitchen BTstack 许可；商用前单独审查 |
| lwIP | TCP/IP 协议栈 | BSD-3-Clause |
| WHD | CYW5551x Wi-Fi 驱动 | Infineon 许可 |
| minimp3 | MP3 解码 | CC0 |
| FAAD2 | AAC 解码 | GPL-2.0-or-later |
| ALAC | Apple Lossless 解码 | Apache-2.0（以上游为准） |
| AirPlay2 参考实现 | 协议、加密、RTP、ALAC/AAC | 参考 ESP 代码包含 Non-Commercial License |
| Infineon PDL/HAL | PSoC Edge 外设驱动 | Infineon 许可 |

AirPlay2 目录保留上游许可证；FAAD2 的 GPL 条款和 AirPlay2 参考代码的非商业条款必须在发布或商用前单独审查，不能把“代码可构建”理解为已取得商业授权。

---

## 10. 演示视频（待录制）

建议按以下脚本录制 3–5 分钟演示，覆盖提交材料中的主要能力：

1. **0:00–0:30 开机与桌面**：双核启动、桌面、应用列表、通知栏、设置。
2. **0:30–1:30 音乐播放器**：SD 卡曲目、3D 封面流、专辑图、播放/暂停/切歌、扬声器输出。
3. **1:30–2:30 蓝牙耳机**：SOURCE 角色、扫描列表、设备名/MAC、连接、音源 USB/本地切换、本地文件到耳机播放。
4. **2:30–3:00 USB**：UAC 扬声器/麦克风、USB MSC、USB 到蓝牙转发。
5. **3:00–3:45 无线**：Wi-Fi 扫描/连接、TCP 状态、AirPlay2 研究原型。
6. **3:45–4:15 LE Audio/Auracast**：单播连接、广播启停。
7. **4:15–5:00 KWS/NPU**：唤醒词、事件联动、NPU 带宽基准、UI/GPU 基准数据。

录制完成后，将视频链接或文件放在本节，并在提交材料中标注分辨率、时长和演示环境。

---

## 11. 文档索引

- [2026 嵌入式大赛发布稿](docs/FeatherTalk_发布稿_2026嵌入式大赛.md)
- [双核构建与上板](projects/FeatherTalk_Common/BRINGUP_zh.md)
- [Wi-Fi / 蓝牙运行时资源管理](projects/FeatherTalk_Common/RADIO_COEXISTENCE_zh.md)
- [M55 显示与 GPU 架构](projects/FeatherTalk_M55/README_zh.md)
- [LE Audio 修复与验证记录](docs/LE_AUDIO_REPAIR_20260913.md)
- [Wi-Fi 性能对齐记录](docs/WIFI_PERFORMANCE_ALIGNMENT_20260913.md)
- [AirPlay2 移植记录](docs/AIRPLAY2_PORTING_zh.md)
- [KWS 上板验证指南](projects/FeatherTalk_M55/KWS_VERIFY_zh.md)
- [Ethos-U55 NPU 带宽基准](projects/FeatherTalk_M55/applications/npu_bench/README.md)
- [Edgi-Talk Core 系统需求与扩展架构](docs/board/PSOC-Edge-E84/Edgi-Talk_Core_系统需求与架构.md)
- [Edgi-Talk 原理图分析](docs/board/PSOC-Edge-E84/Edgi-Talk_原理图分析.md)
- [PSE84 SoC/GPU/显示与内存布局](docs/board/PSOC-Edge-E84/PSE84_SOC_GPU2D_zh.md)
- [FPGA SPI-I2S 音频桥设计](docs/FPGA_SPI_I2S_AUDIO_BRIDGE.md)
- [PSE84 构建、烧录与串口工作流](tools/freather/PSE84_WORKFLOW.md)

部分文档按时间保留问题定位和阶段性结果；阅读时应以最新段落、当前代码和本 README 的状态表为准，不能把历史待办直接当作当前状态。

---

## 12. 许可与使用边界

- 官方 SDK、RT-Thread、BTstack 和各第三方组件分别遵循其原有许可证。
- AirPlay2 目录保留上游许可证：参考 ESP 代码包含 **Non-Commercial License**，FAAD2 使用 **GPL-2.0-or-later**；商用或发布前必须单独审查，不能把代码可构建理解为已取得商业授权。
- AirPlay2 实现是研究原型，不表示获得 Apple 认证，也不冒充 persistent HomeKit 配对、经典 RSA/FairPlay 或未实现的编解码能力。
- 硬件架构和子板文档是设计输入，不代表对应自研硬件已经完成打样、测试或量产。
- 本地播放器到 A2DP Source 的真实耳机出声、长时稳定性，以及 LE Audio/Auracast/AirPlay2 的完整兼容性，仍需按第 6.4 节继续验收。
