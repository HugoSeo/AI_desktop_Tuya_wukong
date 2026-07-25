<p align="center"><a href="https://tuyaos.com" target="_blank"><img src="https://images.tuyacn.com/fe-static/docs/img/4730c2f5-dd04-4438-a5ac-c8653d0db86b.png" width="400"></a></p>

## 概述

`tuyaos_demo_wukong_ai` 是基于 TuyaOS 的 Wukong AI 参考工程，适用于 AI 玩具、AI 机器人、AI 家电、AI 消费电子等场景。

工程主能力包括：

- 多种 AI 对话模式：长按、单次按键、关键词唤醒、自由对话、P2P、翻译
- 多模态输入输出：音频、图像、视频
- 端侧 MCP 能力
- 本地定时任务与闹钟
- KWS、AEC、VAD、播放器等音频能力

## 适用场景

Wukong AI 硬件开发框架广泛应用于 **AI 玩具**、**AI 机器人**、**AI 家电**、**AI 消费电子**等各种品类的产品开发。本示例可作为上述场景的参考实现，便于快速理解框架能力并在此基础上进行二次开发。

## 快速开始

从零到编译、烧录与运行，请按步骤阅读 **[快速开始](https://developer.tuya.com/cn/docs/iot-device-dev/quick-start?id=Kectxdshpvsqr)**。

## 源码与文档结构

以下为源码目录与文档入口的对应关系，点击链接可进入各模块说明。

## src 顶层 C 文件

当前 `src/` 顶层核心 `.c` 文件主要如下：

| 文件 | 角色 | 说明 |
|------|------|------|
| `tuya_app_main.c` | 应用总入口 | 创建应用主线程，完成 Tuya IoT、网络、授权、板级/UI、AI toy 总初始化 |
| `tuya_ai_toy.c` | AI toy 主控制器 | 管理 AI 运行态、事件订阅、模式调度、音频链路、`cron` / `alarm`、idle / lowpower timer |
| `tuya_ai_toy_key.c` | 按键与快速复位配网 | 处理按键驱动、上电次数复位配网、Ubuntu 模拟按键 |
| `tuya_ai_toy_led.c` | LED 指示灯封装 | 对 LED 驱动做简单封装，提供亮/灭/闪烁能力 |
| `tuya_ai_toy_camera.c` | 摄像头与 LCD 显示桥接 | 初始化相机、注册帧回调、完成 JPEG 捕获、LCD 刷新、P2P 视频推流 |

顶层关系可以简化理解为：

```text
tuya_app_main.c
    └─ 系统与 IoT 框架入口
       └─ tuya_ai_toy.c
          ├─ key / led / camera 等顶层设备封装
          ├─ wukong 核心能力
          ├─ mode 状态机
          └─ boards / drivers / miscs 提供的板级与底层能力
```

## 初始化流程

### 应用入口

`tuya_app_main.c` 是整个应用的最外层入口，主流程如下：

```text
main() / tuya_app_main()
    -> 创建 tuya_app_thread
    -> tuya_base_utilities_init()
    -> user_main()
    -> __soc_device_init()
    -> tuya_ai_toy_init()
```

它主要负责：

- 初始化基础运行环境
- 初始化 Tuya IoT 参数与本地数据库
- 注册升级、DP、网络状态、复位等 IoT 回调
- 根据编译配置初始化 Wi-Fi / BLE / 有线 / 蜂窝能力
- 初始化板级、UI 与 AI toy

### 设备初始化阶段

`__soc_device_init()` 是应用层和 TuyaOS / IoT 框架的汇合点，主要负责：

- 订阅 `EVENT_LINK_UP` / `EVENT_LINK_DOWN` / `EVENT_MQTT_CONNECTED`
- 处理软件授权或产测烧录授权
- 配置 Wi-Fi 低功耗参数，例如 DTIM
- 初始化板级能力和 UI
- 最终调用 `tuya_ai_toy_init()`

### AI toy 初始化阶段

`tuya_ai_toy.c` 中 `tuya_ai_toy_init()` 是运行态控制中心，主要完成：

- 分配并初始化 `s_ai_toy` 上下文
- 读取本地音量、触发模式配置
- 初始化 LED、按键、摄像头、电池、蜂窝等外围能力
- 创建 `idle_timer` 与 `lowpower_timer`
- 调用 `__ai_toy_start()` 进入 AI 工作态

### AI 工作态启动

`__ai_toy_start()` 会把真正跑起来的 AI 子模块串起来：

- 订阅 OTA、AI client、KWS、VAD、RESET 等事件
- 初始化 `wukong_ai_agent`、音频输入、播放器、KWS
- 注册 Wi-Fi 网络状态回调
- 初始化 MCP
- 初始化 `wukong_cron`
- 初始化 `wukong_time_manage`
- 在时间可用时调用 `wukong_cron_time_ready_notify()`
- 初始化 `wukong_ai_mode`
- 创建 `ai_toy_state` 线程循环执行 `wukong_ai_mode_dispatch(AI_MODE_OP_TASK, ...)`

## 低功耗流程

### 启动阶段低功耗参数配置

`tuya_app_main.c` 在 `__soc_device_init()` 里会做一次 Wi-Fi 低功耗参数配置：

- `tal_cpu_set_lp_mode(TRUE)`
- `tal_wifi_set_lps_dtim(3)`
- 然后默认先 `tal_cpu_lp_disable()` / `tal_wifi_lp_disable()`

这一步更像是预配置低功耗参数，并不是一启动就直接进入低功耗。

### 运行阶段 idle / lowpower timer

真正的运行时低功耗控制在 `tuya_ai_toy.c` 里：

- `idle_timer`
  - 超时后如果没有播放，就通知 `mode` 进入 idle 状态
  - 如果仍在播放，就重启 idle timer
- `lowpower_timer`
  - 超时后如果没有播放，则根据 `TY_AI_DEFAULT_LOWP_MODE` 进入轻睡眠或深睡眠
  - 如果仍在播放，就重启 lowpower timer

流程可简化为：

```text
正常运行
  -> idle_timer 到期
     -> 无播放: 通知 mode 进入 idle
     -> 有播放: 继续重置 timer

idle 持续一段时间
  -> lowpower_timer 到期
     -> 深睡眠: 配置 wakeup source 后进入 deep sleep
     -> 轻睡眠: 停止 AI 运行态，关闭外设，开启 CPU/Wi-Fi LP
```

### 低功耗唤醒恢复

按键触发 `__on_ai_toy_key_press_exit_lowpower()` 后，会执行恢复流程：

- 关闭 CPU / Wi-Fi 低功耗
- 打开背光、扬声器、LED 等外围
- 重新执行 `__ai_toy_start()`
- 重新初始化 AI 工作态

当前轻睡眠恢复本质上是“停掉 AI 工作态，再重新拉起”。

### 功能模块

| 域 | 文档 | 说明 |
|----|------|------|
| **src 顶层** | [应用入口与流程说明（详细版）](src/docs/cron/README_CN.md) | 顶层入口的详细补充说明 |
| **wukong** | [Wukong AI 核心](src/wukong/README_CN.md) | AI 对话、音频、模式、技能、MCP 等核心 |
| ├─ audio | [音频模块](src/wukong/audio/README_CN.md) | 音频输入输出、AEC/VAD、播放器 |
| ├─ kws | [KWS 模块](src/wukong/kws/README_CN.md) | 关键词唤醒 |
| ├─ mcp | [MCP 服务器](src/wukong/mcp/README.md) | Model Context Protocol 端侧能力 |
| ├─ assets | [资源](src/wukong/assets/README_CN.md) | 提示音等资源 |
| ├─ cron | [cron 模块](src/wukong/cron/README_CN.md) | 本地定时任务与 JSON-RPC 调度 |
| ├─ tm | [时间管理模块](src/wukong/tm/README_CN.md) | 闹钟、提醒、倒计时、正计时、番茄时钟统一入口 |
| **mode** | [模式模块](src/mode/README_CN.md) | 对话模式（hold/oneshot/wakeup/free/p2p/translate） |
| **boards** | [板级支持](src/boards/README_CN.md) | 板级 BSP 与各开发板说明 |
| **miscs** | — | 功能模块层：GUI、音频播放、uart_codec 等（详见各子目录） |
| **drivers** | — | 驱动层：摄像头、显示、按键、LED、触摸、IMU 等（详见各子目录） |

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
