# Wi-Fi / 蓝牙运行时资源管理

日期：2026-09-04。适用当前 FeatherTalk 双核产品（M55 WHD、M33 BTstack）。

## 目标与分层

Wi-Fi 和蓝牙是独立功能。编译宏只决定是否包含某个功能，不承担另一功能的电源或启动控制。

| 资源/动作 | 所有者 | 规则 |
| --- | --- | --- |
| 底板 P7.2、无线共用电源 P16.3 | 板级 Radio Manager | M33 释放 M55 前完成上电，与是否启用蓝牙无关；普通开关/失败不切断 |
| WLAN P11.6 / SDIO0 / Wi-Fi 固件、CLM、NVRAM | M55 Wi-Fi | 只复位 WLAN；WHD 平台适配通过管理器申请与复位 |
| BT P11.0 / SCB4 UART / HCD PatchRAM | M33 蓝牙 | UART/RTS 时序由蓝牙驱动处理；申请、复位、释放由管理器执行 |
| Wi-Fi 逻辑开关 | Wi-Fi worker | WLC_UP / WLC_DOWN；关闭后保留固件与电源占用，状态为 QUIESCED |
| 蓝牙控制器关闭 | M33 BTstack owner 线程 | 先停止音频/定时器，HCI OFF、UART/IRQ 回收，再 release BT_REG_ON；不影响 WLAN |
| 整机关机 | board poweroff | 唯一可终止两域并关闭公共供电的入口，不作为无线错误恢复手段 |

管理器不处理连接密码、网络扫描、配对、音频或驱动数据包；这些仍留在各自服务内。
Wi-Fi 的启动不等待蓝牙 READY，蓝牙也不等待 WHD 下载/初始化完成。
BT 当前有等待 M55 IPC 上线的有界等待，用于事件传递，不是等待 Wi-Fi。

## 运行时状态与跨核一致性

- 公共实现：`radio/radio_manager.c`，接口：`include/feathertalk/radio_manager.h`。
- 双核分别编译同一份仓库内源码，对象文件留在各自 `build/radio`。
- 共享区固定为 `0x240FFF40..0x240FFFBF`，128 B；双核链接器明确预留并校验地址，不占音频环形缓冲或 SMIF XIP guard。
- 每个功能有独立的 owner、claimed、state、resets、error、sequence；claim 是幂等的单所有者占用，不是可无限叠加的引用计数。
- `OFF → POWERED → RESETTING → POWERED → READY`；逻辑暂停为 `QUIESCED`，失败为 `ERROR`。另一项功能不被连带改状态。
- owner 在板级硬件描述中指定，通过运行时调用检查；错误核发起 acquire/reset/release/set_state 会被拒绝，且在写 GPIO 之前返回。
- P11.0 / P11.6 同属 GPIO P11，复用/驱动模式只在单核板级启动阶段初始化；运行时使用 PDL 的 OUT_SET/OUT_CLR 原子位写，不再跨核执行整端口配置寄存器的读改写。
- 公共头和每个功能槽分属独立的 32 B cache line；每槽单写者，短本核临界区 + sequence 快照 + M55 cache clean/invalidate。不在跨核锁内等待 GPIO 延时。
- WHD 通过平台运行时接口查询蓝牙归属，不再通过产品宏屏蔽蓝牙，也不会启动/重新加载 M33 的蓝牙 Host。
- 原 M33 `INIT_BOARD` 在释放 M55 之后再次拉低共享电源的路径已删除；M55 LCD/codec 初始化也不再写公共电源或无线复位脚。

当前电源策略是 **retain supply**：即使两个功能都不用，也保留公共电源，只有整机关机关闭。
这是明确的运行时电源策略，不把一个驱动的 stop 当作整模组掉电。将来做低功耗需先验证两域时钟、RAM 保留、唤醒和固件重载，再扩展同一个管理器。

## 验证入口

