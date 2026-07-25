# 桌面机器人通话功能技术文档（视频通话 + 语音通话）

## 1. 概述

通话功能（P2P Call）基于涂鸦 P2P SDK，支持设备与手机 APP 之间建立实时点对点音视频通话。设备既可主动呼叫 APP，也可接收 APP 发起的通话请求。通话期间自动切换为 P2P 设备模式，结束后恢复原模式。

- **视频通话**：摄像头 YUV → H.264 编码 → 推流至 APP；APP 视频流当前仅接收不渲染（预留接口）。
- **语音通话**：麦克风 PCM 16kHz → 重采样至 8kHz → G.711 μ-law 编码 → 推流；APP 下发 G.711 → 解码 → 重采样至 16kHz → 扬声器播放。

**功能特性：**
- 设备端主动呼叫 APP（桌面 UI 通话屏）
- APP 端发起语音/视频通话，设备自动接受
- 通话期间自动暂停 AI 聊天（TTS/音乐），通话结束后自动恢复
- 双向音频：G.711 μ-law @ 8kHz，设备内部采集/播放均为 16kHz，自动双向重采样
- 视频推流：H.264，480×480，25fps，1 Mbps
- 最大并发 1 路 P2P 连接
- 屏幕端实时显示通话状态（"呼叫中…" / "通话中…"）

**相关核心文件：**

| 文件 | 说明 |
|------|------|
| `src/miscs/p2p/tuya_p2p_app.c` | P2P 应用层：事件回调、音视频帧处理、启动流程 |
| `src/miscs/p2p/tuya_p2p_sdk.c` | P2P SDK 初始化、媒体适配器、RingBuffer 管理 |
| `src/miscs/p2p/tuya_sdk_call.c` | 通话控制接口（呼叫/接听/挂断） |
| `src/mode/wukong_ai_mode_p2p.c` | P2P 设备模式状态机与音频路由 |
| `src/boards/T5AI_BOARD_DESKTOP/ui/desk_func_call.h/.c` | 桌面通话 UI |
| `include/tuya_app_config.h` | 配置参数定义 |

---

## 2. 编译配置

```c
#define ENABLE_MQTT_P2P        1    // 启用 P2P 通话功能（总开关）
#define ENABLE_AI_MODE_P2P     1    // 启用 P2P 设备模式
#define ENABLE_TUYA_CAMERA     1    // 启用摄像头（视频通话必须）
```

> 所有 P2P 相关代码均包裹在 `#if defined(ENABLE_MQTT_P2P) && (ENABLE_MQTT_P2P == 1)` 中。

---

## 3. 媒体配置参数

### 3.1 视频流

| 参数 | 值 | 说明 |
|------|-----|------|
| 流路 | `E_IPC_STREAM_VIDEO_MAIN` | 主视频流 |
| 分辨率 | 480 × 480 | 与摄像头 ISP 输出一致 |
| 编码格式 | H.264（`TUYA_CODEC_VIDEO_H264`） | |
| 帧率 | 25 fps | |
| GOP | 25 | I 帧间隔（1 秒一个关键帧） |
| 码率 | 1 Mbps（`TUYA_VIDEO_BITRATE_1M`） | |
| RTP 时钟 | 90000 Hz | 标准视频 RTP 时钟频率 |

### 3.2 音频流

| 参数 | 值 | 说明 |
|------|-----|------|
| 流路 | `E_IPC_STREAM_AUDIO_MAIN` | 主音频流 |
| 传输编码 | G.711 μ-law（`TUYA_CODEC_AUDIO_G711U`） | 网络传输格式 |
| 传输采样率 | 8 kHz（`TUYA_AUDIO_SAMPLE_8K`） | |
| 位深 | 16 bit（`TUYA_AUDIO_DATABITS_16`） | |
| 声道 | 单声道（`TUYA_AUDIO_CHANNEL_MONO`） | |
| 帧率 | 25 fps（每帧 40ms） | |

### 3.3 连接参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 最大并发客户端 | 1 | `max_client_num` |
| 接收缓冲区 | 16 KB | `recv_buffer_size` |
| 低功耗模式 | 启用（`low_power=1`） | |
| 传输模式 | `TRANS_DEFAULT_STANDARD` | |
| 呼叫超时 | 30 秒 | 超时自动挂断 |

---

## 4. 启动与初始化流程

