# FeatherTalk Wi-Fi 首轮移植

更新日期：2026-09-04。本文区分代码完成、构建通过和实板结果；未验证项不视为通过。

后续共享电源/复位已改为运行时 Radio Manager，见
`../../FeatherTalk_Common/RADIO_COEXISTENCE_zh.md`。下方首轮固件哈希和 READY 标志测试属于历史验收，不能代替该管理器的实板 HCI 应答回归。

**当前状态（2026-09-04）**：用户明确指定改回 AU，M55 `.config` / `rtconfig.h` 已恢复 AU，
继续进行 Wakaka 双频连接功能测试。下方 CN 初始化失败保留为诊断历史；本次显式选择 AU
不是驱动自动回退，也不代表本板取得中国大陆地区合规验收。实板连接结果见文末。

## 架构与边界

- 复用 SDK Edgi_Talk_M55_WIFI 的 WHD 4.1.1、CYW55500A1 资源、SDIO0、RT-Thread WLAN 和 lwIP 2.1.2。
- M55 负责 Wi-Fi 和网络，M33 保留既有蓝牙。Wi-Fi 本地状态合入快捷状态/系统网络状态，不改 IPC 帧 ABI。
- 固件使用 WHD_RESOURCES_IN_MEMORY；生成器解析真实路径，排除旧的弱符号 incbin 资源，避免重复编译和错误相对路径。
- 不写 FAL 无线分区，不修改外部 Flash 末尾 2 MiB 的 filesystem 分区；用户数据和壁纸保留。
- 复用同仓库 demo/packages 内的 FreeRTOS_Wrapper、netutils。FreeRTOS_Wrapper 只是 API 适配，实际内核仍为 RT-Thread；没有编译仓库外代码。
- 蓝牙 off 只拉低 BT_REG_ON，不能切断 P16.3 无线共用电源。Wi-Fi off 先断开，再 WLC_DOWN；on 为 WLC_UP。首轮保留共用电源，不宣称深度断电省电。
- lwIP 静态网络池 153856 字节放入链接器预留的 M55 HyperRAM 0x64200000 区域，与 0x64400000 起、当前配置 8 MiB 的动态堆隔离（该区域映射上限为 12 MiB）；memp_init/mem_init 初始化内存。SDIO 保留 DMA 对齐、bounce buffer 和 cache 维护。RT-Thread 启用了多堆自动绑定，片内堆不足时普通分配可回退到 HyperRAM，不能只看片内堆剩余量判断全部内存耗尽。

## 后台服务与 UI

- feathertalk_wifi.c：单个请求队列/工作线程，扫描和连接不会在 LVGL 线程执行；状态快照由短临界区互斥锁保护。
- 驱动初始化成功才报告 available；等待、关闭、空闲、扫描、连接、等待 IP、连接成功、错误分开显示。
- 首轮只做 STA；无自动扫描、自动连接或密码落盘。每次上电默认初始化 WLAN；连接由用户触发。
- 被动扫描一次，最多展示 32 个 BSSID；保留原始 SSID，不翻译网络名。当前按用户明确要求使用
  AU；初始化失败必须显式报告，驱动不自动切换国家码掩盖资源不匹配。CN 资源仍未补齐。
- 修正 SDK join 将加密类型写死 WPA2 的问题，使用 WLAN 请求携带的 security。
- 设置 → Wi-Fi：总开关、扫描、网络列表、密码输入、连接、断开、SSID/IP/网关/MAC/RSSI。
- 密码默认掩码显示，取消/Back 清除；键盘是页面内覆盖表单的一部分，有收起和重新展开动作。
- 列表仅在一轮扫描完成后更新，不在每次状态刷新时销毁/重建正在触摸的行。状态栏和下拉开关使用同一服务。
- 导航契约见 ui/UI_NAVIGATION_MAP_zh.md。

## 构建和操作

在仓库根目录：

    tools\freather\build-demo.cmd FeatherTalk_M55
    tools\freather\build-demo.cmd FeatherTalk_M33
    tools\freather\flash-feathertalk.cmd

M55 串口：

    ft_wifi
    ft_wifi scan
    ft_wifi on
    ft_wifi off
    ft_wifi disconnect
    feather_ui_scene 11
    feather_wifi_ui_test

连接优先通过板上密码框输入，不将密码写入调试日志或仓库。MSH 仍可使用 SDK wifi 命令，但不要与产品后台操作同时操作同一个 WLAN。

## 本轮遇到的问题

