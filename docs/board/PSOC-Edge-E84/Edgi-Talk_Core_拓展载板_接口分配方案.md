# Edgi-Talk_Core 拓展载板 — 接口分配方案

> 定位：载板本身无功能，只做核心板 ↔ 各子板的互连。
> 依据：E84 数据手册 Table 12 + BSP 引脚复用表（`gpio_pse84_bga_220.h`，机器生成、最可靠）+ BTB 逐针核对（`Edgi-Talk_原理图分析.md`）。
> 配套约束：`Edgi-Talk_Core_自研底板设计约束.md`（电源/电平/工艺）。

## 1. 分配总表

| 子板接口 | 硬件资源 | MCU 引脚 | BTB 网络名 | 验证状态 |
|---|---|---|---|---|
| **HiFi 音频**（I2S **全双工**，HiFi 子板） | TDM1 TX+RX | TX：SCK=P11.0、FSYNC=P11.1、SD=P11.2、MCK=P11.7；RX：MCK=P11.3、SCK=P11.4、FSYNC=P11.5、SD=P11.6 | GPIO_8/9/10/11、BT_REG_ON、WL_DEV_WAKE 等（P11 整组） | ✓ 改底板重新定义；TDM0 仅共享时钟全双工（P21.5 不出核心板），独立时钟全双工只能 TDM1 |
| **本地音频**（I2S 输出，本地音频子板） | TDM0 TX | MCK=P21.3、SCK=P21.2、SD=P21.1、FSYNC=P12.3 | I2S_TX_MCK/SCK/SD/FSYNC | ✓ 原板已在用；可加 P21.4（RX_SD）升级共享时钟全双工（RX_CTL.MODE=2，RX 吃 TX 内部时钟，RX_FSYNC 脚不需要） |
| **WiFi+BT 模组（统一规格）** | SDHC0（SDIO 4-bit）+ BT UART | CLK=P12.0、D0~3=P12.1/12.2/12.4/12.5、CMD=P21.0；BT_UART=P10.0~10.3 | WL_SDIO_*、BT_UART_* | ✓ 原板已在用；蓝牙通话语音走 HCI（E84 无对外 PCM 总线） |
| **屏幕** | MIPI DSI 2-lane + 触摸 I2C + 控制 | DSI 专用脚；触摸 I2C7=P13.1/13.2；DISP_RST=P20.7、TE=P20.5、BL_PWM=P20.6 | DSI_*、I2C7_*、DISP_* | ✓ 原板已在用 |
| **IO6 拓展口** | P16/P17 共 16 脚 | P16.0~16.7 + P17.0~17.7 | CAN0_*、Audio_CTRL、WIFI_CTRL、USER_LED1~3、USB_FAULT/HOST_EN/DEVICE_DET、RPI_UART5_*、DISP_TP_*、BOOT、SD_CARD_DET | ✓ 全部在 BTB 上 |
| **SD/eMMC 互斥拓展** | SDHC1 | CMD=P7.0、CLK=P7.1、RST=P7.2、D0~3=P7.3/7.5/7.6/7.7；eMMC 8-bit 加 D4~7=**P6.4~6.7** | SD_CARD_*、CTRL、UART_* | ✓ 可行，互斥逻辑见 §3 |

录音输入不需要 I2S：核心板自带 2× PDM 麦 + 2× AMIC 差分前端（CTB+ADC）。

## 2. 两路 I2S 的依据与细节

E84 有 **2 个独立 TDM/I2S 硬件块**（各 8 声道、全/半双工），引脚互不重叠：

