# FeatherTalk UI 资源策略

本策略面向 480×800、RGB565 显示链路和 LVGL 9.2。资源先经过筛选再进入产品工程，禁止直接复制参考仓库的全部图片目录。

## 格式选择

- 产品图标以 `assets-src/` 下的单色 SVG 为唯一可编辑源。构建工具直接生成可交给 GPU 的 `VG_LITE_FP32` 原生命令流、fill/stroke 各自的精确边界和 `LV_COLOR_FORMAT_A8` 三档回退图；运行时不解析 XML/SVG、不重新拼接 shape/point，也不依赖 ThorVG。
- 当前 50 个 SVG 图标全部使用 LVGL Vector API，经 VG-Lite path 硬件光栅化；24/32/48 px 的 A8 产物只作为矢量 path 创建失败时的防御性回退，不参与正常界面绘制。
- 矢量和 A8 回退都使用 LVGL 运行时颜色/透明度，能跟随主题强调色。矢量 SVG 的 fill 与 stroke 必须分开交付：stroke 保留原始中心线、闭合指令、宽度、cap 和 join，由 VG-Lite 原生 tessellation 生成最终轮廓；禁止用独立线段矩形和圆点近似拼接，否则 `NONZERO` fill 下的反向重叠会形成孔洞。VG-Lite 颜色必须按 `0xAARRGGBB` 打包，与矩形、文字和 A8 图像后端保持一致，禁止交换红蓝通道。
- 无透明度的小插图和固定背景使用 `LV_COLOR_FORMAT_RGB565`，每像素 2 字节。
- 必须保留多色透明边缘的图片使用 `LV_COLOR_FORMAT_ARGB8888`，每像素 4 字节，内存代价是 RGB565 的两倍。
- 能用文字、纯色和 LVGL 原生控件表达的界面，不增加图片资源。
- 转换后检查宽、高、stride、透明通道和实际显示颜色；不得仅凭生成成功判定资源可用。

## 存放位置

- SVG 可编辑源放在 `assets-src/<group>/`；生成的 A8 描述符和 `VG_LITE_FP32` fill/stroke 原生路径放在 `assets/generated/`，生成文件不得手工修改。
- 启动必需且体积小的资源编译进 M55 XIP 镜像。当前 50 个图标生成 24/32/48 三档，共 195,200 字节 A8 回退像素数据，并生成一套与尺寸无关的原生矢量流。设备首次实际使用图标时只建立轻量 LVGL/VG-Lite 包装对象；路径命令本身直接引用 XIP 常量，不复制、不再次转换，也不在启动时一次性建立 50 份包装。
- 大背景、媒体封面和可替换主题不编译进固件；相册通过 LVGL POSIX `P:` 驱动从 `/flash` 或 `/sdcard` 按需读取 JPG/JPEG/PNG/BMP。产品级 LVGL 配置只在 FeatherTalk_M55 工程启用 TJPGD、LODEPNG、BMP 与 POSIX FS，不修改 SDK 公共 `lv_conf.h`。
- PNG 解码会分配完整 ARGB 图像，当前限制为 2 MiB 文件且不超过 160,000 像素；JPEG/BMP 上限为 16 MiB、4,000,000 像素。超限、损坏或介质丢失必须显示受控错误，不能直接交给解码器耗尽堆。
- Files 页面只显示 DFS 实际挂载卷。当前 SDHC1/Elm-FatFS 已启用，SD 卡挂载成功后读取 `/sdcard` 的真实容量和目录；未插卡或卸载后显示等待状态，不伪造容量或文件列表。

## 转换命令

产品图标使用仓库外置工具目录中的纯标准库转换器，一条命令生成全部尺寸：

```powershell
.\tools\freather\build-ui-icons.cmd
```

`assets/manifest.json` 记录图标 ID、用途、旧资源参考和许可证决策；`assets/THIRD_PARTY_NOTICES.md` 保留参考工程的 MIT 声明。FeatherTalk 使用重新绘制的中性图形，不复制 Microsoft 或其他第三方商标。

矢量后端必须保持产品帧提交合同。VG-Lite 4.0.107 的 `vg_lite_set_scissor()` 会把 render target 标为 dirty；官方 `set_render_target()` 原实现即使只有 scissor 改变也会先 `flush`。当前适配先复用 draw task 已安装的相同裁剪；确需更窄子裁剪时，把 `0x0A13` scissor 状态按顺序追加进当前 GPU command chain，不再把它误判成 render-target 所有权切换。修改 vector/scissor 代码后必须用 `feather_ui_bench` 确认 `submits == rendered`，不能只检查画面是否出现。

Stroke path 的中心线 bounds 不能直接作为最终轮廓 bounds。调用 `vg_lite_update_stroke()`
后必须至少按 `stroke_width / 2` 向四周扩张，并保留 1 source pixel 的 AA guard；否则圆环、
文件夹和图片外框会被一个不可见矩形削掉外侧半个线宽。每次调整 path bounds、矩阵或
scissor，必须使用同一固件的 `feather_ui_icon_renderer a8/vector` 双模式回读 framebuffer，
并检查 A8/Vector 二值拓扑在 1 px AA 容差外的差异为 0。

