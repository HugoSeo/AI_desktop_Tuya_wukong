# 桌面机器人录音功能技术文档

## 1. 概述

录音模式（Record Mode）是桌面机器人的专用设备模式，通过麦克风采集用户语音并保存为本地音频文件（WAV 或 OPUS 格式）。录音文件可在录音列表屏中播放和管理，也可通过内置上传功能发送给 AI 服务端进行语音转文字与总结。

**功能特性：**
- 长按按键触发录音，松开结束；也可通过 UI 按钮控制开始/暂停/停止
- 支持 PCM（保存为 WAV）和 OPUS 双编码格式
- WAV 文件头自动计算并在录音结束时回填
- 录音文件以时间戳命名，元数据以 JSON 格式索引持久化
- 最多保留 100 条录音，超出时自动删除最旧的（FIFO 淘汰）
- 支持录音回放（WAV/OPUS 自动检测格式）
- 支持上传录音到 AI 服务端，并触发语音转文字与总结
- 退出录音屏时自动恢复进入前的设备模式

**相关核心文件：**

| 文件 | 说明 |
|------|------|
| `src/mode/wukong_ai_mode_record.c` | 录音模式状态机与音频路由 |
| `src/mode/wukong_ai_mode.h` | 模式管理接口定义 |
| `src/boards/T5AI_BOARD_DESKTOP/ui/desk_func_record.h/.c` | 录音 UI、文件管理、播放与上传 |
| `src/miscs/audio_analysis/wav_encode.h` | WAV 文件头生成 |
| `include/tuya_app_config.h` | 配置参数定义 |

---

## 2. 编译配置

```c
#define ENABLE_AI_MODE_RECORD      1    // 启用录音模式
#define ENABLE_TUYA_CODEC_OPUS     1    // 启用 OPUS 编码（可选，默认 PCM/WAV）
#define ENABLE_T5AI_BOARD_UI_DESKTOP 1  // 启用桌面 UI
```

---

## 3. 配置参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `RECORD_PCM_SAMPLE_RATE` | `16000` | 录音采样率（Hz） |
| `RECORD_PCM_CHANNELS` | `1` | 声道数（单声道） |
| `RECORD_PCM_BIT_DEPTH` | `16` | 位深（bit） |
| `RECORD_STORE_DIR` | `/t5_fs/tmp/record` | 录音文件存储目录 |
| `RECORD_INFO_SAVE_PATH` | `/t5_fs/tmp/record/record_list.json` | 录音元数据索引文件 |
| `RECORD_INFO_ITEM_NUM` | `100` | 最多保留录音条数 |
| `RECORD_UPLOAD_READ_SIZE` | `6144`（6 KB） | 上传时每次读取块大小 |
| `WAV_HEAD_LEN` | `44` | WAV 文件头长度（字节） |

---

## 4. 数据结构

### 4.1 录音 UI 状态

```c
typedef enum {
    RECORD_STATE_DEFAULT = 0,   // 默认/停止
    RECORD_STATE_RECORDING,     // 录音中
    RECORD_STATE_PAUSED,        // 已暂停
} RECORD_STATE_E;
```

### 4.2 录音列表项

```c
typedef struct {
    int          id;              // 记录 ID（0～99）
    char         name[64];        // 文件名（如 REC_20250427_145230.wav）
    UINT64_T     len;             // 文件大小（字节）
    UINT32_T     duration;        // 录音时长（秒）
    POSIX_TM_S   create_time;     // 创建时间
    LIST_HEAD    list_node;       // 链表节点
} RECORD_AUDIO_LIST_T;
```

### 4.3 音频输入回调句柄

```c
typedef struct {
    OPERATE_RET (*input_audio)(AI_AUDIO_CODEC_TYPE codec_type, VOID *data, INT_T len);
} AI_RECORD_HANDLE_T;
```

UI 层通过此回调接收来自模式层路由的 PCM/OPUS 数据，写入文件。

### 4.4 上传上下文