1. M55 原工程未导出 SCons env，WHD 资源生成器无法使用；补充 Export。
2. SDK 同时存在生成资源和旧 incbin 资源；统一编译生成资源，使用仓库实际资源路径。
3. demo 的网络依赖包括 FreeRTOS API wrapper、系统工作队列、POSIX poll/select、iperf 栈配置；产品原先关闭了这些配置，已补齐。
4. 完整链接显示 DTCM 溢出 123700 字节；不是固件 Flash 不够，而是网络池与 UI 静态数据争用 DTCM。网络池迁往单独的 HyperRAM 段，大小由链接器计算。
5. WHD 4.1.1 没有新版 whd_wifi_up/down API；使用该版本已有的 WLC_UP/DOWN ioctl，不移植新版 API 名称。
6. 首次双核烧录后 WHD 启动成功，但约十秒后无串口响应。GDB 停在 `_scheduler_stack_check`，`sdio_irq` 的 512 字节栈首哨兵已被覆盖，回溯为 SDIO 卡中断唤醒线程。产品 `RT_SDIO_STACK_SIZE` 调整为 4096，并加编译期下限检查；保留 RT-Thread 栈溢出保护。
7. `wlan0` 是 WLAN 设备而不是网络接口名；SDK lwIP 注册的是 `w0`。IP/网关从 WLAN 设备绑定的 `netdev` 获取，避免写死名称导致连接成功却显示空 IP。
8. 包版本标注 WHD 4.1.1；实板运行横幅为 `4.1.0.24264 / 11ax v4.1.0`，无线固件为 `28.10.301 / FWID 01-8cf45cc8`。保留原 SDK 配套资源，不混用外部新版固件。
9. 产品后台消息接收最初按旧接口检查 `rt_mq_recv()==RT_EOK`，而本 SDK RT-Thread 5 返回消息字节数；请求已取出却未执行，表现为一直“扫描中”。按完整消息长度判断成功后修正。SDK FreeRTOS Wrapper 本身已有对应版本兼容处理，无需修改。

## 验证进度

- [x] 完成服务、设置页和状态栏对接代码。
- [x] M33 构建及签名通过。
- [x] M55 构建、双核烧录和复位启动（未远程执行物理断电冷启动）。
- [x] SDIO 枚举、固件启动、2.4/5 GHz 被动扫描。
- [x] Wi-Fi 三轮关闭/开启，蓝牙 Host 始终保持 READY；关闭时拒绝扫描。
- [x] 反向关闭蓝牙后验证 Wi-Fi 被动扫描：后续已补齐 M33 BTstack stop/start 并完成首轮三循环 142 项检查；最终扩展回归见 Common 的 RADIO_COEXISTENCE_zh.md。
- [x] UI 页面/密码表单/键盘/返回路径的板上自动测试，2 × 35 项，无失败。
- [x] 按用户指定 AU 完成 Wakaka 双频 WPA2 认证、DHCP、网关 ping、连接态扫描和 off/on 后重连。
- [x] Wakaka_5G 上 DNS/外网 ping、限时 TCP 收包 + UI 绘制 + 空闲蓝牙实时 HCI 检查。
- [ ] CN 配套资源及对应地区验证；WPA3、双向吞吐上限、连接态蓝牙音频并发验收。
- [ ] UI + 音频 + Wi-Fi + 蓝牙 + SD 并发压力测试，断线重连和持久化。

## 2026-09-04 初始移植实板验收记录（历史基线）

- KitProg3 / COM17，M55 和签名 M33 均已更新；最终 M55 HEX SHA-256：
  `BC259DECD502D5171CA85475F7263AAEA910EDB118EA9AB26E3DBF2E95BC2D8E`。
  M33 HEX SHA-256：`A88AE1BFDF0A5005E9A7C328BB57EEACAF645325929369295953FFD990695EB0`。
