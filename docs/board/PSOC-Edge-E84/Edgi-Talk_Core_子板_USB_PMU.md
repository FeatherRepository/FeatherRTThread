# USB+PMU 子板设计（两种规格）

> 基于 Edgi-Talk_Core + 自研载板体系，见 `Edgi-Talk_Core_拓展载板_接口分配方案.md`、`Edgi-Talk_Core_自研底板设计约束.md`。
> 两种规格共用 PMU（电池充放电 + 系统供电），区别在 USB 角色：
> - **子板 A：USB-C OTG（DRP 双角色）**——设备模式从 USB 充电；主机模式对外输出 5 V VBUS
> - **子板 B：磁吸 4 触点**——纯 USB 设备，只受电（5V/GND/D+/D-）

## 1. E84 侧资源契约（两规格共用，全在 BTB 上）

| 信号 | MCU 引脚 | 用途 |
|---|---|---|
| USB_DP / USB_DN | 专用脚（N7/P7） | USB HS 480 Mbps 数据，device/host 同一对 |
| VDDUSB_3V3 | VDD.USB（M7） | USB PHY 供电 3.3 V，两规格都必须供 |
| USB_HOST_EN | P17.5 | 主机模式 VBUS 输出开关使能 |
| USB_DEVICE_DET | P17.4 | VBUS 插入检测（设备模式） |
| USB_FAULT | P16.4 | 主机输出过流标志输入 |
| DRP/CC 控制（ID/DIR/中断等） | 空闲 GPIO 池 | ⚠️ 别照搬 AI 板的 P7.x/P8.3——本设计 P7 是 SD/eMMC 组，用 P9/P13/P20/P21 空闲脚 |

E84 USB PHY 内置 **BC1.2 充电器类型识别**（CHARGER_TYPE_DETECTION），设备模式下可分辨 SDP/CDP/DCP。

## 2. 子板 A：USB-C OTG + 充放电

```
        设备模式（受电）                    主机模式（放电）
USB-C VBUS ──┐                            电池 ──► boost 5V ──► VBUS 负载开关 ──► VBUS 对外
             ▼                                                  ▲
        充电器(带电源路径) ──► 电池 ──► 系统电源                 USB_HOST_EN (P17.5) 使能
             │                    │                             CC 控制器做 DRP 角色协商
             └────► VIN(核心板) + 3.3V buck(载板)
```

**关键点：OTG 的"电池→5V"反向路径，主流 PMIC 里不是标配**

| 方案 | 器件（本地 BOM 已有） | 说明 |
|---|---|---|
| 方案一 | **NPM1300** + 外挂 boost（TPS61023）+ VBUS 负载开关 | NPM1300 只有降压充电，**VBUSOUT 只是 VBUS 直通**（规格书 §6.1.5：仅 VBUS 存在时输出，给 nRF 供电用），无反向 boost；外挂 TPS61023（原 Edgi-Talk 底板同款 5V boost）+ 负载开关组成 host 供电路径 |
| 方案二 | **BQ25895**（单芯片） | 降压充电 + **集成 OTG boost**（电池→5 V，2 A+ 级），I2C 控制；但无电量计，需 ADC 测压或外挂 MAX17055（BOM 也有） |

- CC/DRP：Type-C 双角色需要 CC 控制器，参照 AI 板（U21 DRP 控制器 + XC8111 host 供电开关），MCU 侧控制信号从空闲 GPIO 池分配
- 主机供电能力：USB2.0 默认 500 mA；CC 宣告 1.5 A/3 A 则 boost、电池放电能力、触点/线材都要跟上
- 保护：VBUS 输出限流开关（OCP 反馈→USB_FAULT=P16.4）、ESD、浪涌
- 参考实现：AI 板的"TPS61022 5V 升压 + XC8111 host 开关 + CC DRP 控制器"就是完整的官方 OTG 电源设计（见其分析文档 §3）

## 3. 子板 B：磁吸 4 触点（纯设备）

```
磁吸触点: 5V ──► 充电器 ──► 电池 ──► 系统电源（VIN + 3.3V）
         GND ──► 共地
         D+/D- ──► E84 USB_DP/DN（device only）
```

- 结构最简：无 boost、无 host 开关、无 CC 控制器
- **没有 CC 脚**（只有 4 触点）→ 按 legacy USB 处理，输入限流来源：
  - 充电器自带 D+/D- BC1.2 检测（如 BQ25606 standalone，原 Edgi-Talk 底板同款）自主设定限流；或
  - NPM1300 由 E84 经 I2C 配置（默认只有 100 mA，必须主机软件改限流）
