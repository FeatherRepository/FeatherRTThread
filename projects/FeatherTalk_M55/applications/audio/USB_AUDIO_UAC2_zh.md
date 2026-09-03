# FeatherTalk USB Audio UAC2 设计与实板记录

## 1. 当前结果

`FeatherTalk_M55` 现在可以把开发板 Type-C 用户口切换为双向 USB Audio
Class 2.0 设备。Windows 使用系统自带 `usbaudio2.sys`，不需要产品私有驱动。

数据通路如下：

- 播放：USB Host -> UAC2 OUT `EP 0x02` -> 环形缓冲 -> RT-Thread `sound0`
  -> TDM0/I2S -> ES8388 DAC -> MD8002 -> 板载扬声器。
- 录音：双 PDM 麦克风 -> RT-Thread `mic0` -> UAC2 IN `EP 0x81`
  -> USB Host。

USB 设置页可以选择 USB 设备功能、查看输入/输出设备，并按驱动能力显示格式：

| 方向 | 产品设备 | 采样率 | 采样深度 | 声道 | 当前可修改性 |
| --- | --- | --- | --- | --- | --- |
| Host -> Board | `sound0` | 16/24/48/96 kHz | 16/24 bit | 2 | 采样率、深度可改 |
| Board -> Host | `mic0` | 16 kHz | 16 bit | 2 | PDM 驱动固定，控件置灰 |

`sound0` 的本地接口仍支持单声道，但 UAC Terminal 当前是固定双声道拓扑；不能把
同一个固定双声道 Terminal 的 alternate setting 伪装成单声道。若后续要求主机侧
可选择单声道，需要增加独立合法的单声道 USB topology/endpoint，而不是只改一个字段。

## 2. 双向控制模型

### 2.1 主机改变设备

CherryUSB 处理标准 UAC2 控制请求：

- Clock Source `SET_CUR/RANGE` 控制采样率；
- AudioStreaming `SET_INTERFACE` 选择 16-bit 或 packed 24-bit alternate；
- Feature Unit `CUR/RANGE` 控制 mute 和 volume；
- 主机播放开始后，第一包 OUT 数据把最终协商格式提交给 `sound0`，USB 设置页的
  500 ms 状态监控同步按钮、状态文本和本机持久化偏好。

Windows 枚举时会遍历所有合法格式。候选协商值单独保存，只有收到真实音频包才成为
“实际生效格式”，因此能力探测不会把本机选择错误覆盖成最后探测的 96 kHz/24-bit。

### 2.2 设备改变主机

从“设置 > USB”或“设置 > 音频”改变 UAC 输出格式时：

1. 停止 UAC endpoints，让工作线程关闭 `sound0`；
2. 由 RT-Thread Audio 驱动以一次事务配置 sample rate/depth/channels：同时更新
   TDM0 的 LRCK/BCLK/MCLK 和字长、ES8388 的 DAC 字长/单双速/MCLK 比率；ES8388
   寄存器必须读回一致，任一步失败都恢复上一组物理格式；
3. 成功后更新设备偏好和 UAC 当前 Clock 值；
4. 增加 `bcdDevice` 并软重新枚举，要求 USB Host 重新读取能力和当前 Clock；
5. Windows 下一次真正打开播放流时，以其最终选择回写设备。

USB 规范没有“设备强制修改主机操作系统默认音频格式”的通用命令。这里保证的是标准
UAC 能力刷新、Clock/alternate 双向协商和实际流格式一致，不伪造对 Windows 控制面板
私有设置的直接控制。

## 3. 并发与缓冲

- USB 类回调可能运行在中断上下文，只更新原子/临界区状态并投递 RT-Thread event；
  不在回调中获取 mutex、访问 I2C codec 或重新配置 TDM。
- 输出、输入各有常驻工作线程。格式、音量和 mute 都在工作线程应用。
- OUT 使用 16 KiB 环形缓冲；读取按 `channels * bytes_per_sample` 整帧对齐，避免
  24-bit stereo 的 6 字节帧被 2048 字节工作块截断。
- IN 端点在 16 kHz、16-bit、stereo 的 64 B/ms 有效载荷上声明额外一个 stereo
  sample frame 的 packet 裕量（68 bytes）。Windows UAC2 需要该时钟漂移余量；精确
  声明 64 bytes 会使 `usbaudio2.sys` 以 `STATUS_RANGE_NOT_FOUND` 拒绝启动。

## 4. 使用与诊断

设置页采用两层结构：

1. “设置 > USB”用总开关控制整个 USB Device stack；关闭后执行
   `ft_usb_set_function(FT_USB_FUNCTION_NONE)`，会真正 deinitialize 端点并停止枚举，
   不是只隐藏 UI 状态；
2. 角色区显示 Device/Host。当前硬件只能选择 Device，Host 因没有受控 5 V VBUS
   source 而保持禁用；
3. Device 功能区用单选项选择 Storage 或 USB Audio。USB 关闭时只改变待启动功能；
   USB 已开启时选择另一项会停止旧 class、再启动新 class；