VG-Lite 原生 stroke 的转换结果可以跨帧缓存，但 `vg_lite_update_stroke()` 产生的展平点链表、分段链表和临时轮廓只服务于一次转换。最终 `stroke_path` 命令流生成后必须立即释放这些工作数据，缓存中只保留中心线路径、最终命令流和上传状态；否则 32 个默认缓存条目会耗尽 UI 堆。

一般 PNG 资源使用另一条转换链路。工具只使用 Python 标准库，支持非交错 8-bit 灰度、RGB、索引色、灰度透明和 RGBA PNG，输出可直接参与 LVGL 9.2 编译的 `lv_image_dsc_t`：

```powershell
python .\tools\freather\ui-asset-convert.py icon.png icon.c --name ft_icon --format argb8888
python .\tools\freather\ui-asset-convert.py background.png background.c --name ft_background --format rgb565
```

中文界面使用 Noto Sans SC 的 7,586 个离线轮廓字形。`build-lvgl-vector-font.js` 将字体轮廓一次性编译为 canonical 1000 UPM、`VG_LITE_S16` 原生命令流；12/14/16/22 px 共用同一轮廓，仅在运行时改变矩阵，不再生成四份中文字形位图，也不在设备端调用 FreeType/TTF 解析。字形度量和原生路径包装均跨帧缓存。旧 `feathertalk_noto_sans_sc_*.c` 只保留为迁移产物，产品 `SConscript` 明确不编译，页面不得重新引用。

普通 `tools\freather\build-demo.ps1 -Project FeatherTalk_M55` 会先校验/更新矢量字体。修改字库来源或生成规则时可单独执行对应字体生成脚本；GB2312 基础字形、源码实际使用的 CJK 标点、Unicode 通用标点、全角字符和间隔号 `·` 必须继续进入同一 canonical 轮廓集合。新增弯引号 `“ ”` 等字符不得依赖 Montserrat fallback 猜测非 ASCII 是否存在。

PSE84 当前启用 `LV_VG_LITE_USE_PATH_UPLOAD=1`，但明确保持
`LV_VG_LITE_USE_STROKE_UPLOAD=0`。这里的 path upload 是显式 opt-in，不是“所有 immutable
path 自动上传”：只有通过 `lv_draw_vg_lite_vector_path_attach_native()` 绑定的离线字体/SVG
fill 原生流使用 CALL，LVGL 运行时转换路径保持内联。离线 fill/字体已通过 CALL 的完成中断、
像素校验和、真实复杂 SVG、同帧多次调用与跨帧复用验证。上传器会在 8 字节
对齐、末尾为 `CLOSE` 的路径后补 32 位 `END`，再追加 `RETURN` 并 clean 完整上传区
D-Cache。曾给 256 KiB 预留并让所有运行时路径自动上传的压力测试仍会在页面/通知生命周期
切换中停滞，因此不得把全局宏误当成通用安全许可。Stroke 则必须先由
`vg_lite_update_stroke()` 展开成最终命令流，再**内联**到本帧
主 command buffer；实板 framebuffer A/B 已确认，当前 GC265/驱动组合下把该最终 stroke
命令流改为独立上传并 CALL 虽能返回完成，却会丢失/破坏部分描边像素。因此不得把“无
超时”当成“像素正确”，也不得重新打开 stroke upload，除非新增同一 framebuffer 的逐像素
等价测试。内联 stroke 仍属于 GPU 绘制，并不破坏一帧一次 submit 的整帧单链合同。

纯对象缩放优先使用 `LV_DRAW_TRANSFORM_USE_MATRIX=1` 直接变换主 framebuffer 上的 draw
task；不得为 Tile 选中动画额外建立中间 Layer。只有 opacity、filter、复杂 clip 等确实需要
离屏纹理的效果才允许 Layer。PSE84 实测“GPU 写 Layer 后不等待、切回主目标并立即把该
Layer 作为纹理源”的 read-after-write 链会停滞，所以通用 Layer 的 render-target 同步边界
必须保留，不能用普通命令顺序替代资源依赖/fence。

评审时同时记录源文件许可证、转换参数、生成尺寸和固件体积增量。参考项目中的资源只有在许可证和产品用途均明确后才可迁入。新增常用图标时必须先扩展 SVG、清单和 `ft_icon_id_t`，页面代码不得重新引入散落的 `LV_SYMBOL_*` 或固定 PNG 图标。每个应用、Settings 分类、System 摘要卡和专用功能入口必须拥有独立语义图标；Gallery 与 Wallpaper 因此分别使用“叠放照片”和“悬挂画框”，不得复用 Files、Media 或 Personalization 图标。
