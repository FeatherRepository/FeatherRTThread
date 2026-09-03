# FeatherTalk Windows Phone UI 移植进度

更新时间：2026-08-30

本文档以“代码完成、自动测试覆盖、真实双核板验证”作为完成标准。没有硬件数据源的功能必须显示为不可用，不允许用虚构数据代替。

| 阶段 | 当前状态 | 验收标准 |
|---|---|---|
| Shell 骨架 | 已完成、已板测 | Home、状态栏、导航栏、Alert 和通知快捷面板均可交互 |
| 系统通知与快捷面板（P0） | 已完成、已板测 | 跟手/距离速度吸附、真实快捷状态、通知队列、完整关闭路径和全量自动测试 |
| 桌面数据模型 | 已完成、已板测 | App 注册表、公共/私有 Tile 属性、动态内容策略、Start 编辑、All Apps、Home/Back/Search 均有自动测试 |
| 路由与生命周期 | 已完成、已板测 | push/pop/home、深度 8、溢出拒绝、资源释放和对象泄漏检查 |
| 主题与偏好 | 已完成、已板测 | 强调色、Tile 透明度、背景选择及内存偏好后端 |
| 资源策略 | 已完成、已板测 | SVG 源、A8 三档生成、统一图标 API、RGB565/ARGB8888 规则和许可证清单 |
| 响应式布局 | 已完成、已板测 | 五种纵横屏配置计算自测、真实 480×800 控件几何检查和完整交互测试 |
| 系统状态 IPC | 已完成、已板测 | ABI 4 携带系统状态、快捷能力/启用/连接/信号强度和命令结果；M55 状态栏、System 页、Live Tile 和快捷面板实时显示 |
| 业务页面 | 已完成、持续板测 | Settings、Media、Gallery、Files、System/Device 和 About 均有真实交互或状态；无 SMS 硬件的 Messages 已下架 |
| 动画与性能 | 已完成、已板测 | Live Tile、跟手面板/遮罩/速度吸附、Search Spinner、FPS、峰值内存和切页泄漏 |

## 本轮实现对应原移植顺序

1. Shell 使用 LVGL 9.2 原生对象；通知中心支持从状态栏跟手下拉、在面板内跟手上推、释放吸附及点击切换；Alert 使用 Message Box，底栏为 Back/Home/Search。
2. 六个 App 由静态描述符驱动，Start Tile、All Apps 列表和 Search 结果共用同一数据源。
3. 路由栈容量固定为 8；测试覆盖 push/pop/home、溢出拒绝、深层释放和返回 Start。
4. 偏好后端按计划使用内存桩，保存强调色、Tile 透明度、背景模式和 revision；生产持久化后端仍可在不改 UI API 的情况下替换。
5. 资源格式与位置规则见 `UI_ASSET_POLICY_zh.md`，转换工具为 `tools/freather/ui-asset-convert.py`。
6. M33 每个心跳发送 16 字节系统状态帧和 16 字节快捷状态帧。RTC 有效时发送 Unix 时间；当前板级电源、电池、Wi-Fi/网络和旋转驱动未启用。M33 蓝牙 Host 已单独打通，但尚未映射到 ABI 4 快捷状态帧，因此 M55 端蓝牙字段仍使用 `unknown/unavailable` 和显式能力位，不填充虚构数据。
7. System 显示双核和性能状态；Settings 修改真实偏好；Media 支持上一首、播放/暂停、下一首和音量；Gallery 浏览 Flash/SD 图片并设置壁纸；Files 刷新真实的存储可用性；About 显示产品/固件/ABI 版本。
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

## 2026-08-29 UI 背光硬件闭环

- 通知快捷面板的亮度滑块保持 `0..100%` 用户范围，但线性映射到 M55 `pwm18` 通道 0 的 `50..100%` 物理占空比；周期保持 200,000 ns（5 kHz）。因此 UI 最低亮度仍有物理 50% 亮度，不再允许日常操作把屏幕直接调黑。
- 修复了 LCD 驱动启动时实际设置 80%，而 UI 缓存初值为 100% 的状态不一致。产品 UI 不再只相信缓存：读取和设置后均通过 `PWM_CMD_GET` 读取实际 period/pulse 并换算百分比；读回失败时才使用最近缓存值。该实现仅位于 FeatherTalk 产品层，没有改动 SDK 原生 PWM/LCD 驱动。
- 首轮现场验证暴露出寄存器读回的盲区：`board.c` 为上电时序把 P20_6 临时设为 GPIO，`applications/main.c` 又在第一帧后把它固定为 GPIO 高电平，因此 TCPWM 占空比虽在变化却无法到达面板。现保留 P15_7 背光电源的延时开启，并在第一帧后用 BSP 生成配置把 P20_6 恢复到 `TCPWM0 line 265`；UI 可用性还会核对真实 HSIOM，状态命令报告 `pwm-routed=1`，以后该故障会直接使自动测试失败。
- 自动测试完整覆盖物理启动 80%（映射为 UI 60%）、快捷按钮 UI 30%、滑块 UI 65%、UI 最低 0%（物理 50%）、恢复 65%，并在测试结束恢复 UI/物理 100%。最终结果为 `203 PASS / 0 FAIL / 105 actions`，耗时 51,101 ms。
- M55 `-Clean` 构建通过：text=862,008、data=3,768、bss=4,305,628 字节；HEX 2,435,306 字节，SHA-256 `63B5DE5A54537CD2FD35292400847079A46E7558136AF652CC7D86034875FF93`。
- 使用 Infineon Customized OpenOCD 5.19.0.4782 和 KitProg3 `0D141868022E2400` 完成双核写入与校验：M55 写入 868,352 字节、校验 865,776 字节；M33 写入 167,936 字节、校验 160,536 字节。
- 测试结束后又用 MSH 直接确认新的安全下限：period=200,000、pulse=100,000、Duty=50%，随后恢复 pulse=200,000；UI 路由状态保持 `pwm-routed=1`、IPC err=0。
- 安全范围完整回归日志：`tools/freather/serial-monitor/logs/brightness-safe-range-board.log`；50% 物理下限读回：`brightness-safe-min-pwm.log`；最终恢复记录：`brightness-safe-restore.log`。

## 2026-08-29 Start Tile 应用模型与桌面编辑

- `ft_app_descriptor_t` 不再只包含应用名、图标和 `wide_tile`。现在明确拆分 `ft_tile_common_properties_t` 与 `ft_tile_private_properties_t`：公共层包含名称、列/行跨度、单 Tile 透明度和背景图案；私有层由应用提供图标、是否循环、周期、动态内容回调及上下文。
- Start 桌面为每个描述符创建独立运行时模型。公共属性可以在不修改应用页面的情况下改变；最终透明度为单 Tile 透明度与全局主题透明度的乘积。动态内容回调现在接收一个透明 LVGL 内容宿主，应用可自行创建和更新宿主内的任意控件；Media 和 Messages 已作为动态文字示例，System 保持 M33 外部状态驱动，后续 Gallery 可直接在同一接口中创建 `lv_image` 并切换图片帧。
- 普通短按使用 LVGL `LV_EVENT_SHORT_CLICKED` 打开应用；只有 `LV_EVENT_LONG_PRESSED` 才进入编辑态并显示轻微呼吸缩放动画。不能使用 `CLICKED` 代替短按，因为 LVGL 在长按松手后同样可能发送 `CLICKED`。四个手柄圆心精确落在 Tile 外轮廓四角；左上角使用 LVGL 线段直接绘制单一、居中的四向移动符号，右上、左下、右下是居中的斜向双向缩放符号，不依赖字体字形或新的位图资源。
- 移动开始时，选中 Tile 使用 `LV_OBJ_FLAG_FLOATING` 脱离 Flex 布局并跟随触点，同时原位置放入等尺寸占位符。占位符随触点越过其他 Tile 中心而实时重排，因此所有兄弟 Tile 会立即让位；松手后浮动对象替换占位符并吸附到合法顺序。
- 缩放拖动期间使用连续像素尺寸并触发 Flex 实时重排；松手时按当前响应式布局的列宽、基础行高和间距取整。列跨度限制为当前设备的 `tile_columns`，行跨度限制为 1..3，因此布局不会保存与 480×800 绑定的绝对尺寸。
- 点选中 Tile、点桌面空白、切换 All Apps 或执行 Home/Back 会退出编辑并恢复滚动。自动测试新增公共/私有描述符、长按编辑、四手柄、占位符排序、2×2 缩放、兄弟重排、名称/透明度/图案修改、应用动态帧和默认布局恢复检查。
- UI 事件层修复了两处问题：应用跳转曾监听长按后也会产生的 `CLICKED`；手柄曾监听 `LV_EVENT_ALL` 并在判断事件类型前无条件选中 Tile。现在应用只响应 `SHORT_CLICKED`，Tile 只响应 `LONG_PRESSED`，手柄只注册 `PRESSED/PRESSING/RELEASED/PRESS_LOST` 且仅在 `PRESSED` 中选中。
- 第二次板上排查确认仅修 UI 事件仍不够。最终按 ST7123 host protocol 修正为全 16 位寄存器寻址：`0x0010` bit 3 给出 With Coord，置位时必须从 `0x0014` 读到最后一个支持的坐标槽以自动清 INT。每槽只按协议定义的 Valid 位确认触点，不再错误要求非零 Touch Intensity；成功读到无坐标就是松手，只有 I2C 传输错误才短暂维持旧状态，并过滤 3 px 内静止抖动。LVGL 长按阈值明确设为 500 ms，滚动阈值由 10 px 提升至 18 px。
- `feather_ui_status` 新增 Tile 编辑态、选中序号以及触摸 `frames/held/press/release` 计数。修复后的无人触摸基线为 `frames=0 held=0 press=0 release=0`，没有幽灵触摸；页面 Home、路由深度 1、`editing=0 selected=-1`。
- M55 最终 `-Clean` 构建通过：text=876,784、data=3,768、bss=4,307,548 字节；HEX 2,476,857 字节，SHA-256 `E8AE2A5CE19420B4C4BD3812F73E522BF5F90E85027128611637ADA4B9C86597`。
- 使用 Infineon Customized OpenOCD 5.19.0.4782 与 KitProg3 `0D141868022E2400` 烧录并校验该 clean M55 产物：写入 884,736 字节、校验 880,552 字节。M33 代码未变化，板上继续运行已签名并验证的 ABI 4 镜像。
- 最终全量自动测试为 `221 PASS / 0 FAIL / 114 actions`，耗时 51,510 ms；Tile 专项检查单独 `PRESSED` 不编辑也不跳转，`LONG_PRESSED` 才选中，随后 `PRESS_LOST + CLICKED` 仍保持编辑且路由深度为 1。其余移动、重排、缩放、吸附、属性、动态帧和默认恢复测试全部通过；最终路由对象增量为 0。
- 最终状态为 55 FPS、刷新 2,813 次、当前堆 133,528 / 1,441,752 字节、峰值 459,784 字节、对象峰值 172、IPC err=0；页面为 Home、路由深度 1、亮度 100%、`pwm-routed=1`。
- 完整 clean 板端日志：`tools/freather/serial-monitor/logs/touch-long-press-driver-clean-board.log`；最终状态：`tools/freather/serial-monitor/logs/touch-long-press-driver-clean-status.log`。

## 2026-08-29 Start Tile 边界与手柄视觉修复

- 缩放上限不再只使用屏幕总列数。每次按下缩放手柄都会读取当前 Tile 与桌面可见内容区的绝对坐标，计算从当前位置到右、下边界仍可容纳的完整列/行跨度；连续像素缩放和松手后的网格吸附都受同一上限约束。达到边界后停止增长，不再为了满足拖动距离把选中 Tile 强制换到下一行或下一列。
- 四个可见手柄由 36 px 缩小为当前 480×800 配置下的 29 px 奇数直径，圆心因此可精确对齐 Tile 角点；额外 8 px 的不可见点击扩展保留约 45 px 的触摸命中范围。Tile 和桌面容器允许手柄越界绘制，圆圈不会再被压进矩形内部。
- 移动图标不再叠加两组字体 `↔`。现在由 LVGL `lv_draw_line()` 绘制一组共享中心的水平/垂直轴和四个外向箭头；三个缩放图标也使用同一绘制路径生成斜向双箭头，图形中心直接取圆形手柄的几何中心，不再受字体旋转基点影响。
- 自动测试新增 `tile.handles.geometry`，逐一检查手柄为 25..35 px 的等宽奇数圆、没有残留字体子对象，且圆心与对应 Tile 角点误差不超过 1 px；新增 `tile.resize.boundary`，对当前最右 Tile 施加超大拖动并确认 X/Y 起点不变、右边缘不越界、列跨度不超过本次手势上限。
- 最终 M55 `-Clean` 构建通过：text=879,352、data=3,768、bss=4,307,564 字节；HEX 2,484,086 字节，SHA-256 `AF4AF1D354A67B8BC533CEC82FA3ADE3829B0E4AEC3AB0273057ABE9BBA14E2A`。
- 使用 Infineon Customized OpenOCD 5.19.0.4782 与 KitProg3 `0D141868022E2400` 烧录并校验 clean M55：写入 884,736 字节、校验 883,120 字节。M33 未改动，继续使用板上已验证的 ABI 4 镜像。
- clean 板端全量测试为 `223 PASS / 0 FAIL / 114 actions`，耗时 54,878 ms；新增两项断言与原有长按、移动、重排、缩放、全部页面和资源释放检查均通过。最终状态为 52 FPS、刷新 3,219 次、堆 122,504 / 1,441,752 字节、峰值 368,120 字节、对象峰值 142、路由对象增量 0、IPC err=0。
- 完整日志：`tools/freather/serial-monitor/logs/tile-boundary-handle-clean-board.log`；最终状态：`tools/freather/serial-monitor/logs/tile-boundary-handle-clean-status.log`。

## 2026-08-29 Start Tile 扩展绘制与显存验证

- 显示链路已按实际实现核对：LVGL 使用两个 160 行局部绘制缓冲区和 `LV_DISPLAY_RENDER_MODE_PARTIAL`，脏区最终合入 LCD 驱动的常驻 RGB565 `graphics_storage`。该显存位于运行时地址 `0x263A5000`，可见区域 480×800、物理 stride 512 像素，总长度 819,200 字节，再由 GFXSS/MIPI 扫描到面板。
- 使用 OpenOCD 直接读取上述常驻显存。修复前稳定抓帧 `tools/freather/serial-monitor/logs/tile-render-stable-before-fix.png` 已在送屏前出现四角圆圈仅剩局部圆弧，证明截断不是 LCD 或 MIPI 传输造成。根因是仅设置 `LV_OBJ_FLAG_OVERFLOW_VISIBLE`，但 Tile/父容器扩展绘制尺寸仍为 0；LVGL 的父级裁剪与脏区失效都不会覆盖圆圈伸出对象边界的区域。
- Tile 和桌面容器现响应 `LV_EVENT_REFR_EXT_DRAW_SIZE`，按响应式比例报告 32 px 扩展绘制区，并在创建后刷新扩展尺寸。手柄显示/隐藏前后均使整个 Tile 失效；退出编辑时先隐藏手柄再移除边框，避免角点基准先内缩而遗留旧像素。自动测试的 `tile.handles.geometry` 同时断言 Tile 与容器实际扩展绘制尺寸达到要求。
- 同一常驻显存的修复后稳定抓帧 `tools/freather/serial-monitor/logs/tile-render-stable-after-fix.png` 显示四个完整圆圈和居中的箭头；手柄隐藏后的 `tools/freather/serial-monitor/logs/tile-render-hidden-after-fix.png` 中原四角区域已恢复桌面背景，没有绘制残留。
- 撤掉临时抓帧等待逻辑后，正式 M55 `-Clean` 构建通过：text=879,592、data=3,768、bss=4,307,564 字节；HEX 2,484,761 字节，SHA-256 `6504741BE483A3D6267B5A2534B44E6831B293B4CFD57F9FDC6B9EFD7674A53D`。
- 使用 Infineon Customized OpenOCD 5.19.0.4782 与 KitProg3 `0D141868022E2400` 烧录并校验正式 M55 镜像：写入 884,736 字节、校验 883,360 字节。板端全量自动测试为 `223 PASS / 0 FAIL / 114 actions`，耗时 54,681 ms；最终为 51 FPS、刷新 1,853 次、当前堆 122,744 / 1,441,752 字节、峰值 492,312 字节、对象峰值 142、路由对象增量 0、IPC err=0。
- 正式回归日志：`tools/freather/serial-monitor/logs/tile-ext-draw-clean-board.log`；最终状态：`tools/freather/serial-monitor/logs/tile-ext-draw-clean-status.log`。

## 2026-08-29 Start Tile 前景层级与双轴锚定缩放

- 先前只把四个手柄移动到 Tile 自己的子对象末尾；同一桌面容器中创建时间更晚的兄弟 Tile 仍在选中 Tile 之后绘制，因此越过边界的圆圈可能被相邻 Tile 覆盖。现在长按进入编辑态时立即创建等尺寸 Flex 占位符，把选中 Tile 设为 `LV_OBJ_FLAG_FLOATING` 并移动到容器最后一个子对象。选中 Tile、边框和四个手柄在整个编辑期间始终最后绘制，移动或缩放结束也继续保持前景，只有退出编辑时才以占位符的逻辑序号重新并回布局。
- 占位符不只服务移动。缩放过程中它同步接收选中 Tile 的连续像素尺寸并驱动所有兄弟 Tile 实时回流，而前景 Tile 保持独立坐标，因此层级提升不会破坏原有“其他标签跟随让位”的行为。
- 三个缩放角改为标准对角锚定：右上角只移动右边和上边，固定左边与下边；左下角只移动左边和下边，固定右边与上边；右下角只移动右边和下边，固定左边与上边。每次手势分别按其固定对边计算到桌面四周的可用宽高，达到边界后仅停止对应轴增长，不改变另外两条边，也不强制换行扩展。
- `tile.handles.geometry` 现在同时断言选中 Tile 具有 `FLOATING` 标志、占位符有效且选中 Tile 是容器最后绘制的子对象。新增 `tile.resize.anchors` 使用真实 2×2 Tile 分别拖动 TR/BL/BR 三个角，逐像素检查宽高变化以及两条对边坐标完全不变。
- 正式 M55 `-Clean` 构建通过：text=881,192、data=3,768、bss=4,307,564 字节；HEX 2,489,261 字节，SHA-256 `111FEBFD53A22C5D34D2E0D0D0ACA21939D33BC83258B82D8900DFE68A47D426`。
- 使用 Infineon Customized OpenOCD 5.19.0.4782 与 KitProg3 `0D141868022E2400` 烧录并校验：写入 888,832 字节、校验 884,960 字节。clean 板端全量测试为 `224 PASS / 0 FAIL / 114 actions`，耗时 55,032 ms；最终为 51 FPS、刷新 1,881 次、当前堆 122,776 / 1,441,752 字节、峰值 591,472 字节、对象峰值 142、路由对象增量 0、IPC err=0。
- 正式回归日志：`tools/freather/serial-monitor/logs/tile-foreground-anchor-clean-board.log`；最终状态：`tools/freather/serial-monitor/logs/tile-foreground-anchor-clean-status.log`。

## 2026-08-29 Start Tile 二维就近吸附

