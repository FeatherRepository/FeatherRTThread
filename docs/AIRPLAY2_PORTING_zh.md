# AirPlay 2 移植记录

## 当前状态（2026-09-13）：默认启用的应用层接收器，实机验收进行中

### 2026-09-14 /info TXT 与规范字段补齐

手机仍停在 GET /info 后，没有 pair-setup。核对参考 GET /info 流程后补齐
`txtAirPlay` 原始 DNS TXT RDATA，以及 deviceID、protocolVersion、sourceVersion
字段；保留旧字段以兼容已有调用者。mDNS 与 /info 共用同一个 TXT 构造函数，
身份 pi/protovers 同步，未扩大加密能力声明。
参考：https://openairplay.github.io/airplay-spec/audio/rtsp_requests/get_info.html

实机 test_airplay_info.py 通过 HTTP/XML、RTSP/binary 各自带/不带
qualifier=[txtAirPlay] 的四种请求；核对所有能力/身份字段，并逐字节匹配 live
mDNS TXT RDATA。二进制响应改为临时堆缓冲，发送后释放，避免增大内部静态 RAM。
固件日志 tmp/airplay-info-txt-build.log、tmp/airplay-info-txt-flash.log。
尚不能据此宣称 iPhone 后续配对和真实音乐播放已通过，需继续读取手机现场。

### HTTP/XML /info 整数序列化修复

手机记录停在连续 GET /info/200，尚未配对。实机分别请求 HTTP/1.1 与 RTSP/1.0 后，
发现 HTTP XML 的 features/vv 等整数实际为 `lu`/`ld`，Python plistlib 也拒绝解析；
二进制 RTSP 路径正常。原先只测试二进制路径，未覆盖此兼容性错误。
改用有界十进制转换输出 64-bit 有符号/无符号整数，不依赖 newlib-nano 的 `%ll` 支持。
`test_airplay_xml.c` 验证 INT64_MIN/MAX、UINT64_MAX、零、数组；
`test_airplay_info.py` 实机验证 HTTP/XML 与 RTSP/binary 的 features、vv、pk、deviceid、
name、audioFormats 相同且可解析。修复版已烧录，日志 `tmp/airplay-xml-fix-*.log`。
真实 iPhone 后续协商仍需验收，不能仅凭 /info/200 判断播放成功。

### 最新：缓冲音频已启用（用户批准监听资源调整）

FeatherTalk 项目通过 `applications/airplay2/netconfig/lwipopts.h` 包装原 SDK 配置，
仅覆盖 `MEMP_NUM_TCP_PCB_LISTEN=3`；SConstruct 增加此项目私有头文件目录。
共享 `rt-thread/`、驱动和 BSP 原配置文件未修改。其他工程继续使用原 SDK 值。
AAC-LC 与 buffered/type103 现已启用，mDNS 和 /info 同步声明。

缓冲接收停止时由任务独占 close，外部停止只 shutdown 并等待任务退出，
避免重复关闭；加密缓冲帧在读取 RTP 头之前检查最短长度。
事件端口分配失败会明确返回错误，不再用 eventPort=0 冒充建立成功。

实机连续两轮测试通过：原生 SRP3072、控制/事件/缓冲音频三个 TCP 连接共存；
每轮 260 个加密 AAC 包、777 个解码队列分块。两轮累计 PCM=564385，
decrypt-errors=0、write-errors=0，结束后 state=2，并可再次发现/连接。
这验证了 type103 传输/解码/音频接口，不替代 iPhone 网易云音乐真实试听与 PTP 同步验收。
镜像 SHA256：`980A710E58EED03F7CCE9540A27CCAC75B63561B14DF924BB6DE7F814400A889`。
日志 `tmp/airplay-buffered-build.log`、`tmp/airplay-buffered-flash.log`。
以下阶段记录中“监听数仍为 1”“缓冲未启用”等内容保留为历史问题描述。

### AAC 补齐（后续更新，以本段为准）

已引入 FAAD2 AAC-LC 解码（GPL-2.0-or-later，保留 COPYING），应用层适配 raw AAC
AudioSpecificConfig、44.1/48 kHz 双声道与 16-bit PCM 输出。未集成 SBR/PS/AAC-ELD。
`test_airplay_aac.py` 用独立 PyAV/FFmpeg 编码器生成测试包，两种采样率各 65 包、
65536 PCM 帧验证通过，包含非零样本与小输出缓冲拒绝检查。

板上首次测试暴露应用线程栈不足：GCC `.su` 显示 channel_pair_element 24600 字节，
下层 reconstruct_channel_pair 8248、ifilter_bank 8320、faad_imdct 4144 字节；
原 16 KB 栈不足。实时和缓冲接收线程改为 64 KB，仍使用原有 RT-Thread API。
只读 FAAD2 Huffman/TNS 表增加 const，避免占用受限内部静态 RAM；未改链接布局。