```
应用启动（tuya_ai_toy.c）
    │
    ├─→ tuya_ai_toy_camera_init()          // 摄像头初始化
    │
    ├─→ tuya_p2p_app_start()               // 订阅事件，注册 P2P 回调
    │    ├─ 订阅 EVENT_POST_ACTIVATE       → __event_active_cb()（更新 P2P 密码）
    │    ├─ 订阅 EVENT_LINK_UP             → __ipc_start_cb()（创建 P2P 线程）
    │    └─ 若网络已连接/已激活：立即补偿触发（竞态保护）
    │
    └─→ TUYA_IPC_call_init()               // TMM 通话中间件初始化
         └─ tuya_tmm_control_init(tmm_control_evt_cb, NULL, 30)  // 30s 超时
    │
    │ [网络 Link Up 事件]
    │
    └─→ __ipc_start_cb()
         └─→ tuya_ipc_thread（栈 8192 B，THREAD_PRIO_2）
              └─→ tuya_ipc_app_start()
                   ├─ 配置视频/音频媒体参数
                   ├─ tuya_p2p_sdk_init(&sdkVar)  // 初始化 P2P SDK
                   └─ 初始化音视频 RingBuffer
```

---

## 5. P2P 事件处理

所有 P2P 媒体事件由 `__tuya_ipc_p2p_event_cb()` 统一处理（`tuya_p2p_app.c`）：

| 事件 | 语音/视频 | 处理流程 |
|------|---------|---------|
| `MEDIA_STREAM_LIVE_VIDEO_START` | 视频通话开始 | 保存当前模式 → 停止所有播放 → 中断 AI 聊天 → 切换 P2P 模式 → 摄像头切 H.264 → 启动 H.264 编码 |
| `MEDIA_STREAM_LIVE_VIDEO_STOP` | 视频通话结束 | 停止所有播放 → 中断 AI 聊天 → 恢复原模式 → 摄像头切回 JPEG → 停止 H.264 编码 |
| `MEDIA_STREAM_LIVE_AUDIO_START` | 语音通话开始 | 保存当前模式 → 停止所有播放 → 中断 AI 聊天 → 切换 P2P 模式 → 发布 `TUYA_IPC_CALL` 事件至 UI |
| `MEDIA_STREAM_LIVE_AUDIO_STOP` | 语音通话结束 | 停止所有播放 → 中断 AI 聊天 → 恢复原模式 → 发布 `TUYA_IPC_CALL` 事件至 UI |
| `MEDIA_STREAM_SPEAKER_START` | 扬声器启用 | 预留（当前无操作） |
| `MEDIA_STREAM_SPEAKER_STOP` | 扬声器停用 | 预留（当前无操作） |

**模式切换时序（进入 P2P）：**

```c
g_device_mode_before_p2p = tuya_ai_toy_device_mode_get();  // 1. 保存当前模式
wukong_audio_player_stop(AI_PLAYER_ALL);                    // 2. 停止 TTS + 音乐
tuya_ai_agent_event(AI_EVENT_CHAT_BREAK, 0);                // 3. 中断 AI 会话
wukong_ai_device_mode_switch(AI_DEVICE_MODE_P2P);           // 4. 切入 P2P 模式
// (仅视频) tuya_ai_toy_camera_switch_to_h264_mode();       // 5. 摄像头切 H.264
// (仅视频) tuya_ai_toy_camera_h264_start();                // 6. 启动 H.264 编码
```

**模式切换时序（退出 P2P）：**

```c
wukong_audio_player_stop(AI_PLAYER_ALL);
tuya_ai_agent_event(AI_EVENT_CHAT_BREAK, 0);
wukong_ai_device_mode_switch(g_device_mode_before_p2p);     // 恢复原模式
// (仅视频) tuya_ai_toy_camera_switch_to_jpeg_mode();
// (仅视频) tuya_ai_toy_camera_h264_stop();
```

---

## 6. 音频处理流程

### 6.1 上行音频（设备 → APP）

```
麦克风采集（16kHz, 16bit, 单声道）
    │  ~1280 字节 / 40ms（640 样本）
    │
    └─→ wukong_ai_p2p_handle_audio_input()  [模式层音频路由]
         │
         └─→ tuya_ipc_app_audio_frame_put(data, len)
              │
              ├─ Step 1：resample_to_8k_fixed()
              │          16kHz → 8kHz（tmp_pcm[1600]）
              │          输出：~640 字节（320 样本）
              │
              ├─ Step 2：tuya_g711_encode(TUYA_G711_MU_LAW)
              │          PCM → G.711 μ-law（g711_buf[1600]）
              │          输出：~320 字节（每样本 1 字节）
              │
              └─ Step 3：tuya_p2p_sdk_put_audio_frame()
                         写入音频 RingBuffer → P2P SDK 推流至 APP
```

**缓冲区设计（含 20% 余量）：**
```c
UCHAR_T tmp_pcm[1600];    // 重采样临时缓冲（最大 800 字节有效数据）
UCHAR_T g711_buf[1600];   // G.711 编码输出缓冲（最大 320 字节有效数据）
```

### 6.2 下行音频（APP → 设备）

