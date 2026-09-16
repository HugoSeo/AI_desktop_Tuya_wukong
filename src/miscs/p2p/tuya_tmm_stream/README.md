# tuya_tmm_stream

## 概述

`tuya_tmm_stream` 是 TMM VoIP 的媒体传输层，封装涂鸦 IPC 媒体栈（media adapter / media stream / `tuya_tmm_link`）与 P2P 客户端（`tuya_ipc_p2p_client_*`），对上只暴露 connect/disconnect/ctrl/audio_send/video_send。它是 [tuya_tmm_manager](../tuya_tmm_manager/README.md) 之下的媒体子模块，manager 是唯一调用方；整体架构（与 [tuya_tmm_control](../tuya_tmm_control/README.md) 信令层的分工）见 manager 的 README。

初始化时订阅 MQTT 已连事件来触发耗时的媒体栈初始化（注册 media adapter/media stream、`tuya_tmm_link` 编解码器、环形缓冲），避免阻塞设备启动流程；这段初始化在独立线程中执行，且做了幂等保护（事件触发与"MQTT 已连补偿"路径可能双触发）。

## 设计要点与坑

- **P2P session 用固定数组管理**（`tuya_tmm_stream_link.c`，`TMM_STREAM_MAX_SESSION_NUM=10`），按 `dev_id` 索引，一次只用一个；同 `dev_id` 重连会先断开旧连接。App 目标（`key_*` 前缀）或目标 id 等于自身设备 id 会被 `tuya_tmm_stream_link_connect` 直接拒绝——P2P 直连只用于设备对设备通话。
- **上行音频编码用 G.711 μ-law（8bit，约 8KB/s）而非 PCM（16bit，16KB/s）**：为减半上行带宽、缓解 relay 单 TCP 双向复用下的拥塞延迟。下行声明的解码能力同样对齐 G.711 μ-law@8k（64kbps）——曾经的移植版本误声明 PCM@16k（256kbps），导致 App 端按 4 倍带宽下发裸流，对讲时上行 KCP 立即拥塞。
- **环形缓冲 `max_buffer_seconds` 故意设为 0**（不做多秒缓冲）：曾设为 4s，拥塞时会积压数秒延迟；改 0 后拥塞时直接丢旧帧、控住时延，而不是攒着放大延迟。
- **G711 解码输出缓冲按长度关系校验**：解码后字节数 = 输入字节数 × 2，写入固定大小的静态缓冲（`__tmm_media_recv_audio_cb` 里的 `audio_buf[2048]`）前必须按此校验，否则会栈缓冲区溢出。
- **`low_power=TRUE`** 向 P2P 栈声明本机是低功耗设备，影响保活/唤醒策略；早期移植版本误置为 `FALSE`，是曾经踩过的坑。
- **视频帧不经过 jitter 缓冲**：由上层相机编码器按其自身节拍直接写入环形缓冲（`tuya_tmm_stream_video_send`）；音频侧的节拍匀速化在 manager 层完成，不在本模块。

## 改动指南

- 改编解码格式/码率/分辨率：`__tmm_ipc_media_adapter_set_media_info` 与 `__tmm_link_init` 里的 `encoder_info`/`decoder_info`，两处编解码声明需要保持一致。
- 改 P2P session 管理/媒体开关时序：`tuya_tmm_stream_link.c`。
- 改环形缓冲策略（缓冲时长/丢帧行为）：`__tmm_ringbuf_init` 的 `RING_BUFFER_INIT_PARAM_T`。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