```c
typedef struct {
    TUYA_FILE  fp;           // 文件句柄
    UINT8_T   *read_buf;     // 6 KB 读缓冲
    UINT64_T   file_len;     // 文件总大小
    UINT64_T   total_sent;   // 已发送字节数
} __RECORD_UPLOAD_CTX_T;
```

---

## 5. 状态机

### 5.1 状态定义

录音模式复用通用聊天状态机枚举（`wukong_ai_mode.h`）：

```c
typedef enum {
    AI_CHAT_INIT,    // 初始化
    AI_CHAT_IDLE,    // 空闲（等待触发）
    AI_CHAT_LISTEN,  // 录音中（麦克风启用）
    AI_CHAT_UPLOAD,  // 结束录音（关闭麦克风，保存文件）
    AI_CHAT_INVALID,
} AI_CHAT_STATE_E;
```

> 录音模式不使用 `AI_CHAT_THINK` 和 `AI_CHAT_SPEAK` 状态。

### 5.2 状态转换图

```
             ┌─────────────────────────────────────┐
             │             AI_CHAT_INIT             │
             └──────────────────┬──────────────────┘
                                │ 禁用 KWS，设置手动 VAD
                                ▼
             ┌─────────────────────────────────────┐
     ┌──────►│            AI_CHAT_IDLE              │
     │       └──────────────────┬──────────────────┘
     │                          │ 长按按键 / UI 点击录音
     │                          ▼
     │       ┌─────────────────────────────────────┐
     │       │           AI_CHAT_LISTEN             │
     │       │           （麦克风启用，持续写文件）  │
     │       └──────────────────┬──────────────────┘
     │                          │ 松开按键 / UI 暂停/停止
     │                          ▼
     └───────┤           AI_CHAT_UPLOAD             │
             │           （关闭麦克风，保存文件）   │
             └─────────────────────────────────────┘
```

### 5.3 按键事件映射

| 按键事件 | 目标状态 | 操作 |
|---------|---------|------|
| `LONG_KEY`（长按） | LISTEN | 启用麦克风录音 |
| `RELEASE_KEY`（松开） | UPLOAD | 关闭麦克风，触发文件保存 |
| `NORMAL_KEY`（单击） | IDLE | 重置状态 |

---

## 6. 音频编码路径

### 6.1 PCM 直接写入（WAV）

```
麦克风 PCM（16kHz, 16bit, 单声道）
    │
    └─→ wukong_ai_record_handle_audio_input()
         │
         └─→ AI_RECORD_HANDLE_T.input_audio(AUDIO_CODEC_PCM, data, len)
              │
              └─→ __record_input_audio_cb()
                   ├─ 首帧：__record_file_open() → 写入 44 字节 WAV 占位头
                   └─ 每帧：tkl_fwrite(pcm_data, len, fp)
```

### 6.2 OPUS 编码写入（`.opus`）

```
麦克风 PCM（16kHz, 16bit, 单声道）
    │
    └─→ wukong_ai_record_handle_audio_input()
         │
         └─→ s_record_opus.encoder->encode(pcm, len, __ai_record_encoder_data_cb)
              │
              └─→ __ai_record_encoder_data_cb()
                   │
                   └─→ AI_RECORD_HANDLE_T.input_audio(AUDIO_CODEC_OPUS, opus_data, len)
                        │
                        └─→ __record_input_audio_cb()
                             └─ tkl_fwrite(opus_data, len, fp)
```

### 6.3 OPUS 编码器配置

```c
s_record_opus.encoder_info = {
    .encode_type    = AUDIO_CODEC_OPUS,
    .sample_rate    = 16000,
    .channels       = 1,
    .bits_per_sample = 16,
    .bitrate        = 16000,
    .bandwidth      = 1102,   // OPUS_BANDWIDTH_NARROWBAND
    .vbr            = 0,      // 固定码率
    .dtx            = 0,      // 禁用 DTX
    .complexity     = 0,      // 最低复杂度（嵌入式优化）
};
```

---

## 7. 完整录音流程

