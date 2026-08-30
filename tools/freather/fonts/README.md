# FeatherTalk 中文字体构建

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
