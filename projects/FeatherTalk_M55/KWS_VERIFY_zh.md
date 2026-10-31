# 「小麦小麦」唤醒词上板验证指南（v9 固件 / M55 CM55）

> **2026-10-19 最终版**。验证状态：**✅ 已通过**，且命中副作用 / 开机自启 /
> M33 联动已实现并联测通过（见 §1.2）。boot loop 真因 = 3.3V 供电不足（§4）。
> **COM9（KitProg3 USB-UART）就是 M55 uart2 控制台，无需任何外接适配器。**

---

## 1. 验证结果（已通过 ✅）

前置条件：**板子必须由带外接电源的 USB Hub 或独立 5V 供电**（见 §4，否则会 boot loop）。

### 1.1 本地模型全链路（2026-10-18/19 实测）

COM9（115200）主动发命令 → M55 `msh />` 正常响应，实测：

```
msh /> kws start
[I/kws] KWS 启动: model=rbb666-project-1 sr=16000 win=1000ms hop=500ms 阈值=50% 标签="xiaomai"
[I/kws]   label[0] = noice
[I/kws]   label[1] = xiaomai
[I/kws] 麦克风已打开（mic0）
[I/kws] 开始监听唤醒词...
[W/kws] *** 唤醒词命中: xiaomai (99%, 2/3窗) ***
[W/kws] 唤醒命中（默认回调）: xiaomai (99%)
[FeatherTalk M55] tx=49 rx=161 err=0 local=0x0000000e peer=0x00000015

msh /> kws info
[I/kws] ==== KWS 状态 ====   监听: 运行中   唤醒词: "xiaomai"（阈值 50%）
[I/kws] 窗口: 1000ms / hop 500ms   模型: rbb666-project-1 (sr=16000Hz, 1000ms/窗)
[I/kws] 推理: 6 次，命中: 1 次，唤醒词最近分数: 99%
[I/kws] 平均耗时: DSP 52 ms / NN 30 ms
```

日志原件：`kws_start.log` / `kws_probe.log` / `KWS_VERIFIED_2026-10-19.log`（本目录）。
**结论：M55 + 自训 Edge Impulse 格式的本地唤醒词「小麦小麦」全链路打通**
（供电 → M55 启动 → 模型加载 → PDM 麦克风 → 滑窗推理 → 命中回调）。

### 1.1 复测（2026-10-19，25 秒连续抓包）

判定"不是偶发误触"的复测：用户现场连续喊话，COM9 抓包 25s，**命中 4 次**，
top1 全程为 `xiaomai`：

```
[I/kws] #100 dsp=52ms nn=31ms top1=xiaomai(99%) top2=noice(0%)
[W/kws] *** 唤醒词命中: xiaomai (37%, 2/3窗) ***
[W/kws] 唤醒命中（默认回调）: xiaomai (37%)
[I/kws] #120 dsp=52ms nn=31ms top1=xiaomai(69%) top2=noice(30%)
[W/kws] *** 唤醒词命中: xiaomai (58%, 2/3窗) ***
[W/kws] 唤醒命中（默认回调）: xiaomai (58%)
[I/kws] #140 dsp=52ms nn=31ms top1=xiaomai(99%) top2=noice(0%)
[W/kws] *** 唤醒词命中: xiaomai (95%, 2/3窗) ***
[W/kws] 唤醒命中（默认回调）: xiaomai (95%)
[I/kws] #160 dsp=53ms nn=30ms top1=xiaomai(86%) top2=noice(13%)
[W/kws] *** 唤醒词命中: xiaomai (54%, 2/3窗) ***
[W/kws] 唤醒命中（默认回调）: xiaomai (54%)
```

推理计数 #100→#180 连续推进，IPC 全程健康（`err=0`），实时性 **DSP 52ms / NN 30ms**。
**结论：稳定可重复，非偶发。**

### 1.2 命中副作用 / 开机自启 / M33 联动（2026-10-19 新增，实测通过）

v9+ 之后新增三项产品化能力，均已联测：

