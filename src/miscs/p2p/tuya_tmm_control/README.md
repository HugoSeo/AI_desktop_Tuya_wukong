# tuya_tmm_control

## 概述

`tuya_tmm_control` 是 TMM VoIP 通话的信令层：基于 MQTT 自定义协议号 308，使用与 SS190（涂鸦另一套成熟对讲方案）兼容的 JSON 格式收发 `call`/`answer`/`reject`/`cancel`/`hang_up`/`busy`/`stop`/`heartbeat`/`ring` 等信令事件。它是 [tuya_tmm_manager](../tuya_tmm_manager/README.md) 之下的信令子模块，manager 是唯一调用方；整体架构（与 [tuya_tmm_stream](../tuya_tmm_stream/README.md) 媒体层的分工）见 manager 的 README。

`tuya_tmm_control_init` 注册的事件回调把信令事件交给 manager，由 manager 转换成自己的呼叫状态；本模块只维护一套精简状态（IDLE/CALLING/INCOMING/ACCEPTED）用于超时和心跳判定，不感知媒体层。

## 设计要点与坑

- **后台轮询任务**（2s 周期）做两件事：MQTT 连接后上报 RTC 能力（`rtcCapability:7`，走 `tuya.device.meta.save`，只报一次）；以及状态超时判定——CALLING/INCOMING 超过 `call_timeout`（初始化时传入，默认 30s；manager 实际传的是 180s）判未接通，ACCEPTED 状态超过 30s（`TMM_CONTROL_HEART_BEAT_TIMEOUT`）未收到对端心跳判异常并上报 ERROR。
- **App 目标与设备目标走不同的 call JSON 格式**：`targetId` 带 `key_*` 前缀时使用带 `callType:3` 和 `bizType:dgnzk` 的格式，云端据此路由到 App RTC 通道而非设备直连 P2P。manager 层对这两类目标后续是否发起 P2P connect 的判断（`__tmm_need_p2p_connect`）与这里的格式选择必须保持一致，改协议格式时两处要同步。
- **MQTT 收到的信令 JSON 是不可信输入**：`event`/`sessionId` 等字段必须同时判空指针存在与 `valuestring` 非空才能 `strcmp`，否则空指针崩溃（现有代码已加保护，新增字段解析须遵循同样写法）。
- **`call` 事件靠 `timeout` 字段值去重**（`last_time == timeout` 直接丢弃），是历史遗留的简易去重手段，不是通用幂等设计；扩展信令类型时不要假设它能防住所有重复投递。
- **心跳只在 ACCEPTED 状态双向进行**：收到对端心跳会回复心跳并刷新计时；非通话状态收到心跳会被忽略。
- **`tuya_tmm_control_deinit` 未实现**（恒返回 `OPRT_NOT_SUPPORTED`）——模块生命周期与设备进程等长，不支持运行时卸载。

## 改动指南

- 改信令 JSON 字段/协议格式：`TMM_CONTROL_*_FORMAT_STR` 宏与 `__tmm_control_mqc_proto_cb` 中对应分支，两处要同步改。
- 改超时/心跳时长：`TMM_CONTROL_HEART_BEAT_TIMEOUT`，以及 `tuya_tmm_control_init` 的 `call_timeout_s` 入参（当前由 manager 传入 180s）。
- 新增信令事件类型：`TUYA_TMM_CONTROL_EVT_E`（头文件）+ `__tmm_control_mqc_proto_cb` 分支 + manager 侧 `tuya_tmm_control_evt_cb` 对应处理，三处同步。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
