# Edgi-Talk PSoC Edge E84 SDK 与 FeatherTalk

**中文** | [**English**](./README.md)

本仓库包含两层内容：

1. **Edgi-Talk 官方 BSP/SDK**：PSoC Edge E84 的 RT-Thread 板级支持、驱动和独立外设示例。
2. **FeatherTalk 产品工程**：在同一 SDK 上开发的双核综合应用，集成 LVGL、音频、USB、Wi-Fi、经典蓝牙、LE Audio、Auracast、本地唤醒词、NPU 基准和 AirPlay2 原型。

当前产品分支为 `product/edgi-talk`。FeatherTalk 的综合 Demo 不是一个新的单核示例，而是 **`FeatherTalk_M33` 与 `FeatherTalk_M55` 两个配套工程及其共享模块**。

![Edgi-Talk](docs/figures/board_large.jpg)

## 1. 工程架构

### 1.1 SDK 与产品代码边界

| 目录 | 作用 |
| --- | --- |
| `projects/Edgi_Talk_*` | 官方独立示例：驱动、IPC、USB、LVGL、Wi-Fi、DEEPCRAFT 等 |
| `projects/FeatherTalk_M33` | FeatherTalk 控制核工程，负责蓝牙、板级控制、M55 启动和 IPC |
| `projects/FeatherTalk_M55` | FeatherTalk 应用核工程，负责 UI、Wi-Fi、USB、存储、音频和 AirPlay2 |
| `projects/FeatherTalk_Common` | 双核共享的 IPC ABI、版本信息和无线资源管理器 |
| `bluetooth/port_rtthread` | M33 上的 BTstack 产品适配、经典蓝牙和 LE Audio 服务 |
| `bluetooth/reference/btstack` | BTstack 子模块 |
| `libraries/HAL_Drivers` | 板级外设驱动，包括 I2S、IPC、LCD、PDM、SPI 等 |
| `libraries/FeatherUI` | GPU 原生 UI 实验与公共 UI 组件；当前产品界面使用 M55 的 LVGL Shell |
| `tools/freather` | 产品构建、签名、烧录、串口、测试和诊断工具 |
| `docs` | 板级设计、无线、音频、AirPlay2 和性能验证记录 |
| `rt-thread` | RT-Thread 内核及组件 |

官方示例仍可按原 SDK 方式使用。FeatherTalk 的产品配置、构建脚本和应用代码主要集中在上述 `FeatherTalk*`、`bluetooth/port_rtthread` 和 `tools/freather` 目录，不需要用综合工程替换官方示例。

### 1.2 启动链与镜像关系

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

## 2. FeatherTalk 运行架构

### 2.1 双核职责

| 模块 | M33 控制核 | M55 应用核 |
| --- | --- | --- |
| 系统 | 板级初始化、启动 M55、版本与状态监督 | 产品应用、界面和主要业务服务 |
| 无线 | BTstack、经典蓝牙、BLE、LE Audio、Auracast、HCI UART | WHD、lwIP、Wi-Fi 扫描/连接、AirPlay2 |
| 音频 | A2DP/ISO 数据收发、跨核音频生产或消费 | SBC/LC3/AAC/ALAC 处理、播放、录音、`sound0` 输出 |
| 用户功能 | 蓝牙状态与控制命令 | LVGL、USB UAC/MSC、文件系统、图库、设置和持久化 |
| 共享资源 | Radio Manager 的蓝牙所有者 | Radio Manager 的 Wi-Fi 所有者 |

### 2.2 跨核通信与音频

- **IPC ABI**：固定宽度消息、HELLO/HEARTBEAT、系统状态、快速控制和音频事件；不兼容变更必须提升 ABI 版本。
- **跨核音频链路**：共享 SRAM 环形缓冲区配合 doorbell，传输蓝牙音频帧、格式和状态。
- **音频输出仲裁**：本地播放、UAC、A2DP、LE Audio 和 AirPlay2 通过 owner 机制竞争 `sound0`，不把所有音频源设计成无约束同时输出。
- **无线资源管理**：Wi-Fi 与蓝牙分别拥有独立的 claim、状态、复位和错误计数；普通功能开关不切断另一功能或整组公共电源。
- **共享 Flash 保护**：M55 擦写用户 Flash 时通过跨核守卫避免 M33 继续从同一 SMIF 设备执行旧缓存代码。

### 2.3 显示与图形管线

当前产品界面是 M55 上的 LVGL 9.2 FeatherTalk Shell：

- 自适应中英文界面、应用导航、设置、文件、图库、播放、录音和蓝牙页面。
- LVGL draw task 整帧收集后编码为 VG-Lite 命令链，正常路径接近每帧一次 GPU 提交。
- 双 RGB565 framebuffer 同时用于 LVGL 渲染和 DC scanout，避免额外的整帧复制。
- 提供 GPU、刷新、scanout、堆和页面状态诊断。

