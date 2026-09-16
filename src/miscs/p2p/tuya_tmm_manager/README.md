# tuya_tmm_manager

## 概述

`tuya_tmm_manager` 是 TMM VoIP 通话的编排层，位于信令模块 [tuya_tmm_control](../tuya_tmm_control/README.md) 与媒体传输模块 [tuya_tmm_stream](../tuya_tmm_stream/README.md) 之上，把两者组合成设备侧统一的呼叫状态机。三者是同一套 P2P VoIP 栈的三层：control 只管信令 JSON/MQTT protocol 308 收发；stream 只管媒体推拉流、编解码声明与环形缓冲；manager 是对外唯一入口——应用层（`src/miscs/p2p/tuya_p2p_tmm_voip.c`）只调用 manager 的 API，不直接触碰 control/stream。

manager 持有呼叫状态，通过 `tuya_tmm_control_evt_cb` 消费信令事件驱动状态迁移；需要建立/控制媒体通道时，把命令投递到内部消息队列，由独立线程串行调用 `tuya_tmm_stream_connect/disconnect/ctrl`。

除通话本身外，`tuya_tmm_manager_atop.c` 还封装了一套与呼叫状态机无关的通讯录/房间云端接口（`thing.ai.contact.*`），供 NFC 加好友、临时组网等场景使用。

## 设计要点与坑

- **状态机**：`TUYA_TMM_MANAGER_STA_E` —— IDLE/INCOMING/CALLING/P2P_CONNECTING/P2P_CONNECTED（`P2P_DISCONNECTING` 保留未使用）。对端各类信令事件（accepted/reject/busy/cancel/hangup/unanswered/error）都会把状态收敛回 IDLE。呼入时若 `target_id` 带 `key_*` 前缀说明对端是 App，`__tmm_need_p2p_connect` 会判定跳过 P2P connect、走云端 RTC 通道；这个判断在 manager 和 stream 两层都各自做了一次防御性校验，改协议要两处同步。
- **两条工作线程**：manager task 线程（串行消费内部 queue，执行 CONNECT/DISCONNECT/TYPE_CHANGE/MIC_CHANGE）与 40ms 匀速音频发送线程（`__tmm_audio_send_task`）。二者共享 `lock`（保护状态/`stream_conf`/`peer_name`）与独立的 `audio_jitter_lock`（只保护音频环形缓冲）。反初始化顺序是硬约束：先置两个线程的运行标志为 FALSE、等音频线程退出，再 sleep 600ms 等 task 线程自删除，最后才能释放 jitter 缓冲/锁——提前释放会和仍在跑的发送线程竞态导致 use-after-free。
- **上行音频走 jitter 缓冲**：上游按约 80ms 一个大包喂入（`tuya_tmm_manager_data_feed`），而 P2P 发送侧按 40ms 取一帧，两者节拍不匹配会导致周期性取空。缓冲容量 240ms（6 帧），由独立的 40ms 线程匀速取送吸收错配；发送线程用绝对时间锚点累加下一次发送时刻（而非相对 `sleep`），避免长通话时因调度误差累积导致节拍漂移。
- **发送门控不依赖呼叫状态**：`send_audio_enable`（媒体通道已起）与 `stream_conf.enable_send_audio`（麦克风未静音）是两个独立开关，音频发送/喂入都只看它们，不看 `status`——因为 IPC 预览式视频对讲（App 直接拉流，不走 TMM 呼叫流程）时 `status` 恒为 IDLE，若发送门控依赖 `status` 则永远不会发送。
- **ATOP 封装是另一套独立 API**：通讯录同步/申请/查询/删除 + 房间创建/加入/查询成员/批量申请/退出，供联系人 UI 使用，与呼叫状态机无耦合。遗留的 `tuya_tmm_manager_get_dev_rtc_list`（走 `tuya.device.rtc.callable.list`）仅为兼容旧接口保留，新功能应使用 `tuya_tmm_manager_atop_contact_sync_list`。

## 改动指南

- 改呼叫状态机/事件语义：`tuya_tmm_manager.c` 中的 `tuya_tmm_control_evt_cb` 与 `TUYA_TMM_MANAGER_STA_E`。
- 改音频上行节拍/jitter 缓冲策略：`TMM_AUDIO_*` 系列宏 + `__tmm_audio_send_task`。
- 改通讯录/房间云端接口：`tuya_tmm_manager_atop.c` / `tuya_tmm_manager_atop.h`。
- 新增呼叫触发入口：唯一调用方是 `src/miscs/p2p/tuya_p2p_tmm_voip.c`，从这里接入，不要绕过 manager 直接调用 control/stream。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
