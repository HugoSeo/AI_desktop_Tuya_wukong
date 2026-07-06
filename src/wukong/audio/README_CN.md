# 音频模块

## 概述

音频模块为 Wukong AI 系统提供全面的音频处理能力，包括音频输入输出、音频前端处理（AEC、VAD、KWS）和音频播放管理。

## 目录结构

```
audio/
├── wukong_audio_player.h/c            # 音频播放器（TTS、音乐、提示音）
├── README_CN.md
├── frontend/                           # 音频前端处理（平台无关抽象层）
│   ├── wukong_audio_frontend.h/c       # 前端调度器：调用 ops，处理后喂入 KWS
│   ├── aec_vad/                        # AEC/VAD 实现
│   │   └── tuya/                       # 涂鸦平台实现（Speex AEC + RNN VAD）
│   │       └── wukong_audio_aec_vad.h/c
│   └── kws/                            # 关键词唤醒
│       ├── wukong_kws.h/c              # KWS 核心接口
│       ├── tutuclear/                   # TUTUClear 唤醒引擎
│       ├── sndx/                        # SNDX 唤醒引擎
│       └── uart/                        # UART 外接唤醒模式
├── input/                              # 音频输入
│   ├── wukong_audio_input.h/c          # 音频输入接口（包装器）
│   ├── wukong_audio_input_board.c      # 板级音频输入实现
│   └── wukong_audio_input_uart.c       # UART 音频输入实现
└── output/                             # 音频输出
    ├── wukong_audio_output.h/c         # 音频输出接口（包装器）
    ├── wukong_audio_output_board.c     # 板级音频输出实现
    └── wukong_audio_output_uart.c      # UART 音频输出实现
```

## 数据流

```
┌────────────┐     ┌──────────────────────────────────────────┐     ┌──────────────┐
│  input/    │     │  frontend/                                │     │  事件系统     │
│            │     │                                           │     │              │
│ board/uart │────►│ frontend_process() ──► ops->process()    │     │              │
│ 音频驱动    │     │       │                  (AEC + VAD)      │     │              │
│            │     │       ▼                                   │     │              │
│            │     │ wukong_kws_feed_with_vad() ──► KWS 引擎  │────►│ KWS_WAKEUP  │
└────────────┘     └──────────────────────────────────────────┘     └──────────────┘
```

## 核心组件

### 1. 音频前端 (`frontend/wukong_audio_frontend`)

提供平台无关的音频前端处理抽象层。通过操作表（ops table）解耦 AEC/VAD 的具体实现，支持不同平台注册自己的音频前端处理方案。

#### 架构

- **调度器** (`wukong_audio_frontend.c`): 调用已注册的 ops->process，处理完成后自动将结果喂入 KWS
- **涂鸦实现** (`aec_vad/tuya/`): 基于 Speex AEC + RNN VAD 的默认实现，导出 `g_tuya_frontend_ops`
- **新平台适配**: 实现 `WUKONG_AUDIO_FRONTEND_OPS_T` 并在板级初始化时通过 `wukong_audio_frontend_register()` 注册

#### 操作表定义

```c
typedef struct {
    OPERATE_RET (*init)(UINT32_T min_speech_len_ms, UINT32_T max_speech_interval_ms, UINT32_T frame_size);
    OPERATE_RET (*deinit)(VOID);
    OPERATE_RET (*process)(INT16_T *mic_data, INT16_T *ref_data, INT16_T *out_data);
    OPERATE_RET (*vad_start)(VOID);
    OPERATE_RET (*vad_stop)(VOID);
    OPERATE_RET (*vad_set_threshold)(WUKONG_AUDIO_VAD_THRESHOLD_E level);
    INT_T       (*vad_get_flag)(VOID);
    OPERATE_RET (*get_kws_output)(INT16_T **data, UINT32_T *len);
} WUKONG_AUDIO_FRONTEND_OPS_T;
```

#### 核心 API

