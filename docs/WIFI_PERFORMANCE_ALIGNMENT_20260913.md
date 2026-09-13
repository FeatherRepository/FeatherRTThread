# Wi-Fi 性能差异对齐

## 提交版本范围

本版本保存已烧录验证的产品配置：默认O3、WHD/lwIP热点SoCMEM、RT-Thread内核热点ITCM、
启动Cache同步、大图像素HyperRAM分配，以及可复用的脱敏TCP测试和诊断工具。
不保留无收益的优先级调整或WiFi池迁移，不提交临时SDK参考配置、固件二进制、日志或密码。
验收范围仅为下文两个频段各方向30秒TCP测试；UDP TX/RX尚未测试，
长时稳定性及LE Audio音乐并发仍待验证。以下按时间保留完整实验过程，最终结果见文末。

固定测试路径：Wakaka_5G ch48 → 主机有线192.168.0.106；板子192.168.0.112。
上下行均20秒，独立顺序测试，不是同时全双工。密码不入库。

## 官方参考

Infineon mtb-tester-psoc-edge-wifi-bluetooth / README / Throughput Measurements：
M55 400MHz，SDIO 100MHz，Asus AX3000，RF屏蔽箱，RSSI约-4至-8dBm，
网络关键代码放入SoCMEM。GCC参考5GHz TCP Tx58.2/Rx72Mbps。
当前办公环境RSSI约-50dBm；当前SDIO主机配置为50MHz/4bit。
本次SDK iperf-like原始TCP测试不同于官方iperf2完整测试流程，不能直接作合规验收。

## 对照结果（方向以板子为视角）

| 阶段 | Rx Mbps | Tx Mbps | 结论 |
|---|---:|---:|---|
| 原有O0 | 4.077 | 4.297 | 已验证基线 |
| O2、网络仍在XIP | 5.707 | 0.088 | 上行退化，不可直接视作优化成功 |
| O2、大型图片优先HyperRAM | 7.145 | 0.090 | 主堆压力解除，但上行退化仍存在 |
| O2、HyperRAM图片、关闭严格别名优化 | 8.863 | 7.320 | 双向恢复，保留为回退镜像 |
| 上述配置 + SoCMEM热点 + 启动Cache同步 | 12.468 | 14.758 | 当前已烧录并实测通过 |

当前比原始基线Rx提升3.06倍、Tx提升3.43倍，仍未达到官方RF箱/100MHz SDIO结果。
最终20秒下行31227904字节，上行36900864字节，分别耗时20.037/20.003秒。
主堆结束可用776448字节；Wi-Fi仍连接Wakaka_5G，error=0；
共享radio状态显示Wi-Fi/BT均claimed=1/state=3/error=0、reset计数各1。
本次未同时运行LE Audio音乐流，因此不宣称完成音频与Wi-Fi并发压力验收。

## 已发现并处理

- 主堆仅余约10KB时，元数据中有一个768064字节分配，与480×800 RGB565图片缓存相符；
  HyperRAM基本空闲。将图库/壁纸的大型像素缓存优先放HyperRAM，沿用LVGL对齐与释放机制。
  实测主堆空闲恢复至777792字节，HyperRAM使用约768KB，壁纸checksum仍为0x9ca95495。
- M55 SConstruct漏声明link.ld依赖，导致仅改链接脚本时不重新链接。
  增加env.Depends；已实测触发重链接。必须核对ELF段地址，不能只看脚本改动。
- 增加可选FEATHERTALK_OPT_LEVEL=0/2以复现编译优化对照；保留调试符号。
- SoCMEM热点搬迁初次启动异常，曾撤回隔离。后来确认GCC启动路径缺少
  RAM代码复制后的Cache同步：Reset_Handler先启Cache，CMSIS复制代码后直接
  调用项目_start，未调用BSP提供的cy_cache_update（其他工具链路径有调用）。
  在项目_start进入RT-Thread前补调用后，SoCMEM布局启动和跑流均通过；
  当前保留116480字节网络热点在SoCMEM，不削减数据堆或GFX区域。
- -fno-strict-aliasing使O2上行从0.090恢复至7.320Mbps；具体违反别名假设的
  源码位置尚未定位，这一编译兼容性配置已通过本轮对照，但不应伪称已找到所有驱动缺陷。
- O2上行低速伴随WHD IOCTL响应ID不匹配；未证明其原因，不以信号强度解释全部差距。
- HAL存在500us轮询后备路径，同时编入RTOS信号量路径；
  尚未测得实际命中占比，不可断言500us轮询就是吞吐主因。
- 100MHz未实施：现有RT-Thread主机仅配置High-Speed能力；
  不能只将freq_max常量改为100MHz就宣称已完成官方总线模式对齐。

## 日志与诊断

