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
- 第二次板上排查确认仅修 UI 事件仍不够。ST7123/ST7102 在 `0x0010` bit 3 给出新坐标帧，读完坐标表后会清除 INT；原 BSP 却在读完后用已经清除的 INT 判断是否抬手，因此静止手指会立即形成伪释放，LVGL 永远累积不到长按时间。驱动现按每槽 Valid 位与非零 Touch Intensity 双条件确认触点；没有新帧时保持上次状态，释放需连续 3 个坐标帧确认，并过滤 3 px 内的静止抖动。LVGL 长按阈值明确设为 500 ms，滚动阈值由 10 px 提升至 18 px。
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

- 电池、电源采样、Wi-Fi/网络、蓝牙和屏幕旋转的板级驱动尚未在产品配置中启用；SD 卡已经通过 SDHC1/Elm-FatFS 接入 `/sdcard`，末尾固定 2 MiB 外部 Flash 作为 `/flash` FAT 用户盘，USB Device MSC 使用 Flash/SD 双 LUN。UI 与 ABI 4 IPC 已具备无线能力、启用、连接和 Wi-Fi 信号强度接入点，并明确显示不可用。显示亮度已经接入 M55 `pwm18`。
- 当前偏好仅在本次启动期间保存，这是原计划要求的“内存桩”；持久化需要后续选定 FlashDB/EasyFlash 或产品配置服务。
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
