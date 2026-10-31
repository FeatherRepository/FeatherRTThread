# Edgi-Talk（RT-Thread / 上海睿赛德）PSoC Edge E84 开发板原理图分析

> 分析对象：
> - `PSoc_Edge_Core_Schematic.pdf` —— **Edgi-Talk_Core 核心板**（Rev V1.0，2025-09-29，共 10 页）
> - `PSoc_Edge_Basic_Schematic.pdf` —— **Edgi-Talk 底板**（Rev V1.0，2025-08，共 16 页）
>
> 形态与官方 KIT_PSE84_EVAL 类似（核心板 + 底板），核心板经 **2× DF40C_80DP**（Hirose 0.4mm 80-pin）板对板连接器插到底板。
> 与官方两块板（SoM / AI 板）的电源拓扑**相反**：本板用**外部稳压模式 + VDDIO6=1.8V**，3.3V 接口全靠分立式电平转换器——是 E84 的另一种合法且完整落地的设计范式。
>
> 配套文档：**`Edgi-Talk_Core_自研底板设计约束.md`**（基于该核心板自研底板的约束清单）。

## 1. 概述

语音对讲场景开发板：E84 核心板（MCU + 双存储 + PDM/AMIC 前端 + 1.8V 电源）+ 底板（BQ25606 充电、ES8388 音频、CYW55512 WiFi/BT、MIPI DSI 显示、SD 卡、CAN、树莓派 40-pin 扩展、KitProg3 调试器）。

```
底板: USB-C×2 / 1S电池 ──► BQ25606 充电 ─► SY6280 限流 ─► LM66100×2 理想二极管 ─► VOUT ─┬─► TPS61023 → VCC_5V
                                                                                       └─► TPS63802 → VDD/VDDIO_3V3
                                                       VOUT ══BTB══► 核心板 TPS63802 → VDD_1V8 ─► MCU/存储/外设（1.8V 一切）
                                                       VDDUSB_3V3 ══BTB══► MCU USB PHY
```

---

# 第一部分：核心板 Edgi-Talk_Core（10 页）

## 2. 主控与时钟

- **Explorer_220BGA**（PSoC Edge E84，BGA-220，图纸符号名 Explorer_220BGA_rev9280_May22）
- 时钟（sheet 10）：
  - **ECO = 12.288 MHz**（Y2，2×10 pF/25V，P19.0/P19.1）——音频友好频点（256×48 kHz，I2S MCK 整数分频），**与官方板的 17.2032 MHz 不同**
  - WCO = 32.768 kHz（Y1，2×10 pF，P18.0/P18.1）
  - EXT_CLK 有源晶振输入在 P7.4：核心板 sheet 10 留有 **Y3 = 24 MHz 有源晶振焊盘（三态输出，R99 0Ω 供电自 VDD/VDDIO_PERI_1V8），默认 DNP 不贴**
- 复位：XRES_L（D8），核心板上有 R? 4.7K 上拉 + C62 1 µF + ESD（sheet 10 Explorer MCU SWD Header 旁），底板另有复位按钮
- BOOT 配置（P17.6）：底板 DS04 拨码，POS=0 内部 RRAM / POS=1 外部 FLASH（SMIF）

## 3. 电源架构 ★与官方最大差异

### 3.1 供电拓扑：外部稳压模式（Supply powered）

sheet 1 明确画法和 datasheet "Supply powered" 参考图一致（页内还贴了该图）：

| 引脚 | 接法 | 与官方 SoM/AI 板（电池模式）对比 |
|---|---|---|
| **VBAT_1/2（B11/D10）** | **接地** | 官方：接 3.3 V |
| VDD_VBAT_1/2（A10/B10） | 1.8 V（VDDA 网络） | 同 |
| VDDD×5 | FB1 120Ω → 1.8 V，4×0.1 µF + 1 µF | 同 |
| VDDA×3 | FB2 120Ω → 1.8 V，2×0.1 µF + 2×1 µF | 同 |
| VDDQ（A11）/ VDDIO7（G12） | 1.8 V，4.7 µF + 0.1 µF | 同（VDDIO7 永远 1.8 V） |
| VDD_1P8_1/2（A9/C12） | R2/R3 0Ω → 1.8 V，各 4.7 µF | 同 |
| **核心电源** | **VOUT_1P8（C13）→ L1 2.2 µH → C10 4.7 µF → R1 0Ω → VCCD**，VFB_1P8（B13）反馈；注："Use DCDC1.8V power supply" | 官方：走 VBAT buck（L22 路径） |
| VOUT_VBAT（A12）/ VFB_VBAT（B12） | **接地**（VBAT buck 停用） | 官方：2.2 µH 电感 |
| **VDDIO6（R10）** | **经 R6 0Ω 接 1.8 V**（网络名 VDD_TEST，注释 "P16、P17 Power Buck"），C41/C42 去耦 | 官方：**接 3.3 V** |
| VDDIO0~5, 8 | FB3 120Ω → 1.8 V，4×1 µF | 同 |
| VDDIO.SMIF0/1 | FB4 120Ω → 1.8 V，4×1 µF | 同 |
| VDD.DSI（D6） | 1.8 V，0.1 µF | 同 |
| DSI_REXT（J6） | **R5 = 200 Ω** | 同（官方 R19/R155 也是 200 Ω） |
| VCCD / VLDO.RET / VLDO.SRAM / VCC.SRAM / VLDO.DSI | 各自去耦（5×0.1 µF、1 µF×4 等） | 同 |
| VDD.USB（M7） | VDDUSB_3V3（来自底板），1 µF + 0.1 µF | 同 |
| VREF（K14） | R4 0Ω **DNI** + C17 1 µF | 官方贴 0Ω |