tmp/wifi-o2-down.jsonl、tmp/wifi-o2-up.log、
tmp/wifi-cache-down.jsonl、tmp/wifi-cache-up.log。
tools/freather/tests/wifi_heap_probe.py：通过调试器只读取堆元数据，统计分配大小；
ELF必须与板上镜像一致，不读取凭据或应用数据。
构建、烧录日志在tools/freather/tests/wifi_*_build.log和wifi_*_flash.log，
临时文件与二进制不提交。

最终日志：tmp/wifi-socmem-final-down.jsonl、tmp/wifi-socmem-final-up.log、
tmp/wifi-aligned-final-status.log。
最终M55 HEX SHA256：DED52ED231012A6ED70AC2B13EBA23FFAF9D1A668872502CD57E1F95239F4241。
构建、OpenOCD编程校验、启动和双向TCP测试已完成，所有测试监听已关闭，
板子保留Wakaka_5G关联。M33未修改，LE-only配置未改。
回退镜像（O2兼容编译+HyperRAM图片、网络代码仍XIP）在
../build-artifacts/wifi-known-good/，SHA256
EF00885D1E6ACFB88B8979B654AF06648DA197917E0ACCCD1780B802E3B8697E。

## 复现配置

第一轮M55为带调试符号的-O2 -fno-tree-vectorize -fno-strict-aliasing。
需要O0对照时，PowerShell在构建前设置 $env:FEATHERTALK_OPT_LEVEL='0'；
第二轮默认已调整为3；可用FEATHERTALK_OPT_LEVEL=0/2/3复现。该开关不改变链接布局，比较时须记录两者。
全局编译选项改变会触发全量重编；链接脚本修改现在也会触发重链接。

## 剩余差异

- SDIO仍为50MHz/4bit；100MHz协商、主机总线模式及硬件时序尚未对齐。
- 官方RX缓冲区DTCM布局、RTOS适配层及协议栈配置尚未全部对齐。
- 办公网络、AP型号/带宽、RSSI与官方RF箱不同；本轮是短时SDK TCP测试。
- 仍需长时吞吐、错误计数及LE Audio并发回归后，才可宣称性能优化稳定验收。

## 第二轮：以 RT-Thread SDK demo 为对照（2026-09-13）

不能把上文 Infineon FreeRTOS/RF箱的 58.2/72 Mbps 当作 RT-Thread SDK 发布指标。
RT-Thread 官方 WiFi 示例文档只给出了 iperf/JPerf 操作步骤，示意图没有成绩：
https://rt-thread-studio.github.io/sdk-bsp-psoc_e84-edgi-talk/latest/driver/Edgi_Talk_M55_WIFI/README_zh.html

因此在同一硬件上临时烧录本地 `Edgi_Talk_M55_WIFI` 建立实测对照。
这是**本地 SDK demo 对照**，不是上游官方认证成绩：共用当前工作树驱动与RT-Thread源码。
保持 SDK 默认 O3、SDIO priority=0、WHD实际priority=25、50MHz/4bit和TCP参数。
为避免写入产品数据区，仅对参考镜像做以下适配；源文件测试后恢复：

- FAL资源改为SDK支持的IN_MEMORY；禁用BSP_USING_FILESYSTEM，避免自动挂载。
- 补启用RT_WLAN_JOIN_SCAN_BY_MGNT：原配置缺失时rt_wlan_connect传递UNKNOWN
  安全类型，drv_wlan_join直接返回WHD_UNKNOWN_SECURITY_TYPE，扫描正常但无法关联。
  该适配只修正关联入口，不调节吞吐路径。补配置后同AP关联成功。
- 固件/CLM与产品使用文件SHA256一致，未改射频资源、国家码或TCP窗口。

参考HEX：`../build-artifacts/wifi-sdk-reference/rtthread.hex`，
SHA256 `E47B6165BF055A8F8970D0298D0DD5B74C8CD1C6FD8D9A07DF7D32DFB0FF2B05`。

| Wakaka_5G，20秒 | Rx Mbps | Tx Mbps |
|---|---:|---:|
| SDK demo 对照 | 10.457 | 13.869 |
| 产品 O3，原优先级25/7 | 15.867 | 14.951 |
| 产品 O3，SDK优先级25/0 | 16.037 | 14.925 |
| 产品 O3，WHD提优先级14/7 | 15.721 | 15.050 |
| 产品 O3，WiFi池移片内SoCMEM | 15.023 | 14.527 |
| 产品 O3，RT-Thread内核热点移ITCM | 31.555 | 28.644 |

