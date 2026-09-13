# LE Audio 单播修复与验证记录

## 本次提交状态

Xperia 5 V 已完成媒体服务发现、加密配对、双 CIS 建立，并由用户确认板载喇叭出声。
随后针对卡顿修复 ISO 逐包打印阻塞和 LC3 误欠载；该最终版本双核构建、烧录校验与
主机回归测试已通过，但尚未完成用户连续播放复测，不能标记“流畅播放验收通过”。

最后烧录镜像 SHA256：

- M33：5B07C0B501B50BEA70F6B9C0F864E8913571CB3F2A6161F7111C07BE621FB01B
- M55：DDF2264B253D59B0E8FBCAC0D4F5CF96175278F5537E1129334D86F2132FF0FD

待办：连续音乐流的 ISO/PLC/欠载统计；LE 绑定持久化；完整 TMAP 能力与状态机合规性。
以下为分阶段证据，旧阶段“未连接／无声”的结论不代表当前版本。

提交前已获取并合并远程 a228fb48（含新增 VCS 与分类元数据提交）。
重叠实现采用本地已板测的版本：VCS 三字节状态/0–255 音量/Change Counter、
Available Audio Contexts Notify/CCCD、现有7上下文位图和扩展广播。
远程两字节 VCS、旧操作码与不带 CCCD 的声明由上述修复替代；
保留现行 LE-only 验证配置，未额外扩大已声明的音频上下文。
合并保留远程提交历史；最终代码测试覆盖不因历史整合而改变。

基线：origin/product/edgi-talk a8db342b，叠加原有本地 USB/SPI 修改。
目标：Xperia 5 V → CYW5551x → M33 BTstack ISO → M55 LC3 → ES8388/sound0。
FPGA 音频桥保持默认关闭。当前仅验证 48 kHz、10 ms、每 ASE 单声道、30–120 字节 LC3 帧；
这不是完整 TMAP 合规声明，广播接收和其他强制能力仍待补齐。

## 修复

- 使用 BTstack Extended Advertising API 调度参数、数据和启用，发送 BAP General
  Announcement、CAP Announcement 与 TMAS UMR Service Data。
- 监听 BTstack 转换后的 GAP LE Connection Complete；SMP 提前初始化并确认 Just Works。
- 启用本 BTstack 版本处理 Advertising Set Terminated 所依赖的 CENTRAL 编译路径。
  不启用时，第一次连接断开后 ACTIVE 标志遗留，start 调用不会再次发送广播启用命令。
- PACS 修复采样率位图长度；ASCS 控制点增加 Notify/CCCD；TMAS 使用 16-bit UUID、
  UMR 角色 0x0008，移除错误挂在 CAS 下的 TMAP Role。
- 修复生成器把纯十六进制序列号字符串解释为单个数值的问题。暂不暴露未实现
  robust caching 的 Database Hash，避免生成器缺少 CMAC 依赖时暴露随机哈希。
- Codec Configure 按 Num_ASEs 解析，预检完整变长请求；QoS 每项按 16 字节解析。
- ASE 返回补全 Preferred QoS、CIG/CIS ID；错误返回控制点，不污染 ASE 状态值。
- 通知排队使用 ATT can-send 回调；ATT 长值支持分段读取。
- 修正 Channel Allocation LTV 类型为 0x03；拒绝 M55 解码器不支持的配置。
- Sink 在 Enable 和 CIS 建立均完成后进入 Streaming；支持 Sink Disable；
  Release 保留订阅，ACL 断开清理订阅。
- ISO 校验包长、SDU 状态与完整包标志。BTstack 已在 HCI 层完成分片重组。
- ISO → IPC 写入长度包含 2 字节长度头，避免每帧漏掉末尾两个字节。

## 可重复测试

在仓库根目录运行：

```powershell
gcc -std=c11 -ffunction-sections -fdata-sections '-Wl,--gc-sections' -Itools/freather/tests -Ibluetooth/port_rtthread -Ibluetooth/reference/btstack/src tools/freather/tests/le_audio_wire_test.c bluetooth/reference/btstack/src/btstack_util.c -o tools/freather/tests/le_audio_wire_test.exe
./tools/freather/tests/le_audio_wire_test.exe
```

测试编译实际 bt_le_audio_unicast.c，隔离硬件接口，覆盖双 ASE Codec/QoS、状态编码、
部分 ATT 读取、ISO 帧尾、Disable/Release、截断请求和不支持的采样率。
同时遍历生成的 GATT 表检查 PAC LTV 与控制点 Notify、TMAS Role。

空中诊断脚本：tools/freather/tests/le_audio_ble_probe.py。
依赖安装到 tools/freather/ble-validation-vendor 的 bleak 3.0.2。
--debug 输出 Windows 服务访问拒绝原因，--pair 可执行配对验证。
普通模式只建立临时 BLE 连接和读取 GATT，不发送 ASCS 配流指令。

## 已取得的证据与边界

