# KWS（关键词唤醒）模块

## 概述

KWS 模块为 Wukong AI 系统提供关键词唤醒检测功能。位于 `audio/frontend/kws/` 目录下，作为音频前端处理管线的一部分。支持两种接入方式：板载麦克风 与 UART 外接语音芯片。

## 目录结构

```
audio/frontend/kws/
├── wukong_kws.h/c              # KWS 接口与核心实现
├── tutuclear/                   # TUTUClear 唤醒引擎
│   ├── tutuclear.h/c
│   └── [模型库].a
├── sndx/                        # SNDX 唤醒引擎
│   ├── sndx.h/c
│   └── [模型库].a
└── uart/                        # UART 外接模式（外部 CODEC 检测）
    └── uart.h/c
```

## KWS 处理流程

### 模式一：板载麦克风（CONFIG_USING_BOARD_AUDIO_INPUT=y）

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ 1. 初始化                                                                   │
│    wukong_kws_init() / wukong_kws_default_init()                            │
│    ├── 创建环形缓冲 (2s, WUKONG_KWS_BUFSZ)                                  │
│    ├── 创建信号量、互斥锁                                                   │
│    ├── 创建 KWS 工作线程                                                    │
│    └── 调用 cfg.create() 初始化引擎 (TUTUClear/SNDX)                        │
└─────────────────────────────────────────────────────────────────────────────┘
                                        │
                                        ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ 2. 音频喂入 (每帧 ~20ms，由前端调度器 wukong_audio_frontend.c 调用)          │
│    wukong_kws_feed_with_vad(data, datalen, vadflag)                         │
│    ├── __wukong_kws_feed: 写入环形缓冲                                      │
│    ├── VAD 逻辑分支:                                                        │
│    │   • vad on 或 vad end → __wukong_kws_post (满足条件时 post 信号量)     │
│    │   • vad off → __wukong_kws_drop (静音超时丢弃老数据)                   │
│    └── 条件: 缓冲 ≥ 100ms 或 vad end 时 force post                         │
└─────────────────────────────────────────────────────────────────────────────┘
                                        │
                                        ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ 3. 工作线程 (__wukong_kws_thread)                                           │
│    while (未请求退出) {                                                      │
│        sem_wait (VAD 模式: 永久等待; 连续模式: 100ms 超时)                  │
│        mutex_lock → 从环形缓冲读取 → mutex_unlock                           │
│        若 readlen > 0:                                                       │
│            cfg.detect(ctx, buffer, readlen)                                  │
│            若唤醒: reset 引擎 + reset 环形缓冲 + 发布 EVENT_WUKONG_KWS_WAKEUP│
│    }                                                                        │
└─────────────────────────────────────────────────────────────────────────────┘
                                        │
                                        ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ 4. 事件发布                                                                 │
│    ty_publish_event(EVENT_WUKONG_KWS_WAKEUP, WUKONG_KWS_INDEX_E)            │
│    订阅者 (如 mode 模块) 处理唤醒，切换对话状态                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 模式二：UART 外接（CONFIG_USING_BOARD_AUDIO_INPUT 未启用）

```
外部 UART CODEC 芯片 ──检测到唤醒词──► tdl_comm_audio 回调
                                          │
                                          ▼
                              __WAKE_UP_CB() → wukong_kws_event(index)
                                          │
                                          ▼
                              ty_publish_event(EVENT_WUKONG_KWS_WAKEUP, ...)
```

无本地 PCM 喂入，唤醒由外部 CODEC 上报。

## 支持的关键词

### TUTUClear 引擎

- **libtutuClear_wakeup_nihaotuya_*.a**: 你好涂鸦
- **libtutuClear_wakeup_xiaozhitongxue_*.a**: 小智同学
- **libtutuClear_wakeup_heytuya_*.a**: Hey Tuya
- **libtutuClear_*_small_model.a**: 多合一模型

### SNDX 引擎

- **libsndxasr-nihaotuya.a**: 你好涂鸦
- **libsndxasr-hey-tuya.a**: Hey Tuya
- **libsndxasr-hey-smart-life.a**: Hey SmartLife

### 用户自定义

- `WUKONG_KWS_UDF1/2/3`: 用户自定义唤醒词

## API 参考

### 初始化

```c
/* 使用默认引擎 (TUTUClear) */
INT_T wukong_kws_default_init(VOID);

/* 使用自定义引擎配置 */
typedef struct {
    INT_T (*create)(WUKONG_KWS_CTX_T *ctx);
    INT_T (*detect)(WUKONG_KWS_CTX_T *ctx, UINT8_T *data, UINT32_T datalen);
    INT_T (*reset)(WUKONG_KWS_CTX_T *ctx);
    INT_T (*deinit)(WUKONG_KWS_CTX_T *ctx);
    UINT8_T is_detect_vad;   /* 1=VAD 节流; 0=持续检测 */
} WUKONG_KWS_CFG_T;

INT_T wukong_kws_init(WUKONG_KWS_CFG_T *cfg);
INT_T wukong_kws_uninit(VOID);
```

### 控制

```c
INT_T wukong_kws_enable(VOID);
INT_T wukong_kws_disable(VOID);
INT_T wukong_kws_set_vad_detect(UINT8_T is_detect_vad);
```

### 数据喂入（板载模式）

```c
/* 由前端调度器 (wukong_audio_frontend.c) 在 ops->process 完成后自动调用；vadflag: 1=有语音, 0=静音 */
INT_T wukong_kws_feed_with_vad(UINT8_T *data, UINT16_T datalen, UINT8_T vadflag);
```

### 事件

- **名称**：`EVENT_WUKONG_KWS_WAKEUP`
- **数据**：`WUKONG_KWS_INDEX_E`（唤醒词索引）

```c
ty_subscribe_event(EVENT_WUKONG_KWS_WAKEUP, "my_module", on_kws_wakeup, SUBSCRIBE_TYPE_NORMAL);
```

## 音频要求

- **采样率**：16 kHz
- **格式**：16 bit PCM，单声道
- **帧长**：通常 320 采样/帧（20 ms）

## 切换引擎

```c
/* 使用 TUTUClear（默认） */
WUKONG_KWS_CFG_T cfg = {
    .create  = TUTUClear_kws_create,
    .detect  = TUTUClear_kws_detect,
    .reset   = TUTUClear_kws_reset,
    .deinit  = TUTUClear_kws_deinit,
    .is_detect_vad = 1,
};
wukong_kws_init(&cfg);

/* 使用 SNDX */
cfg.create = SNDX_kws_create;
cfg.detect = SNDX_kws_detect;
cfg.reset  = SNDX_kws_reset;
cfg.deinit = SNDX_kws_deinit;
wukong_kws_init(&cfg);
```

## 自定义唤醒词

1. 训练或获取唤醒词模型（可联系涂鸦或方案商）
2. 在 `WUKONG_KWS_INDEX_E` 中增加对应索引
3. 将模型编译为 `lib<name>.a` 放入 `libs/`
4. 在 `cfg.create/detect` 中接入新引擎，并在 detect 中调用 `wukong_kws_event(index)`