- 原移动逻辑只寻找距离指针最近的兄弟 Tile，再用指针位于该兄弟前半或后半来推断逻辑序号。Flex 自动换行后，逻辑序号和屏幕二维距离并不等价，因此把小 Tile 拖到桌面底部时，可能被上方较早出现的空位吸走。
- 新的吸附解析器枚举全部合法插入序号，每次让现有占位符经过真实 Flex 布局后读取其屏幕中心，并选择与被拖动 Tile 中心欧氏距离平方最小的候选。等距离时优先选择相对原序号移动较少的候选，避免边界位置无意义抖动。这样只有上方空位在几何上确实更近时才会吸附到上方；靠近底部释放会保留在最近的下方合法落点。
- 候选枚举全部发生在同一个 LVGL 输入事件内，显示刷新只会看到最终选中的占位符位置；兄弟 Tile 仍由占位符驱动实时回流。该方案选择的是当前 Flex 桌面能够形成的最近合法落点，不引入脱离布局的永久自由坐标。
- 新增 `tile.move.nearest` 板端测试：从真实 Flex 候选中取得最下方落点的实际屏幕中心作为释放目标，验证二维解析器选择该下方落点而不是上方空位，并在测试结束后恢复原序号。
- 正式 M55 `-Clean` 构建通过：text=881,440、data=3,768、bss=4,307,564 字节；HEX 2,489,952 字节，SHA-256 `786761224FE90EA8A04E677612B9F9E2D5A7711A5DA6D7884F1ADE8CADC4E3B2`。
- 使用 Infineon Customized OpenOCD 5.19.0.4782 与 KitProg3 `0D141868022E2400` 烧录并校验：写入 888,832 字节、校验 885,208 字节。clean 板端全量测试为 `225 PASS / 0 FAIL / 114 actions`，耗时 54,940 ms；最终为 50 FPS、刷新 1,965 次、当前堆 122,776 / 1,441,752 字节、峰值 591,472 字节、对象峰值 142、路由对象增量 0、IPC err=0。
- 正式回归日志：`tools/freather/serial-monitor/logs/tile-nearest-slot-clean-board.log`；最终状态：`tools/freather/serial-monitor/logs/tile-nearest-slot-clean-status.log`。

## 2026-08-29 Start Tile 交互结束整体收敛

- 此前移动结束只把前景 Tile 放到占位符坐标；缩放结束则只固定选中 Tile 和占位符的最终尺寸，没有强制执行最终 Flex 布局，也没有把前景 Tile 对齐到换行后的占位符。缩放触发换行时，选中 Tile 可能仍停在手势锚点，而其他 Tile 已按占位符排列，导致整个桌面没有形成一致的最终状态。
- 移动和缩放现在共用 `tile_settle_layout_for_edit()`：释放后先删除临时占位符、移除选中 Tile 的 `FLOATING` 标志，把真实 Tile 插入占位符的最终逻辑序号，并强制完成一次 Flex 布局。这一步让全部真实 Tile 同时落到最终位置。
- 整体布局收敛后，再以选中 Tile 的最终尺寸和序号重建占位符，把同一个 Tile 提升为容器最后绘制的前景对象并精确对齐占位符，然后恢复呼吸动画。选中边框和四角手柄继续保留，用户不需要重新长按；再次强制布局时全部 Tile 坐标保持不变。
- 新增 `tile.move.settled` 与 `tile.resize.settled` 板端断言，同时检查：交互状态已经结束、选中 Tile 与占位符的位置和尺寸一致、选中 Tile 仍为最后绘制的 `FLOATING` 对象、呼吸动画仍注册，以及全部真实 Tile 在再次强制 Flex 布局后坐标不发生变化。
- 正式 M55 `-Clean` 构建通过：text=882,592、data=3,768、bss=4,307,564 字节；HEX 2,493,192 字节，SHA-256 `EE3AA8400D7001201FFDEECB51886B7DF45DC3D4E3FE625548918B750763B389`。
- 使用 Infineon Customized OpenOCD 5.19.0.4782 与 KitProg3 `0D141868022E2400` 烧录并校验：M55 写入 888,832 字节、校验 886,360 字节。clean 板端全量测试为 `227 PASS / 0 FAIL / 114 actions`，耗时 55,139 ms；最终为 50 FPS、刷新 2,332 次、当前堆 122,776 / 1,441,752 字节、峰值 591,472 字节、对象峰值 142、路由对象增量 0、IPC err=0。
- 正式回归日志：`tools/freather/serial-monitor/logs/tile-settle-clean-board.log`；最终状态：`tools/freather/serial-monitor/logs/tile-settle-clean-status.log`。

## 2026-08-29 Start Tile 行列坑位吸附

- 上一版虽然比较了占位符的真实二维坐标，但候选仍然只是“把占位符插入哪个逻辑序号”。例如三列桌面第二行第三列明明有对齐空位，序列中紧随其后的两列宽 Tile 却会提前换行；单独改变占位符序号无法让后面的窄 Tile 局部补位，因此中间坑位不会成为候选，实际体验仍可能退化成只在上/下或左/右极端之间跳转。
- 新解析器直接枚举桌面的行列对齐坑位。每个候选先在栈上建立占用图，保留目标坑位，再按照当前稳定顺序安置其他 Tile；后面的窄 Tile 可以填补宽 Tile 前面的局部空位。随后按行列顺序模拟 LVGL Flex 的换行、行高、间距和滚动偏移，得到占位符真正会出现的屏幕中心，全程不修改 LVGL 对象。
- 选择规则首先最小化“拖动 Tile 中心到候选实际中心”的二维距离；距离相同时再最小化所有兄弟 Tile 的总位移平方，避免为了等价坑位制造大范围重排。选出唯一方案后才更新一次子对象顺序并执行一次真实布局，因此中间行、中间列与四周坑位地位相同，同时不会把候选试算过程推送到屏幕。
- `tile.move.nearest` 已改为专门覆盖该缺陷：在包含两列宽 Tile 的真实桌面顺序中，把 1×1 Tile 的目标中心放到第二行第三列，断言占位符 X/Y 都精确对齐该中间坑位，并且其 Y 严格位于顶部和底部 Tile 之间。该测试在旧的仅插入序号算法下无法成立。
- 正式 M55 `-Clean` 构建通过：text=885,992、data=3,768、bss=4,307,564 字节；HEX 2,502,761 字节，SHA-256 `0EF67C4A1D808B1590D1325B3358B3017819DCB38CAEDB6C902EB7B250BC07DB`。
- 使用 Infineon Customized OpenOCD 5.19.0.4782 与 KitProg3 `0D141868022E2400` 烧录并校验：M55 写入 892,928 字节、校验 889,760 字节。clean 板端全量测试为 `227 PASS / 0 FAIL / 114 actions`，耗时 55,246 ms；最终为 51 FPS、刷新 1,930 次、当前堆 122,776 / 1,441,752 字节、峰值 591,120 字节、对象峰值 142、路由对象增量 0、IPC err=0。
- 正式回归日志：`tools/freather/serial-monitor/logs/tile-pit-snap-clean-board.log`；最终状态：`tools/freather/serial-monitor/logs/tile-pit-snap-clean-status.log`。

## 2026-08-30 Start Tile 显式网格、确认式移动与碰撞让位

- Start 桌面已从 LVGL Flex 自动紧凑布局切换为响应式显式网格。每个 Tile 持有独立的 `grid_column/grid_row` 与列、行跨度；候选位置覆盖桌面全部合法行列坑位，不再由子对象序号、上/下极端或预先算好的单一路径决定。中间、边缘、空坑和已占用坑位都可以成为移动目标。
- 移动采用两阶段确认状态机。自由拖动且中心尚未进入坑位确认半径时，占位吸附框隐藏，全部兄弟 Tile 保持手势开始时的位置；进入最近坑位的确认半径后吸附框才出现，松手则强制确认离手指最近的坑位。已占用坑位同样允许确认；只有确认发生后，与目标矩形冲突的 Tile 才寻找最近空坑并执行 180 ms ease-out 吸附动画，未冲突 Tile 不动。候选坐标判定前会强制刷新本帧 LVGL 布局，避免读取上一帧坐标而提前让位。
- 缩放继续保持连续跟手，但把“当前像素覆盖跨度”和“松手后的取整跨度”分开计算。拉伸边缘真正进入相邻网格单元时才建立占用，只有被覆盖的 Tile 立即让位；没有冲突的 Tile 始终保持基线坑位，缩回使冲突消失时原 Tile 可动画返回。TR/BL/BR 三个手柄各自固定两条对边，列/行跨度仍受当前桌面可见边界约束，不会强制换行或换列扩大。
- 让位解析器先把未冲突 Tile 作为不可移动障碍，再只为冲突集合按二维距离寻找最近完整空矩形；最终模型增加成对重叠检查。吸附动画修复了 LVGL 对象坐标已经包含 `translate` 时又重复相加的问题，连续重定向不会产生双倍位移、跳跃或残影。
- 自动测试新增并强化 `tile.move`、`tile.move.nearest`、`tile.resize.collision`、`tile.resize.reflow` 与专用默认网格恢复：覆盖未确认阶段不重排、吸附框确认后覆盖已占用坑位并让位、任意中间/边缘/占用坑位、仅碰撞 Tile 带动画迁移、非碰撞 Tile 原位、最终无重叠以及选中 Tile 保持前景和呼吸动画。
- M55 干净构建通过：text=889,800、data=3,768、bss=4,307,644 字节；HEX 2,513,471 字节，SHA-256 `D36D1C31C84A7DADAC5D608CFAFC9CA9F53CFB4706C3383CB011F3DA9FC5BE0B`。
- 使用 Infineon Customized OpenOCD 5.19.0.4782 与 KitProg3 `0D141868022E2400` 完成双核烧录及校验：M55 写入 897,024 字节、校验 893,568 字节；M33 写入 167,936 字节、校验 160,536 字节。最终板端全量测试为 `228 PASS / 0 FAIL / 114 actions`，耗时 54,777 ms；最终为 50 FPS、刷新 2,664 次、当前堆 122,936 / 1,441,752 字节、峰值 590,232 字节、对象峰值 142、路由对象增量 0 / 2,336 字节、IPC err=0。
- 最终回归日志：`tools/freather/serial-monitor/logs/tile-explicit-grid-confirmed-snap-final-board.log`；最终状态：`tools/freather/serial-monitor/logs/tile-explicit-grid-confirmed-snap-final-status.log`。

## 2026-08-30 Start Tile 热路径与导航栏交界修复

- 交互卡顿来自同一个 `PRESSING` 热路径内的重复事务：指针仍在同一确认坑位时会再次运行占用求解、删除并重启动画、强制刷新布局；缩放即使像素尺寸和覆盖网格没有变化，也会再次执行碰撞解析。现在移动仅在确认坑位变化时提交网格事务，缩放仅在尺寸变化时更新几何、在覆盖单元集合变化时解析让位；已经朝同一目标运行的吸附动画不会重启。指针位置直接按手势起点计算，不再逐样本读取一次完整布局。
- 内容区现在明确作为状态栏与导航栏之间的硬裁剪视口。Tile 和桌面容器仍保留扩展绘制区以显示四角手柄，但任何子对象都不能越过内容区边界污染导航栏接缝。
- 通过 OpenOCD 读取 M55 常驻 RGB565 显存 `0x263A5000`（480×800 可见区、512 像素 stride）定位到左下角所谓“脏块”中的实际文字为 LVGL 默认性能监视器。它是系统层上合法存在的半透明控件，位置恰好跨在 Start 内容与导航栏边缘，并不是 LCD/MIPI 推屏残留。产品初始化现调用 `lv_sysmon_hide_performance()` 隐藏该覆盖层；FPS、刷新次数、堆、对象和路由指标仍由 `feather_ui_status` 输出。
- 修复后显存全屏抓帧 `tools/freather/serial-monitor/logs/tile-seam-perf-overlay-hidden.png` 与底部 100 行裁剪 `tile-seam-perf-overlay-hidden-seam.png` 显示：内容区和导航栏蓝色边线连续，左下角性能文字与黑色背景消失，边界没有旧帧矩形残留。修复前对照为 `tile-seam-performance-final.png`。
- 最终 M55 `-Clean` 构建通过：text=890,224、data=3,768、bss=4,307,668 字节；HEX 2,514,657 字节，SHA-256 `6F0A7190B1F093764AD8D805F03EB366455EB0FAA599D97D7881772B1F98B777`。
- 使用 Infineon Customized OpenOCD 5.19.0.4782 与 KitProg3 `0D141868022E2400` 重新烧录并校验 clean 产物：M55 写入 897,024 字节、校验 893,992 字节；M33 写入 167,936 字节、校验 160,536 字节。
- clean 板端全量测试为 `228 PASS / 0 FAIL / 114 actions`，耗时 54,869 ms。最终状态为 54 FPS、刷新 3,695 次、当前堆 122,936 / 1,441,752 字节、峰值 591,320 字节、对象峰值 142、路由对象增量 0 / 2,336 字节、IPC err=0。
- 完整回归日志：`tools/freather/serial-monitor/logs/tile-seam-hotpath-clean-board.log`；最终状态：`tools/freather/serial-monitor/logs/tile-seam-hotpath-clean-status.log`。

### 现场复核：FLOATING 滚动坐标与真实接缝残块

- 上述性能监视器结论只解释了左下角的一类覆盖物，并没有覆盖用户随后指出的新残块。再次直接读取常驻显存后，`tile-seam-live-dirty-1.rgb565` 明确记录到 `x=16..463、y=727..731` 的 448×5 像素强调色旧块：2240 个像素全部为 RGB565 `0x03DA`，位置在导航栏顶线 `y=736` 上方。这证明新现象确实是 Tile 内容的旧帧残留。
- 根因是编辑态 Tile 为保持最高绘制层级而使用了 `LV_OBJ_FLAG_FLOATING`，但移动起点仍来自 `lv_obj_get_x/y()`。LVGL 9.2 的取坐标接口会给浮动子对象加上父容器滚动量，而 `lv_obj_set_pos()` 对浮动子对象使用不带滚动量的本地坐标；Start 页面滚动后两种坐标混用，Tile 会跳离触点，并反复触及内容区硬裁剪边缘。
- 现在浮动对象统一使用 `style_x/style_y` 作为手势本地坐标；进入浮动层和吸附网格时通过 `grid_object_x/y()` 显式抵消桌面滚动量。移动、缩放或滚动事务结束以及最终吸附动画完成时，只执行一次 `tile_repair_viewport()`，既清理硬裁剪边缘暴露区域，也避免回到逐触摸样本整页重绘的卡顿路径。
- 新增板端回归 `tile.move.scrolled`：先把 About Tile 放到第 8 行并产生约 502 px 的真实桌面滚动，再检查提升为浮动层前后屏幕坐标不变，以及 19×13 px 指针位移与 Tile 位移完全一致。该断言通过。
- 修复后的稳定显存抓帧为 `tools/freather/serial-monitor/logs/tile-scroll-coordinate-seam-v2-stable.rgb565`（全屏 PNG 与接缝裁剪同名）。原 2240 像素矩形中强调色和非黑像素均为 0，`y=720..735` 全部为黑色，`y=736` 仅保留 480 像素导航栏顶线。
- 最终 M55 `-Clean` 构建通过：text=891,656、data=3,768、bss=4,307,668 字节；HEX 2,518,691 字节，SHA-256 `96B9A9AB338A258B1C77A58B1348D9643CEC7A2653E8BE0895208057D23D2B04`。使用 Infineon Customized OpenOCD 5.19.0.4782 与 KitProg3 `0D141868022E2400` 重新写入 M55 897,024 字节并校验 895,424 字节；M33 镜像未改变，先前写入 167,936 字节并校验 160,536 字节。
- clean 镜像板端全量测试为 `229 PASS / 0 FAIL / 114 actions`，耗时 54,299 ms。最终状态为 54 FPS、刷新 3,606 次、当前堆 122,712 / 1,441,752 字节、峰值 591,472 字节、对象峰值 142、路由对象增量 0 / 2,112 字节、IPC err=0。完整回归日志：`tools/freather/serial-monitor/logs/tile-scroll-coordinate-seam-clean-board.log`；最终状态：`tile-scroll-coordinate-seam-clean-status.log`。

## 2026-08-30 通知下拉热路径优化与真实帧统计

- 复核确认下滑卡顿不是单纯的合理降帧。LVGL 每 18 ms 对按住状态发送一次 `PRESSING`，旧代码即使触点 Y 没有变化也重复调用 `lv_obj_set_y()` 和遮罩透明度设置；480×700 的遮罩改变透明度会使接近整个内容区失效，同时 480×460 的复杂面板移动会合并旧、新区域。当前软件绘制后端下，这会形成明显过绘制。
- 手势热路径现在只在面板 Y 真正变化时提交位置更新，相同坐标直接计入 skipped；遮罩按 12 个进度等级变化，面板仍连续跟手，但整屏 alpha 层不再按每个像素重绘。拖动和 180 ms 回弹期间暂停一秒状态、Live Tile 与快捷控制刷新，避免后台刷新插入手势帧。
- 通知模型新增单调 revision。无数据变化时，打开空通知或已读队列不会再执行 `lv_obj_clean()` 并重建全部卡片；push/remove/首次 mark-read/clear 才更新 revision。快捷开关、状态栏无线状态、普通标签和外部 Live Tile 均增加值相等短路，避免每秒重复提交相同样式和文本。
- 原 `FPS` 统计实际计数 `LV_EVENT_REFR_READY`；启用 LVGL 性能监视后，即使没有无效区，该事件仍随 18 ms 定时器发生，因此空闲时约 55 不能代表真实显示帧率。现统计分别记录实际 `RENDER_READY` 的 present FPS、调度 refresh Hz、render/flush 总数、刷出像素、每秒像素量和最后/峰值 render 时间。空闲板端结果 `present-fps=0、refresh-hz=55` 是正确含义：没有新画面送出，但调度器正常运行。
- 自动测试新增 `notification.render.cached`、`notification.drag.dedup`、`notification.mask.quantized` 和 `metrics.present`。最终 clean 板端结果为 `233 PASS / 0 FAIL / 114 actions`，耗时 50,955 ms；测试状态中的 shade 计数为 `drag=5、applied=4、skipped=1、render=19/6`，证明重复触点和 6 次无变化重建请求被挡住。最终 IPC err=0，堆 123,224 / 1,441,752 字节、峰值 591,960 字节、对象峰值 142、路由对象增量 0 / 2,528 字节。
- 最终 M55 `-Clean` 构建：text=894,336、data=3,768、bss=4,307,780 字节；HEX 2,526,222 字节，SHA-256 `69A6BBA4DDEEF0228A8C3ABA1D20A2FABAA92453FBC91A3993AFB09EB963B6D1`。Infineon Customized OpenOCD 5.19.0.4782 写入 M55 901,120 字节并校验 898,104 字节；M33 写入 167,936 字节并校验 160,536 字节。
- 完整回归日志：`tools/freather/serial-monitor/logs/shade-perf-optimized-clean-board.log`；最终状态：`tools/freather/serial-monitor/logs/shade-perf-optimized-clean-status.log`。

## 2026-08-30 设置应用信息架构与板级能力筛选

