# FeatherTalk Wi-Fi 首轮移植

更新日期：2026-09-04。本文区分代码完成、构建通过和实板结果；未验证项不视为通过。

后续共享电源/复位已改为运行时 Radio Manager，见
`../../FeatherTalk_Common/RADIO_COEXISTENCE_zh.md`。下方首轮固件哈希和 READY 标志测试属于历史验收，不能代替该管理器的实板 HCI 应答回归。

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
- 被动扫描一次，最多展示 32 个 BSSID；保留原始 SSID，不翻译网络名。国家码目前沿用 demo AU，实际所在地确认前不执行主动连接验收。
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
- [ ] 正确地区配置后的连接、DHCP、ping、iperf（需要用户指定测试网络）。
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

1. 确认实物国家/地区，更新 `WHD_COUNTRY_CODE`；确认测试 SSID，密码优先在板上输入。
2. 验证 WPA2/WPA3（分别验证，不能以扫描成功替代认证）、DHCP、网关 ping、DNS 和 iperf。
3. 网络掉线、扫描超时/取消、异常固件恢复与凭据安全持久化；检查与音频/USB/SD/GPU 的并发。
4. 当前扫描/切换/页面功能已板测；认证、联网和上述后续项仍未验收。结束时设备留在设置 → Wi-Fi，Wi-Fi 开启且未连接。
