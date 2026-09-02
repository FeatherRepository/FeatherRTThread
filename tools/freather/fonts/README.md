# FeatherTalk 中文字体构建

## FeatherUI（当前产品固件）

`FeatherTalk_M55` 当前使用 FeatherUI，不再链接 LVGL 字体。普通构建会先运行
`build-featherui-font.js`：脚本扫描产品场景中的 UTF-8 文案，从官方 Noto Sans SC
离线生成实际用到字符以及完整可打印 ASCII 的统一 A8 atlas，并且仅在内容变化时更新
`libraries/FeatherUI/src/fui_font_product_data.c`。因此产品字符数量由当前文案动态产生，
不能在代码中作为人工维护的构建输入；当前仓库产物为 398 个字形（95 ASCII + 303
CJK）、129,600 字节 A8 数据。固件只常驻一套 18 px 母版，正文和大标题由 GPU
双线性缩放，避免低分辨率点阵锯齿，也避免三套字体同时占用有限 GFX 内存。

```powershell
node tools\freather\fonts\build-featherui-font.js
tools\freather\build-demo.ps1 -Project FeatherTalk_M55
```

如果主机没有 Node 或本地字体转换器缓存，构建脚本会明确告警并使用仓库中已生成的
atlas，不会联网下载或在目标固件中解析 TTF。

## LVGL + VG-Lite 矢量字体

当 `FeatherTalk_M55` 选择 `FEATHERTALK_USING_UI_SHELL` 时，构建入口自动运行
`build-lvgl-vector-font.js`。它从同一份官方 Noto Sans SC 提取完整 GB2312、可打印
ASCII 和项目源码中的额外字符，生成一份规范化轮廓。12/14/16/22 px 四档字体共享
轮廓，只保留各自字号度量和 GPU 缩放矩阵。

目标端第一次遇到字形时把紧凑的离线命令展开成不可变 `lv_vector_path_t`，VG-Lite
后端再生成一次原生路径并上传 GPU 可寻址内存；后续帧仅提交颜色、平移、缩放和
`CALL`，不再栅格化 TTF、拷贝 A8 位图或重复翻译路径。

## 旧 LVGL 字体流程（保留供历史工程使用）

UI 使用 Google Fonts 官方发布的 **Noto Sans SC** TTF 作为构建源，使用
`lv_font_conv 1.5.3` 在主机上离线生成 LVGL C 字体。固件不解析 TTF，也不携带
完整 TTF。固件字库包含 GB2312 一级、二级的全部 6763 个简体中文汉字，并额外
合并当前 UI 源码中的中文标点、全角字符和 GB2312 以外字形。

运行：

```powershell
tools\freather\fonts\build-ui-fonts.cmd
```

- 官方源：`google/fonts/ofl/notosanssc/NotoSansSC[wght].ttf`
- 固定 SHA-256：`A3041811A78C361B1DE50F953C805E0244951C21C5BD412F7232EF0D899AF0DA`
- 许可证：SIL Open Font License 1.1，见 `OFL-NotoSansSC.txt`
- 本地缓存：`cache/`，被 Git 忽略，可随时重建
- 固件产物：`projects/FeatherTalk_M55/applications/ui/assets/generated/`

输出为 12、14、16、22 px、2 bpp、无 RLE 压缩字体。2 bpp 在 RGB565 面板上保留
抗锯齿，同时把四档完整基础字库控制在 M55 的 8 MiB XIP 分区内。ASCII 与 LVGL
符号由对应尺寸的 Montserrat fallback 提供。新增或修改中文界面文案后仍应重新
运行本脚本，以合并基础字库之外的字符，再构建 M55 固件。