```
用户进入录音屏
    │
    ├─→ 记录当前设备模式（s_ai_mode_before_record）
    ├─→ wukong_ai_device_mode_switch(AI_DEVICE_MODE_RECORD)
    │    └─ 初始化：禁用 KWS，设置手动 VAD，初始化 OPUS 编码器
    └─→ desk_record_handle_register()
         └─ 注册 __record_input_audio_cb 为 AI_RECORD_HANDLE_T.input_audio
    │
    │ [用户长按按键或 UI 点击录音按钮]
    │
    ├─→ 状态 → LISTEN
    ├─→ wukong_audio_input_wakeup_set(TRUE)（启用麦克风）
    │
    │ [麦克风持续采集 PCM 帧]
    │
    ├─→ wukong_ai_record_handle_audio_input() → [编码] → __record_input_audio_cb()
    │    ├─ 首帧：创建文件，写 WAV 占位头（PCM 模式）
    │    └─ 每帧：tkl_fwrite(data, len, fp)
    │
    │ [用户松开按键或 UI 点击停止]
    │
    ├─→ 状态 → UPLOAD
    ├─→ wukong_audio_input_wakeup_set(FALSE)（关闭麦克风）
    └─→ __record_file_close_and_save()
         ├─ 回填 WAV 文件头（重写 data chunk 大小）
         ├─ tkl_fclose(fp)
         ├─ 超限检查（> 100 条）→ 删除最旧文件
         ├─ 分配新 ID（0～99，找第一个空位）
         ├─ 创建 RECORD_AUDIO_LIST_T 节点，加入链表
         └─ __record_info_json_write() → 更新 record_list.json
```

---

## 8. 文件管理

### 8.1 目录结构

```
/t5_fs/tmp/record/
├── REC_20250427_145230.wav     // 录音文件（PCM 编码）
├── REC_20250427_150012.opus    // 录音文件（OPUS 编码）
└── record_list.json            // 元数据索引
```

### 8.2 文件命名规则

格式：`REC_YYYYMMDD_HHMMSS.{wav|opus}`

示例：`REC_20250427_145230.wav`（2025年4月27日14:52:30 录制）

### 8.3 JSON 元数据格式

```json
{
    "num": 2,
    "list": [
        {
            "id": 0,
            "name": "REC_20250427_145230.wav",
            "len": 2560000,
            "duration": 80,
            "year": 2025, "mon": 4, "mday": 27,
            "hour": 14, "min": 52, "sec": 30
        },
        {
            "id": 1,
            "name": "REC_20250427_150012.opus",
            "len": 64000,
            "duration": 32,
            "year": 2025, "mon": 4, "mday": 27,
            "hour": 15, "min": 0, "sec": 12
        }
    ]
}
```

### 8.4 WAV 文件头格式（44 字节）

```
[RIFF 标识 + 文件大小] (8 bytes)
[WAVE 格式标识]        (4 bytes)
[fmt 子块]             (24 bytes)
  - PCM 格式 (AudioFormat=1)
  - 单声道 (NumChannels=1)
  - 采样率 (SampleRate=16000)
  - 位深 (BitsPerSample=16)
[data 子块头]          (8 bytes)
  - "data" 标识
  - 数据区大小（录音结束后回填）
```

---

## 9. 录音回放与上传

### 9.1 回放

```
用户点击录音列表条目
    │
    └─→ __record_play_start_playback()
         ├─ 检测扩展名（.opus / .wav）
         ├─ wukong_audio_play_local(filepath, name, NULL, codec, 0)
         └─ 启动 500ms 定时器更新进度条
```

### 9.2 上传至 AI 并转写总结

```
用户点击"分享"按钮
    │
    └─→ 打开文件，分配 6 KB 读缓冲，创建 20ms 上传定时器
         │
         └─→ __record_upload_timer_cb()（每 20ms 执行）
              ├─ tkl_fread(read_buf, 6144, fp)
              ├─ wukong_ai_agent_send_file(read_buf, read_len)
              ├─ 更新进度条（total_sent * 100 / file_len）
              └─ 文件读完时：
                   wukong_ai_agent_send_text("将刚才上传的录音文件转换成文字并总结")
```