| 能力 | 实现 | 实测证据 |
|---|---|---|
| **开机自启** | `kws.h: KWS_AUTOSTART=1`，`main.c` 在 IPC/UI 就绪后调用 `kws_start()` | boot 日志：`[kws] 开机自启完成，对着麦克风喊「小麦小麦」`，随后自动开麦监听——**无需手动敲命令** |
| **IPC 上报 M33** | 命中回调 `kws_detected_default()` → `feathertalk_ipc_post_kws_event()`（待发槽，IPC 线程代写帧）→ M33 `FEATHERTALK_IPC_MSG_KWS_EVENT` 处理器 | `[FeatherTalk M55] KWS 唤醒已通知 M33: label=1 conf=99% (第 N 次)` |
| **M33 回执** | M33 收到唤醒事件后经 EVENT 通道回传码 `0x4B57('K''W')` | `[M33 evt 19287 @...ms]`（**19287=0x4B57**，延迟约 11ms；uart5 不在手边也能看到端到端证据） |
| **绿灯快闪** | `feathertalk_m55_wake_flash()`：P16_6 绿灯 100ms 快闪 1s（区分于 500ms 心跳） | OpenOCD 轮询 `s_led_wake_flash`(0x200203bc)：触发后 `1` → 1s 后自动 `0` → 环境再次命中重新 `1` → `0`，生命周期完整 |
| **空闲零误触** | — | 安静环境 30s 抓包：top1 全程 `noice(99%)`，无命中 |

协议变更（`FeatherTalk_Common/include/feathertalk/ipc_protocol.h`）：
新增 `FEATHERTALK_IPC_MSG_KWS_EVENT = 11` + `feathertalk_ipc_kws_event_t`（16B，含
label_id/confidence_pct）。旧固件收到未知 ID 直接忽略，**向后兼容，无需 bump ABI**。

> **已知局限（实测记录）**：M33 的 EVENT 事件队列深 16，BT bring-up 期间 tracing
> 事件爆发会把唤醒回执挤掉（丢帧，幂等无害）；唤醒词在环境人声嘈杂时会以
> 40~90% 命中（正常对话中的"mai"类音节），可 `kws conf 60` 提高阈值、
> `kws cool 10000` 拉长冷却。

### 1.3 软件自测命令（无人在场回归用）

```
kws testevt    # 合成一次 99% 唤醒事件，走与真实命中完全相同的链路
               # （默认回调 -> IPC 上报 -> M33 -> 回执 19287），不必喊话
```
实测输出：`[自测] 合成唤醒事件 -> IPC 上报 M33` → `唤醒命中（默认回调）: xiaomai (99%)`
→ `KWS 唤醒已通知 M33: label=1 conf=99% (第 N 次)` → `[M33 evt 19287 @...ms]`。

## 2. 串口映射与"必须主动发命令"

| 口 | 实际承载 | 用法 |
|---|---|---|
| **COM9**（KitProg3 USB-UART） | **M55 uart2 控制台 = KWS 终端** | 验证 KWS 走这里，**不需要外接适配器** |
| COM15（CH343） | ESP32-S3「小智」设备（**与本 PSE84 板无关，按要求不再管**） | 见 §6 |

**踩坑要点（本文件的初版就是栽在这里）**：判定一个口是不是控制台，**不能靠"被动听有没有输出"**。
RT-Thread 的 `msh` 空闲时不输出任何东西，被动读必然收到 0 字节。正确做法是
**拉低 DTR/RTS 复位后主动 `\r\n` 发命令**再判读（现成脚本：`worklog/scripts/boot_capture.py`、
本目录 `kws_start.py` / `kws_probe.py`）。

## 3. 手动验证步骤（复现用）

1. **确认供电**：带外接电源的 USB Hub 或独立 5V（§4）。
2. COM9 打开 115200/8N1，敲回车 → 应出现 `msh />`；若看到同一位置反复重启 → 先修供电，不要查代码。
3. **无需 `kws start`**——开机自启已在 boot 日志打印 `[kws] 开机自启完成` 并自动开麦。
   确认可执行：
   ```
   kws info        # 期望：监听=运行中、唤醒词 "xiaomai"、阈值 50%、模型 rbb666-project-1
   kws testevt     # 无人自测：合成 99% 事件走完整上报链路（应见 [M33 evt 19287]）
   kws mic 3       # 可选：免 SD 麦克风实测，说话时 RMS 电平应明显跳起
   ```
