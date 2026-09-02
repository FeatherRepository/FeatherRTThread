# FeatherUI：面向 PSoC Edge E84 GPU2D 的 UI 库

FeatherUI 是 FeatherTalk M55 产品工程的新图形运行时。它不在 LVGL 上增加一层适配，而是直接把 UI 状态转换成不可变显示列表，再把整帧命令一次性交给 GCNano Ultra V。当前目标是先建立正确、可测的 GPU 原生主路径，再逐页迁移旧 Shell 的功能。

## 强制帧合同

正常一帧必须满足：

1. UI 线程只收集 `clear/rect/line/text/image` 等绘制任务，不在收集期间访问 GPU；
2. 显示列表收集完毕后，VG-Lite 后端顺序编码全部命令；
3. 一帧只能调用一次 `vg_lite_finish()`；
4. GPU 完成中断只能对应一次完成事件；
5. 完成后在 vblank/frame-done 边界切换 DC framebuffer；
6. 禁止把 CPU 像素绘制、整帧 memcpy 或逐命令 `flush/finish` 混入主路径。

板端通过 VG-Lite 的弱 hook 统计真实 submit、命令字节、完成中断和 GPU 忙时。任何一帧的 submit 或 complete 不等于 1 都会累计 `contract_violations`，并输出诊断日志。

## 数据流

```text
产品状态 / 触摸事件
        |
        v
collect callback  --只写-->  FeatherUI display list
        |
        v
VG-Lite encoder  --顺序编码全部图元-->  224 KiB command buffer
        |
        v
一次 vg_lite_finish() --> GCNano Ultra V --> 后台 RGB565 framebuffer
                                                |
                                                v
                         frame-done 同步 --> DC 直接换址 --> MIPI-DSI
```

LCD 使用两张 `512 × 800 × RGB565` 物理缓冲；可见宽度是 480，stride 保持 512。GPU 直接画入后台缓冲，显示控制器直接扫描前台缓冲，不再经过 LVGL draw buffer、dirty-area 合并或 AXI-DMA/CPU 二次搬运。

## 为什么不能逐命令设置 scissor

PSE84 随附 VG-Lite 驱动的 `set_render_target()` 在 `scissor_dirty` 时会调用 `vg_lite_flush()`。若每个图元都调用 `vg_lite_set_scissor()`，142 条桌面命令会变成 142 次 GPU submit，命令链表完全失效。

FeatherUI 因而采用以下边界：

- 收集阶段先做屏幕和控件 clip 的几何求交；
- 一帧编码期间不改变 VG-Lite scissor；
- 需要复杂裁剪的后续控件使用预裁剪路径、遮罩纹理或独立离屏层，不能恢复逐命令 scissor；
- render target、blend、纹理和路径状态应尽量按批次稳定排列。

## 第二阶段：复合路径与整串文本缓存

第二阶段继续保持显示列表的原始顺序，不跨透明图元重排命令，并在此约束内减少 VG-Lite API 调用：

- 连续、同色、完全不透明的矩形/圆角矩形/线段会合并为一条复合 path；
- 一个复合 path 内的每个子图元以新的 `MOVE` 开始，前一个开放子路径由后续 `MOVE` 结束，整条 path 只在末尾写入一个 `END`，不向公开路径数据写入驱动内部使用的 `CLOSE`；
- 单个不透明、无圆角矩形仍使用 `vg_lite_clear()`，避免为简单背景额外构造 path；
- ASCII 或中英混合文本按完整 UTF-8 字符串缓存为 A8 纹理，由一次 `vg_lite_blit()` 绘制，不再逐字符提交 blit；
- 文本缓存固定为 24 项，每项 `256 × 16` 字节，总计 96 KiB，采用帧内固定与 LRU 淘汰；CPU 只在缓存未命中时组合 A8 源纹理，最终 framebuffer 仍完全由 GPU 绘制；
- 构建脚本从产品场景字符串提取字符，并使用 Noto Sans SC 生成统一的产品 A8 atlas。当前产物包含 95 个 ASCII 和 303 个产品 CJK 字符，共 398 个字形、129,600 字节；ASCII 不再使用 5×7 点阵。
- atlas 以 18 px 单一母版常驻，正文通过 GPU 双线性缩放到约 14 px，标题保持 18 px 或放大到约 27 px。这样避免同时常驻三套字体压垮 3 MiB GFX 内存，又消除了点阵字体放大后的锯齿和中英文字重不一致。