- **TDM0**（本地音频）：TX 半 = P21.1/21.2/21.3 + P12.3，与原板 ES8388 接法一致。RX 半（P21.4~21.7）中 FSYNC（P21.5）被板上 MEMORY_RST 占用且不出核心板；但共享时钟全双工仍可用（RX_CTL.MODE=2，RX 吃 TX 内部时钟，数据只需 RX_SD=P21.4）——不能的只是异时钟 RX
- **TDM1**（本地音频）：**整组在 P11**：TX_SCK=P11.0、TX_FSYNC=P11.1、TX_SD=P11.2、TX_MCK=P11.7（RX 半 = P11.3~11.6，不用可留作 GPIO）
- 两块可同时工作（独立硬件、独立时钟）
- MCK 由芯片 TDM 输出（ECO 12.288 MHz 基准，48 kHz 系列整数分频，HiFi 友好）
- 音频 codec 的 IOVDD 选支持 1.8 V 的型号（全系 1.8 V 逻辑）；HiFi 子板的模拟电源（AVDD/电荷泵等）载板自产

## 3. SD 4-bit / eMMC 8-bit 互斥共用

SDHC1 引脚布局（两组恰好错开）：

```
P7:  CMD(P7.0) CLK(P7.1) RST(P7.2) D0(P7.3) D1(P7.5) D2(P7.6) D3(P7.7)   ← SD 4-bit / eMMC 共用
P6:  D4(P6.4) D5(P6.5) D6(P6.6) D7(P6.7)                                 ← 仅 eMMC 8-bit 需要
```

- **SD 卡模式**：只用 P7 组；P6.4~6.7 空闲（可留作调试 UART）
- **eMMC 8-bit 模式**：加用 P6.4~6.7——**此时调试 UART 没了**（SWD 在 P6.0~6.3，不受影响）。这就是"互斥"的硬约束，连接器上建议留 0Ω 跳线
- **卡检测**：sdhc1 的 card_detect_n 在 P7.4，**该脚没过 BTB**——用自由 GPIO 代替（如 P17.7=SD_CARD_DET 原网络）
- **电平**：P6/P7 是 1.8 V 域——eMMC 选 1.8 V IO 料直连；SD 卡要经 TXS02612 类专用转换器出 3.3 V（原板方案），或强制 UHS-I 1.8 V 模式

## 4. 冲突与对策（载板设计要点）

1. **WiFi 控制脚让位**：原板把 WL_REG_ON/WL_HOST_WAKE 等放在 P11.x；本方案 P11 整组给 TDM1。WiFi 模组的 REG_ON/WAKE/CLK_REQ 只是普通 GPIO，改从 GPIO_0~4/7 池或 IO6 池分配（软件改引脚定义即可）
2. **P12.3 双重身份**：它是 TDM0 FSYNC，**不是** SDHC0 的一部分——WiFi SDIO 与 HiFi I2S 无冲突（官方两板同款用法）
3. **eMMC 8-bit ↔ 调试 UART**（P6.4~6.7）：二选一，见 §3
4. **IO6 拓展口里的保留脚**：BOOT（P17.6）必须引到启动配置跳线；SD_CARD_DET（P17.7）若用 SD 卡则接到卡槽；其余 14 脚完全自由
5. **IO6 电平**：默认 1.8 V；若核心板做 R6 改造（见约束文档 §2.1），拓展口可跳线选 3.3 V——对接 3.3 V 外设生态更友好

## 5. 载板电源/杂项责任

- 供 VIN 3.0~5.0 V（CN1 pin 1~5）；VDDUSB_3V3 按需（不用 USB 则该针接地）
- 自产 3.3 V（子板供电）与 HiFi 需要的干净模拟电源；1.8 V 小负载可吃核心板回供的 VDD_1V8（计入预算）
- BOOT/XRES/复位键；SWD 排针
- 子板连接器上把 1.8 V 逻辑标清楚，3.3 V 子板接口前放电平转换

## 6. 分配后的剩余资源盘点

### 6.1 完整空闲的接口/子系统（整组可用）