4. 对板载 PDM 麦克风清晰喊 **「小麦小麦」**（正常音量、两个音节）。
5. 期望三连：
   ```
   [W/kws] *** 唤醒词命中: xiaomai (XX%, 2/3窗) ***
   [FeatherTalk M55] KWS 唤醒已通知 M33: label=1 conf=XX% (第 N 次)   ← IPC 上报
   [M33 evt 19287 @...ms]                                            ← M33 回执（0x4B57）
   ```
   同时板载绿灯（P16_6）快闪 1s。参考口径（10-17 真人实测）：
   清晰喊 85~90%、较轻/较远 25~55%、环境音 0~8%；**阈值 50%**，命中后 5s 冷却；
   `2/3窗` = 3 个滑窗里 2 个过阈值。
6. `kws stop` 释放麦克风给其他音频功能（之后要恢复监听再 `kws start`）。

## 4. boot loop 的真因：3.3V 供电不足（不是代码）

v9 烧录后 M55 反复"启动到 SAL 那行 print 就复位"，`CFSR=0 / HFSR=0` 全程无 fault，
halt 停在 `Cy_DelayCycles_loop` 忙等。**换上带外接电源的 Hub / 独立 5V 后立即正常启动进 `msh`。**

机制：外部 Flash（S25FS128S）所在的 **3.3V XIP 供电轨在负载下跌落 → 取指间歇失败**。
为什么软件/调试侧查不出来：KitProg3 只能量到 VTarget=1.837V（1.8V 域正常），
**BOD 也只监控 1.8V**，`RES_CAUSE=0` 无欠压记录 → 3.3V 轨跌落对软件完全隐形。

排查过程与逐项排除的假因清单见**外层归档仓**的
`git_edgi_talk/local_ai/firmware/CHANGES.md`（10-18 小节，不在本代码仓内）。

> **长期约定**：遇到"跑到一半崩 / CFSR=0 无 fault / 无欠压记录 / 换代码无效"的重启循环，
> **先查供电，再查代码**。

## 5. 固件与调试事实（实测，供后续接手）

| 项 | 结论 | 证据 |
|---|---|---|
| CM55 是否被 M33 释放 | **是** | `AP_CTL@0x441C1000 = 0x00000031`，bit0 `CM55_0_ENABLE` 置位 |
| CM55 调试域 | **可附着**：`set ENABLE_CM55 1` + `adapter speed 1000`；默认配置下 Examination 会失败（`Failed to read memory`） | OpenOCD 实测 |
| CM55 硬件断点 | **不可用**（`resource not available`，FPB 未开）→ 抓卡点只能靠 halt 读 PC + 加 print | OpenOCD 实测 |
| 模型是否真进固件 | **是**（ELF 含 `xiaomai`、无 `xiaorui`） | `rt-thread.elf` 二进制检查 |
| KWS 是否自启 | **是**（`KWS_AUTOSTART=1`，main 在 IPC/UI 就绪后自启；`-DKWS_AUTOSTART=0` 可关） | `kws.h` + `main.c` |
| 命中时做什么 | **三件事**：`LOG_W` 打印 + IPC 上报 M33（EVENT 回执可见）+ 绿灯快闪 1s | `kws_detected_default()` |
| KWS 自测命令 | **`kws testevt`**：合成 99% 事件走完整上报链，无人可回归 | `kws.cpp` |
| 蓝牙是否受影响 | 不受影响（仅双镜像烧录 + 软件复位，未碰蓝牙代码） | — |
| M33 侧唤醒处理 | `applications/ipc/feathertalk_ipc.c`：收 `KWS_EVENT` → 打印 + 计数（`feather_status` 可看）+ 回传 `0x4B57` | M33 代码 |

## 6. 另一路（**不属于本项目**，仅作环境事实）

COM15 的 ESP32-S3 跑的是「小智 XiaoZhi」固件，自定义唤醒词已配置为「小麦小麦」
（`CustomWakeWord: Command: xiao mai xiao mai`），10-18 实测喊话可唤醒并进入多轮对话。
**但那是 ESP32-S3 的 ESP-SR 唤醒 + 云端对话，与 PSE84 M55 的本地推理是两条独立实现，
不满足本项目"完全本地、不联网"的目标，按要求不再跟进。**