缓冲播放能力与 AAC 编译开关分离：未定义 `FT_AP_WITH_BUFFERED` 时不广播 bit40，
type103 SETUP 明确返回 461，而不是虚报成功。当前 SDK 固定 TCP listener 数为 1，
与控制/事件/缓冲音频多个监听端点冲突；尚未修改此平台配置。
AAC 解码加入并不等于完整 iPhone 音乐投放已通过。

64 KB 栈修复版本实机通过加密 AAC-LC/type96 回归：260 个网络包，777 个
解码队列分块（当前 decoded 字段按分块计数，并非网络包），286442 PCM 输出帧，
decrypt-errors=0、write-errors=0，停止后恢复 state=2 待机。
构建/烧录日志 `tmp/airplay-aac-stack-fix-build.log`、`tmp/airplay-aac-stack-fix-flash.log`。
真实 iOS 的 buffered/type103 验收仍未完成，不以该 UDP 测试替代。

当前代码已替换 A0 探针：开机默认启用，Wi-Fi 获取 IP 后自动启动
HAP、RTSP 与应用层 mDNS；断网/IP 改变时停止旧会话，联网后重建。
`ft_airplay off` 在本次上电期间禁用，`ft_airplay on` 恢复自动等待网络。
只有服务启动全部成功才显示 `enabled=1 state=2 error=0`。
待机不申请 sound0；播放时通过原有音频 owner 接口申请，不抢占 LE Audio/UAC。
当前 Wi-Fi 应用仍不保存密码、不自动加入 AP；AirPlay 不越权打开被用户关闭的 Wi-Fi。

所有新逻辑限于 applications/airplay2 与现有应用音频 owner 枚举，
没有更改 RT-Thread、驱动、BSP、全局 lwIP 或 SConstruct 配置。
ESP32 的 FreeRTOS/网络/分配接口由应用私有适配层接到现有 RT-Thread API。

密码学使用 Monocypher 与现有 mbedTLS 软件算法。原生 transient SRP3072、
加密 RTSP、加密 RTP 与 Apple ALAC 解码已移入；不冒充经典 RSA/FairPlay 或
未实现的持久 HomeKit 配对。AAC 尚未编入，也不广播 AAC 能力。
输出接口目前为 sound0 / 48 kHz / 16-bit / 双声道。

首次需用 `tools/freather/tests/provision_airplay_seed_serial.py` 导入主机 CSPRNG
种子（不写入源码、命令参数或日志），保存于 `/flash/.airplay2/`，
每次启动在输出随机数前轮换持久状态；缺失或写入失败则拒绝配对。
这是本地原型的配置流程，不宣称具备 flash 回滚/物理攻击防护。
串口逐字节发送后导入成功，板上 RNG 与 SRP3072 自检通过。

测试脚本：`test_airplay_protocol.py`、`test_airplay_crypto.py`、
`test_airplay_alac.py` 已通过；独立网络脚本 `test_airplay_discovery.py`、
`test_airplay_pairing.py`、`test_airplay_stream.py` 用于实机验证。
发送测试包不等于喇叭播放成功，必须核对 decoded/PCM/write-errors 并最终由真实发送端试听。

实机适配修复：响应 Content-Length 使用有界 unsigned 格式，避免当前 C 库
将 `%zu` 输出成文字 `zu`；初始化显式置为暂停，避免上游默认 playing=true 占用声卡。

以下为早期 A0 历史记录，不代表当前服务行为。

### 实机验证与剩余限制

#### 手机连接阶段 401 修复

板内 `ft_ap_trace` 保留的手机序列为 OPTIONS/200 → POST /command/401 →
POST /command/401，尚未进入 pair-setup 或 SETUP。统一认证门禁误拦了上游
本来只确认、不执行的 /command 通知。修复为仅对明文 POST 的精确 /command
路径直接返回 200，不解析正文、不设置 DACP/音量/会话/密钥，不开放媒体。
应用层修改，平台和 OS 配置无变化。

`test_airplay_pairing.py` 实机回归通过：两次 /command 均 200，随后未认证
SETUP、RECORD、GET /command、POST /command-extra 均 401；之后正常 SRP3072
配对与加密 /info 仍通过。构建/烧录日志为 `tmp/airplay-command-fix-*.log`。
这修复了已观测到的阻断点，不等于真实手机的后续播放流程已验收。

