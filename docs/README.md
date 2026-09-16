# 文档地图

按「你想做什么」查找文档。本页是全部文档的唯一入口。

## 上手

| 你想… | 看这篇 |
|---|---|
| 从零编译、烧录、跑起来 | [快速开始](quickstart.md) |
| 理解整体架构、初始化链路、低功耗行为 | [整体架构](architecture.md) |
| 了解各版本变更与升级注意 | [版本说明](../CHANGELOG_CN.md) |

## 任务指南（howto/）

按真实开发任务组织，按客户高频问题持续补充。

| 你想… | 看这篇 |
|---|---|
| 适配一块新板子 | [适配新板](howto/porting-new-board.md) |
| 新增一个 AI 技能 | [新增 AI 技能](howto/add-ai-skill.md) |
| 更换唤醒词、调整 KWS 灵敏度 | [更换唤醒词](howto/customize-wakeword.md) |
| 外挂 GX8006/CI1302 等离线语音芯片（芯片管唤醒拾音，模组管 AI 对话） | [对接 UART 离线语音芯片](howto/connect-uart-voice-chip.md) |
| 新增或裁剪一个对话模式 | [新增或裁剪对话模式](howto/add-dialog-mode.md) |
| 新增一个 MCP 工具，让 AI 能调用新能力 | [新增 MCP 工具](howto/add-mcp-tool.md) |
| 定制 UI（改页面 / 加页面 / 板级自定义界面） | [定制 UI](howto/customize-ui.md) |
| 用 TEF 替代 GIF 高效存储与播放动图（表情/动效） | [TEF 动图](howto/use-tef-animation.md) |
| 量产/试产时触发整机产测（热点触发、定制产测 SSID） | [成品产测](howto/factory-test.md) |
| 更换/新增本地提示音（叮咚声等） | [提示音资源目录](../src/wukong/assets/README.md)（模块 README） |

## 排障指南（troubleshooting/）

| 你遇到… | 看这篇 |
|---|---|
| 语音识别/打断/回声异常，需要调音频算法 | [语音算法调试](troubleshooting/audio-tuning.md) |
| 音乐/TTS 播放断音、碎卡、起播慢 | [播放卡顿排查](troubleshooting/audio-playback-stutter.md) |
| 抓取上行 5 路音频流到 SD 卡分析（AEC 残留、唤醒、上传音质） | [音频上行 5 路抓流](troubleshooting/audio-stream-capture.md) |
| 硬件声学结构（麦克风/喇叭布局）测试验收 | [硬件声学结构测试](troubleshooting/acoustic-test.md) |
| 设备崩溃/重启，需要看懂串口日志、定位到源码行 | [T5 崩溃日志定位入门](troubleshooting/crash-analysis.md) |
| 配网进不去、App 搜不到设备、激活失败、解绑/恢复出厂后无法重配 | [配网/激活失败排查](troubleshooting/network-pairing.md) |

## 模块参考

改某个模块前先读它目录内的 README（职责、设计要点与坑、改动指南都在那里），本表是全部入口。

**AI 核心（src/wukong/）**

| 模块 | 说明 |
|---|---|
| [wukong 总览](../src/wukong/README.md) | AI 对话、音频、技能、MCP 等核心的总入口 |
| [audio](../src/wukong/audio/README.md) | 音频输入输出、AEC/VAD、播放控制 |
| [kws](../src/wukong/audio/frontend/kws/README.md) | 关键词唤醒 |
| [mcp](../src/wukong/toolkits/mcp/README.md) | MCP 端侧工具，让 AI 调用设备能力 |
| [skills](../src/wukong/toolkits/fc/README.md) | 音乐故事、情绪、云事件等 AI 技能 |
| [picture](../src/wukong/picture/README.md) | AI 生图/识图、相册管理 |
| [video](../src/wukong/video/README.md) | 视频输入输出处理 |
| [cron](../src/wukong/cron/README.md) | 本地定时任务与 JSON-RPC 调度 |
| [tm](../src/wukong/tm/README.md) | 闹钟、提醒、倒计时、正计时、番茄时钟统一入口 |
| [storage](../src/wukong/storage/README.md) | 存储层：外部介质（SD 卡/QSPI Flash）统一挂载 + 记录读写 |
| [assets](../src/wukong/assets/README.md) | 提示音等内置资源 |

**交互**

| 模块 | 说明 |
|---|---|
| [ui](../src/ui/README.md) | Vue-inspired LVGL 响应式 UI 框架（`ENABLE_TUYA_UI`） |
| [mode](../src/mode/README.md) | 对话模式（hold/oneshot/wakeup/free + translate/p2p/record/picture/detection） |
| [boards](../src/boards/README.md) | 板级 BSP 与各开发板说明 |

**平台组件（src/miscs/）**

| 模块 | 说明 |
|---|---|
| [tef](../src/miscs/tef/README.md) | GIF→TEF 动图编码工具链 + 免 LVGL 播放器（用法见 [TEF 动图](howto/use-tef-animation.md)） |
| [audio_player](../src/miscs/audio_player/README.md) | AI 播放器 svc_ai_player：mem/url/file 三数据源 → 解码 → 重采样/混音 → 输出 |
| [uart_codec](../src/miscs/uart_codec/README.md) | 离线语音模组（GX8006、CI1302 及同协议型号）的 UART 协议与业务管理（接入见 [对接 UART 离线语音芯片](howto/connect-uart-voice-chip.md)） |
| [svc_media_alg](../src/miscs/svc_media_alg/README.md) | IPC 移植的图像/视频算法集，当前接入移动侦测 |
| [p2p/tuya_tmm_manager](../src/miscs/p2p/tuya_tmm_manager/README.md) | TMM VoIP 通话编排层，对外唯一入口（三层分工见此篇） |
| [p2p/tuya_tmm_control](../src/miscs/p2p/tuya_tmm_control/README.md) | TMM VoIP 信令层（MQTT protocol 308） |
| [p2p/tuya_tmm_stream](../src/miscs/p2p/tuya_tmm_stream/README.md) | TMM VoIP 媒体传输层（P2P 推拉流） |

**硬件驱动（src/drivers/）**

| 模块 | 说明 |
|---|---|
| [app_tuya_key](../src/drivers/app_tuya_key/README.md) | 按键检测与事件上报（单击/连击/长按），软件去抖 |
| [app_tuya_led](../src/drivers/app_tuya_led/README.md) | LED 常亮/常灭/闪烁的多路管理 |
| [app_tuya_imu](../src/drivers/app_tuya_imu/README.md) | SC7A20 加速度计驱动 + 姿态（倾斜）检测 |
| [app_tuya_driver](../src/drivers/app_tuya_driver/README.md) | GPIO/UART 的 TAL 层实现与触摸 TKL 封装 |
| [tal_audio](../src/drivers/app_tuya_audio/tal_audio/README.md) | 统一音频采集/播放接口 |
| [tal_uvc](../src/drivers/app_tuya_camera/tal_uvc/README.md) | UVC 摄像头抽象层 |

## 资源统计

线程清单（优先级 / 栈大小 / 创建条件）、固件静态 DATA/BSS/ROM 统计方法（`tuyaos_statistics_ai.sh`）与运行时观测手段，见[资源统计](resource-usage.md)。

## 文档约定

写文档/改文档前请读 [DOC_POLICY](DOC_POLICY.md)。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
