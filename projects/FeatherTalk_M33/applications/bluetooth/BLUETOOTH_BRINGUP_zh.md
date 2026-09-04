# FeatherTalk CYW55500A1 蓝牙调试与上板记录

> 历史记录：下文是 2026-08-30/31 的 AIROC 验证，不代表当前产品后端。
> 2026-09-04 当前 M33 BTstack 与 M55 Wi-Fi 的共享电源、独立复位、IPC 修复及实板共存结果，
> 见 [运行时无线资源管理](../../../FeatherTalk_Common/RADIO_COEXISTENCE_zh.md)。
> 编译宏只决定功能是否编入，不能代替运行时资源所有权和启停管理。

## 1. 文档目的

本文记录 2026-08-30 至 2026-08-31 在 PSoC Edge E84 开发板上打通
CYW55500A1 蓝牙功能的完整过程。它既是调试复盘，也是后续开发者重新构建、烧录、
验证和继续实现配对/GATT 功能时的操作依据。

本轮最终打通的范围是：

- M33 控制板载 CYW55500A1，并在每次冷启动时下载匹配本板的 PatchRAM；
- 启动 Infineon AIROC BLE Host Stack；
- 读取本机蓝牙地址；
- 主动扫描 BLE 广播并维护去重后的设备列表；
- 开关非连接广播；
- 通过 M33 MSH 查看状态、设备和执行扫描/广播命令；
- 蓝牙运行期间，M55 可继续执行完整 UI 和外部 Flash 文件系统测试，不再导致
  M33 XIP 异常、HardFault 或复位。

本轮尚未宣称完成的范围是配对、加密连接、Bond、密钥持久化、产品 GATT 服务、
Bluetooth Classic Profile、蓝牙音频和低功耗休眠。

## 2. 最终架构

```text
M33 / RT-Thread
  |
  |-- AIROC BLE Host Stack（预编译 libbtstack.a）
  |-- Infineon HCI-UART integration（仓库内可审计源码）
  |-- FeatherTalk cyabs_rtos（RT-Thread 适配）
  |
  `-- SCB4 HCI UART + RTS/CTS
          |
          `-- CYW55500A1
                |-- 上电时装载 PatchRAM 到控制器 RAM
                `-- BLE Controller / Radio

M55 / RT-Thread / LVGL
  `-- 后续通过 FeatherTalk IPC 消费蓝牙状态和设备数据
      （当前 MSH 验证入口仍在 M33）
```

板级关键连接来自 SDK 生成配置：

| 信号 | PSoC 资源 | 管脚 |
| --- | --- | --- |
| HCI UART | SCB4 | — |
| BT UART RX | GPIO P10.0 | 控制器 TX → M33 RX |
| BT UART TX | GPIO P10.1 | M33 TX → 控制器 RX |
| BT UART CTS | GPIO P10.2 | 硬件流控 |
| BT UART RTS | GPIO P10.3 | 硬件流控/自动波特率时序 |
| HOST_WAKE | GPIO P10.4 | 控制器唤醒主机 |
| DEVICE_WAKE | GPIO P10.6 | 主机唤醒控制器 |
| BT POWER/REG_ON | GPIO P11.0 | 控制器复位与启动 |
| 无线模组电源 FET | GPIO P16.3 | 使能载板无线电源轨 |

当前关闭控制器休眠，重点先保证 PatchRAM、HCI、Host Stack、扫描和广播链路稳定。

## 3. 固件与软件来源

### 3.1 为什么必须下载控制器固件

CYW55500A1 的蓝牙补丁运行在控制器 RAM 中，不是烧进 M33/M55 镜像后就永久驻留
在无线芯片里。因此每次无线控制器掉电或冷复位后，主机都必须重新执行：

1. 使能无线电源轨；
2. 通过 REG_ON 与 RTS 进入自动波特率启动状态；
3. 建立 HCI UART；
4. 把匹配板级射频、时钟和天线配置的 PatchRAM 下载到控制器；
5. 执行 Launch RAM；
6. 由 AIROC Host Stack 继续初始化 BLE 功能。

Linux 下的 `brcmfmac`/Wi-Fi 固件不能替代这一蓝牙 PatchRAM。Wi-Fi 与蓝牙共享组合
无线器件，但使用不同主机接口、固件装载过程和驱动栈。

### 3.2 仓库内固定依赖