- 将参考照片仅作为 Windows Phone 风格的信息架构参考，没有照搬其手机功能。Settings 从单页个性化表单升级为“设置主页 + 本地搜索 + 路由子页”：主页提供固定覆盖式软键盘、收起键、名称/摘要/关键字过滤，以及 4 个真正可配置的分类入口；子页继续使用已有的有界路由，因此 Back 返回设置主页、Home 返回桌面，资源释放仍由统一生命周期管理。
- 分类收紧为显示与亮度、Wi-Fi、蓝牙和个性化。通知队列仍归通知下拉面板管理，存储状态归 Files，系统诊断和 About 保持独立应用；没有可修改后端的 RTC/语言/键盘也不占用设置入口。参考照片里的蜂窝网络、SIM、电话、移动热点、VPN、NFC、账户和应用卸载同样没有入口。Wi-Fi、蓝牙从 ABI 4 读取 M33 capability/enabled/connected 与 Wi-Fi 信号；驱动未启用时明确显示服务不可用并禁用操作，绝不伪造开关成功。
- 显示子页直接复用 M55 `pwm18` 平台适配器，滑条写入后读取实际亮度，仍保持 UI 0~100% 映射到面板安全 50~100% 占空比；原有强调色、Tile 透明度、背景选择完整迁移到 Personalization 子页。新增独立的显示器+亮度与调色板 SVG 图标，Wi-Fi、蓝牙继续使用其专用无线图标，不再用齿轮或系统图标代替分类语义。
- 自动测试新增 Settings 分类数量/范围、搜索 `wifi`、键盘覆盖几何与收起、4 个分类逐页 push/pop、亮度从 65% 实写到可稳定回读的 30% 后恢复、全部个性化控件和最终 transient slot 释放检查。
- 最终 M55 clean 构建通过：text=908,968、data=3,768、bss=4,307,828 字节；HEX 2,567,381 字节，SHA-256 `90BD54EAF695FD559995EF846A463730A9D1A50FFAF7E12EC5D749A1C5F10581`。Infineon Customized OpenOCD 5.19.0.4782 写入 917,504 字节并校验 912,736 字节。板端完整自动化为 `255 PASS / 0 FAIL / 122 actions`，耗时 56,613 ms；最终 route 对象增量 0、堆 122,872 / 1,441,752 字节、对象峰值 155，M55 IPC tx/rx/err=19/57/0。完整日志：`tools/freather/serial-monitor/logs/settings-scope-icons-clean-board.log`；UI 状态：`settings-scope-icons-clean-status.log`；IPC 状态：`settings-scope-icons-ipc-status.log`。

## 2026-08-30 详细 System 硬件与资源信息

- System 应用从简短的 M33/性能状态扩展为七个可滚动分区：双核运行状态、处理器与时钟域、片上存储、片外 Flash/HyperRAM、通信与显示链路、M55 已注册外设、UI 运行指标。页面明确区分物理容量、链接槽分配、运行期实际占用和当前核心不可观测的数据。
- 新增 M55 平台采集器：核心/M33/NPU/GFX 时钟使用 PDL 运行期接口，I/D Cache 读取当前 CPU 状态；内外部 heap 使用 RT-Thread `rt_memory_info()` / `rt_memheap_info()` 获取 current/total/peak；RT-Thread device object list 在运行期枚举，不维护易过期的静态设备名单。
- 链接器新增只读诊断边界。最终映射核准 M55 XIP 为 `0x60580000..0x60660AD0`，实际跨度 920,272 字节（含 1 KiB MCUboot header），DTCM 静态区结束于 `0x2000EB10`，GFX 静态缓冲为 `0x26200000..0x2646D000`（2,484 KiB / 3 MiB）。
- 片上资源现在完整显示 64 KiB Boot ROM、512 KiB 物理 RRAM、328 KiB 用户可寻址 RRAM 窗口、512 KiB M55 TCM、1 MiB M33 SRAM、5 MiB SoC memory；片外显示 16 MiB S25FS128S Flash 与 16 MiB S70KS1283 HyperRAM 的当前分区、M55 XIP 使用率以及 8 MiB `hyperam` heap 的实时 current/peak。
- 通信/显示信息覆盖 SMIF0 200 MHz x4 SDR、SMIF1 399 MHz x8 DDR、MIPI-DSI 2 × 900 Mb/s / 33.984 MHz pixel clock、UART2 115200 8-N-1、I2C0 100 kHz、software I2C1、ABI 4 / 16-byte IPC frame、5 kHz 背光 PWM 和 8 KiB AXI-DMA 阈值。未启用的 Wi-Fi/Bluetooth、audio、SDHC、USB、CAN-FD、I3C、PDM、TDM 继续明确标记为无产品驱动。
- 新增 `system.inventory` 板端断言并扩展 transient-object 生命周期检查；详细口径、地址窗口和权威数据源记录在 `SYSTEM_INFORMATION_zh.md`。
- 最终 M55 `-Clean` 构建通过：text=914,640、data=4,608、bss=4,307,852 字节；HEX 2,585,713 字节，SHA-256 `E016E4110267937DB0F08AE568BEDD3531CD711A4404BBD79483D5996BEB1F35`。
- 使用 Infineon Customized OpenOCD 5.19.0.4782 与 KitProg3 `0D141868022E2400` 烧录并校验：写入 921,600 字节、校验 919,248 字节。板端全量自动化为 `255 PASS / 0 FAIL / 122 actions`，耗时 56,797 ms；最终 route 对象增量 0、堆 123,224 / 1,441,752 字节、峰值 591,632 字节、对象峰值 155，M55 IPC tx/rx/err=93/281/0。
- 完整日志：`tools/freather/serial-monitor/logs/system-information-clean-board.log`；最终 UI/IPC/CPU 状态：`tools/freather/serial-monitor/logs/system-information-clean-status.log`。

## 2026-08-30 System 概览卡片与折叠详情

- 保留上一阶段全部硬件采集字段，但把七段连续诊断文字重构为“摘要卡 + 折叠分组”。480×800 默认使用 2×2 卡片显示 16 MiB 存储、16 MiB 片外 RAM、6.50 MiB 片上 RAM和 PSoC Edge E84/双核主频；宽屏和横屏配置可自适应为一行四卡。
- 设备规格默认展开；存储与内存、接口与外设、运行状态默认折叠。每个字段改为固定名称列和可换行值列，信息仍然完整，但用户不再进入页面就面对全部低层参数。新增 storage 与 memory 两个产品化 SVG/A8 图标，未复制参考系统品牌资产。
- `system.inventory` 板端测试现在同时检查四张卡片均有运行期值、四个分组的默认展开状态、真实点击可展开并再次收回，以及关键 SoC/Flash/HyperRAM/MIPI/RT-device 字段。页面销毁后还逐一断言卡片、分组和字段对象槽位清空。
- M55 `-Clean` 构建通过：text=925,016、data=4,624、bss=4,308,004 字节；HEX 2,614,947 字节，SHA-256 `65EEE91111AACD89DBA44BABD4B27AA873A52FE3A90381B20AE2E1963CB8976D`。
- 使用 Infineon Customized OpenOCD 5.19.0.4782 与 KitProg3 `0D141868022E2400` 烧录并校验：写入 933,888 字节、校验 929,640 字节。板端全量自动化为 `255 PASS / 0 FAIL / 122 actions`，耗时 58,469 ms，IPC err=0。完整日志：`tools/freather/serial-monitor/logs/system-info-card-layout-board.log`。

## 2026-08-30 Start Tile 底部工作区与导航接缝闭环

- 本轮把“Tile 自身边界”和“桌面/导航栏边界”彻底拆开。Tile 继续报告 32 px 扩展绘制区并允许四角手柄越出自身矩形；桌面容器不再设置 `LV_OBJ_FLAG_OVERFLOW_VISIBLE`，因此浮动 Tile 本体和控制柄都不能穿过页面硬裁剪写入导航栏。
- 原接缝修复只对 Start 页面对象失效。若旧的浮动对象已经碰到导航栏扫描线，页面重绘没有权限覆盖导航栏像素，便会保留 Tile 本体或圆圈的旧块。现在交互旧/新扩展区域接近底部时，只把一条窄带提交给活动 Screen 根对象重绘，顺序固定为内容在前、导航栏在后；交互结束再执行页面收敛和同一接缝修复，不把全屏失效重新塞回每个 `PRESSING` 采样。
- 桌面网格现明确区分逻辑范围和物理滚动范围。编辑态至少提供 12 行合法坑位，并在最底行后额外预留一个完整控制柄半径；正常态的物理范围只到最低的真实 Tile，避免出现大段无意义空白。长按进入编辑时会把目标轻微滚入完整手柄可见区，退出编辑时收缩范围并钳制旧滚动位置。
- 移动或向下缩放进入上下 48 px 边缘区后，以 14 px 有界步长自动滚动；浮动 Tile 仍使用屏幕坐标跟手，坑位解析使用滚动后的逻辑原点。底部不再是死边界：继续按住即可暴露更低的空坑位，确认后才触发被覆盖 Tile 的就近避让。三个缩放角仍只改变各自拥有的两条边，行跨度保持产品约束 1..3。
- 板上回归覆盖 `tile.move.scrolled`、`tile.move.edge_scroll`、`tile.resize.edge_scroll`、`tile.resize.boundary`、`tile.resize.anchors` 等专项，最终 clean 镜像为 `243 PASS / 0 FAIL / 114 actions`，耗时 52,988 ms。第一次回归的唯一失败来自旧断言要求进入编辑前后屏幕坐标不变；新产品行为会为完整圆圈主动微调滚动，断言已改为检查微调后的拖动增量严格等于触点增量。
- 最终 M55 `-Clean` 镜像构建为 text=954,976、data=4,624、bss=4,308,028 字节；HEX 2,699,203 字节，SHA-256 `9D7DD9716432880CDBF48B5EB3AC1A391E033D65D80134C5FF603D2AA463CD3E`。Infineon Customized OpenOCD 5.19.0.4782 与 KitProg3 `0D141868022E2400` 写入 M55 962,560 字节并校验 959,600 字节；随后写入签名 M33 167,936 字节并校验 160,536 字节。
- 自动测试结束后通过 OpenOCD 直接读取 `0x263A5000` 的 819,200 字节 RGB565 常驻显存，生成 `tools/freather/serial-monitor/logs/tile-seam-current.rgb565/png` 及 `tile-seam-current-seam.png`。像素统计确认 `y=716..735` 每行 480 个可见像素全部为黑色，导航栏从 `y=736` 开始每行恰有 480 个非黑像素；Tile 本体、四角圆圈和强调色旧块均未越过交界。
- 完整 clean 板端日志为 `tools/freather/serial-monitor/logs/COM17-20260830-085233.log`；最终 `feather_ui_status` 报告 auto-test passed=243/fail=0、route-depth=1、editing=0、IPC seq 持续递增且 err=0。

## 2026-08-30 Start Tile 内嵌 Chevron 与本体长按移动

- 移除四个外置圆圈以及左上角专用移动图标。编辑态现在只显示四个两笔 Chevron，两条线严格呈 90° 并分别指向左上、右上、左下、右下；四个角全部只负责缩放，对应 TL/TR/BL/BR 各自移动相邻的两条边，另外两条对边固定。
- 移动入口改为 Tile 本体：持续按住 500 ms 进入编辑后，同一次按压可直接拖动，短按仍只打开应用。原有“未确认不让位、进入最近合法吸附坑后才让位、占用坑允许覆盖并就近重排”的移动事务保持不变。
- 操作图标使用 35 px 全透明点击对象并增加 8 px 扩展命中边距，父级裁剪后的角部有效区域约为 51×51 px；四个区域只占据角部，中央和四边中段仍由本体长按移动使用。不再使用外圆、阴影、扩展绘制区或 `LV_OBJ_FLAG_OVERFLOW_VISIBLE`，因此四角控件没有机会写到侧边滚动条或内容/导航栏接缝。
- Tile 现在拆成固定透明外壳和内部视觉本体：外壳负责触摸、网格几何及硬裁剪；背景、图标、名称、图案和 Live Content 全部位于视觉本体。呼吸动画把完整视觉本体从 248/256 缩放回 256/256，标签本身确实弹跳；四角 Chevron 是视觉本体的同级对象，位置和尺寸不随动画改变。动画无效区仍被外壳限制在 Tile 自身范围内。
- 缩放手势现在同步更新外层几何和完整视觉本体，拖动每个像素都会实时改变背景、图标、文字、图案及 Live Content 的可见宽高；模型的最终列/行跨度继续单独取整。松手后从当前像素矩形执行 180 ms ease-out 动画，位置和尺寸共同吸附到最终合法网格矩形，然后恢复呼吸动画。自动测试故意在距离 2×2 标准尺寸 8 px 处松手，确认动画已建立后再检查最终尺寸。
- 自动测试扩展为四角锚点和新绘制约束：`tile.handles` 检查四个斜向 Chevron，`tile.handles.geometry` 检查完整视觉本体在 Tile 内缩放且透明点击目标固定，`tile.move` 检查长按本体移动，`tile.resize.anchors` 逐一验证 TL/TR/BL/BR 的对边固定关系。相关的滚动后跟手、底部边缘滚动、边界钳制、碰撞避让和最终收敛测试继续通过。
- 最终 M55 clean 构建为 text=956,984、data=4,624、bss=4,308,132 字节；HEX 2,704,857 字节，SHA-256 `372982F081FD0CDF0313FC7DC9689436DEAD5E95D31369B3904E3D8CDD78E48E`。
- 使用 Infineon Customized OpenOCD 5.19.0.4782 与 KitProg3 `0D141868022E2400` 重新烧录并校验 M55 和签名 M33 镜像。最终板端状态为 `243 PASS / 0 FAIL / 114 actions`、耗时 52,757 ms、route depth=1、route object delta=0、IPC err=0；专项输出明确为 `four inset 90-degree resize Chevrons`、`body scales inside Tile; 51px corner targets stay fixed`、`live body follows; release animates to 2x2` 和 `TL/TR/BL/BR keep opposite edges fixed`。最终 clean 回归日志：`tools/freather/serial-monitor/logs/COM17-20260830-094804.log`。

## 已知边界

- 电池、电源采样、Wi-Fi/网络和屏幕旋转的板级驱动尚未在产品配置中启用；M33 蓝牙已能扫描和非连接广播，但真实状态/命令/设备列表尚未接入 M33→M55 IPC，因此 UI 仍明确显示不可用。SD 卡已经通过 SDHC1/Elm-FatFS 接入 `/sdcard`，末尾固定 2 MiB 外部 Flash 作为 `/flash` FAT 用户盘，USB Device MSC 使用 Flash/SD 双 LUN；显示亮度已经接入 M55 `pwm18`。
- 强调色、Tile 透明度、背景/壁纸、时间制式、固定时区和语言已通过 `/flash/.feathertalk` A/B 配置掉电保存；USB 导出、本机格式化和自动测试均具备冻结/恢复协议。
- 自动测试宏当前为板级验收而启用；发布固件应关闭 `CONFIG_FEATHERTALK_UI_TEST_MODE`。

后续每次板测继续在本文末尾追加构建、镜像、自动测试和 IPC 结果。

## 2026-08-30 时间与语言设置及无线设置图标修订

- Settings 新增独立“时间和语言”分类，不与 System 信息页混放。默认偏好为简体中文、UTC+08:00 和 24 小时制；用户可在 12/24 小时制、7 个固定 UTC 偏移及简体中文/English 之间切换。
- 状态栏在 M33 `FEATHERTALK_SYSTEM_TIME_VALID` 有效时，把 Unix 时间按选定偏移和制式格式化；RTC 未就绪时继续明确显示 `UP` 启动计时，不把 uptime 冒充本地时间。System 运行状态同步显示本地时间、UTC 偏移和 12/24 小时制。
- 当前产品没有时区数据库和夏令时服务，因此页面明确标记为固定 UTC 偏移；这些偏好与强调色等设置一样暂存于内存后端，重启后恢复默认。这里没有增加虚假的手动 RTC 写入能力。
- Settings 主页的标题、说明、搜索提示、分类名称/摘要和键盘收起文字增加简体中文/English 双语数据；搜索始终同时匹配中英文名称、摘要和关键字。
- 新增独立的时钟+地球 SVG/A8 图标。Wi-Fi 设置图标改为对称三层信号弧，Bluetooth 设置图标改为标准六段轮廓，均移除原先容易造成误读的调节滑块叠加图形；状态栏仍使用其独立状态资产。
- 自动测试扩展为 7 个设置入口，并真实触发 12/24 小时切换、UTC+00:00/UTC+08:00 选择、中英文切换和最终恢复，同时把新增控件纳入路由销毁后的 transient slot 检查。

## 2026-08-30 全局中英文切换与完整简体中文基础字库

- 语言不再只是“时间和语言”页面里的一个偏好值。桌面 Start Tile 名称、All Apps、搜索、设置分类与子页、System 信息、媒体、消息、文件、About、状态栏运行文字、快捷设置和通知面板全部统一使用 `ft_preferences_text()`。切换后路由器按原有页面栈重建可销毁页面，Shell 常驻对象原地刷新；自定义过的 Tile 名称不会被语言切换覆盖。
- 自动测试把 English 与简体中文拆为两个真实异步阶段，分别检查当前路由、设置分类、桌面 Tile、快捷面板标题和蓝牙名称已经更新，再切回默认中文。板端结果包含 `settings.language.surface` 的英文、中文两项 PASS。
- 最初仅从源码收集 288 个静态中文字形，无法覆盖 IPC/设备数据、后续新增文字和未直接写在 UI 源文件里的运行期字符串，实际板上会出现空白占位符。现改为产品基础字库：GB2312 一级、二级全部 6763 个简体中文汉字，再合并源码里的中文标点、全角字符和 GB2312 以外字形，本次合计 6771 个字形。
- 字体源使用 Google Fonts 官方 Noto Sans SC 可变 TTF，固定 SHA-256 `A3041811A78C361B1DE50F953C805E0244951C21C5BD412F7232EF0D899AF0DA`，许可证为 SIL OFL 1.1。TTF、Node 模块只进入 Git 忽略的主机缓存；`lv_font_conv 1.5.3` 生成 12/14/16/22 px、2 bpp、无 RLE 压缩的 LVGL C 字体，Montserrat 继续作为 ASCII/LVGL symbol fallback。可复现脚本和许可证位于 `tools/freather/fonts/`。
- 完整基础字库镜像构建为 text=3,130,384、data=4,712、bss=4,308,196 字节；HEX 8,818,340 字节，SHA-256 `22E2379E9EC7C80FDCA1CA80354D86A73468CB8A5E51F121D9452B53C923FB1D`。实际 M55 XIP 有效写入/校验为 3,137,536 / 3,135,096 字节，仍在 8 MiB 分区内；M33 写入/校验 167,936 / 160,536 字节。
- 使用 Infineon Customized OpenOCD 5.19.0.4782 和 KitProg3 `0D141868022E2400` 完成双核烧录。完整板端回归为 `259 PASS / 0 FAIL / 120 actions`，耗时 56,631 ms，路由对象无泄漏、IPC err=0。日志：`tools/freather/serial-monitor/logs/gb2312-font-board-test.log`。

## 2026-08-30 通知栏字体与真实横滑删除修复

- 全局主题普通字体改为 Noto Sans SC 14，并对通知面板标题、汇总、清除、亮度、空状态、通知来源、标题和正文分别显式绑定 12/14/16 px 应用字体。已从 `0x263A5000` 读取 480×800 RGB565 显存，完整展开的中文通知栏无方框占位符；抓帧为 `tools/freather/serial-monitor/logs/notification-font-preview.bmp`。
- 语言切换只翻译界面语义，不翻译产品、平台和协议关键词；`FeatherTalk`、`Feather`、`PSoC`、`M33/M55`、`Wi-Fi` 等保持原文。媒体曲目中的“羽翼序曲”相应修正为“Feather 序曲”。
- 单条通知不再依赖容易被父级滚动容器抢走的最终 `LV_EVENT_GESTURE`。卡片现在处理 `PRESSED/PRESSING/RELEASED`：横向拖动实时跟手，超过卡片宽度 1/4 或释放速度达到阈值时删除，否则 160 ms 回弹；纵向动作继续用于通知列表滚动和上滑关闭。
- 最新固件重新构建、烧录并完成 `259 PASS / 0 FAIL / 120 actions` 全量板端回归，日志为 `tools/freather/serial-monitor/logs/notification-swipe-board-test.log`。测试模式命令 `feather_ui_notification_preview` 可稳定打开两条中文通知，供真机触摸和显存复核。

