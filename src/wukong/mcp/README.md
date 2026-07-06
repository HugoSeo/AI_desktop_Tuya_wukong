# Wukong MCP Server v3.0

## 概述

Wukong MCP Server 是一个**符合 [MCP 2024-11-05 规范](https://modelcontextprotocol.io/specification/2024-11-05)** 的纯 C 语言 JSON-RPC 2.0 服务器，为 AI 智能设备提供标准化的能力接口。

v3.0 采用**模块化架构**，分为三层：

- **MCP 框架层** — 协议路由、传输、能力模块（Tools / Resources / Prompts / Logging），通过 `MCP_ENABLE_*` 宏控制编译
- **应用初始化层** — `wukong_ai_mcp.c` 统一初始化各 tool 模块，通过 `ENABLE_TOOLKITS_*` 宏控制使能
- **工具实现层** — `tools/mcp_tool_*.c` 独立实现各业务工具，通过 `MCP_TOOL_ADD` 注册

---

## 架构

```
                    AI Agent / LLM
                         │
                    JSON-RPC 2.0
                         ▼
┌────────────────────────────────────────────────────────┐
│             mcp_server.c (Core Framework)              │
│  Router ──► initialize / ping                          │
│         ──► tools/*      (if MCP_ENABLE_TOOLS)         │
│         ──► resources/*  (if MCP_ENABLE_RESOURCES)     │
│         ──► prompts/*    (if MCP_ENABLE_PROMPTS)       │
│         ──► logging/*    (if MCP_ENABLE_LOGGING)       │
│                                                        │
│  Transport: reply_result / reply_error /                │
│             send_notification                          │
├────────────────────────────────────────────────────────┤
│  mcp_content.h/c         │  Capability Modules         │
│  make_text / make_image  │  mcp_server_tools.c         │
│  make_resource / base64  │  mcp_server_resources.c     │
│                          │  mcp_server_prompts.c       │
│                          │  mcp_server_logging.c       │
└────────────────────────────────────────────────────────┘
                         │
                         ▼
┌────────────────────────────────────────────────────────┐
│           wukong_ai_mcp.c (App Init Layer)             │
│  mcp_server_init("Wukong AI", "3.0")                   │
│                                                        │
│  #if ENABLE_TOOLKITS_CONTROL                           │
│      mcp_tool_control_init()                           │
│  #if ENABLE_TOOLKITS_CAMERA                            │
│      mcp_tool_camera_init()                            │
│  ...                                                   │
└────────────────────────────────────────────────────────┘
                         │
                         ▼
┌────────────────────────────────────────────────────────┐
│           tools/mcp_tool_*.c (Tool Modules)            │
│  每个模块通过 MCP_TOOL_ADD() 注册一个或多个工具         │
│                                                        │
│  control: device_info_get, volume_set, mode_set        │
│  camera:  device_camera_take_photo                     │
│  tm: alarm / reminder /                   │
│                   countdown / stopwatch / pomodoro     │
│  playback: device_playback_control                     │
│  imm:     device_imm_send, device_imm_query            │
│  social:  device_social_feed                           │
│  motion:  device_motion_control_set                    │
└────────────────────────────────────────────────────────┘
```

---

## 文件结构

```
src/wukong/mcp/
├── wukong_ai_mcp.h           # 伞头文件（include tools/content + init/deinit 声明）
├── wukong_ai_mcp.c           # 应用初始化（统一调用时间管理 tool init）
├── mcp_server.h              # 核心公共 API + MCP_ENABLE_* 编译配置
├── mcp_server.c              # 路由、传输、生命周期（initialize, ping）
├── mcp_server_internal.h     # 模块内部 API（传输函数、handler 声明）
├── mcp_content.h             # 内容构建辅助函数、MIME 常量
├── mcp_content.c
├── mcp_server_tools.h        # Tools 公共 API（回调类型、Schema 宏、注册）
├── mcp_server_tools.c        # tools/list, tools/call, 异步 worker
├── mcp_server_resources.h    # Resources 公共 API
├── mcp_server_resources.c    # list, read, templates, subscribe
├── mcp_server_prompts.h      # Prompts 公共 API
├── mcp_server_prompts.c      # list, get
├── mcp_server_logging.h      # Logging 公共 API（日志级别常量）
├── mcp_server_logging.c      # setLevel handler, log function
├── README.md
└── tools/                    # 应用层工具模块
    ├── mcp_tool_control.h/c       # device_info, volume, mode
    ├── mcp_tool_camera.h/c        # device_camera_take_photo
    ├── mcp_tool_tm.h/c            # alarm, reminder/schedule, countdown, stopwatch, pomodoro
    ├── mcp_tool_playback.h/c      # playback_control
    ├── mcp_tool_imm.h/c           # instant messaging (WeChat, Feishu, ...)
    ├── mcp_tool_social.h/c        # social media feed (Weibo, YouTube, X, ...)
    └── mcp_tool_motion.h/c        # motion_control_set
```

---

## 编译时配置

### 框架能力（MCP_ENABLE_*）

在 `mcp_server.h` 中定义，控制 MCP 协议能力模块的编译：

```c
#ifndef MCP_ENABLE_TOOLS
#define MCP_ENABLE_TOOLS        1   /* 默认启用 */
#endif
#ifndef MCP_ENABLE_RESOURCES
#define MCP_ENABLE_RESOURCES    0   /* 默认关闭 */
#endif
#ifndef MCP_ENABLE_PROMPTS
#define MCP_ENABLE_PROMPTS      0   /* 默认关闭 */
#endif
#ifndef MCP_ENABLE_LOGGING
#define MCP_ENABLE_LOGGING      0   /* 默认关闭 */
#endif
```

在构建系统或项目配置头中覆盖即可启用/禁用：

```c
#define MCP_ENABLE_RESOURCES    1
#define MCP_ENABLE_LOGGING      1
```

禁用后：
- 对应 `.c` 文件内容为空（零代码体积）
- JSON-RPC 路由表不包含对应方法
- `initialize` 响应不声明对应 capability

### 应用工具（ENABLE_TOOLKITS_*）

在 Kconfig 中配置，由构建系统自动生成到 `tuya_app_config.h`，控制各 tool 模块的编译链接和初始化：

| 宏 | 说明 | 默认 |
|----|------|------|
| `ENABLE_TUYA_TOOLKITS` | 总开关：控制整个 MCP 模块 | 1 |
| `ENABLE_TOOLKITS_CONTROL` | 设备控制（信息/音量/模式） | 1 |
| `ENABLE_TOOLKITS_CAMERA` | 拍照 | 1 |
| `ENABLE_TOOLKITS_TM` | 时间管理（闹钟/提醒/倒计时/正计时/番茄时钟）的配置口径，MCP 侧统一初始化 | 1 |
| `ENABLE_TOOLKITS_SCHEDULE` | 历史日程开关口径，能力已并入统一时间管理 MCP | 1 |
| `ENABLE_TOOLKITS_PLAYBACK` | 播放控制 | 0 |
| `ENABLE_TOOLKITS_IMM` | 即时通讯 | 0 |
| `ENABLE_TOOLKITS_SOCIAL` | 社交媒体 | 0 |
| `ENABLE_TOOLKITS_MOTION` | 运动控制 | 0 |

两级配置的关系：

| 级别 | 宏 | 定义位置 | 控制范围 |
|------|-----|---------|---------|
| 框架能力 | `MCP_ENABLE_TOOLS` 等 | `mcp_server.h` | Tools/Resources/Prompts/Logging 协议能力 |
| 应用工具 | `ENABLE_TOOLKITS_*` | `tuya_app_config.h` (Kconfig) | 各 tool 模块的编译和初始化 |

---

## 构建系统集成

在 `local.mk` 中，MCP 模块的源文件通过 Kconfig 条件编译：

```makefile
# 总开关：启用 MCP 框架
ifeq ($(CONFIG_ENABLE_TUYA_TOOLKITS), y)
# 编译框架核心文件（mcp_server.c, mcp_content.c, mcp_server_tools.c, ...）
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/mcp -maxdepth 1 -name "*.c")

# 各 tool 模块按需编译
ifeq ($(CONFIG_ENABLE_TOOLKITS_CONTROL), y)
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/mcp/tools/mcp_tool_control.c
endif
ifeq ($(CONFIG_ENABLE_TOOLKITS_CAMERA), y)
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/mcp/tools/mcp_tool_camera.c
endif
# ... 其他 tool 同理
endif
```

头文件搜索路径已包含 `src/wukong/mcp`（在 `local.mk` 第 45 行），因此 tool 模块中可直接 `#include "wukong_ai_mcp.h"`。

---

## 工具清单

### control — 设备控制

| 工具名 | 说明 | 参数 |
|--------|------|------|
| `device_info_get` | 获取设备型号、序列号、固件版本 | 无 |
| `device_audio_volume_get` | 查询当前设备音量 (0-100) | 无 |
| `device_audio_volume_set` | 设置设备音量。不确定当前音量时先调用 `device_audio_volume_get`。 | `volume`: int [0-100] 必填 |
| `device_audio_mode_set` | 设置语音交互模式 | `mode`: int [0-3] 必填 (0=长按说话, 1=按键说话, 2=唤醒词, 3=自由对话) |

### camera — 拍照

| 工具名 | 说明 | 参数 |
|--------|------|------|
| `device_camera_take_photo` | 拍照并返回图片。用户请求查看、识别或描述视觉内容时必须先调用此工具。 | 无 |

### time-management — 时间管理

| 工具名 | 说明 | 参数 |
|--------|------|------|
| `device_alarm_set` | 管理本地闹钟和**重复提醒**（每天/每周/每月）。支持 `message` 字段存储语义备注。一次性（`repeat_type=0`）闹钟需提供日期且必须在未来。仅一次性的带消息提醒请用 `device_schedule_set`。更新仅合并传入的字段。 | `operation`: int [0-3] 必填 (0=添加, 1=删除, 2=修改, 3=确认响铃), `id`: str 必填, `repeat_type`: int [0-3] 可选 (0=单次, 1=每天, 2=每周, 3=每月), `year`/`month`/`day`: int 可选, `hour`/`minute`: int 可选, `weekday_mask`: int [0-127] 可选, `month_day`: int [1-31] 可选, `message`: str 可选 |
| `device_alarm_query` | 查询本地闹钟，仅返回闹钟记录，不包含提醒事项。 | 无 |
| `device_countdown_timer_set` | 单实例倒计时。不支持原地修改；`query`（4）查剩余/总时长/已运行；`pause`/`delete` 成功返回 `remaining_sec` 与 `elapsed_sec`。 | `operation`: int [0-4] 必填 (0=创建, 1=暂停, 2=恢复, 3=删除, 4=查询), `hour_duration`: int [0-24] 可选, `minute_duration`: int [0-60] 可选, `second_duration`: int [0-60] 可选 |
| `device_stopwatch_timer_set` | 单实例正计时/秒表。`pause`/`stop`/`reset` 成功返回 `elapsed_sec`；`query` 用于运行中查询。 | `operation`: int [0-5] 必填 (0=开始, 1=暂停, 2=恢复, 3=停止, 4=重置, **5=查询**) |
| `device_pomodoro_timer_set` | 番茄时钟。单实例，运行中不可修改配置，需先停止再重建。 | `operation`: int [0-4] 必填 (0=开始, 1=暂停, 2=恢复, 3=停止, 4=查询), `work_duration`: int [1-120] 可选(默认25), `short_break_duration`: int [1-30] 可选(默认5), `long_break_duration`: int [5-60] 可选(默认15), `work_sessions_before_long_break`: int [1-12] 可选(默认4) |
| `device_schedule_set` | 管理**一次性**本地提醒（带消息）。重复提醒（每天/每周/每月）请用 `device_alarm_set`。时间必须在未来，已过的时间需推到下一次（如今天已过则改为明天）。失败时返回 `reason`（如 `time_in_the_past`）。更新仅合并传入的字段，省略的保持不变。 | `operation`: int [0-2] 必填 (0=添加, 1=删除, 2=更新), `id`: str 必填, `categories`: int [0-6] 可选 (0=会议, 1=工作, 2=个人, 3=健康, 4=学习, 5=社交, 6=其他), `year`/`month`/`day`/`hour`/`minute`: int 可选(添加时必填), `message`: str 可选(添加时必填) |
| `device_schedule_query` | 查询本地提醒事项，支持时间范围、分类和关键词过滤。 | `query_method`: int [0-2] 可选 (0=按时间范围, 1=按分类, 2=按关键词), `start_timestamp`/`end_timestamp`: int 可选, `categories`: int [0-6] 可选 (0=会议, 1=工作, 2=个人, 3=健康, 4=学习, 5=社交, 6=其他), `keyword`: str 可选 |

### playback — 播放控制

| 工具名 | 说明 | 参数 |
|--------|------|------|
| `device_playback_control` | 音乐/故事播放控制。`next`/`prev` 从云端获取新曲目，`next` 也用于开始播放音乐。 | `action`: str 必填 (`resume`/`pause`/`replay`/`prev`/`next`/`single_loop`/`sequential_loop`/`no_loop`) |
| `device_playback_status` | 获取当前播放状态，包含歌名、歌手、URL 和播放状态。 | 无 |

### imm — 即时通讯

| 工具名 | 说明 | 参数 |
|--------|------|------|
| `device_imm_send` | 发送即时消息 | `platform`: str 必填 (`wechat`/`feishu`/`discord`/`whatsapp`), `contact`: str 必填, `message`: str 必填 |
| `device_imm_query` | 查询即时消息 | `platform`: str 必填, `contact`: str 可选, `count`: int [1-50] 可选(默认10) |

### social — 社交媒体

| 工具名 | 说明 | 参数 |
|--------|------|------|
| `device_social_feed` | 获取社交媒体动态 | `platform`: str 必填 (`weibo`/`youtube`/`x`/`tiktok`/`bilibili`), `feed_type`: str 可选 (`trending`/`following`/`user`/`search`, 默认`trending`), `query`: str 可选, `count`: int [1-50] 可选(默认10) |

### motion — 运动控制

| 工具名 | 说明 | 参数 |
|--------|------|------|
| `device_motion_control_set` | 旋转/运动控制 | `motion_mode`: int [0-7] 必填 (0=左转, 1=右转, 2=指定角度, 3=顺时针, 4=逆时针, 5=定点, 6=复位, 7=停止), `rotate_value`: int [0-3600] 必填 |

---

## 快速开始

### 1. 初始化

```c
#include "wukong_ai_mcp.h"

wukong_ai_mcp_init();   /* 内部调用 mcp_server_init() 并注册所有已启用的 tool */
```

### 2. 注册工具

```c
STATIC OPERATE_RET __set_volume(CONST CHAR_T *name, CONST ty_cJSON *args,
                                 ty_cJSON **out_content, BOOL_T *is_error,
                                 VOID *user_data)
{
    INT_T volume = 50;
    ty_cJSON *j = ty_cJSON_GetObjectItem(args, "volume");
    if (j && ty_cJSON_IsNumber(j))
        volume = j->valueint;

    set_hw_volume(volume);

    *out_content = ty_cJSON_CreateArray();
    ty_cJSON_AddItemToArray(*out_content, mcp_content_make_text("OK"));
    return OPRT_OK;
}

MCP_TOOL_ADD("device_audio_volume_set",
             "Sets the device's volume level (0-100).",
             __set_volume, NULL,
             MCP_SCHEMA_INT_RANGE("volume", "Volume level (0-100)", 0, 100));
```

### 3. 创建独立工具模块（推荐）

```c
// tools/mcp_tool_my_feature.h
OPERATE_RET mcp_tool_my_feature_init(VOID);

// tools/mcp_tool_my_feature.c
#include "mcp_tool_my_feature.h"
#include "wukong_ai_mcp.h"

STATIC OPERATE_RET __handler(CONST CHAR_T *name, CONST ty_cJSON *args,
                              ty_cJSON **out_content, BOOL_T *is_error,
                              VOID *user_data) { /* ... */ }

OPERATE_RET mcp_tool_my_feature_init(VOID) {
    return MCP_TOOL_ADD("my_feature", "...", __handler, NULL,
                         MCP_SCHEMA_STR("param", "Description"));
}
```

### 4. 外部注册

`MCP_TOOL_ADD` 是公共 API，任何模块都可以在 `mcp_server_init()` 之后注册新工具：

```c
#include "wukong_ai_mcp.h"

void my_plugin_init(void) {
    MCP_TOOL_ADD("plugin_feature", "...", my_handler, NULL,
                 MCP_SCHEMA_STR("param", "Description"));
}
```

### 5. 清理

```c
wukong_ai_mcp_deinit();  /* 内部调用 mcp_server_destroy()，释放所有已注册的工具/资源/提示 */
```

---

## 新增工具指南

### 步骤 1：创建模块文件

```c
// tools/mcp_tool_xxx.h
#ifndef __MCP_TOOL_XXX_H__
#define __MCP_TOOL_XXX_H__
#include "tuya_cloud_types.h"
OPERATE_RET mcp_tool_xxx_init(VOID);
#endif

// tools/mcp_tool_xxx.c
#include "mcp_tool_xxx.h"
#include "wukong_ai_mcp.h"
#include "tal_log.h"

STATIC OPERATE_RET __my_handler(CONST CHAR_T *name, CONST ty_cJSON *args,
                                 ty_cJSON **out_content, BOOL_T *is_error,
                                 VOID *user_data)
{
    /* 实现逻辑 */
    *out_content = ty_cJSON_CreateArray();
    ty_cJSON_AddItemToArray(*out_content, mcp_content_make_text("OK"));
    return OPRT_OK;
}

OPERATE_RET mcp_tool_xxx_init(VOID) {
    return MCP_TOOL_ADD("device_xxx", "Tool description.",
                         __my_handler, NULL,
                         MCP_SCHEMA_STR("param1", "Param description."));
}
```

### 步骤 2：在 `wukong_ai_mcp.c` 中注册

```c
#include "tools/mcp_tool_xxx.h"

// 在 wukong_ai_mcp_init() 中添加：
#if defined(ENABLE_TOOLKITS_XXX) && (ENABLE_TOOLKITS_XXX == 1)
    TUYA_CALL_ERR_LOG(mcp_tool_xxx_init());
#endif
```

### 步骤 3：在 `local.mk` 中添加条件编译

```makefile
ifeq ($(CONFIG_ENABLE_TOOLKITS_XXX), y)
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/mcp/tools/mcp_tool_xxx.c
endif
```

### 步骤 4：在 Kconfig 中添加配置项

在对应的 Kconfig 菜单中添加：

```
config ENABLE_TOOLKITS_XXX
    bool "Enable XXX toolkit"
    default n
```

---

## 回调扩展模式

IMM（即时通讯）和 Social（社交媒体）模块采用**平台回调注册**模式，允许外部模块注册平台实现而无需修改 tool 代码本身：

```c
/* 外部模块注册微信发送/查询实现 */
#include "tools/mcp_tool_imm.h"

STATIC OPERATE_RET my_wechat_send(IMM_PLATFORM_E platform,
                                    CONST CHAR_T *contact,
                                    CONST CHAR_T *message)
{
    /* 实际微信 API 调用 */
    return OPRT_OK;
}

void my_wechat_init(void) {
    mcp_tool_imm_register_platform(IMM_PLATFORM_WECHAT, my_wechat_send, NULL);
}
```

同理，Social 模块：

```c
#include "tools/mcp_tool_social.h"

STATIC OPERATE_RET my_weibo_feed(SOCIAL_PLATFORM_E platform,
                                   SOCIAL_FEED_TYPE_E feed_type,
                                   CONST CHAR_T *query, INT_T count,
                                   CHAR_T **out_json)
{
    /* 实际微博 API 调用 */
    return OPRT_OK;
}

void my_weibo_init(void) {
    mcp_tool_social_register_platform(SOCIAL_PLATFORM_WEIBO, my_weibo_feed);
}
```

未注册回调的平台会返回 "Platform not integrated yet" 错误。

---

## API 参考

### 核心 API (`mcp_server.h`)

| 函数 | 说明 |
|------|------|
| `mcp_server_init(name, version)` | 初始化服务器 |
| `mcp_server_destroy()` | 销毁服务器 |
| `mcp_server_handle_message(sid, eid, msg, ud)` | JSON-RPC 消息处理入口 |

### 工具 API (`mcp_server_tools.h`)

| 函数 / 宏 | 说明 |
|-----------|------|
| `MCP_TOOL_ADD(name, desc, handler, ud, ...)` | 注册工具（自动追加 MCP_SCHEMA_END） |
| `mcp_server_tool_register(...)` | 底层注册函数（变参，需手动追加 NULL 终止） |
| `mcp_server_notify_tools_changed()` | 发送 notifications/tools/list_changed |

### 内容构建 (`mcp_content.h`)

| 函数 | 说明 |
|------|------|
| `mcp_content_make_text(text)` | 文本内容 |
| `mcp_content_make_image(mime, data, len)` | 图片内容（自动 Base64） |
| `mcp_content_make_image_base64(mime, b64)` | 图片内容（已编码） |
| `mcp_content_make_resource(uri, mime, text)` | 嵌入资源内容 |

### Schema 宏 (`mcp_server_tools.h`)

| 宏 | 说明 |
|----|------|
| `MCP_SCHEMA_INT(n, d)` | 必填整数 |
| `MCP_SCHEMA_INT_OPT(n, d)` | 可选整数 |
| `MCP_SCHEMA_INT_RANGE(n, d, lo, hi)` | 必填整数 + 范围 |
| `MCP_SCHEMA_INT_OPT_RANGE(n, d, lo, hi)` | 可选整数 + 范围 |
| `MCP_SCHEMA_STR(n, d)` / `MCP_SCHEMA_STR_OPT(n, d)` | 必填/可选字符串 |
| `MCP_SCHEMA_BOOL(n, d)` / `MCP_SCHEMA_BOOL_OPT(n, d)` | 必填/可选布尔 |
| `MCP_SCHEMA_NUM(n, d)` / `MCP_SCHEMA_NUM_OPT(n, d)` | 必填/可选浮点数 |

### 资源 API (`mcp_server_resources.h`)

| 函数 | 说明 |
|------|------|
| `mcp_server_resource_add(...)` | 注册资源 |
| `mcp_server_resource_remove(uri)` | 移除资源 |
| `mcp_server_resource_notify_updated(uri)` | 资源更新通知 |
| `mcp_server_notify_resources_changed()` | 资源列表变更通知 |

### 提示 API (`mcp_server_prompts.h`)

| 函数 | 说明 |
|------|------|
| `mcp_server_prompt_add(...)` | 注册提示模板 |
| `mcp_server_notify_prompts_changed()` | 提示列表变更通知 |

### 日志 API (`mcp_server_logging.h`)

| 函数 | 说明 |
|------|------|
| `mcp_server_log(level, logger, fmt, ...)` | 发送结构化日志 |

---

## 依赖项

- TuyaOS SDK: `tuya_ai_agent.h`
- JSON: `ty_cJSON.h`
- 日志: `tal_log.h`
- 内存: `tal_memory.h`
- 工作队列: `tal_workq_service.h`
- Base64: `utilities/uni_base64.h`
- 字符串: `utilities/mix_method.h`

---

## 参考资料

- [Model Context Protocol 规范 (2024-11-05)](https://modelcontextprotocol.io/specification/2024-11-05)
- [JSON-RPC 2.0 Specification](https://www.jsonrpc.org/specification)
