
# 模式模块

## 概述

模式模块管理不同的 AI 对话触发模式，为实现各种交互模式提供灵活的框架。每个模式实现一个状态机来处理对话流程。

## 目录结构

```
mode/
├── wukong_ai_mode.h/c              # 模式管理器，提供对外接口
├── wukong_ai_mode_free.c           # 自由对话模式
├── wukong_ai_mode_hold.c           # 长按对话模式
├── wukong_ai_mode_oneshot.c        # 单次按键回合对话模式
├── wukong_ai_mode_wakeup.c         # 关键词唤醒回合模式
├── wukong_ai_mode_p2p.c            # P2P 通信模式，需要APP面板触发
├── wukong_ai_mode_translate.c      # 翻译模式
└── wukong_ai_mode_record.c         # 录音模式
```

## 支持的模式

### 1. 长按对话模式 (`wukong_ai_mode_hold`)

**触发方式**: 长按按键  
**使用场景**: 按键通话式对话  
**特性**:
- AEC 默认支持
- 手动 VAD 模式（按键控制录音）
- KWS 禁用
- 服务器 VAD 禁用

### 2. 单次按键回合对话模式 (`wukong_ai_mode_oneshot`)

**触发方式**: 单次按键  
**使用场景**: 回合制对话  
**特性**:
- AEC 默认支持
- 手动 VAD 模式
- KWS 禁用
- 服务器 VAD 禁用

### 3. 关键词唤醒回合模式 (`wukong_ai_mode_wakeup`)

**触发方式**: 关键词唤醒  
**使用场景**: 免提语音交互  
**特性**:
- AEC 默认支持
- 自动 VAD 模式
- KWS 启用
- 服务器 VAD 启用

### 4. 自由对话模式 (`wukong_ai_mode_free`)

**触发方式**: 关键词唤醒 + 连续对话  
**使用场景**: 自然对话流程  
**特性**:
- AEC 默认支持
- 自动 VAD 模式
- KWS 启用
- 服务器 VAD 启用
- 支持连续对话

### 5. P2P 通信模式 (`wukong_ai_mode_p2p`)

**触发方式**: P2P 通信，需 APP 面板触发  
**使用场景**: 直接点对点音频通信  
**特性**:
- AEC 默认支持
- 手动 VAD 模式
- KWS 禁用
- 直接音频流P2P协议传输给APP

### 6. 翻译模式 (`wukong_ai_mode_translate`)

**触发方式**: 关键词唤醒  
**使用场景**: 实时语言翻译  
**特性**:
- AEC 默认支持
- 自动 VAD 模式
- KWS 启用
- ASR-LLM-TTS 工作流
- 语言列表获取和目标语音切换支持

### 6. 录音模式 (`wukong_ai_mode_record`)

**触发方式**: 需UI触发  
**使用场景**: 音频数据保存  
**特性**:
- AEC 默认支持
- 手动 VAD 模式
- KWS 禁用
- 直接音频流保存至文件

## 如何配置模式

各模式（长按 / 单次 / 唤醒 / 自由 / P2P / 翻译）的启用与板型、功能开关等均在应用配置中完成。配置入口为 `make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai`，配置会生成到应用根目录下的 **build** 目录（如 `APPConfig` 等），修改后需执行 `make app_config` 生成头文件。具体说明见：

- **[build 配置说明](../../build/README_CN.md)**：配置入口与流程、板型与 AI 模式选择、`ENABLE_AI_MODE_*` 与默认模式等。

板型与编译选项的更多说明见 [板级支持包 README](../boards/README_CN.md)。

## 状态机

所有模式遵循共同的状态机：

```
INIT → IDLE → LISTEN → UPLOAD → THINK → SPEAK
        ↑       ↑                         ↓
        └───────└─────────────────────────┘
```

### 状态描述

- **INIT**: 初始化状态
- **IDLE**: 空闲状态，等待触发
- **LISTEN**: 监听用户输入
- **UPLOAD**: 上传音频到云端
- **THINK**: AI 处理（云端思考）
- **SPEAK**: 播放 AI 回复（TTS）