```c
OPERATE_RET wukong_audio_frontend_register(WUKONG_AUDIO_FRONTEND_OPS_T *ops);
OPERATE_RET wukong_audio_frontend_init(UINT32_T min_speech_len_ms, UINT32_T max_speech_interval_ms, UINT32_T frame_size);
OPERATE_RET wukong_audio_frontend_deinit(VOID);
OPERATE_RET wukong_audio_frontend_process(INT16_T *mic_data, INT16_T *ref_data, INT16_T *out_data);
OPERATE_RET wukong_audio_frontend_vad_start(VOID);
OPERATE_RET wukong_audio_frontend_vad_stop(VOID);
OPERATE_RET wukong_audio_frontend_vad_set_threshold(WUKONG_AUDIO_VAD_THRESHOLD_E level);
INT_T       wukong_audio_frontend_vad_get_flag(VOID);
```

#### 新平台适配示例

```c
STATIC WUKONG_AUDIO_FRONTEND_OPS_T g_platform_x_ops = {
    .init              = platform_x_init,
    .deinit            = platform_x_deinit,
    .process           = platform_x_process,
    .vad_start         = platform_x_vad_start,
    .vad_stop          = platform_x_vad_stop,
    .vad_set_threshold = platform_x_vad_set_threshold,
    .vad_get_flag      = platform_x_vad_get_flag,
};

/* 在板级初始化时注册 */
wukong_audio_frontend_register(&g_platform_x_ops);
```

#### VAD 阈值

- `WUKONG_AUDIO_VAD_LOW`: 低阈值（更敏感）
- `WUKONG_AUDIO_VAD_MID`: 中等阈值（默认）
- `WUKONG_AUDIO_VAD_HIGH`: 高阈值（较不敏感）
- 阈值对应的数值可以按需调整，从 -99 到 0，阈值越小越灵敏

#### 向后兼容

旧 API（`wukong_aec_vad_init/deinit/process`、`wukong_vad_start/stop/get_flag/set_threshold`）保留为薄包装函数，内部转调 `wukong_audio_frontend_*` 系列接口。

### 2. 音频输入 (`input/wukong_audio_input`)

为来自不同源（板级或 UART）的音频输入提供统一接口。

#### 特性

- **多输入源**: 支持板级音频和 UART 音频输入
- **VAD 模式**: 手动（按键触发）和自动（语音检测）
- **生产者模式**: 可插拔的音频输入生产者

#### 核心 API

```c
OPERATE_RET wukong_audio_input_init(WUKONG_AUDIO_INPUT_CFG_T *cfg);
OPERATE_RET wukong_audio_input_start(VOID);
OPERATE_RET wukong_audio_input_stop(VOID);
OPERATE_RET wukong_audio_input_wakeup_set(BOOL_T enable);
OPERATE_RET wukong_audio_input_wakeup_mode_set(WUKONG_AUDIO_VAD_MODE_E mode);
```

#### 配置

```c
typedef struct {
    WUKONG_AUDIO_TYPE_E type;      // 板级或 UART
    UINT16_T timeout;
    union {
        WUKONG_BOARD_AUDIO_INPUT_CFG_T board;
        TDL_COMM_AUDIO_CFG_T uart;
    };
} WUKONG_AUDIO_INPUT_CFG_T;

typedef struct {
    TKL_AUDIO_SAMPLE_E sample_rate;    // 采样率
    TKL_AUDIO_DATABITS_E sample_bits;  // 采样位数
    TKL_AUDIO_CHANNEL_E channel;       // 声道配置
    TUYA_GPIO_NUM_E spk_io;            // 扬声器 GPIO
    TUYA_GPIO_LEVEL_E spk_io_level;    // GPIO 极性
    
    WUKONG_AUDIO_VAD_MODE_E vad_mode;  // VAD 模式
    UINT16_T vad_off_ms;               // VAD 补偿时间
    UINT16_T vad_active_ms;            // VAD 检测阈值
    UINT16_T slice_ms;                 // 音频切片时间
    
    WUKONG_AUDIO_OUTPUT output_cb;     // 数据回调
    VOID *user_data;                   // 用户数据
} WUKONG_BOARD_AUDIO_INPUT_CFG_T;
```

#### 使用示例

```c
WUKONG_AUDIO_INPUT_CFG_T input_cfg = {0};
input_cfg.type = WUKONG_AUDIO_USING_BOARD;
input_cfg.board.sample_rate = TKL_AUDIO_SAMPLE_16K;
input_cfg.board.sample_bits = TKL_AUDIO_DATABITS_16;
input_cfg.board.channel = TKL_AUDIO_CHANNEL_MONO;
input_cfg.board.vad_mode = WUKONG_AUDIO_VAD_AUTO;
input_cfg.board.vad_off_ms = 500;
input_cfg.board.vad_active_ms = 300;
input_cfg.board.slice_ms = 20;
input_cfg.board.output_cb = my_audio_callback;

wukong_audio_input_init(&input_cfg);
wukong_audio_input_start();
```