## 2026-08-30 SD 卡自动挂载与真实文件浏览

- 产品配置启用 M55 SDHC1 4-bit、RT-Thread MMC/SD、DFS/POSIX、Elm-FatFS 和 UTF-8 长文件名；继续关闭与本功能无关的 QSPI Flash 文件系统。SDK 原有热插拔线程负责卡检测、分区扫描以及 `/sdcard` 自动挂载/卸载，产品层没有复制一套 SD 驱动。
- 新增通用 `feathertalk_storage.*`，把卷状态、目录枚举、路径安全拼接、返回上级和文件头读取隔离在 UI 之外。文件应用现在显示真实路径、容量/可用空间、文件夹/文件数，支持逐级目录、Back/上一级、手动刷新、文本头预览和二进制元数据；500 ms 监视器只响应挂载状态变化，不持续重扫目录。
- System 信息页同步显示 SDHC1 驱动就绪或真实 `/sdcard` 容量，不再把 SDHC/filesystem 列为未启用。当前只承诺 FAT12/FAT16/FAT32，不伪造 exFAT 支持，也不会自动格式化用户介质。
- 真机识别 30,591,488 KiB SD 卡并自动挂载设备 `sd`；实际读取 `extlinux/`、`rockchip/` 与 50,924,032 字节 `Image`。`sdcard_umount` 后再 `sdcard_mount` 成功并得到相同目录。
- 最终构建为 text=3,233,976、data=5,048、bss=4,312,356 字节；HEX 9,110,678 字节，SHA-256 `D90DCA04A1A335098CECF14D32A263810A2F3C4A86FD727AB97054C976D39F61`。Infineon Customized OpenOCD 写入 3,244,032 字节并校验 3,239,024 字节。板端全量自动化为 `262 PASS / 0 FAIL / 120 actions`，最终日志为 `tools/freather/serial-monitor/logs/sdcard-files-final-board.log`，启动/自动挂载日志为 `sdcard-boot.log`，卸载重挂载日志为 `sdcard-unmount-remount.log`，最终目录复核为 `sdcard-files-final-list.log`。
- USB 进入第二阶段时先按原理图结论实现 Device MSC；Host 所需 VBUS/角色路径尚不具备，因此不开放 Host。详细设计与验收记录见 `applications/STORAGE_INTEGRATION_zh.md`。

## 2026-08-30 Files 页面间隔号占位符修复

- Files 已挂载状态使用的间隔号 `·`（U+00B7）不属于原字库脚本收集的中日韩标点区，Montserrat ASCII fallback 也不含该字符，因此状态行中的两个间隔号显示成方框；SD 目录内容和 UTF-8 解码本身没有异常。
- 字体生成器现显式收集 U+00B7，并重新生成 12/14/16/22 px 四档 Noto Sans SC 字体。产品字形总数从 6771 增至 6772，生成文件均能检索到 U+00B7；同时把“改文案后必须重建字库”的约束补入资源策略。
- 修复固件构建为 text=3,234,072、data=5,048、bss=4,312,356 字节；HEX 9,110,948 字节，SHA-256 `A8AA9DC1E501D5C262C3A1D3E68D6CE5BB7E054752EA3672338D2E19485CAE7E`。Infineon Customized OpenOCD 写入 3,244,032 字节并校验 3,239,120 字节；真实 SD 卡仍自动挂载，Files 根目录检查通过，板端全量回归为 `262 PASS / 0 FAIL / 120 actions`、耗时 52,305 ms。日志：`tools/freather/serial-monitor/logs/files-font-board-test.log`。

## 2026-08-30 USB Device MSC 与设置页

- Settings 新增独立 USB 分类。角色固定为设备；主机模式因板级 VBUS/角色路径未完成而禁用。设备功能列出 USB 存储器和 USB Audio (UAC)，其中 UAC 明确禁用，留待音频链路阶段实现。
- USB 存储器只在真实 SD 卡已挂载时可选。启动流程先从本机 FatFS 安全卸载 `/sdcard`，把 `sd` 块设备独占交给 CherryUSB Device MSC；停止后再关闭 USB、释放块设备并恢复本机挂载，避免主机与设备并发修改 FAT。
- 真机已经启动 MSC，串口报告 `FeatherTalk USB Device MSC started: sd, 61182976 x 512 bytes`。设置页的结构、禁用态和生命周期加入全量自动测试；本轮基线为 `265 PASS / 0 FAIL / 121 actions`，耗时 57,313 ms。
- 新 USB 页面使用了弯引号 `“ ”`（U+201C/U+201D），旧字库采集规则未覆盖 Unicode 通用标点，因而显示占位符。字体生成器现自动纳入 U+2000..U+206F，并重新生成 12/14/16/22 px 四档字体；当前集合为 6,774 个字形，四个生成字体均验证包含 U+201C/U+201D。
- 增加 `feather_usb status/storage/stop` MSH 诊断命令，复用 UI 的同一套独占导出和恢复挂载路径。状态输出包含连接、枚举、介质、块参数以及读写次数/字节数/最大传输长度，便于性能和异常定位。
- 首轮 512 字节 MSC 缓冲的 64 MiB WriteThrough 顺序写只有 `0.213 MiB/s`。产品配置改为 64 KiB 后，板端记录到最大 65,536 字节单次 I/O，同一测试提升到 `4.741 MiB/s`（约 22.3 倍）。通过 Windows 安全弹出、停止并重新启动 MSC 清除文件缓存后，64 MiB 顺序读为 `10.098 MiB/s`；读写 SHA-256 均为 `EEEEA29F19E99097AFF081C75A85115C7732967D5824201DDBA0D2D1F6202723`。测试文件已删除，最终 `/sdcard` 重挂载并通过目录检查。
- 最终性能版构建为 text=3,287,448、data=72,480、bss=4,245,108 字节；64 KiB DMA 缓冲从普通 BSS 移入 USB 数据段，总 RAM 占用基本不变。HEX 为 9,450,761 字节，SHA-256 `26D50175D24C4EA8C1712054FDBAEEF2C24687BF9D2952A954E3FCF0FADEBCEA`。OpenOCD 写入/校验 M55 3,362,816 / 3,359,928 字节，M33 167,936 / 160,536 字节；板端全量回归为 `265 PASS / 0 FAIL / 121 actions`、耗时 57,215 ms。

## 2026-08-30 SD 磁盘管理与安全格式化

- Settings 新增独立且始终可见的“存储”分类与不复用的 SD 卡图标。未插卡时明确显示卡槽可用、介质未插入，格式化按钮禁用并继续定时检测；不会因为无卡而隐藏入口或把无介质误报为驱动故障。
- 新增 `board_storage.h` 和产品层磁盘信息结构：从物理 `sd` 读取真实容量、扇区/擦除块、MBR/GPT 分区表，再与 DFS `/sdcard` 的文件系统、卷容量、已用/可用空间合并展示。USB 占用期间不碰原始分区表，避免与主机写入竞争。
- USB MSC 的所有权移交固定导出完整物理 `sd`，电脑端可以直接删除/创建分区及格式化整卡；安全弹出并停止 USB 存储后，设备重新读取当前布局并恢复本机挂载。
- 设备端提供整卡 FAT 格式化。两层模态确认分别说明“删除全部分区”和“不可撤销”，最终确认后才由后台线程卸载、格式化、重挂载；USB 占用、状态切换或文件句柄导致卸载失败时拒绝擦写。MSH 同样改为必须输入 `sdcard_mkfs ERASE-ALL`。
- 自动测试覆盖分类路由、无卡/有卡禁用态、两层确认和取消路径，但绝不点击最终擦除按钮。当前 FatFS 支持 FAT12/16/32，不宣称 exFAT；约 31 GB 介质整盘格式化时由 FatFs 选择 FAT32。

## 2026-08-30 双存储设备选择与容量视图

- Storage 设置页由两段设备长文本重构为“设备卡片 + 所选设备详情”。内置 Flash 与 SD 卡各有独立且不复用的设备图标；在 480×800 竖屏中两张卡片并排显示，紧凑布局自动改为纵向排列。点击任一卡片后，强调色边框、状态、容量视图和操作按钮同步切换到该设备。
- 主页面只保留设备名、物理容量、挂载状态、文件系统与挂载点等必要信息。所选卷使用真实 `statfs` 数据绘制已用/可用比例条，并显示大号容量值；未挂载、被 USB 占用或容量不可观测时明确显示不可用，不把未知空间误画成全部可用，也不把单卷剩余量误称为整张物理介质的剩余量。
- “浏览文件”按稳定设备枚举进入 `/flash` 或 `/sdcard`，不从本地化显示文字推导路径。格式化操作同样按当前设备独立工作：Flash 只影响外部 Flash 末尾固定 2 MiB 用户卷，SD 才是整卡格式化；第一次确认时即锁定目标，后续切换设备也不会改变待格式化对象。两者均保留两级破坏性操作确认，自动测试只检查确认与取消路径，不执行最终擦除。
- 新增设备数、所选设备、操作目标、容量轨道和容量比例的测试接口，并把新增对象全部纳入路由销毁检查。板端专项测试覆盖 Flash/SD 独立选择、容量图形、浏览与格式化目标、Flash/SD 两级确认取消以及 Files 根目录的两个设备入口。
- 最终 M55 构建为 text=3,382,340、data=72,600、bss=4,245,588 字节；HEX 9,717,996 字节，SHA-256 `85F03F7B3F82EFDA7F5284712882DDA63A5F89BB45BF1CBCDCA1311430884EAA`。Infineon Customized OpenOCD 5.19.0.4782 与 KitProg3 `0D141868022E2400` 写入 M55 3,457,024 字节并校验 3,454,940 字节，随后写入签名 M33 167,936 字节并校验 160,536 字节；M55 擦除范围止于 `0x608FFFFF`，未触碰 `0x60E00000` 起始的固定 2 MiB 用户卷。
- 全量板端自动化为 `281 PASS / 0 FAIL / 129 actions`，耗时 58,150 ms；最终 route-depth=1、路由对象增量为 0、IPC err=0。`/flash` 与 `/sdcard` 均可从 MSH 正常列目录。完整日志：`tools/freather/logs/storage-ui-device-view-final-board.log`；最终状态与双卷目录复核：`tools/freather/logs/storage-ui-device-view-final-status.log`。

## 2026-08-30 Gallery、壁纸与每机配置持久化

- 当前硬件没有移动网络、SIM 或 SMS 接收链路，因此删除 Messages 应用和收件箱测试逻辑；保留通知中心，因为系统/应用通知不依赖短信。原稳定应用枚举槽改为 Gallery，Start 与 All Apps 仍保持 Settings、Media、Gallery、Files 四个应用。
- Gallery 使用 `/flash/Pictures`、`/sdcard/Pictures` 两个专用照片集合，介质挂载且可写时自动建目录，不浏览卷根目录、不复用 Files 应用的通用目录模型。每个集合最多列出 64 张已通过实际解码头、文件大小和像素上限检查的 JPG/JPEG/PNG/BMP；支持单图预览、上一张/下一张/关闭/设为壁纸，介质拔出或被 USB 占用时清除图片源并退回集合页。
- FeatherTalk_M55 使用工程私有 `feathertalk_lv_conf.h` 启用 LVGL POSIX FS、TJPGD、LODEPNG 与 BMP，SDK 公共 LVGL 配置保持不变。Gallery 与 Wallpaper 新增两个独立 SVG/A8 图标；当前总计 41 个 ID、三档尺寸、160,064 字节 A8 数据，应用/设置/摘要卡/壁纸入口的语义图标唯一性继续由自动测试检查。
- Personalization 增加“壁纸与背景”，提供纯黑、深色、强调色、相册图片四种模式。Gallery 选图后保存规范化原生路径并把壁纸放在内容视口底层；路由页透明显示壁纸，状态栏、导航栏和通知层仍保持不透明。SD 图片缺失时回退纯色但保留路径；Flash 图片与配置位于固定 2 MiB 用户卷。
- 新增 `tools/freather/wallpapers` 精选部署包：三张 480x800 JPEG 不编入固件，复制到 `/flash/Pictures` 后由 Gallery 发现并可设为持久壁纸；素材来源、许可和再生成方法记录在[目录 README](../../../../tools/freather/wallpapers/README.md)。
- `feathertalk_ui_preferences_store.*` 实现 `/flash/.feathertalk/ui-config-a.bin`、`ui-config-b.bin` 双槽。磁盘结构显式小端序列化，使用 schema、generation、CRC32、非活动槽写入、`fsync`、底层 Flash sync 和写后回读；2 秒防抖减少 64 KiB NOR 擦写。`feather_ui_status` 报告槽、generation、dirty/frozen/test 和写入错误。
- USB MSC 导出和本机 Flash 格式化前先同步、冻结配置线程；重新挂载后重新验证双槽，如果电脑格式化掉配置则以内存中的当前每机配置自动重建。UI 自动化先快照、同步并暂停落盘，结束前恢复原配置，不再把测试默认值写进用户设备。
- 自动测试已替换 Messages 场景为 Gallery 双来源、刷新、受控预览与可选壁纸场景，并增加偏好存储激活/测试暂停/最终快照恢复检查。最终 clean 整包构建通过：text=3,475,316、data=72,628、bss=4,265,908 字节，HEX=9,979,607 字节，SHA-256 `451F58DC3F460C875D037183FE5E949C08C1A2DE13F397F35DCF3E1C10AF4471`；新增产品模块无编译告警。
- Infineon Customized OpenOCD 5.19.0.4782 与 KitProg3 `0D141868022E2400` 写入 M55 3,551,232 字节并校验 3,547,944 字节；擦除范围止于 `0x608FFFFF`，未触碰 `0x60E00000` 起始的固定 2 MiB 用户卷。三张 480x800 JPEG 通过双 LUN USB MSC 写入 `/flash/Pictures`，共 62,063 字节，Windows 写缓存刷新后板端重新挂载并逐项列出成功。
- 含真实图片的板端全量自动化为 `291 PASS / 0 FAIL / 134 actions`，耗时 75,823 ms；实际通过首图打开、decoder preview、设为壁纸、自定义背景生效、偏好快照恢复和路由对象无泄漏。结束后 `test=0`、`frozen=0`、USB MSC 已停止，设备原有纯色背景偏好保持不变。完整日志：`tools/freather/logs/gallery-wallpaper-final-board.log`；最终状态与目录复核：`tools/freather/logs/gallery-wallpaper-final-status.log`。

## 2026-08-30 壁纸缓存、Shell 接缝与透明页面残影修复

- 原 Gallery 预览和桌面壁纸把文件路径直接交给 LVGL 图片控件，显示期间会反复从 FAT 介质流式解码 JPEG；全屏壁纸参与局部失效区重绘时会重复解码，既造成明显降帧，也增加 USB 介质冻结、拔卡与页面销毁期间的资源竞争。现在图片只在选择或挂载恢复时解码一次，并转换为常驻 RGB565 `lv_draw_buf_t`；Gallery 预览和壁纸分别持有缓存，切图、卸载或 USB 导出前先解除图片源，等待绘制结束后再释放。
- 第一轮直接读取 `0x263A5000` 的 819,200 字节常驻显存，确认状态栏下和导航栏上各有 10 px 旧像素带。根因不是 LCD 传输，而是 Screen 继承主题的 `pad_row=10`；壁纸模式又把 Screen 注册成透明页面背景，两个 Flex 间距暴露了上一帧。Screen 现在是始终不透明的 Shell 清屏层，行/列间距显式为 0，壁纸严格限制在 `480x700` 内容视口；自动测试 `shell.seams` 检查状态栏、内容区、导航栏以及壁纸四个坐标边界逐像素连续。
- 第二次显存读取发现接缝已闭合，但 Gallery 当前页没有控件覆盖的上半区仍保留 Home 标题和 Tile。这是另一条独立残影路径：壁纸激活后路由页为透明，push/pop 只让新旧控件自己的矩形失效，未覆盖区域没有重新合成壁纸。路由器现在仅在 push、pop、Home/全栈刷新完成时使整个内容视口失效一次；普通动画、状态刷新和触摸热路径仍保持局部失效，不引入持续全屏刷新。
- 最终重新构建和烧录后，板端全量自动化为 `294 PASS / 0 FAIL / 135 actions`，耗时 67,918 ms；覆盖 Gallery RGB565 非黑预览/壁纸、Shell 接缝、全部应用 push/pop/Home、深度 8 边界、对象释放和配置恢复。最终静止状态为 `present-fps=1、refresh-hz=54`，说明没有壁纸持续重绘；路由对象增量为 0，当前/总 heap 为 253,288 / 1,374,360 字节。
- 修复后再次直接抓取 `0x263A5000`，画面中内容从 `y=36` 紧接状态栏并持续到 `y=735`，导航栏从 `y=736` 开始；上下均无空隙、Tile/Chevron 旧块或上一页残影。抓帧：`tmp/wallpaper-route-final.bin` 与 `tmp/wallpaper-route-final.png`。最终 M55 构建为 text=3,479,124、data=72,628、bss=4,265,948 字节；HEX=9,990,317 字节，SHA-256 `98278B054B7191D08DBEAC04074BC12D1930F5880F6DBE134C3D6594E6ECB954`。回归日志：`tools/freather/logs/wallpaper-route-final-board.log`；最终状态：`tools/freather/logs/wallpaper-route-final-status.log`。

## 2026-08-30 Files 长按操作、目录管理与卷根格式化入口

- Files 行内不常驻破坏性按钮。长按普通文件或文件夹后弹出统一操作面板：打开、剪切、复制、重命名、粘贴、删除和取消；目录还提供“新建文件夹”。长按当前目录空白区只显示适用于当前目录的新建文件夹、粘贴和取消。文件打开沿用文本/二进制预览，图片交给 Gallery，目录则逐级进入。
- 剪切、复制和删除对普通目录递归工作；粘贴支持同卷移动、跨卷复制后删除、同名自动追加 `-copy`、禁止把目录粘贴进自身。重命名和新建目录通过受控存储接口执行，名称校验拒绝空名称、`.`/`..`、路径分隔符和 FAT/Windows 禁止字符；所有写操作严格限制在 `/flash`、`/sdcard` 之下。
- `/flash`、`/sdcard` 卷根永远不能作为普通文件或目录复制、剪切、重命名、删除。Files 最外层的 Flash/SD 设备项长按后在当前 Files 页原地显示已锁定目标的第一级格式化确认；取消、继续到第二级确认以及最终格式化均不强制切换到 Storage 管理页。介质缺失、USB 占用或繁忙时只显示不可格式化原因，不进入擦写。
- 重命名/新建文件夹使用带屏幕键盘的专用输入面板；中文标题、错误、确认按钮以及格式化确认按钮全部显式绑定 Noto Sans SC，避免 LVGL MessageBox footer 回落到不含中文字形的 Montserrat。长按操作面板按 3 列和可见操作数动态计算 1–3 行高度，每项保持 42 px 触控高度，新增操作不会被默认单行 footer 裁切。
- 真机自动化在固定 Flash 用户卷实际完成“新建目录 → 重命名 → 路径/卷根保护 → 清理”，并覆盖卷根原地长按、第一级点“继续”后第二级确认仍停留 Files、第二级取消后仍停留 Files、普通目录菜单、重命名/新建输入面板和页面生命周期释放；测试不点击最终擦除。最终为 `321 PASS / 0 FAIL / 150 actions`，耗时 75,328 ms，IPC err=0。
- 最终构建为 text=3,503,380、data=72,652、bss=4,270,164 字节；HEX 10,058,598 字节，SHA-256 `C04112073BF70417E506B373AF2C0C75853825E2D4591F07C6B794CE63AC8FA3`。Customized OpenOCD 写入/校验 M55 3,579,904 / 3,576,032 字节，M33 167,936 / 160,536 字节。完整日志：`tools/freather/logs/files-inplace-format-confirm-final-board.log`。