## API 参考

### 模式管理

```c
/**
 * @brief 初始化模式系统
 * 
 * 注册所有启用的模式并初始化默认模式。
 * 
 * @return 成功返回 OPRT_OK，否则返回错误码
 */
OPERATE_RET wukong_ai_mode_init(VOID);

/**
 * @brief 切换到下一个启用的模式
 * 
 * @param[in] cur_mode 当前模式
 * @return 成功返回 OPRT_OK，否则返回错误码
 */
OPERATE_RET wukong_ai_mode_switch(AI_TRIGGER_MODE_E cur_mode);

/**
 * @brief 切换到指定模式
 * 
 * @param[in] mode 目标模式
 * @return 成功返回 OPRT_OK，否则返回错误码
 */
OPERATE_RET wukong_ai_mode_switch_to(AI_TRIGGER_MODE_E mode);
```

### 消息分发

提供统一的接口，处理各种状态、消息。对外只暴露单一入口 `wukong_ai_mode_dispatch`，通过操作枚举将请求分发给当前模式的对应回调。

```c
/** 操作类型：与 AI_CHAT_MODE_HANDLE_T 中的回调一一对应 */
typedef enum {
    AI_MODE_OP_INIT, AI_MODE_OP_DEINIT, AI_MODE_OP_KEY, AI_MODE_OP_TASK,
    AI_MODE_OP_EVENT, AI_MODE_OP_WAKEUP, AI_MODE_OP_VAD, AI_MODE_OP_CLIENT,
    AI_MODE_OP_NOTIFY_IDLE, AI_MODE_OP_AUDIO_INPUT, AI_MODE_OP_MAX,
} AI_MODE_OP_E;

/**
 * @brief 将操作分发给当前模式（推荐使用）
 * @param[in] op   操作类型
 * @param[in] data 载荷（可为 NULL）
 * @param[in] len  载荷长度
 * @return OPRT_OK 成功，OPRT_NOT_FOUND 无对应处理，或回调返回值
 */
OPERATE_RET wukong_ai_mode_dispatch(AI_MODE_OP_E op, VOID *data, INT_T len);
```

### 模式处理器接口

每个模式按需实现以下接口，未实现的接口（赋值为 `NULL`）在事件分发时会被安全拦截或采用默认行为：

```c
typedef struct {
    OPERATE_RET (*on_init)(VOID *data, INT_T len);        /* 模式进入时的初始化（如设置VAD模式、启停KWS等） */
    OPERATE_RET (*on_deinit)(VOID *data, INT_T len);      /* 模式退出时的反初始化（资源清理） */
    OPERATE_RET (*on_key)(VOID *data, INT_T len);         /* 物理按键事件触发（如长按/短按） */
    OPERATE_RET (*on_task)(VOID *data, INT_T len);        /* 定时任务或主循环轮询（处理超时或延迟操作） */
    OPERATE_RET (*on_event)(VOID *data, INT_T len);       /* AI 云端或系统事件（如 ASR识别结果、TTS播放状态等） */
    OPERATE_RET (*on_wakeup)(VOID *data, INT_T len);      /* 语音关键词本地唤醒事件 */
    OPERATE_RET (*on_vad)(VOID *data, INT_T len);         /* 语音端点检测事件（检测到人声起点/尾点） */
    OPERATE_RET (*on_client)(VOID *data, INT_T len);      /* 客户端业务指令或其他网络下发数据 */
    OPERATE_RET (*on_notify_idle)(VOID *data, INT_T len); /* 系统空闲状态通知（可用于超时自动切回默认状态） */
    OPERATE_RET (*on_audio_input)(VOID *data, INT_T len); /* 音频数据流输入（如果不实现该回调，将走默认云端上行） */
} AI_CHAT_MODE_HANDLE_T;
```

各模式内部可共用 `AI_CHAT_MODE_PARAM_T`（`wukong_ai_mode.h` 中定义，含 `wakeup_stat`、`state`）作为模式上下文结构。