优先级与WiFi池迁移未显示可靠收益，均不固化；最终恢复25/7及原HyperRAM池布局。
O3保留标量与no-strict-aliasing兼容选项，不关闭UI或蓝牙以制造成绩。
SDK Wakaka/ch6 Tx=13.801Mbps/20.004s；Rx一个5秒分段8.113Mbps后主机sendall
发生3秒超时，不记作有效20秒吞吐。不能宣称两频段长期稳定达标。

### 排除的假设与工具修正

- 新增只计数不逐包打印的SDHC诊断。一次10秒Rx前后：semaphore 828→43328，
  poll=0→0，delays=0→0。实际走中断信号量，不是500us轮询；不盲改HAL延时。
- `ft_wifi_sched`无参数读计数；product/sdk/balanced仅用于临时A/B，重启不保留。
- build-demo识别SDK的Debug/rtthread.hex，修复构建已成功但脚本误报找不到HEX。
- 主机TCP工具增加--sdk以使用ifconfig检查，不依赖产品ft_wifi/UI命令。
- join工具增加--password-stdin，保留脱敏错误状态；SDK DHCP检查只匹配
  ip address，不能把旧DNS地址误判为已拿到IP。

日志前缀：tmp/wifi-o3-*、wifi-sdkprio-*、wifi-balanced-*、wifi-pool-*、
wifi-wait-*、wifi-sdk-ref-*、wifi-sdk-24*。测试数据为合成TCP负载。

### 第二个主要执行瓶颈

仅移动WHD/lwIP热点不够。每次SDIO信号量完成、消息队列和TCP缓冲处理仍调用
XIP中的RT-Thread内核。增加 `*kernel/src/*.o(.text*)` 到现有ITCM复制段，
并从XIP选择器排除。ITCM代码189840→213904字节，新增24064字节；
ram_vectors位于214016，仍在256KB区域内，不占M33或数据堆。
保留前轮启动Cache同步。未调整主频、SDIO模式、TCP窗口、线程优先级或关闭UI。

实测20秒Rx=78938112B/20.013s=31.555Mbps，Tx=71630847B/20.006s=28.644Mbps。
日志 `tmp/wifi-kernel-down.jsonl`、`tmp/wifi-kernel-up.log`。
相比先前产品O3约16/15Mbps又提升约两倍，支持RTOS执行位置是重要瓶颈的判断。
不能将所有剩余问题都归因于此；最终版本的2.4GHz复测见下文。

增加 `ft_wifi_tcp`：在tcpip线程中读取iperf连接的接收窗口、乱序字节、refused_data
长度和重传计数，不读取负载。主机发送超时时先请求此快照再停止测试。
`--io-timeout`默认仍为3秒；10/20秒仅用于区分长重传与永久停滞，不能抹掉失败记录。
Windows PktMon即使尝试提权也返回拒绝访问，本轮没有成功抓包，不宣称观察到空口/线上重传。

### 最终产品镜像与30秒复测

| AP | Rx Mbps | Tx Mbps | 结果 |
|---|---:|---:|---|
| Wakaka ch6 | 23.802 | 23.590 | 两方向各30秒完整结束，无发送超时 |
| Wakaka_5G ch48 | 21.690 | 25.287 | 两方向各30秒完整结束，无发送超时 |

2.4G Rx89292800B/30.012s，Tx88475059B/30.004s。
5G Rx81395712B/30.022s，Tx94830592B/30.001s。
5G重复值低于先前31.555/28.644，全部保留，不挑选峰值。
最新5G相对SDK同板基线Rx约2.07倍、Tx约1.82倍；2.4G Tx约1.71倍。
SDK的2.4G Rx未取得有效完整成绩，因此不能编造该方向的数值倍率。
当前可以说短时产品吞吐超过本地SDK有效对照；不能声称通过官方RF箱指标、
长时间稳定性或LE Audio音乐并发压力认证。先前2.4G超时在这次复测中未复现，
不等于已经穷尽其发生条件。

已烧录校验的最终HEX SHA256：
`800E737837F871140B019973A770C281FD9F4A8D7ADF4E276811C8DB92C445EE`。
构建/烧录：tmp/wifi-tcpdiag-build.log、tmp/wifi-tcpdiag-flash.log。
跑流：tmp/wifi-kernel-24down.jsonl、tmp/wifi-kernel-24up.log、
tmp/wifi-release-5down.jsonl、tmp/wifi-release-5up.log。
结束状态：tmp/wifi-sdk-aligned-final.log，Wakaka_5G关联，error=0；
Wi-Fi/BT均claimed=1/state=3/error=0/resets=1；active-iperf=0，主机5001无监听。
主堆可用776368B，壁纸checksum不变，UI overflow=0、scanout timeouts=0。
SDHC累计semaphore=662674、poll=0、delays=0。
Python语法检查、git diff --check和LC3水位单元测试通过。
M33、FPGA和用户Flash数据分区未烧写，测试后SDK参考配置已恢复。