### 3.2 板上 1.8 V 电源（sheet 9）

- **U6 = TPS63802DLAR**（2 A buck-boost）：**VIN = 3.0~5.0 V**（来自底板 VOUT，即电池 SYS 或 USB 5 V）→ **VDD_1V8**
  - FB 分压 R87 240 K / R89 91 K，VFB=0.5 V → 0.5×(1+240/91) ≈ **1.82 V**
  - L2 1 µH，C59 10 µF + C60 22 µF；EN 经 R85 100K 上拉（上电即开）
- 0Ω 分配：R91 → VDD/VDDIO_1V8（MCU 域）、R92 → VDD/VDDIO_PERI_1V8（外设）、R93 → VDD_MEM_1V8（存储）

### 3.3 逻辑电平结论

- **全板 1.8 V**：所有 GPIO（**含 P16/P17**）、SMIF、DSI、SDIO、I2C
- **3.3 V 只有两处**：VDD.USB（PHY 专用）与底板的 VDD/VDDIO_3V3
- 3.3V 外设接口全部靠底板上的 TXS0108E/TXS02612 电平转换（见底板 §8）——**没有用 VDDIO6=3.3V 的官方做法**
- 代价与收益：外部稳压模式 Hibernate+RTC 休眠电流 ≈5.7 µA（电池模式可做到 ≈2.2 µA，见电源轨文档 §9）；但电源树简单，buck-boost 直通电池全电压范围
- **底板实际供给**：主电源是 **VOUT 3.0~5.0 V**（USB 5 V / 电池 SYS 经 OR'ing，BTB CN1 pin 1~5 进核心板叫 VIN），不是 3.3 V；3.3 V 仅以 VDDUSB_3V3（USB PHY）+ VDD/VDDIO_3V3（CN2 pin 16，核心板上无消费者，预留）两路小电流进入。若自研底板只供 3.3 V 给 VIN 也能工作（落在 3.0~5.0 V 范围内）
- **VDDIO6=1.8 V 可以改**：VDDIO_6 经 R6 0Ω 接 VDD/VDDIO_1V8（网络名 VDD_TEST）。去掉 R6、往 VDD_TEST 注入 3.3 V（BTB 上现有 VDD/VDDIO_3V3 一针可用）即变 3.3 V 域。但**配套底板就失效了**：底板的 TXS0108E A 侧按 1.8 V 设计（VCCA=1.8 V，A 侧信号超 VCCA 不允许），USER_LED 的 NMOS 栅极、DISP_TP 直连等也都按 1.8 V——改 VDDIO6=3.3 V 意味着配自己的底板（即官方 VDDIO6=3.3 V 路线），这套底板不再匹配

## 4. 存储（sheet 7 + 4）

| 器件 | 型号 | 类型 | 挂载 | 片选 |
|---|---|---|---|---|
| U2 | S25FS128SAGMFI101 | 128 Mb QSPI Flash | SMIF0（DATA0~3_INT、CLKP_INT） | smif0_spihb_select1 = **P2.0** |
| U3 | S70KS1283GABHV020 | 128 Mb HyperRAM | SMIF1（DQ0~7、CK/CK#、RWDS_INT） | smif1_spihb_select2 = **P0.1**（经 R60 0Ω） |

- 全部 SMIF 信号串 **33 Ω**（R32~R53，在 MCU 页 sheet 4）；R61/R62 10K 上拉；VDD_MEM_1V8 供电
- MEMORY_RST = **P21.5**，不出核心板；smif0_spihb_select0 = **P5.0 悬空未用**（板载两颗存储用 select1/select2，select0 没接器件也没上 BTB）
- SMIF0 的 P1_0~P1_3 标 "Float"（板载 QSPI 占用 DATA0~3_INT，不引到连接器）
- 比官方 SoM 少一颗 1 Gb HyperFlash（官方三颗，这里两颗）

## 5. AMIC & DMIC（sheet 8）★音频前端参考

**CTB 运放的反馈/偏置 RC 在核心板上，麦克风本体在底板上**（信号经 BTB 往返）：

