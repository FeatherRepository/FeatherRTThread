# FeatherTalk Windows Phone UI 移植进度

更新时间：2026-08-29

本文档以“代码完成、自动测试覆盖、真实双核板验证”作为完成标准。没有硬件数据源的功能必须显示为不可用，不允许用虚构数据代替。

| 阶段 | 当前状态 | 验收标准 |
|---|---|---|
| Shell 骨架 | 已完成、已板测 | Home、状态栏、导航栏、Alert 和通知快捷面板均可交互 |
| 系统通知与快捷面板（P0） | 已完成、已板测 | 跟手/距离速度吸附、真实快捷状态、通知队列、完整关闭路径和全量自动测试 |
| 桌面数据模型 | 已完成、已板测 | App 注册表、Start、All Apps、Home/Back/Search 均有自动测试 |
| 路由与生命周期 | 已完成、已板测 | push/pop/home、深度 8、溢出拒绝、资源释放和对象泄漏检查 |
| 主题与偏好 | 已完成、已板测 | 强调色、Tile 透明度、背景选择及内存偏好后端 |
| 资源策略 | 已完成、已板测 | SVG 源、A8 三档生成、统一图标 API、RGB565/ARGB8888 规则和许可证清单 |
| 响应式布局 | 已完成、已板测 | 五种纵横屏配置计算自测、真实 480×800 控件几何检查和完整交互测试 |
| 系统状态 IPC | 已完成、已板测 | ABI 4 携带系统状态、快捷能力/启用/连接/信号强度和命令结果；M55 状态栏、System 页、Live Tile 和快捷面板实时显示 |
| 业务页面 | 已完成、已板测 | Settings、Media、Messages、Files、System/Device 和 About 均有真实交互或状态 |
| 动画与性能 | 已完成、已板测 | Live Tile、跟手面板/遮罩/速度吸附、Search Spinner、FPS、峰值内存和切页泄漏 |

## 本轮实现对应原移植顺序

1. Shell 使用 LVGL 9.2 原生对象；通知中心支持从状态栏跟手下拉、在面板内跟手上推、释放吸附及点击切换；Alert 使用 Message Box，底栏为 Back/Home/Search。
2. 六个 App 由静态描述符驱动，Start Tile、All Apps 列表和 Search 结果共用同一数据源。
3. 路由栈容量固定为 8；测试覆盖 push/pop/home、溢出拒绝、深层释放和返回 Start。
4. 偏好后端按计划使用内存桩，保存强调色、Tile 透明度、背景模式和 revision；生产持久化后端仍可在不改 UI API 的情况下替换。
5. 资源格式与位置规则见 `UI_ASSET_POLICY_zh.md`，转换工具为 `tools/freather/ui-asset-convert.py`。
6. M33 每个心跳发送 16 字节系统状态帧和 16 字节快捷状态帧。RTC 有效时发送 Unix 时间；当前板级电源、电池、网络、蓝牙和旋转驱动未启用，字段使用 `unknown/unavailable` 和显式能力位，未填充虚构数据。
7. System 显示双核和性能状态；Settings 修改真实偏好；Media 支持上一首、播放/暂停、下一首和音量；Messages 产生通知；Files 刷新真实的存储可用性；About 显示产品/固件/ABI 版本。
8. System Live Tile 每秒更新；通知中心使用滑动动画；Search 使用 Spinner 动画；System/MSH 暴露 FPS、刷新次数、当前/峰值堆和 UI 对象峰值。

## P0 已完成：系统通知与快捷面板

以下七项均已通过代码检查、自动测试和真实双核板验证：