当前桌面一帧由 73 条显示列表命令编码成 3 次 clear、26 次 path 和 15 次 blit。复合 path 和整串文本缓存既降低 VG-Lite 调用次数，也显著减少 CPU 命令编码开销。

## 一帧一次 D-cache 维护

随芯片提供的 VG-Lite 驱动在每次 `vg_lite_blit()` 的 `flush_target()` 路径中默认执行整块 D-cache 的 clean/invalidate。该动作不会直接产生新的 GPU submit，但若一帧有多次 blit，会造成重复且昂贵的全缓存维护。

为保持驱动默认行为兼容，VG-Lite 中新增的 cache-maintenance hook 是弱符号，默认返回“不延迟”，因此 FeatherUI 以外的调用者仍保留原有逐次 cache 维护语义。FeatherUI 提供强符号覆盖，仅在本库的一帧命令编码窗口内请求延迟维护：

1. 开始编码前启用延迟标志；
2. 顺序编码这一帧的全部 clear、path 和 blit；
3. 在唯一一次 `vg_lite_finish()` 前执行一次 `SCB_CleanDCache()`，使 CPU 写入的命令和源纹理对 GPU 可见；
4. 调用唯一一次 `vg_lite_finish()` 并恢复默认 hook 行为。

这不会放宽“一帧一次提交”的合同，也不会改变其他 VG-Lite 使用者的缓存一致性策略。

## 线程模型

- `feather_ui`：唯一 UI 状态、显示列表和 GPU 提交线程；
- `fui_input`：读取 ST7102/ST7123 触摸，通过固定消息队列送入 UI 线程；
- GPU IRQ：只记录完成和忙时，不运行界面逻辑；
- DC IRQ：负责 present/frame-done 同步。

触摸 I²C 不再串在每一帧渲染之前。UI 回调仍只在 `feather_ui` 执行，避免并发修改页面模型。

触摸路径还执行以下坐标和时延约束：

- 启动时读取 ST7102/ST7123 报告的原始坐标范围，再分别映射到面板物理宽高，最后执行与 LCD 方向一致的旋转；当前板上报告 `1124 × 1124`，不能直接裁剪为 `480 × 800`；
- `feather_ui_stats` 同时输出最近一次 raw/mapped 坐标和控制器 extent，便于区分硬件坐标、旋转映射与页面命中错误；
- 事件队列容量为 32，输入线程优先级高于绘制线程；单点读取只取状态和第一个触点的 16 字节，并利用低有效 IRQ 避免空闲轮询反复占用 I²C；
- 触点滤波死区为 1 px，释放确认缩短为 2 帧；队列记录最近/最大分发延迟；
- Tile 拖动和缩放使用“当前位置减按下锚点”的绝对几何，不累加逐事件 delta。即使中间 MOVE 被合并或错过，控件也不会逐步漂离手指。

## 固定容量动画系统

FeatherUI 0.4 增加了不使用堆内存的通用属性动画调度器：

- 固定 48 个动画槽，按 `target + property` 唯一标识，同一属性的新动画会从当前值平滑接管旧动画；
- 支持线性、三次缓出、三次缓入缓出、Back 缓出和定点 Spring 缓动，以及延迟、重复和往返播放；
- 动画回调只更新产品状态，不直接访问 GPU；每个 16 ms tick 统一推进全部活动动画，仅当属性实际改变时才标记 dirty；
- 无活动动画且界面状态未变化时，dirty gate 继续阻止无意义的收集、编码和 GPU 提交；
- Start Tile 的长按脉冲、松手吸附/冲突让位，以及通知面板的距离/速度吸附均已接到该调度器；
- Tile 位移动画使用不越界的三次缓出，脉冲只缩放 Tile 本体，四角操作箭头保持在逻辑边界内且不随脉冲漂移。

动画仍遵守帧合同：一个 tick 中所有活动属性先更新，随后整个场景只收集一次显示列表，并只产生一次 GPU submit/complete。

## 0.6 组件与语义图标层

FeatherUI 0.6 在 0.5 基础组件层之上补齐第一组选择组件和稳定语义图标：