```
底板 mic ──BTB(AMIC_x_INP/INN_EXT)──► 0Ω ──► 43K ──┬── 75K ──► MCU P14（CTB 裸脚）
                                                   ├── 1.2M 反馈 ──► CTB_OUT
                                                   ├── 10pF（+10pF DNI）
                                                   └── 270pF 差分滤波
偏置：2.4M（一路到 1.8V、一路到 GND）
```

- MIC1：R63 1.2M 反馈，输入 R65/R71 75K + R66/R72 43K，C47 DNI/C49 10pF，C52 270 pF，R75/R77 2.4M
- MIC2：R64 1.2M，R68/R76 75K + R69/R78 43K，C48 DNI/C50 10pF，C51 270 pF，R79/R80 2.4M
- 增益 ≈ 1.2M / (75K+43K) ≈ 10.2 ≈ **20 dB**（官方 AI 板为 1.27M/80.4K ≈ 15.8 ≈ 24 dB）
- **MCU 引脚**：P14.0=AMIC_1_INP、P14.1=AMIC_1_INN、P14.2=AMIC_1_OUT/CTB_IN_4、P14.3=AMIC_2_OUT、P14.4=AMIC_2_INP、P14.5=AMIC_2_INN（P14.6/P14.7 作 GPIO_14/15）
- **PDM 麦 ×2 在核心板上**：U4/U5 = **LMD2718T261-OA1**，共总线 L/R 配对（U4 SELECT=GND 时钟低有效、U5 SELECT=1.8V 时钟高有效），DATA 串 100 Ω、CLK 串 0 Ω → PDM_DATA/PDM_CLK（**P8.5/P8.6**），TP1/TP2 测试点 DNI

### 5.1 模拟子系统资源占用盘点（含电源页相关）

核心板上的模拟电路只有三处：上表两路 AMIC 前端 RC、VDDA 磁珠滤波（FB2 + 2×0.1 µF + 2×1 µF）、VREF 旁路（C17 1 µF，R4 0Ω DNI 预留，VREF 引到 BTB 供底板使用）。E84 其余模拟资源在本板的占用情况：

| 资源 | 端口 | 本板用法 |
|---|---|---|
| CTB0 运放组 | P14.0~14.5 | AMIC×2 前端（正业） |
| CTB1 运放组 | P13.0~13.7 | **全部作数字**：I2C7_SCL/SDA（触摸）、GPIO_0~4/7 |
| SAR ADC 直通道 | P15.0~15.7 | 7 脚被 SPI9/LCD 控制占为数字，**仅 P15.5 作 ADC**（ARD_ADC_3，底板电池分压监测） |
| DAC0 / DAC1 | P14.7 / P13.6 | 作 GPIO_15 / GPIO_1，未作模拟输出 |

即本板只用到了"CTB0 双麦前端 + 1 路 ADC 通道"；底板想加模拟采集只有 P15.5 一路专用通道（其余靠 SARMUX 复用数字脚）。自研板若需要多路模拟（电池、多传感器、电位器），P13/P15 不应这样分配。

## 6. BTB 连接器（sheet 5–6）

2× **DF40C_80DP**（80 pin，0.4 mm）。跨板资源：

- **电源入**：VIN 3.0~5.0 V（核心板唯一主输入）、VDDUSB_3V3；**电源出**：VDD_1V8（回供底板 MCU 域外设）
- 显示：MIPI DSI 2-lane（各串 33 Ω R26~R31）+ DISP_RST/BL_PWM_DISP/DISP_TE/TP 触摸
- 音频：I2S_TX_MCK/SCK/SD、PDM_CLK/DATA、AMIC 差分 EXT
- 通信：SD 卡（P7 组，33 Ω 在核心板侧 R54~R59）、WL_SDIO/BT_UART、I2C、I3C、I2C7、SPI9、CAN0、RPI_UART5、SERIAL_INT×4
- 调试：SWD 全套、TRACE×5、XRES_L、BOOT、调试 UART（UART_TX/RX/CTS/RTS，核心板另有独立 SWD 座 J1）
- 杂项：GPIO_0~19、USER_LED/SW、USB 控制、CTRL（底板 3.3V buck 使能）、VREF
- **不外扩**：SMIF 存储总线（数据/时钟/RWDS/片选全部留在核心板）、EXT_CLK、MEMORY_RST——底板无法外挂 Flash/PSRAM（与官方 SoM 把全套 SMIF 引出金手指的做法不同）

## 7. 核心板 IO 分配总表（sheet 3–4，逐脚核对）

电源域：VDDIO0=P6/P7，VDDIO1=P8，VDDIO2=P9/P20，VDDIO3=P10/P11，VDDIO4=P12/P21，VDDIO5=P18/P19，**VDDIO6=P16/P17（本板=1.8 V！）**，VDDIO7=P13/P14，VDDIO8=P15，SMIF0=P1/P2/P3/P5，SMIF1=P0/P4。

