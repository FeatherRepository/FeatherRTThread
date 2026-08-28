# Edgi-Talk 串口监控工具

这是从旧 ABW 测试工程迁移并解耦的独立串口终端，现已按 Edgi-Talk 工程调整。它不依赖系统 Python，可直接使用随工具复制的 CPython 3.14.7 和 `pyserial==3.5`。

迁移来源：

- 原目录：`D:\ABW\code\ABW_SDIO_WIFI\ultimately\tools\serial-monitor`
- 原始来源归档 SHA-256：`4CC4D6A5E77161F32D37F5682CFCB4F374BC34F2DB55A8F9461B3847A47922FF`
- 保留功能：枚举串口、后台接收、交互发送、UTF-8 容错、文本/事件日志、定时监听和无硬件自检
- 未迁移内容：旧工程历史日志、Wi-Fi 测试业务、pytest 用例、路由器控制和 IDE 文件

## 快速使用

在 `D:\Develop\Edgi-Talk` 下运行：

```powershell
.\tools\serial-monitor.cmd --list
.\tools\serial-monitor.cmd --port COM5
.\tools\serial-monitor.cmd --port COM5 --timestamps
```

Edgi-Talk 工程文档中的调试串口为 115200 8N1，因此本移植版本默认使用 115200 波特率。其他固件可显式覆盖：

```powershell
.\tools\serial-monitor.cmd --port COM5 --baud 1500000
```

交互模式下，每行默认追加 CRLF；输入 `~.` 或按 Ctrl+C 可安全退出。

## 日志

默认日志写入 `tools\serial-monitor\logs\<port>-<timestamp>.log`。默认 `text` 格式仅连续保存设备 RX 文本，并保留串口原始的 CR、LF 或 CRLF：

```powershell
.\tools\serial-monitor.cmd `
  --port COM5 `
  --log .\tools\serial-monitor\logs\m33-session.log
```

`--timestamps` 只影响终端显示，不会写入 `text` 日志。需要检查分片、非法字节或区分 RX/TX 时，可使用事件日志：

```powershell
.\tools\serial-monitor.cmd `
  --port COM5 `
  --log-format event `
  --log .\tools\serial-monitor\logs\m33-events.log
```

只监听 60 秒后自动停止：

```powershell
.\tools\serial-monitor.cmd --port COM5 --no-input --duration 60
```

启动后自动发送 RT-Thread 命令：

```powershell
.\tools\serial-monitor.cmd --port COM5 --send "version" --send "list_thread"
```

## 自检与并行调试

无需串口硬件即可验证运行时、pyserial、loopback 收发、UTF-8 分片解码和日志换行：

```powershell
.\tools\serial-monitor.cmd --self-test
```

串口和调试探针是不同接口，可以在两个终端分别运行：

```powershell
# 终端 1：调试/下载
.\tools\openocd.cmd --version

# 终端 2：串口监控
.\tools\serial-monitor.cmd --port COM5
```

实际 COM 口先用 `--list` 确认；同一 COM 口不能被两个串口程序同时占用。