```
接收 G.711 数据（~320 字节 / 帧，8kHz, 40ms）
    │
    └─→ __tuya_ipc_app_rev_audio_cb()
         │
         ├─ Step 1：tuya_g711_decode(TUYA_G711_MU_LAW)
         │          G.711 → 8kHz PCM（rev_pcm[800]）
         │          输出：~640 字节（320 样本）
         │
         ├─ Step 2：resample_to_16k_fixed()
         │          8kHz → 16kHz（rev_pcm_16k[1600]）
         │          输出：~1280 字节（640 样本）
         │
         └─ Step 3：tkl_ao_put_frame()
                    写入音频输出驱动 → 扬声器播放
```

**缓冲区设计：**
```c
UCHAR_T rev_pcm[800];       // G.711 解码缓冲（8kHz PCM，含余量）
UCHAR_T rev_pcm_16k[1600];  // 16kHz 重采样缓冲（含余量）
```

---

## 7. 视频处理流程

### 7.1 上行视频（设备 → APP）

```
摄像头 ISP 输出（YUV422, 480×480, 10fps）
    │
    └─→ tuya_ai_toy_camera_switch_to_h264_mode()  // 切换摄像头到 H.264+YUV 双流模式
         │
         └─→ tuya_ai_toy_camera_h264_start()       // 启动 H.264 编码线程
              │
              └─→ H.264 编码（25fps, 1Mbps, GOP=25）
                   │
                   └─→ tuya_p2p_sdk_put_video_frame() → 视频 RingBuffer → 推流
```

### 7.2 下行视频（APP → 设备，当前预留）

```c
/* 仅打印日志，未实现解码渲染 */
STATIC VOID __tuya_ipc_app_rev_video_cb(INT_T device, INT_T channel,
                                        CONST MEDIA_VIDEO_FRAME_T *p_video_frame)
{
    PR_INFO("Rev video. size:[%u] video_codec:[%d] video_frame_type:[%d]",
            p_video_frame->buf_len,
            p_video_frame->video_codec,
            p_video_frame->video_frame_type);
}
```

---

## 8. 通话控制接口

设备端可主动发起或挂断通话：

```c
/* 初始化通话控制（TMM 中间件，30s 超时） */
OPERATE_RET TUYA_IPC_call_init(void);

/* 主动呼叫 APP（发起语音通话） */
OPERATE_RET TUYA_IPC_call_app(void);
// 内部：tuya_tmm_control_call("key_1", "sp_dpsxj", TUYA_TMM_CONTROL_STREAM_TYPE_AUDIO)

/* 挂断当前通话 */
OPERATE_RET TUYA_IPC_hangup(void);
// 内部：tuya_tmm_control_hangup()
```

**自动接听（APP → 设备）：**

```c
/* TMM 事件回调，收到来电自动接听 */
STATIC VOID tmm_control_evt_cb(const TUYA_TMM_PINFO_T *pinfo, VOID *priv_data)
{
    if (pinfo->event == TUYA_TMM_CONTROL_EVT_INCOMING) {
        tuya_tmm_control_answer();    // 自动接听，无需用户确认
    }
}
```

---

## 9. P2P 设备模式状态机（`wukong_ai_mode_p2p.c`）

初始化时（`on_init`）：
1. 设置 VAD 为手动模式（`WUKONG_AUDIO_VAD_MANUAL`）
2. 禁用 KWS
3. 使能手动唤醒（`wakeup_stat = TRUE`）
4. 切换至 IDLE 状态

**音频路由（`on_audio_input`）：**
P2P 模式拦截所有麦克风帧，转发至 `tuya_ipc_app_audio_frame_put()`，不经过 AI 代理处理。

**按键处理：**

| 按键事件 | 行为 |
|---------|------|
| `NORMAL_KEY` | 停止播放，中断 AI 会话，状态 → IDLE |
| `LONG_KEY` | 状态 → LISTEN（启动手动录音路径） |
| `RELEASE_KEY` | 状态 → UPLOAD |

---

## 10. 通话 UI（T5AI_BOARD_DESKTOP）

### 10.1 通话状态枚举

```c
typedef enum {
    CALL_STATUS_IDLE = 0,   // 空闲
    CALL_STATUS_CALLING,    // 呼叫中（等待 APP 接通）
    CALL_STATUS_IN_CALL,    // 通话中
} CALL_STATUS_E;
```

### 10.2 屏幕布局

```
┌──────────────────────────────────────────┐
│ [←返回]         通话                     │  ← 标题栏 (320×50)
├──────────────────────────────────────────┤
│                                          │
│    ┌──────────┐      ┌──────────┐        │
│    │  挂断    │      │  呼叫    │        │  ← 操作区 (320×190)
│    │  [图标]  │      │  [图标]  │        │    按钮 65×65px
│    └──────────┘      └──────────┘        │
│                                          │
├──────────────────────────────────────────┤
│  通话中...  /  呼叫中...  /  (空)         │  ← 状态标签
└──────────────────────────────────────────┘
```