| 引脚 | 网络 | 说明 |
|---|---|---|
| P0.0 | GPIO_19 | 通用（经 BTB） |
| P0.1 | smif1_spihb_select2 | HyperRAM 片选 |
| P2.0 | smif0_spihb_select1 | 板载 QSPI Flash 片选 |
| P3.0 / P3.1 | I3C_SCL / I3C_SDA | I3C 总线 |
| P5.0 | smif0_spihb_select0 | **悬空（NC）**——没接器件也没上 BTB |
| P6.0~6.3 | TDO_SWO / TDI / TMS_SWDIO / TCLK_SWCLK | 调试 |
| P6.4~6.7 | UART_CTS / UART_RX / UART_RTS / UART_TX | 调试串口（去 BTB→KitProg3） |
| P7.0~7.1 | SD_CARD_CMD / SD_CARD_CLK | SD 卡（底板 TXS02612 转 3.3 V） |
| P7.2 | **CTRL** | **底板 3.3 V buck 使能**（整机门控） |
| P7.3 | SD_CARD_D0 | |
| P7.4 | EXT_CLK | 外部时钟输入，**仅核心板，未上 BTB**（WiFi 在底板自带时钟，用不到） |
| P7.5~7.7 | SD_CARD_D1 / D2 / D3 | |
| P8.0 / P8.1 | **I2C_SCL / I2C_SDA** | 主 1.8 V I2C（ES8388 控制、LSM6DS3、BTB） |
| P8.2 | GPIO_16 | |
| P8.3 | USER_SW1 | 用户按键 |
| P8.4 | I2C_INT2_EXT | 兼作电池分压门控 ADC_CTRL（底板 Q5） |
| P8.5 / P8.6 | PDM_CLK / PDM_DATA | 核心板两颗 PDM 麦 |
| P8.7 | GPIO_17 | |
| P9.0~9.3 | SERIAL_INT_0~3 | 串行中断（去 BTB） |
| P10.0~10.3 | BT_UART_RXD / TXD / CTS / RTS | 蓝牙 |
| P10.4 / P10.6 | BT_HOST_WAKE / BT_DEV_WAKE | |
| P10.5 / P10.7 | GPIO_13 / GPIO_12 | |
| P11.0 / P11.2 | BT_REG_ON / WL_DEV_WAKE | 无线控制 |
| P11.4 / P11.6 | WL_HOST_WAKE / WL_REG_ON | |
| P11.1 / P11.3 / P11.5 / P11.7 | GPIO_11 / GPIO_10 / GPIO_9 / GPIO_8 | |
| P12.0~12.2 | WL_SDIO_CLK / D0 / D1 | WiFi SDIO（串 33 Ω R20~R25） |
| P12.3 | **I2S_TX_FSYNC** | 与官方一致 |
| P12.4 / P12.5 | WL_SDIO_D2 / D3 | |
| P13.0 / P13.3~13.7 | GPIO_7 / GPIO_4 / GPIO_3 / GPIO_2 / GPIO_1 / GPIO_0 | |
| P13.1 / P13.2 | **I2C7_SCL / I2C7_SDA** | 触摸屏 I2C（底板转 3.3 V） |
| P14.0~14.5 | AMIC_1_INP/INN/OUT、AMIC_2_OUT/INP/INN | CTB0 模拟麦（sheet 8 RC） |
| P14.6 / P14.7 | GPIO_14 / GPIO_15 | |
| P15.0~15.4 | SPI9_CLK / MOSI / MISO / SS0 / SS1 | SPI9（底板转 3.3 V） |
| P15.5 | **ARD_ADC_3** | **电池电压分压采样**（底板 R16/R17） |
| P15.6 / P15.7 | LCD_PWR_EN / LCD_BL_EN | 显示电源/背光门控 |
| P16.0 / P16.1 | CAN0_RX / CAN0_TX | CAN（底板转 3.3 V→TJA1042） |
| P16.2 / P16.3 | Audio_CRTL / WIFI_CTRL | codec 1.8V / WiFi 电源门控 |
| P16.4 | USB_FAULT | |
| P16.5~16.7 | USER_LED3 / USER_LED2 / USER_LED1 | |
| P17.0 / P17.1 | RPI_UART5_RX / RPI_UART5_TX | 树莓派串口（转 3.3 V） |
| P17.2 / P17.3 | DISP_TP_INT / DISP_TP_RST | 触摸 |
| P17.4 / P17.5 | USB_DEVICE_DET / USB_HOST_EN | |
| P17.6 | **BOOT** | 启动源选择拨码 |
| P17.7 | SD_CARD_DET | |
| P18.0 / P18.1 | WCO_OUT / WCO_IN | 32.768 kHz |
| P19.0 / P19.1 | ECO_IN / ECO_OUT | **12.288 MHz** |
| P20.0~20.4 | TRACE_CLK / TRACE_DATA_3~0 | |
| P20.5 / P20.6 / P20.7 | DISP_UPDN(TE) / BL_PWM_DISP / DISP_RST | |
| P21.0 | WL_SDIO_CMD | |
| P21.1~21.3 | I2S_TX_SD / SCK / MCK | → ES8388 |
| P21.4 | GPIO_18 | |
| P21.5 | MEMORY_RST | |
| P21.6 | OA_CTRL | 喇叭功放 5V 门控 |
| P21.7 | I2C_INT1 | |
| DSI / USB | DSI 2-lane（33 Ω）、USB_DP/DM | 去 BTB |