- `Button`、`List Row`、`Switch`、`Slider`、`Progress`、`Text Field` 和 `Scrollbar` 使用公共状态、风格和运行时 bounds；
- UTF-8 文本宽高测量与 ASCII/CJK A8 缓存的 advance 规则一致，按钮、键盘和对话框不再用固定像素猜测文字居中；
- Slider 的轨道几何和 `x -> value` 命中换算来自同一描述符，避免画面与触摸范围分叉；
- Scrollbar 的 Thumb 由 viewport、content、offset 动态计算，不保存某个固定分辨率下的位置；
- List Row 通过 leading callback 接入产品图标，组件库不依赖 FeatherTalk 的应用 ID；
- 组件只向当前 painter 追加图元，不直接调用 VG-Lite，也不会产生额外 GPU submit。
- `Radio Group`、`Segmented Control` 和 `Select Popup` 的绘制与命中均由同一描述符计算；候选数量、选中项和禁用项全部来自运行时模型；
- 本地音频与 UAC 的采样率、位深、声道，时区、语言、背景，Flash/SD 和相册来源已经迁移到公共选择组件；
- `fui_icon_id_t` 为应用、设置项和状态提供约 70 个稳定语义 ID，产品场景不再通过取模重复十种临时图标；
- 图标线段使用显示列表自有的固定容量 segment pool；连续同色线段最多 8 段合并成一条 compound path，不增加第二次 GPU 提交。
- 通用 Context Menu、Dialog、Spinner 和 Toast 已进入同一立即式组件层；文件长按/根设备菜单、删除/格式化/错误对话框、相册加载和剪贴板结果提示已迁移；
- 输入消息队列已按 RT-Thread 5 的 `rt_mq_recv()`“成功返回字节数”语义修正，真实触摸和串口注入事件现在能够从队列进入 UI 线程；诊断统计会同时输出 queued/dispatched/failed；
- 修正纯 ASCII 文本缓存复用不同字号时返回首次缓存缩放值的问题；相同字符串在页面详情和弹窗标题/选项中可按各自请求字号绘制。

产品需要实现的完整组件矩阵和优先级见 [组件清单](COMPONENTS_zh.md)。

## 目录

| 文件 | 作用 |
| --- | --- |
| `include/feather_ui.h` | 公共类型、引擎、统计和 painter API |
| `include/feather_ui_components.h` | 基础组件描述符、状态、风格、共享绘制/命中几何 API |
| `include/feather_ui_icons.h` | 稳定语义图标 ID 与绘制入口 |
| `src/fui_display_list.c` | 固定容量显示列表及收集阶段裁剪 |
| `src/fui_renderer_vglite.c` | VG-Lite 命令编码与唯一帧提交点 |
| `src/fui_engine.c` | UI/输入线程、双缓冲、事件和性能合同 |
| `src/fui_animation.c` | 48 槽定点属性动画、缓动、重复/往返与统计 |
| `src/fui_components.c` | Button/List Row/Switch/Slider/Progress/Text Field/Scrollbar/Radio/Segmented/Select/Context Menu/Dialog/Spinner/Toast |
| `src/fui_icons.c` | GPU 图元语义图标注册表 |
| `src/fui_font_product_data.c` | 由产品字符串和 Noto Sans SC 构建的统一 ASCII/CJK A8 atlas |
| `src/fui_text_cache.c` | 24 项中英混合整串 A8 文本纹理缓存、帧内固定与 LRU 淘汰 |

产品 Shell 位于 `projects/FeatherTalk_M55/applications/gpu_ui/`，FeatherUI 本身不包含业务页面。

## 板端诊断

```text
feather_ui_stats
feather_ui_bench 120
feather_ui_test
feather_ui_page 0
feather_ui_image
feather_ui_pause 1
feather_ui_pause 0
```

- `feather_ui_stats`：最近一帧阶段耗时、命令数、submit/complete、合同违约和动画活动/峰值统计；
- `feather_ui_bench`：连续绘制指定帧数，排除空闲 RTOS tick，输出真实吞吐；
- `feather_ui_test`：运行页面、路由、磁贴、键盘、通知和动态几何回归；
- `feather_ui_page`：按编号进入 17 个产品页面中的任意一个；
- `feather_ui_image`：查看 JPEG/BMP 解码状态、动态 stride、尺寸和耗时；
- `feather_ui_pause`：暂停/恢复渲染，供 OpenOCD 稳定读取双缓冲。OpenOCD 读取一张 819,200 字节缓冲约需 5.8 秒，未暂停时抓到的横线可能只是跨多帧拼接，不可当作 LCD 脏块。

### 第二阶段真机结果

在 PSOC Edge E84 开发板上连续绘制 240 帧，结果如下：

| 指标 | 结果 |
| --- | ---: |
| 实际帧率 | 117.1 FPS |
| collect | 433 us/帧 |
| encode | 4,047 us/帧 |
| render | 7,718 us/帧 |
| GPU busy | 3,605 us/帧 |
| present | 282 us/帧 |
| 显示列表命令 | 71 条/帧 |
| VG-Lite API 分布 | 3 clear / 29 path / 13 blit |
| GPU 命令字节 | 39,904 B/帧 |
| submit / finish / complete | 240 / 240 / 240 |
| 帧合同违约 | 0 |