4. 每个功能右侧的属性入口分别进入 MSC 双 LUN 属性页或 UAC2 输入/输出属性页，
   主页面不再混放音频格式和存储介质细节。

总开关和功能单选不是纯 UI 状态：开启、关闭和运行中换类都读取驱动返回值并刷新
`ft_usb_status_t`。关闭后的可观测状态必须为 `function=none / active=0 / luns=0`；
再次开启时使用此前选中的 Device 功能。Host 选项仅表达未来能力边界，当前不能被选中，
也不会伪造 Host stack 或 VBUS 状态。

MSH 命令仍可直接控制相同驱动状态：

```text
feather_usb audio
feather_usb status
feather_usb format 48000 24 2
feather_usb stop
```

`status` 显示连接/枚举、两个 streaming 状态、格式、主机/设备更新次数、传输 KiB、
overrun/underrun 和最后错误。

`feather_i2s_diag` 额外显示 `sound0` 已提交格式、格式提交/失败/回滚计数，以及从
ES8388 I2C 寄存器实时读回的采样率、字长和 MCLK/Fs 比率，用于排除“USB/UI 已变、
外部 DAC 仍是旧配置”的假同步。

## 5. 2026-08-31 实板验证

- M55 clean/incremental 构建通过，最终链接只有仓库既有 RWX LOAD segment 警告；
- Infineon Customized OpenOCD 5.19.0.4782 + KitProg3
  `0D141868022E2400` 对 M55 XIP 镜像写入并 verify 通过；
- Windows 11 初次识别为 `FeatherTalk Bidirectional USB Audio`，早期 descriptor 的
  IN packet 没有时钟裕量，PnP 为 `CM_PROB_FAILED_START / 0xC000028C`；
- 修正为 68-byte IN max packet 后，`USB Audio 2.0` 的 MEDIA 节点为
  `CM_PROB_NONE`，复合 USB 父设备同样为 `CM_PROB_NONE`；
- 抓取到 Windows 的 Clock/Feature Unit RANGE、CUR、SET_INTERFACE 请求；输出流首次
  打开 `EP 0x02` 时发现并修正了“ISR 内获取 mutex”的 RT-Thread 断言；
- 修正后连续枚举无断言，恢复 4 个采样率和 16/24-bit alternate 后 Windows 仍为
  `CM_PROB_NONE`；设备侧切换到 48 kHz/24-bit 会重新枚举并保持主机驱动正常。
- 最终双向同步版本构建为 text=3,603,836、data=91,000、bss=4,271,000 字节，HEX
  10,392,778 字节，SHA-256
  `F1521B5E9DC6297097AE5BC7E189AEA191DA2C888885A20F23C23EDAA0368C65`；Customized
  OpenOCD 写入 3,698,688 字节并校验 3,694,836 字节。
- 最终板测先保持 USB 关闭完成 UI 回归，再执行 `feather_usb audio` 和
  `feather_usb format 48000 24 2`。Windows 重新枚举后板端状态为
  `connected=1 / configured=1 / error=0`，输出仍为 48 kHz、24-bit、2-channel，
  输入为驱动固定的 16 kHz、16-bit、2-channel，两个 stream 均空闲、pending=0；
  Windows MEDIA 节点为 `OK / CM_PROB_NONE`。这证明设备主动格式与主机枚举探测值
  已经分离，重枚举不会再把设备设置误改成最后一个被探测的格式。
- UI 全量自动化最终为 `349 PASS / 3 FAIL / 163 actions`。USB、音频、录音、页面
  生命周期和 UAC 控件均通过；3 个失败是文件管理器在固定 Flash 上执行写入合约时
  M33 XIP park 握手超时（`M33 XIP park failed before erase: -116`），与 UAC 描述符、
  Audio 驱动或 USB 枚举无关。
- 追加外部 DAC 同步验证：`48 kHz / 16-bit / stereo` 时 `sound0` 与 ES8388 读回
  一致，MCLK 为 `128*Fs`；`96 kHz / 24-bit / stereo` 时两者仍一致，MCLK 为
  `256*Fs` 且 ES8388 已进入 double-speed。连续 17 次格式提交为 0 失败、0 回滚失败。

## 6. 尚未完成的边界

- 当前 Codex 进程所在 Windows 会话只暴露“远程音频” MMDevice endpoint，无法在该
  会话直接运行 WASAPI 播放/录音压力测试；PnP、class-control 和 endpoint 打开已经
  验证，最终音质、长时间漂移与真实数据吞吐仍需在本机交互音频会话继续量化。
- UAC OUT 当前没有显式 feedback endpoint。需要在长时间播放中测量环形缓冲水位、
  overrun/underrun，再决定加入 asynchronous feedback 或自适应重采样。
- `mic0` PDM 驱动尚不支持动态采样率/深度/声道，所以输入格式不能修改。
- UAC 与 Recorder/Media 同时占用同一个 RT-Thread Audio device 的完整仲裁策略仍需
  补充；当前格式切换会先停止 USB stream，但尚无跨应用优先级策略。