---

# 第二部分：底板 Edgi-Talk_Basic（16 页）

## 8. 电源树（sheet 2、10）★低功耗门控设计参考

```
USB-C (CN11/CN12) 5V ──SS34──► USB_VCC_5V ──► U12 SY6280(1.44A 限流) ──► USB_VCC_5V_O ──┐
                                                                                         ├─ U11/U14 LM66100 理想二极管 ─► VOUT
1S 电池(CN1) ──► U2 BQ25606 充电 ─► SYS ──► U13 SY6280(1.44A 限流) ──► SYS_O ──────────┘      │
                                                                                              ├─► U16 TPS61023 boost → VCC_5V（SMAJ5.0CA 保护）
                                                                                              ├─► U15 TPS63802 → VDD/VDDIO_3V3（EN=CTRL，MCU P7.2 可控！）
                                                                                              └─► BTB VIN → 核心板 1.8 V
```

- **充电 U2 = BQ25606RGER**（standalone 3 A buck 充电，1S）：478 mA 充电限流（R12 1K ILIM），TS 接电池 NTC（R13 510Ω），STAT/PG# 驱动红/绿 LED，C24 10 µF；注释"仅支持 1S 电池，充电完成红灯熄灭"
- **U12/U13 = SY6280AAC** 限流开关（R42/R44 4.7K → 1.44 A）
- **U11/U14 = LM66100DCKR** 理想二极管 OR'ing（USB 优先/电池备份，CE# 交叉控制）
- **U16 = TPS61023DRLR** 升压 → VCC_5V（音频功放、背光、CAN、USB host 用）
- **U15 = TPS63802DLAR** → VDD/VDDIO_3V3，**EN 由 MCU 的 CTRL（P7.2）控制**（R45 100K 下拉）——MCU 可切断整个 3.3 V 域
- **无库仑计/电量计**：电量靠 ADC 测压（下节）

### 8.1 电池电压监测（sheet 2，省电路）

VBAT_OUT → R16 10K / R17 7.5K 分压（4.2 V→1.8 V 满量程）→ **ARD_ADC_3（P15.5，SAR ADC）**，D3 ESD5Z2.5 + C25 10 nF。
分压器下端串 **Q5 AO3400A NMOS**（栅极=ADC_CTRL/I2C_INT2_EXT=P8.4，R19 100K 下拉）——**只有测量时才导通**，避免分压器常耗 ~240 µA。这是"无电量计芯片"的低成本做法（对照：NPM1300 方案是 System Monitor + 主机算法）。

### 8.2 电源门控一览（全部 MCU GPIO 经 LMUN2212+CJ2301 高边开关）

| 控制信号（MCU 引脚） | 门控对象 |
|---|---|
| Audio_CRTL（P16.2，经电平转换 Audio_CRTL_L） | ES8388 的 Audio_VDD_1V8 |
| OA_CTRL（P21.6） | MD8002 功放的 OA_VCC_5V |
| WIFI_CTRL（P16.3 → WIFI_CTRL_L） | CYW55512 的 1.8 V + 3.3 V 两路 |
| LCD_PWR_EN（P15.6） | 屏 VCI（3.3 V） |
| LCD_BL_EN（P15.7） | 背光 boost（VCC_5V） |
| CTRL（P7.2） | **整个 VDD/VDDIO_3V3 域** |

休眠时把 3.3 V 域整体关掉，只留核心板 1.8 V（RTC/Hibernate 所需）——弥补了外部稳压模式休眠电流偏高的短板。

## 9. 音频（sheet 1）★与官方 TLV320 方案并列的参考

- **IC1 = ES8388**（Everest 立体声 codec，双 ADC+双 DAC）：
  - I2S：I2S_TX_MCK/SCK/FSYNC/SD（P21.3/21.2、P12.3、P21.1，串 R1~R4 0Ω）→ MCLK/SCLK/LRCK/DSDIN；**ASDOUT 未接**（录音走片上 PDM/CTB，codec 只做回放）
  - 控制：CCLK/CDATA = **1.8 V I2C**（P8.0/P8.1 主总线）
  - 供电：DVDD=Audio_VDD_1V8（Q1 门控），模拟输出驱动接 3.3 V（"3V3 Supply for analog output drivers"，C5/C6）
  - LOUT2/ROUT2 经 C7/C8 1 µF → SPK_IN；LOUT1/ROUT1（耳机）与 LIN/RIN 输入均 NC