为了保证任何开发机克隆仓库后都能重现构建，正式构建不再引用
`D:\Develop\Edgi-Talk\CYW5551x`、`D:\Develop\Edgi-Talk\wifi` 或其他仓库外绝对
路径，也不再通过 `#include "xxx.c"` 编译外部源码。

| 内容 | 仓库位置 | 固定版本 |
| --- | --- | --- |
| Infineon HCI-UART integration | `vendor/infineon/hci_uart` | 从 `btstack-integration` 的 `a9a0e7f9...` 导入并保留 Apache-2.0 许可 |
| AIROC BTSTACK | `third_party/infineon/btstack` | `3d4617f296ccb1b271abe033fc5a855087faf75f` |
| CYW55500A1 固件 | `third_party/infineon/bt-fw-ifx-cyw55500a1` | `c8f0c55d63d62fc028b93472b42066d624d171a0` |

BTSTACK 和控制器固件仍由官方 Infineon 仓库以 Git 子模块提供，许可证/EULA 跟随
各自官方仓库。克隆产品分支后必须执行：

```powershell
git submodule update --init --recursive
```

本板选择的固件组件是：

```text
COMPONENT_wlbga_iPA_sLNA_ANT0_LHL_XTAL_IN/btfw.c
```

已验证的控制器固件信息：

```text
CYW55500A1_001.002.032.0145.0000_Generic_UART_47MHz
PatchRAM payload: 126951 bytes
HCD records: 519
```

不要仅按芯片型号随意换用其他 `COMPONENT_*` 目录；dLNA/sLNA、天线路径和低功耗
时钟输入配置不匹配时，即使 HCI 能响应，也不代表射频性能和稳定性正确。

## 4. 调试前的基础链路

### 4.1 先确认 M33 串口

M33 的产品控制台是 UART5，参数为 115200 8N1；M55 UART2 是图形核诊断口，不能
拿 M55 日志代替蓝牙 Host 日志。本次使用 ST-Link 的 USB 转串口接入，主机枚举为
`COM4`；其他电脑应按设备管理器中的实际端口替换。

```powershell
.\tools\freather\serial-monitor.cmd --port COM4 --baud 115200
```

最初验证只看到了 `feather_status`、`feather_ping`，执行 `bt_status` 返回
`command not found`。这一步确认了板子、M33、串口和 MSH 正常，问题只是蓝牙功能尚未
编入 M33，而不是串口接错或 M33 未启动。

### 4.2 先隔离变量，再接完整 Host Stack

调试没有直接从完整 AIROC Host Stack 开始，而是先用最小 HCI 控制器诊断路径验证：

- 无线电源 FET、REG_ON 和自动波特率时序；
- SCB4 和 RX/TX/RTS/CTS 管脚复用；
- CTS 能在超时内拉到允许发送的状态；
- HCI Reset、厂商波特率命令、Write RAM 和 Launch RAM；
- HCD 数据格式和逐记录下载；
- Read BD_ADDR 与 Read Local Version。

该诊断实现保留在 `feathertalk_bt_controller.c`，用于板级故障隔离；最终正式构建走
`feathertalk_bt_host.c` 与 AIROC Host Stack，不把手写 HCI 状态机当作产品蓝牙栈。

## 5. 调试过程时间线

### 5.1 阶段一：控制器与 PatchRAM 单独打通

第一版 M33 蓝牙诊断程序完成了以下时序：

1. P16.3 拉高，打开载板共享无线电源轨；
2. DEVICE_WAKE 保持为不休眠状态；
3. RTS 在 REG_ON 翻转期间保持低电平，使 CYW55500 进入自动波特率模式；
4. 恢复 HCI 四线 UART 的 BSP 管脚复用；
5. 以 3 Mbit/s 建立下载链路；
6. 解析 HCD，依次发送 519 条记录；
7. Launch RAM 后先回到 115200，再用厂商命令切回 3 Mbit/s 并执行 HCI 查询；
8. 读取地址与 HCI/LMP 版本。

首次成功的关键日志为：

```text
[BT] HCI UART baud 3007518 (requested 3000000)
[BT] download CYW55500A1_001.002.032.0145.0000_Generic_UART_47MHz (126951 bytes)
[BT] HCD complete: 519 records
[BT] HCI UART baud 115207 (requested 115200)
[BT] controller ready
[BT] address=9C:C7:D3:E1:BC:53
[BT] HCI=14 rev=0x1000 LMP=14 sub=0x2220 manufacturer=9
```