- 最终 M55 编译：text=5666200、data=91664、bss=5075256 字节。
- 板上脚本执行三轮扫描/关闭/开启，加一轮结束扫描，21 项断言通过。每轮发现 4～6 个 BSSID，包含 2.4 GHz 和 5 GHz，未连接任何 AP，也未记录网络密码。
- `feather_wifi_ui_test` 每次执行 5 轮页面创建/销毁，选择真实扫描结果、密码掩码、键盘收起/展开、几何边界和 Back 清空密码，35 项断言；本轮执行两次均为 failures=0。它不覆盖真实手指触摸，也不执行连接按钮的网络认证。
- 扫描同时执行 `feather_ui_bench`：60/60 帧，1763 ms，34.03 FPS，60 batch / 60 submit，GPU busy=48.41%；collect=10491 μs、encode=16884 μs、finish=606 μs / 帧。扫描 result=0，UI overflow=0，scanout timeout=0。这不是联网吞吐或音频并发压力成绩。
- 实测栈峰值：sdio_irq 4096 B 的 12%，WHD 5120 B 的 23%，ftwifi 6144 B 的 21%。WHD 的配置优先级经过 FreeRTOS Wrapper 反向映射，`list thread` 实际 RT-Thread 优先级为 25；后续吞吐/实时音频并发阶段需重新评估，不能把配置的 6 当成 RT-Thread 实际优先级。
- 内存峰值：片内堆 1355816 / 1356248 B；HyperRAM 动态堆峰值 28472 / 8388608 B。片内堆余量很低，多堆回退已发生；下一阶段结合真实网络吞吐检查分配延迟与实时线程优先级。
- 末尾 2 MiB Flash 和 SD 均正常挂载；偏好仍为 generation=26，壁纸 `/flash/Pictures/02.jpg` checksum=0x8f45aa87，与移植前一致。
- 双 framebuffer 回读已检查 Wi-Fi 页中英文、信号/信道、禁用状态和边界。顺带修正主机 `framebuffer-rgb565-to-png.ps1` 的 Byte 左移截断：须先转 int，否则回读图会假性偏蓝；未因此改动板上配色。
- 本地证据（不含密码，临时文件不作为固件输入）：`tmp/wifi-verified.log`、`tmp/wifi-scan-bench.log`、`tmp/wifi-final-status.log`、`tmp/wifi-page-0.png` / `wifi-page-1.png`。

## 下一步

1. AU 下双频 WPA2 基础联网已通过；补齐匹配本板的 CN CLM，并单独验收 WPA3。
2. 进行两频段双向 TCP 吞吐与 SDIO/线程/缓存分析，当前短时收包结果不能当成带宽上限。
3. 网络掉线、扫描超时/取消、异常固件恢复与凭据安全持久化；检查与音频/USB/SD/GPU 的并发。
4. 当前按用户指定 AU 运行，结束停留设置 → Wi-Fi，连接 `Wakaka_5G`；没有新增自动重连或密码落盘。

## 2026-09-04 Wakaka 双频测试：CN 地区资源阻塞

用户授权连接的目标为 `Wakaka` 与 `Wakaka_5G`；密码不写入本文、脚本或日志。
在旧 AU 配置下只做被动扫描，分别发现信道 6（2.4 GHz）与信道 48（5 GHz），
扫描安全类型均为 `0x400004`（WPA2 AES）。扫描可见不等于已认证或已获取 IP。

确认地区后同步修改 M55 `.config` / `rtconfig.h` 为 CN，构建并烧录；M33 镜像未修改。
完整启动日志显示 `Could not set Country code`，随后 WHD 初始化退出。尚未向板子发送 AP 密码。

本轮补充：

- WHD 启动失败保留原始返回码，并只读查询 `WLC_GET_COUNTRY_LIST`；不篡改 CLM、不换地区兜底。
- 后台服务收到明确初始化失败后进入 `FT_WIFI_ERROR`，设置页显示错误码、禁用开关和扫描；
  修复此前永远显示“正在初始化”的状态管理问题。
- `tools/freather/test-wifi-ap.py` 准备了多轮授权 SSID/信道 → 连接 → DHCP → 网关 4 次 ping →
  蓝牙实时 HCI/复位计数的回归流程。密码从隐藏输入或 stdin 读取，完整串口行脱敏后才写日志；
  拒绝国家码不符、无线未初始化的测试，只连接命令行显式指定的目标。脚本不修改主机网卡、磁盘或配对信息。
  **该脚本尚未完成实板 AP 流程验收，不能把准备脚本等同于测试通过。**

资源核对：当前 `resources/55500A1.clm_blob` 为 1008 字节，标识 `IFX.5551x / v2 24/06/28`，
SHA-256 `168143AE18571B7DA101BB4A52D8FD25F56275F49EA6B6E8A31A703887E6E0F4`。
配套 Wi-Fi 固件 SHA-256 为 `EFDD804346CBC8DCE8B50D467F13B114E79C2B9B4CC5D5452729662F0C6CE600`，
板级 NVRAM `cyw55513modpse84som_rev3.txt` SHA-256 为
`919B681D80AFCC939CB6B71B7D31F3FEF5A67D81F5B71AC01938B28DCB34F07B`。

