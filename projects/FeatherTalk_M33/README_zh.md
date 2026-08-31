# FeatherTalk M33 产品工程

**中文** | [**English**](./README.md)

## 简介

本产品工程由 **Edgi_Talk_M33_Template** 派生，在 Cortex-M33 核上运行 **RT-Thread** 产品控制应用。
它用于完成板级初始化、启动 Cortex-M55，并继续作为产品控制核、IPC 监督核和板载
CYW55500A1 蓝牙 Host 运行；未被产品显式接入的可选外设 demo 仍默认关闭。

共享的 Secure M33 固件包位于：`libraries/components/infineon-pse84-secure-firmware-latest`，同时保留 Template 自己的最小 `.config`。

## 软件说明

* 工程基于 **Edgi-Talk** 平台开发。
* 已启用 `SOC_Enable_CM55`，板级初始化阶段会启动 M55 核。
* 已关闭 `SOC_Enable_CM33_DeepSleep`，确保 M33 启动 M55 后继续进入 RT-Thread 和产品 MSH。
* AHT20、LSM6DS3、Audio、ADC、RTC、SD 卡、文件系统、LCD、Wi-Fi 等可选外设 demo 均保持关闭。
* 板级初始化会将外部 Wi-Fi/音频电源控制脚拉低，避免 Template 默认打开外设电源。
* CYW55500A1 蓝牙已作为产品服务接入：启动时下载官方 PatchRAM，运行 AIROC BLE Host
  Stack，并提供扫描、设备列表和非连接广播 MSH 诊断。当前尚未接入 M55 UI、连接、
  GATT、SMP、Bond 和密钥持久化。
* 蓝牙构建依赖两个固定版本的 Infineon 子模块。首次克隆后执行
  `git submodule update --init --recursive`；完整过程见
  [蓝牙调试与上板记录](applications/bluetooth/BLUETOOTH_BRINGUP_zh.md)。
## 使用方法

### 编译与下载

1. 打开工程并完成编译。
2. 使用 **板载下载器 (DAP)** 将开发板的 USB 接口连接至 PC。
3. 通过编程工具将生成的固件烧录至开发板。

### 运行效果

* 烧录完成后，开发板上电即可运行示例工程。
* M33 初始化 RT-Thread，启动 M55，运行 IPC 监督器，并在 UART5 保留产品 MSH。
* Template 不会自动闪灯，也不会自动启动外设 demo。

## 注意事项

> **⚠️ 注意：** 本工程要求使用 **RT-Thread Studio 2.2.9** 或以上版本。

* M33 工程的串口日志不会通过板载 DAP 虚拟串口直接输出。查看 `msh />` 和 demo 日志时，需要额外连接 USB 转串口硬件，例如 CH340。
位置如图下所示，RX 接串口硬件的 TX，TX 接串口硬件的 RX，上位机波特率 115200：

![alt text](figures/m33_uart.png)

* 如需修改工程的 **图形化配置**，请使用以下工具打开配置文件：

```
tools/device-configurator/device-configurator.exe
libs/TARGET_APP_KIT_PSE84_EVAL_EPC2/config/design.modus
```

* 修改完成后保存配置，并重新生成代码。
* 同时更新两个镜像时，先烧写 **FeatherTalk_M55**，最后烧写已签名的 **FeatherTalk_M33**；最后一次 M33 复位会完成板级初始化并启动 M55。
* 在 M33 MSH 使用 `feather_status` 和 `feather_ping` 查看或探测 M55；使用
  `bt_status`、`bt_scan`、`bt_scan_stop`、`bt_devices` 和 `bt_adv on|off` 验证蓝牙。

## 启动流程

系统启动顺序如下：

```
+------------------+
|   Secure M33     |
|   (安全内核启动)  |
+------------------+
          |
          v
+------------------+
|       M33        |
|   (非安全核启动)  |
+------------------+
          |
          v
+-------------------+
|       M55         |
|  (应用处理器启动)  |
+-------------------+
```

⚠️ 请严格按照以上顺序烧写固件，否则系统可能无法正常运行。

---

* 若 M55 应用无法正常运行，请先编译并烧录 **FeatherTalk_M33**，确保初始化与核心启动流程正常。
* 若要开启 M55，需要在 **M33 工程** 中打开配置：

  ```
  RT-Thread Settings --> 硬件 --> select SOC Multi Core Mode --> Enable CM55 Core
  ```