实际波特率 `3007518` 和 `115207` 是 SCB 时钟分频后的可实现值，分别与请求值非常
接近，不是波特率配置错误。随后又通过 `bt_init` 重复下载并通过板级复位重测，地址、
版本、519 条记录与 126951 字节均保持一致。

这一阶段证明了硬件连接、供电、UART、流控和固件组件选择是正确的。后续 Host Stack
若仍卡住，应优先排查软件适配，而不是继续更换固件或怀疑射频器件。

### 5.2 阶段二：接入 AIROC Host Stack

完整 Host Stack 需要三类内容同时存在：

- 官方 `libbtstack.a` 和 WICED/AIROC 头文件；
- 官方 CYW55500A1 PatchRAM；
- HCI-UART 平台集成层以及 RTOS 抽象。

`SConscript` 现在会在构建前检查这些资产，缺失时列出文件并提示初始化子模块。固件
源文件先复制到忽略的 `projects/FeatherTalk_M33/build/bluetooth` 再编译，所有对象也
落在项目 `build` 树中，不会污染官方子模块或仓库源码目录。

同时新增了 RT-Thread 版 `cyabs_rtos`，覆盖线程、互斥锁、信号量、消息队列、软定时
器、系统时间、延时和内存等 AIROC 所需接口。

### 5.3 问题一：Host 长期停在 `starting`

现象：

```text
[BT] starting AIROC BLE host stack at 3 Mbaud
BT host: state=starting error=0 scan=off adv=off reports=0 unique=0
bt_scan: 45
```

当时底层控制器已经能单独启动，但 Host Stack 没有收到完整的异步 HCI 事件，所以
`BTM_ENABLED_EVT` 未到达应用。打开分层 trace 后，确认流程已进入
`wiced_bt_stack_init()`、`hci_open()`，问题位于 HCI 任务、队列和中断适配层。

调试期间分别尝试了 3 Mbit/s 和 115200 bit/s 运行波特率。最终策略固定为：

- PatchRAM 下载：3 Mbit/s；
- PatchRAM 启动后的 Feature/Runtime：115200 bit/s。

这样保留固件下载速度，同时让长期运行链路使用保守波特率。但波特率不是唯一根因，
后面的 RTOS 语义修复才真正让 Host 进入 `ready`。

### 5.4 问题二：UART ISR 没有进入 RT-Thread 中断上下文

Infineon 集成层直接注册了 SCB4 ISR。原实现调用 HAL 中断处理函数时，没有通知
RT-Thread 已进入中断上下文，ISR 内投递消息队列和唤醒线程时可能使用错误的调度语义。

修复方式是在 BT UART ISR 外层加入成对的：

```c
rt_interrupt_enter();
mtb_hal_uart_process_interrupt(...);
rt_interrupt_leave();
```

修复后 ISR/线程边界明确，随后暴露出的队列返回值错误也能稳定复现和定位。

### 5.5 问题三：RT-Thread 5 消息队列成功返回值被误判为失败

现象：

```text
hci_tx_task(): queue error (0x60e0002)
hci_rx_task(): queue error (0x60e0002), msg = 0xdadbf005
```

根因是 `rt_mq_recv()` 在当前 RT-Thread 5 实现中，成功时返回收到的字节数；Infineon
抽象层则要求成功返回 `CY_RSLT_SUCCESS`（数值 0）。适配器把正数也当作错误，于是
HCI TX/RX 任务明明收到了消息，却把它丢弃并继续等待。

修复为：只要 `rt_mq_recv()` 返回非负值，就归一化为 `CY_RSLT_SUCCESS`；负值再映射
为超时、队列空或通用错误。修复后 PatchRAM 的 HCI 数据可以连续传完，日志出现：

```text
Launch RAM successful
bt_patch_download_complete_cback(): status = 1
bt_fw_download_complete_cback(): post-reset process is Done
wiced_post_stack_init_cback(): BT sleep mode is NOT enabled
```

### 5.6 问题四：临界区与中断状态恢复不安全

Infineon 平台层用全局 IRQ 关闭保护 TX heap。移植时需要同时满足：

- 支持嵌套进入；
- 最外层退出时恢复进入前的 PRIMASK，而不是无条件开中断；
- 即使 TX heap 分配失败，也必须退出临界区；
- 在 ISR 和线程并发访问队列/heap 时不能破坏调用者原本的中断状态。