1. **顶部跟手拖动（已完成、已板测）**：从屏幕顶部状态栏按下并拖动，面板 Y 坐标与触点位移实时同步；展开后可在面板内向上跟手拖动。
2. **距离/速度吸附（已完成、已板测）**：使用带时间戳的移动采样计算速度；快速甩动按速度方向吸附，低速拖动按距离阈值和展开比例决定目标，并覆盖上下方向及边界测试。
3. **第一排快捷按钮（已完成、已板测）**：已加入 Wi-Fi、蓝牙、亮度和屏幕旋转。亮度按钮及 Slider 控制真实 M55 `pwm18`；其余按钮服从 M33 能力位，在驱动缺失时禁用。
4. **真实状态与 M33 能力契约（已完成、已板测）**：FeatherTalk IPC 已升级为 ABI 4，固定 16 字节快捷状态帧包含能力位、启用位、连接位、Wi-Fi 信号百分比及命令结果。M33 当前能力位为 0，UI 明确显示 `Unavailable`，不伪造 Wi-Fi、蓝牙、信号强度或旋转状态。
5. **通知队列（已完成、已板测）**：固定容量 8 条，具有稳定 ID、来源、标题、正文、时间和未读状态；支持左右滑动单删、全部清除及满队列丢弃最旧项，数据生命周期与 LVGL 页面对象解耦。
6. **完整关闭路径（已完成、已板测）**：点击内容遮罩、Home、Back、Search 路由动作和面板内向上滑动均会关闭；拖动开始会接管并停止尚未完成的吸附动画。
7. **完整自动测试（已完成、已板测）**：覆盖实时中间 Y、距离/速度吸附、四类快捷控件、IPC 不可用、亮度 PWM、通知入队/未读/单删/全清/溢出、遮罩/Home/Back/Search/上滑关闭，以及原有全部页面、控件、路由边界和对象泄漏检查。

发布验收在 480×800 下保持持续刷新、无 HardFault/Assert、路由对象增量为 0；缺失硬件能力明确显示不可用。

## 已验证基线

- 480 x 800 RGB565 Shell 已在 M55 运行。
- 历史 IPC ABI v1 基线收发无错误；系统状态阶段升级为 ABI 2，本轮快捷能力契约升级为 ABI 3。
- 历史 UI 自动测试基线：95 PASS、0 FAIL、40 次交互动作。

## 2026-08-29 SVG 图标系统

- 参考旧项目中图标的功能语义和布局角色，重新绘制 25 个无品牌、单色 SVG；源文件、参考关系和 MIT 声明均已入库。
- `tools/freather/ui-svg-icon-convert.py` 只接受受约束的 SVG 几何子集，拒绝 path、transform、脚本和外部引用；生成 24/32/48 三档 LVGL A8 描述符，共 97,600 字节像素数据。
- 新增统一 `ft_icon_id_t`、`ft_icon_create()` 和 `ft_icon_set()`；导航栏、状态栏、六个默认应用、Start Tile、All Apps、Search、页面标题、媒体控制和 Files 刷新均已迁移。
- A8 图标支持白色或运行时强调色重着色；默认应用描述符不再持有字体符号字符串。
- M55 `-Clean` 干净构建通过：text=817,856、data=3,916、bss=4,304,032 字节；HEX 2,311,518 字节。随后已完成下述双核板测。

## 2026-08-29 SVG 图标系统双核板测

- M55 HEX SHA-256 为 `C2DF7BD316CBABDBD7EB1302D1CE4FCDD27142A0A77E1DCD38E1921B2FBFF8B3`；M33 最终合并 HEX 为 451,039 字节，SHA-256 `1F05A88AE42C9912C940C8CCB6F3B65B62116C89FC32BE88B1CC1788F0D40797`。
- 使用 Infineon Customized OpenOCD 5.19.0.4782 和 KitProg3 `0D141868022E2400`：M55 外部 SMIF XIP 镜像写入 823,296 字节、校验 821,772 字节；M33 Signed/Secure 合并镜像写入 167,936 字节、校验 160,336 字节。两步均成功。
- 板端报告 LCD 初始化、GPU 显存分配和触摸注册成功，Shell 分辨率为 480×800、App 数量 6、初始路由深度 1。
- 自动测试完整结果为 `134 PASS / 0 FAIL / 68 actions`，耗时 35,591 ms；覆盖通知/Alert、Search、六个 Start Tile、六个 All Apps 入口、全部主题偏好、Media 全控制、Messages、Files、Back/Home、路由深度/溢出/释放和泄漏检查。
- 测试结束后页面为 Home、路由深度 1、偏好恢复默认、强调色对象 20、注册溢出 0；路由对象增量 0，堆增量 560 字节（分配器高水位，只记录不作泄漏断言）。
- 两次延时状态采样均为 53 FPS，刷新计数从 3,424 增至 4,866；当前堆 90,304 / 1,441,752 字节，峰值 112,576 字节，UI 对象峰值 103，说明显示刷新持续运行而非停在静态首帧。
- 最终 IPC 为 tx/rx/err=`109/219/0`，系统状态序号持续增长；自动测试状态保持 `passed`。
- 完整启动与测试日志：`projects/FeatherTalk_M55/build/feathertalk-ui-svg-board.log`；状态采样：`feathertalk-ui-svg-status.log`、`feathertalk-ui-svg-status-followup.log`。