本轮并发启动回归还暴露了原 IPC 驱动的独立问题：`Cy_IPC_Sema_Clear()` 可能因全局硬件仲裁忙而失败，旧代码忽略返回值，导致额外的软件信号量 48 保持占用。现场 M33 的 `stats_sema_fail=84329`、发送错误 836 次，M55 停在 tx=8/rx=30，但蓝牙 HCI 应答仍持续增长、无硬件错误。相关现场保留在 `tmp/radio-m33-ipc-stall.log` 和 `tmp/radio-ipc-failure-boot.log`。

处理：IPC Send 使用 PDL 原有目的通道原子锁，并用短本核临界区保护端点 busy/callback；删除多余全局软件信号量，不采用强清锁或重启蓝牙的绕行处理。发送 clean / 接收 invalidate 完整帧，避免 PDL 仅处理指针大小的头部时跨 cache line 的 payload 陈旧。回归同时检查 HCI 和双向 IPC 计数，不能只观察一条链路。

M55 控制台，不需要重新接 M33 串口：

```text
ft_radio
ft_radio_test
ft_wifi off
ft_wifi on
ft_wifi scan
bt_status
```

`ft_radio_test` 只测试越权和非法参数拒绝，12 项断言，不实际修改 GPIO。
`bt_status` 显示 IPC 上报的 enabled，不再把这个标志冒充实时 HCI 探测。
M33 的 `g_bt_coex_diag` 累计现有 HCI Command Complete、成功广播使能应答、启动次数、硬件错误；不增加探测命令或周期性 IPC 流量。

双核烧录相应固件后，以官方 OpenOCD **不复位**连接目标，再执行：

```powershell
tools/freather/serial-monitor/python/python.exe tools/freather/test-wifi-bluetooth-isolation.py --serial COM17
```

脚本通过 OpenOCD Tcl / Debug AP 在 M33 运行时读计数，反复开关/扫描 WLAN，检查蓝牙应答继续增长、复位次数不变、无 UART timeout/硬件错误；不暂停核心、不连接 AP、不配对、不记录 SSID/密码。退出时尝试恢复 Wi-Fi 开启。
默认要求蓝牙没有已建立的连接，因为应答来自现有的空闲广播保活；连接态应另测真实 GATT/A2DP 数据，不套用这个判据。

## 不能据此宣称的结果

- GPIO 和启动隔离不等于已经验证射频共存性能。Wi-Fi 2.4 GHz 与 Bluetooth 同时传输的吞吐、丢包、音频连续性需要连接态压力测试。
- Wi-Fi 认证/DHCP/吞吐仍等待实际国家地区与测试网络配置；当前沿用 demo 国家码 AU，只做被动扫描。
- 当前 BTstack 后端已补齐 IPC stop/start，空闲态反复启停的验收见下文；这不替代 Wi-Fi 已连接、BLE/GATT/A2DP 活动连接时的双向启停验收。
- 本轮不宣称非协调 M33 单核复位/任意硬件故障可透明恢复，也不宣称 AIROC 备用后端已完成本轮共存回归。
- 管理器不会在某个功能失败时自动 reset 整机或切共享电源。SDIO 枚举不成功时既有 WHD 等待线程仍可能留在等待状态，后续需补充独立的超时/重试策略。

## 2026-09-04 资源隔离阶段双核实板结果（历史基线）

M33、M55 均重新构建，Infineon Customized OpenOCD 写入并校验成功。只更新固件分区，未格式化或擦除末尾 2 MiB 用户盘。