最终实现记录 `platform_irq_lock_depth` 和最外层的 `platform_irq_saved_primask`，并保证
TX heap 分配无论成功或失败都执行成对恢复。这避免了偶发“之后所有异步事件都不再
到达”的死锁型故障。

### 5.7 问题五：蓝牙栈定时器不能在 RT-Thread 软定时器小栈中执行重处理

扫描停止和 Host Stack 定时任务最初可能直接落在 RT-Thread 软定时器线程中。该线程
默认栈只有约 512 字节，而 `wiced_bt_process_timer()` 会进入蓝牙栈并触发更多 HCI
处理，不适合在这个回调上下文执行。

最终软定时器回调只完成两件事：记录触发序号，并向 HCI_RX 任务投递
`BT_IND_TO_BTS_TIMER`。真正的 `wiced_bt_process_timer()` 在 HCI_RX 任务栈中执行。
为确认 8 秒扫描定时器没有丢失，还临时加入只在线程上下文打印的 arm/dispatch 序号：

```text
BT timer arm: seq=17 delay=7998 ms
BT timer dispatch: seq=17 delay=7998 ms
[BT] scan complete: reports=19 unique=11
```

这既解决了软定时器栈风险，也给异步扫描完成提供了可核对的因果链。

### 5.8 问题六：关闭 SMP 时仍打印伪失败 `0x28`

现象：Host 已经能进入 ready、扫描和广播都正常，但启动日志仍有：

```text
host_stack_platform_smp_adapter_init(): failed, result = 0x28
```

根因不是控制器或 HCI 失败，而是 `USE_AIROC_STACK_SMP=0` 时平台包装函数仍默认返回
`WICED_ERROR`。这会误导后续调试者把健康链路判断为启动失败。

修复为：SMP 被明确禁用时返回 `WICED_SUCCESS`，并同时关闭：

```text
ENABLE_SMP_SERVER_MODULE=0
ENABLE_SMP_CLIENT_MODULE=0
ENABLE_CREATE_LOCAL_KEYS=0
```

不能只把 `USE_AIROC_STACK_SMP` 改成 1。正式启用配对前还必须实现本机身份密钥、远端
Link Key 的读取/更新/掉电保存回调，并明确 I/O 能力与配对策略。

### 5.9 阶段三：BLE 扫描与广播验证

Host Stack 稳定进入 `ready` 后，在 M33 MSH 验证：

```text
bt_status
bt_devices
bt_adv off
bt_adv on
bt_scan
```

`bt_scan` 返回的 `8100` 是 AIROC 异步请求已排队/处理中状态，代码把 SUCCESS 与
PENDING 都视为成功发起；最终是否成功必须看扫描完成回调，而不能仅按非零数值判断。

最终结果：

| 项目 | 结果 |
| --- | --- |
| Host 状态 | `ready`, `error=0` |
| 本机地址 | `9C:C7:D3:E1:BC:53` |
| 设备名 | `FeatherTalk-E84` |
| 启动自动扫描 | 18 个报告，11 个唯一设备 |
| 运行时再次扫描 | 19 个报告，11 个唯一设备 |
| 广播关闭 | 返回 0，状态变为 `adv=off` |
| 广播开启 | 返回 0，状态变为 `adv=on` |
| 设备列表 | 地址、地址类型、RSSI、广播名均可输出 |

一次中间稳定性测试还收到 29 个报告、15 个唯一设备，说明扫描链路能持续接收真实环境
中的广播，不是硬编码的模拟数据。

### 5.10 问题七：蓝牙正常后，M55 写外部 Flash 会让 M33 异常

这不是蓝牙协议问题，但它在“蓝牙持续运行 + M55 全量 UI/存储测试”中暴露，是产品
集成必须解决的问题。

M33 Non-Secure 和 M55 都从同一颗 SMIF0 NOR Flash XIP 执行；M55 又把末尾固定
2 MiB 作为 `/flash` 文件系统。M55 发出 Program/Erase 时，Flash 会暂时离开 XIP
读状态。如果 M33 此时正在执行蓝牙线程或取指，就可能读取到无效指令，表现为 M33
复位、HardFault、蓝牙突然消失，或者 `M33 XIP park failed`。

最终增加跨核 SMIF XIP 守卫：

