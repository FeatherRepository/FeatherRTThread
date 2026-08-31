# FeatherTalk 双核构建与上板

## 核心职责

- M33：安全启动后的系统控制核，负责板级初始化、启动 M55、IPC 监督和产品 MSH。
- M55：图形应用核，负责 LVGL、LCD、触摸和后续多媒体业务。
- M33 UART5：产品控制/MSH 终端，115200 8N1。
- M55 UART2：图形核诊断终端，115200 8N1；不作为主产品命令入口。

M33 使用 SDK 原生 `Cy_SysEnableCM55()` 从 `0x60580400` 启动 M55。两核使用
SDK 的 PSoC E84 IPC Pipe 驱动交换 16 字节的 FeatherTalk ABI 消息，不需要外部连线。

M33 的 Non-Secure 主程序与 M55 主程序都在同一颗 SMIF0 NOR Flash 上执行。M55
访问末尾固定 2 MiB `/flash` 用户卷前，必须通过共享 SRAM 守卫让 M33 停在内部
SRAM；擦写完成且器件确认 ready 后，M33 先失效外部 `ICACHE0` 再返回 XIP。任何
握手或 ready 检查失败都按 fail-closed 处理，不允许 M33 带着旧 XIP cache 继续运行。

## 构建

在 SDK 根目录执行：

```powershell
.\tools\freather\build-demo.cmd FeatherTalk_M55 -Clean
.\tools\freather\build-demo.cmd FeatherTalk_M33 -Clean
```

产物：

- `projects/FeatherTalk_M55/rtthread.hex`：M55 XIP 镜像；入口 `0x60580400`。
- `projects/FeatherTalk_M33/build/rtthread.hex`：Secure M33 与 Non-Secure M33
  签名、重定位和合并后的最终镜像。

## 烧录

更新双核时必须先写 M55，最后写 M33 并由 M33 的复位流程启动 M55：

```powershell
.\tools\freather\flash-feathertalk.cmd
```

脚本使用 Infineon OpenOCD，分别擦写和校验两个 HEX。PyOCD 暂不用于本板的
SMIF 下载，因为当前 CMSIS Pack 没有自动启用非默认的 PSE84 SMIF FLM。

`flash-demo.ps1` 会检查 PSE84 `reset_halt` 的 Tcl 布尔返回值。只有普通 helper
明确返回 0 时才允许执行受约束的 XRES/vector-catch fallback；fallback 还必须确认
CM33 已停机、处于 Secure 域、PC 位于 Secure boot RRAM 且未停留在 Test Mode。
上述任一条件或写入后 Non-Secure reset-halt 失败时，脚本都会停止，不会把未知状态
误报成“烧录成功”。

## 运行验证

M33 终端预期出现：

```text
FeatherTalk M33 0.2.0-dev
SMIF XIP guard service ready at 0x240fffc0
[FeatherTalk M33] M55 online: ABI=4 status=...
msh >
```

在 M33 MSH 执行：

```text
feather_status
feather_ping
```

`feather_status` 同时显示 CM55 硬件状态、应用层心跳、IPC 驱动收发计数和忙重试计数。
M55 终端可用 `feather_m55_status` 查看 LVGL 与 IPC 状态。

2026-08-31 真机全量 UI/存储回归为 `326 PASS / 0 FAIL / 150 actions`；其中
`files.delete.contract`、`files.clipboard.contract`、`files.name.contract` 均实际
擦写 `/flash` 并通过，过程中未再出现 `M33 XIP park failed`、HardFault 或复位。

## 蓝牙当前状态

CYW55500A1 的官方 PatchRAM 与 AIROC Host Stack 已在 M33 打通。冷启动会以 3 Mbit/s
下载 126,951 字节、519 条 HCD 记录，随后以 115200 bit/s 运行；真机 Host 状态为
`ready/error=0`，启动扫描为 18 reports / 11 unique，运行期复扫为 19 / 11，非连接
广播开关通过。

当前真实入口是 M33 MSH 的 `bt_status`、`bt_scan`、`bt_scan_stop`、`bt_devices` 和
`bt_adv on|off`。M33 尚未通过 IPC 向 M55 发布真实蓝牙能力与扫描列表，因此 UI 仍应
显示不可用；配对、加密连接、Bond、密钥持久化和 GATT 也尚未完成。完整调试过程、
问题根因、复现方法和后续计划见
[BLUETOOTH_BRINGUP_zh.md](../FeatherTalk_M33/applications/bluetooth/BLUETOOTH_BRINGUP_zh.md)。