| 检查 | 结果 |
| --- | --- |
| 启动顺序 | WHD ready 后、BT HCD 下载期间执行 `ft_wifi off`；Wi-Fi 成功关闭，蓝牙仍于 16059 ms 报事件 40、16110 ms 报事件 41，进入广播 |
| 运行时开关/扫描 | 3 轮 off → 关闭时拒绝 scan → on → 被动 scan，136 项主机断言通过 |
| 越权保护 | `ft_radio_test` 内部 12 项断言通过，未写 GPIO |
| 蓝牙真实应答 | HCI Command Complete 35 → 51；成功广播使能应答 7 → 23 |
| 独立复位与电源 | BT 启动次数始终 1；BT/WLAN 各自复位次数均始终 1；公共电源始终开启 |
| 控制器错误 | BT hardware error=0，UART TX timeout=0 |
| 双向状态更新 | M33 IPC tx 111 → 255、rx 32 → 80；M55 接收的 BT 状态 age 均小于 3000 ms |
| IPC 错误口径 | M33 错误累计在测试开始前已为 2，结束仍为 2；本轮无新增错误，不等于启动以来错误为零。已有两次错误的具体原因未单独归因 |
| 测试侵入性 | 不暂停 M33/M55、不连接 AP、不配对；通过现有 HCI 流量验证活性，未用重置或新增探测掩盖问题 |
| UI 回归 | Wi-Fi 页五轮进入/退出、真实 AP 表单、密码掩码、键盘收起/展开及返回清空，共 35 项通过；结束回到 Home，depth=1 |
| 用户存储 | `/flash/Pictures/02.jpg` 正常加载，480×800、checksum=0x8f45aa87；偏好 generation=26、dirty=0、写入次数=0，均与更新前一致 |

最终固件 SHA-256：

```text
M55 projects/FeatherTalk_M55/rtthread.hex
ADC0ABFBAB187B40B965AD468A53AF10C54786D1E098D85BBFCFF1F0ED4E072E
M33 projects/FeatherTalk_M33/build/rtthread.hex
36A4C17FD1416F35947D4A185817E4299261B481708812B02E5A4FC23B8B7A06
```

本机原始证据：`tmp/radio-final-{m55-build,m33-build,flash,boot-test}.log`、
`tmp/radio-final-cycles.jsonl`、`tmp/radio-final-ui.log`；`tmp` 是忽略的临时产物，不是版本库交付依赖。
可复用回归脚本位于 `tools/freather/test-wifi-bluetooth-isolation.py`，源码、管理策略、结果摘要和固件哈希均保存在仓库目录内。

下一阶段按以下顺序推进，不能把本轮空闲态隔离测试当作全部无线功能验收：

1. 确认真实国家地区和授权测试网络，验证认证、DHCP、断线与恢复；仍只控制 WLAN 自身。
2. BT Host 的 stop/start 资源回收已在下一节补齐；继续验证 Wi-Fi 已连接时蓝牙启停，以及 BLE/GATT/A2DP 活动连接的关闭/恢复。
3. 同时进行 Wi-Fi 数据传输和 BLE/GATT 或蓝牙音频，测吞吐、延迟、丢包与音频连续性。
4. 独立故障注入、SDIO 初始化超时及单域恢复；验证后再引入共享电源低功耗策略。

## 2026-09-04 双向独立开关生命周期

本节接续上面的资源隔离基线，当前产品后端为 BTstack；备用 AIROC 后端仍明确返回
`ENOSYS` 拒绝关闭，不能以它的编译兼容接口冒充已验证功能。

### 请求、所有权与完成语义

- `bt_service_set_enabled(0/1)` 接受异步目标，返回成功不等于硬件已完成。使用
  `busy/enabled/error` 查询；状态为 OFF → STARTING → READY → STOPPING → OFF，失败为 ERROR。
- M33 只有一个常驻 `btloop` 拥有 HCD、H4、UART、HCI、定时器和 profile；普通启停不删除/新建
  线程，不重复注册 SDP/ATT/A2DP 或初始化内存池。协议栈只初始化一次，重启走 HCI OFF/ON。
- 同目标请求幂等；HCD 下载中请求 OFF 可在命令/记录边界取消。目标发布和请求清理使用短 IRQ
  临界区，避免 IPC 抢占后新请求被旧操作的收尾误清掉。HCD/HCI 等待不在临界区里执行。