1. M55 在共享 SRAM `0x240fffc0` 发布 Program/Erase 请求；
2. M33 高优先级服务切到内部 SRAM 代码，关闭可屏蔽中断和 fault；
3. M33 回写 `parked_seq`，之后只轮询共享 SRAM；
4. M55 收到确认后，才从 SRAM 执行实际 Flash Program/Erase；
5. Flash 确认 WIP 清零后，M55发布 release；
6. M33 在 SRAM 中失效外部 `ICACHE0`，再返回 XIP；
7. 任何握手或 ready 检查失败都 fail-closed，不允许带着未知 Flash/cache 状态继续。

最初还误用了 M33 的 C-bus alias，跨核不可写；改为两核共同使用可写的 S-bus 地址
`0x240fffc0` 后，握手才稳定。M55 D-cache 的发布/确认数据分别放在两个 32 字节 cache
line，并在请求和读取确认时显式 clean/invalidate，避免缓存一致性造成假超时。

最终 M55 全量 UI/存储回归为：

```text
326 PASS / 0 FAIL / 150 actions
```

测试实际覆盖 `/flash` 删除、剪贴板和命名等写擦操作；期间 M33 蓝牙继续保持 ready，
未再出现 XIP park 失败、HardFault 或复位。

### 5.11 问题八：频繁烧录时 OpenOCD 的 reset-halt 结果可能被误判

本板外部 SMIF 映射依赖 Infineon PSE84/KitProg3 的目标脚本。PyOCD 当前 Pack 不会
自动启用非默认 PSE84 SMIF FLM，也不能替代 KitProg3 的专用 acquire 流程，因此本轮
构建后的 M33/M55 烧录继续使用官方 Infineon OpenOCD。

调试发现 PSE84 `reset_halt` Tcl helper 返回布尔 0 时，命令行本身不一定失败，旧脚本
可能继续写入并误报成功。现在 `feathertalk_prepare_cm33.tcl` 显式检查返回值；只有受约束
的 fallback 确认以下条件后才允许写入：

- CM33 确实 halted；
- 当前为 Secure 域；
- PC 位于 Secure boot RRAM；
- 芯片不在 Test Mode；
- 写入后可以重新 halt 到 Non-Secure 应用。

最终 M33 签名、合并、写入和校验均成功。此改动让后续蓝牙迭代的“烧录成功”具有可信
含义，避免把旧固件仍在运行误认为新代码没有生效。

## 6. 最终构建、烧录与复现

在 SDK 根目录执行：

```powershell
git submodule update --init --recursive
.\tools\freather\build-demo.ps1 -Project FeatherTalk_M33 -Jobs 16
.\tools\freather\flash-demo.ps1 -Project FeatherTalk_M33 -Programmer OpenOCD -AdapterKHz 1000
.\tools\freather\serial-monitor.cmd --port COM4 --baud 115200
```

如果同时更新 M55，应先烧 M55，最后烧 M33，让 M33 的安全启动流程重新拉起 M55：

```powershell
.\tools\freather\flash-feathertalk.cmd
```

成功启动应看到：

```text
FeatherTalk M33 0.2.0-dev
SMIF XIP guard service ready at 0x240fffc0
Bluetooth: bt_status, bt_scan, bt_devices, bt_adv
[BT] starting AIROC BLE host stack (download=3000000 runtime=115200)
wiced_post_stack_init_cback(): BT sleep mode is NOT enabled
[BT] AIROC host stack ready, addr=9C:C7:D3:E1:BC:53
[BT] non-connectable advertising: 0
[BT] startup BLE scan: 8100
[BT] scan complete: reports=... unique=...
```

推荐的最小验收顺序：

```text
bt_status
bt_devices
bt_adv off
bt_status
bt_adv on
bt_scan
bt_devices
feather_status
```

验收标准：Host 为 `ready/error=0`；扫描定时结束；列表有真实地址和 RSSI；广播开关
返回 0 且状态同步变化；`feather_status` 中 M55 保持 online；执行 M55 存储写擦回归时
M33 不复位。

## 7. 问题与修复速查