- **U1 = MD8002**（2 W AB 类功放，LM4871 兼容）：SPK_IN 经 C14 1 µF + R6 5.6K 输入，R9 20K 反馈（增益≈7），VDD=OA_VCC_5V（Q3 门控，R8 10Ω + C15/C16）→ 喇叭 ACC1（MCABS-247-RC）
- 对照：官方 AI 板用 TLV320DAC3100（D 类，效率高）；这里 ES8388+MD8002 是低成本 AB 类方案

### 9.1 底板模拟麦（sheet 9）与跨板链路

模拟麦**本体在底板、CTB 反馈/偏置 RC 在核心板**，中间经 BTB 连接器 CN2 pin 1~4 走 4 根差分线（AMIC_2_INP/INN_EXT、AMIC_1_INP/INN_EXT），麦克风供电 VDD_1V8 也来自核心板：

```
底板 sheet 9                          BTB(CN2 1~4)            核心板 sheet 8                MCU
┌──────────────────────┐                                     ┌────────────────────┐
│ MIC1: IM73A135V01     │  OUTPUT_P ─R36 25K─► AMIC_1_INP_EXT ═► R73 0Ω─R72 43K─┬─R71 75K─► P14.0 (INP)
│      (差分, 默认NC不贴) │  OUTPUT_N ─R37 25K─► AMIC_1_INN_EXT ═► R67 0Ω─R66 43K─┼─R65 75K─► P14.1 (INN)
│      C50/51/53 100pF  │                                     │      R63 1.2M 反馈 ◄─ P14.2 (OUT)
├──────────────────────┤                                     │      R75/R77 2.4M 偏置  │
│ MIC2: GMI4015P-2C-42db│  偏置节点 ─────────► AMIC_2_INP_EXT ═► R74 0Ω─R78 43K─┬─R76 75K─► P14.4 (INP)
│      (单端, 默认贴装)   │  输出 ──R40 680Ω──► AMIC_2_INN_EXT ═► R70 0Ω─R69 43K─┼─R68 75K─► P14.5 (INN)
│      R39 2.2K 偏置    │                                     │      R64 1.2M 反馈 ◄─ P14.3 (OUT)
└──────────────────────┘                                     └────────────────────┘
```

- **MIC1（默认不贴）**：IM73A135V01XTSA1 差分麦（与官方 AI 板同款），OUTPUT_P/N 各串 25K + 100pF 滤波后上 BTB
- **MIC2（默认贴装）**：GMI4015P-2C-42db 单端麦（-42 dB 灵敏度），R39 2.2K 偏置到 VDD_1V8，信号经 R40 680Ω 进 **INN 腿**，INP 腿接偏置节点作参考——即"伪差分"接法
- 到核心板后进 CTB0 运放（伪差分 PGA，增益≈20 dB，见 §5），运放输出片内送 SAR ADC
- AMIC_1/2_INN_EXT 还引到了底板 **CN6（2x5P 排针）**——可从外部注入模拟 mic 信号做实验
- 设计权衡：高阻抗模拟小信号过 BTB 连接器是无奈之举（麦要在机壳边缘、运放裸脚要贴近 MCU）；自研单板应把麦和 RC 都放在 MCU 旁边（参照官方 AI 板 §6.1）

## 10. 显示（sheet 7）

- **CN10 = FPC-05F-30P 30-pin**：**MIPI DSI 2-lane** + VCI + 1.8V + 触摸（TP_SDA/TP_SCL、DISP_TP_INT/RST）+ DISP_RST + DISP_TE（R29 0Ω DNP）
- VCI = 3.3 V（Q8 门控，LCD_PWR_EN）
- 背光：**AP3019A** boost LED 驱动（VCC_5V 经 Q6 门控，LCD_BL_EN；L2 22 µH，R28 10 Ω 恒流反馈；CTRL=BL_PWM_DISP=P20.6）
- DSI 测试点 TP12~TP17；触摸 I2C = MCU I2C7（P13.1/13.2）经 TXS0108E 转 3.3 V
- 与官方一致：**没有使用 8080 并口**

## 11. 无线（sheet 15）