## 2026-08-29 响应式布局与双核板测

- 新增统一布局配置 `feathertalk_ui_layout.c/.h`。启动时从 LVGL Display 读取分辨率，统一计算缩放、compact/landscape、状态栏、导航栏、通知中心、页面边距、控件/列表/键盘高度、Tile 列数与列宽；设计基准为 480×800，最低支持几何为 200×240。
- Start 和 All Apps 已改为“固定标题行 + flex-grow 可滚动内容区”，普通页面允许纵向滚动；Settings 的多组按钮改为等宽 flex，Media 控制改为 `1:2:1` flex。Shell、Alert、Search、字体和 24/32/48 三档图标也全部由同一布局配置选择。
- 固件启动自测覆盖 240×320、320×480、480×800、720×1280 和 800×480；每个自动点击的真实控件在事件发送前检查非零宽高且不得宽于父容器。本轮固定尺寸审计未发现业务 UI 中残留的固定页面宽高或剩余高度。
- M55 `-Clean` 干净构建通过：text=820,832、data=3,916、bss=4,304,096 字节；HEX 2,319,888 字节，SHA-256 `57CF3EC9F89985CA355D766AC86AB59BDBCD2A85CE90A98D567F6DB9DBF8F808`。
- M33 代码本轮没有变化，因此复用上一轮已签名的最终合并 HEX：451,039 字节，SHA-256 `1F05A88AE42C9912C940C8CCB6F3B65B62116C89FC32BE88B1CC1788F0D40797`。
- 使用 Infineon Customized OpenOCD 5.19.0.4782 和 KitProg3 `0D141868022E2400` 完成双核烧录与校验：M55 写入 827,392 字节、校验 824,748 字节；M33 写入 167,936 字节、校验 160,336 字节。
- 真实板卡报告布局为 480×800、scale=100%、compact=0、landscape=0、3 列、列宽 144、状态/导航栏 36/64、键盘高度 235；LCD、GPU 显存和触摸均初始化成功。
- 自动测试结果为 `136 PASS / 0 FAIL / 68 actions`，耗时 35,480 ms。新增的五配置计算检查与当前屏幕几何检查通过，原有 Shell、通知/Alert、Search、全部应用、主题偏好、业务控件、路由边界和资源释放测试无回归。
- 稳定运行状态采样为 53 FPS、刷新 2,778 次，当前堆 91,152 / 1,441,752 字节、峰值 122,400 字节、UI 对象峰值 105；测试结束时对象增量 0，路由堆增量 560 字节（分配器高水位，只记录不作泄漏断言）。
- 完整启动与交互日志：`projects/FeatherTalk_M55/build/feathertalk-responsive-board.log`；最终状态采样：`projects/FeatherTalk_M55/build/feathertalk-responsive-status.log`；布局规则见 `RESPONSIVE_LAYOUT_zh.md`。

## 2026-08-29 页面生命周期 HardFault 修复

- 现场使用 Infineon OpenOCD 附加 M55，第一次抓到 `status_timer_cb -> ft_pages_update_system_status -> lv_label_set_text -> lv_free -> rt_memheap_free -> rt_assert_handler`；非法释放参数为 `0x10e`。第二次复现停在 `rt_memheap_free` 第 615 行并进入 HardFault。
- 根因是 System 页面销毁后 `s_system_status_label` 没有清空。原地址随后被 Search 的 Spinner 或普通对象复用，`lv_obj_is_valid()` 只能确认该地址属于某个活动对象，不能确认它仍是原 Label；状态定时器因而发生对象类型混淆。
- 新增统一 `track_object()`：所有页面级全局 LVGL 控件均注册 `LV_EVENT_DELETE`，仅当被删除目标仍等于槽位内容时将其清空，避免旧实例销毁时误清理较新的槽位。异步状态 Label 还使用 `lv_obj_check_type()` 作防御检查。
- 新增回归顺序：访问并退出全部业务页面，确认全部临时槽位为空；再次打开 Search，等待 1.6 秒确保状态定时器执行，再点击 Media、Home，并再次检查 Search/Media 槽位释放。
- M55 `-Clean` 干净构建通过：text=822,256、data=3,916、bss=4,304,096 字节；HEX 2,323,893 字节，SHA-256 `72B3FC3C34C52A71BCECC2AAEBA1163130477A5422003691EA675F8D5796DE61`。
- OpenOCD 完成 M55 写入 827,392 字节、校验 826,172 字节；未变化的 M33 Signed/Secure 镜像写入 167,936 字节、校验 160,336 字节。
- 板端完整结果为 `144 PASS / 0 FAIL / 71 actions`，耗时 39,288 ms；新增 8 个生命周期断言全部通过，对象增量仍为 0。
- 两次相隔 20 秒的状态采样保持 52–53 FPS，刷新数从 3,329 增至 4,390，最终为 6,523；当前堆稳定为 92,104 / 1,441,752 字节，峰值 123,448 字节，IPC err=0。
- 修复后再次用 GDB 附加，M55 正常停在 idle 线程 `rt_thread_defunct_dequeue()`，两个 System 动态 Label 槽位均为 `NULL`，未进入 Assert/HardFault；detach 后刷新和 MSH 继续正常。
- 完整日志：`projects/FeatherTalk_M55/build/feathertalk-lifecycle-fix-board.log`；延时状态：`feathertalk-lifecycle-fix-status-delay.log`、`feathertalk-lifecycle-fix-status-final.log`。