`libraries/FeatherUI` 和 `applications/gpu_ui` 保留作 GPU 原生实验与对照，不是当前默认产品 Shell。

## 3. 功能与开发内容

| 领域 | 已开发内容 | 当前状态与边界 |
| --- | --- | --- |
| 双核基础 | 双核启动、版本化 IPC、心跳、跨核音频 ring、共享 Flash 和无线资源管理 | 已整合；当前镜像启动和 IPC 基础检查通过 |
| 产品 UI | 中英文 Shell、滑动导航、本地播放、录音、文件、图库、存储、设置和蓝牙页面 | 已整合；UI 初始化、GPU 和导航检查通过，完整交互仍需随版本回归 |
| GPU 与显示 | VG-Lite 批处理、双 framebuffer direct scanout、背光控制与性能基准 | 已有专项板测；当前基础状态无溢出和 scanout 超时 |
| 本地音频与 USB | 本地播放、录音、双向 UAC、动态速率/时钟校准、USB MSC、USB 到蓝牙转发 | 功能已开发并有专项测试记录；当前版本仍需外部 USB 主机和试听回归 |
| 经典蓝牙 | BTstack、可发现、SSP 配对、link key 持久化、A2DP Sink/Source、角色切换、重连和 AVRCP 修复 | 已有端到端出声及转发记录；不同手机和长时间播放仍需持续兼容性验证 |
| LE Audio | BAP Unicast Server、双 CIS、LC3、PACS/ASCS/CAS/TMAP/VCS、LE bond 持久化 | 已有真实手机连接与出声记录；长时连续性、更多手机和完整合规性未宣称完成 |
| Auracast | BIG/BIS 广播源、周期广播、启停与单播互斥 | BIG/BIS 数据面已打通；当前综合镜像完成两轮控制启停，接收端试听仍另需验收 |
| Wi-Fi 与共存 | 双频扫描/连接、独立无线生命周期、性能优化、TCP/内存诊断 | 有双频短测和性能记录；当前镜像实扫通过，长时吞吐及音频并发仍待回归 |
| AirPlay2 | mDNS、RTSP/HTTP、HAP transient 配对、会话/RTP 加密、ALAC/AAC-LC、type96/type103、`sound0` 输出 | 研究原型；协议和模拟网络音频测试通过，真实 iPhone 连续播放、PTP 同步和长时间验收未完成 |
| 本地唤醒词 KWS | “小麦小麦”自训模型、PDM 麦克风、滑窗推理、开机自启、M33 事件联动、绿灯指示、采集/训练/验证工具和两级 verifier | 已有上板全链路验证；模型仍在迭代，最新模型的真人召回率与误触率须按版本单独验收 |
| Ethos-U55 NPU | 裸寄存器命令流、HyperRAM/Flash 权重读带宽基准和模型生成工具 | 研究与性能测量工具，不代表本地大模型推理功能已经完成 |
| 构建与诊断 | 双核构建、签名合并、OpenOCD 烧录校验、串口、PyOCD 辅助路径、UI/无线/音频测试 | 已整合并用于当前版本构建和上板验证 |
| 自研硬件 | Core 扩展架构、接口分配、USB+PMU 子板和设计约束 | 需求与设计文档阶段，不等同于硬件已投产或验证 |
| SPI 转 I2S | FPGA 音频桥设计资料；SoC SPI + Smart I/O 官方路线分析 | 未纳入当前综合固件；Smart I/O 驱动尚未开始开发 |

## 4. 开发状态和进度

状态基线：**2026-10-31，`product/edgi-talk`**。

当前阶段是 **“综合 Demo 已整合一轮，进入真实外部设备兼容性与长时间稳定性验收”**，不是所有功能已经完成产品认证。

### 4.1 已完成的主要阶段

1. 建立双核运行、IPC、构建烧录和产品工具链。
2. 建立 LVGL 产品 Shell、存储、图库、播放、录音和 GPU 优化路径。
3. 打通经典蓝牙配对、A2DP Sink、A2DP Source 与配对持久化。
4. 建立 LE Audio 单播、LC3 管线、VCS 和 Auracast 广播源。
5. 完成 Wi-Fi/蓝牙独立资源管理、双模音频通道控制和 Wi-Fi 性能优化。
6. 移入 AirPlay2 协议、加密、解码和输出适配，形成可测试的研究原型。
7. 建立本地 KWS 全链路、训练与板测工具，并完成 Ethos-U55 权重带宽基准。
8. 将上述模块纳入同一套 M33/M55 综合固件，完成双核构建、签名、烧录和基础上板检查。

### 4.2 本轮 AirPlay2 综合镜像的上板记录（KWS 合入前）

- M55/M33 均完成 OpenOCD 写入和校验。
- 双核 IPC 收发正常，应用错误计数为零。
- UI 已初始化，GPU 路径运行，未观察到 overflow 或 scanout timeout。
- 蓝牙可用，Wi-Fi 就绪且实际扫描成功。
- Auracast 两轮控制启停完成，跨核音频 ring 就绪且错误计数为零。
- AirPlay2 服务默认启用；未连接 AP 时保持等待网络状态，不抢占音频设备。