## 2026-08-30 Gallery 点击响应与解码热路径修复

- 原相册缩略图定时器每 20 ms 在 LVGL 线程同步读盘和解码，点击回调又在切换查看器之前同步生成最大 480×800 的 RGB565 预览；连续解码使触摸事件长期排队，表现为整行难以点中、反复点击后才出现查看窗口。
- 图片行改为完整的 76 px Button 热区，缩略图容器、图片、占位符和文字列显式取消 `CLICKABLE`，不会再截获父行点击。手指按下立即暂停渐进缩略图加载，释放或滚动取消后恢复；按压态提供即时背景反馈。
- 点击回调现在只设置目标并立即切换到带“正在加载...”状态的查看器，40 ms 后再开始预览转换，确保至少先提交一帧可见反馈。缩略图间隔放宽到 160 ms；RGB565 渲染删除了“先打开解码器探测、随后再次打开正式解码”的重复路径，文件头、尺寸和安全上限校验仍保留。
- 自动测试在相册为空时生成一个 32×24 非黑 BMP，覆盖完整热区、查看器立即加载态、最终非黑 RGB565 缓存、Files 跳转、壁纸和删除确认，结束后删除临时文件。最终板端回归为 `328 PASS / 0 FAIL / 151 actions`，耗时 62,603 ms，路由对象增量为 0；日志为 `tools/freather/logs/gallery-click-final-board.log`。
- 最终构建为 text=3,507,484、data=72,652、bss=4,270,180 字节；HEX 10,070,147 字节，SHA-256 `87C290E6EAB042567D7A78DFA81B5628413206B8A779A7859867B4E265FAB9F9`。Customized OpenOCD 写入/校验 M55 3,584,000 / 3,580,136 字节，M33 167,936 / 160,536 字节。

## 2026-08-31 双核共享 SMIF 保护与蓝牙首阶段闭环

- 根因确认：M33 Non-Secure XIP 位于同一颗 S25FS128S 的物理 `0x6034xxxx`，M55 的固定 2 MiB 用户卷位于 `0x60E00000`。M55 在 M33 继续取指时 program/erase 会让 M33 外部 `ICACHE0` 吸入无效数据，最终表现为调度位图、列表或任意 XIP 代码随机损坏；它不是 RT-Thread 调度器本身的缺陷。
- 新增共享 SRAM 双线协议。M55 在写/擦前发请求；M33 高优先级线程进入 `.cy_sram_code`、屏蔽中断和 fault、确认停驻后才 ACK。M55 的 program/erase、状态轮询及直接调用链全部位于 ITCM/DTCM，并把写入拆成 256 字节 page、擦除拆成 64 KiB sector。器件 WIP 未明确清零时保持两核停驻，不返回 XIP。
- M33 收到 release 后先操作 PSE84 外部 `ICACHE0` 的自清 `CMD.INV`，确认完成后才恢复异常和返回。共享页使用两核都可见的 S-bus `0x240FFFC0`；原先的 M33 C-bus `0x040FFFC0` 并不是此跨核页的有效公共别名，实机读写差异正是首轮握手 `-116` 的原因。
- IPC Pipe 端点避开 MTB-SRF 占用的通道/IRQ，并补齐 RT-Thread ISR enter/leave。干净复位后的 M33 明确报告 `SMIF XIP guard service ready at 0x240fffc0`，M33/M55 心跳持续在线。
- M55 真机完整回归为 `326 PASS / 0 FAIL / 150 actions`、耗时 82,162–82,255 ms。实际 Flash 删除、递归目录、复制/移动/重命名三项 `#253..#255` 全部通过；没有再出现 `M33 XIP park failed`、HardFault 或异常复位。
- CYW55500A1 官方 PatchRAM、HCI-UART integration 和 AIROC host stack 的有效输入均已放入 `FeatherTalk_M33/applications/bluetooth` 管理，构建输出确认不读取仓库外路径。PatchRAM 以 3 Mbit/s 下载后切回 115200 bit/s，host `ready/error=0`；启动扫描得到 18 reports / 11 unique，运行期复扫得到 19 / 11，广播 on/off 均成功。
- 启动时原 `0x28` 不是控制器故障，而是 `USE_AIROC_STACK_SMP=0` 时供应商包装函数默认返回 `WICED_ERROR` 的假告警。禁用 SMP 现按合法配置返回 success；当前明确只承诺扫描和非连接广播，配对、加密、绑定、密钥持久化与 GATT 留到下一阶段。
- 下载脚本同步修复了另一个独立问题：PSE84 `reset_halt` 返回 Tcl 0 时 OpenOCD 本身不会退出失败。现在由仓库内 Tcl guard 检查停机域、Secure boot PC、Test Mode 和写后 Non-Secure 启动条件，再允许官方 FLM 擦写。修正版实机写入 372,736 字节、校验 365,728 字节通过。

## 2026-08-31 蓝牙进度快照与后续计划

详细的硬件链路、固件来源、调试时间线、八类故障根因、复现命令和日志索引见
[蓝牙调试与上板记录](../../../FeatherTalk_M33/applications/bluetooth/BLUETOOTH_BRINGUP_zh.md)。

当前进度：

| 层级 | 状态 | 已完成/当前边界 |
| --- | --- | --- |
| 板级供电与 HCI UART | 已完成、已板测 | P16.3 无线电源、REG_ON、SCB4、RTS/CTS 和自动波特率链路通过 |
| CYW55500A1 PatchRAM | 已完成、已板测 | 官方匹配组件随每次冷启动下载；126,951 字节、519 条 HCD 记录 |
| AIROC Host Stack | 已完成、已板测 | M33 启动为 `ready/error=0`，本机地址可读，运行波特率 115200 bit/s |
| BLE Observer | 已完成、已板测 | 启动扫描 18/11、运行期复扫 19/11，地址、RSSI、类型和广播名可查询 |
| 非连接广播 | 已完成、已板测 | `bt_adv on/off` 均返回 0，状态可查询 |
| 仓库可复现性 | 已完成 | 官方 BTSTACK/固件固定为子模块，HCI 集成源码入库，不读取开发机绝对路径 |
| 双核并发稳定性 | 已完成、已板测 | M55 完整 UI/Flash 回归 `326 PASS / 0 FAIL / 150 actions`，M33 蓝牙持续在线 |
| M33→M55 蓝牙 IPC | 未完成、下一阶段第一项 | ABI 4 有无线状态槽位，但 M33 尚未发布真实蓝牙 capability/enabled/connected/scan 数据 |
| Settings/快捷面板真实控制 | 未完成 | M55 仍应显示蓝牙不可用，不能因为 M33 MSH 可扫描就伪造 UI 开关成功 |
| 连接与 GATT | 未完成 | 尚无连接、服务发现、Characteristic 读写、Notification/Indication 验证 |
| SMP/配对/Bond | 明确禁用 | 尚无 I/O 策略、身份密钥、Link Key 回调和掉电持久化 |
| 低功耗与恢复 | 未完成 | HOST_WAKE/DEVICE_WAKE、控制器睡眠、异常重启和下载失败恢复尚未产品化 |
| Wi-Fi | 未纳入本轮 | 蓝牙闭环不代表 Wi-Fi 驱动、固件或网络栈已经打通 |

下一阶段按依赖顺序推进：

1. **P1：蓝牙 IPC 与 UI 接入。** 在不破坏 16 字节 ABI 4 契约的前提下，由 M33 发布
   capability、Host ready、enabled、advertising、scanning、connected 和错误状态；为扫描
   列表设计分页/事件消息。Settings 和快捷面板只调用 M33 命令，不直接接触 HCI。
   验收要求是 MSH 与 UI 状态一致，M33 不在线或 Host failed 时 UI 自动退回不可用。
2. **P2：连接与基础 GATT。** 先连接一个已知 BLE 测试外设，完成连接/断开、MTU、服务
   发现、Characteristic 读写和 Notification；加入超时、用户取消和断连清理。验收要求是
   连续连接/断开 100 次无对象、线程或 heap 泄漏。
3. **P3：SMP 与密钥持久化。** 明确开发板的显示/输入能力与配对策略，实现本机身份密钥
   和远端 Bond/Link Key 的读取、更新、删除与 CRC/双槽掉电保存，再启用 SMP 相关宏。
   验收包括首次配对、重启后重连、删除设备、错误 PIN/确认拒绝和存储损坏回退。
4. **P4：产品 GATT 与应用模型。** 在通用链路稳定后再定义 FeatherTalk 自有服务，避免把
   扫描结果、控制命令和业务数据耦合到 UI 页面；同时建立权限、长度和并发访问边界。
5. **P5：低功耗与故障恢复。** 接入 HOST_WAKE/DEVICE_WAKE、控制器休眠和系统电源状态；
   为 CTS 超时、PatchRAM 失败、HCI 卡死和控制器掉电增加有次数上限的恢复状态机。
6. **P6：联合回归与量化。** 同时运行 BLE 扫描/连接、M55 动画、SD/Flash、USB MSC 和 IPC，
   记录 UART 错包、扫描丢失、连接稳定性、heap 峰值、线程栈余量、功耗和 8/24 小时压力结果。

阶段口径保持严格：P1 完成前只能说“蓝牙底层和 M33 MSH 可用”；P2 完成后才能说
“BLE 可连接”；P3 完成后才能说“安全配对和 Bond 可用”。

## 2026-08-31 M55 板载音频设备与设置页

- Settings 新增独立“音频”分类，不照搬 PC 设备列表。默认输出只列板载扬声器
  `sound0`（TDM0/I2S -> ES8388 -> MD8002），默认输入列双 PDM 麦克风 `mic0`；
  AMIC2 模拟麦克风前端因产品驱动尚未接入而置灰显示。
- FeatherTalk_M55 默认启用 RT-Thread Audio、播放、录音和双声道 PDM feed。
  `sound0`、`mic0` 在设备初始化阶段注册，设置页同时区分“已注册”和“初始化成功”，
  不把 codec/I2C 初始化失败伪装成可用设备。
- 新增 `feathertalk_audio.*` 统一查询设备格式、音量/增益和初始化状态，并提供
  `feather_audio_status` MSH 命令。输出音量为 0-100；PDM 增益为 0-37.5 dB、0.5 dB
  步进。滑块松手才写 Audio 控制，避免拖动期间反复 I2C 访问影响 UI 帧率。
- 音量和输入增益复用偏好记录 schema 1 的两个保留字节，以 value+1 编码兼容旧记录
  和真实 0 值；双槽、CRC、防抖、USB 冻结与测试快照语义保持不变。
- 修正 ES8388 功放控制权：板级上电只打开 codec 电源并保持 P21.6 功放关闭，codec
  初始化使用真实 P21.6 引脚并在配置后启用，不再向 `es8388_init()` 传空引脚或在板级
  初始化阶段无条件拉高功放。
- 音频分类及三种设备使用四个独立 SVG/A8 图标；资源重新生成后为 45 个图标、三档
  尺寸、175,680 字节。中文源字符重新纳入 6,775 字形的 Noto Sans SC 子集。
- M55 clean 构建通过：text=3,551,848、data=85,384、bss=4,259,776 字节，HEX
  10,230,740 字节。M33 安全固件与签名构建也通过；本阶段不依赖 M33 串口。
- 当时边界：尚未实现真实播放/录音应用、AMIC2 驱动和 USB UAC；它们不能因设备注册
  和设置页完成而标记为可用。

## 2026-08-31 录音机、输入设备选择与 WAV 实板闭环

- 新增与 Settings、Media、Gallery、Files 并列的 Recorder 应用。页面提供双 PDM
  `mic0` 与 AMIC2 `amic0` 两张独立输入卡、计时、实时峰值、开始录音、结束并保存以及
  跳转 Files；当前实板 `mic0` 可选，尚未注册驱动的 AMIC2 明确置灰，不伪造可用状态。
- `feathertalk_recorder.*` 将阻塞采集和文件写入放在独立 8 KiB 工作线程。状态机覆盖
  STARTING、RECORDING、STOPPING、SAVED 和 ERROR；页面离开时会结束并保存，错误路径
  关闭设备/文件、释放缓冲并删除半成品，不把文件描述符或 Audio 设备生命周期绑在
  LVGL 对象上。
- 文件为设备原生参数的标准 PCM RIFF/WAVE，当前 `mic0` 是 16 kHz、双声道、16 bit。
  目标目录固定为 `Recordings`，优先 `/sdcard`，内部 2 MiB Flash 用户卷兜底；卷未挂载、
  被 USB 导出或可用空间不足 64 KiB 时不允许开始。
- 新增录音机、开始、停止、PDM 输入与模拟输入五个独立 SVG/A8 图标；图标资源现在为
  50 个 ID、三档尺寸、195,200 字节，未复用其他应用或设置项语义图标。中英文名称、
  设备状态、按钮和错误文本均纳入全局语言/中文字体生成流程。
- UI 回归中录音机应用的注册、路由、两个设备选择器、默认 `mic0`、AMIC2 不可用状态、
  开始/停止控件、Home/Back 和页面对象释放全部通过。整套回归为 `345 PASS / 3 FAIL /
  160 actions`；三项失败仍是未运行 M33 peer 时固定 Flash 文件测试无法完成 XIP park
  握手，与录音页面、PDM 或 SD 写入无关。本阶段按用户要求不依赖或查看 M33 串口。
- 实板执行 `feather_record 2` 会先等设备真正进入 RECORDING 再计时，得到 2,020 ms、
  129,280 字节 PCM（一个采集块的停止粒度），整段最大峰值 473/1000，证明收到非零
  采样；文件保存为 `/sdcard/Recordings/REC_0000003887_00.wav`，总长 129,324 字节。
  状态命令回读并校验
  RIFF/WAVE/fmt/data 标识、头内长度和实际长度，结果为 `wav=valid`。
- 最终 M55 构建为 text=3,584,004、data=85,388、bss=4,260,236 字节，HEX
  10,321,207 字节，SHA-256
  `7AFED3187D34DD825EAE330DE28A1482691F80BCE59C12FBDB499DC6D7EE9BAC`。官方 Infineon
  Customized OpenOCD 5.19.0.4782 通过 KitProg3 `0D141868022E2400` 写入 3,674,112
  字节并校验 3,669,392 字节。
- 详细架构、文件合约、命令和后续边界见
  [录音应用设计](../audio/RECORDER_DESIGN_zh.md)。AMIC2 驱动、WAV 播放、可靠 RTC 命名和
  USB UAC/本地录音设备仲裁仍是后续工作。

## 2026-08-31 双向 USB Audio UAC2 与设置页

- USB 设备功能新增 UAC2，与 MSC 互斥切换。Host 播放经 `EP 0x02`、16 KiB 整帧
  环形缓冲和工作线程进入 `sound0`；双 PDM `mic0` 经 `EP 0x81` 送回 Host。USB
  中断回调只投递状态，不再在 ISR 内获取 mutex 或直接访问 codec/I2S。
- Settings > USB 新增输出设备、输入设备、采样率、采样深度和声道控制。`sound0`
  的 UAC 双声道输出支持 16/24/48/96 kHz 与 16/24-bit；当前 `mic0` 只支持
  16 kHz、16-bit、stereo，对应输入控件按真实驱动能力置灰。AMIC2 继续标记无驱动。
- 主机侧 Clock `SET_CUR`、AS alternate、volume/mute 会更新设备；设备侧修改会先停止
  endpoints、配置 RT-Thread Audio、更新偏好并用递增 `bcdDevice` 软重枚举。Windows
  枚举时的格式探测与真实流格式分离，只有首包 OUT 数据才提交 Host 最终格式，避免
  探测过程污染本机配置。
- Windows 初测曾为 `CM_PROB_FAILED_START / STATUS_RANGE_NOT_FOUND`。根因是 16 kHz
  stereo IN endpoint 只声明精确 64 B/ms，没有异步时钟漂移余量；max packet 改为
  68 bytes 后，Windows 11 系统 `usbaudio2.sys` 的 FeatherTalk MEDIA 节点和复合父
  设备均为 `CM_PROB_NONE`。恢复多采样率/24-bit 后仍正常。
- 实板捕获到 UAC2 Clock/Feature Unit RANGE/CUR、采样率 SET_CUR、SET_INTERFACE 和
  `EP 0x02` 打开。板端切换 48 kHz/24-bit 后完成软重枚举，Host 驱动保持正常；状态
  命令可报告双向格式、stream、同步次数、KiB、overrun/underrun 和错误。
- 最终双向同步镜像构建为 text=3,603,836、data=91,000、bss=4,271,000 字节，HEX
  10,392,778 字节，SHA-256
  `F1521B5E9DC6297097AE5BC7E189AEA191DA2C888885A20F23C23EDAA0368C65`。Customized
  OpenOCD 写入/校验 M55 3,698,688 / 3,694,836 字节。执行设备侧 48 kHz、24-bit、
  双声道切换和软重枚举后，板端仍回读同一生效格式，`connected/configured=1/1`、
  `pending=0`、`error=0`；Windows MEDIA 节点为 `OK / CM_PROB_NONE`。
- 本轮板端全量 UI 自动化为 `349 PASS / 3 FAIL / 163 actions`。USB/UAC、音频、录音、
  页面切换和对象释放通过；3 个失败均来自文件系统写入合约遇到 M33 XIP park 超时
  `-116`，不是 USB Audio 回归。测试结束后才启用 UAC，避免自动化音频格式切换干扰
  USB 枚举结论。
- 当前限制：UAC Terminal 固定双声道；本地 `sound0` 的 mono 不在同一 USB topology
  中伪装。Codex 所在 Windows 会话只暴露远程音频，尚未在本机 WASAPI 会话完成长时
  播放/录音、漂移、音质和显式 feedback endpoint 压力验证。详细记录见
  [UAC2 设计与实板记录](../audio/USB_AUDIO_UAC2_zh.md)。

## 2026-09-01 Flash 图片不可见与 M33 XIP 守护恢复

- 现象为 `/flash` 挂载目录存在但为空，Gallery 无法读取图片；M55 串口持续报告
  `M33 XIP park failed before erase: -116`。USB MSC 当时未启用，故障不是卷被主机
  独占，而是 M33 只留下启动时的 guard-ready 状态，之后不再响应 NOR program/erase
  前的 SRAM park 请求。FAT 挂载/元数据写回失败后，本地目录自然不可用。
- 没有绕过双核 XIP 保护，也没有格式化末尾 2 MiB 用户卷。M33 构建在选择 BK 栈但
  可选 BlueKitchen 源目录缺失时，改为链接明确返回 `-RT_ENOSYS` 的不可用桩；IPC 与
  SMIF guard 因而仍可独立构建和运行，真实 BK 源存在时不会编入该桩。
- 重新构建、官方签名并烧录 M33 supervisor 后，M55 system/quick IPC 序号持续递增，
  `/flash` 恢复列出 `System Volume Information`、`.feathertalk`、`Pictures`；其中
  `01.jpg=136,780 B`、`02.jpg=111,496 B`。当前 `02.jpg` 已实际解码为 480×800
  RGB565，非黑像素 384,000、checksum `0x8f45aa87`。
- 连续复核期间没有再次出现 XIP park 超时或 FAL 擦写错误。M33 镜像写入/校验
  167,936/161,604 B，外部写入范围止于 `0x6037ffff`，未触碰从 `0x60e00000`
  开始的用户盘。

## 2026-09-01 SVG 矢量图标接入与单提交修复

