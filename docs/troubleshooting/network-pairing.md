# 配网/激活失败排查

## 概述

本文排查 wukong AI 设备「配网（netconfig）」与「激活（activate）」阶段的常见故障：设备进不去配网态、App 搜不到设备、配网后激活失败、连上路由器但云端不通、解绑/恢复出厂后无法重新配对。

涉及代码：

- 按键与快速复位配网：[`../../src/tuya_ai_toy_key.c`](../../src/tuya_ai_toy_key.c)
- 配网键长按复位、Wi-Fi 网络状态回调、LED 指示：[`../../src/tuya_ai_toy.c`](../../src/tuya_ai_toy.c)
- 授权信息（PID/UUID/AUTHKEY）、IoT 回调注册、网络与 MQTT 状态事件：[`../../src/tuya_app_main.c`](../../src/tuya_app_main.c)
- 引脚 Kconfig 默认值：[`../../src/boards/Kconfig`](../../src/boards/Kconfig)
- 带屏设备首启激活向导与屏上连网页：[`../../src/ui/pages/ui_page_activation.c`](../../src/ui/pages/ui_page_activation.c)、[`../../src/ui/pages/ui_page_wlan.c`](../../src/ui/pages/ui_page_wlan.c)

排查前建议：确认日志级别（`user_main()` 中已调用 `tal_log_set_manage_attr(TAL_LOG_LEVEL_DEBUG)`，默认即为 DEBUG 级别），通过串口终端或 tyuTool 查看设备实时日志。

## 现象速查

