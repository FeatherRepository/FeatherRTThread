# FeatherTalk 可移动存储接入

## 当前状态

SD 卡第一阶段已经接通真实硬件链路，不是 UI 模拟数据：

1. 板级 `SDHC1` 以 4-bit 模式连接卡槽，RT-Thread MMC/SD 层负责枚举卡与分区。
2. SDK 公共文件系统服务 `libraries/Common/board/ports/filesystem/mnt.c` 监听插拔事件，自动把首个可识别 FAT 卷挂载到 `/sdcard`；拔卡时自动卸载。
3. 文件名使用 FatFS UTF-8 长文件名模式，最大长文件名为 255 字符。
4. FeatherTalk 产品层 `feathertalk_storage.*` 只依赖 DFS/POSIX 接口，提供卷状态、目录枚举、路径拼接、返回上级和文件头预览，不直接操作 SDHC 寄存器。
5. “文件”应用显示真实挂载状态、总容量、可用空间、文件夹数和文件数；先列文件夹、再列文件。点击文件夹进入，导航 Back/“上一级”返回；点击普通文件时，文本文件预览开头 383 字节，二进制文件只显示真实大小，避免把任意二进制内容当文本渲染。
6. 页面打开期间每 500 ms 只检查挂载状态；只有插拔状态变化、进入目录或手动刷新时才重建列表，不在每帧扫描介质。

当前 FatFS 配置支持 FAT12/FAT16/FAT32。没有启用 exFAT，因此 exFAT 卡不会被伪装成可用；需要先在电脑上格式化为 FAT32。`sdcard_mkfs` 会破坏卡上数据，只能由用户明确执行，产品 UI 不提供自动格式化。

## 调试命令

```text
ls /sdcard
sdcard_umount
sdcard_mount
sdcard_mkfs
```

拔卡前可执行 `sdcard_umount`。重新插入后正常路径由卡检测中断与热插拔线程自动恢复；`sdcard_mount` 只用于诊断。当前 SDK 的 MSH 没有实现 `df`，容量由文件应用和 System 页直接通过 `statfs` 获取。

## 2026-08-30 板端验收

- SDHC1 识别到 `30,591,488 KiB` 卡，分区探测为 256 MiB 与 28.930 GiB。
- Elm-FatFS 自动把设备 `sd` 挂载到 `/sdcard`。
- 真实目录读取到 `extlinux/`、`rockchip/` 和 50,924,032 字节的 `Image` 文件。
- 安全卸载后手动重新挂载成功，同一目录再次读取一致。
- UI 全量自动化为 `262 PASS / 0 FAIL / 120 actions`；新增检查覆盖文件页路径、状态、上一级、刷新、列表、挂载监视器以及页面销毁后的对象/定时器释放。
- 最终构建镜像 text/data/bss 为 `3,233,976 / 5,048 / 4,312,356` 字节，HEX 为 9,110,678 字节，SHA-256 为 `D90DCA04A1A335098CECF14D32A263810A2F3C4A86FD727AB97054C976D39F61`；OpenOCD 写入 3,244,032 字节并校验 3,239,024 字节。

## USB 第二阶段设计

文件应用不会为 USB 复制另一套目录浏览器。下一阶段以 CherryUSB **Host MSC** 接入 U 盘，枚举后的块设备仍通过 Elm-FatFS/DFS 暴露，存储层只增加一个 USB 卷描述符，文件应用首页再显示“SD 卡”和“USB 存储”两个真实卷。

SDK 已有 M55 USB RoleSwitch 示例和 `platform/rtthread/usbh_dfs.c`，能把 MSC 注册为 `/dev/sd%c` 并在断开时卸载。产品接入前仍需完成以下工作：

1. 确认 Type-C 口在 Host 模式下的 VBUS 供电、角色切换和过流保护路径。
2. 产品配置选择 `BSP_USB_ROLE_HOST_MSC`，启动 `usbh_initialize(0, USBHS_BASE, NULL)`；单控制器同一时刻不能同时宣称 CDC Device 与 MSC Host。
3. 将默认的根目录挂载策略改成产品明确的 `/usba`、`/usbb`，并在 ROMFS 根目录预留挂载点，不能覆盖当前 `/` 或 `/sdcard`。
4. 复核 CherryUSB RT-Thread 适配层在读写失败时的 DMA 对齐缓冲释放，并为连接/断开、挂载失败和多 LUN 增加状态回调。
5. 接入同一文件应用和 System 信息页，再做真实 U 盘插拔、目录遍历、长文件名、断开时正在浏览、读失败恢复和全量 UI 回归。

在以上硬件与适配层检查完成前，System 页继续把 USB 标为未启用，避免把“SDK 有示例”误报成“产品 USB 已完成”。