## 2026-08-29 通知面板跟手手势

- 通知面板不再只有状态栏点击开关。状态栏接收 `PRESSED/PRESSING/RELEASED`，关闭状态可直接向下拖动；展开后的面板接收同一套事件，可直接向上推回。
- 按下时立即停止未完成的吸附动画；拖动期间把触点位移映射到面板 Y 坐标，并严格限制在 `-notification_height` 到 `status_bar_height` 之间，因此面板与手指逐帧同步且不会越界。
- 松手时优先参考最后一段移动方向；方向不明确时按展开比例是否越过中点决定打开或关闭。最终使用 180 ms ease-out 动画吸附，轻触未发生拖动时仍保留原来的点击切换。
- 状态栏右侧信息容器不再截获触摸；状态栏和通知面板均使用 LVGL `PRESS_LOCK`，手指离开初始对象范围后仍可完成手势。拖动后的 `CLICKED` 会被抑制一次，避免松手吸附后又被点击事件反向切换。
- Back 在通知面板展开或处于中间位置时优先关闭面板而不改变路由；Home/Search 在执行路由动作前关闭面板。
- 自动测试新增实时中间 Y、向上关闭吸附、向下展开吸附、Back 关闭且路由不变等检查。板端完整结果为 `152 PASS / 0 FAIL / 73 actions`，耗时 40,396 ms。
- M55 构建结果：text=824,072、data=3,916、bss=4,304,112 字节；HEX 2,329,007 字节，SHA-256 `2742B12E61BC56A0896880985C21D36C1EDB8D52D68910A9D9FBDC1D8B17F5D6`。M33 复用未变化的 Signed/Secure 合并 HEX，SHA-256 `1F05A88AE42C9912C940C8CCB6F3B65B62116C89FC32BE88B1CC1788F0D40797`。
- 使用 Infineon Customized OpenOCD 5.19.0.4782 和 KitProg3 `0D141868022E2400` 完成双核写入与校验；稳定状态为 53 FPS、刷新 2,749 次、当前堆 92,528 / 1,441,752 字节、峰值 123,872 字节、对象峰值 91、路由对象增量 0、IPC err=0。
- 完整板端日志：`projects/FeatherTalk_M55/build/feathertalk-notification-drag-board.log`；状态采样：`projects/FeatherTalk_M55/build/feathertalk-notification-drag-status.log`。

## 2026-08-29 双核板验收

- M55 与 M33 均完成 `-Clean` 干净构建；M33 的 Secure 镜像由 SDK 官方 Edge Protect 流程签名、重定位和合并。
- OpenOCD 对 M55 外部 SMIF XIP 镜像和 M33 Secure/Non-secure 合并镜像均完成写入与 `verify_image` 校验。
- 最终自动测试：`134 PASS / 0 FAIL / 68 actions`，结束时页面为 Home、路由深度 1、偏好恢复默认、通知中心关闭。
- 路由对象增量为 0；堆增量为 560 字节，按 RT-Thread 分配器缓存/高水位只记录不作泄漏断言。
- 实测快照：53 FPS，刷新 1858 次，堆 85,816 / 1,441,752 字节，峰值 108,120 字节，UI 对象峰值 81。
- IPC 快照：应用层 tx/rx/err = 48/97/0，驱动层 tx/rx = 48/97；随后周期日志仍为 err=0。
- 系统状态 seq=49，RTC 时间有效且 RTC 存在（flags `0x03`）；battery=`255`、network=`0` 表示未知/不可用，符合当前驱动能力。
- 资源转换器使用参考仓库 `stars.png`（320×480）实测通过：ARGB8888 数据 614,400 字节，RGB565 数据 307,200 字节，生成描述符的宽、高、stride 和 data_size 正确。
- M55 HEX：2,085,720 字节，SHA-256 `706D34EFB1A89BDAD542D15758D4156C5BC38989996B13860B62F59F70E036DC`。
- M33 最终合并 HEX：451,039 字节，SHA-256 `58731F9A3F0C98357D3E496A78479396DD80EF9BF7A9C074736C62470A5CA3D6`。
- 启动与交互日志：`projects/FeatherTalk_M55/build/feathertalk-ui-v2-final.log`。
- 最终 MSH 验收日志：`projects/FeatherTalk_M55/build/feathertalk-ui-v2-status.log`。