- `ui-svg-icon-convert.py` 保留原 24/32/48 A8 产物，同时从受约束 SVG 子集生成 line、
  polyline、polygon、rect、ellipse 的 shape/point 表。设备端不解析 SVG/XML；一个源文件
  同时服务 VG-Lite 矢量路径和 A8 回退，避免两套图标人工漂移。
- Back/Home/Search、网络和蓝牙状态、媒体控制、刷新、飞行模式、定位、亮度、旋转等
  高频简单图标改为 LVGL Vector 绘制；应用图标等固定复杂图形暂留 A8。SVG stroke 在
  首次使用时展开为 fill contour 并缓存，帧内只更新颜色、透明度和 viewBox 缩放矩阵。
- 初次接入出现 10–12 submit/frame。取证确认不是 command buffer 容量不足：每次提交
  仅约 4 KiB，batch 的 software/resource/explicit boundary 均为 0。根因是 Vector 后端
  对每条 path 重复调用 `vg_lite_set_scissor()`；该调用设置 `scissor_dirty`，下一次
  `set_render_target()` 会自动提交已有命令。
- 后端现在比较 vector scissor 与 draw task clip；两者相同就复用已安装的硬件裁剪，只有
  真正的子裁剪才改变并恢复 scissor。60 帧实板基准恢复为 `60 frames / 60 submits`，
  vector 编码由约 12.4 ms/frame 降到 2.366 ms/frame；最终镜像结果为 25.74 FPS、
  collect 9.721 ms、encode 15.552 ms、finish 10.808 ms、GPU busy 27.10%。
- 官方 Customized OpenOCD 已完成写入和校验。运行时 framebuffer 回读能看到矢量
  Back/Home/Search、状态栏 Bluetooth 及媒体控制图形；`feather_ui_status` 持续报告
  GPU 路由 100%、平均 1.00 submit/frame、software/resource/explicit boundary 为 0。
  画面回读文件仅用于本轮诊断，位于未跟踪的 `tmp/`，不作为产品资源提交。
- 最终 M55 构建为 text=3,577,524、data=91,000、bss=4,614,584 字节，HEX
  10,318,752 字节，SHA-256
  `73CFD8D359A91BD2EE3B7D241C9D438FFAE184E04FD31750B7E462D970BD94CF`；OpenOCD
  写入 3,670,016 字节并校验 3,668,524 字节。

## 2026-09-02 50 个 SVG 全量矢量化与颜色通道修复

- `FT_ICON_COUNT` 内全部 50 个图标已启用运行时矢量路径；A8 三档资源只保留为 path
  创建失败时的防御性回退。应用、设置、录音、存储、状态栏、快捷操作和导航图标不再
  因图标类别不同而走两种正常绘制路径。
- 修复 LVGL Vector VG-Lite 后端的颜色打包：原实现把 `lv_color32_t` 写为
  `0xAABBGGRR`，与本平台 `lv_vg_lite_color()` 使用的 `0xAARRGGBB` 不一致，导致
  矢量图标红蓝通道交换。修复后显存回读中主题强调色为 `#0078d7`，与 Tile、文字和
  A8/矩形后端一致。
- M55 构建通过：text=3,577,516、data=91,000、bss=4,614,584 字节；HEX
  10,318,736 字节，SHA-256
  `AA9BC42A15FB002A2DBB7D1F335F5D487C377B0F9E3E178A0CB114EE080CF3B0`。Customized
  OpenOCD 写入 3,670,016 字节并校验 3,668,516 字节。
- 实板 60 帧结果为 60 render / 60 submit、23.90 FPS、collect 11.657 ms、encode
  14.774 ms、finish 10.832 ms、GPU busy 25.39%；vector 为 4.809 ms/frame，image
  降为 0.278 ms/frame。GPU 路由 100%、software draw 为 0、batch boundary/overflow
  均为 0，仍满足一帧一条 GPU 命令链。
- 相对 19 个图标矢量化版本，当前堆占用增加约 11.8 KiB、峰值增加约 23.9 KiB，来自
  当前页面按需建立的额外 path cache，不是 50 份缓存一次性预分配。基准日志为
  `tools/freather/logs/COM17-all-vector-color-bench.log`，状态日志为
  `tools/freather/logs/COM17-all-vector-color-status.log`。

## 2026-09-02 矢量 stroke 闭合、缓存生命周期与画质修复

- A/B 对比确认 50 个 SVG 源和软件 A8 结果均正确，断口来自设备端旧的 stroke 整理层。
  旧实现把每条中心线展开成独立矩形，再用圆形补 cap/join；矩形与圆点的轮廓绕向相反，
  合并到同一 `LV_VECTOR_FILL_NONZERO` path 后重叠区发生抵消，因此 System 滑杆、Recorder
  话筒和 Files 文件夹出现孔洞、断口或不连续拐角。
- 应用层现为每个图标分别缓存 fill path 与原始 stroke centerline。SVG 的 polygon 继续
  显式 close，polyline 保持开放；线宽、round/butt/square cap 和 miter/bevel/round join
  原样交给 VG-Lite `vg_lite_update_stroke()`，不再手工近似最终轮廓。Home 显存回读确认
  System、Recorder、Files、Back、Home、Search 与软件 A8 参考一致。
- 修复 LVGL VG-Lite stroke cache 两项缺陷：比较函数原先错误比较 `lhs->width` 与自身，
  现改为比较 `rhs->width`；纯 stroke 中心线进入缓存前显式标为
  `VG_LITE_DRAW_STROKE_PATH`，避免无 END 的合法中心线被 ZERO/fill 规则误判并断言。
- `vg_lite_update_stroke()` 生成最终 `stroke_path` 后立即释放展平点、分段与临时轮廓链表，
  只保留跨帧绘制必需的最终命令流和上传状态。冷启动 Home 堆为
  1,079,736 / 1,356,248 B，与手工展开版 1,080,328 B 基本相同；未清理的试验版曾在跨页
  后达到 1,354,600 B，已明确淘汰。
- 最终 60 帧真机基准为 60 render / 60 submit、26.08 FPS、collect 10.181 ms、encode
  12.881 ms、finish 10.676 ms、GPU busy 27.32%；Vector 2.390 ms/frame，software draw、
  batch boundary 与 overflow 均为 0。相较错误手工展开版的 23.90 FPS / 4.809 ms Vector，
  画质与性能同时改善。
- 最终 M55 为 text=3,576,644、data=91,000、bss=4,614,784 字节；HEX 10,316,277 字节，
  SHA-256 `3C861431F53C4AB3E15EE55BFEE5CE277184680D5D7E2B6C09360062A9519CA8`。
  OpenOCD 写入 3,670,016 字节并校验 3,667,644 字节。基准和状态日志为
  `tools/freather/logs/COM17-native-stroke-work-free-final-bench.log` 与
  `tools/freather/logs/COM17-native-stroke-work-free-home-status.log`。

## 2026-09-02 字体/SVG 离线原生化与完整 GPU 单链

- `build-lvgl-vector-font.js` 现将 Noto Sans SC 的 7,586 个字形直接输出为 canonical
  1000 UPM、`VG_LITE_S16` 命令流，共 5,924,806 字节路径数据。12/14/16/22 px 共用
  同一份轮廓；M55 运行时只查表、设置平移/缩放矩阵和颜色，不再解析 TTF，也不再逐命令
  重建 LVGL/VG-Lite path。矢量字体被标记为稳定 descriptor，60 帧基准达到
  11,926 hit / 8 miss，布局/字体成本降至 0.648 ms/frame。
- `ui-svg-icon-convert.py` 将全部 50 个 SVG 的 fill 与原始 stroke centerline 直接生成
  `VG_LITE_FP32` 命令流，并分别记录真实几何边界。设备端轻量包装直接引用 XIP 常量；
  不解析 XML，不遍历 shape/point，不复制路径。24/32/48 A8 仍是创建失败时的防御性回退。
- `lv_vg_lite_path_create_static()` 支持以只读方式包装离线原生流，区分路径数据所有权，
  避免销毁包装时释放 XIP 常量；immutable LVGL path 可直接绑定 fill/stroke native cache。
  旧的四份 A8 中文字体源已从产品 `SConscript` 排除，防止页面静默退回位图字体。
- 已重新完成 fill/font `vg_lite_upload_path()`/CALL 根因验证。GC265 的 CALL/RETURN 和 CALL 后
  `STALL 0x10` 均可用；旧停滞来自上传 DATA 在 8 字节对齐的末尾 `CLOSE` 后直接放置
  `RETURN`，缺少真正的 `END`。上传器现对上传路径补 32 位 `END`、对完整上传块 clean
  D-Cache，并启用 `LV_VG_LITE_USE_PATH_UPLOAD=1`。返回值之外已经检查 GPU 完成、
  像素校验和、同帧 100 次 CALL、真实 SVG fill 跨 100 帧复用和完整桌面 60 帧压力。
- 精确 SVG 边界先把错误版本的 23 submits/frame 恢复到 5；剩余拆分来自官方驱动把
  纯 scissor dirty 当成 render-target 改变并强制 flush。现在纯 scissor 更新作为有序
  `0x0A13` 状态留在同一链，真实 target/mirror/gamma/flexa 改变仍保留原同步规则。
- 最终真机 60 帧为 **60 render / 60 submit**、26.22 FPS、collect 6.286 ms、encode
  6.875 ms、finish/GPU wait 11.235 ms、GPU busy 29.13%。其中 Vector 1.687 ms/frame；
  Label 4.014 ms/frame，内含 70 个矢量字形的命令编码 3.366 ms，布局/字体仅 0.648 ms。
  GPU 路由 100%，software/resource/explicit boundary 和 transient overflow 均为 0。
- 干净构建通过：text=7,507,244、data=91,080、bss=4,614,784 字节，`.app_code_itcm`
  219,792 字节，`.cy_gpu_buf` 2,809,856 字节，HEX 21,372,341 字节，SHA-256
  `7231FFF2E7E40A4ABA28BC2357BA4027F9E67A876B26938E5070D9414CB7EB4C`。最后一次实板镜像
  与干净构建的链接尺寸一致；基准日志为
  `tools/freather/logs/COM17-native-vector-scissor-chain-bench.log`。

## 2026-09-02 VG-Lite path upload/CALL 根因闭环

- 新增独立于 LVGL 页面结构的 `feather_vg_path_test`。它在 LVGL/GPU 所属线程中创建
  64×64 BGRA 离屏目标，分别执行 inline、upload-only、CALL、CALL 无 STALL、真实 SVG、
  同帧循环和跨帧循环，并回读像素数与 FNV 校验和；不会再用“上传 API 返回 0”替代完成
  中断。芯片实测为 GCNanoUltraV `chip=0x265 rev=0x1003`。
- 最小 END 三角形的 inline/CALL 均改变 1,200 像素，校验和同为 `0x9cc8104d`；有无
  CALL 后 `STALL 0x10` 都能完成。1,156 字节 `tile-pattern` 真实 SVG 的 inline/CALL
  校验和同为 `0xb4cbcfde`；同帧 100 次 CALL 和跨 100 次独立 finish 复用均完成。
- 专门构造 40 字节、8 字节对齐且真正以 `CLOSE` 结束的 FP32 三角形。inline 会停滞，
  新增的 5 秒有限等待返回错误并读到 GPU idle `0x7ffffffe`，证明 `CLOSE` 不能替代
  `END`。旧上传布局为 `DATA(path), RETURN`，非对齐流仅因零 padding 偶然正常。
- `vg_lite_upload_path()` 与 `vg_lite_upload_stroke()` 现在把 DATA payload 增加 4 字节，
  复制源路径后显式写入 32 位 `END`，再按 64 位对齐并放置 `RETURN`，最后 clean 完整
  上传区。相同 CLOSE 边界流变为 64 字节
  `DATA(6 qword), ..., CLOSE, END, padding, RETURN`，3 ms 完成并得到正确校验和。
- 非 FreeRTOS `vg_lite_hal_wait_interrupt()` 过去忽略 `timeout`，导致失败表现为永久卡死；
  现在 `vg_lite_finish()` 的 5,000 ms 等待可退出并报告寄存器，timeout=0/UINT32_MAX 仍保留
  无限等待语义。此改动是诊断/容错，不用于掩盖 GPU 错误。
- 产品默认已改为 `LV_VG_LITE_USE_PATH_UPLOAD=1`。本阶段数据中的 LVGL 转换路径上传是
  诊断构型；后续完整页面生命周期压力证明它不能作为产品通用策略。产品最终只让离线 SVG/
  字体通过 `attach_native()` 显式 opt-in，运行时转换路径保持内联。完整 Home 首帧和
  60 帧压力均通过，后者为 60 render / 60 submit、26.13 FPS、collect 9.671 ms、encode
  9.203 ms、finish 10.578 ms、GPU busy 27.06%。相对只上传 LVGL 转换路径，label 从
  6.273 降至 5.721 ms/frame，glyph draw 从 5.239 降至 4.837 ms/frame。
- 证据日志：`COM17-vg-close-inline.log`（预期失败反例）、
  `COM17-vg-close-call-fixed.log`、`COM17-vg-call-100-fixed.log`、
  `COM17-vg-asset-call-100-fixed.log`、`COM17-vg-asset-frames-100-fixed.log`、
  `COM17-vg-upload-ui-bench-60-fixed.log` 与 `COM17-vg-native-upload-ui-bench-60.log`，
  以及最终固件的 `COM17-vg-native-upload-asset-frames-512.log`、
  `COM17-vg-native-upload-final-status.log`，均位于 `tools/freather/logs/`。

## 2026-09-02 Stroke CALL 隔离、触摸协议修复与卡死回归

- 实板 framebuffer A/B 证明 SVG 源、fill 和字体路径正确；缺失的 System 滑杆、Media
  圆环等画面来自最终 stroke path 的独立 upload/CALL。该路径能收到完成 IRQ，但像素与
  内联结果不等价。因此产品保持 `LV_VG_LITE_USE_PATH_UPLOAD=1`、新增并关闭
  `LV_VG_LITE_USE_STROKE_UPLOAD=0`。Stroke 仍由 GPU 在本帧主 command buffer 内执行，
  不是 CPU/A8 回退；压力测试仍为 60 render / 60 submit。回读
  `touch-fix-fb1.png`/`touch-fix-fb2.png` 已确认桌面全部可见图标的描边闭合、颜色正确。
- 触摸根因是 BSP 把 Sitronix 报告页当 8 位寄存器读取。驱动现按 ST7123 host protocol
  对所有寄存器发送 16 位地址，先读 `0x0010` Advanced Touch Info，仅在 With Coord(bit 3)
  置位后读取 `0x0014` 起的 10 个 7-byte 坐标槽；读取最后槽由控制器自动清 INT，不再向
  只读状态寄存器写清零。实板读到真实 `480x800`、10 points、firmware `01`、revision
  `01470105`，旧 8 位寻址则全零。
- “成功读到无坐标”现在是确定的松手事件。旧状态机会无限复用上一次按下坐标，第一次
  触摸后 LVGL 永远保持 pressed，外观上等同卡死；只有 I2C 传输失败才短暂保留 held 状态。
  I2C 轮询放在独立 20 ms 输入线程，UI 刷新线程不再同步等待总线。
- 修复后连续三轮 60 帧全屏压力均完成：25.97、26.08、26.16 FPS，每轮 60 submit，
  GPU busy 27.70% 左右，software boundary/overflow/scanout timeout 均为 0。最终重新烧录后的
  单轮为 26.13 FPS，render max 78 ms；串口在每轮后继续响应。证据为
  `COM17-stability-bench-3x.log`、`COM17-release-fix-validation.log`、
  `COM17-final-stability-validation.log`、`COM17-vg-inline-100.log` 与
  `COM17-vg-asset-frames-100.log`。
- 当前远程条件下没有真实手指输入，`frames/press/release` 仍为 0，所以这里只确认控制器
  在线、协议/坐标范围正确、UI 不被轮询拖死；物理按下帧与坐标变换仍需现场触摸验收，
  不能把无人触摸的零计数表述为“触摸已板测通过”。

## 2026-09-02 SVG 图标 A8/Vector 像素 A/B 与描边边界修复

- 增加运行时诊断命令 `feather_ui_icon_renderer vector|a8|status`。它只切换同一批 50 个
  SVG 图标的绘制表示，字体、页面对象、颜色和布局保持不变；`a8` 使用构建时预生成的
  24/32/48 px 栅格参考，切换后整屏失效。每种模式先运行 60 帧全屏基准，使两张 direct
  scanout framebuffer 都被当前模式覆盖，再由 OpenOCD 回读两张 512×800 RGB565 缓冲。
- 首轮 A/B 证实不是 SVG 源错误。A8 中完整的 Media 圆环、Settings 滑杆、Gallery 外框和
  Files 文件夹，在 Vector 中都沿中心线路径自己的矩形边界被削掉。VG-Lite 后端调用
  `vg_lite_update_stroke()` 得到外扩轮廓后，又把最终 stroke path 的 bounding box 覆盖成
  原中心线 bounding box，因此外侧半个线宽被 path 自身裁剪；这发生在对象 scissor 之前。
- 最终 stroke bounding box 现按 `centerline bounds ± (stroke_width / 2 + 1 AA guard)` 设置，
  LVGL 对象/图层 scissor 仍是矩阵变换后的最终逻辑裁剪。修复前 8 个稳定图标 ROI 中，A8
  与 Vector 有 1,458 个强差异像素，去除 1 px 抗锯齿邻域后仍有 413 个拓扑缺失像素；修复
  后分别降为 704 和 **0**。原先最大的 40×32、34×30、16×22 连续缺失块消失，剩余区域
  均在 A8 预采样与 VG-Lite 实时 AA 的 1 px 边缘邻域内。
- 修复后的 Vector 基准为 60 render / 60 submit、26.04 FPS、collect 9.300 ms、encode
  9.458 ms、finish 10.898 ms、GPU busy 27.77%，没有引入额外 submit、软件回退或停滞；
  板上最终状态已恢复为 `vector`。显存原图、五倍差分、连通区域和 ROI 报告位于
  `projects/FeatherTalk_M55/build/icon-ab/`，最终日志为
  `tools/freather/logs/COM17-icon-vector-bbox-final.log`。

## 2026-09-02 Tile 矩阵直绘、Layer 同步归因与产品边界

- 编辑动画旧实现把整个 Tile 先画到临时 Layer，再切回主 framebuffer 缩放合成。旧基准
  12.58 FPS、约 10 submits/frame，并把 14.738 ms 记到 Layer、13.786 ms 记到 Border。
  调用级跟踪证明 Border 是 render-target 切换后的首个任务，统计吸收了前一 Layer 的
  `vg_lite_finish()`；边框本身已经由 GPU 绘制，并不是 13 ms 的 CPU 几何热点。
- 打开 `LV_DRAW_TRANSFORM_USE_MATRIX` 后，满足全不透明、普通 blend、无复杂裁剪的纯缩放
  Tile 直接把任务矩阵交给 VG-Lite，在主 framebuffer 上依次执行 fill、border、label、SVG
  和四角 Chevron。首轮实板编辑态提升到 23.92 FPS、1 submit/frame，Layer=0、
  Border=0.187 ms/frame。两张 512-stride RGB565 scanout framebuffer 回读只有 686 像素
  差异（0.18%），包围盒完全位于动画 Tile；状态栏、导航栏和交界处逐像素不变。
- 尝试过“GPU 写中间 Layer -> 不等待切换 render target -> 立即用该 Layer 作纹理源”的
  单 command-buffer read-after-write 链，PSE84/GC265 实板会停滞。故通用 opacity/filter/
  complex-clip Layer 保留安全完成边界；本次只消除根本不需要离屏表面的纯矩阵 Layer。