暂停渲染后读取完整 819,200 字节 framebuffer，与启用本阶段 D-cache 延迟维护前、相同状态的参考显存逐像素比较，480×800 全可见区域差异为 0。因此性能提升没有改变最终显示结果。

### 当前功能真机结果

当前包含 398 字形的统一 Noto Sans SC 产品 atlas、动态布局、JPEG/BMP 纹理、固定容量动画系统、完整第二批公共组件、唯一图标注册表和全部 17 个页面。板上自动交互为 `90 pass / 0 fail`；Home 页连续 60 帧为 64.0 FPS，平均 collect 1,494 us、encode 5,941 us、render 13,342 us、GPU busy 6,551 us、present 400 us、整帧 15,345 us。当前 Home 显示列表为 53 条、GPU 命令流 37,448 bytes/frame；60 帧仍严格对应 60 次 submit、60 次 complete、0 次合同违约。统一抗锯齿字体比旧 5×7 ASCII 点阵路径增加了一定纹理采样开销，但连续吞吐仍高于 60 FPS，画面精细度和中英文一致性不再依赖低分辨率点阵放大。串口注入事件的板测队列结果为 `queued=1 / dispatched=1 / failed=0`，最近/最大分发延迟均为 2 ms。

## 动态数据边界

FeatherUI 固定的是资源和协议边界，不固定运行时状态：

- 固定容量的 display list、事件队列、线程栈、命令缓冲与 GFX SRAM 预算属于可审计资源上限；
- surface 宽高、stride、触摸阈值、页面布局、命中区域、可见行数和图片解码 stride 均在启动或请求时计算；
- 产品 Shell 的绘制区域和触摸区域使用同一组几何函数，避免“画面已缩放、点击仍在旧坐标”；
- 音频格式、存储容量、设备状态等业务能力来自对应服务，FeatherUI 不维护第二套能力表；
- 设置项用单一描述符绑定名称、详情、图标和路由；录音设备、磁贴网格、菜单动作、主题色和字体字符数量均由数据源长度派生；
- 600 ms 长按与 60 Hz 目标帧率是明确产品交互常量，通过引擎配置传入，不埋在事件识别逻辑中。
- 动画槽位数、缓动类型和属性协议是固定资源边界；起止值、时长、拖动速度、吸附目标和屏幕边界均由当前状态与运行时几何计算。

## 参考资源与适配依据

实现以本仓库/工作区现有资料为准：

- `docs/board/PSOC-Edge-E84/PSE84_SOC_GPU2D_zh.md`：E84 GFXSS、GPU/DC、时钟和内存审计；
- `libraries/components/mtb-device-support-pse8xxgp/pdl/drivers/third_party/COMPONENT_GFXSS/vsi/gcnano/`：当前芯片实际编译的 VG-Lite/HAL/GCNano 驱动；
- `libraries/Common/board/ports/lvgl/`：旧 LVGL VG-Lite 适配中可复用的格式与路径经验，不复用其调度模型；
- 工作区 `D:/Develop/Edgi-Talk/GC355/`：旧 IP data book 和测试资料，仅用于理解 Vivante 命令/路径/总线模型，不能替代 PSE84 当前头文件与 feature 选项；
- 工作区 `official-resources/training/mtb-training-psoc-edge-graphics/`：Infineon 图形训练资料与参考性能。

## 当前限制

- 当前实现矩形、圆角矩形、线段、A8 中英文文本和 RGB565 图片；JPEG/BMP 已接入，PNG 尚未接入；
- 渐变、任意变换、离屏层和复杂 clip 仍需扩展；通用属性动画和统一模态层已接入，但页面转场、模态进出动画和更复杂的组合时间线尚未完成；
- 基础组件、唯一图标注册表、Radio/Segmented/Select、Context Menu、Dialog、Toast/Spinner 已建立；虚拟列表、缩略图网格和文件多选仍未完成；
- 存储、USB、音频和录音已绑定；Wi-Fi/蓝牙扫描列表与真实媒体解码仍受底层服务进度约束；
- 键盘当前输入 ASCII；中文显示正常，但中文输入法与候选词尚未实现；
- 显示列表容量固定为 768，溢出会拒绝送出不完整帧；
- 当前仍通过官方 VG-Lite API 编码命令，CPU 编码开销是下一阶段的主要优化对象。