- Wakaka_5G，板子 `192.168.0.108`，有线测试主机 `192.168.0.103`。
- 自动 DNS-SD 响应、未手动开启时的 RTSP OPTIONS 均通过。
- 独立客户端 SRP3072 transient 配对、服务端证明校验、加密 RTSP `/info` 通过（约 4.5 秒）。
- 加密 RTP/ALAC：512 包收到、512 包解码、195396 帧写到 sound0，解密/写入错误均为 0。
  该次测试音量为静音；这是数据通路验证，不是主观听音验收。后续测试脚本使用 -12 dB。
- ALAC 端序适配补充编译器 `__BYTE_ORDER__` 判断，修复小端 ARM 被旧源码当成大端的问题。
- 初始化与完整停止均明确暂停，避免 timing reset 默认 playing=true 导致待机占用音频输出。
- 当前平台 `MEMP_NUM_TCP_PCB_LISTEN=1`：独立 AirPlay event TCP listener 创建失败。
  没有突破应用层范围去改 SDK；原生手机对该事件端口的要求仍须验证/解决。
- 尚未完成真实 iOS/macOS 连续音乐、PTP 同步、长时间播放与多客户端竞争验收。
  因此“默认待连接/测试链路通过”不等于完整 AirPlay 2 产品兼容性通过。
- 最终烧录校验镜像 SHA256：`846DDC1ECAB7D09E1816D96FDBD885BA46B53BD3C39566FB927B7636E499C661`。
  日志 `tmp/airplay-ready-build.log`、`tmp/airplay-ready-flash.log`。
- 最终版本 -12 dB 测试：512 包收到/解码，193098 PCM 帧写入，解密/写入错误为 0；
  完整 TEARDOWN 后两次查询 PCM=193098、empty-reads=114 均不再增长，恢复待机。
- 手动 off 返回 503、on 恢复 state=2；Wi-Fi disconnect 自动 state=0，重新联网自动 state=2；
  最后 DNS-SD/OPTIONS 检查通过，板子保持 Wakaka_5G 上的 `enabled=1 state=2 error=0`。

## 历史状态：A0 协议与输出适配基础，不可播放

本阶段不是完整AirPlay 2接收器。尚未实现配对、加密认证、媒体解码、mDNS发现及PTP同步。
不向手机广播服务，不返回配对成功，不用测试音或经典AirPlay代替AirPlay 2播放验收。
最终必须使用真实iOS/macOS发送端做配对、连续音乐播放与断线重连测试。

## 参考源码与许可证

- 项目：https://github.com/rbouteiller/airplay-esp32
- 固定提交：`9ef96118e5ed149958c7913d9708824f5b97cb13`
- 本地：`D:/Develop/Edgi-Talk/airplay2-resources/airplay-esp32`
- 上游LICENSE为Non-Commercial License；商业使用需作者书面许可。
  保留原仓库及LICENSE，作为非商业研究参考，不将其整包并入产品或宣称获得Apple认证。
- 上游依赖ESP-IDF、libsodium、mbedTLS、espressif/mdns、esp_audio_codec，
  不能直接把ESP32固件或平台库链接到Cortex-M55。

## 已实现

代码：`projects/FeatherTalk_M55/applications/airplay2/`。

| 模块 | 当前实现与边界 |
|---|---|
| ft_airplay_protocol | 自有有界适配层：RTSP/HTTP请求分帧、HAP加密记录长度检查、TLV8读写；不包含密码学 |
| ft_airplay_probe | 手动启停的TCP 7000开发诊断入口；仅OPTIONS成功，pair-setup等返回501；无mDNS广播 |
| ft_airplay_sink | 解码后PCM → 现有sound0接口；独立AIRPLAY2输出owner，忙时返回错误，不抢占LE Audio/UAC/本地播放器 |

RTSP头最多4096B，body最多16384B；支持TCP拆包/粘包及二进制body。
拒绝负数/溢出Content-Length、重复长度或CSeq、Transfer-Encoding及畸形头；
不依赖接收缓冲的NUL结尾，解析结果body借用输入，consumed仅包含一条完整请求。
HAP分帧只检查LE16长度、1024B上限和16B认证标签占位，**不验证认证标签**。
TLV先验证完整输入及输出容量再复制，截断尾部不能被当作有效配对字段。

Probe最多同时处理1个客户端，每连接最多8请求、5秒绝对处理期限，
收发1秒超时；仅显式`ft_airplay probe`才监听，stop由工作线程关闭socket。
产品SCons启用lwIP SO_REUSE=1，监听socket显式设置SO_REUSEADDR，
防止关闭后TIME_WAIT导致立即重启失败；SDK demo配置不改。
诊断过程中不打开音频设备、不改变Wi-Fi连接、不操作蓝牙电源或状态。

