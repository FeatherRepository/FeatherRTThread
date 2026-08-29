# FeatherTalk UI 资源策略

本策略面向 480×800、RGB565 显示链路和 LVGL 9.2。资源先经过筛选再进入产品工程，禁止直接复制参考仓库的全部图片目录。

## 格式选择

- 产品图标以 `assets-src/` 下的单色 SVG 为可编辑源，构建前确定性转换为 `LV_COLOR_FORMAT_A8`，每像素 1 字节；颜色由 LVGL 运行时重着色，能跟随主题强调色。
- 当前 LVGL 配置没有启用 ThorVG，因此固件不在运行时解析 SVG。这样保留矢量源的维护优势，同时避免在设备端引入 SVG 解析器、浮点几何和额外堆内存。
- 无透明度的小插图和固定背景使用 `LV_COLOR_FORMAT_RGB565`，每像素 2 字节。
- 必须保留多色透明边缘的图片使用 `LV_COLOR_FORMAT_ARGB8888`，每像素 4 字节，内存代价是 RGB565 的两倍。
- 能用文字、纯色和 LVGL 原生控件表达的界面，不增加图片资源。
- 转换后检查宽、高、stride、透明通道和实际显示颜色；不得仅凭生成成功判定资源可用。

## 存放位置

- SVG 可编辑源放在 `assets-src/<group>/`，生成的 A8 C 描述符放在 `assets/generated/`；生成文件不得手工修改。
- 启动必需且体积小的资源编译进 M55 XIP 镜像。当前 25 个图标生成 24/32/48 三档，共 97,600 字节 A8 像素数据。
- 大背景、媒体封面和可替换主题不编译进固件；待外部存储驱动启用后，由资源服务按需读取。
- 当前产品配置未启用外部存储驱动，因此 Files 页面明确显示“unavailable”，不伪造容量或文件列表。

## 转换命令

产品图标使用仓库外置工具目录中的纯标准库转换器，一条命令生成全部尺寸：

```powershell
.\tools\freather\build-ui-icons.cmd
```

`assets/manifest.json` 记录图标 ID、用途、旧资源参考和许可证决策；`assets/THIRD_PARTY_NOTICES.md` 保留参考工程的 MIT 声明。FeatherTalk 使用重新绘制的中性图形，不复制 Microsoft 或其他第三方商标。

一般 PNG 资源使用另一条转换链路。工具只使用 Python 标准库，支持非交错 8-bit 灰度、RGB、索引色、灰度透明和 RGBA PNG，输出可直接参与 LVGL 9.2 编译的 `lv_image_dsc_t`：

```powershell
python .\tools\freather\ui-asset-convert.py icon.png icon.c --name ft_icon --format argb8888
python .\tools\freather\ui-asset-convert.py background.png background.c --name ft_background --format rgb565
```

评审时同时记录源文件许可证、转换参数、生成尺寸和固件体积增量。参考项目中的资源只有在许可证和产品用途均明确后才可迁入。新增常用图标时必须先扩展 SVG、清单和 `ft_icon_id_t`，页面代码不得重新引入散落的 `LV_SYMBOL_*` 或固定 PNG 图标。