这些结果属于基础集成检查，不代替下列外部设备验收。

### 4.3 下一阶段优先级

1. 使用真实手机回归 A2DP、LE Audio、音量控制、断线重连和长时间播放。
2. 使用真实 iOS/macOS 发送端完成 AirPlay2 配对、连续音乐、PTP 同步和断线恢复。
3. 回归 USB UAC/MSC、本地播放、录音、图库和存储持久化。
4. 进行 Wi-Fi 已连接条件下的蓝牙音频并发、吞吐、丢包和长时稳定性测试。
5. 在功能冻结后形成明确的发布配置和完整验收清单；实验性的 FPGA/Smart I/O 音频桥不阻塞当前综合 Demo 基线。

## 5. 构建、烧录与运行

### 5.1 环境准备

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

### 5.2 构建综合 Demo

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

### 5.3 烧录综合 Demo

```powershell
.\tools\freather\flash-feathertalk.cmd
```

脚本自动按 **M55 → M33** 顺序擦写、校验并复位。只更新固件分区，不应把该操作理解为格式化末尾用户数据卷。

### 5.4 串口检查

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

M33 UART5 诊断终端可使用 `feather_status`、`feather_ping` 和底层蓝牙诊断命令。Wi-Fi 不保存密码、不会因 AirPlay2 自动加入 AP；AirPlay2 配对前还需按其移植文档完成随机种子配置。

## 6. 测试与验证

以下主机测试使用仓库随附 Python，覆盖实际生产 C/C++ 源码或其适配层：

```powershell
.\tools\freather\serial-monitor\python\python.exe tools/freather/tests/test_airplay_protocol.py
.\tools\freather\serial-monitor\python\python.exe tools/freather/tests/test_airplay_crypto.py
.\tools\freather\serial-monitor\python\python.exe tools/freather/tests/test_airplay_alac.py
.\tools\freather\serial-monitor\python\python.exe tools/freather/tests/test_airplay_aac.py
.\tools\freather\serial-monitor\python\python.exe tools/freather/tests/test_lc3_watermark.py
.\tools\freather\serial-monitor\python\python.exe tools/freather/check-ui-navigation-map.py
```

LE Audio 线协议测试的编译命令见 `docs/LE_AUDIO_REPAIR_20260913.md`。无线生命周期、Wi-Fi TCP 和 AirPlay2 实机脚本位于 `tools/freather` 与 `tools/freather/tests`；运行前应阅读各自参数和硬件前提。

主机单元测试、网络模拟包测试和基础启动检查都不能替代真实发送端、耳机、USB 主机和长时间播放验收。

## 7. 文档索引

- [双核构建与上板](projects/FeatherTalk_Common/BRINGUP_zh.md)
- [Wi-Fi / 蓝牙运行时资源管理](projects/FeatherTalk_Common/RADIO_COEXISTENCE_zh.md)
- [M55 显示与 GPU 架构](projects/FeatherTalk_M55/README_zh.md)
- [LE Audio 修复与验证记录](docs/LE_AUDIO_REPAIR_20260913.md)
- [Wi-Fi 性能对齐记录](docs/WIFI_PERFORMANCE_ALIGNMENT_20260913.md)
- [AirPlay2 移植记录](docs/AIRPLAY2_PORTING_zh.md)
- [KWS 上板验证指南](projects/FeatherTalk_M55/KWS_VERIFY_zh.md)
- [Ethos-U55 NPU 带宽基准](projects/FeatherTalk_M55/applications/npu_bench/README.md)
- [Edgi-Talk Core 系统需求与扩展架构](docs/board/PSOC-Edge-E84/Edgi-Talk_Core_系统需求与架构.md)
- [FPGA SPI-I2S 音频桥设计](docs/FPGA_SPI_I2S_AUDIO_BRIDGE.md)
- [PSE84 构建、烧录与串口工作流](tools/freather/PSE84_WORKFLOW.md)

部分文档按时间保留问题定位和阶段性结果；阅读时应以最新段落、当前代码和本 README 的状态表为准，不能把历史待办直接当作当前状态。

## 8. 许可与使用边界

- 官方 SDK、RT-Thread、BTstack 和各第三方组件分别遵循其原有许可证。
- AirPlay2 目录保留上游许可证：参考 ESP 代码包含 **Non-Commercial License**，FAAD2 使用 **GPL-2.0-or-later**；商用或发布前必须单独审查，不能把代码可构建理解为已取得商业授权。
- AirPlay2 实现是研究原型，不表示获得 Apple 认证，也不冒充 persistent HomeKit 配对、经典 RSA/FairPlay 或未实现的编解码能力。
- 硬件架构和子板文档是设计输入，不代表对应自研硬件已经完成打样、测试或量产。
