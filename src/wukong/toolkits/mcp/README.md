# Wukong MCP Server

## 概述

`src/wukong/mcp` 是一个符合 [MCP 2024-11-05 规范](https://modelcontextprotocol.io/specification/2024-11-05) 的纯 C JSON-RPC 2.0 服务器，为 AI 智能体提供设备控制能力（拍照、音量/模式、闹钟/日程/倒计时/秒表/番茄钟、播放控制、即时通讯、社交、运动控制等）。

分三层：

- **框架层**（`mcp_server.c` 等）：JSON-RPC 路由、传输、Tools/Resources/Prompts/Logging 四个协议能力，由 `mcp_server.h` 里的 `MCP_ENABLE_*` 宏控制编译（默认只开 `MCP_ENABLE_TOOLS`，其余三个默认关闭）。
- **应用初始化层**（`wukong_ai_mcp.c`）：`wukong_ai_mcp_init()` 调用 `mcp_server_init()` 后，按 `ENABLE_TOOLKITS_*` 宏逐个初始化各工具模块。
- **工具实现层**（`tools/mcp_tool_*.c`）：每个模块通过 `MCP_TOOL_ADD()` 注册一个或多个工具，独立实现业务逻辑。

接入方式：应用在初始化阶段调用 `wukong_ai_mcp_init()`；退出时调用 `wukong_ai_mcp_deinit()` 释放所有已注册的工具/资源/提示。工具粒度的编译开关是 Kconfig 里的 `ENABLE_TOOLKITS_*` 系列（`src/wukong/mcp/Kconfig`），生成到 `tuya_app_config.h` 时为**裸宏名**（如 `#define ENABLE_TOOLKITS_CONTROL 1`，无 `CONFIG_` 前缀）；`CONFIG_` 前缀形式只出现在 `.config` 快照与 Makefile 判断中（`local.mk` 的 `ifeq ($(CONFIG_ENABLE_TOOLKITS_XXX), y)` 据此决定源文件是否参与编译）。

## 设计要点与坑

- **两级开关不要混淆**：`MCP_ENABLE_*`（框架能力，`mcp_server.h`，编译期常量，一般不需要改）和 `ENABLE_TOOLKITS_*`（应用工具，Kconfig 配置项）是两套独立的宏。改错层级是新增工具时最常见的坑——工具不生效，先确认 `.config` 快照里 `CONFIG_ENABLE_TUYA_TOOLKITS=y` 这个总开关，再确认对应 `CONFIG_ENABLE_TOOLKITS_XXX=y`（C 代码里对应的判断则是裸名 `ENABLE_TOOLKITS_XXX`）。
- **各工具默认值不一致，且不是都在 mcp Kconfig 里统一 default y**：`ENABLE_TOOLKITS_*` 系列里只有 `ENABLE_TOOLKITS_CONTROL` 在 Kconfig 里 `default y`（另有板级限定的 `ENABLE_APP_MOTION_ROTATION_MCP` 在 `if T5AI_BOARD_DESKTOP` 下 `default y`，不属于该系列）；`CAMERA`/`TM`/`PLAYBACK`/`IMM`/`SOCIAL`/`MOTION` 默认都是 `n`，实际是否开启由各板级预置配置（`build/appconfig/<BOARD>`）显式打开的（例如 T5AI_BOARD 打开了 CAMERA+TM，桌面版只打开 CAMERA+TM 不含 MOTION）。不要假设某个 toolkit "默认就有"。
- **闹钟与日程已统一到 `ENABLE_TOOLKITS_TM` 一个开关**：`device_alarm_set/query`、`device_countdown_timer_set`、`device_stopwatch_timer_set`、`device_pomodoro_timer`、`device_schedule_set/query` 全部由 `mcp_tool_tm.c` 一个模块提供，历史上曾经存在的独立日程开关已不存在，不要在配置里找一个单独的 schedule 宏。
- **闹钟（alarm）响铃，日程（schedule）朗读**：这是两个语义完全不同的工具，别以为是同一件事的两种叫法——`device_alarm_set` 到点只响铃，`message` 字段只是备注不会被朗读；需要 AI 语音朗读内容的场景必须用 `device_schedule_set`。
- **单实例工具不支持并发多个**：倒计时、秒表、番茄钟都是单实例语义，运行中不能直接改参数，需要先停止/删除再重建，调用方需要自己处理这个约束，工具内部不做多实例管理。
- **IMM / Social 用回调注册模式解耦平台实现**：`mcp_tool_imm_register_platform()` / `mcp_tool_social_register_platform()` 允许外部模块（如具体的微信/微博对接代码）在工具模块之外注册平台实现，工具本身只是壳；未注册的平台调用会返回 "Platform not integrated yet"，这不是 bug，是特意留的扩展点。
- **工具调用是异步 worker 执行的**：`mcp_server_tools.c` 通过 `tal_workq_schedule(WORKQ_SYSTEM, ...)` 在系统工作队列里跑 handler，handler 内部不要做假设它跑在调用方线程里的事情（如直接操作调用方线程局部状态）。

## 改动指南

新增一个 MCP 工具的完整步骤（新建 tool 模块、注册、Kconfig 开关、local.mk 条件编译、验证）见 [`../../../docs/howto/add-mcp-tool.md`](../../../docs/howto/add-mcp-tool.md)，本文不重复。

需要新增/修改某个已有工具的行为（如闹钟 `rolled_to_next_day` 逻辑、播放控制的 `PlayControl` 异步响应分发）时，先读对应 `tools/mcp_tool_*.c` 的实现和注释，再动代码。

## 工具参数参考

以下参数表逐一对照当前 `tools/mcp_tool_*.c` 里的 `MCP_TOOL_ADD()` / `MCP_SCHEMA_*` 声明核实（而非旧版协议草稿），是目前唯一一份权威的工具参数契约。**注意**：闹钟 / 倒计时 / 秒表 / 番茄时钟 / 日程这五类工具已从旧版"一个工具 + `operation` 整数编码增删改查"的形态，重构为"按操作拆分成独立工具、`action` 用字符串枚举、时间用 ISO 8601 字符串"的形态，且新增/更新不再需要显式传 `operation`——由 `id` 是否已存在自动判定（`__set_alarm()` / `__set_schedule()` 内部探测 `wukong_tm_alarm_get()` / `wukong_tm_reminder_get()`）。若你在旧文档或记忆中见过 `operation: 0/1/2/3` 这类参数，那已经是过时协议，以下表格为准。

### 设备控制（`mcp_tool_control.c`）

| 工具 | 参数 | 说明 / 边界语义 |
|---|---|---|
| `device_info_get` | 无 | 返回 `model`/`serialNumber`/`firmwareVersion` |
| `device_audio_volume_get` | 无 | 返回当前音量（0-100） |
| `device_audio_volume_set` | `volume`: int [0-100] 必填 | 越界自动钳位到 [0,100]；设置后同步上报 `dpid=3` |
| `device_audio_mode_set` | `mode`: enum 必填 (`hold_to_talk`/`press_to_talk`/`wake_word`/`free_conversation`) | 切换对话子模式（对应长按说话/按键说话/唤醒词/自由对话） |
| `device_audio_mode_get` | 无 | 查询当前对话子模式 |

### 拍照（`mcp_tool_camera.c`）

| 工具 | 参数 | 说明 / 边界语义 |
|---|---|---|
| `device_camera_take_photo` | 无 | 抓拍一张照片并以图片内容返回；若同时启用 `ENABLE_TUYA_PICTURE`，会自动存入相册并推送到显示总线 |

### 时间管理 — 闹钟（`mcp_tool_tm.c`）

| 工具 | 参数 | 说明 / 边界语义 |
|---|---|---|
| `device_alarm_set` | `id`: str 必填；`start_time`: str(ISO 8601) 新增时必填，周期闹钟只取时分；`repeat_type`: enum 可选 (`once`/`daily`/`weekly`/`monthly`，缺省 `once`)；`weekday_mask`: int [0-127] 可选（bit0=周日..bit6=周六，工作日=62，周末=65）；`month_day`: int [1-31] 可选；`message`: str 可选（仅存储备注，响铃时**不朗读**）；`enabled`: bool 可选 | 新增/更新由 `id` 是否已存在自动判定，不再需要显式 `operation`；一次性闹钟若指定时间已过，自动顺延到明天并返回 `{"success":true,"rolled_to_next_day":true,"start_time":"<ISO8601>"}`；新增时若同一时间已存在闹钟，返回 `{"success":false,"reason":"...","existing_id":"<id>"}`；最多 8 条（`WUKONG_TM_ALARM_MAX_COUNT`） |
| `device_alarm_delete` | `id`: str 必填 | 按 id 删除 |
| `device_alarm_delete_all` | 无 | 删除全部闹钟，不可逆，仅在用户明确要求清空时调用 |
| `device_alarm_ack` | `id`: str 必填 | 确认/关闭正在响铃的闹钟 |
| `device_alarm_query` | 无 | 仅返回本地闹钟（一次性 + 周期性，响铃类），**不含** `device_schedule_*` 的朗读型提醒；与 `device_schedule_query` 是互不重叠的两个查询面 |

### 时间管理 — 倒计时 / 秒表 / 番茄时钟（`mcp_tool_tm.c`）

| 工具 | 参数 | 说明 / 边界语义 |
|---|---|---|
| `device_countdown_timer_set` | `hour_duration`: int [0-24] 可选（仅 create 用）；`minute_duration`: int [0-60] 可选；`second_duration`: int [0-60] 可选；`action`: enum 必填 (`create`/`pause`/`resume`/`delete`) | 单实例，不支持原地改参数；重复 `create` 返回 `{"success":false,"current":{...}}`；`pause`/`delete` 成功返回 `remaining_sec`+`elapsed_sec` |
| `device_countdown_timer_query` | `action`: 固定 `"query"` | 返回 `active`/`state`/`remaining_sec`/`duration_sec`/`elapsed_sec` |
| `device_stopwatch_timer_set` | `action`: enum 必填 (`start`/`pause`/`resume`/`stop`/`reset`) | 单实例正计时；重复 `start` 返回 `{"success":false,"current":{...}}`；`pause`/`stop`/`reset` 成功返回 `elapsed_sec` |
| `device_stopwatch_timer_query` | `action`: 固定 `"query"` | 返回 `active`/`paused`/`elapsed_sec` |
| `device_pomodoro_start` | `action`: 固定 `"start"`；`work_duration`: int [1-120] 可选（默认 25）；`short_break_duration`: int [1-30] 可选（默认 5）；`long_break_duration`: int [5-60] 可选（默认 15）；`work_sessions_before_long_break`: int [1-12] 可选（默认 4） | 单实例，运行中不可改配置；重复 `start` 返回 `{"success":false,"current":{...}}` |
| `device_pomodoro_control` | `action`: enum 必填 (`pause`/`resume`/`stop`) | 控制运行中的番茄时钟；成功返回 `{"success":true,"action":"...","active":...,"paused":...,"phase":"...","remaining_sec":N,"phase_start_time":"<ISO8601>","phase_end_time":"<ISO8601>"}`；`stop` 后实例销毁，返回 `{"success":true,"action":"stop","active":false}` |
| `device_pomodoro_query` | `action`: 固定 `"query"` | 返回当前阶段/剩余时间等快照；`remaining_sec`/`elapsed_sec` 单位为秒，播报前需转换为分钟 |

### 时间管理 — 日程（schedule → reminder，`mcp_tool_tm.c`）

| 工具 | 参数 | 说明 / 边界语义 |
|---|---|---|
| `device_schedule_set` | `id`: str 必填；`start_time`: str(ISO 8601) 可选（与 `offset_minutes` 二选一，同时提供时 `start_time` 优先）；`offset_minutes`: int [1-525600] 可选（相对当前设备时间的分钟数）；`repeat_type`/`weekday_mask`/`month_day` 语义同闹钟；`message`: str（新增时必填，触发时**朗读**） | 新增/更新同样由 `id` 是否已存在自动判定；解析后的触发时间必须晚于当前时间，否则返回 `reason` 说明；周期性提醒不会自动删除；映射到 `wukong_tm_reminder_add()`/`wukong_tm_reminder_update()` |
| `device_schedule_delete` | `id`: str 必填 | 映射到 `wukong_tm_reminder_remove()` |
| `device_schedule_delete_all` | 无 | 删除全部提醒，不可逆；映射到 `wukong_tm_reminder_remove_all()`，返回 `removed_count` |
| `device_schedule_query` | `start_date`/`end_date`: str(ISO 8601 日期或日期时间) 可选（含边界，纯日期覆盖全天）；`keyword`: str 可选（匹配 `message`） | 只查一次性/周期性 schedule 提醒，**不含**闹钟；映射到 `wukong_tm_reminder_query_text()`；与 `device_alarm_query` 语义模糊时（如"我有什么日程安排"）应两个都调用 |

### 播放与曲库（`mcp_tool_playback.c`）

| 工具 | 参数 | 说明 / 边界语义 |
|---|---|---|
| `device_playback_control` | `action`: str 必填 (`next`/`prev`/`pause`/`resume`/`replay`/`single_loop`/`sequential_loop`/`no_loop`/`auto_play_next_on`/`auto_play_next_off`) | `next`/`prev` 从云端取新曲目并返回 `song_name`+`artist`；`next` 也用于开始播放 |
| `device_playback_status` | 无 | 返回 `song_name`/`artist`/`state`(playing/paused/stopped)/`auto_play_next` |
| `device_music_search` | `keyword`/`tag`/`name`/`artist`/`album_name`: str 可选；`offset`: int 可选（默认 0）；`limit`: int [1-5] 可选（默认 5，硬上限 `MUSIC_SEARCH_LIST_LIMIT_CAP=5`） | 一次调用完成搜索 + 全部结果入队 + 自动播放第一首，不需要再单独调用播放 |
| `device_playlist_list` | 无 | 返回播放列表 `{name, artist}` 数组 |
| `device_playlist_clear` | 无 | 清空播放列表；切换曲风前建议先调用 |

### 即时通讯（`mcp_tool_imm.c`）

| 工具 | 参数 | 说明 / 边界语义 |
|---|---|---|
| `device_imm_send` | `platform`: str 必填 (`wechat`/`feishu`/`discord`/`whatsapp`)；`contact`: str 必填；`message`: str 必填 | 平台未通过 `mcp_tool_imm_register_platform()` 注册发送回调时返回 `"Platform not integrated yet"` |
| `device_imm_query` | `platform`: str 必填；`contact`: str 可选（缺省查全部联系人）；`count`: int [1-50] 可选（默认 10） | 平台未注册查询回调时同样返回 `"Platform not integrated yet"` |

### 社交媒体（`mcp_tool_social.c`）

| 工具 | 参数 | 说明 / 边界语义 |
|---|---|---|
| `device_social_feed` | `platform`: str 必填 (`weibo`/`youtube`/`x`/`tiktok`/`bilibili`)；`feed_type`: str 可选 (`trending` 默认/`following`/`user`/`search`)；`query`: str 可选（`user`/`search` 时需要）；`count`: int [1-50] 可选（默认 10） | 平台未注册 feed 回调时返回 `"Platform not integrated yet"` |

### 运动控制（`mcp_tool_motion.c`）

| 工具 | 参数 | 说明 / 边界语义 |
|---|---|---|
| `device_motion_control_set` | `motion_mode`: int [0-7] 必填（0=左转/1=右转/2=指定角度/3=顺时针/4=逆时针/5=定点/6=复位/7=停止）；`rotate_value`: int [0-3600] 必填（≤360 视为角度，>360 视为圈数） | |

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