- **U23 = CYW55512**（WiFi+BT 模组）：
  - WiFi：SDIO（WL_SDIO_D0~3/CMD/CLK，核心板侧串 33 Ω）；BT：UART_RTS_N/RXD/TXD/CTS_N
  - 控制：WL_REG_ON、BT_REG_ON（10K 上拉）、WL_HOST_WAKE、BT_HOST_WAKE、BT_WAKE、WL_DEV_WAKE
  - 供电：**BAT=3.3 V**（WIFI_VDD_3V3，R98 0Ω，Q15 门控）、**VDDIO=1.8 V**（WIFI_VDD_1V8，Q13 门控，匹配 MCU 1.8 V IO）；两路都被 WIFI_CTRL_L（P16.3 经电平转换）门控，可整模块断电
  - VIN_LDO/VIN_LDO_OUT 接 L5 4.7 µH（模块内部调节器外置电感）
  - X1 32.768 kHz 睡眠时钟（R102 DNP）
  - 射频：WL_BT_ANT → C85 10 pF + C86/C87 DNP 匹配 → R137 0Ω → 板载 RF_PCB 天线 / J3（2450AD18A6050）

## 12. 其余外设

### 12.0 底板连接器规格表（料号→间距/规格）

SDK 内可获得性：原理图含全部料号（间距由料号确定）；3D 模型（`docs/3d model/basic.stp` 778 MB、`core.stp` 395 MB）可量取位置与叠高；**底板 BOM 未附带**，个别料号后缀以实物为准。

| 位号 | 功能 | 料号/命名 | 间距/规格 |
|---|---|---|---|
| BTB×2（核心板↔底板） | 板对板 | 核心板侧 DF40C_80DP ↔ 底板侧 **DF40C_80DS** | **0.4 mm**，80 P，0.3 A/针；叠高 1.5~4.0 mm 系列（具体档位原理图未标注，以 BOM/3D 为准） |
| CN5 | 树莓派 40-pin | 2x20P_2.54RPI | **2.54 mm** |
| CN6 / CN7 | 扩展排针 / 调试串口 | 2x5P_2.54 | **2.54 mm** |
| CN8 / CN9 | PMOD ×2 | 2x6p_2.54 | **2.54 mm** |
| CN10 | 显示 FPC | FPC-05F-30P | **0.5 mm** 30P 卧贴 FPC |
| CN1 | 电池座（VBAT/NTC/GND 3P） | Würth WR-WTB 系列（同系列 2P 款 620102131822 已确认） | **2.00 mm**，卧贴带锁；3P 完整料号以 BOM 为准 |
| CN4 | CAN 总线 | WAFER-PH2.0-2PWB | **2.00 mm**（JST PH 兼容） |
| CN11 / CN12 | USB 设备 / 调试 | Type-C 16P | USB-C 标准 |
| J2 | JTAG/SWD | HDR 10x2 | 间距待确认（Cortex 10P 调试常用 1.27 mm） |
| ACC1 | 喇叭 | MCABS-247-RC | 喇叭端子 |
| J3 | WiFi 天线 | 2450AD18A6050 | 2.4 GHz 天线（非连接器） |

| 功能 | 器件/设计 | 电平 |
|---|---|---|
| SD 卡（sheet 11） | **TXS02612RTWR** 专用 SD 电平转换（VCCA=1.8 V / VCCB=3.3 V，带 ESD/EMI），SD_CARD_DET=P17.7 | MCU 侧 1.8 V，卡侧 3.3 V |
| CAN（sheet 5） | **TJA1042T/3**（VIO=3.3 V，VCC=5 V），R25 120 Ω 终端，PESD2CAN，CN4 PH2.0 座；MCU 侧 CAN0_RX/TX（P16.0/16.1）经 TXS0108E 转 3.3 V | 3.3 V 域 |
| 温湿度（sheet 12） | **AHT20**，挂在 **RPI_SCL/RPI_SDA（3.3 V I2C）** | 3.3 V |
| IMU（sheet 12） | **LSM6DS3TR-C**，1.8 V，挂**主 I2C（P8.0/8.1）**，IMU_INT1，CS 上拉（I2C 模式） | 1.8 V |
| USB（sheet 13） | CN11 Type-C **device**（CC 5.1K×2，BST236A054U ESD）→ MCU USB_DP/DM，VBUS 经 SS34 入电源树；CN12 Type-C **debug** → KitProg3 | — |
| 用户界面（sheet 14） | 3× 用户 LED（3.3 V 经 AO3400 NMOS，P16.5~16.7）、用户按键（P8.3）、复位键（XRES_L）、BOOT 拨码、JTAG 10×2 座（SWD+TRACE） | — |
| 调试（sheet 16） | **PSoC 5LP KitProg3**（U24）：SWD + UART 桥（DEBUG_UART ↔ P6.4~6.7），VTARG=1.8 V，USB_V_SENSE 分压监测 | — |
| 扩展（sheet 6） | **CN5 树莓派 40-pin**（标准引脚定义，全 3.3 V：I2C/SPI/UART/19×GPIO，注意 27/28 脚 ID_I2C 复用为 TP_SDA/TP_SCL）、CN8/CN9 PMOD×2、CN6（PDM/AMIC_EXT/VREF/DISP 控制）、CN7（调试串口） | 3.3 V |

## 13. 电平转换设计（sheet 8、11）★全 1.8 V 系统的 3.3 V 对策