### 3. 音频输出 (`output/wukong_audio_output`)

为音频输出到不同目标提供统一接口。

#### 特性

- **多输出目标**: 支持板级音频和 UART 音频输出
- **消费者模式**: 可插拔的音频输出消费者

#### 核心 API

```c
OPERATE_RET wukong_audio_output_init(WUKONG_AUDIO_OUTPUT_CFG_T *cfg);
OPERATE_RET wukong_audio_output_start(VOID);
OPERATE_RET wukong_audio_output_write(UINT8_T *data, UINT16_T datalen);
OPERATE_RET wukong_audio_output_set_vol(INT32_T volume);
```

### 4. 音频播放器 (`wukong_audio_player`)

管理 TTS、音乐和提示音的音频播放。

#### 特性

- **双播放器系统**: 前台（TTS）和后台（音乐）播放器
- **多格式支持**: 支持 MP3、WAV、Opus、Speex、OggOpus
- **音量混合**: TTS 播放时自动调整音乐音量
- **播放控制**: 播放、暂停、恢复、重播、停止操作

#### 核心 API

```c
OPERATE_RET wukong_audio_play_tts_stream(WUKONG_AI_EVENT_TYPE_E type, 
                                         AI_AUDIO_CODEC_E codec, 
                                         CHAR_T *data, INT_T len);
OPERATE_RET wukong_audio_play_music(WUKONG_AI_MUSIC_T *music);
OPERATE_RET wukong_audio_player_alert(TY_AI_TOY_ALERT_TYPE_E type, BOOL_T cloud);
OPERATE_RET wukong_audio_player_stop(TY_AI_TOY_PLAYER_TYPE_E player);
BOOL_T wukong_audio_player_is_playing(VOID);
```

#### 支持的音频格式

- `AI_AUDIO_CODEC_MP3`: MP3 格式
- `AI_AUDIO_CODEC_WAV`: WAV 格式
- `AI_AUDIO_CODEC_OPUS`: Opus 格式
- `AI_AUDIO_CODEC_SPEEX`: Speex 格式
- `AI_AUDIO_CODEC_OGGOPUS`: OggOpus 格式

## 实现细节

### 板级音频输入 (`input/wukong_audio_input_board.c`)

- 使用 TKL 音频驱动进行硬件访问
- 实现环形缓冲区管理音频数据
- 支持手动和自动 VAD 模式
- 初始化时注册音频前端 ops 并设置 `wukong_audio_frontend_process` 为驱动回调

### 板级音频输出 (`output/wukong_audio_output_board.c`)

- 使用 TKL 音频驱动进行硬件访问
- 处理音频帧写入
- 支持音量控制

### UART 音频

- 提供基于 UART 的音频输入输出（`input/wukong_audio_input_uart.c`、`output/wukong_audio_output_uart.c`）
- 适用于外部音频编解码芯片
- 目前支持 GX8006: [获取唤醒词和固件](https://tuyaos.com/viewtopic.php?t=9147)
- 目前支持 CI1302: [获取唤醒词和固件](https://tuyaos.com/viewtopic.php?t=9148)

## 事件系统

音频模块发布以下事件：

- `EVENT_AUDIO_VAD`: VAD 状态改变时发布
  - 数据: `WUKONG_AUDIO_VAD_FLAG_E` (START/STOP)

## 配置

### 编译时标志

- `CONFIG_USING_BOARD_AUDIO_INPUT`: 使用板级音频输入（默认）
- `CONFIG_USING_UART_AUDIO_INPUT`: 使用 UART 音频输入
- `CONFIG_USING_BOARD_AUDIO_OUTPUT`: 使用板级音频输出
- `CONFIG_USING_UART_AUDIO_OUTPUT`: 使用 UART 音频输出

### 运行时配置

- 采样率: 通常为 16kHz
- 采样位数: 16 位
- 声道: 单声道
- 帧大小: 320 个采样（16kHz 下为 20ms）
