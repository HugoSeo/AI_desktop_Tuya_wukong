# 定制 UI

## 目标

了解在 wukong demo 里改 UI 有哪几种方式、各自改哪些文件，并知道去哪找详细规则。

本文只做导航，**不复述框架细节**——UI 框架的架构、数据流、编码规范权威定义在
[`src/ui/RULES.md`](../../src/ui/RULES.md)（AI 工具与人类贡献者都必须遵守），
子系统说明在 [`src/ui/README.md`](../../src/ui/README.md)。改 `src/ui/` 前请先读这两篇。

## 前置条件

- 已按[快速开始](../quickstart.md)跑通编译、烧录。
- 板子已启用新 UI 框架（`CONFIG_ENABLE_TUYA_UI=y`，`make app_menuconfig` 可查看/开启）。

## 步骤

UI 定制分四个层级，按改动范围从小到大选择：

### ① 改一个已有 wukong 页面

页面源码在 `src/ui/pages/ui_page_<name>.c`（如 `ui_page_home.c`、`ui_page_settings.c`），
页面清单见 `src/ui/README.md`「二、页面实现」一节。

- **页面自包含**：一个页面的布局、状态、渲染逻辑都写在它自己的 `.c` 文件里，不要拆成独立文件；
  只有当一个 UI 元素被**多个页面**复用时才提取到 `src/ui/widgets/`（`RULES.md` §2/§6）。
- 直接改该页面的 `on_create`（建 widget 树）/ `on_enter`（按 dirty 位刷新）/ `on_leave` / `on_destroy`
  四个生命周期回调即可，不需要碰路由、注册等框架代码。
- 新增/修改状态展示，遵守单向数据流：业务线程 `ui_state_set_*()` 标脏 → `ui_tick`（30ms）→
  页面 `on_enter(dirty)` 消费 → 刷新 LVGL（`RULES.md` §1、§5）。

### ② 新增一个页面

按 `src/ui/RULES.md` 的「Quick Reference: Adding a New Page」抄模板：

1. 在 `src/ui/pages/ui_page_ids.h` 的枚举里加一个 `UI_PAGE_XXX`（禁止用魔法数字）。
2. 新建 `src/ui/pages/ui_page_xxx.c`，实现 `on_create/on_enter/on_leave/on_destroy` 并导出
   `const ui_page_entry_t ui_page_xxx_entry`。
3. 在 `src/ui/ui_app.c` 的 `ui_app_init()` 里 `extern` 该 entry 并调用
   `ui_route_register(&ui_page_xxx_entry)`（参照文件内已有的 21 行 `ui_route_register` 调用）。
4. 需要从其他页面跳转过来，用 `ui_route_push(UI_PAGE_XXX)`（不要让页面互相 `#include`）。

新页面同样遵守自包含、单向数据流、i18n 等约束，见下方常见问题与 `RULES.md`。

### ③ 按功能簇裁剪页面（`UI_FEATURE_*`，不用改代码）

不需要某些功能页（如没有摄像头、不做录音）时，不用删代码——用 Kconfig 把整簇功能从固件裁掉：
`make app_menuconfig` → UI 配置下的 **UI Features** 菜单，共 9 个开关（相机相册、音乐、录音转写、
时间管理、侦测记录、P2P 通话、文件浏览、音频诊断、天气），默认全开。关闭一个开关会同时裁掉：

- 对应页面与后台服务源文件（不参与编译，直接省 flash/RAM）；
- 应用中心里的入口卡片（卡片表按 `ui_feature_available()` 门控，自动隐藏）。

核心页（home/chat/settings/诊断）不受影响：被关闭功能的服务函数由降级桩 `src/ui/services/ui_svc_stubs.c`
提供 no-op 实现，保证链接通过、界面优雅降级。注意部分开关依赖底层能力（如相机簇依赖
`ENABLE_TUYA_CAMERA`、录音簇依赖 `ENABLE_AI_MODE_RECORD`），底层没开时菜单里不可见。
改完执行 `make app_config APP_NAME=tuyaos_demo_wukong_ai` 重新生成配置头再编译。

### ④ 整板自定义 UI（不用 wukong 页面）

如果目标是让某块板子完全不用 wukong 的默认页面集，走 board UI 接管方式：在板级目录下实现
`app_ui_init()` / `app_ui_msg_handler()`，并在 `tuya_device_board_init()` 里调用
`ui_app_register_board_ui(app_ui_init, app_ui_msg_handler)`。这是[适配新板](porting-new-board.md)
第 6 步的内容，完整说明和现有板子示例（`T5AI_BOARD_ROBOT`/`T5AI_BOARD_EVB`/`T5AI_BOARD_EVB_PRO`/
`T5AI_BOARD_EYES`）见 `src/boards/README.md`「UI 实现」一节。

## 验证方法

1. `make app_config APP_NAME=tuyaos_demo_wukong_ai`（改过 Kconfig/开关后）→
   `make app APP_NAME=tuyaos_demo_wukong_ai` 编译通过。
2. 烧录到真机：目标页面能正常进入/退出，触摸手势（右滑返回等）正常。
3. 切换中英文（设置页语言项），确认新增/改动的文案都跟着变、没有残留旧语言文字。
4. 让页面停留超过几分钟，确认没有因为动画/`lv_timer` 误用导致的音频卡顿（有音乐播放时尤其要试）。

## 常见问题

**改了状态但页面不刷新**
检查是否用了 `ui_state_set_*()` 标脏，而不是直接改 page 内部变量后指望自动生效；也检查
`on_enter(dirty)` 里是否漏了对应 `UI_STATE_GROUP_*` 位的分支（`RULES.md` §5）。

**新页面/组件用 `lv_timer_create` 自己做刷新**
框架禁止这样做——刷新一律走 dirty 位 → `on_enter(dirty)`，否则绕过了单向数据流约束
（`RULES.md`「Prohibited Patterns」）。

**语言切换后文字变乱码/没变**
`ui_i18n_text()` 的返回指针**不能跨语言切换缓存**，每次渲染要重新调用取值；新增文案必须走
`i18n/ui_i18n.h` + `i18n/ui_i18n_data.c` 两语言表，禁止硬编码字符串（`RULES.md` §「i18n」）。

**销毁页面/清空列表后紧接着 crash（`PC=0`）**
大概率是缓存的 `lv_obj_*` 静态指针在容器被 `lv_obj_clean`/`lv_obj_del` 释放后没有同步置 NULL，
之后的 tick 刷新或异步回调解引用了悬空指针。释放容器与置空所有指向其子树的静态指针必须在同一步完成
（`RULES.md` §7.1）。

**自定义 board UI 没生效，还是走默认页面**
检查 `ui_app_register_board_ui()` 是否确实在 `tuya_device_board_init()` 里被调用，以及调用发生在
UI 初始化之前；详见[适配新板](porting-new-board.md)的同名常见问题。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