- HCI 启动有 15 秒超时；停止等待 HCI OFF 最多 5 秒。失败只清理 BT 域并记录错误，不复位
  Wi-Fi、不切共享电源、不无限自动重试；部分 profile 初始化失败或 HCI 无法停妥时拒绝危险重入。
- 仓库内新增 `bt_uart_block_rtthread.c` 替换通用 embedded UART adapter（原 close 留为 TODO）：
  关闭时移除 run-loop data source，停止异步 RX、IRQ 和 UART，清除完成标志；重开重新绑定回调。
  ISR 只通知，H4 回调仍由 owner 线程处理。原厂 HCD 和运行 3 Mbps 配置保留。
- 关闭时终止 BLE 通知/广告保活，清 BLE 与 Classic 连接标志，并发布音频格式为零，交由 M55
  原音频消费者停止解码/释放输出设备。拒绝关闭阶段迟到的 STREAM_STARTED/媒体数据；停止完成
  后再次清理残留 profile 状态。此路径已实现，但连接中音频效果仍须单独验收。
- `bt_inq` 和诊断 A2DP connect 改由 owner 回调邮箱执行；原始 HCI Reset 禁止绕过 H4 注入。
- Wi-Fi 的 WLC_UP/DOWN 对重复目标跳过冗余命令；关闭后清除陈旧关联、SSID/IP 和信号，完成后
  才发布非 busy 状态。Wi-Fi worker 不受 BT busy 阻塞，反之亦然。

### IPC 与界面

- QUICK_RESULT 增加 PENDING 枚举，报文大小与布局不变。M33 周期上报真实处理状态，完成后
  为 OK，失败为 FAILED；enabled 仅表示 READY，connected 覆盖 BLE 或 Classic。
- 设置 → 蓝牙增加随页面释放的 200 ms 状态监视，文字和开关从真实 IPC 回读更新，不再停在
  页面创建时的快照。处理期间只禁用本项操作并显示“处理中”，另一无线功能仍可操作。
- 状态栏、通知面板和设置使用相同服务数据，不用 UI 猜测成功，也不以宏控制另一功能。

### 可重复验证

先烧录匹配的双核镜像，再以官方 OpenOCD 不复位连接（Tcl 6666），执行：

```powershell
tools/freather/serial-monitor/python/python.exe tools/freather/test-radio-lifecycle.py --serial COM17
```

脚本要求蓝牙空闲且两功能已开启，默认三轮：BT off 时 Wi-Fi scan、两者 off、Wi-Fi off 时
BT on、重复 on/off、两者 on 后 scan；再覆盖 HCD 中取消/重启、HCD 中 Wi-Fi scan 和快速
交替请求。读取运行中的 M33 SRAM，检查 owner 指针、一次初始化、独立复位次数、UART 波特率、
真实 HCI 应答和双向 IPC。最后进入蓝牙设置，从匹配 M55 ELF 的调试类型动态解析 LVGL 字段偏移，
读实际 label/按钮状态，检查 ON/OFF/PENDING/READY 自动刷新及退出页面的对象释放；不是只读 IPC 模型。
不会连接 AP、配对、暂停核心或记录 SSID/密码，结束尝试恢复两者开启。
临时日志在 `tmp`，可复用脚本与本摘要在仓库内；不能把只存在 `tmp` 的证据当作构建输入。

### 最终实板结果（双向生命周期版本）

基于已同步的 `a689fba0` 和本地无线管理改动，两核重新构建、签名/烧录并校验通过。
当前固件含上述 owner、原子请求、UART 回收及音频迟到事件处理，不是首轮 142 项时的旧镜像。