PCM适配按现有sound0能力检查采样率/位宽/声道，单次写入不超过4096B且须整帧。
写接口返回实际写入字节数；调用者须处理短写。打开失败释放owner，关闭失败保留owner。
现有sound0不声明44.1kHz支持；**44.1→48kHz重采样尚未实现，不能直接标作兼容**。
PCM适配仅完成模拟驱动单元测试，未完成真实AirPlay解码后的喇叭出声测试。

## 验证与复现

```powershell
.\tools\freather\serial-monitor\python\python.exe tools/freather/tests/test_airplay_protocol.py
.\tools\freather\build-demo.cmd FeatherTalk_M55 -Jobs 8
# 已连接Wi-Fi后，仅运行诊断入口测试：
.\tools\freather\serial-monitor\python\python.exe tools/freather/tests/test_airplay_probe.py --board 192.168.0.108 --bind 192.168.0.103
```

Host测试编译实际生产C文件（不是Python重写算法），包括：逐字节拆包、两请求粘包、
非NUL缓冲、大小写头、重复字段、负数/溢出长度、TLV分片/截断/输出容量、
加密记录边界、20000组确定性畸形输入。它是单元/烟雾模糊测试，不等于完整安全审计。
另对实际PCM适配C文件使用模拟RTOS/音频驱动，验证忙拒绝、格式检查和失败回收。

构建/烧录日志：tmp/airplay-foundation-build.log、tmp/airplay-foundation-flash.log。
诊断日志：tmp/airplay-foundation-probe.log；仅测试开发TCP入口，不能证明AirPlay配对成功。
本轮DHCP地址已变化，先用`ft_wifi`和主机`ipconfig`确认后再测试。
最初两次测试使用历史地址而失败；probe3在新地址上通过拆包/粘包与拒绝路径测试。
probe4暴露立即重启绑定失败，已补SO_REUSE配置，后续镜像须复测重复启停。

### 最终A0镜像验证

- SO_REUSE修复后连续两轮板端测试通过：拆包/粘包、501拒绝未实现的配对、
  400拒绝畸形长度、启动/停止及立即重启；两轮结束audio-owner均为0。
- 工作线程ap_probe的4KiB栈峰值32%，结束后probe=0，无mDNS广播。
- 日志：tmp/airplay-restart-probe1.log、tmp/airplay-restart-probe2.log。
- 已烧录校验M55 HEX SHA256：
  `ECA28AE8CB8C7C248361B51CBAD3CB243709DB47DAADE415798CDA0053F54DD3`。
- 最终构建/烧录：tmp/airplay-restart-build.log、tmp/airplay-restart-flash.log。
- Host协议与PCM模拟驱动测试、LC3水位回归、UI导航检查均通过。
- SO_REUSE版本Wi-Fi回归：有线192.168.0.103→板子192.168.0.108，
  32735232B/10.029s=26.113Mbit/s，正常结束；本次AP/地址已有变化，
  不用此短测替代前一轮完整性能对照。日志tmp/airplay-wifi-regression.jsonl。
- 最终状态tmp/airplay-final-status.log：诊断probe=0，Wi-Fi/BT均state=3/error=0/resets=1；
  主堆可用776560B。只确认管理状态，不代表已经测试LE Audio音乐并发。
- 未改动M33、FPGA、蓝牙配对存储或用户数据分区。尚未提交本轮代码。

## 下一阶段顺序与验收门槛

1. **HAP配对与会话加密**：SRP、Ed25519/X25519、HKDF-SHA512、ChaCha20-Poly1305；
   移植可用于M55的密码库，接入硬件熵源，测试已知向量和认证失败路径。
   不以rand()、固定密钥或跳过验证代替；设备长期密钥与配对记录需单独规划存储，
   不占用已有蓝牙bond区或格式化用户文件系统。
2. **二进制plist和RTSP状态机**：/info、pair-setup/verify、SETUP/RECORD/FLUSH/TEARDOWN；
   明确会话生命周期、重放计数、断连清理和输入尺寸限制。
3. **媒体解码与缓冲**：上游audio_decoder.c直接调用esp_alac_dec/esp_aac_dec；
   必须替换为可在ARM上构建且许可证适用的解码器，不能用占位解码输出静音。
   加入有界媒体队列、预缓冲、欠载恢复、44.1/48kHz适配，测CPU/内存峰值。
4. **时钟与发现**：接入媒体时间戳/PTP及采样时钟漂移补偿；仅在可用能力真实实现后
   发布_airplay._tcp/_raop._tcp，避免广告声称支持而服务拒绝。
5. **实机验收/UI**：真实Apple发送端连续播放、暂停/恢复、断线/重连，
   与LE Audio/USB/本地播放做占用仲裁，再接UI设置与全局导航图。

当前UI未添加“AirPlay可用”开关，避免把未完成能力当作可用产品功能。