- VG-Lite contiguous heap 增加 256 KiB path-upload 预留，`.cy_gpu_buf` 从 `0x2ae000`
  增至 `0x2ee000`，3 MiB `gfx_mem` 尚余 72 KiB。全关上传不会 OOM，但相同固件压力状态下
  可降至约 17.36 FPS；然而把全部运行时转换路径自动上传仍会在页面生命周期压力中停滞。
  产品最终只允许离线 native 字体/SVG fill 通过 `attach_native()` 使用 CALL；运行时路径
  内联，stroke upload 保持关闭。
- 安全构型的全量自动测试完成 351 PASS / 1 FAIL / 163 actions、100,520 ms，未出现
  `VG_LITE_OUT_OF_MEMORY`、GPU hang、对象泄漏或路由泄漏；唯一失败是当前 M33 无线驱动
  未提供测试能力。日志为
  `tools/freather/logs/COM17-direct-matrix-autotest-native-upload-only.log`。
- 关闭测试模式后的最终实板固件：静态桌面 25.78 FPS，collect/encode/finish 为
  13.009/11.061/10.921 ms；Tile 编辑态 23.21 FPS，为
  14.118/12.256/11.146 ms。两轮都是 60 render / 60 submit、Layer=0、GPU 路由 100%、
  software/resource/explicit boundary=0。日志为
  `COM17-direct-matrix-release-wallpaper-static.log`、
  `COM17-direct-matrix-release-edit-bench.log` 与
  `COM17-direct-matrix-release-status.log`。
- 最终 M55 构建 text=7,521,596、data=91,172、bss=4,876,924 字节；
  `.app_code_itcm=0x36138`（余 40,648 字节），`.cy_gpu_buf=0x2ee000`（余 73,728 字节）。
  HEX 21,412,985 字节，SHA-256
  `5D31492013759DB1A5840EE9730F63BEBE1F054F9A0F6FD7D94B1BF38B37FD4E`。已用 Infineon
  Customized OpenOCD 5.19.0.4782 写入并校验 M55 7,612,768 字节及签名 M33 镜像；正式板上
  `test=0`、IPC err=0。验证后已执行 `feather_ui_tile_preview off`，设备留在普通桌面状态。

## 2026-09-02 GPU/CPU 双槽跨帧流水线

- 旧流程在每帧唯一 submit 后立刻 `vg_lite_finish()`，CPU 约有 11 ms 被 GPU 同步等待。
  当前改为双 command buffer + 双帧资源槽：帧 N 异步提交后，CPU 立即收集和编码帧 N+1；
  到下一提交边界才等待、回收并复用帧 N 的 decoder/gradient/transient/framebuffer 资源。
- GFX END 和 DC DISP0 都改为 RT-Thread semaphore 驱动。M55 等待时进入阻塞态，不再 1 us
  忙轮询；GPU 完成由高优先级 worker 延迟提交给 DC，确保 scanout 永远不会看到尚未完成的
  framebuffer。最后一帧由 `lv_gpu_batch_wait_idle()` 显式 drain，页面/基准退出不会遗留引用。
- 统计新增 GPU residual wait、scanout residual wait、流水线阶段与 active/inflight slot。
  `feather_ui_bench` 在开始和结束各排空一次，正式数据严格为 60 render / 60 submit /
  60 completed jobs，不再把动画场景已有的飞行帧误计入基准。
- 重新构建、OpenOCD 写入并显式 reboot 后，静态桌面连续三轮为 50.84--51.32 FPS，
  collect 约 7.33 ms、encode 6.54--6.74 ms、GPU 残余等待 0.217--0.220 ms、scanout 残余
  4.70--4.95 ms、GPU busy 54.2--54.7%。旧同步流程为 25.78 FPS，提升约 97%。
- Tile 编辑动画连续 8 轮、共 480 个全屏帧为 50.50--51.59 FPS；每轮均为 60 submit /
  60 job，collect 8.04--8.06 ms、encode 7.55--7.78 ms、GPU 残余等待 0.220--0.223 ms、
  scanout 残余 2.73--3.30 ms、GPU busy 55.1--56.3%。旧同步流程为 23.21 FPS，提升约
  118%；GPU 路由 100%，software/resource/explicit boundary 与 scanout timeout 均为 0。
- 当前硬件仍是“一帧一条有序 GPU command chain、一次 submit”；并行发生在 CPU 准备
  N+1 与 GPU 执行 N 之间。已证实不安全的跨 render-target read-after-write 仍保留完成边界，
  不会为了合链破坏正确性。
- 一次调查中出现 GPU core 已 idle、软件未完成尾帧回收。等待现限定 100 ms，并在超时后
  检查 core interrupt/idle，以恢复已经完成但 wrapper IRQ 丢失的序列；阶段诊断加入后 8 轮
  压力均正常结束于 `stage=0`。日志为
  `tools/freather/logs/COM17-gpu-pipeline-final-static-3x.log` 与
  `tools/freather/logs/COM17-gpu-pipeline-stage-stress.log`。
- 当前构建 text=7,525,100、data=91,172、bss=4,878,292 字节；`.app_code_itcm=0x36480`
  （222,336 B，余 39,808 B），`.cy_gpu_buf=0x2ee000`（3,072,000 B，余 73,728 B）。其中
  双 transient arena 各 192 KiB、持久 glyph arena 192 KiB，`lv_gpu_batch.o` 共 576 KiB。
  HEX 21,422,840 字节，SHA-256
  `BCC4942A7CF0094F3694D0A1CAA8D14FC01E2709FCA0A80F8545B81BD643740D`。

## 2026-09-02 全场景 GPU/CPU 压力回归

- 新增 `feather_ui_scene <id|list>`，覆盖 Home、Search、System、Settings、Media、Recorder、
  Gallery、Files、About、7 个 Settings 子页，以及 All Apps、通知栏展开/半拖动、两个键盘、
  Tile 编辑、相册查看器、文件菜单、播放态和 Alert，共 27 个可重复视觉状态。
- 新增 `tools/freather/benchmark-ui-scenes.py/.cmd`。脚本在一个 COM17 会话内逐场景建立状态、
  等待稳定、执行 60 帧全屏基准、解析完整/primitive/Label 三组记录、输出 CSV 并在最后恢复
  Home。多 submit 现在作为 batching warning 单独报告；只有 render/batch/job 未完成才判失败。
- 基准帧泵保留 LVGL 一次性 async timer 的调度边界。曾尝试从 `RENDER_READY`/`REFR_READY`
  直接失效或复用 1 ms timer，实板分别暴露无效区被清零、同一 handler 内连续重入和 UI 线程
  饥饿；这些方案已撤销。独立 4 KiB 看门狗只在连续 1 秒无新帧时读取无锁标量快照，不参与
  正常帧路径。首次用 RT-Thread soft timer 执行大结构统计曾使 timer 线程栈溢出并在
  `rt_tick_increase()` 触发精确 BusFault，现已改为独立线程。
- 基准开始到结束期间暂停 M55 IPC 的 10 秒周期报告，防止它插入 `[UI-BENCH]` 数据行；IPC
  通信本身继续运行，基准结束立即恢复日志。连续下载后曾出现 M55/LVGL 未干净重启和 Home
  仅约 41 FPS；执行 Infineon OpenOCD `feathertalk_prepare_cm33` +
  `feathertalk_restart_cm33_ns` 并等待 UI 完成启动后恢复为 51 FPS，因此正式基线均在显式
  reset/run 后采集。
- 最终 27 场景、1,620 个全屏帧全部完成，均为 60 render / 60 batch，job 与 submit 相等，
  无看门狗停帧、无 scanout timeout；算术平均 40.30 FPS。14 个场景 >=48 FPS，6 个为
  30--48 FPS，7 个低于 30 FPS。最快 Media playing 51.81 FPS；最慢 Settings+keyboard
  16.03 FPS，CPU collect/encode 为 22.99/38.07 ms、GPU busy 仅 26.07%，确定为 CPU 准备瓶颈。
- 其余主要热点：System 24.84 FPS（379 glyph/frame、Label 20.51 ms）；Search+keyboard
  24.27 FPS（CPU 40.21 ms）；USB/Audio 设置为 31.00/32.39 FPS，仍由大量文字主导；
  Gallery viewer 25.92 FPS 是例外，CPU 仅 13.26 ms，但 GPU/scanout 残余等待 23.97 ms。
  Recorder、Files action、Alert 分别仍有 3、14、13 submit/frame，后两者各有约 11.8/11.4 ms
  Layer 编码，是下一轮批次合并重点。
- 完整逐场景表写入 `docs/board/PSOC-Edge-E84/PSE84_SOC_GPU2D_zh.md`；原始证据为
  `tools/freather/logs/COM17-ui-scene-benchmark-clean-final-20260902.log/.csv`。测试结束已恢复
  Home、route depth 1、Tile edit off、通知栏关闭，IPC err=0。
- 当前 M55 构建 text=7,528,068、data=91,176、bss=4,878,296 字节；HEX=21,431,189 字节，
  SHA-256 `3CCFC57D675BB6AC437C913746C3A38AFD4C3AF17B3F23072D603E3EF5AB8779`。Infineon
  Customized OpenOCD 写入 7,622,656 字节、校验 7,619,244 字节。

## 2026-09-02 音乐播放器 Cover Flow

- Media 页面升级为 Music 播放器：用 5 个循环虚拟槽承载 3 个当前示例专辑，中心封面放大，
  两侧封面按距中心的距离缩小并渐隐。横向拖动使用 LVGL 惯性、单项滚动和中心吸附；点击
  侧边封面会滚入中心。完成吸附后围绕新曲目重绑 5 个槽，因此连续浏览不需要无限对象列表。
- 曲目、艺人、专辑、播放/暂停、上一首/下一首和音量位于同一状态模型；中英文切换会同步
  刷新封面标题与详情，`Feather`、`PSoC` 等产品关键词保持不翻译。自动测试新增 Cover Flow
  五槽有效性、中心/侧边尺寸关系及中心曲目绑定检查；性能场景 4/25 也把该检查作为 ready 条件。
- 布局按实际显示尺寸动态计算。480×800 竖屏采用“Cover Flow 在上、信息和控制在下”；
  90/270 度构建会自动切换为宽屏双栏。当前 LCD scanout 与触摸坐标旋转仍由板级编译配置
  决定，所以进入 Music 不会在运行时偷偷改变整个系统方向；后续若需要单应用自动横屏，
  必须先实现显示 buffer、DC scanout 和触摸矩阵的原子运行时切换。
- 首版对封面父对象设置整体透明度，实板立即暴露 25 submit/frame、Layer 10.33 ms/frame、
  约 27.5 FPS。现改为直接调整各 fill/border/text primitive 的透明度并取消复杂圆角裁剪；
  最终 Media/Media playing 均为 60 render / 60 batch / 60 submit、Layer=0、software=0，
  分别达到 51.23/51.41 FPS，GPU busy 56.97%/57.14%；两个场景的 Cover Flow ready 校验
  均为 `result=0`。最终构建 text=7,533,124、data=91,176、bss=4,878,464 字节，HEX
  21,445,422 字节，SHA-256
  `F94BB170F9FCE5A48CCA5528BD9F3E84909A7ACF00B3F78221CD586854AEC988`。证据日志为
  `tools/freather/logs/COM17-cover-flow-ready-media-final-20260902.log` 和
  `COM17-cover-flow-ready-final-20260902.log`。
- 随后按经典 Cover Flow 补齐 2.5D 翻页：中心封面正对屏幕；左右封面绕视觉 Y 轴向内翻，
  宽度压缩、纵向轻微后退、远侧角内收并叠加暗边。角度、尺寸、透明度和内容偏移都由封面
  中心到视口中心的有符号距离连续计算，穿过中心时自动反转方向；上一首/下一首与触摸拖动
  共用同一条惯性滚动、中心吸附和曲目提交路径。首轮两个三角形拼面在显存回读中暴露对角
  AA 接缝；动态闭合 path 与持续缩放的圆角装饰则会在长循环中触发 VG-Lite path 生命周期
  停滞；在 `DRAW_MAIN` 回调中逐帧追加矩形斜边任务也会在长循环后占住 UI 绘制锁。当前封面
  主体回到 LVGL 普通 GPU 矩形对象，保留宽度压缩、纵向后退、透明度和远侧暗边表达 2.5D
  深度，圆盘装饰保持矩形快路径，不再生成或临时插入逐帧几何任务；侧面透明标题改为真正
  停止绘制。
- 3D 版本实板场景 4/25 的 ready 校验均为 `result=0`，60 帧均保持 60 batch / 60 submit、
  Layer=0、software=0；两场景均为 37.26 FPS，GPU busy 40.74%/40.71%。相较平面版约
  51 FPS 的差额来自 CPU 对动态矢量面和字形的收集/编码，不是额外提交或离屏图层。
  最终构建 text=7,535,412、data=91,176、bss=4,878,504 字节，HEX=21,451,857 字节，
  SHA-256 `6B8D8022FB38032D32C67BE7BD6874727F91F4DB4DCDADAE7A1305631ADB8A84`；日志为
  `tools/freather/logs/COM17-cover-flow-3d-title-cull-final-20260902.log/.csv`。

## 2026-09-02 描边图标分块裁剪修复

- 下拉面板“自动旋转”和桌面 Media Tile 波形的 SVG 源文件、24×24 viewBox 与生成路径均完整；
  缺块不是 SVG 路径少点，而是描边中心线靠近 viewBox 边缘后，真实半线宽与 VG-Lite 的 AA
  保守边界越出图标对象 clip。逐图标 scissor 随后在 160 行局部 framebuffer 边界泄漏，导致
  跨边界的下半个快捷卡片或 Tile 看起来被矩形切掉。
- 公共矢量图标渲染器现在只对含 stroke 的资源保留固定 2 个物理像素内边距，再按剩余区域
  计算矩阵。1 像素 A/B 仍会触发边界问题；2 像素同时覆盖实际轮廓和保守 AA guard，路径不再
  需要比对象更窄的硬件 scissor。fill-only 图标不缩小。
- 同一固件完成 A8/Vector 显存对照；最终 Vector 双 buffer 回读中四个快捷卡片均完整，自动
  旋转图标完整；Home 双 buffer 中 Media Tile 本体及右侧波形完整。证据位于
  `projects/FeatherTalk_M55/build/vector-clip-ab/`。两个 buffer 上位置不同的单条水平线来自
  OpenOCD 停住 M33 而 M55 仍换帧时的抓取撕裂，不是稳定在同一位置的裁剪缺块。
- 新增 `tools/freather/framebuffer-rgb565-to-png.ps1`，按 480×800、512 像素 stride 把板端
  RGB565 显存转换为 PNG，后续边界问题必须先对双 buffer 做像素检查。
- 为避免产品混合流继续依赖跨帧跳转，离线字体和 SVG fill/stroke 都改为预编译 VG-Lite
  原生命令流内联：仍由 GPU 光栅化并进入整帧单链，只取消 `CALL/RETURN`。当时 Cover Flow
  的 100 次连续程序化翻页仍未输出完成标志；后续已通过 M55 线程栈抓取确认它与 SVG/GPU
  无关，并按下节所述修复触摸采样优先级死锁。

## 2026-09-02 Cover Flow 长循环卡死根因与修复

- 卡死现场中 DC frame IRQ、M33/M55 IPC 和 shell 都继续运行，但 LVGL render/present 计数
  停止，全局 LVGL mutex 一直由 `LVGL` 线程持有；GPU 为 `stage=0 active=0 pending=0`，无
  timeout，排除了 GPU command chain、scanout 和 Cover Flow path 生命周期停滞。
- 通过 M33 system AP 的 M55 DTCM remap (`0x48040000`) 读取 `LVGL` TCB 保存的 PSP，并按
  Cortex-M55/RT-Thread 异常栈格式还原 PC/LR。真实 PC 为 `touch_sample_snapshot()` 的
  `if (sequence & 1) continue`，LR 为 `touchpad_read()`，证明 LVGL 在输入读取阶段无界自旋。
- 旧触摸缓存使用 sequence lock。低优先级 `touch` 线程先把 sequence 改成奇数，尚未写完
  就被优先级更高的 `LVGL` 抢占；LVGL 随后等待 sequence 变回偶数，却又阻止发布线程恢复，
  形成确定性的优先级反转死锁。Cover Flow 连续动画只是提高了在这个极短窗口命中的概率，
  不是根因。
- `touch_sample_publish()` 和 `touch_sample_snapshot()` 现改为仅覆盖三个缓存字段的短中断临界
  区，删除 sequence 与所有无界重试。I2C 采样、坐标换算和 LVGL 事件处理均不在临界区内，
  因而不会把慢操作带入关中断区，也不存在等待低优先级线程完成的路径。
- Cover Flow 同时收敛为一个持久控制 timer 的显式状态机：drag、animate、commit、settle
  顺序推进，触摸拖动和按键翻页共用相同曲目提交路径；不再叠加 LVGL scroll/snap/async
  生命周期。动画仍实时改变封面位置、宽高、明暗和远侧遮光，松手后再提交中心专辑。
- 实板不重启连续执行三轮 `feather_ui_media_stress 100`，共 300 次完整动画，每轮均输出
  `complete steps=100 ... ready=1`。结束后 `LVGL` 与 `touch` 线程均为 ready，全局 mutex
  owner 为 NULL，scanout timeout 为 0。
- 随后运行全部 27 个视觉场景、共 1,620 个强制全屏帧，脚本退出码 0；每个场景均完成
  60 render / 60 batch 且 job=submit，最终自动恢复 Home。Media 为 41.03 FPS，
  Media playing 为 36.05 FPS；全局统计 `stage=0`、scanout timeout=0。原始证据为
  `tools/freather/logs/COM17-cover-flow-seqlock-fix-20260902.log` 与
  `tools/freather/logs/ui-scene-benchmark-seqlock-fix-20260902.log/.csv`。
- 最终 M55 构建 text=7,535,732、data=91,176、bss=4,878,552 字节；HEX=21,452,744 字节，
  SHA-256 `FB07605FB829A3527EC7B7F644F763A7DEBB61C8C1EFAEDF08AC5098D509CA62`。
  已用 Infineon Customized OpenOCD 写入并校验 M55 7,630,848/7,626,908 字节及签名 M33
  镜像；验证结束设备留在普通 Home、route depth 1。

## 2026-09-02 原生矢量字体消失回归修复

- Cover Flow 修复后的首个实板帧缓冲回读确认：壁纸、Tile、SVG 图标和导航栏仍在，但所有
  中英文字形都没有产生像素，因而排除了 LCD scanout、颜色转换和字库缺失。
- 根因是 `lv_draw_vg_lite_vector_path_attach_native()` 原有的单个布尔参数同时被误解为
  “是否使用 upload/CALL”。该参数实际是 `add_end`，用于选择 LVGL vector path 的 fill 或
  stroke 缓存槽；字体调用把它改成 `false` 后，字形轮廓进入 stroke 槽，而字体绘制继续请求
  fill，最终得到空路径。
- API 现拆为两个独立参数：`add_end` 明确选择 fill/stroke，`upload` 独立控制上传/CALL。
  字体和 SVG fill 使用 `(true, false)`，SVG stroke 使用 `(false, false)`；因此字形重新进入
  fill 槽，同时仍保持产品要求的原生命令流内联和整帧一次 submit。关闭 CALL 不再能改变
  路径语义。
- 重新构建烧录后，直接从板上读取双 framebuffer 并转换为 480×800 PNG，设置页标题、说明、
  搜索框、全部设置项及状态栏中英文均恢复。60 帧全屏基准实际绘制 182 glyph/frame，
  `glyph-draw=30.517 ms`、`layout/font=3.945 ms`，证明字体路径已进入真实 GPU 绘制链。