- C 测试通过；M33 编译签名成功；M55 重新构建检查成功，FPGA 关闭。
- 首版修复上板：0x2036/0x2037/0x2039 Command Complete 均 status=0；
  本机收到地址 9C:C7:D3:E1:BC:53 的 BAP/CAP/TMAS 广播。
- 本机 BLE 连接成功，MTU=527，TMAS UMR 读回 0800。
  Windows 通用 GATT API 可能隐藏系统占用服务，缺失不能直接判定服务端没有服务。
- 首次断开后停止广播已在本机复现，并定位到上述 BTstack 编译条件。
- M55 LC3 自检观察到 54 帧、PCM 103680 字节、sound0 写入 102400 字节，
  plc=0、bad_len=0；未取得完整 300 帧自检结束日志，不记为完整播放通过。
- 用户反馈 Xperia 5 V 在首版修复后仍无法连接。手机音乐端到端验证仍未通过。

### 广播恢复阶段板测（历史版本）

- 广播恢复修复固件 SHA256：
  17CE128F6A04CCBAF08790062961B488DCAE52AB99D6988CC30F36B0610DB4F9。
- M33 OpenOCD 写入、verify_image、复位均成功。
- 两次连续扫描 → 连接 → 断开均成功，两次 MTU=527。
- Windows 日志明确报告 PACS/ASCS/CAS 为 access denied（系统占用），
  不是服务未注册；普通应用不能据此继续执行 ASCS 空中配流测试。
- 单独执行配对成功：Paired to device with protection level ENCRYPTION。
- 测试后 M33 READY、error=0、GATT connected=0，本机已释放连接。
- M55 LC3 streams=0、sound0 claim=0/open=0，已退出自检等待真实手机流。
- Xperia 5 V 需要针对这一最终版本重新连接。普通 BLE 配对通过不等于
  Xperia 的 ASCS/CIS/音乐播放已通过。
- 日志：tools/freather/tests/le_audio_air_1.log、le_audio_air_2.log、le_audio_pair.log。

## 媒体识别后续修复

用户反馈 Xperia 5 V 连接后马上断开，且未识别为媒体设备。
已增加 VCS (0x1844)：Volume State、Volume Control Point、Volume Flags。
实现 0–6 控制操作、Change Counter 校验和状态通知；
音量写入共享 volume_percent，供 M55 的 LC3 解码链应用到 ES8388。
主机 C 测试包含绝对音量、陈旧计数器拒绝和静音。
扩展广播增加 PACS/ASCS/CAS/VCS/TMAS 的 UUID 列表，保留 Service Data 公告。
增加 g_le_connect_diag[8]：连接数、断开数、最近断开 reason、配对完成数、
配对 status/reason、最近动态 ATT handle、最近写操作第一个字节。
这组证据不会被周期性广播命令覆盖。

后续实测：广播五项 UUID 均可见；带旧 Windows 配对记录时服务发现持续失败，
板端最近断开 reason=0x13。仅移除本机测试配对后，重新加密配对成功且 MTU=527。
VCS 也被 Windows 以 access denied 隐藏，不能声称其已经完成空中读写验证；
其控制逻辑已通过主机 C 测试。Xperia 媒体识别和音乐播放仍需新配对验证。
现有 LE 绑定数据库使用内存，复位/重烧后旧绑定可能失效；持久化尚待实现。

### Xperia 报“连接遇到问题”的诊断

再次观察到 M33 READY/error=0，最近 reason=0x13。这个原因只能表示对端终止，
不足以判定是哪项媒体服务不被接受，不能继续据此猜测或认定修复完成。
已在本地 HCI dump 包装层增加 64×64 字节环形记录 g_le_trace：
只记录 ATT、SMP 操作码以及连接/加密/断链事件，不记录 SMP 密钥或音频。
读取命令：

```powershell
./tools/freather/serial-monitor/python/python.exe ./tools/freather/tests/read_le_trace.py
```

脚本从本次 ELF 定位符号，并检查读取前后的提交计数一致性。
诊断固件烧录校验成功，初始 trace_count=0。
下一次 Xperia 连接失败后应先抓取记录，再决定后续协议修复。

### Xperia 实际失败记录与 CCCD 修复

用户再次尝试后：trace_count=124，pair_completions=1，pair_status=0。
手机完成服务/特征发现并读取 Volume State、Sink PAC 和 Volume Flags，
没有进入 ASE 配置，随后 reason=0x13 断开。
对照 AOSP LeAudioClient 服务发现逻辑发现：
Available Audio Contexts 缺少 CCCD 时，客户端直接 disconnectInvalidDevice(INVALID_DB)。
当前表确实把 0x2BCD 声明为 READ，未生成 CCCD；这与现场阶段吻合。