### 状态变更宏

状态变更宏用于在各模式内部进行状态转换并打印日志：

```c
#define MODE_STATE_CHANGE(_mode, _old, _new) \
do { \
    PR_DEBUG("模式 %s 状态从 %s 变更为 %s", \
             _mode_str[_mode], _state_str[_old], _state_str[_new]); \
    _old = _new; \
} while (0)
```

## 使用示例

### 基础初始化

```c
#include "wukong_ai_mode.h"

/* 初始化模式系统 */
OPERATE_RET init_modes(VOID)
{
    OPERATE_RET rt = OPRT_OK;
    
    /* 初始化模式管理器 */
    /* 这将注册所有启用的模式并启动默认模式 */
    rt = wukong_ai_mode_init();
    if (rt != OPRT_OK) {
        printf("初始化模式失败: %d\n", rt);
        return rt;
    }
    
    return rt;
}
```

### 切换模式

```c
/* 切换到唤醒模式 */
wukong_ai_mode_switch_to(AI_TRIGGER_MODE_WAKEUP);

/* 切换到下一个启用的模式 */
AI_TRIGGER_MODE_E current = tuya_ai_toy_trigger_mode_get();
wukong_ai_mode_switch(current);
```

### 处理事件（统一用 dispatch + 操作类型）

```c
/* 按键 */
PUSH_KEY_TYPE_E key_event = NORMAL_KEY;
wukong_ai_mode_dispatch(AI_MODE_OP_KEY, &key_event, sizeof(key_event));

/* AI 事件 */
WUKONG_AI_EVENT_T event = { .type = WUKONG_AI_EVENT_ASR_OK, .data = asr_result };
wukong_ai_mode_dispatch(AI_MODE_OP_EVENT, &event, 0);

/* 唤醒、VAD */
wukong_ai_mode_dispatch(AI_MODE_OP_WAKEUP, NULL, 0);
WUKONG_AUDIO_VAD_FLAG_E vad_flag = WUKONG_AUDIO_VAD_START;
wukong_ai_mode_dispatch(AI_MODE_OP_VAD, &vad_flag, sizeof(vad_flag));
```

### 状态变更宏

```c
/* 在模式内部使用 MODE_STATE_CHANGE 进行状态转换 */
MODE_STATE_CHANGE(AI_TRIGGER_MODE_WAKEUP, s_ai_wakeup.state, AI_CHAT_LISTEN);
```

## 创建新模式

无需单独头文件，在 `mode/` 下新增 `wukong_ai_mode_my_mode.c`，仅依赖 `wukong_ai_mode.h`。

### 步骤 0: 在配置中新增模式

在构建与配置体系中增加新模式开关，使该模式可在 menuconfig 中勾选并被板型选用：

1. **新增配置项**：在应用的 Kconfig 中增加 `ENABLE_AI_MODE_MY_MODE`（或当前模式名）选项，使 `make app_menuconfig` 中能勾选该模式。
2. **为板型启用模式**：在应用根目录下的 **build/APPconfig** 中，于需要支持该模式的板型对应的 `config` 块下添加 `select ENABLE_AI_MODE_MY_MODE`；若希望所有板型可选，则为各板型逐一添加。
3. **生成配置**：保存后执行 `make app_config APP_NAME=tuyaos_demo_wukong_ai` 生成头文件。

详细配置入口、流程及板型与模式对应关系见 [build 配置说明](../../build/README_CN.md)。

### 步骤 1: 实现模式 .c