| 验证 | 最终结果 |
| --- | --- |
| 主机生命周期回归 | `test-radio-lifecycle.py`：201 项通过、0 失败，三轮交叉开关 + HCD 取消恢复 + HCD 中扫描 + 16 次快速交替请求 + 蓝牙设置页状态/对象回收 |
| M55 Wi-Fi 页面回归 | `feather_wifi_ui_test`：35 项通过、0 失败，扫描结果 5 个；未点击连接进行认证 |
| 资源权限回归 | `ft_radio_test`：12 项通过、0 失败，无 GPIO 写入 |
| UART 与状态 | 每次 OFF 均验证 UART baud=0、管理器 BT=OFF；每次 READY 均为 3000000 baud、BT=READY，IPC result=OK |
| 线程/初始化 | worker 指针全程相同；`stack_inits` 始终为 1，关闭/重启不重复注册协议栈 |
| 独立域 | WLAN reset 始终为 1；收尾 BT starts/resets=13、stops=12，只跟随实际蓝牙启停；共享电源始终开启 |
| 活性与错误 | 收尾读取 HCI Command Complete=296、广告应答=72（基线 39/11）；hardware error=0、UART timeout=0、BT service error=0 |
| IPC 口径 | 收尾 M33 tx/rx=1220/393（基线 147/44）；错误累计 2→2，没有新增。M55 控制台 err=0，不把 M33 启动前两次错误隐去 |
| 重复 WLAN 请求 | 收尾重复 off/on 均成功，关闭时 scan 返回 -16；关联/IP 保持空，未把缓存 AP 列表误称成当前连接 |
| 状态页 | 实际 LVGL label 和按钮通过 ON → OFF → PENDING → READY 检查；PENDING 禁用本项，完成自动恢复，不需重进；回 Home 后对象指针清零 |
| 用户数据 | 壁纸仍为 `/flash/Pictures/02.jpg`、480×800、checksum=0x8f45aa87；偏好 generation=26、dirty=0、writes=0/0。末尾 2 MiB 用户盘未擦除 |
| 结束状态 | Home、route depth=1；Wi-Fi 和蓝牙均开启、均未连接新设备/AP |

M33 非安全镜像 text/data/bss = 416156 / 1488 / 261173 B；M55 = 5671168 / 91672 / 5075484 B。
构建仍有既存的 RWX LOAD 段警告，以及 `ft_gatt.h` 序列号常量被截为单字节的警告；本轮不将其
称为零警告构建，该 GATT 字段修正应在后续连接态功能回归中处理。
M55 片内堆峰值 1356232 / 1356248 B，仍依赖多堆向 HyperRAM 回退；不能据此宣称片内堆有充裕余量。

```text
M33 projects/FeatherTalk_M33/build/rtthread.hex
8585D6215E82CB0387587DA8AF10E44DD3397F4E6EDBB50C0BAFEF06B5E2FC3D
M55 projects/FeatherTalk_M55/rtthread.hex
F7379B03E230BD46EE0412C398BCC2C05E43EE6A4BC525FC624137D215506283
```

可复核日志：`tmp/radio-lifecycle-final.jsonl`、`tmp/radio-lifecycle-final-{m33-build,m55-build,flash,boot,ui,end}.log`。
JSON 回归不记录 SSID；额外串口收尾日志会显示扫描到的网络名，仅作为本机忽略的临时证据。
两张显存回读为 `tmp/radio-lifecycle-bt-{0,1}.png`：中文标题/状态/按钮可见且无占位符，但壁纸区域
仍可见不同位置的细横线，尚未归因于运行中回读或实际绘制，本轮**不以此宣称整条显示路径验收通过**。
这与本节已验证的开关/页面状态更新分开跟踪，不通过改电源或重启无线掩盖。

下一步仍为：真实地区/授权 AP 的认证和 DHCP、连接态 GATT/A2DP 交叉启停与并发流量、故障注入。
蓝牙重开会重新下载 HCD，需等待约十余秒的真实 STARTING 过程；不是 UI 立即切图标就代表已经工作。