修复：将 Available Audio Contexts 声明为 READ | NOTIFY，保存/读取其 CCCD，
断开后清理订阅。新增测试验证生成表内存在 CCCD，订阅写入和读回成功。
引用：https://android.googlesource.com/platform/packages/modules/Bluetooth/+/db88f358a32fc583ee907eea9d8b87d738badb63/system/bta/le_audio/client.cc
定位：kAudioContextAvailabilityCharacteristicUuid 分支、audio_avail_hdls_.ccc_hdl == 0。

修复固件 SHA256：A48C074BEC2C5D64213AAE46DDE62F0D54DD71EA2677E14EC88D7B7C11815A3A。
M33 编译、签名、OpenOCD 写入及校验成功。等待 Xperia 对新 GATT 表重新配对验证；
此处不将代码修复和普通 BLE 配对等同于手机媒体播放通过。

### CCCD 修复后：手机建立双 CIS，无声定位到 M55 栈

Xperia 新记录中 Codec Configure/QoS/Enable 均得到成功应答。
CIS 0x60、0x61 建立成功，手机通过 VCS 调节音量；用户确认正在播歌。
M33 ISO produce 计数增长，跨核 ring 接近满；M55 LC3 frames=0，sound0 已打开为
48 kHz / 16-bit / 2ch。线程列表检查出现对象损坏并在 tshell 遍历时 HardFault。

发现消费线程 ft_alink 仅分配 2048 字节栈，而 ft_lc3_decode_pair 的局部 PCM
数组已占 1920 字节，后续 LC3 变换调用还需工作栈。将该线程栈增至 32768 字节；
同时补全测试音使用 sin 所需的 math.h。重新构建 M55 成功，需烧录后验证
真实 LC3 解码计数和线程栈用量。M33 CCCD 修复无需撤回。

### 首次真实出声后的卡顿

用户确认有声音，但断续。统计：276 个立体声解码帧、PLC=218、starves=11、
跨核读取43542字节；ft_alink 栈最高使用约20%，对象列表正常。
定位到 HCI dump 过滤器只过滤 ACL，未过滤 ISO：每秒200个音频包完整 hex 输出
大幅超过115200串口带宽，同步打印会阻塞蓝牙收包。增加 ISO 过滤，保留信令诊断。
另修复 LC3 水位计时器：未进入真正重缓冲时，新包到达必须清空空缓冲计时；
旧代码在正常包间空隙后持续累计50ms并误触发重新预填充。
test_lc3_watermark.py 编译实际生产函数，验证1000个正常包间空隙不触发欠载，
以及真实50ms欠载与水位恢复，测试通过。双核重新构建通过，等待上板连续播放结果。

## 参考资料

### 小米 15 Pro：纯 LE 配对入口诊断

冷启动后本机可收到 LE Audio 扩展广播，但用户多次在小米上尝试时 trace_count
与连接计数仍为0。用户明确要求仅验证 LE Audio；短暂双模试验已撤回。
当前 FEATHERTALK_BT_LE_AUDIO_ONLY=1，经典蓝牙可发现/可连接入口均关闭。

增加第二组 BLE legacy connectable/scannable PDU（属性0x13），同样通过 HCI
Extended Advertising API 配置，Flags=0x06；保留 BAP 扩展广播。
它是 BLE 发现兼容性试验，不是 BR/EDR，也不是 A2DP 回退。
任一 LE 连接建立后停止两组广播，断开后恢复。编译、签名、写入和校验完成，
Windows 空中扫描确认同一地址能收到 Extended(type5) 与 ConnectableUndirected(type0)。
用户重试后已确认可关联并播放音乐。板端 trace_count=188，LE配对status=0，
完成 Codec/QoS/Enable，CIS 0x60/0x61建立，断开计数为0，收到VCS音量指令。
这次证据支持 BLE 发现入口兼容性是原先小米配对失败的原因。
M33已烧录镜像SHA256：B79CDA2DD63419985FBCC7876496085CE2E00D80C99BCA169450E8535A151149。
成功关联/出声已验证；长时间播放的丢帧、欠载与音质仍需独立验收。
扫描工具新增 --scan-only，避免诊断主机连接污染手机记录。

推送前合并远程32c7f093：纳入I2S停止完成通知和M55禁用C++配置。
其中a5f30f89将ISO写入长度改为total，实际total=1+sdu_len，不含长度头；
回归测试在123字节完整记录断言处失败。合并时恢复total+2并补清晰注释，
防止已验证的ISO帧尾修复被回退。合并后双核构建成功，协议和水位回归测试通过；
本节上板哈希仍指实际验证过的小米发现版本，不代表新构建的M55已重新烧录。

## 参考链接

- STM32CubeWBA / STM32WBA65I-DK1 / BLE_Audio_TMAP_Peripheral：
  STM32_WPAN/App/tmap_app.c，AudioUseCases/tmap/tmap.c。
- Bluetooth SIG BAP：Table 3.7 Unicast Server AD format。
  https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/BAP_v1.0/out/en/index-en.html
- Bluetooth SIG ASCS：ASE state layouts and ASE Control Point procedures。
  https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/23166-ASCS-html5/out/en/index-en.html