## 2026-08-29 P0 通知与快捷面板最终验收

- FeatherTalk IPC 升级至 ABI 3。M33/M55 共同使用固定 16 字节快捷状态与命令帧；当前板上 `caps=0x0000`，Wi-Fi、蓝牙和旋转均显示不可用且不能误触发。亮度由 M55 `pwm18` 实际控制，测试结束恢复为 100%。
- 通知面板支持顶部向下和面板内向上逐帧跟手，遮罩透明度与面板位置同步；松手按时间采样速度或距离阈值吸附。遮罩、Home、Back、Search 和上滑关闭路径均通过测试。
- 通知模型固定容量 8 条，稳定 ID 与 LVGL 对象生命周期解耦；未读计数、打开已读、左右滑动单删、全部清除和溢出丢弃最旧项均通过板测。
- M55 `-Clean` 构建：text=843,112、data=3,764、bss=4,305,592 字节；HEX 2,382,136 字节，SHA-256 `1D180A952F310F6B02EB67D2987FB9DA87AC38D37DD047EAB2BA2CA963B07A91`。
- M33 `-Clean` 构建及官方 Edge Protect 签名/重定位/合并通过；最终 HEX 451,579 字节，SHA-256 `146A40777C44D0758238ECF7E18809D7A29962D7C06D5156B7AE3C18F79796E5`。
- 使用 Infineon Customized OpenOCD 5.19.0.4782 与 KitProg3 `0D141868022E2400` 烧录并校验最终 clean 产物：M55 写入 851,968 字节、校验 846,876 字节；M33 写入 167,936 字节、校验 160,528 字节。
- 最终自动测试为 `185 PASS / 0 FAIL / 99 actions`，耗时 47,952 ms；结束时 page=0、路由深度 1、通知 0、未读 0、亮度 100、对象溢出 0、路由对象增量 0。
- 最终状态为 53 FPS、刷新 3,628 次、堆 104,088 / 1,441,752 字节、峰值 137,144 字节、UI 对象峰值 92；IPC 状态序号 91，持续日志 err=0。
- 完整交互日志：`projects/FeatherTalk_M55/build/feathertalk-quick-shade-board.log`；最终 clean 固件状态：`projects/FeatherTalk_M55/build/feathertalk-quick-shade-clean-status.log`。

## 2026-08-29 Search 键盘覆盖层修复

- 根因是键盘原来作为可滚动 Search 页面中的最后一个 flex 子对象，排在结果列表之后；显示键盘只是取消 `HIDDEN`，因此它会扩大页面内容高度，用户仍需向下滚动才能看到并操作完整键盘。
- Search 现使用独立路由根容器，可滚动内容页与键盘托盘为同级对象。键盘托盘固定贴在内容区域底部，480×800 板上的覆盖高度为 235 px，不再参与内容页排版或随内容滚动。
- 键盘托盘顶部新增 `Hide keyboard` 向下收起按钮；LVGL 键盘原生取消键和完成键也会收起。收起后即使 Textarea 仍处于焦点状态，再次点击同一输入框仍可重新唤起。
- 自动测试增加 10 个断言，覆盖点击唤起、固定底部几何、独立层级、显式收起按钮、收起状态、同输入框重开、原生取消键收起及页面销毁后的槽位清理。最终结果为 `195 PASS / 0 FAIL / 103 actions`，耗时 49,836 ms。
- M55 `-Clean` 构建：text=844,976、data=3,764、bss=4,305,600 字节；HEX 2,387,372 字节，SHA-256 `41B20FB26129E25D552D32777C0B266208BA79F367C0227022CA126BEBA2E0D1`。
- 使用 Infineon Customized OpenOCD 5.19.0.4782 将该 clean HEX 写入 851,968 字节、校验 848,740 字节；板端最终为 52 FPS、堆 104,088 / 1,441,752 字节、峰值 137,144 字节、对象峰值 110、路由对象增量 0、IPC err=0。
- 板端回归日志（含键盘专项断言和 COMPLETE）：`projects/FeatherTalk_M55/build/feathertalk-keyboard-overlay-board.log`；最终状态：`projects/FeatherTalk_M55/build/feathertalk-keyboard-overlay-status.log`。