```c
/* wukong_ai_mode_my_mode.c */
#include "wukong_ai_mode.h"

#if defined(ENABLE_AI_MODE_MY_MODE) && (ENABLE_AI_MODE_MY_MODE == 1)

STATIC AI_CHAT_MODE_HANDLE_T s_ai_my_mode_cb = {0};
STATIC AI_CHAT_MODE_PARAM_T s_ai_my_mode = {0};
STATIC AI_CHAT_STATE_E s_ai_cur_state = AI_CHAT_INVALID;

/* 状态回调 */
STATIC OPERATE_RET __ai_my_mode_idle_cb(VOID *data, INT_T len)
{
    tuya_ai_toy_led_off();
    wukong_audio_input_wakeup_set(FALSE);
    return OPRT_OK;
}

STATIC OPERATE_RET __ai_my_mode_listen_cb(VOID *data, INT_T len)
{
    tuya_ai_toy_led_flash(500);
    wukong_audio_input_wakeup_set(TRUE);
    return OPRT_OK;
}

/* ... 实现其他状态回调 ... */

/* 事件处理器 */
STATIC OPERATE_RET wukong_ai_my_mode_event_cb(VOID *data, INT_T len)
{
    WUKONG_AI_EVENT_T *event = (WUKONG_AI_EVENT_T *)data;
    
    switch (event->type) {
    case WUKONG_AI_EVENT_ASR_OK:
        MODE_STATE_CHANGE(AI_TRIGGER_MODE_MY_MODE, s_ai_my_mode.state, AI_CHAT_THINK);
        break;
        
    case WUKONG_AI_EVENT_TTS_PRE:
        MODE_STATE_CHANGE(AI_TRIGGER_MODE_MY_MODE, s_ai_my_mode.state, AI_CHAT_SPEAK);
        break;
        
    /* ... 处理其他事件 ... */
    }
    
    return OPRT_OK;
}

/* 初始化处理器 */
STATIC OPERATE_RET wukong_ai_my_mode_init_cb(VOID *data, INT_T len)
{
    wukong_audio_input_wakeup_mode_set(WUKONG_AUDIO_VAD_AUTO);
    wukong_kws_enable();
    MODE_STATE_CHANGE(AI_TRIGGER_MODE_MY_MODE, s_ai_my_mode.state, AI_CHAT_IDLE);
    return OPRT_OK;
}

/* 注册模式 */
OPERATE_RET ai_my_mode_register(AI_CHAT_MODE_HANDLE_T **cb)
{
    s_ai_my_mode_cb.on_init       = wukong_ai_my_mode_init_cb;
    s_ai_my_mode_cb.on_deinit     = wukong_ai_my_mode_deinit_cb;
    s_ai_my_mode_cb.on_key        = wukong_ai_my_mode_key_cb;
    s_ai_my_mode_cb.on_task       = wukong_ai_my_mode_task_cb;
    s_ai_my_mode_cb.on_event      = wukong_ai_my_mode_event_cb;
    s_ai_my_mode_cb.on_wakeup     = wukong_ai_my_mode_wakeup;
    s_ai_my_mode_cb.on_vad        = wukong_ai_my_mode_vad;
    s_ai_my_mode_cb.on_client     = wukong_ai_my_mode_client_run;
    s_ai_my_mode_cb.on_notify_idle = wukong_ai_my_mode_notify_idle_cb;
    
    *cb = &s_ai_my_mode_cb;
    return OPRT_OK;
}

#endif /* ENABLE_AI_MODE_MY_MODE */
```

### 步骤 2: 在模式管理器中注册

在 `wukong_ai_mode.c` 中增加对 `ai_my_mode_register` 的 extern 声明，并在 `wukong_ai_mode_init()` 里按现有模式方式注册（根据 `ENABLE_AI_MODE_MY_MODE` 开关启用，并设置 `s_ai_mode_map[AI_TRIGGER_MODE_MY_MODE]`）。不修改配置宏的前提下，需在枚举中新增 `AI_TRIGGER_MODE_MY_MODE` 并在 `_mode_str` 等表中补齐。

### 步骤 3: 添加模式枚举（若为新模式类型）

在 `wukong_ai_mode.h` 的 `AI_TRIGGER_MODE_E` 中增加新枚举值（如 `AI_TRIGGER_MODE_MY_MODE`），并同步更新 `wukong_ai_mode.c` 中的 `_mode_str` 等表；配置宏（如 `ENABLE_AI_MODE_*`）由现有构建/配置体系管理，此处不新增或修改配置宏定义。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。