- 回归执行 `feather_ui_media_stress 100`，输出 `complete steps=100 track=1 ready=1`，字体修复
  未重新引入 Cover Flow/LVGL 锁死。当前 M55 构建 text=7,535,756、data=91,176、
  bss=4,878,552 字节；HEX=21,452,818 字节，SHA-256
  `D78CD7DF15B554F1709923E083C33FA2401FDDEDCE77556AE9CBF327FCC9EFEB`。

## 2026-09-02 本地录音与 PCM WAV 播放

- 之前 Music 页只有三组 Cover Flow 演示元数据，播放按钮只改变 UI 状态，既没有打开文件，
  也没有向 `sound0` 写 PCM。新增仓库内 `feathertalk_player.c/.h` 后端，扫描
  `/sdcard/Recordings`、`/flash/Recordings`、`/sdcard/Music` 和 `/flash/Music`，最多保留
  24 个通过校验的本地曲目。
- WAV 解析器按 RIFF chunk 遍历 `fmt ` 与 `data`，不把音频数据硬编码在 44 字节；当前只接收
  与板载播放驱动能力一致的未压缩 PCM：16/24/48/96 kHz、16/24 bit、单/双声道。录音机生成
  的 16 kHz、16 bit、双声道 WAV 可直接播放；MP3/AAC/FLAC 解码尚未宣称支持。
- 后台 `ft_player` 线程从文件系统读取对齐 PCM block，按文件格式配置 TDM0 与 ES8388 后再
  打开 `sound0`。支持播放、暂停、继续、停止、当前位置和总时长，离开 Music 页面后继续播放。
  Music 的 Cover Flow、标题、来源、格式、播放图标和进度条均绑定真实媒体库；从 Recorder
  返回 Music 时会重新扫描，因此新录音无需经过 Files 应用。
- 新增共享输出所有权：本地播放器和 USB UAC 在改变格式或打开 `sound0` 前分别申请
  `LOCAL_PLAYER`/`USB_UAC`，冲突返回 `-RT_EBUSY`，不会让两个线程同时改 TDM/Codec 并交错
  写入。UAC 停流或关闭设备后主动释放所有权。
- 实板 SD 卡识别到 4 个 Recorder WAV，逐一解析得到 1.792--6.502 秒、16 kHz/16 bit/2 ch。
  `feather_player play 3` 后驱动实际切换为 16 kHz，ES8388 回读为 16 bit、MCLK=128×Fs；
  诊断统计累计 810 次 `rt_device_write`、0 次 transmit fail、0 次 underflow。暂停状态保持
  position=3840 ms 与 owner=LOCAL，继续后恢复，曲末释放 owner；通过 UI 场景 25 也实际启动
  相同播放链。动态四曲媒体库完成 Cover Flow 100 次连续翻页，最终 `ready=1`。
- 最终媒体库把录音按文件名时间戳降序排列，最新录音位于 Cover Flow 中心；当前首项为
  `REC_0000116025_00.wav`。最终 M55 构建 text=7,544,560、data=91,200、bss=4,887,720
  字节；HEX=21,477,637 字节，SHA-256
  `81DD6ECEDEC418EAC642345E34638D00AB89EB0460CA71E5F18F02E9B65345CF`。Infineon
  Customized OpenOCD 已写入并校验 M55 7,639,040/7,635,760 字节以及签名 M33 镜像。

## 2026-09-02 可选文件夹播放列表与 MP3

- 修正“播放器只绑定录音目录”的模型：Music 页新增文件夹选择器，可在 SD 卡和内置 Flash
  中逐层浏览并选取任意目录。媒体库只收集所选目录的直属 WAV/MP3，目录本身就是明确的
  播放列表边界；默认目录为 `/sdcard/Music`。循环开关启用时曲末自动播放下一项并回绕。
- SDK 内原先没有 MP3 解码器。现把官方 `lieff/minimp3` 单头文件及 CC0-1.0 许可证完整固化
  到 `applications/third_party/minimp3`，产品构建不依赖仓库外源码。扫描阶段只解析 MPEG
  Layer III 帧头，播放线程负责真正解码；44.1 kHz PCM 以线性插值转为板载 TDM/ES8388
  已验证的 48 kHz、16 bit、单/双声道输出。
- 首轮实板扫描直接在 tshell 中调用完整 MP3 解码器，超过 4 KiB shell 栈并触发 stack
  overflow；改为无解码的帧头探测后消除。首轮播放又测得 minimp3 Layer-III 合成峰值超过
  16 KiB worker 栈并损坏线程控制块；播放器改用独立 32 KiB 栈后，实测峰值为 72%，留有
  约 9 KiB 余量。该内存不是 UI 绘制缓存，也不会占用线程栈以外的长期静态区。
- 实板 `/sdcard/Music` 识别到浏览器下载的 319112 ms MP3：44.1 kHz、16 bit、双声道。
  播放时 `sound0`/TDM0/ES8388 切换为 48 kHz、16 bit、双声道，连续 14.8 秒诊断累计
  1392 次写入、0 transmit fail、0 underflow；暂停保持位置，继续恢复，停止后 owner 从
  `LOCAL_PLAYER` 释放。随后切换到 `/sdcard/Recordings`，5 个 PCM WAV 均能重新扫描并播放。
- 在同一构建上再次执行 100 次 Cover Flow 连续翻页，输出
  `complete steps=100 track=0 ready=1`；压力测试结束后立即重新播放该 MP3，连续 15.3 秒
  状态保持 `error=0`，停止后输出设备所有权正常释放。最终 HEX SHA-256 为
  `06B211680A7FA1FD74767D3A97004E3A7C513F18622672C08F4FEF0D4474711E`。

## 2026-09-02 音乐目录切换与动态矢量字体度量

- “选择音乐文件夹”不再把目录选择当作只能在 STOPPED 状态修改的普通设置。选择有效目录
  会立即发布 stopped 状态、关闭旧音频流、替换播放列表根目录并重新扫描；无论之前正在
  播放、暂停还是尚未播放，都不要求用户先暂停，也不再弹出语义错误的异常提示。worker
  消费 STOP 后再次清零 position/duration，消除了最后一个音频 block 与目录切换并发时偶发
  的旧进度残留。实板在 MP3 播放中从 `/sdcard/Music` 切到 `/sdcard/Recordings` 返回 0，
  得到 5 个 WAV，最终状态为 stopped、position=0、owner=0。
- 目录弹窗改为明确的“音乐文件夹”：SD 卡、内置 Flash、上一级和子目录组成浏览视图，底部
  主操作为“切换到此文件夹”。弹窗和滚动内容显式继承产品字体，目录行不再混入私有区
  `LV_SYMBOL_DIRECTORY`；双 framebuffer 回读确认内置中英文文案均能形成字形，没有方框占位。
- 字体偏细的根因是旧生成器用 `opentype.js` 打开可变字体后落在默认 Thin 100 实例。LVGL
  矢量生成器现固定使用 SDK 已跟踪的官方 Noto Sans SC Medium 500 静态 OTF，并验证 OS/2
  weight；任一请求字符缺失会让构建直接失败。当前产物包含 7,586 个字形、3,427,980 字节
  canonical S16 路径数据。
- 字体轮廓仍只保存一份 1000 UPM 原始坐标，不是预先栅格化的固定字号。8--48 px 的每个
  整数尺寸按需建立持久 `lv_font_t`，GPU 矩阵动态缩放轮廓；advance、glyph offset、line
  height 和 baseline 则从 UPM 与字体 typographic ascender/descender 动态换算。旧实现把
  18 px 折为 16、30 px 折为 22，并用 `字号+2`/`字号÷6` 猜行高和基线，已完全删除。
- 实板运行新增 `media-folder` 在内的全部 28 个视觉场景，共 1,680 个强制全屏帧，脚本退出
  码 0；所有场景均完成 60 render/60 batch，GPU pipeline 最终 stage=0，scanout timeout=0，
  UI overflow=0。随后执行 100 次 Cover Flow 连续动画，输出
  `complete steps=100 track=0 ready=1`。证据为
  `tools/freather/logs/ui-scene-benchmark-font-folder-final-20260902.log/.csv` 和
  `COM17-font-folder-stress-final-20260902.log`。最终 M55 构建 text=5,132,936、
  data=91,376、bss=4,894,560 字节；HEX=14,694,817 字节，SHA-256
  `E5E0852AAA351B0485B903014B7BCDBBB2B16E6264C2C171613827A2325D512F`。Infineon
  Customized OpenOCD 已写入 5,226,496 字节并校验 5,224,312 字节；验证结束设备留在
  Home、route depth 1，本地播放器 stopped、owner=0。

## 2026-09-03 文本块动态重排修复

- 实板 framebuffer 证明字体轮廓、GPU scale 和 baseline 本身一致；系统信息错位来自页面
  布局：flex-grow 值标签在最终列宽确定前参与 `LV_SIZE_CONTENT` 计算，换成两行后父行仍
  使用左侧键名的一行高度，导致下一条卡片内分隔线穿过第二行。现在每个系统值行在尺寸或
  布局变化后，用实际文本、实际字体、letter/line spacing 和最终可用列宽调用
  `lv_text_get_size()`，再把测得的最大文本高度发布为行 content height。没有假设字号、
  行数或固定像素高度；分辨率、语言、运行数据或 UI scale 改变都会重新测量。
- 音乐页原来把 Cover Flow 之后的固定剩余高度强塞给文件名、来源、格式、进度、控制和
  音量。长文件名的 `LV_LABEL_LONG_DOT` 在高度未约束时会先换成多行，再覆盖后续对象。
  现在文件名、来源和格式均以当前 `lv_font_get_line_height()` 建立一行省略视图，详情容器
  使用内容高度，外层可滚动页面承载增加的空间；字号和屏幕 scale 改变后行高会自动更新。
- 修复前后分别回读 System 与 Media framebuffer。修复后“处理器”的第二行位于本行内部，
  下一分隔线落在文字之后；MP3 长文件名只占一行并在实际宽度处省略，来源、格式、按钮和
  音量互不覆盖。证据 PNG 位于 `tools/freather/logs/font-layout-ab/`。
- 实板场景 2/4 各完成 60 个全屏帧，render/batch 全部完成，pipeline stage=0、
  scanout timeout=0、UI overflow=0；测试结束恢复 Home。最终构建 text=5,133,824、
  data=91,376、bss=4,894,560 字节；HEX=14,697,321 字节，SHA-256
  `804D20DB9E776FD20C36BD1E980ECBE5087A84FE50268B606E4F029B3B06421C`。

## 2026-09-03 全局矢量字形像素边界与基线修复

- 上一节只解决了文本块与父容器的换行高度，不能据此判定字形基线正确。实板放大检查
  “ST7102/ST7123 电容触控；长按 500 ms”确认，同一行内仍有个别拉丁字符、数字和汉字
  上下错开 1 像素；这是字体后端的通性问题，不是该页面或该字符串的局部布局问题。
- 根因是旧描述符分别计算 `ceil((yMax-yMin)*scale)` 和 `floor(yMin*scale)`。LVGL 用
  `line_height-base_line-box_h-ofs_y` 定位字形顶部，这两个独立取整相加并不恒等于
  `ceil(yMax*scale)`，不同轮廓会得到不同的额外像素。横向的相同算法也可能导致右边界
  少 1 像素或多 1 像素。
- 字体后端现在先对每个字形的绝对 `xMin/yMin` 做 floor、对绝对 `xMax/yMax` 做 ceil，
  再由两端之差得到 `box_w/box_h`；advance 使用整数四舍五入。全部换算使用有符号整数
  除法，不依赖浮点数接近整数时的舍入结果。由此对任意字号都严格满足
  `ofs_x+box_w=xMaxPx`、`ofs_y+box_h=yMaxPx`，所有字形共享字体基线。
- 新增 `ft_vector_font_metrics_self_test()` 及 MSH 命令 `feather_font_metrics_test`，遍历
  8--48 px 的每个整数字号和全部 7,586 个生成字形，而非只检查当前可见文字。实板执行
  输出 `PASS (7586 glyphs, 8..48 px)`。系统信息场景 framebuffer 与放大前后对比位于
  `tools/freather/logs/font-global-metrics-20260903/`。
- 修复固件重新构建、写入并校验成功；系统信息页单独 60 帧基准完成 60/60，29.15 FPS，
  GPU busy 33.59%，pipeline 与 scanout 均无 timeout。最终构建 text=5,134,384、
  data=91,376、bss=4,894,560 字节；HEX=14,698,896 字节，SHA-256
  `BAF0E9701F1B854C1550B5777574D1EF4D86D1C3C93691C6C9B2F794CC22E7AD`。
- 全局调用链审计确认：产品页面显式字体全部来自 `ft_layout_font()`，display theme 的
  normal font 也指向同一后端；旧 12/14/16/22 px A8 字体由 UI `SConscript` 排除，未混入
  当前固件。Montserrat fallback 只服务 LVGL 的剪切、目录、上下箭头等私有图标码位。
- 修复后完整执行 0--27 共 28 个视觉场景、每场景 60 个强制全屏帧，合计 1,680 帧；
  脚本退出码 0，全部场景完成 60/60，最终 Home route depth 1、UI overflow=0、pipeline
  stage=0、scanout timeout=0。日志与 CSV 为
  `tools/freather/logs/ui-font-global-metrics-all-scenes-20260903.log/.csv`。

## 2026-09-03 音频设备选择页与独立属性页

- “设置 > 音频”从把全部设备和参数堆在同一页面，改为两层设备模型。主页按输出/输入列出
  物理设备，每行包含默认设备单选状态、独立设备图标、注册状态、驱动名称和属性页箭头；
  当前唯一可用输出 `sound0` 与输入 `mic0` 分别被选为默认设备。AMIC2 模拟前端继续显示，
  但由于没有 RT-Thread Audio 驱动，不可选择也不能进入伪属性页。
- 点击板载扬声器进入输出属性页。页面从 `feathertalk_audio` 的实时状态和完整候选组合判断
  生成 16/24/48/96 kHz、16/24 bit、单/双声道控件，并保留 0--100 音量；任意组合若未被
  `sound0`、TDM0 和 ES8388 整条链路接受，就保持禁用，不能只靠 UI 表格假定可用。
- 点击双 PDM 麦克风进入输入属性页。当前 `mic0` 驱动只上报 16 kHz、16 bit、双声道，
  因此格式作为只读实时值显示，只提供驱动实际支持的 0--37.5 dB 输入增益，不伪造采样格式
  下拉框、音频增强或空间音效。
- 自动化接口新增主页、输出属性页和输入属性页的对象/路由/能力断言；视觉场景新增 28/29，
  覆盖两种属性页。实板场景 10、28、29 均进入成功，route depth 分别为 3、4、4；三场景
  各完成 60 个全屏压力帧，无 UI overflow 或 scanout timeout。输出与输入属性页保持每帧
  一次 GPU submit。主页初测每帧有 3 个 software boundary，逐项统计确认不是字体或圆形
  轮廓，而是三个 `lv_button` 设备行继承的主题阴影；显式清零 shadow 后主页也达到
  60 frame / 60 batch / 60 submit、software task=0，帧率由 20.81 提升到 32.11 FPS。
  单选状态使用矢量空心圆和独立中心点，不依赖软件 Border 绘制。
- 板端双 framebuffer 已分别回读声音主页、输出属性页和输入属性页；设备行、单选标记、
  属性箭头、格式控件和底部导航均处于有效区域，没有新增交界脏块或裁剪。验证日志和 PNG
  位于 `projects/FeatherTalk_M55/build/audio-*-page.*` 与
  `projects/FeatherTalk_M55/build/audio-device-pages-bench.log`（构建目录不纳入版本控制）。
- 最终固件 text=5,137,976、data=91,376、bss=4,894,600 字节；HEX SHA-256 为
  `5337B3AD7E628D043E3C50089928B24709206695C03D432CD5C1256EBC5B93E7`。Infineon
  Customized OpenOCD 已写入 5,230,592 字节并校验 5,229,352 字节。

## 2026-09-03 USB 总开关、功能选择与独立属性页

- “设置 > USB”改为清晰的四层状态：USB 总开关、端口角色、设备功能选择、功能属性。
  总开关直接调用现有 `ft_usb_set_function()`：关闭进入 `NONE` 并真正停止 CherryUSB，
  开启则启动当前选中的 MSC 或 UAC2，不再用页面底部的“停止 USB 功能”按钮替代开关。
- Device 是当前唯一可选角色；Host 仍显示但禁用，并明确说明本板 Type-C 用户口没有主机
  VBUS 供电路径。MSC 与 UAC2 使用独立单选区域，USB 关闭时可以先配置待启动功能，USB
  运行时切换功能会沿驱动现有的 stop-old/start-new 路径执行，失败时保留真实错误状态。
- MSC 与 UAC2 分别增加独立属性页。MSC 页显示固定的 LUN0 Internal Flash、LUN1 SD card
  及实际可用状态，并可跳转本机存储管理；SD 卡缺失时 MSC 单选项禁用，但属性页仍可进入
  查看原因。UAC2 页承接 sound0/mic0 设备和输出/输入格式，主 USB 页不再承载专属参数。
- 自动化增加主页面总开关/角色/功能状态断言，以及 MSC、UAC2 属性页 push/pop 和控件能力
  检查；性能场景新增 30 `settings-usb-storage`、31 `settings-usb-audio`。新增中文字符已重新
  进入全局矢量字库，当前生成资源含 7,587 个字形。
- 修复属性页返回后的共享状态监视生命周期：USB 主页面统一持有监视定时器，两个属性页
  push/pop 不再提前销毁它。实板自动化中 USB #176--#187 全部通过，覆盖离线选择 UAC2、
  总开关真实启动/停止 USB stack、MSC/UAC2 属性页进入与返回；本轮出现的 3 个非 USB 失败
  仍是 M33 快捷能力不可用及既有动态磁贴内容未推进。
- 正式配置 clean build 为 text=5,141,920、data=91,376、bss=4,894,656 字节；HEX 为
  14,720,091 字节，SHA-256
  `C567BD9BB87AB8BD0E54E34D246966743E3735D1048AD858EA6A984D6FC29E79`。Customized
  OpenOCD 写入 5,234,688 字节并校验 5,233,296 字节。实板 60 帧压力测试分别为 USB 主页
  26.03 FPS、MSC 属性页 35.60 FPS、UAC2 属性页 25.41 FPS；每页均为 60 batch / 60 submit、
  software task=0、无 pipeline/scanout 错误。测试后回到 Home（route depth 1），最终 USB
  状态为 `function=none active=0 luns=0 error=0`。

## 2026-09-03 UI 功能导航图与构建一致性门禁

- 新增 `UI_NAVIGATION_MAP_zh.md`，以 Mermaid 路由图和契约表同时记录常驻 Shell、Start/
  All Apps、通知面板、键盘/Alert、设置层级、5 个默认应用、21 个路由页面及 32 个实板
  性能场景。每个功能分支同时描述入口、操作、返回/释放语义、硬件门控、状态所有者与
  持久化边界，避免导航图退化为只有页面名称的静态示意图。
- 新增 `tools/freather/check-ui-navigation-map.py`。检查器从 `ft_page_id_t`、`s_pages[]` 和
  `benchmark-ui-scenes.py` 读取稳定标识，要求文档中的 Page ID 与 Scene 标识一一对应，
  并拒绝缺失、未知或重复项。`build-demo.ps1` 已把它接入 FeatherTalk_M55 构建前置步骤；
  因此新增页面或场景但未同步导航图会使构建失败。
- 本地检查结果为 `21 pages, 32 benchmark scenes`，随后 FeatherTalk_M55 增量构建通过；
  固件尺寸保持 text=5,141,920、data=91,376、bss=4,894,656 字节，没有因文档门禁引入
  运行时代码或存储开销。