## 2026-08-29 状态栏无线状态与图标修复

- 蓝牙旧 SVG 的折线端点连接顺序错误，会把左下分支串入主轮廓。现已替换为标准蓝牙实体符文的闭合 polygon：竖向主干与上下两个三角分支边界清楚，不再依赖会产生错误连线的多段 polyline。旋转图标也改为居中设备轮廓和位于外侧的双向旋转箭头。
- 状态栏新增独立的 Wi-Fi 和 Bluetooth 图标对象。不可用、可用但关闭、已开启但未连接、已连接四种状态分别使用不同的颜色/透明度；不会再用文本 `on` 代替无线状态。
- 新增 `wifi_off`、`wifi_weak`、`wifi_medium` 三个 SVG，与原有完整 Wi-Fi 图标组成断开/弱/中/强四档。已连接且有有效百分比时按 `0..33`、`34..66`、`67..100` 映射三档信号；断开显示带斜线图标，未知强度不显示虚构百分比。
- FeatherTalk IPC 升级为 ABI 4，快捷状态帧保持 16 字节，增加 `connected` 位图与 `wifi_signal_percent`。快捷面板会显示 `Off`、`Not connected`、`Connected` 或 `Connected N%`；Bluetooth 同样区分开启、断连和已连接。
- 当前 M33 产品配置没有启用 Wi-Fi/蓝牙板级驱动，真实板上 `caps=0x00 enabled=0x00 connected=0x00 wifi-signal=255`。因此状态栏以灰色不可用样式显示两个无线图标，这是能力契约的真实结果，不代表驱动已经连接。
- 图标生成结果为 25 个图标、24/32/48 三档、97,600 字节 A8 数据。M55 `-Clean` 构建为 text=858,792、data=3,768、bss=4,305,628 字节；HEX 2,426,261 字节，SHA-256 `4CA0A3BB97F932C5EDB60ACF13EA1F90E55B225E475C6F062123196C858965D9`。
- M33 `-Clean` 和官方 Edge Protect 签名/重定位/合并通过：text=73,236、data=1,040、bss=261,497 字节；最终 HEX 451,608 字节，SHA-256 `9586E55E7B6A9D01C8E5D3E02F1A3353B381D13D55CC860F0B753A7571A8DDCA`。
- 使用 Infineon Customized OpenOCD 5.19.0.4782 与 KitProg3 `0D141868022E2400` 烧录并校验 clean 产物：M55 写入 864,256 字节、校验 862,560 字节；M33 写入 167,936 字节、校验 160,536 字节。
- 自动测试新增状态栏双无线对象、四档 Wi-Fi 图标映射和不可伪造连接/信号三项断言。最终结果为 `198 PASS / 0 FAIL / 103 actions`，耗时 50,229 ms；最终采样为 50 FPS、路由对象增量 0、IPC err=0。
- 板端回归日志：`tools/freather/serial-monitor/logs/radio-status-clean-board.log`；最终状态：`tools/freather/serial-monitor/logs/radio-status-clean-status.log`。

## 已知边界

- 电池、电源采样、Wi-Fi/网络、蓝牙、屏幕旋转和外部存储的板级驱动尚未在产品配置中启用；UI 与 ABI 4 IPC 已具备能力、启用、连接和 Wi-Fi 信号强度接入点，并明确显示不可用。显示亮度已经接入 M55 `pwm18`。
- 当前偏好仅在本次启动期间保存，这是原计划要求的“内存桩”；持久化需要后续选定 FlashDB/EasyFlash 或产品配置服务。
- 自动测试宏当前为板级验收而启用；发布固件应关闭 `CONFIG_FEATHERTALK_UI_TEST_MODE`。

后续每次板测继续在本文末尾追加构建、镜像、自动测试和 IPC 结果。