| 你遇到… | 看章节 |
|---|---|
| 长按配网键 / 反复上电，设备没有进入配网状态 | [1. 无法进入配网状态](#1-无法进入配网状态) |
| 设备已进入配网状态，但涂鸦智能 App 搜不到 / 添加不上 | [2. App 搜不到设备](#2-app-搜不到设备) |
| App 配网流程走完（输入了 Wi-Fi 密码），但最终提示激活失败 | [3. 配网成功但激活失败](#3-配网成功但激活失败) |
| 设备已连上路由器（能上网），但一直显示离线、对话不可用 | [4. 连上路由器但云端 MQTT 不通](#4-连上路由器但云端-mqtt-不通) |
| App 解绑或本地恢复出厂后，设备无法直接重新配对 | [5. 解绑/恢复出厂后重新配对](#5-解绑恢复出厂后重新配对) |

> **带屏设备先看这里**：开启新 UI 框架（`CONFIG_ENABLE_TUYA_UI=y`）的板型，未激活设备开机会自动进入**首启激活向导**（语言选择 → 激活方式 → 扫码激活，实现见 `src/ui/pages/ui_page_activation.c`），配网入口比无屏板多两条：
>
> - **APP 激活**（向导默认项）：设备回主页等待 App——AP+BLE 并发配网开机即在后台运行，与无屏板的 App 配网流程相同，本文第 1/2 节的排查照常适用；
> - **手动激活（扫码直连）**：先在屏上 WLAN 页（软键盘输入密码）连接路由器，再展示云端激活短链二维码，用涂鸦智能 App 扫码完成激活——**不依赖 AP/BLE 发现**，App 搜不到设备时可用这条路兜底；二维码一直不出来通常是 WLAN 未连通（回第 4 节查网络）或云端短链未就绪（检查 PID/授权，见第 3 节）。
>
> 向导上的「跳过」只是收起界面，不影响后台配网广播；扫码激活以日志/事件 `EVENT_POST_ACTIVATE` 为完成判据，仅 MQTT 连上（`EVENT_MQTT_CONNECTED`）不算激活完成。

---

## 1. 无法进入配网状态

### 排查步骤

1. **确认当前板型是否配了独立的「配网键」GPIO。** `TUYA_AI_TOY_NET_PIN_NUM` 在 [`../../src/boards/Kconfig`](../../src/boards/Kconfig) 中定义，**默认值是 64（禁用）**，只有 `T5AI_BOARD_EVB`（12）、`T5AI_BOARD_EVB_PRO`（7）、`T5AI_BOARD_ROBOT`（4）三种板型有默认配网键引脚；默认板型 `T5AI_BOARD`（以及 `T5AI_BOARD_DESKTOP`/`T5AI_BOARD_EYES`/`T2AI_BOARD`/`L511_*`）都没有配网键，`tuya_ai_toy_key_init()` 在 pin 为 `TUYA_GPIO_NUM_MAX` 时会直接跳过初始化（[`../../src/tuya_ai_toy_key.c`](../../src/tuya_ai_toy_key.c) `tuya_ai_toy_key_init()`）。这类板型上**长按配网键不会有任何反应是正常的**，需要改用下一条的「反复上电复位」方式。
2. **反复上电复位是否满足条件。** 复位配网逻辑在 [`../../src/tuya_ai_toy_key.c`](../../src/tuya_ai_toy_key.c)：设备每次上电，`__reset_netconfig_start()` 把持久化计数（uFILE `rst_cnt`）加一并启动一个 5 秒一次性定时器，5 秒后清零计数；只有在 **5 秒内连续上电达到 3 次**（`RESET_NETCNT_MAX`），下一次上电时 `__reset_netconfig_check()` 才会触发 `tuya_iot_wf_gw_fast_unactive(GWCM_OLD, WF_START_SMART_AP_CONCURRENT)` 进入配网。日志关键字：
   - `ai toy -> power up/down 3 times reset counter start`（每次上电都会打印）
   - `reset cnt clear!`（超过 5 秒未达到 3 次，计数被清零——说明上电间隔太长）
   - `Reset ctrl data!`（达到 3 次，即将触发配网）
   如果只看到反复出现 `reset cnt clear!` 而没有 `Reset ctrl data!`，说明每次断电/上电的间隔超过了 5 秒，需要更快速地断电重连。
3. **确认编译时是否打开了 Wi-Fi 服务。** `__reset_netconfig_check()` 里触发配网的 `tuya_iot_wf_gw_fast_unactive()` 调用被 `#if defined(ENABLE_WIFI_SERVICE) && (ENABLE_WIFI_SERVICE == 1)` 包裹；若该宏未开启（例如纯有线/蜂窝板型），计数复位逻辑不会真正触发 Wi-Fi 配网，需要按板型使用相应的联网方式配置流程。
4. **配网键本身按下是否有响应。** 若板型有配网键（EVB/EVB_PRO/ROBOT），长按阈值是 `LONG_KEY_TIME * 10 = 4000ms`（[`../../src/tuya_ai_toy.c`](../../src/tuya_ai_toy.c) `tuya_ai_toy_init()`），按下后日志应打印 `ai toy -> net pin pressed <type>`，长按（`type` 对应 `LONG_KEY`）应打印 `net pin long press trigger Reset!`。若完全没有 `net pin pressed` 日志，先排查 GPIO 硬件接线与电平（初始化为 `TUYA_GPIO_PULLUP` + 低电平触发 `low_detect=TRUE`，按下应拉低）。

### 解决办法

- 无配网键的板型：改用「5 秒内连续上电 3 次」的方式进入配网，注意断电间隔要短。
- 有配网键的板型：长按 4 秒以上，观察是否有 `net pin pressed`/`long press trigger Reset` 日志；无日志先查硬件接线，有日志但没进入配网态则继续看第 2 节。
- 若需要在自定义板型上启用/更换配网键引脚，在 `make app_menuconfig` 的 Pin Configuration 菜单里修改 `TUYA_AI_TOY_NET_PIN_NUM` 后重新 `make app_config`。

---

## 2. App 搜不到设备

### 排查步骤

1. **确认设备确实已经进入配网广播状态（见第 1 节），而不是仍处于「已配网/离线」态。** 网络状态回调 `__on_ai_toy_wf_nw_stat_cb()`（[`../../src/tuya_ai_toy.c`](../../src/tuya_ai_toy.c)）会打印 `network status = <nw_stat>, ...`；进入配网广播时 `nw_stat` 应为 `STAT_UNPROVISION_AP_STA_UNCFG`（smart+AP 并发配网中），此时 LED 会以 200ms 周期闪烁（`tuya_ai_toy_led_flash(200)`）。若一直停在 `STAT_LOW_POWER` 或其它值，说明配网根本没有被触发，回到第 1 节排查。
2. **确认应用侧启动模式。** `__soc_device_init()`（[`../../src/tuya_app_main.c`](../../src/tuya_app_main.c)）里设备以 `tuya_iot_wf_soc_dev_init(GWCM_OLD, WF_START_AP_FIRST, ...)` 启动，`WF_START_AP_FIRST` 表示 AP 与 Smart 配网都支持、默认 AP 优先。若 App 端选择的配网方式与设备当前实际广播方式不一致（如 App 走 Smart 配网、设备处于 AP 热点态），会导致互相搜不到。
3. **常见 Wi-Fi 环境限制（通用配网限制，非本仓库代码可验证，供参考）：**
   > **说明**：路由器是否为 2.4GHz 频段、SSID/密码是否含特殊字符或过长、是否开启了 AP 隔离、手机与设备是否在同一局域网——这些是涂鸦配网的通用前置条件，具体以 App 端提示与官方配网文档为准，本文不展开验证。

### 解决办法

- 先确认 LED 处于配网闪烁状态、日志显示 `STAT_UNPROVISION_AP_STA_UNCFG`，再打开 App 添加设备。
- 若设备处于 AP 热点态，App 内选择「AP 配网」/「热点配网」方式，手动连接设备热点后再配置。
- 确认手机连接的是 2.4GHz Wi-Fi、SSID/密码无特殊字符，必要时更换路由器测试。

---

## 3. 配网成功但激活失败

### 排查步骤（按可能性从高到低）

1. **PID 与授权信息（UUID/AUTHKEY）不匹配，是最常见原因。** demo 工程默认写死了示例 PID（[`../../src/tuya_app_main.c`](../../src/tuya_app_main.c) `#define PID "gcwfmdfkv6824tuh" // T5AI_BOARD_DESKTOP`），随代码附带的授权信息仅供跑通示例使用。代码注释明确提示：直接使用默认授权可能出现「多用户冲突」问题，必须替换为在涂鸦 IoT 开发者平台为自己产品申请的 PID + 授权码（免费申请两组，或购买授权，见 [快速开始](../quickstart.md) 「第一步：创建产品」「3.3 修改 PID 和授权信息」）。
2. **未走硬编码授权、依赖产测（MF）烧录授权，但产测未烧录或烧录信息与 PID 不匹配。** 当 `UUID`/`AUTHKEY` 未在代码中定义时，走的是 `mf_init()` 产测烧录授权分支（[`../../src/tuya_app_main.c`](../../src/tuya_app_main.c) `__soc_device_init()`），需确认涂鸦云模组工具已正确烧录该 PID 对应的授权信息。
3. **云端激活流程本身失败/超时。** 激活状态机在 SDK 组件 `svc_devos`（`tuya_svc_devos_activate.c`，不在本仓库路径下，不提供跳转链接）中实现，常见日志关键字：
   - `activate fail` —— 激活流程完整跑完但被判定失败（`tuya_svc_devos_activate_finish(FALSE)`）
   - `linkage not ready` —— 触发激活时网络链路还未就绪，会自动退避重试
   - `activate backoff` / `activate timeout` —— 按 `retry_cnt`（默认 10 次）退避重试，重试耗尽后上报 `EVENT_LINK_ACTIVATE` 失败
   - `result is null` —— 云端未返回有效激活结果，通常与 PID/授权不匹配或云端异常有关（解析函数要求返回数据必须包含 `secKey`/`localKey`/`devId`，否则直接判定解析失败）
4. **确认应用侧对应回调是否收到明确失败提示。** `__soc_dev_status_changed_cb()`（[`../../src/tuya_app_main.c`](../../src/tuya_app_main.c)）只打印 `SOC TUYA-Cloud Status:<status>`，本 demo 未对失败状态做额外处理，需要结合上面日志关键字定位具体阶段。

### 解决办法

- 优先检查并替换为自己产品的 PID + 授权信息（`../../src/tuya_app_main.c` 中的 `PID` 宏与 `UUID`/`AUTHKEY`，或对应的产测配置），这是目前已知最常见的激活失败原因。
- 若使用产测烧录授权，核对产测工具烧录的 PID 与工程编译使用的 PID 完全一致。
- 若日志停在 `linkage not ready`/`activate backoff`，先确认第 4 节的网络连通性，等网络恢复后设备会自动重试。
- 若持续 `result is null`，联系涂鸦 IoT 平台核实该 PID 的产品配置（schema/PID 状态）是否正常。

---

## 4. 连上路由器但云端 MQTT 不通

### 排查步骤

1. **区分「链路 up」和「MQTT connected」两个独立状态。** `__soc_dev_net_status_cb()`（[`../../src/tuya_app_main.c`](../../src/tuya_app_main.c)）在 `EVENT_LINK_UP`/`EVENT_LINK_DOWN`/`EVENT_MQTT_CONNECTED` 时都会触发，日志关键字：
   - `linkage status changed, current status is up` —— 仅表示网络层（拿到 IP）已通
   - 紧接着若打印 `mqtt is connected!` 才代表 MQTT 已建联；若只有前者没有后者，说明卡在了 MQTT 连接阶段。
2. **看 MQTT 客户端自身的状态迁移日志。** SDK 组件 `svc_tuya_cloud`（`tuya_svc_mqtt_client.c`，不在本仓库路径下）里 MQTT 状态变化会打印 `[<broker_domain>] mqtt state change <old> -> <new>`，反复在几个状态间跳变通常代表连接被拒绝或握手失败；`mqtt_ping err`/`respond timeout` 则代表已连上但心跳异常（网络不稳定/防火墙间歇性拦截）。
3. **常见根因排查方向：**
   - 设备系统时间是否正确：MQTT/TLS 建联前有云端时间同步逻辑（`mqc_app_time.c` 的 `mqc_app_get_cloud_time_sync()`），设备 RTC 严重偏差可能导致证书校验/握手异常。
   - DNS 解析是否正常：MQTT 连接失败会清空 DNS 缓存重试（`unw_clear_all_dns_cache()`），若网络环境 DNS 异常或强制走了自定义 DNS，会反复触发重连。
   - 网络出口是否放通了涂鸦云 MQTT 服务所需的域名/端口：
     > **说明**：具体域名列表、端口号（TLS/明文）以当前 SDK 与云端环境为准，本文不列出具体值，请以涂鸦 IoT 平台文档或工单为准。

### 解决办法

- 先确认日志能看到 `linkage status changed, current status is up`，若没有该行，问题在网络层（路由器/DHCP），不属于 MQTT 问题。
- 若有「链路 up」但没有「mqtt is connected」，检查设备系统时间、DNS、以及网络出口是否有防火墙/内容过滤拦截 MQTT 流量。
- 若日志显示 `mqtt state change` 反复跳变但从未稳定，考虑更换网络环境（如手机热点）做对比测试，排除当前路由器/局域网限制。

---

## 5. 解绑/恢复出厂后重新配对

### 排查步骤

1. **明确「App 解绑」和「本地/远程恢复出厂」是两种不同的复位类型，但结论一致：都需要重新走一遍配网流程。** 复位类型定义在 SDK 头文件 `tuya_cloud_com_defs.h`（`GW_RESET_TYPE_E`，不在本仓库路径下）：`GW_REMOTE_UNACTIVE`（App 端解绑）、`GW_LOCAL_RESET_FACTORY`/`GW_REMOTE_RESET_FACTORY`（恢复出厂）、`GW_RESET_DATA_FACTORY`（恢复出厂且需额外清本地数据）。二者的区别只在于**本地数据清除的深度**（恢复出厂通常会额外清除设备本地保存的历史数据，解绑则不一定），而不在于是否需要重新配网——**绑定关系一旦解除，云端必须由 App 重新下发新的 token 才能重建「用户-设备」绑定关系，设备本地保存的旧 token 已失效**，因此两种情况下设备复位后都会回到未激活状态，必须由 App 重新执行一遍添加设备/配网流程。
2. **确认设备复位后确实回到了未配网状态，而不是卡在中间态。** 复位完成后应能在日志中看到 `nw_stat` 回落（见第 2 节 `network status = ...` 日志），进而按第 1 节的方式重新触发配网。若复位后 LED/日志没有变化，可能是复位回调没有被正确触发。
3. **本 demo 当前对复位类型不做区分处理。** `__soc_dev_reset_inform_cb()`（[`../../src/tuya_app_main.c`](../../src/tuya_app_main.c)）收到 `GW_RESET_TYPE_E type` 后仅打印 `reset type <type>` 并在开启 KV 缓存时强制同步一次缓存，**没有按 type 区分「解绑」与「恢复出厂」做不同的本地数据清理**。若产品需要区分二者的本地数据清除范围（如仅恢复出厂才清除历史记录），需要在此回调里自行按 `type` 分支扩展。

### 解决办法

- 解绑或恢复出厂后设备无法直接重新绑定是预期行为：需要用 App 重新执行一遍「添加设备」的配网流程（回到第 1、2 节），而不是尝试跳过配网直接绑定。
- 若产品需要「解绑保留部分本地数据、恢复出厂清空全部数据」这类差异化处理，需要在 `__soc_dev_reset_inform_cb()` 中按 `GW_RESET_TYPE_E` 的具体取值扩展逻辑，当前 demo 未实现。

---

## 相关文档

- [快速开始](../quickstart.md)：创建产品、获取 PID 与授权信息、烧录固件的完整流程。
- [整体架构](../architecture.md)：按键与快速复位配网在整体初始化链路中的位置。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