| 现象 | 根因 | 修复/判断方法 |
| --- | --- | --- |
| `bt_status: command not found` | 蓝牙模块没有编入 M33 | 检查 M33 `applications/bluetooth/SConscript` 与 `main.c` |
| 固件缺失或开发机能编、其他电脑不能编 | 使用了仓库外绝对路径 | 初始化两个官方子模块；构建只读取仓库内路径 |
| CTS 一直高或 HCI 无响应 | 电源、REG_ON/RTS 自动波特率或四线 UART 配置错误 | 先跑最小控制器诊断，检查 P16.3、P11.0、P10.0~P10.3 |
| 控制器 ready，但 Host 一直 starting | HCI 异步任务没有正常消费事件 | 打开分层 trace，检查 ISR、队列与 Host 回调 |
| `queue error (0x60e0002)` | `rt_mq_recv` 成功字节数被当作错误 | 非负返回值统一映射为 `CY_RSLT_SUCCESS` |
| 偶发后续事件全部停止 | IRQ 临界区嵌套/失败路径未恢复 | 保存 PRIMASK、记录嵌套深度、所有路径成对恢复 |
| 扫描到时不结束或软定时器不稳定 | 在小栈软定时器中执行蓝牙栈重处理 | 回调只投递消息，HCI_RX 线程调用 `wiced_bt_process_timer` |
| `SMP adapter ... 0x28` | 禁用 SMP 时包装函数仍返回通用错误 | 禁用配置返回成功，同时关闭 SMP 相关模块 |
| `bt_scan: 8100` | AIROC 异步 pending，不是扫描失败 | 等待 scan complete 回调并检查设备计数 |
| M55 写 `/flash` 时 M33/蓝牙复位 | 两核共享 SMIF XIP，写擦破坏 M33 取指 | 使用共享 SRAM XIP 守卫，M33 停在 SRAM 并失效 ICACHE0 |
| OpenOCD 看似成功但新代码未运行 | `reset_halt` 返回 0 未被当成命令失败 | 使用受约束的 CM33 prepare/restart Tcl，写后强制校验 |

## 8. 代码与诊断入口

| 文件 | 作用 |
| --- | --- |
| `feathertalk_bt_host.c` | 最终 AIROC Host 启动、状态、扫描、设备列表、广播和 MSH |
| `feathertalk_bt_controller.c` | 第一阶段的最小 HCI/PatchRAM 板级诊断参考，不是最终 Host 通路 |
| `rtos/feathertalk_cyabs_rtos.c` | AIROC 到 RT-Thread 的线程/IPC/定时器适配 |
| `vendor/infineon/hci_uart` | 仓库内可审计的 HCI-UART 平台层及本地 RT-Thread 修复 |
| `SConscript` | 固定依赖、固件组件、编译宏、波特率和链接配置 |
| `../services/feathertalk_smif_guard.c` | M33 驻 SRAM 的跨核 XIP 停驻服务 |
| `../../../FeatherTalk_M55/applications/services/feathertalk_smif_guard.c` | M55 Flash 写擦前后的守卫请求/释放 |
| `../../../../tools/freather/openocd/feathertalk_prepare_cm33.tcl` | 受约束的 PSE84 CM33 reset-halt 与烧录准备 |

本地硬件日志保存在 `tools/freather/logs`，没有纳入 Git。关键过程日志包括：

```text
m33-bt-first-board-validation.log
m33-bt-host-scan-validation.log
m33-bt-host-isr-fix-fullboot.log
m33-bt-host-queue-fix-fullboot.log
m33-bt-3m-stability.log
smif-guard-sbus2-m33.log
bluetooth-smp-clean-m33.log
bluetooth-command-validation-m33.log
bluetooth-runtime-actions-m33.log
bluetooth-rescan2-m33.log
```

## 9. 后续开发顺序

建议不要立即把 UI 开关等同于“蓝牙全功能完成”，而按以下顺序推进：

1. 定义 M33→M55 蓝牙 IPC 数据模型：电源状态、Host 状态、扫描结果和连接状态；
2. 增加基础 GATT Client/Server 验证，先完成连接、服务发现、读写和通知；
3. 设计配对 I/O 策略并启用 SMP；
4. 把身份密钥、Bond/Link Key 放进明确的掉电保存分区；
5. 增加断连、控制器异常、PatchRAM 下载失败和重复初始化的恢复策略；
6. 再打开 HOST_WAKE/DEVICE_WAKE 与低功耗休眠；
7. 最后进行长时间连接、并发 UI/存储、射频吞吐和功耗回归。

在完成第 3、4 步前，当前系统只能宣称 BLE 扫描和非连接广播可用，不能宣称安全配对、
Bond 或加密业务已经完成。