### 10.3 UI 状态更新

```c
/* 订阅 TUYA_IPC_CALL 事件，根据 P2P 事件更新状态标签 */
int call_status_event(MEDIA_STREAM_EVENT_E event)
{
    switch (event) {
    case MEDIA_STREAM_LIVE_AUDIO_START:
        s_call_status = CALL_STATUS_IN_CALL;
        lv_label_set_text(ui->status_label, "通话中...");
        break;
    case MEDIA_STREAM_LIVE_AUDIO_STOP:
        s_call_status = CALL_STATUS_IDLE;
        lv_obj_add_flag(ui->status_label, LV_OBJ_FLAG_HIDDEN);
        break;
    }
}
```

### 10.4 按钮交互

| 按钮 | 操作 |
|------|------|
| 呼叫按钮 | `TUYA_IPC_call_app()` → 启动 30s 超时计时器；超时后挂断并显示"呼叫失败" |
| 挂断按钮 | `TUYA_IPC_hangup()` → 立即更新状态为 IDLE |
| 返回按钮 | 若通话未结束则先调用 `TUYA_IPC_hangup()`，再返回前一屏幕 |

---

## 11. 视频通话 vs 语音通话对比

| 特性 | 视频通话 | 语音通话 |
|------|---------|---------|
| 触发事件 | `MEDIA_STREAM_LIVE_VIDEO_START/STOP` | `MEDIA_STREAM_LIVE_AUDIO_START/STOP` |
| 摄像头模式 | 切换至 H.264 模式 | 不切换（保持原模式） |
| 视频推流 | 启动 H.264 编码器 | 无 |
| 音频处理 | 相同（G.711 双向） | 相同（G.711 双向） |
| UI 事件通知 | 不发 `TUYA_IPC_CALL` | 发布 `TUYA_IPC_CALL` 事件更新 UI |
| 退出时恢复 | 切回 JPEG 模式，停止 H.264 | 仅恢复设备模式 |

---

## 12. 完整数据流总览

```
┌──────────────────────────────────────────────────────┐
│                  上行音频（设备 → APP）                │
│  麦克风 16kHz → 重采样 8kHz → G.711 编码 → P2P 推流  │
└──────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────┐
│                  下行音频（APP → 设备）                │
│  接收 G.711 → G.711 解码 → 重采样 16kHz → 扬声器播放 │
└──────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────┐
│                  上行视频（设备 → APP）                │
│  摄像头 YUV422 → H.264 编码 → P2P 推流               │
└──────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────┐
│              下行视频（APP → 设备，预留）              │
│  接收 H.264 帧 → 仅打印日志，未解码渲染              │
└──────────────────────────────────────────────────────┘
```

---

## 13. 调试日志

```
# P2P 初始化
[p2p] already activated, trigger event_active directly
[p2p] link already up, trigger ipc start directly
tuya_ipc_thread start

# 视频通话事件
p2p rev event cb=[MEDIA_STREAM_LIVE_VIDEO_START]
chn[0] video start
p2p rev event cb=[MEDIA_STREAM_LIVE_VIDEO_STOP]
chn[0] video stop

# 语音通话事件
p2p rev event cb=[MEDIA_STREAM_LIVE_AUDIO_START]
chn[0] audio start
p2p rev event cb=[MEDIA_STREAM_LIVE_AUDIO_STOP]
chn[0] audio stop

# 音频帧处理
put audio success: data size 1280
g711 decode success, src data size 320 decode size 640 sample 8000 codec G711U

# 视频帧接收
Rev video. size:[8192] video_codec:[0] video_frame_type:[1]
```

---

## 14. 常见问题

| 现象 | 可能原因 | 排查方向 |
|------|---------|---------|
| 通话无音频（APP 端听不到声音） | P2P 客户端未连接 | 检查 `tuya_ipc_get_client_online_num()` 返回值 |
| 重采样失败 | 输入帧大小不匹配 | 检查 `resample_to_8k_fixed` 返回值，确认输入为 16kHz PCM |
| G.711 编解码错误 | 编解码库未链接 | 确认 `tuya_g711_encode/decode` 可用，检查返回值 |
| 视频通话黑屏 | H.264 编码器未启动 | 确认 `ENABLE_TUYA_CAMERA=1`，检查 `tuya_ai_toy_camera_h264_start()` |
| P2P 连接超时 | 网络未就绪 | 确认 `EVENT_LINK_UP` 已触发，检查 MQTT 连接状态 |
| 通话结束后 AI 不响应 | 模式未恢复 | 检查 `g_device_mode_before_p2p` 是否正确保存，确认退出时调用了 `wukong_ai_device_mode_switch` |
| 呼叫 APP 无响应 | APP 未在线或超时 | 检查 `TUYA_IPC_call_app()` 返回值，等待 30s 超时后重试 |