| 资源 | 引脚/位置 | 说明 |
|---|---|---|
| **USB HS** | USB_DP/DM（BTB CN2） | 480 Mbps device/host，完全未用；载板放 Type-C 即得一路 USB |
| **主 I2C** | P8.0/P8.1 | 屏幕触摸用 I2C7 后，这条 1.8 V 主 I2C 空出（原板挂 codec 控制/传感器的总线） |
| **I3C** | P3.0/P3.1 | 整组空闲 |
| **SPI9 全套** | P15.0~15.4（CLK/MOSI/MISO/SS0/SS1） | 不用 SPI 时 = **5 路 SAR ADC 直通道**（P15 是模拟口） |
| **电池/模拟采集** | P15.5（ARD_ADC_3） | 专用 SAR 通道，VREF 也在 BTB 上 |
| **BT UART 组** | P10.4~10.7 | BT_UART（P10.0~10.3）已分配给 WiFi+BT 子板；剩 4 脚空闲 |
| **TRACE×5** | P20.0~20.4 | 不用 ETM trace 就是 5 个普通 GPIO |
| ~~TDM1 RX 半组~~ | P11.3~11.6 | **已分配给 HiFi 子板（全双工 RX）**，不再空闲 |
| **DAC ×2** | P14.7 / P13.6 | 两路 12-bit DAC 输出脚都空着（原板当 GPIO 用） |
| **CTB1 运放组** | P13 部分脚 | P13.0/13.3~13.7 空闲（GPIO_0~4/7），可复用为第二组运放 |
| **GFX 8080 并口** | P16/P17（IO6 域内复用） | IO6 拓展口可整体改作第二块 8-bit 8080 屏（d0~d7+csx/wrx/dcx） |
| PDM 总线 | P8.5/P8.6 | 核心板双麦之外还能再挂（总线过了 BTB） |

### 6.2 空闲 GPIO 清点（按 BTB 网络）

| 网络组 | 引脚 | 数量 |
|---|---|---|
| GPIO_19 | P0.0 | 1 |
| GPIO_16/17、USER_SW1、I2C_INT2_EXT | P8.2/P8.7/P8.3/P8.4 | 4 |
| SERIAL_INT_0~3 | P9.0~9.3 | 4（中断友好） |
| BT 组（不带 BT 时） | P10.0~10.7 | 8 |
| ~~TDM1 RX 半组~~ | ~~P11.3~11.6~~ | 已分配给 HiFi 子板全双工 |
| GPIO_0~4、GPIO_7 | P13.0、P13.3~13.7 | 6 |
| GPIO_14/15 | P14.6/14.7 | 2 |
| SPI9 组 | P15.0~15.4 | 5 |
| TRACE 组 | P20.0~20.4 | 5 |
| GPIO_18、OA_CTRL、I2C_INT1 | P21.4/P21.6/P21.7 | 3 |
| 调试 UART（SD 4-bit 模式时） | P6.4~6.7 | 4 |
| **小计（IO6 之外）** | | **约 30~34 个**（已扣除 BT_UART 与 TDM1 RX） |
| IO6 拓展口内自由脚 | P16.0~16.7、P17.0~17.5、P17.7（BOOT=P17.6 保留） | 15 |

### 6.3 还要从空闲池支出的

- **WiFi 模组控制**：WL_REG_ON、BT_REG_ON、WL_HOST_WAKE、BT_HOST_WAKE、WL_DEV_WAKE、BT_DEV_WAKE ≈ 6 个 GPIO（P11 已让给 TDM1，从 §6.2 的空闲池分配，软件改引脚定义即可）
- 屏幕背光/电源门控按需 1~2 个（可用 P15.6/P15.7 原网络）

### 6.4 实际不可用/需注意的

- **Ethernet MAC**：管脚在 P10/P11，与 TDM1 + BT UART 冲突——本方案下不可用（要用就得重分配 P11，牺牲本地音频 I2S）
- **TDM0 仅共享时钟全双工**：RX_FSYNC（P21.5）是板上 MEMORY_RST 且不过 BTB；共享时钟模式（RX_CTL.MODE=2）下 RX 只需 RX_SD=P21.4（在 BTB 上）——播放+同速率回采可用，异时钟 RX 不可用
- P18/P19（晶振）、P21.5（MEMORY_RST）、SMIF 全区：本就不可用
- eMMC 8-bit 模式期间 P6.4~6.7 被占用（调试 UART 让位）