---

## 10. UI 屏幕

### 10.1 录音主界面

```
┌──────────────────────────────────────────┐
│ [←返回]      录音         [列表/完成按钮] │  ← 标题栏
├──────────────────────────────────────────┤
│                                          │
│           00:01.23                       │  ← 时间显示
│          (HH:MM.SS)                      │
│                                          │
│        [  录音按钮（三态）  ]             │
│     DEFAULT=灰 / RECORDING=红 / PAUSED=黄│
│                                          │
└──────────────────────────────────────────┘
```

时间格式：
- 录音中：`HH:MM.SS`（如 `00:01.23`）
- 播放时：`MM:SS`（如 `01:45`）

### 10.2 录音列表界面

```
┌──────────────────────────────────────────┐
│ [←返回]      录音列表                     │
├──────────────────────────────────────────┤
│ REC_20250427_145230.wav          [×删除] │
│ 2025-04-27 14:52:30  80s                 │
├──────────────────────────────────────────┤
│ REC_20250427_150012.opus         [×删除] │
│ 2025-04-27 15:00:12  32s                 │
└──────────────────────────────────────────┘
```

点击条目后展开播放器（进度条 + 播放/暂停 + 分享按钮）。

---

## 11. 模式注册与初始化

```c
OPERATE_RET ai_record_register(AI_CHAT_MODE_HANDLE_T **cb)
{
    s_ai_record_cb.on_init        = wukong_ai_record_int_cb;
    s_ai_record_cb.on_deinit      = wukong_ai_record_deint_cb;
    s_ai_record_cb.on_key         = wukong_ai_record_key_cb;
    s_ai_record_cb.on_task        = wukong_ai_record_task_cb;
    s_ai_record_cb.on_event       = wukong_ai_record_event_cb;
    s_ai_record_cb.on_audio_input = wukong_ai_record_handle_audio_input;  // 拦截音频帧
    // on_wakeup、on_vad、on_client 均为 stub
    *cb = &s_ai_record_cb;
    return OPRT_OK;
}
```

初始化时（`on_init`）：
1. 设置 VAD 为手动模式（`WUKONG_AUDIO_VAD_MANUAL`）
2. 禁用 KWS
3. 初始化 OPUS 编码器（若启用）
4. 切换至 IDLE 状态

反初始化时（`on_deinit`）：
1. 关闭麦克风
2. 重置音频输入路径，防止 PCM 帧流入 AI 代理

---

## 12. 调试日志

```
# 状态切换（DEBUG 级别）
mode record state change from INIT to IDLE
mode record state change from IDLE to LISTEN
mode record state change from LISTEN to UPLOAD

# 文件操作（INFO 级别）
[record] file open: /t5_fs/tmp/record/REC_20250427_145230.wav
[record] file close, size=2560000, duration=80s
[record] list saved, num=3

# 上传进度（INFO 级别）
[record] upload progress: 45%
[record] upload done, send transcript request
```

---

## 13. 常见问题

| 现象 | 可能原因 | 排查方向 |
|------|---------|---------|
| 录音无声（文件大小只有 44 字节） | 麦克风未启用 | 检查 `wukong_audio_input_wakeup_set(TRUE)` 是否在 LISTEN 状态调用 |
| WAV 文件无法播放 | 文件头未回填 | 确认 `__record_file_close_and_save()` 在所有退出路径均被调用 |
| 录音列表不显示 | JSON 文件未加载 | 检查 `RECORD_INFO_SAVE_PATH` 文件是否存在，查看 `[record]` 日志 |
| 超过 100 条后旧文件未删除 | 链表操作互斥锁未释放 | 检查 `tal_mutex_unlock(s_record_list.mutex)` 是否配对 |
| OPUS 文件播放乱码 | 解码器未编译 | 确认 `AI_PLAYER_DECODER_OPUS_ENABLE=1` |
| 上传卡在 0% | 文件打开失败 | 检查文件路径，查看 `tkl_fopen` 返回值 |
