# UI 框架 — Vue-Inspired LVGL 框架

> 本文件是 `src/ui/` 各子目录 README 的合并版本，覆盖框架全部子系统。

本目录实现了一套面向嵌入式设备的 "Vue-inspired" 响应式 UI 框架，基于 LVGL 8.3.11，运行在 TuyaOS 平台（ARM Cortex-M33）。框架采用**单向响应式数据流**：`ui_state` 是唯一数据源，通过 dirty 位图驱动页面刷新。

由 Kconfig `ENABLE_TUYA_UI`（生成的宏为 `CONFIG_ENABLE_TUYA_UI=y`）控制编译。所有改动须遵守 `src/ui/RULES.md`。

---

## 导航目录

- [目录结构](#目录结构)
- [一、核心运行时 — `core/`](#一核心运行时--core)
  - [响应式数据流](#响应式数据流)
  - [`ui_state` — 全局状态存储](#ui_statec--ui_stateh--全局状态存储)
  - [`ui_route` — 页面路由与生命周期](#ui_routec--ui_routeh--页面路由与生命周期)
  - [`ui_tick` — 定时驱动器（30ms）](#ui_tickc--ui_tickh--定时驱动器30ms)
- [二、页面实现 — `pages/`](#二页面实现--pages)
  - [编写约束](#编写约束)
  - [页面 ID 枚举](#ui_page_idsh--页面-id-枚举)
  - [Home — 主屏页面](#ui_page_homec--主屏页面锁屏风格)
  - [Chat — AI 对话页面](#ui_page_chatc--ai-对话页面)
  - [Pulldown — 下拉面板](#ui_page_pulldownc--下拉面板快捷控制中心)
  - [App Center — 应用中心](#ui_page_app_centerc--应用中心)
  - [Settings — 设置页面](#ui_page_settingsc--设置页面)
  - [About — 关于页面](#ui_page_aboutc--关于页面)
  - [OTA — 系统升级页面](#ui_page_otac--系统升级页面)
  - [Mode — 设备模式选择](#ui_page_modec--设备模式选择)
  - [Call — 通话页面](#ui_page_callc--通话页面)
  - [Contacts — 联系人页](#ui_page_contactsc--联系人页)
  - [Photo — 相册页面](#ui_page_photoc--相册页面viewer--grid)
  - [Video — 本地视频页](#ui_page_videoc--本地视频页)
  - [Music — 音乐播放页面](#ui_page_musicc--音乐播放页面)
  - [Music List — 播放列表页面](#ui_page_music_listc--播放列表页面)
  - [Clock — 时钟页](#ui_page_clockc--时钟页)
  - [Schedule — 日程提醒页](#ui_page_schedulec--日程提醒页)
  - [Camera — 相机页](#ui_page_camerac--相机页)
  - [Recording — 录音页](#ui_page_recordingc--录音页)
  - [Recording List — 录音列表页](#ui_page_recording_listc--录音列表页)
  - [Recording Transcribe — 录音转写页](#ui_page_recording_transcribec--录音转写页)
  - [Files — 文件浏览页](#ui_page_filesc--文件浏览页)
  - [Detection — 侦测记录页](#ui_page_detectionc--侦测记录页)
  - [Audio Diag — 音频诊断页](#ui_page_audio_diagc--音频诊断页)
  - [Diag — 诊断页](#ui_page_diagc--诊断页)
  - [Sys Status — 系统状态页](#ui_page_sys_statusc--系统状态页)
  - [Net Status — 网络诊断页](#ui_page_net_statusc--网络诊断页)
  - [Screen Test — 屏幕测试页](#ui_page_screen_testc--屏幕测试页)
  - [WLAN — WiFi 连接页](#ui_page_wlanc--wifi-连接页)
  - [Activation — 首启激活向导](#ui_page_activationc--首启激活向导)
  - [页面导航关系](#页面导航关系)
- [三、可复用组件库 — `widgets/`](#三可复用组件库--widgets)
- [四、视觉样式系统 — `style/`](#四视觉样式系统--style)
- [五、国际化 — `i18n/`](#五国际化--i18n)
- [六、后台服务 — `services/`](#六后台服务--services)
- [七、平台移植层 — `port/`](#七平台移植层--port)

---

## 目录结构

| 目录 | 职责 |
|---|---|
| `core/` | 状态管理、页面路由、渲染驱动时钟 |
| `pages/` | 所有页面实现（Home、Chat、Pulldown、App Center、Settings、About、Mode、Call、Contacts、Photo、Video、Music、Music List、Clock、Schedule、Camera、Recording、Recording List、Recording Transcribe、Files、Detection、Audio Diag、Diag、Sys Status、Net Status、Screen Test、WLAN、Activation、OTA） |
| `widgets/` | 可复用 UI 组件库（StatusBar、Button、Label、Card、List、Navbar、Bar、Icon、Popup、Slider、Link、Picture、RingAlarm、Keyboard、Tabview、lv_tef） |
| `style/` | 视觉设计令牌（颜色/字体/间距/圆角）、自适应缩放、Flex 布局辅助 |
| `i18n/` | 国际化文案管理（中/英双语） |
| `services/` | 后台数据/控制服务（天气、设备控制、设备信息、通话、相册图片、音乐、相机、录音、转写、侦测、文件系统、时间管理、音频诊断、系统监控、网络监控、WiFi、NFC、激活状态；另有功能簇关闭时的降级桩 `ui_svc_stubs.c`） |
| `port/` | 平台移植层（LVGL ↔ TuyaOS 硬件解耦） |
| `lvgl/` | LVGL 8.3.11 源码（未修改） |
| `assets/` | 图标/图片资源 |

---

## 一、核心运行时 — `core/`

框架的骨架，负责**状态管理、页面路由、事件总线和渲染驱动时钟**四项基础能力。业务代码（pages、widgets）只依赖这四个模块，不直接操作 LVGL 的全局状态。

顶层入口在 `ui_app.c`：`ui_app_init()` 按 port → adaptive/theme/i18n → state/event → route 注册 → tick/statusbar/pulldown/services → push HOME 的顺序完成初始化；`ui_app_async_call(cb, data)` 是非 UI 线程向 UI 线程投递回调的统一通道（services 层依赖它做线程封送）。

### 响应式数据流

```
ui_state_set_*()  →  dirty 位图  →  ui_tick（30ms）→  on_enter(dirty)  →  page 刷新 LVGL
```

`ui_state` 是唯一数据源；`ui_tick` 是唯一消费者；page 的 `on_enter` 回调根据 `dirty` 按需更新 widget，绝不反向读取 widget 状态。

### `ui_state.c / ui_state.h` — 全局状态存储

将所有运行时状态划分为三个组（`ui_state_group_t`）：

| 组 | 结构体 | 字段 |
|---|---|---|
| `UI_STATE_GROUP_SYSTEM` | `ui_state_system_t` | 电池、WiFi 强度、音量、时间戳、充电状态、日历/闹钟数量、联网状态 |
| `UI_STATE_GROUP_SETTINGS` | `ui_state_settings_t` | 语言、主题模式、背光亮度 |
| `UI_STATE_GROUP_CHAT` | `ui_state_chat_t` | 对话状态、设备模式、对话子模式 |

每个 `ui_state_set_*()` 函数内部通过 `STATE_SET` 宏实现**值变才标脏**的优化（相同值写入不触发刷新）；`ui_state_consume_dirty()` 原子读取并清零 `s_dirty` 位图，供 `ui_tick` 调用后分发给当前页面。

### `ui_route.c / ui_route.h` — 页面路由与生命周期

维护一个深度为 8（`UI_ROUTE_STACK_DEPTH`）的页面 ID 栈。**根页面（`ui_route_init(root)` 指定，当前为 HOME）是不可移动的栈底**：永远不会被 pop / replace 掉。

| 函数 | 行为 |
|---|---|
| `ui_route_push(id)` | 推入新页面。已在栈内 → 提升到栈顶（去重）；push 根页面且当前在更深层 → 等价"回到主页"（unwind 到根页） |
| `ui_route_pop()` | 销毁当前页面并返回上一页；**拒绝弹出根页面**（在根页或空栈上是安全 no-op） |
| `ui_route_replace(id)` | 替换当前页面（不增加栈深度）；若当前就是根页，则退化为 push（根页不被替换） |
| `ui_route_reset(id)` | 清空根页以上的所有页面（根页保留在栈底），再 push 目标页 |
| `ui_route_current()` / `ui_route_previous()` | 只读查询当前页 / 当前页正下方页面；用于 overlay 判断能否复用其覆盖的页面实例 |
| `ui_route_refresh_current(dirty)` | 用指定 dirty 位图重新调用当前页的 `on_enter`，由 `ui_tick` 调用 |

每个页面通过 `ui_page_entry_t` 注册，包含四个生命周期回调：`on_create`（一次性建 widget）、`on_enter`（每次进入/状态刷新）、`on_leave`（离开前台，含被新页面覆盖）、`on_destroy`（销毁 widget）。

注意：**push 新页面时旧页面不销毁**（只触发 `on_leave`），其 LVGL 树仍留在栈底——因此持续动画必须在 `on_leave` 停止，见 RULES.md §9。

### `ui_tick.c / ui_tick.h` — 定时驱动器（30ms）

通过 `lv_timer_create()` 挂载到 LVGL 计时系统：

1. **消费 dirty 位图**：`ui_state_consume_dirty()` 获取当前变化的组。
2. **轮询本地时钟**（约 1s 一次）：每 33 个 tick 读一次系统时间，**秒变化即标脏 SYSTEM**（秒级粒度，供通话时长、音乐进度等页面渲染秒级内容；statusbar 等只关心分钟的消费者自行去重，所以秒级 tick 对它们近乎免费）。
3. **刷新 StatusBar**：`UI_STATE_GROUP_SYSTEM` 变脏时调用 `ui_comp_statusbar_refresh()`。
4. **通知当前页面**：调用 `ui_route_refresh_current(dirty)`。

无 dirty 时 tick 只做秒级时钟比对，**不会触碰任何页面**——页面刷新完全由状态变化驱动。

---

## 二、页面实现 — `pages/`

### 编写约束

- `on_create`：一次性创建 LVGL widget，不设置内容。
- `on_enter(dirty)`：根据 dirty 掩码按需从 `ui_state_get_*()` 读取状态并更新 widget。
- `on_leave`：离开前台（含被覆盖）。停止持续动画、清理单槽服务回调、设置类页面调用 `ui_svc_dev_ctrl_save()` 持久化。
- `on_destroy`：必须用 `lv_obj_add_flag(HIDDEN)` + `lv_obj_del_async()`，**禁止**用 `lv_obj_del()`。
- 绝不在页面内反向读取 widget 状态，`ui_state` 是唯一数据源。

### `ui_page_ids.h` — 页面 ID 枚举

| ID | 说明 |
|---|---|
| `UI_PAGE_HOME` (1) | 主屏（锁屏页，根页面） |
| `UI_PAGE_CHAT` (2) | AI 对话页 |
| `UI_PAGE_PULLDOWN` (3) | 下拉面板（快捷控制中心，overlay） |
| `UI_PAGE_APP_CENTER` (4) | 应用中心（卡片网格启动器） |
| `UI_PAGE_SETTINGS` (5) | 设置页 |
| `UI_PAGE_ABOUT` (6) | 关于页（设备信息） |
| `UI_PAGE_MODE` (7) | 设备模式选择页（保留页） |
| `UI_PAGE_CALL` (8) | 通话页（P2P 语音呼叫） |
| `UI_PAGE_CONTACTS` (9) | 联系人页（P2P 通话 + NFC 发现） |
| `UI_PAGE_PHOTO` (10) | 相册页（大图浏览 + 缩略图网格） |
| `UI_PAGE_MUSIC` (11) | 音乐播放页 |
| `UI_PAGE_MUSIC_LIST` (12) | 播放列表页 |
| `UI_PAGE_CLOCK` (13) | 时钟页（闹钟/倒计时/秒表/番茄钟） |
| `UI_PAGE_SCHEDULE` (14) | 日程提醒页 |
| `UI_PAGE_CAMERA` (15) | 相机页（预览 + 拍照） |
| `UI_PAGE_RECORDING` (16) | 录音页 |
| `UI_PAGE_RECORDING_LIST` (17) | 录音列表页 |
| `UI_PAGE_RECORDING_TRANSCRIBE` (18) | 录音转写页 |
| `UI_PAGE_FILES` (19) | 文件浏览页 |
| `UI_PAGE_DETECTION` (20) | 侦测记录页 |
| `UI_PAGE_AUDIO_DIAG` (21) | 音频诊断页 |
| `UI_PAGE_DIAG` (22) | 诊断页（性能/测试项入口） |
| `UI_PAGE_SYS_STATUS` (23) | 系统状态页（内存/运行时长/复位原因） |
| `UI_PAGE_NET_STATUS` (24) | 网络诊断页（WiFi/IP/RSSI/云连接） |
| `UI_PAGE_SCREEN_TEST` (25) | 屏幕测试页（纯色/彩条/触摸画线，全屏沉浸） |
| `UI_PAGE_WLAN` (26) | WiFi 连接页（扫描/密码输入/连接） |
| `UI_PAGE_ACTIVATION` (27) | 首启激活向导 |
| `UI_PAGE_OTA` (28) | 系统升级页（检查更新 + OTA 进度） |
| `UI_PAGE_VIDEO` (29) | 本地 AVI 列表与播放页 |

### `ui_page_home.c` — 主屏页面（锁屏风格）

设备待机时的默认页面（根页面）：

- **StatusBar**：进入时切 `MINIMAL` 模式（仅时间），`on_leave` 切回 `FULL`（`ui_comp_statusbar_set_mode()`）。
- **时钟**：Montserrat-48 大号字体 `HH:MM`，去重守卫避免冗余刷新。
- **日期与星期**：`M月D日 星期X`，i18n 获取星期名；`SETTINGS` 脏时失效守卫并重排（语言切换）。
- **天气卡片**：温度+描述+高低温（`ui_svc_weather`），无数据时隐藏。
- **闹钟卡片**：仅展示下一个闹钟或隐藏（`ui_page_home_set_alarm()` 由 home 内部据 `ui_svc_tm` 计算设置），不承载页面跳转；时钟功能从应用中心或下拉面板计时卡进入。
- **快捷按钮行**：底部两个圆形磨砂按钮（`ui_comp_btn_circle_create()`）——**相机**（`ui_svc_camera_available()` 时导航到 `UI_PAGE_CAMERA`）与**通话**（导航到 `UI_PAGE_CONTACTS` 先选联系人再进呼叫页；call 特性未编译时置灰禁用）。

手势：右滑 → `UI_PAGE_CHAT`；左滑 → `UI_PAGE_APP_CENTER`；顶部下滑 → `UI_PAGE_PULLDOWN`（由 pulldown 的手势感应区触发）。

### `ui_page_chat.c` — AI 对话页面

展示用户语音识别文本和 AI 回复文本：

- **标题栏**（50px）：中央 AI 图标，左侧对话模式（长按/唤醒/自由等），右侧对话状态（待命/倾听中/思考中/说话中…）。
- **聊天气泡列表**：滚动显示多轮对话（最多 6 条滑动窗口），用户文本右对齐（蓝色 `UI_COLOR_PRIMARY`），AI 文本左对齐（`UI_COLOR_BG_CARD`）。
- **流式追加**：`appendMode` 流式文本，UTF-8 安全截断，每 32 字节节流布局更新。
- **附件预览条**：高 52px，包含 40px 缩略图、"已附加图片"说明和右侧删除按钮；与模式条共用圆角底部面板，无附件时自动折叠。
- **底部模式切换条**：高 36px，底部面板左右留白 4px、底部留白 6px；四个等宽按钮为"闲聊 / 翻译 / 生图 / 侦测"，选中态使用 `UI_COLOR_PRIMARY`，未选中态使用 `UI_COLOR_BG_CARD`。
- **"查看图片"超链接**（生图模式）：`TY_DISPLAY_TP_AI_IMAGE` 携带保存的图片文件名 → 追加一条 `ui_comp_link` 链接气泡；点击经 `ui_svc_picture_view_request()` 异步解码，结果显示在页内**全屏看图 overlay**（点击任意处关闭并释放缓冲）。

通过 `ui_page_chat_on_msg(msg, len, display_tp)` 接收 `tuya_ai_display_stub.c` 转发的业务消息。

手势：右滑 → `ui_route_pop()`。

### `ui_page_pulldown.c` — 下拉面板（快捷控制中心）

从屏幕顶部下滑弹出，创建在 `lv_layer_top()` 上（overlay 页面）。原 9 卡片功能网格已迁移至[应用中心](#ui_page_app_centerc--应用中心)，本页收敛为纯快捷控制：

- **音量滑块**：`ui_comp_slider`（带图标），0~100，通过 `ui_svc_dev_ctrl` 读写；亮度滑块对象保留但当前隐藏。
- **OTA 辅助进度**：默认不占布局；用户从独立升级页返回后，可在此继续查看“正在升级 NN%”。首个数据块到达前显示往返动画，收到完成/失败事件后隐藏；百分比来自 SDK 的 `EVENT_OTA_PROGRESS_NOTIFY`（约 2s 一次，下载段封顶 98%）。百分比写入有缓存去重，语言切换时清除去重键以便重新格式化文案。页面离开时停止动画，重新打开后从 `ui_svc_ota` 缓存补齐状态与百分比。
- **状态快捷卡**：WLAN、倒计时、秒表、番茄钟组成固定 2×2 网格，四张卡统一使用圆角、浅色背景、描边和居中双行布局。WLAN 卡展示连接状态（`ui_svc_wlan` 回调实时刷新），点击 **`ui_route_replace(UI_PAGE_WLAN)`**——用 replace 而非 push，先隐藏 overlay 树再显示常规 WLAN 页，从 WLAN 返回落到面板下层页面。三张计时卡未启动时显示“未运行”，运行时每秒显示当前值，暂停时显示“暂停 + 时间”。倒计时为黄色、秒表为绿色，番茄钟按专注/短休/长休使用红/青/蓝；秒表卡只显示到秒，所有重复文本与样式写入均有缓存去重。
- **直达标签**：点击任一计时卡先通过 `ui_svc_tm_clock_tab_request()` 写入一次性目标。若下拉层下面已经是时钟页，则 `ui_route_pop()` 关闭下拉层，由原时钟实例在 `on_enter` 消费目标并切换标签；否则 `ui_route_replace(UI_PAGE_CLOCK)` 创建时钟页并在 `on_create` 消费目标。这样不会叠加第二个时钟实例，也保留秒表计次等页面临时状态。计时功能未编入时显示不可用 toast，不跳转。
- **底部固定提示**：`▲ 上滑退出`（固定在面板底部，不参与 flex 流）。

关闭方式：面板上滑手势（`lv_indev_wait_release` 吞掉余下触摸 + `lv_async_call` 延迟 dismiss，避免事件漏到下层页面）；WLAN 卡使用 `ui_route_replace()`。计时卡优先复用下方已有时钟页，否则替换下拉层创建时钟页；目标时钟页返回后仍回到原本位于下拉层下面的页面。

`on_leave()` 调用 `ui_svc_dev_ctrl_save()` 持久化。`ui_page_pulldown_init()` 在 `lv_layer_top()` 注册顶部 40px 透明手势感应区（面板打开期间隐藏）；提供 `ui_page_pulldown_is_visible()` 查询。

### `ui_page_app_center.c` — 应用中心

主屏左滑进入的应用启动器，复用下拉面板卡片的观感（图标在上、文字在下的卡片网格；顶部为全局状态栏 + 留白，无标题栏）：

- **10 张卡片**：相册、视频、录音、音乐、时钟、日程、侦测、文件、诊断、设置。卡片表（`s_card_defs[]`）每项带 `ui_feature_id_t` 功能簇标记，功能不可用时点击只提示不可用（`UI_FEATURE_ID_NONE` = 核心项永远可用，如诊断/设置）。
- **导航**：点卡片 `ui_route_push(target)`——从目标页返回落回应用中心；卡片文案每次 `on_create` 重新经 i18n 解析。

手势：右滑 → 返回主屏。

### `ui_page_settings.c` — 设置页面

从应用中心"设置"卡片进入：

- **标题栏**（48px）：左侧返回按钮、中央标题。
- **语言行**：标签 + 分段控件（中文/English），`UI_COMP_BTN_TEXT` 样式；选中段填充 `UI_COLOR_PRIMARY`。通过 `ui_svc_dev_ctrl_language_set()` 切换，`SETTINGS` 脏时刷新全部文案和分段高亮。
- **模式选择已移至聊天页底部**，设置页不再显示模式行。
- **NFC 开关行**（`ENABLE_TUYA_NFC` 编入时）：单向持久化控制——开=初始化 PN532，关不做去初始化；开关值由 dev-ctrl 启动时恢复。
- **P2P 开关行**（call 功能簇编入时）：`ui_svc_call_enabled_get/set`，控制 P2P 通话栈；联系人页据此隐藏/显示列表。
- **AI 相机开关已移除**；相机固定为普通拍照，AI 操作统一从相册页发起。
- **来电自动接通行已隐藏**：产品默认不自动接通，来电一律以响铃 UI 呈现由用户手动接听。开关行创建与 `ui_svc_dev_ctrl_load()` 中的 KV 恢复统一由编译宏 `ENABLE_UI_CALL_AUTO_ANSWER`（定义在 `ui_svc_call.h`，默认 0）包裹——同关避免老固件持久化的"开"在无开关可关的情况下继续生效，置 1 即可整体恢复开关及其持久化。
- **关于行**：导航行，点击 → `UI_PAGE_ABOUT`。
- **重置行**：点击 → 确认弹窗 → `lv_async_call` → 加载弹窗 → `ui_svc_dev_ctrl_reset()` 解绑。

导航行由 `nav_row_create()` 构建：宽度 100%、高度 56、`BG_CARD` 背景、`MD(8)` 圆角、按压透明度 80%、标签 + 可选值 + ">" 箭头。

`on_leave()` 调用 `ui_svc_dev_ctrl_save()`。

### `ui_page_about.c` — 关于页面

从设置页"关于"行导航进入：

- **标题栏**（48px）：左侧返回按钮、中央标题。
- **信息行列表**（`info_row_create()`）：固件版本、SDK 信息、设备 ID，数据来自 `ui_svc_devinfo_*()`。
- **系统升级行**：位于固件版本下方，右侧实时显示“检查更新 / 正在检查 / 发现新版本 / 已是最新 / 检查失败”，真实 OTA 开始后优先显示“正在升级 NN% / 正在安装 / 升级失败”；点击进入 `UI_PAGE_OTA`。

每行：宽度 100%、最小高度 48、`BG_CARD` 背景；左侧标签 + 右侧值（`TEXT_SEC` 色，flex_grow，自动换行）。

### `ui_page_ota.c` — 系统升级页面

从“设置 → 关于 → 系统升级”进入；检测到 OTA 后也会自动打开：

- **待检查/检查中**：显示当前固件版本和居中的紧凑圆角描边“检查更新”按钮；点击后异步调用 `tuya.device.screen.ota.check`（入参 `{"lang":"zh-Hans"}`），页面隐藏按钮并用“正在检查更新”配合轻量环形加载指示，避免重复点击。
- **发现新版本**：解析主模块（`type == 0`）的 `upgradeStatus/currentVersion/version/fileSize/desc`，展示目标版本、版本信息卡和更新说明卡；底部同一行显示紧凑等高的“重新检查 / 确认升级”按钮。点击确认后异步调用 `tuya.device.screen.ota.confirm`（入参 `{"channel":0}`），请求与响应原文写入日志，按钮暂时禁用并等待 OTA 事件接管页面。
- **已是最新/检查失败**：`upgradeStatus == 0` 时明确显示已是最新；请求失败、返回缺少主模块或关键字段异常时显示失败状态和重试入口。
- **准备/下载态**：进度区域固定放在操作按钮行下方，并显示进度条与紧凑百分比；确认升级和未收到首个真实进度时静态显示空进度条与 `0%`，收到进度后同步更新填充和百分比。此阶段允许返回，随后可从下拉面板查看辅助进度。
- **校验/安装态**：SDK 下载进度达到 98% 后自动重新打开本页，隐藏返回按钮并禁用右滑返回，同时关闭顶部下拉手势，防止安装阶段离开。
- **安装/失败态**：固件包校验完成后显示满进度条与“正在安装更新 / 即将自动重启”，不提前宣称升级完成；失败时恢复返回与重试入口。

页面通过 `ui_svc_ota` 的前台单槽回调刷新，`on_leave()` 停止检查动画并清回调；所有 OTA 业务 API 都隔离在服务层。

### `ui_page_mode.c` — 设备模式选择（保留页）

页面路由仍保留以兼容旧入口，当前设置页已不再导航到此页：

- **标题栏**（48px）：左侧返回按钮、中央标题。
- **模式行**：模式表共 6 项（索引与 `AI_DEVICE_MODE_E` 对齐：闲聊/翻译/P2P/录音/生图/侦测），其中 **P2P、录音 `visible=false` 隐藏**，实际仅显示 4 项（闲聊、翻译、生图、侦测）。

选中：背景 `UI_COLOR_PRIMARY`；未选中：背景 `UI_COLOR_BG_CARD`。点击调用 `ui_svc_dev_ctrl_mode_set(idx)` 后 `ui_route_reset(UI_PAGE_CHAT)`（清空路由栈、切到对话页）。`CHAT` 脏时刷新高亮。`on_leave()` 调用 `ui_svc_dev_ctrl_save()`。

### `ui_page_call.c` — 通话页面

从联系人页选定对象（App / 设备）拨号后进入；来电振铃/接通时也由 `ui_svc_call` 直接推入本页（无论当前在哪个页面）。业务通过 `ui_svc_call`（页面不含任何 P2P/IPC 业务头文件）：

- **状态机**（`ui_call_state_t`，权威值在服务层，页面持本地副本）：`IDLE` → `CALLING`（去电拨号）/ `INCOMING`（来电响铃）→ `IN_CALL` → 终态 `FAILED` / `ENDED` / `ENDED_BY_OTHERS`（均 toast 提示后回 IDLE）。
- **两个圆形操作按钮按状态复用**：绿色 = 呼叫（空闲）/ 接听（响铃中）；红色 = 挂断（通话/拨号中）/ 拒接（响铃中）。颜色创建时固定，状态只切换文案和使能。
- **状态行/通话时长**：固定位置（不在 flex 流中，避免文字变化推动按钮）；`IN_CALL` 时显示 `MM:SS` 时长时钟，由**秒级 SYSTEM tick** 驱动（`on_enter(SYSTEM)` 中刷新）。
- **`on_leave` 收尾**：离开页面即结束通话——响铃中的来电拒接（回busy），活动/拨号中的呼叫挂断，避免"无界面通话"。
- 服务回调由 `ui_svc_call` 封送到 UI 线程后调用，可直接操作 LVGL；`on_create` 末尾注册（确保树已建好），`on_destroy` 清除。

手势：右滑 → 返回（`on_leave` 自动挂断）。

### `ui_page_contacts.c` — 联系人页

主屏"通话"按钮进入，底部选项卡布局（`ui_comp_tabview`）双 tab，业务仅经 `ui_svc_call` / `ui_svc_nfc`（RULES.md §6/§8）：

- **联系人 tab**：App 行（设备→App 呼叫）+ 设备联系人列表（`ui_svc_call_contacts_async()` 异步拉取）。点击条目设置 peer 名称并 `dial()` / `dial_device()`，随后 push `UI_PAGE_CALL`；支持删除联系人（异步）。**P2P 开关关闭时**，含 App 行在内全部隐藏，列表只显示开关提示。
- **发现 tab**：NFC 读写——由 `ui_svc_nfc` 异步执行（`read_async` / `write_uuid_async`），结果回 UI 线程后以满屏浮层展示；回调里先校验当前页仍是 CONTACTS 再动 UI。

手势：右滑 → 返回主屏。

### `ui_page_photo.c` — 相册页面（Viewer + Grid）

从应用中心"相册"卡片进入。**单路由页 + 双内部布局**（ADR-0001）：全屏大图 Viewer 和缩略图 Grid 两棵子树同时创建，由页内 `s_mode` 切换显示。数据全部经 `ui_svc_picture`（页面无业务/平台头文件）。

**Viewer（默认）**：
- 全屏黑底大图（`ui_comp_picture`），打开时定位到最新一张。
- **左右滑切换图片**：索引 + 递增 `seq` 的 **latest-wins** 机制——快速滑动时过期的解码帧直接释放丢弃，绝不渲染陈旧帧（ADR-0002）。
- overlay 四角按钮：返回（pop）、**AI**、删除（确认弹窗）、"全部图片"（切到 Grid）。点击 AI 后弹出两个操作按钮：
  - **AI识图**：若当前不是闲聊模式则先切到闲聊模式，立即上传当前图片和固定文本“请解释刚刚上传的图片内容，请勿触发 MCP 技能。”，随后进入聊天页等待云端结果。
  - **图生图**：暂存当前图片的附件预览并排入图片输入队列，若当前不是生图模式则先切到生图模式，随后进入聊天页；聊天页显示附件框，供下一轮图生图输入使用。

**Grid**：
- 3 列缩略图（最多 `UI_PICTURE_MAX_THUMBS`=30），缩略图缓冲**借用**服务内部列表（零拷贝，free_fn=NULL）。
- 顶栏"选择"进入多选模式：每格左上角圆形勾选框、底栏显示已选数 + 删除按钮（确认后批量删除并重建网格）。
- 右滑 / 返回按钮 → 回到 Viewer。

注意：**本页是"右滑=返回"全局约定的例外**——Viewer 中右滑是"上一张"，返回靠 overlay 的返回按钮（见 RULES.md §3.1 例外条款）。

`on_destroy`：清空全部服务回调（迟到的异步结果变 no-op）→ `ui_svc_picture_close()` 释放会话。

### `ui_page_video.c` — 本地视频页

从应用中心“视频”卡片进入，单个路由页内部切换录像列表与播放器：

- 列表只经 `ui_svc_video_playback` 请求 `UI_FS_VIDEO` 中的 `.avi` 文件，按时间戳文件名倒序显示，并把标准录像文件名转换为本地月日/时间；卡片显示格式与文件大小，不在 UI 线程扫描目录或生成缩略图。
- 顶栏“选择”进入多选模式，支持逐项选择、全选/全不选和底栏批量删除。确认后服务复制文件名并在 `WORKQ_SYSTEM` 串行删除 AVI 及其 `.avi.idx` 伴生索引，再回传 AVI 删除的成功/失败数量并重建列表；播放器尚未完全关闭时选择入口保持禁用。
- 点选录像后异步打开；沉浸式播放态隐藏全局状态栏，并把 48px 标题栏独立放在 320×320 播放框上方，不再覆盖视频画面。播放器把 MJPEG 解码为最大 320×320 的 RGB565 帧，页面通过 `ui_comp_picture` 接管并释放缓冲；仅 16kHz/16-bit/单声道 PCM 音轨经共享音频输出同步播放，不兼容音轨降级为无声视频。
- 播放区提供暂停/继续、停止、重播、只读进度和分列时长。返回或右滑先停止并回到列表；选择模式下则优先取消选择，再次返回才退出页面。
- `on_leave` / `on_destroy` 先清空画布和服务回调，再异步停止播放，迟到帧会由服务释放。

### `ui_page_music.c` — 音乐播放页面

从应用中心"音乐"卡片进入，业务经 `ui_svc_music`：

- **打开即播**：`on_create` 调 `ui_svc_music_autoplay()`（播放中 no-op / 暂停则恢复 / 停止则续播或播列表第一首 / 空列表云端默认推荐）。仅在 `on_create`：从覆盖页（歌单/下拉）返回不强制播放，尊重用户的主动暂停。
- **歌名/歌手**：`LV_LABEL_LONG_DOT` 截断（音乐页**禁用跑马灯**，连续重绘会饿死音频线程）；设文本前先 `lv_obj_update_layout()` 解析百分比宽度（LONG_DOT 按当前宽度烘焙省略号）。
- **唱片动画**：用小 marker **绕盘公转**代替盘面 `transform_angle` 旋转（后者每帧分配中间层 + 软件旋转，会饿死音频）；播放时启动、暂停/停止时删除；**`on_leave` 必须停止动画**（被歌单页/下拉页覆盖时 LVGL 树仍存活，动画会以 ~33fps 在不可见区域持续重绘）。
- **进度条**：由**秒级 SYSTEM tick** 驱动（`on_enter(SYSTEM)` → `ui_svc_music_progress_percent()`）；缓存上次渲染值，**百分比没变不写 LVGL**（避免每秒无效 invalidate）；长度未知（流媒体）时变暗显示。
- **控制行**：上一首/播放暂停/下一首/列表（→ `UI_PAGE_MUSIC_LIST`）；**播放模式**文字按钮循环切换 顺序/列表循环/单曲循环/随机。
- **服务回调单槽**：`on_create`/`on_enter` 注册，`on_leave` 清除；从覆盖页返回时 `on_enter` 重注册并用当前状态做一次全量同步（补上被覆盖期间错过的变化）。

手势：右滑 → 返回。

### `ui_page_music_list.c` — 播放列表页面

从音乐页"列表"按钮进入：

- **行列表**：每行歌名+歌手（`LONG_DOT`）+ 播放状态图标 + 删除按钮；最多 `MLIST_MAX_ROWS`=128 行（镜像后端播放列表硬上限，超出部分不创建）。**进入即渲染**：`on_create` 只建框架并显示"加载中"占位，数据经 `ui_svc_music_list_async()` 在 WORKQ 取回后再建行（播放列表读取要拿播控互斥锁，后端 URL 刷新期间可达秒级，不能在 UI 线程同步等）；已有行时刷新不闪烁（旧行保留到新数据到达）。
- **增量更新**：行注册表（id → 行对象），当前曲目高亮移动只**重设两行样式**，不重建整表（整表 250+ 对象的销毁/重建会卡音频）。仅当当前 id 在注册表中找不到（列表本身变化，如云端推送）才整表重建；重建后仍找不到则**闩锁该 id**，避免每个播放器事件都触发重建。
- **点行播放**：`ui_svc_music_play_id()`（异步，首播需 MQTT 刷新 URL 可达秒级）；先行内高亮即时反馈，真实播放状态由事件回调落定。
- **删除行**：`lv_async_call` 延迟重建——在按钮自己的点击回调里同步重建会释放 LVGL 正在派发事件的对象。
- 服务回调单槽模式同音乐页（`on_leave` 清除、`on_enter` 重注册+重建追平）。

手势：右滑 → 返回。

### `ui_page_clock.c` — 时钟页

从应用中心“时钟”卡片进入，或由下拉面板的倒计时/秒表/番茄钟状态卡直达对应标签；数据经 `ui_svc_tm`：

- **四 tab 分段**（`ui_comp_tabview`，底部选项卡）：闹钟（`TAB_ALARM`）、倒计时（`TAB_COUNTDOWN`）、秒表（`TAB_STOPWATCH`）、番茄钟（`TAB_POMODORO`）。
- **闹钟**：使用深色圆角卡片展示，启用项以黄色强调、停用项降低为灰色层级，空列表使用简洁纯文本空态；一次性闹钟副标题保持“日期 仅一次”的空格分隔。页面持有自己的 id 表（避免 cJSON UAF），保留开关启停与长按删除（确认弹窗）行为。
- **倒计时**：空闲态先显示 1/5/10/30/45/60 分钟快捷入口，也可进入自定义的时/分/秒三列 roller；自定义页使用“取消 + 居中标题”的轻量导航，运行与暂停态显示剩余时间、环形进度及纯文本操作按钮。倒计时状态由**秒级 SYSTEM tick** 驱动，去重后刷新。
- **秒表**：按未开始、运行、暂停三态显示大号时间和纯文本圆形操作按钮；运行时可“计次”，页面保留并按最新优先展示最多 4 条记录，暂停后可取消复位或继续。后端使用单调毫秒时钟累计，前台运行时通过受页面生命周期约束的 LVGL 动画刷新百分秒；仅固定宽度的百分秒标签高频重绘，主时间保持独立固定布局，分隔点由图形绘制，不依赖数字字体字形；切换 tab、暂停或离开页面即停止动画。
- **番茄钟**：空闲态以专注/短休息/长休息三张时长卡展示配置，点击卡片可用单列分钟 roller 调整时长；编辑页与倒计时统一使用“取消 + 居中标题”的轻量导航；运行态按阶段切换红/青/蓝色倒计时环，以纯文本数字展示番茄周期，并提供“取消/暂停/继续”等纯文本操作。默认配置 `{25, 5, 15, 4}`（专注/短休/长休分钟 + 长休前专注轮数），运行中的实际配置可由 MCP/MQTT 下发覆盖；运行态由秒级 SYSTEM tick 去重刷新。

手势：右滑 → 返回。

### `ui_page_schedule.c` — 日程提醒页

从应用中心"日程"卡片进入，数据经 `ui_svc_tm`：日程提醒列表 + 删除（确认弹窗），页面持有自己的 id 表避免 cJSON UAF。

手势：右滑 → 返回。

### `ui_page_camera.c` — 相机页

从主屏"相机"快捷按钮进入，业务经 `ui_svc_camera`（页面无业务/平台头文件）：

- **三个核心对象**：全屏预览画布（`ui_comp_picture`）、左下角相册缩略图（点击 → `UI_PAGE_PHOTO`），以及底部固定位置的模式选择与主快门。`UI_FEATURE_VIDEO` 开启时可在“拍照 / 录像”分段控件中切换；中央快门随模式显示白色拍照圆点、红色录像圆点或录像中的红色停止方块，录像期间锁定模式切换，并在标题栏右侧显示红点与 `MM:SS` 录像时长。
- 实时 YUV422→RGB565 预览、JPEG 拍照保存、JPEG→RGB565 缩略图全部在服务侧完成，经回调把帧/缩略图封送 UI 线程交给本页刷新；RGB565 缓冲所有权转移给页面。
- 快门固定执行普通拍照并保存到相册，不上传图片，也不触发 AI 模式或页面跳转。
- `UI_FEATURE_VIDEO` 开启时，录像模式下的主快门经 `ui_svc_video` 在 `WORKQ_SYSTEM` 异步启停 MJPEG AVI；视频来自共享 `VIDEO_CONSUMER_AVI_RECORD`，麦克风可用时附带 PCM 音轨，文件保存到 `UI_FS_VIDEO`。录像目标为 15fps，存储拥塞时快速降帧；检测到连续秒级写入卡顿或 I/O 失败会自动安全停止并提示存储错误。录像中禁止拍照，离开页面自动停止并封装文件，单段当前限制 110 秒。
- **流生命周期对称**：预览在 `on_enter` 启动、`on_leave` 停止——push 相册页即暂停相机，pop 回来即恢复，无泄漏。

### `ui_page_recording.c` — 录音页

从应用中心"录音"卡片进入，业务经 `ui_svc_recording`：录制/停止（无暂停态）、录制计时（秒级 tick 驱动）、右上角"列表"按钮 → `UI_PAGE_RECORDING_LIST`。

手势：右滑 → 返回。

### `ui_page_recording_list.c` — 录音列表页

从录音页"列表"按钮进入：

- **行列表**（最多 `RLIST_MAX_ROWS`=20）：每条录音 + 内嵌播放卡（独立配色，进度条）。
- 行内"AI"图标 → `UI_PAGE_RECORDING_TRANSCRIBE`（先设置目标录音再 push）。
- 标题栏右侧显示录音总条数。

手势：右滑 → 返回。

### `ui_page_recording_transcribe.c` — 录音转写页

从录音列表页"AI"图标进入，业务经 `ui_svc_transcribe`（数据所有权归 `ui_svc_recording`）：

- **动作按钮**：上传并转写 / 重试；上传进度条 + 处理中转圈 + 状态文案（转写中/失败/无法转写/暂无内容）。
- **阅读卡**：转写 / 总结两 tab 切换，可滚动文本（单次读入上限 `TR_READ_MAX`=16KB，UTF-8 安全截断）。

手势：右滑 → 返回。

### `ui_page_files.c` — 文件浏览页

从应用中心"文件"卡片进入，业务经 `ui_svc_fs`：

- **逐级遍历 `UI_FS_MOUNT`（整张卡，含 tuyaos/ 旁的用户文件）**：目录可下钻，普通文件点击弹只读信息弹窗。
- 目录列表在 UI 线程外经 `ui_fs_list_async` 取回，结果按 `seq` latest-wins 落地。
- 自包含（行内建：共享 `ui_comp_list` 无法渲染 `LV_SYMBOL` 图标和每行 user_data）。

手势：右滑 → 返回。

### `ui_page_detection.c` — 侦测记录页

从应用中心"侦测"卡片进入，业务经 `ui_svc_detection`：

- **云端 AI 侦测告警只读浏览**（`thing.ipc.ai.robot.msg.list`）：标题栏 `[返回] 侦测记录 [一键总结]`，记录卡列表（标题 + 时间），底部分页栏 `[上一页] cur/total [下一页]`。
- 阻塞式 HTTP 拉取在 `WORKQ_SYSTEM`，结果封送 UI 线程后刷新。

手势：右滑 → 返回。

### `ui_page_audio_diag.c` — 音频诊断页

从诊断页"音频诊断"行进入，业务经 `ui_svc_audio_diag`：

- **audio_dump 通道单选**：关闭 / UART / 局域网 / SD 卡（对应 `AUDIO_DUMP_CHANNEL_E` 0~3），不可用通道灰显。
- **apply-on-leave**：点击行只更新待定选择 + 高亮，重的后端切换（SD 需 PSRAM 分配、teardown flush）在 `on_leave` 一次性应用，避免中间点击反复 alloc/free。运行时值，不持久化。

手势：右滑 → 返回。

### `ui_page_diag.c` — 诊断页

从应用中心"诊断"卡片进入，收纳面向开发/测试的诊断项（原设置页的两行迁移至此，后续新增测试项也加在这里）：

- **系统状态行**：导航行，点击 → `UI_PAGE_SYS_STATUS`。
- **网络诊断行**：导航行，点击 → `UI_PAGE_NET_STATUS`。
- **屏幕测试行**：导航行，点击 → `UI_PAGE_SCREEN_TEST`。
- **显示帧率开关行**：标签 + `lv_switch`，切换端口层 FPS/CPU overlay（`ui_svc_dev_ctrl_fps_overlay_*`，内存态不持久化）。
- **播放统计开关行**：标签 + `lv_switch`，切换音频播放器 AP-STAT 运行时开关（`ui_svc_dev_ctrl_ap_stat_*`，内存态不持久化、默认关）；仅当统计代码编入（`AI_PLAYER_DEBUG_STATS=y`）时显示。
- **音频诊断行**：导航行，点击 → `UI_PAGE_AUDIO_DIAG`（仅当 `ui_svc_audio_diag_available()` 时显示）。

手势：右滑 → 返回。

### `ui_page_sys_status.c` — 系统状态页

从诊断页"系统状态"行进入，About 页同款只读信息行，数据经 `ui_svc_sysmon`：

- **SRAM / PSRAM 剩余**：实时内存水位（KB/MB 自适应格式化；PSRAM 行仅在有 PSRAM 的板型上创建）。
- **运行时长**：`Xd HH:MM:SS`。
- **上次复位原因**：`TUYA_RESET_REASON_E` 数字码，开机常量，只在 `on_create` 画一次。
- **刷新机制**：复用 `ui_tick` 的秒级 SYSTEM 脏位（`on_enter` 收到后重读），不建页面级 `lv_timer`；内存值带去重守卫，未变化不重写 label。

手势：右滑 → 返回。

### `ui_page_net_status.c` — 网络诊断页

从诊断页"网络诊断"行进入，只读信息行，数据经 `ui_svc_netmon`：

- **WiFi 状态 / SSID / 信号强度（dBm）/ IP 地址 / MAC 地址**：来自 TAL WiFi（非 WiFi 构建显示未连接/`--`）。
- **云连接**：在线/离线，读 `ui_state` 的 `is_online`（与状态栏 WiFi 图标同源）。
- **刷新机制**：同系统状态页走秒级 SYSTEM 脏位；所有值经 changed-guard（`value_set_if_changed()`），逐秒真正变化的只有 RSSI。WiFi/云两行的**值**也是 i18n 文案，语言切换时随 `refresh_stats()` 一并重解析。

手势：右滑 → 返回。

### `ui_page_screen_test.c` — 屏幕测试页

从诊断页"屏幕测试"行进入，全屏沉浸式 LCD/触摸测试（隐藏全局状态栏，`ui_app` 同时禁用下拉手势感应区，与 WLAN/相机同款处理）：

- **测试序列**：点击逐屏切换——白/黑/红/绿/蓝/50%灰纯色（坏点/漏光；RGB 三屏可一眼暴露 RGB565 字节序类故障）→ 8 竖条彩条 → 触摸画线。
- **触摸画线**：按压轨迹用 `lv_line` 渲染（静态点池 8 笔 × 256 点，笔满复用最旧一笔；不用全屏 canvas，避免几百 KB 缓冲），左上「清屏」、右上「退出」按钮。
- **退出**：纯色/彩条屏右滑返回；画线屏吞掉手势（与画线冲突），只能按「退出」。
- 进入时 toast 提示操作方式；测试图案无语言相关内容，`on_enter` 无需刷新。

### `ui_page_wlan.c` — WiFi 连接页

两个入口：下拉面板 WLAN 快捷入口（`ui_route_replace` 进入）与首启激活向导"手动激活"（push 进入）。业务经 `ui_svc_wlan`：

- **扫描列表**：`ui_svc_wlan_refresh()` 异步扫描，结果回调渲染；标题栏带手动刷新按钮。
- **连接面板**：点击 AP 弹出——SSID + 密码输入框（`lv_textarea` password 模式，可切换明文/密文）+ `ui_comp_keyboard` 四行英文软键盘；`ui_svc_wlan_connect()` 发起连接，连接状态变化经回调驱动 UI（成功后 `ui_route_pop()` 返回来源页，激活向导路径由向导 `on_enter` 接续二维码步骤）。
- **跳过按钮**：仅首启未激活时显示，`push(UI_PAGE_HOME)` 利用根页 unwind 语义收起向导。

手势：右滑 → 连接面板打开时先收起面板，否则返回。

### `ui_page_activation.c` — 首启激活向导

开机时若设备未激活（`ui_svc_activate_is_activated()` 为假），`ui_app_wukong_init()` 在 `push(HOME)` 之后 push 单页向导 `UI_PAGE_ACTIVATION`。语言选择、激活方式、扫码激活是同一页面内的三个 page-local step：三个容器在 `on_create` 一次创建，切换时只改 hidden 状态、标题和进度点，不反复销毁 LVGL 子树。返回键/右滑在步骤内逐级后退，第一步再退回 HOME；标题栏「跳过」始终 `push(HOME)`，利用根页 unwind 语义收起向导。

- **语言选择**：中/英单选列表（行文案用 `UI_TEXT_LANG_ZH/EN`，两张 i18n 表内均为原生名），点击即 `ui_svc_dev_ctrl_language_set()` 生效；「继续」先 `ui_svc_dev_ctrl_save()`，再切换到激活方式步骤。
- **激活方式**：两项与语言选择共用“整行描边 + 单选圆点 + 底部继续”的交互语义，默认选中「APP 激活」，点击只切换选中态，点击「继续」后再执行流程。「APP 激活」回 HOME 等待 APP（AP+BLE 并发配网开机即在跑）；「手动激活」若 WLAN 已连接则直接进入二维码步骤，否则记录 page-local pending 状态并 push WLAN。WLAN 连接成功后 `ui_route_pop()` 返回仍在栈中的向导，向导 `on_enter` 读取 `ui_svc_wlan_get()->connected` 决定是否进入二维码步骤；取消 WLAN 则保持在激活方式步骤。
- **扫码激活**：白底卡片内 `lv_qrcode`（184px，`LV_USE_QRCODE` 在 `port/lv_conf.h` 开启）展示云端激活短链；短链未就绪时保留占位卡片。数据与事件经 `ui_svc_activate` 单槽回调，只有二维码步骤前台时注册，`on_leave`/离开该步骤即清除；激活完成锁存只由 `EVENT_POST_ACTIVATE` 置位（扫码直连阶段的 `EVENT_MQTT_CONNECTED` 不算完成），收到对应 `EVT_ACTIVATED` 后 toast「激活成功」并回 HOME。

### 页面导航关系

```
HOME（根页面，不可弹出）
 ├─ 右滑 → CHAT ── 右滑返回
 │          ├─ 附件预览条（相册 AI 识图交接）
 │          └─ "查看图片"链接 → 页内全屏看图 overlay
 ├─ 相机快捷按钮 → CAMERA ── 缩略图 → PHOTO
 ├─ 通话快捷按钮 → CONTACTS（联系人/发现双 tab）── 选联系人拨号 → CALL ── 右滑/返回（on_leave 自动挂断/拒接）
 ├─ 闹钟卡片（仅展示下一闹钟，不跳转）
 ├─ 顶部下滑 → PULLDOWN（overlay，上滑退出）
 │              ├─ 音量滑块（亮度滑块保留但隐藏）
 │              ├─ WLAN 快捷卡 ──replace→ WLAN ── 右滑返回
 │              └─ 倒计时/秒表/番茄钟状态卡 ──replace→ CLOCK（直达对应 tab）
 └─ 左滑 → APP_CENTER（9 张卡片，按功能簇裁剪；右滑返回）
                ├─ 相册卡片   → PHOTO（viewer ⇄ grid 页内切换）
                │                └─ AI 识图按钮 → CHAT（带附件）
                ├─ 录音卡片   → RECORDING ── 列表按钮 → RECORDING_LIST
                │                                          └─ AI 图标 → RECORDING_TRANSCRIBE
                ├─ 音乐卡片   → MUSIC ── 列表按钮 → MUSIC_LIST ── 右滑返回
                ├─ 时钟卡片   → CLOCK（闹钟/倒计时/秒表/番茄钟四 tab）
                ├─ 日程卡片   → SCHEDULE
                ├─ 侦测卡片   → DETECTION
                ├─ 文件卡片   → FILES
                ├─ 诊断卡片   → DIAG
                │              ├─ 系统状态行 → SYS_STATUS ── 返回
                │              ├─ 网络诊断行 → NET_STATUS ── 返回
                │              ├─ 屏幕测试行 → SCREEN_TEST（沉浸）── 右滑/退出按钮
                │              └─ 音频诊断行 → AUDIO_DIAG（条件）── 返回
                └─ 设置卡片   → SETTINGS
                               ├─ 关于行     → ABOUT ── 系统升级行 → OTA
                               └─ 重置行     → 确认弹窗 → 加载弹窗 → 解绑重启

业务侧：对话从空闲转活跃且当前在 HOME 时，自动 push CHAT（tuya_ai_display_stub.c）
来电：振铃/接通时 ui_svc_call 若发现当前不在 CALL 页则直接 push CALL

首启激活向导（开机且未激活时叠加在 HOME 之上）：
 HOME → ACTIVATION（页内：语言选择 → 激活方式）
                    ├─ APP 激活  → HOME（等待 APP 配网）
                    └─ 手动激活  → WLAN ──连接成功──pop→ ACTIVATION（二维码步骤）──EVENT_POST_ACTIVATE→ HOME
```

---

## 三、可复用组件库 — `widgets/`

每个组件以 `ui_comp_<name>.c/h` 独立封装，对外只暴露工厂函数和属性更新函数，**不暴露内部 LVGL 对象结构**。组件由 page 在 `on_create` 中创建，在 `on_enter` 中通过属性函数更新，不持有业务状态。

### `ui_comp_statusbar` — 状态栏

屏幕顶部全局状态条（`lv_layer_top()` 单例）：时间、日历/闹钟提示、WiFi 信号、电池（含充电动画）。电池图标仅在设备支持电量检测（`ui_feature_available(UI_FEATURE_ID_BATTERY)`，对应 `ENABLE_BATTERY` 配置）时创建；不支持时不显示，WiFi 图标靠右补位。WiFi 图标仅在设备联网（`ui_state` 的 `is_online`）时显示，未联网时隐藏。

| 接口 | 说明 |
|---|---|
| `ui_comp_statusbar_init()` | 创建单例（`ui_app_init` 调用） |
| `ui_comp_statusbar_refresh(props)` | 由 `ui_tick.c` 在 `SYSTEM` 变脏时调用 |
| `ui_comp_statusbar_set_mode(mode)` | `FULL`（完整信息）/ `MINIMAL`（仅时间，主屏使用） |
| `ui_comp_statusbar_set_visible(visible)` | 整条显隐 |

### `ui_comp_btn` — 按钮

五种样式（`ui_comp_btn_style_t`）：

| 样式 | 外观 |
|---|---|
| `UI_COMP_BTN_PRIMARY` | 蓝色填充（主操作） |
| `UI_COMP_BTN_SECONDARY` | 透明背景+蓝色边框（次操作） |
| `UI_COMP_BTN_TEXT` | 纯文字（无背景/边框） |
| `UI_COMP_BTN_ICON` | 纯图标 |
| `UI_COMP_BTN_CIRCLE` | 圆形磨砂图标按钮（锁屏风格） |

主要接口：
- `ui_comp_btn_create(parent, text, cb)` — 主样式按钮。
- `ui_comp_btn_create_styled(parent, text, style, cb)` — 指定样式。
- `ui_comp_btn_icon_create(parent, icon_src, cb)` — 纯图标按钮。
- `ui_comp_btn_circle_create(parent, icon_src, text, cb)` — 圆形磨砂按钮（text=NULL 时仅图标）。
- `ui_comp_btn_create_with_icon(parent, icon_src, text, style, pos, cb)` — 图标+文字按钮（pos=LEFT/RIGHT/TOP）。
- `ui_comp_btn_set_text(btn, text)` — 更新按钮文案（i18n 刷新/状态复用按钮）。
- `ui_comp_btn_set_enabled(btn, enabled)` — 启用/禁用。

### `ui_comp_label` — 文本标签

三种文字级别：`TITLE`（粗体）、`BODY`（正文）、`CAPTION`（灰色小字）。支持字符串、格式化字符串和 i18n key 三种文案来源。

### `ui_comp_card` — 卡片容器

带圆角、深色背景和内边距的卡片。支持空白卡片和带标题卡片。

### `ui_comp_list` — 列表

纵向列表：主文字 + 可选副文字 + 可选图标 + 点击回调。支持 `set_items` 刷新和 `clear` 清空。

### `ui_comp_navbar` — 底部导航栏

固定底部、图标+文字标签，点击调用 `ui_route_push()` 跳转。支持 `set_active` 高亮。

### `ui_comp_bar` — 进度条

`lv_bar` 封装，应用 `ui_style_bar` 样式。支持立即更新和动画更新。

### `ui_comp_slider` — 滑块

带可选左侧图标的水平滑块（隐藏旋钮、圆角轨道），下拉面板音量/亮度使用。`ui_comp_slider_create(parent, icon_src, value)` 创建，`ui_comp_slider_set_value()` 无动画更新。

### `ui_comp_icon` — 图标

`lv_img` 最小封装，统一管理图标资源引用。

### `ui_comp_link` — 超链接标签

带颜色+下划线的可点击文字（如对话页"查看图片"）。`ui_comp_link_create(parent, text, cb, arg, arg_len)`：`arg` 被**拷贝**进 widget（LVGL 堆），随 widget 删除自动释放——调用方可随意清除父节点不泄漏；每次点击把拷贝传回 `cb`。

### `ui_comp_picture` — RGB565 图片画布

**纯渲染器**：在父容器中央按原始尺寸绘制一块 RGB565 缓冲（不做 JPEG 解码、不依赖任何业务/平台头文件——解码在 `ui_svc_picture`）。缓冲所有权由 `set_rgb565` 的 `free_fn` 决定：

- `free_fn == NULL`：widget 只引用，调用方保留所有权（如借用服务缩略图列表，零拷贝）。
- `free_fn != NULL`：所有权转移给 widget，替换/清除/删除时自动调用 `free_fn(data)` 释放（如大图传 `ui_svc_picture_free_rgb565`）。

`ui_comp_picture_clear()` 释放持有的缓冲并显示空占位。

### `ui_comp_ring_alarm` — 全局响铃 overlay

闹钟/提醒/倒计时到点时弹出的全局响铃覆盖层（仅 UI 线程调用）。`ui_ring_kind_t` 三种：`ALARM`（显示 Snooze + Stop）、`REMINDER`（Stop）、`COUNTDOWN`（Confirm/确认）。`ui_comp_ring_alarm_show(kind, id, message, on_stop)`：用户点击主操作时在 overlay 关闭**前**回调 `on_stop`，由调用方接到对应业务动作（如确认闹钟）；`ui_comp_ring_alarm_dismiss()` 手动关闭。

### `ui_comp_popup` — 弹窗

全局单例，四种类型：

| 类型 | 说明 |
|---|---|
| `UI_COMP_POPUP_INFO` | 信息提示（仅"确定"） |
| `UI_COMP_POPUP_CONFIRM` | 确认对话框（"确定"+"取消"） |
| `UI_COMP_POPUP_LOADING` | 加载中（旋转动画，无按钮） |
| `UI_COMP_POPUP_TOAST` | 短暂提示（自动消失） |

`ui_comp_popup_show(type, title, msg, cb)` 显示；`ui_comp_popup_toast(msg, duration_ms)` 显示 Toast；`ui_comp_popup_dismiss()` 手动关闭。

### `ui_comp_keyboard` — 英文软键盘

紧凑四行英文键盘（`1234567890` / `qwertyuiop` / `asdfghjkl` / 大小写切换 + `zxcvbnm` + 删除），绑定到目标 `lv_textarea` 后按键直接写入；支持初始大小写属性。WLAN 页密码输入使用。

### `ui_comp_tabview` — 统一风格选项卡

对原生 `lv_tabview` 包一层创建参数（tab 位置等）的统一样式封装；返回的就是原生 tabview 对象，增删/改名/查询/切换 tab 仍用 `lv_tabview_*` API。时钟页（底部四 tab）、联系人页（底部双 tab）使用。

### `lv_tef` — TEF 动画控件

循环播放 TEF（Tuya Emotion Format）动画的 LVGL 控件，用法同 `lv_gif`：create 后 `set_src` 即循环播放，删控件即停。素材是编译进固件的 `.tef` 常量数组（`miscs/tef/tools/gen_assets.py` 生成），播放期间须常驻；解码发生在 LVGL timer（UI 线程），多控件可并存，每控件 PSRAM 占用约 `画布 w*h*2 + 索引 w*h + 字典 16KB + tinfl 11KB`。格式与工具链见 `src/miscs/tef/`，用法见 [TEF 动图 howto](../../docs/howto/use-tef-animation.md)。

---

## 四、视觉样式系统 — `style/`

定义全部视觉设计令牌（design tokens）：颜色、字体、间距、圆角、Flex 布局辅助及分辨率自适应缩放。**禁止在 widget/page 中硬编码颜色值或像素尺寸**。

### `ui_adaptive` — 分辨率自适应

以 **320×480** 为基准（`UI_ADAPT_BASE_W / UI_ADAPT_BASE_H`），`ui_adapt()` 等比缩放：

```c
lv_obj_set_size(btn, LV_PCT(100), ui_adapt(48));
```

缩放因子用定点数（`×256`）避免浮点运算。辅助：`ui_adapt_screen_w/h()`、`ui_adapt_is_small()`（宽 ≤ 240px）。

### `ui_theme` — 设计令牌与共享样式

颜色宏（`UI_COLOR_*`）、字体（`UI_FONT_DEFAULT`）、圆角（`UI_RADIUS_*`）、间距（`UI_SPACE_*`）：

| 颜色令牌 | 用途 |
|---|---|
| `UI_COLOR_PRIMARY` / `PRIMARY_DARK` | 主题蓝 / 深色变体 |
| `UI_COLOR_BG` / `BG_CARD` | 页面背景 `#1A1A1A` / 卡片背景 `#2A2A2A` |
| `UI_COLOR_TEXT` / `TEXT_SEC` | 正文白 / 辅助灰 `#AAAAAA` |
| `UI_COLOR_SUCCESS` / `WARNING` / `ERROR` | 语义色（成功/警告/错误） |
| `UI_COLOR_LINK` | 超链接色（`ui_comp_link`） |
| `UI_COLOR_STATUSBAR_BG/_BG_OPA/_ICON/_ACCENT` | 状态栏背景/透明度/图标/强调色 |
| `UI_COLOR_BATTERY_OK/_WARN/_LOW` | 电池电量三档色 |
| `UI_COLOR_SLIDER_TRACK/_FILL` | 滑块轨道/填充色 |

预构建的 LVGL 共享样式对象：

| 样式对象 | 用途 |
|---|---|
| `ui_style_screen` | 页面根容器 |
| `ui_style_card` | 卡片容器 |
| `ui_style_btn` / `ui_style_btn_pressed` | 主要按钮 / 按压态 |
| `ui_style_btn_sec` / `ui_style_btn_sec_pressed` | 次要按钮 / 按压态 |
| `ui_style_btn_ghost_pressed` | 无填充按钮按压态 |
| `ui_style_btn_circle` / `ui_style_btn_circle_pressed` | 圆形按钮（透明填充+环形边框）/ 按压态 |
| `ui_style_text` / `ui_style_text_title` / `ui_style_text_caption` | 正文 / 标题 / 辅助文字 |
| `ui_style_bar` | 进度条轨道 |
| `ui_style_slider` | 滑块 |

其他常量：`UI_BTN_CIRCLE_SIZE`（圆形按钮默认直径 56，经 `ui_adapt()` 换算）。

间距常量（基准值，需经 `ui_adapt()` 换算）：

| 常量 | 值 |
|---|---|
| `UI_SPACE_XS` | 4px |
| `UI_SPACE_SM` | 8px |
| `UI_SPACE_MD` | 12px |
| `UI_SPACE_LG` | 16px |
| `UI_SPACE_XL` | 24px |

目前仅暗色主题（`init_dark_theme`），`ui_theme_set_mode()` 预留亮色切换（当前为 no-op）。

### `ui_layout` — Flex 布局辅助

LVGL Flex Layout 的语义封装，间距参数自动经 `ui_adapt()` 换算：

| 函数 | 说明 |
|---|---|
| `ui_layout_row(parent)` | 水平 Flex 容器 |
| `ui_layout_col(parent)` | 垂直 Flex 容器 |
| `ui_layout_full_screen(parent)` | 占满父容器的垂直 Flex |
| `ui_layout_gap(obj, gap)` | 子项间距 |
| `ui_layout_padding(obj, h, v)` | 水平/垂直内边距 |
| `ui_layout_align(obj, main, cross)` | 主轴/交叉轴对齐 |
| `ui_layout_grow(child, grow)` | 伸缩系数 |
| `ui_layout_wrap(obj)` | 换行 |

---

## 五、国际化 — `i18n/`

所有用户可见文案必须通过 `ui_i18n_text()` 获取，**禁止硬编码字符串**。支持简体中文（`UI_LANG_ZH_CN`，默认）和英语（`UI_LANG_EN`）。

### 运行时接口

- `ui_i18n_init(lang)` — `ui_app_init()` 中调用一次。
- `ui_i18n_set_lang(lang)` / `ui_i18n_get_lang()` — 运行时切换/查询。
- `ui_i18n_text(key)` — 返回对应字符串；缺翻译时回退英语。**不可缓存返回指针**（语言切换后失效）。

### 文案存储 — `ui_i18n_data.c`

二维数组 `ui_i18n_strings[语言][key]`，使用 C99 指定初始化器：

```c
[UI_LANG_ZH_CN] = {
    [UI_TEXT_CHAT_IDLE] = "待命",
    [UI_TEXT_CHAT_LISTENING] = "倾听中",
    ...
}
```

**新增文案**：① `ui_i18n.h` 枚举加 key → ② `ui_i18n_data.c` 两语言块填字符串 → ③ UI 代码用 `ui_i18n_text(UI_TEXT_新KEY)`。

### 文案分类

| 前缀 | 用途 |
|---|---|
| `UI_TEXT_OK/CANCEL/CONFIRM/...` | 通用操作 |
| `UI_TEXT_CHAT_*` | AI 对话状态 |
| `UI_TEXT_MODE_*` | 设备模式名称 |
| `UI_TEXT_APP_*` | 功能入口名称 |
| `UI_TEXT_PULLDOWN_*` / `UI_TEXT_SWIPE_*` | 下拉面板 |
| `UI_TEXT_HOME_*` | 首页专用 |
| `UI_TEXT_WEEKDAY_*` | 星期名称 |
| `UI_TEXT_SETTINGS_*` / `UI_TEXT_LANG_*` | 设置页文案 |
| `UI_TEXT_ABOUT_*` | 关于页文案 |
| `UI_TEXT_OTA_*` | 系统升级页、升级状态文案 |
| `UI_TEXT_RESET_*` / `UI_TEXT_RESETTING` | 重置流程 |
| `UI_TEXT_CALL_*` | 通话页（拨号/接听/挂断/时长等） |
| `UI_TEXT_PICTURE_*` | 相册页（空态/选择/删除确认/查看图片） |
| `UI_TEXT_MUSIC_*` | 音乐页（标题/未知歌曲/播放模式等） |
| `UI_TEXT_CLOCK_*` | 时钟页（闹钟/倒计时/秒表 tab 等） |
| `UI_TEXT_SCHEDULE_*` | 日程提醒页（日期/空态等） |
| `UI_TEXT_POMODORO_*` | 时钟页番茄钟 tab（阶段/循环等） |
| `UI_TEXT_RECORDING_*` | 录音页/录音列表（列表/删除等） |
| `UI_TEXT_TRANSCRIBE_*` | 录音转写页（转写/总结 tab 等） |
| `UI_TEXT_FILES_*` | 文件浏览页（文件系统/信息等） |
| `UI_TEXT_DETECTION_*` | 侦测记录页（图片/总结等） |
| `UI_TEXT_AUDIO_DIAG_*` | 音频诊断页（关闭/UART/局域网/SD） |
| `UI_TEXT_DIAG_*` | 诊断页（系统状态/网络诊断/显示帧率/音频诊断入口） |
| `UI_TEXT_SYS_*` | 系统状态页（SRAM/PSRAM/运行时长/复位原因） |
| `UI_TEXT_NET_*` | 网络诊断页（WiFi/SSID/RSSI/IP/MAC/云连接） |
| `UI_TEXT_SCREEN_TEST_*` | 屏幕测试页（提示/清屏/退出） |

---

## 六、后台服务 — `services/`

独立于具体页面的后台数据/控制服务。服务不直接操作 LVGL widget，只更新 `ui_state` 或通过回调传递数据；**回调一律由服务侧经 `ui_app_async_call()` 封送到 UI 线程后调用**，页面回调内可直接操作 LVGL。

### `ui_services.c / ui_services.h` — 服务初始化汇总

`ui_services_init()` 在 `ui_app_init()` 中调用，统一启动所有后台服务：

```c
ui_fs_init();               // 挂载权已归 wukong_storage；此处只订阅就绪事件建树
ui_svc_dev_ctrl_load();     // 启动恢复持久化设置
ui_svc_wlan_init();
ui_svc_activate_init();
ui_svc_ota_init();          // OTA 生命周期/进度桥接（独立升级页 + 下拉辅助进度）
ui_svc_weather_init();
ui_svc_call_init();
ui_svc_picture_init();
ui_svc_camera_init();
ui_svc_music_init();
ui_svc_recording_init();
ui_svc_transcribe_init();   // 起常驻转写轮询线程（阻塞态，按需唤醒）
ui_svc_detection_init();
ui_svc_tm_init();           // 时间管理：观察者 + 数量同步
```

新增服务时在此追加初始化调用。`ui_fs_init()` 必须最先调用：外部卷由 `wukong_storage` 在 WORKQ_SYSTEM 上异步挂载，挂载完成后发布 `EVENT_WUKONG_STORAGE_READY`；`ui_fs_init()` 先订阅该事件（base_event 按订阅顺序派发），保证其目录树构建先于后订阅的消费者执行。依赖存储的服务不在 init 里直接读盘，而是把开机一次性加载延迟到 storage.ready（订阅 + `wukong_storage_ready()` 追赶，经 WORKQ_SYSTEM 蹦床串行化——参照 `ui_svc_fs.c` / `ui_svc_recording.c` 的范式）。

### 单槽回调约定

数据服务的页面回调是**单槽**（`set_cb` 后写覆盖）。两种生命周期模式：

- **会被覆盖页打断的页面**（音乐/歌单）：`on_create`/`on_enter` 注册，**`on_leave` 清除**；`on_enter` 检测到未注册时重注册并用当前状态做一次全量同步，补上被覆盖期间错过的变化。
- **普通页面**（首页天气、通话、相册）：`on_create` 注册，**`on_destroy` 清除**（迟到的异步结果成为 no-op）。

### `ui_svc_weather` — 天气数据服务

从涂鸦云 IoT（`thing.weather.get`）拉取天气数据（温度、高/低温、描述），供首页天气卡片显示。

工作机制：启动后延迟 3 秒首次请求（等网络就绪）→ 之后每 **30 分钟**刷新 → 网络未就绪 3 秒重试 → 成功后封送 UI 线程回调。

| 函数 | 说明 |
|---|---|
| `ui_svc_weather_init()` | 启动定时拉取 |
| `ui_svc_weather_set_cb(cb)` | 注册更新回调 |
| `ui_svc_weather_available()` | 判断是否有有效数据 |
| `ui_svc_weather_get()` | 获取最近数据指针 |

### `ui_svc_dev_ctrl` — 设备控制服务

管理设备运行时设置（音量、亮度、模式、语言），并提供持久化与恢复机制。是 UI 层与 `gw_intf.h`/`tuya_ai_toy.h` 等业务头文件之间的隔离层。另含两组内存态诊断开关转发：FPS overlay（`fps_overlay_*` → ui_port）与 AP-STAT 播放统计（`ap_stat_*` → svc_ai_player，`ap_stat_available()` 反映统计代码是否编入）。

持久化模型：

- `load()` — 启动时从 KV 恢复全部设置到运行时状态。
- `save()` — 页面退出时将运行时状态与影子副本比较，有变化才调度 KV 写（`WORKQ_SYSTEM`，非阻塞）。
- `*_set()` — 仅修改运行时状态（不写 KV），适合滑块/按钮高频调用。
- `*_get()` — 读取运行时状态（`ui_state`），不读 KV。

| 函数 | 说明 |
|---|---|
| `ui_svc_dev_ctrl_load()` / `save()` / `reset()` | 恢复 / 持久化 / 恢复出厂（异步解绑） |
| `ui_svc_dev_ctrl_mode_get/set()` | 设备主模式（↔ `AI_DEVICE_MODE_E`） |
| `ui_svc_dev_ctrl_language_get/set()` | UI 语言（↔ `ui_lang_t`） |
| `ui_svc_dev_ctrl_volume_get/set()` | 音量 0~100 |
| `ui_svc_dev_ctrl_brightness_get/set()` | 背光亮度 0~100 |

### `ui_svc_devinfo` — 设备信息服务

为"关于"页提供只读设备信息。所有 getter 返回非 NULL 字符串（不可用时返回 `"--"`）：`fw_version()` / `sdk_info()` / `device_id()`。

### `ui_svc_sysmon` — 系统监控服务

为诊断"系统状态"页提供实时系统数据的薄封装（页面不直接 include `tal_*` 头）：`sram_free()` / `psram_free()`（无 PSRAM 板型返回负值）/ `uptime_sec()` / `reset_reason()`（`TUYA_RESET_REASON_E` 数字码——describe 出参在本平台不可靠，与开机日志 `system reset reason:[%d]` 对照）。纯 getter 无 init，每秒调用级开销。

### `ui_svc_netmon` — 网络监控服务

为诊断"网络诊断"页提供实时网络信息的薄封装：`wifi_connected()` / `ssid()` / `rssi()`（dBm，0 = 不可用）/ `ip()` / `mac()`。字符串 getter 返回非 NULL（`"--"` 兜底）、静态缓冲仅限 UI 线程。整体套 `ENABLE_WIFI_SERVICE` 守卫，非 WiFi 构建编译 stub 分支。纯 getter 无 init。

### `ui_svc_call` — 通话控制服务

UI 层**唯一**触碰 P2P / `TUYA_IPC_*` 业务 API 与 `TUYA_IPC_CALL` 事件的文件。

- **状态机**：`IDLE / CALLING / INCOMING / IN_CALL / FAILED / ENDED / ENDED_BY_OTHERS`；`get_state()` 为权威值（页面重建后可恢复）。
- **来电**：以 `INCOMING` 状态上抛，由通话页呈现响铃 UI 供接听/拒接。自动接通运行时值默认关闭，设置页开关与 KV 恢复由 `ENABLE_UI_CALL_AUTO_ANSWER`（默认 0）统一编译隔离，当前不会自动接听。
- **线程模型**：IPC 事件与 30s 去电超时都在非 UI 线程触发；服务把每次状态变化经 `ui_app_async_call` 封送到 UI 线程后才调页面回调。
- **接口**：`init()`（幂等；特性未编译时为空实现）、`available()`、`set_cb()`、`dial()`、`answer()`、`reject()`、`hangup()`、`auto_answer_get/set()`（运行时值在 call 层，持久化归 `ui_svc_dev_ctrl`）。

### `ui_svc_picture` — 相册图片服务

UI 层访问图片后端（`wukong_picture_*`、JPEG→RGB565 解码）的唯一入口。所有触碰相册扫描会话的操作都在 `WORKQ_SYSTEM` 串行执行（UI 线程零 flash 等待），结果封送回 UI 线程经回调交付。

- **Viewer 流**：`open()` → `on_count` 回调 → `request(index, seq)` 按索引解码大图；`seq` 回显实现 **latest-wins**（快速滑动时页面据此丢弃过期帧）。大图缓冲**所有权转移**给接收方（交给 `ui_comp_picture_set_rgb565` 并传 `ui_svc_picture_free_rgb565` 作 free_fn，或直接释放）。
- **Grid 流**：`thumbs_request()` → `on_thumbs` 交付缩略图列表（≤30 张，缓冲**借用**服务所有、`close()` 前有效，接收方不得释放）；`delete_batch(names, n)` 按文件名批量删除。
- **"查看图片"**：`set_view_cb()` + `view_request(name)` 按名独立解码（无需相册会话），供对话页生图链接使用。
- **AI识图**：`recognize_current(done_cb)` 在工作队列中读取当前 JPEG，必要时切换到闲聊模式，通过 `AI_MODE_OP_PICTURE` 发送图片和固定提示词，完成后由页面进入聊天页等待结果。
- **图生图附件**（ADR-0003）：`generate_from_current(done_cb)` 把当前图片排入下一轮图片输入、暂存 RGB565 预览并切换到生图模式；对话页 `take_pending_attachment()` 领取预览（所有权转移）。

### `ui_svc_music` — 音乐控制服务

包装 `wukong_playback_ctrl` / `wukong_audio_player`，订阅 `EVENT_MUSIC_PLAYER` / `EVENT_MUSIC_BREAK` 事件维护状态缓存（播放状态/歌名/歌手），每次变化封送 UI 线程调单槽回调。

- `init()`：种子化当前后端状态 + 订阅事件 + 设置 UI 默认播放模式（列表循环）。
- `autoplay()`：打开页面即播语义（播放中 no-op / 暂停恢复 / 停止续播或播第一首 / 空列表云端推荐），全程非阻塞（秒级 URL 刷新在后端 worker）。
- 播控：`play_pause()` / `next()` / `prev()` / `play_id()` / `remove_id()` / `mode_get/set()`。
- 查询：`get_status()`（缓存）、`list_async(cb)`（WORKQ 取列表 → UI 线程交付 cJSON，接收方释放；同步版 `list()` 会在播控互斥锁上阻塞，UI 线程热路径禁用）、`current_id()`、`progress_percent()`（0~100，未知长度返回 -1）。

### `ui_svc_camera` — 相机数据服务

UI 层访问相机后端（`tuya_camera_*`、相册 `wukong_picture_*`）的唯一入口。所有像素转换（YUV422→RGB565 预览、JPEG→RGB565 缩略图）在相机适配层完成；本服务**绝不触碰 LVGL**，只把目标流/缩略图封送 UI 线程交给相机页。

- 相机线程产出预览帧 → 服务只保留最新一帧（drop-old）→ `ui_app_async_call` 封送 UI 线程；拍照与最新缩略图加载在 `WORKQ_SYSTEM`（快照/相册 IO 阻塞）。
- 拍照路径仅执行快照、相册保存和缩略图刷新，不包含图片上传或 AI 模式分支。
- 两个回调都在 UI 线程触发并把 RGB565 缓冲**所有权转移**给接收方（用 `ui_svc_camera_rgb_free` 释放，或交给 `ui_comp_picture_set_rgb565`）；NULL 缓冲表示"无内容"不带所有权。

### `ui_svc_recording` — 录音服务

录音采集 + 录音列表 + 播放 + 转写态持久化。转写态以整数持久化（`-1/0/1/2`，兼容旧 view 数据），以音频 md5（`UI_REC_MD5_HEX_LEN`=32）为云端转写关联键；采集态无暂停（启动后只能停止）。

### `ui_svc_video` — 本地录像服务

包装 `ty_avi_recorder` 的异步启停和状态机（`IDLE / STARTING / RECORDING / STOPPING / ERROR`）。文件名以时间戳为基准并在落盘前检查重名、递增后缀，路径由 `ui_fs_path(UI_FS_VIDEO)` 创建；服务不触碰 LVGL，状态变化统一封送到 UI 线程。录像以 15fps 为目标并按采集时间戳封装 MJPEG；音频管线运行且未被 P2P 通话占用时，旁路复制 PCM 到 AVI。录像线程检测到连续秒级写入卡顿或 I/O 失败后只上报事件，停止原因会锁存到统一的收尾任务；正常停止与存储故障不会重复关闭或覆盖终态。若 `WORKQ_SYSTEM` 队列已满，已运行录像会由一次性 `avi_rec_stop` 线程完成清理，避免录像线程自等待死锁。

### `ui_svc_video_playback` — 本地视频播放服务

使用 `ui_fs_list_app_async(UI_FS_VIDEO)` 枚举应用录像，以 `ui_fs_path()` 生成绝对路径，再在 `WORKQ_SYSTEM` 打开、关闭和控制 `ty_avi_player`。解码帧最多保留一个待投递缓冲，并经 `ui_app_async_call()` 封送到 UI 线程；帧所有权转移给页面。批量删除同样在 `WORKQ_SYSTEM` 执行，只接受合法 AVI 叶文件名并经 `ui_fs_remove_app()` 删除应用根目录中的录像，AVI 删除成功后同步清理 libavi 生成的 `.avi.idx` 伴生索引；播放器持有文件期间拒绝删除。服务不挂载文件系统、不直接操作 LVGL/LCD；仅在文件确有 PCM 音轨时以 `VIDEO` owner 获取共享音频输出，P2P 可抢占而 AI 播放不会混入，旧 owner 的停止也不会误关新 owner 的 DAC。若 `WORKQ_SYSTEM` 已满导致收尾或停止任务投递失败，服务会启动一次性 `avi_cleanup` 线程回收播放器、音频线程和文件句柄。

### `ui_svc_transcribe` — 云端转写工作流服务

驱动"上传录音 → 触发云端转写 → 轮询取结果 → 下载转写/总结文本"工作流；数据所有权归 `ui_svc_recording`，本服务只经其领域 API 读写。

- **线程模型**：上传泵跑在 `WORKQ_SYSTEM`（自驱动分块）；轮询为常驻线程，平时阻塞在信号量，上传完成被唤醒、无待处理回到阻塞，`deinit()` 可干净停止；状态变化经 `ui_app_async_call` 封送 UI 线程触发单槽回调。
- 转写阶段（派生量）：`UNAVAILABLE / NOT_UPLOADED / UPLOADING / PROCESSING / DONE / FAILED`。

### `ui_svc_detection` — 侦测记录服务

侦测记录页的只读数据源（云端 AI 侦测告警 `thing.ipc.ai.robot.msg.list`）。阻塞式 HTTP 拉取在 `WORKQ_SYSTEM`，结果经 `ui_app_async_call` 封送 UI 线程后再触发页面回调（回调内可直接操作 LVGL）。服务持有解析后的页缓冲 + 分页元数据（每页 `UI_SVC_DETECTION_PAGE_SIZE`=10，最多 `UI_SVC_DETECTION_MAX_PAGE`=20 页），页面经 getter 读取。

### `ui_svc_fs` — 统一文件系统服务

挂载本身由 `wukong_storage` 统一完成（编译期选 SD 卡 / 外挂 flash，WORKQ_SYSTEM 异步挂载，完成后发布 `EVENT_WUKONG_STORAGE_READY`）。`ui_fs_init()` 不再同步挂载：它订阅该事件（并以 `wukong_storage_ready()` 做追赶），在卷就绪后构建标准目录树。设备根 `UI_FS_MOUNT`（如 `/sdcard`）即 `WUKONG_STORAGE_ROOT`；应用自身数据位于子命名空间 `UI_FS_ROOT`（`<root>/tuyaos`）。两个根分用途：

- `UI_FS_ROOT` — 应用读写自身数据（`ui_fs_path()`）；应用媒体枚举/删除分别使用 `ui_fs_list_app_async()` / `ui_fs_remove_app()`。
- `UI_FS_MOUNT` — 整张挂载卷，文件浏览页据此遍历（`ui_fs_list_async()` 相对它），故 tuyaos/ 旁的用户文件也可见。

挂载失败不回退（`ui_fs_ready()` 保持 false）。依赖存储的消费者（录音索引、经 wukong store FS 后端的歌单/闹钟等）同样把开机一次性加载延迟到 storage.ready，不假设 init 时卷已挂载。目录列表上限：单项名 `UI_FS_NAME_MAX`=128、单目录 `UI_FS_LIST_MAX`=256 项。

`UI_FS_ROOT`（`/sdcard/tuyaos`）下的目录布局：

```
tuyaos/
├── picture/            # AI 生图相册
│   ├── album/thumb/    # 缩略图
│   └── DCIM/thumb/     # 拍照缩略图
├── data/               # 用户自定义下载目录（应用不干预、开机不清空）
├── font/               # 字体，应用自行读取喂给 LVGL（不走 LVGL fs 驱动）
├── music/
│   ├── local/          # 本地音乐 + playlist.json
│   └── cloud/          # 云端音乐 + playlist.json
├── recording/
│   ├── recording_list.json
│   └── transcribe/     # 转写文本结果
├── video/              # 本地 MJPEG AVI 录像与回放
└── tmp/                # 临时内容，每次上电默认清空整个目录
```

### `ui_svc_tm` — 时间管理服务

闹钟/日程提醒/倒计时/秒表/番茄钟的隔离层（镜像 `WUKONG_TM_*`，对页面保持 LVGL/业务头文件无关）。注册观察者 + 数量同步；到点事件（`ALARM / REMINDER / COUNTDOWN_DONE / POMODORO_PHASE`）经 `ui_app_async_call` 封送 UI 线程后触发单槽 `fire_cb`，供 `ui_comp_ring_alarm` 弹出响铃。供 clock/schedule 页读取倒计时/秒表/番茄钟运行态。

### `ui_svc_audio_diag` — 音频诊断服务

UI 层访问 `audio_dump` 的唯一入口（隔离 FS 与 miscs 依赖）。通道值同 `AUDIO_DUMP_CHANNEL_E`：0=关闭 1=UART 2=局域网 3=SD卡。运行时状态，**不持久化**（掉电恢复为关闭）。`available()` 判断是否至少有一个真实通道可用（否则设置页不显示入口），`caps()` 返回位掩码供页面灰显不可用项。

### `ui_svc_wlan` — WiFi 扫描/连接服务

WLAN 页与下拉面板快捷入口的数据源：`refresh()` 异步扫描、`refresh_status()` 只刷新当前连接状态、`connect(ssid, password)` 发起连接；结果与连接状态变化经单槽回调（`set_cb`）通知，`get()` 取最近一次结果快照。

### `ui_svc_nfc` — NFC 读写服务

联系人页"发现"tab 与设置页 NFC 开关的隔离层：`available()`/`ready()` 判断硬件与初始化状态；`switch_get/set()` 是单向持久化开关（开=初始化 PN532，关不做去初始化）；`read_async()` / `write_uuid_async()` 异步读写，完成后回调带结果码与 UUID。

### `ui_svc_activate` — 激活状态服务

首启激活向导的数据源：`is_activated()`（设备是否已激活，决定开机是否叠加向导）、`is_complete()`（本次向导是否走完）；单槽回调（`set_cb`）上报激活事件（短链就绪、`EVENT_POST_ACTIVATE` 完成等），仅二维码步骤前台时注册。

### `ui_svc_ota` — OTA 状态服务

订阅 `EVENT_OTA_START_NOTIFY` / `EVENT_OTA_PROCESS_NOTIFY` / `EVENT_OTA_PROGRESS_NOTIFY` / `EVENT_OTA_FINISHED_NOTIFY` / `EVENT_OTA_FAILED_NOTIFY`，把 OTA 映射为 `PREPARING / DOWNLOADING / VERIFYING / INSTALLING / FAILED` 状态并与下载百分比一起封送到 UI 线程缓存。前台页面使用单槽回调实时刷新；app 级全局回调负责首次升级和校验/安装阶段的强制路由。`FINISHED` 只表示固件包下载与 hmac 校验完成，服务随后保持 `INSTALLING`，等待平台写入并重启。

`check_now()` 在系统工作队列调用 `tuya.device.screen.ota.check`，解析主模块结果后把 `CHECKING / AVAILABLE / UP_TO_DATE / FAILED` 和版本、包大小、说明统一封送到 UI 线程缓存。`confirm_now()` 同样在工作队列调用 `tuya.device.screen.ota.confirm`，缓存 `CONFIRMING / ACCEPTED / FAILED` 并记录完整返回；确认成功后由真实 OTA 事件进入下载、校验和安装状态。检查/确认状态与真实升级状态分开保存，真实 OTA 状态在页面和“关于”入口中拥有更高展示优先级。

`EVENT_OTA_PROGRESS_NOTIFY` 的载荷 `TUYA_UPGRADE_PROGRESS_T` 位于发布方栈上、同步分发，因此订阅回调里只取出 `percent` 转发（随 `ui_app_async_call` 按值传递，`percent + 1` 编码过 `void *`），不保留指针。`get_percent()` 返回 `-1` 表示尚无进度，此时页面画不确定进度条；达到 98% 后切到 `VERIFYING`，其余百分比由校验与写入占用。SDK 的进度定时器在发布完成/失败事件之后才删除，可能多出一拍进度，服务侧用“已结束”标记丢弃，避免页面被重新点亮。

### `ui_svc_stubs.c` — 可选服务降级桩

功能簇关闭时，为核心页（home/chat/settings）仍会引用的可选服务函数提供 no-op/false 桩，使核心页无需 `#if` 即可链接并优雅降级。每块用 `#if !(UI_FEATURE_X)` 守护，与真服务文件被 `local.mk` 剔除的条件严格互补——功能开启时本块编空（真服务提供符号），不会重复定义。**新增可选服务时记得在此补桩**。

---

## 七、平台移植层 — `port/`

将 LVGL 与 TuyaOS 硬件解耦，是 UI 框架唯一直接调用 TuyaOS 系统 API（`tal_*`、`tkl_*`）的地方。上层业务不应直接引用此目录接口（除 `ui_port_lock/unlock`）。

### `ui_port.c / ui_port.h` — LVGL 主循环与线程

- `ui_port_init(cfg)` — 初始化互斥锁、LVGL、显示驱动和输入设备。
- `ui_port_start()` — 启动 `ui_loop` 线程，循环调用 `lv_timer_handler()`。
- `ui_port_lock()` / `ui_port_unlock()` — LVGL 互斥锁，外部线程调用 LVGL API 前必须持有。

### `ui_port_disp_sw.c` — 显示驱动（软件版）

CPU 拷贝 flush：在 PSRAM 中分配 double buffer，通过 `tal_display_flush()` 提交像素帧，`s_vsync_sem` 等待垂直同步。

### `ui_port_disp_dma2d.c` — 显示驱动（DMA2D 加速版）

与 `ui_port_disp_sw.c` 接口相同（二选一编译），使用 `tkl_dma2d_memcpy()` 复制帧缓冲。4 块 PSRAM 缓冲（2 绘制 + 2 帧），通过 `s_dma2d_sem` 等待 DMA2D 中断。

### `ui_port_perf.c` — FPS / CPU overlay

`lv_layer_top()` 上的性能浮层：在显示驱动每帧回调（`ui_port_perf_on_frame()`）里累计帧数，每秒重算 FPS 与 CPU 占用并重绘标签。由设置页"显示 FPS"开关切换显隐。

### `ui_port_indev.c` — 输入设备驱动

按编译宏选择性注册：

| 宏 | 输入类型 | 说明 |
|---|---|---|
| `UI_INDEV_TOUCH` | `POINTER` | 触摸坐标 |
| `UI_INDEV_KEYPAD` | `KEYPAD` | 键盘（桩） |
| `UI_INDEV_ENCODER` | `ENCODER` | 编码器（桩） |

### `lv_conf.h` — LVGL 配置

控制 LVGL 8.3 编译选项：颜色深度（RGB565）、字体、内存来源、DMA2D 启用等。

### `tuya_ai_display_stub.c` — 业务层消息桥

将 `miscs/display/` 的通用显示消息总线与本 UI 框架连接：

1. 注册 `ui_display_msg_handler`。
2. 加 `ui_port_lock()` 保证线程安全。
3. 根据 `TY_DISPLAY_TYPE_E` 更新 `ui_state`（对话状态/设备模式/子模式）；**对话从空闲转活跃且当前页为 HOME 时**自动 push `UI_PAGE_CHAT`。
4. 转发消息给 `ui_page_chat_on_msg()` 更新气泡。处理的消息类型含对话流（`HUMAN_CHAT` / `AI_CHAT_START/DATA/STOP`）、状态（`STAT_*` / `CHAT_STAT` / `CHAT_MODE` / `MODE_NOTIFY`）、附件清除（`CLEAR_ATTACHMENT`）与生图结果（`AI_IMAGE`，携带保存的图片文件名）。

此文件是**业务→UI 方向**（`tuya_ai_display_msg` 消息总线）的唯一耦合点；**UI→业务方向**不经过 display 层（旧框架的 `tuya_ai_display_action_post()` 已不使用），由页面事件回调直接调用 `services/ui_svc_*` 接口完成。

### `port/compat/tuya_port_disp.c` — 编译存根

提供 `tkl_system_sram_malloc/free` 等平台 API 的弱实现，在不支持 SRAM 独立分配的平台上占位。与同目录下的 `tuya_port_disp.h`（满足 vendored libjpeg-turbo 硬编码 include 的兼容垫片）成对。