检索 `CYW5551x` 中 SDK 同版本、STM32 参考和 Infineon Linux 的 CYW55500 CLM，未找到可确认适用于
本板的 CN 版本。仅作资源筛查的 ASCII `CN` 缺失不能代替固件运行时国家列表；不同厂商模组的 CLM
也不能因芯片号一致就视为本板已验证资源。[Infineon CLM 手册](https://community.infineon.com/gfawx74859/attachments/gfawx74859/WifiMCU/1191/1/Infineon-AN225347_-_Infineon_Wi-Fi_CLM_regulatory_manual-ApplicationNotes-v03_00-EN.pdf)
说明 CLM 按产品/客户及芯片、频段等条件定义。

需要板厂或 Infineon 提供匹配 PSE84 SOM rev3 / CYW55513、支持 CN 的 CLM，确认与现有 firmware/NVRAM
配套关系。取得后再执行两个 AP 的认证、DHCP、ping、重复切换及吞吐验证；不得用其他模组配置冒充通过。

### 诊断固件实板结果

```text
Could not set Country code
Unable to start WiFi: result=33556434
[wifi] requested country=CN
[wifi] country-list result=0 count=2: AU US
[wifi] initialization failed result=33556434; radio unavailable
```

`33556434 = 0x020007D2 = WHD_WLAN_BADARG`；查询由无线固件成功返回，不只是主机解析字符串的推测。
当前 CLM 不接受 CN，因此本轮**没有执行两个 AP 的密码认证、DHCP、ping 或吞吐，不宣称连接成功**。

- 实际 UI 对象回读：错误标签含 `33556434`；总开关、扫描按钮均 DISABLED，不再永久停在初始化状态。
- 蓝牙仍为 READY：5 秒前后 HCI Command Complete 40→42，广告应答 12→14；硬件错误/发送超时均为 0。
  两域本次启动后的 reset 都保持 1，公共供电维持；M33 IPC 累计错误为 2→2，没有新增。
- Flash、SD 正常挂载；偏好 generation=26、writes=0/0，壁纸 checksum=0x8f45aa87 保持不变。
- M55 text/data/bss：5674048 / 91676 / 5075488 B，构建及官方 OpenOCD 烧录校验通过。
  当前 HEX SHA-256：`C0C83B65592C350B3F9C83A3E8716B2CB7946E15251410F00F90C82F4A98B393`。
  M33 HEX 保持 `8585D6215E82CB0387587DA8AF10E44DD3397F4E6EDBB50C0BAFEF06B5E2FC3D`。
- 本地日志（忽略、不含密码）：`tmp/wifi-cn-country-probe.log`、`tmp/wifi-cn-final-status.log`、
  `tmp/wifi-cn-bt-isolation.log`、`tmp/wifi-cn-ui-failure.log`、`tmp/wifi-cn-diagnostic-{build,flash}.log`。
  结束停留设置 → Wi-Fi；Wi-Fi 因 CN 资源缺失不可用，蓝牙正常。未提交或推送本轮改动。

## 2026-09-04 用户指定恢复 AU：实际连接调试

M55 国家码恢复 AU，保留启动结果/国家列表只读诊断；M33 镜像及末尾 2 MiB 用户盘不变。
AU 初始化 `ready=1 enabled=1 state=2 error=0`，两域资源 READY、reset 各为 1。

第一轮实测发现 `Wakaka` 连接立即返回 `33555477 / WHD_WEP_NOT_ALLOWED`，不是密码认证超时。
根因是此 SDK 的 `rt_wlan_connect()` 在未启用 `RT_WLAN_JOIN_SCAN_BY_MGNT` 时，仅填写 SSID，
其余描述符仍为 `INVALID_INFO`，security 为 `SECURITY_UNKNOWN (-1)`；WHD 的 WEP 位检查因此命中。
虽然设置页已经有正确的 WPA2 AES 扫描结果，旧产品调用没有传递它。

修复为单一后台 worker 从真实扫描结果选择匹配 SSID 的最强 BSSID，复制 security/band/channel/
BSSID 后调用 `rt_wlan_connect_adv()`；没有缓存时只在 worker 内补一次被动扫描，未发现或未知
安全类型则明确失败，不猜测或降级成 WPA2/Open。扫描入口复用同一个函数，不启用重复的 MGNT
join-scan 路径。WHD 边界同时拒绝未知安全类型，避免今后再次显示误导性的 WEP 错误。

回归脚本补充被动扫描最多三次（记录每次是否可见，不自动重试认证失败）、连接态扫描、
第二轮前 off/on 与地址清除检查。失败的首轮日志保留 `tmp/wifi-au-ap.jsonl`；后续结果另存，
不能用新结果覆盖第一次失败的证据。

### 修复后实板结果

`test-wifi-ap.py` 两轮、两个 AP：**38 项检查通过**。四轮预连接扫描均一次发现目标，无认证重试。

| 轮次 | SSID / 频段 / 扫描信道 | 连接至 DHCP 就绪（含串口轮询） | 网关 ping，ms |
| --- | --- | --- | --- |
| 1 | Wakaka / 2.4 GHz / 6 | 10.47 s | 9 / 9 / 7 / 9 |
| 1 | Wakaka_5G / 5 GHz / 48 | 8.90 s | 10 / 8 / 8 / 8 |
| 2 | Wakaka / 2.4 GHz / 6 | 5.82 s | 10 / 10 / 10 / 8 |
| 2 | Wakaka_5G / 5 GHz / 48 | 8.93 s | 9 / 8 / 8 / 8 |

- 四次均获得 `192.168.0.109/24`，网关 `192.168.0.1`，ping 共 16/16；WPA2 AES。
  信道由扫描描述符及 WLAN 连接信息核对；尚未额外读取协商速率，`wifi status` 的 DataRate=0
  不代表真实链路速率为零。底层仍按 SSID join，尚未加入指定 BSSID/漫游策略。
- 每次连接后再执行被动扫描，关联和 DHCP 保持。第一轮后关闭已连接 Wi-Fi，SSID/IP/网关清空，
  关闭时扫描被拒绝；重新开启后空闲、不自动连接，再完成第二轮。两域 reset 均保持 1。
- BT HCI Command Complete 31→88，广告应答 3→60；硬件错误、UART 超时均为 0，IPC 错误 2→2。
  这是空闲广播控制器共存，不是 BLE/GATT/A2DP 已连接业务压力测试。
- 5 GHz 上域名 `www.rt-thread.org` 成功解析并 ping 4/4，17/20/20/19 ms。
- `test-wifi-tcp.py` 使用 SDK netutils 的原始 TCP 接收模式（不是 iperf3）：主机 Ethernet
  `192.168.0.106` → 板子 `192.168.0.109`，12.318 s 收到 1,769,472 B，端到端 **1.149 Mbps**。
  同时 60 帧全屏 UI 压测全部完成，**31.41 FPS**、60 batch / 60 submit、GPU busy=45.21%，
  collect/encode/finish=11247/18353/870 μs；无 scanout timeout/overflow。BT HCI 101→108，
  广告应答 73→80，错误和复位计数不变。吞吐偏低，仍需专项定位，不宣称带宽性能已达标。
- TCP 第一次因主机沙箱限制返回 WinError 10013；按权限流程允许访问板子局域网后复测通过，
  未更改主机防火墙、路由或远程连接。测试服务已停止。
- Wi-Fi UI 自动回归 **55 项、0 失败**；在线读取实际 LVGL 标签为“已连接”，详情显示当前
  SSID/IP/网关/RSSI，开关 CHECKED，扫描和断开按钮可用。两核运行中读取，没有 halt。
- Flash 偏好 generation=26、dirty=0、writes=0/0；壁纸 checksum=`0x8f45aa87` 不变。
- 最终 M55 text/data/bss = 5674472 / 91676 / 5075744 B，官方 OpenOCD 烧录及校验通过；
  HEX SHA-256 `97C940B5D03AA8EF829B45C5B0B9238F272557B4ED29EBBD78617BFAFEC50E91`。
  M33 未重新构建/烧录，保持 `8585D6215E82CB0387587DA8AF10E44DD3397F4E6EDBB50C0BAFEF06B5E2FC3D`。
- 证据：`tmp/wifi-au-joined-ap.jsonl`、`tmp/wifi-au-tcp-verified.jsonl`、`tmp/wifi-au-ui-online.log`、
  `tmp/wifi-au-final-checks.log`、`tmp/wifi-au-join-{build,flash}.log`；密码未写入这些文件。

复测入口（密码使用隐藏输入，不放到命令行或脚本）：

```powershell
.\tools\freather\serial-monitor\python\python.exe tools/freather/test-wifi-ap.py --serial COM17 --network Wakaka:6 --network Wakaka_5G:48 --country AU --cycles 2 --debug-port 6666
.\tools\freather\serial-monitor\python\python.exe tools/freather/test-wifi-tcp.py --serial COM17 --board 192.168.0.109 --seconds 12 --ui-bench --debug-port 6666
```

需先用官方 OpenOCD 无复位附着，提供 Tcl 6666；TCP 的地址须取当次 DHCP 结果，不能固定用于所有板子。