**3× TXS0108EPWR + 1× TXS02612RTWR**，全部 VCCA=1.8 V、VCCB=3.3 V，OE 经 1K 上拉常开：

| 转换器 | 1.8 V 侧（MCU） | 3.3 V 侧 |
|---|---|---|
| U4 | GPIO_0/1/3/4/5/6/8~15/18/19（含 I3C_SDA/SCL） | 树莓派 40-pin GPIO |
| U6 | SERIAL_INT_0~3、CAN0_RX/TX、Audio_CRTL、WIFI_CTRL、RPI_UART5_TX/RX、SPI9 全套、I2C_INT1 | CAN 收发器、codec 门控、WiFi 门控、RPI UART、SPI 外设 |
| U8 | GPIO_16/17、**I2C7_SCL/SDA** | RPI_GPIO_16/17、**TP_SCL/TP_SDA（触摸）** |
| U17（TXS02612） | SD_CARD_D0~3/CLK/CMD | SD 卡 |

注意 TXS0108E 是自动双向推挽型，适合开漏 I2C 和推挽 IO，但驱动能力弱（上拉靠内部 4K/40K，页内有注释提醒）——高速/重负载信号不宜走这条路。

---

# 第三部分：与官方开发板的关键差异（对 openhifi 的启示）

| 维度 | 官方 SoM / AI 板 | Edgi-Talk | 点评 |
|---|---|---|---|
| 供电拓扑 | **电池模式**：VBAT=3.3 V，核心走 VBAT buck | **外部稳压模式**：全 1.8 V，VBAT 接地，核心走 1P8 buck | 电池模式休眠电流低 2~3 倍（2.2 vs 5.7 µA）；Edgi-Talk 用"整域门控 3.3 V"补偿 |
| 3.3 V 对策 | **VDDIO6=3.3 V**，P16/P17 承担全部 3.3 V IO | **VDDIO6=1.8 V**，4 颗电平转换芯片 | 官方路线省 BOM；Edgi-Talk 路线下 P16/P17 仍当普通 GPIO 用，且 3.3 V 域可整体断电 |
| 核心板 1.8 V 来源 | 底板供给 | **核心板自产**（TPS63802 buck-boost，VIN 3~5 V） | 核心板可脱离底板单跑 |
| ECO | 17.2032 MHz | **12.288 MHz**（音频整数倍） | 做音频产品值得参考，I2S MCK 分频干净 |
| 充电 | 无（AI 板只有 OR'ing） | BQ25606（478 mA，standalone，NTC） | openhifi 选 NPM1300 集成度更高（充电+电量计算法+2Buck+2LDO） |
| 电池监测 | KitProg3 测 VTARG | **NMOS 门控分压 → SAR ADC**（省电、零成本） | 与 NPM1300 的 System Monitor 是两种价位路线 |
| 音频回放 | TLV320DAC3100（D 类，I2C 1.8 V） | ES8388（回放 only）+ MD8002（AB 类 2 W） | 两条都验证过；D 类效率优先，AB 类成本优先 |
| 音频采集 | AMIC→CTB（增益 ~24 dB）、IM73D122 PDM | AMIC→CTB（增益 ~20 dB，mic 在底板 RC 在核心板）、LMD2718 PDM 在核心板 | 拓扑相同，RC 取值与布局分割不同 |
| 显示 | DSI 15-pin（树莓派屏） | DSI 30-pin FPC + AP3019A 背光 | 都不用 8080 |
| WiFi | CYW55513（SoM 集成） | CYW55512（底板，可整模块断电） | 55512 BAT=3.3 V / VDDIO=1.8 V |

## 14. 可借鉴的设计点

1. **电源门控矩阵**：codec / 功放 / WiFi / 背光 / 屏 VCI / 整个 3.3 V 域都有 MCU 可控高边开关（LMUN2212+CJ2301，约 2 分钱一颗），休眠时只留核心板 1.8 V——电池产品直接参考。
2. **NMOS 门控电池分压**：P8.4 控制 Q5，测量才导通，省掉 240 µA 常耗和专用电量计。
3. **ECO 用 12.288 MHz**：I2S 音频时钟整数分频。
4. **外部稳压模式 + 整域门控**是电池模式的合法替代，但休眠电流高 ~3 µA；openhifi 用 NPM1300 时应坚持**电池模式**（BUCK1=3.3 V→VBAT，见电源轨文档 §14），不要照搬本板。
5. **mic 与 CTB 反馈 RC 分板放置**（mic 在底板、RC 在核心板近 MCU）是可行的，但走线过 BTB 会增加干扰风险——自研单板应把 mic、RC、MCU 放一起（参照官方 AI 板 §6.1）。
6. 电平转换芯片方案（TXS0108E/TXS02612）只适合低速信号；**3.3 V 外设多的产品仍应走官方 VDDIO6=3.3 V 路线**。