- VBUS 插入检测 → USB_DEVICE_DET（P17.4）
- **注意磁吸触点额定电流**：充电电流上限受触点规格约束（常见 2 A/触点），别让充电器限流超过触点能力
- 5V 触点进板先做 ESD/浪涌防护（触点暴露在外）

## 4. 两规格对比

| 项 | 子板 A（OTG） | 子板 B（磁吸） |
|---|---|---|
| USB 角色 | device + host（DRP） | 仅 device |
| 充电 | ✓（USB-C，CC 协商电流） | ✓（5V 触点，BC1.2/固定限流） |
| 对外供电 | ✓ 电池→boost→VBUS 5V | ✗ |
| 关键器件 | 充电 + boost + VBUS 开关 + CC 控制器 | 仅充电器 |
| E84 控制信号 | HOST_EN/DEVICE_DET/FAULT + CC 控制器接口 | DEVICE_DET |
| 面积/BOM | 大 | 小 |
| 典型场景 | 桌面/拓展坞形态，挂 U 盘等外设 | 便携/穿戴，充电底座形态 |

## 5. 与载板/核心板的接口契约

两规格子板都向载板提供：

| 输出 | 规格 | 说明 |
|---|---|---|
| VIN（给核心板） | 3.0~5.0 V，≥300 mA | 即系统电源轨（电池/USB 经电源路径） |
| VDDUSB_3V3 | 3.3 V | 两规格都用 USB，必供 |
| 电池接口 | 1S Li-ion + NTC | 充电器的 TS 监测 |
| 3.3 V（给载板其他子板） | 按子板负载 | 可由子板 A/B 上的 buck 或载板自产，设计时定一处 |

USB_DP/DN 从子板连接器 → 载板 → BTB（CN2 pin 58/59）到核心板——高速差分，走线 90 Ω 差分阻抗、等长、远离开关节点。

## 6. 原型验证捷径：nPM1300 EK 评估板

若 PMU 方案选 NPM1300，原型阶段不必先画子板——直接用 **nPM1300 EK**（Nordic 官方评估板）当 PMU 子板插进体系：

- EK 内容：nPM1300 全引脚排针、JST 电池座（带/不带 NTC 均可）、USB-C 充电口、LED/按键、nPM PowerUP GUI 配置工具
- 接法：VSYS 排针 → 载板 VIN；BUCK1 配 3.3 V → VDDUSB_3V3 + 载板 3.3 V；E84 主 I2C（P8.0/8.1）→ EK 的 TWI 排针（配置 + 读电池电压/电流/温度，电量计数据）
- 限制：每路 BUCK 仅 200 mA（WiFi/大电流负载挂 VSYS）；插 USB 时 VSYS≈5 V 直通，核心板 TPS63802 上限 5.5 V 可承受
- **不能验证的部分**：EK 没有 OTG host 输出（无 boost/VBUS 开关/CC DRP）、USB 数据不过到 E84——子板 A 的 OTG 功能和子板 B 的磁吸形态仍需自绘
- 产品化时照 EK 公开的原理图/布局画自己的 PMU 子板即可

### 6.1 EK 实际排针映射（PCA10152 原理图 sheet 4 确认）

| 排针 | 内容 | 接载板哪里 |
|---|---|---|
| **P7** | **VSYS**（电池/USB 直通轨）+ 探针环 | → 核心板 VIN（3.0~5.0 V） |
| P4 / P5 | VOUT1（BUCK1）/ VOUT2（BUCK2）+ 探针环 | → VDDUSB_3V3 / 载板子板供电 |
| P6 | VBUSOUT | 一般不用 |
| **P11** | **TWI：SDA、SCL、VDDIO** | → E84 主 I2C（P8.0/8.1） |
| **P18** | **VDDIO 选择跳线**：VDDIO=VOUT1 / VOUT2 / 外部 | **把 VOUT2 配成 1.8 V、P18 跳 VOUT2，TWI 电平就和 E84 的 1.8 V 直连**，无需电平转换 |
| P12 | GPIO0~4 | 中断/电源告警接 E84 GPIO |
| P10 | VSET1/VSET2、SHPHLD（SW1 长按复位） | SHPHLD 可作系统硬复位 |
| P8 | LS/LDO1/2 输入输出 | 外设门控实验 |
| J1/J2（JST） | 电池 + NTC | 接电池 |
| J3 | USB-C → 充电输入（CC 检测充电电流） | 充电器/电脑 |
| J4 | USB-C → nPM Controller（配置用） | 电脑（nPM PowerUP） |

- 板载 VBAT/VBUS/VOUT 电流测量跳线（配合 PPK2 或示波器）——正好用来实测系统各模式功耗
- v0.9.0 老版本 EK 有 erratum（VSYS 需手动接 LSIN），v1.1.0 已修；买到的板子注意版本
