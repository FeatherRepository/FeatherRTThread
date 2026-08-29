# FeatherTalk 双核构建与上板

## 核心职责

- M33：安全启动后的系统控制核，负责板级初始化、启动 M55、IPC 监督和产品 MSH。
- M55：图形应用核，负责 LVGL、LCD、触摸和后续多媒体业务。
- M33 UART5：产品控制/MSH 终端，115200 8N1。
- M55 UART2：图形核诊断终端，115200 8N1；不作为主产品命令入口。

M33 使用 SDK 原生 `Cy_SysEnableCM55()` 从 `0x60580400` 启动 M55。两核使用
SDK 的 PSoC E84 IPC Pipe 驱动交换 16 字节的 FeatherTalk ABI 消息，不需要外部连线。

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

## 运行验证

M33 终端预期出现：

```text
FeatherTalk M33 0.1.0-dev
[FeatherTalk M33] M55 online: ABI=1 status=...
msh />
```

在 M33 MSH 执行：

```text
feather_status
feather_ping
```

`feather_status` 同时显示 CM55 硬件状态、应用层心跳、IPC 驱动收发计数和忙重试计数。
M55 终端可用 `feather_m55_status` 查看 LVGL 与 IPC 状态。
