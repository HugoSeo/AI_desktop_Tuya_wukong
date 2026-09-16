# Wukong Time Manage 模块

## 概述

`wukong_time_manage`（`wukong_tm.h`）是 Wukong 本地时间管理能力的统一入口，聚合闹钟、提醒、倒计时、秒表、番茄时钟五项能力：对外提供统一 service API，对内按能力拆分独立源文件（`wukong_tm_alarm.c` / `wukong_tm_reminder.c` / `wukong_tm_countdown.c` / `wukong_tm_stopwatch.c` / `wukong_tm_pomodoro.c`），闹钟/提醒/倒计时/番茄时钟均通过同级 `wukong_cron` 模块完成定时调度，秒表纯本地不依赖 cron。上层 MCP tool（`mcp_tool_tm.c`）负责解析 `device_alarm_set/query`、`device_schedule_set/query`、`device_countdown_timer_set`、`device_stopwatch_timer_set`、`device_pomodoro_timer` 六个云端/语音入口，转发到本模块的 API；本模块再通过 `WUKONG_AI_EVENT_CLOCK_MCP_*` 系列事件把状态变化（开始/暂停/恢复/结束等）以 TLV 形式回传给应用层。

各能力的详细开发者接入说明（API、事件 TLV 逐字段含义、JSON-RPC 方法、边界行为）分别见：[README_ALARM.md](./README_ALARM.md)、[README_COUNTDOWN.md](./README_COUNTDOWN.md)、[README_STOPWATCH.md](./README_STOPWATCH.md)、[README_POMODORO.md](./README_POMODORO.md)。提醒（reminder）功能较简单，未单独成文，见下节说明。

## 设计要点与坑

- **初始化顺序与时间就绪耦合**：`wukong_time_manage_init()`（`wukong_tm.c`）依次初始化 alarm → reminder → countdown → stopwatch → pomodoro，然后订阅 `EVENT_TIME_SYNC` 事件，并立即补做一次时间就绪检查（`__tm_notify_time_ready`）——避免设备启动时系统时间/时区已同步过、但初始化晚于该事件导致 cron 调度一直挂起。改动初始化顺序或新增子模块时必须保留这个「先初始化、后订阅、再补检查」的顺序，否则会引入竞态。
- **持久化不对称**：闹钟与提醒各自把数据序列化进 KV（`WUKONG_TM_ALARM_KV_KEY = "wk_tm_alarms"` / `WUKONG_TM_REMINDER_KV_KEY = "wk_tm_reminders"`，通过 `wd_common_write/read`），重启后由 `__alarm_store_load()` / `__reminder_store_load()` 恢复到内存表；**倒计时、秒表、番茄时钟不做任何持久化**，纯运行态单例，重启或反初始化后直接丢失（不会恢复）。新增能力时若需要跨重启保留状态，需照抄闹钟/提醒的 KV 存取模式，不要假设框架会自动处理。
- **过期一次性闹钟只读不触发**：`__alarm_store_load()` 会把已过期的一次性闹钟加载进内存表供 `list`/`get` 查询，但**不会**为其重建 cron job，因此不会再次触发响铃；这是有意设计（避免设备离线很久后开机瞬间补发大量过期闹钟），但容易被误读成"重启后过期闹钟应该照常响"。
- **单例类能力（倒计时/秒表/番茄时钟）互斥**：同一时刻只允许一个活跃实例，重复 `create`/`start` 返回 `OPRT_COM_ERROR`（MCP 层包装为 `already_exists` + 当前快照），不支持运行中修改配置——要改时长/配置必须先结束当前实例再新建。
- **闹钟去重与默认周期类型**：MCP `device_alarm_set` 新增闹钟时，未指定 `repeat_type` 默认建一次性闹钟（`repeat_type=0`）；若已存在相同时间的闹钟则返回 `already_exists` + `existing_id`，不会创建重复条目（`WUKONG_TM_ALARM_MAX_COUNT = 8` 条上限）。
- **事件 TLV 缓冲区生命周期**：所有 `WUKONG_AI_EVENT_CLOCK_MCP_*` 事件都是`wukong_ai_event_notify()` **同步**调用应用层回调，回调返回后 TLV 缓冲区即被模块释放；若要跨线程或延迟处理，必须在回调内自行拷贝整块 buffer（各 `README_*.md` 均有强调，是最容易踩的坑）。
- **提醒触发链路的实际调用**：`wukong_tm_reminder_fire()` → `wukong_tm_reminder_action_notify(message)` 内部调用的是 **`wukong_ai_agent_input_start(TRUE)` / `wukong_ai_agent_send_text(prompt)` / `wukong_ai_agent_input_stop()`**（`wukong_tm_reminder.c:668-670`，声明见 `wukong_ai_agent.h`），而不是 `tuya_ai_input_start/stop`；提示词模板 `REMINDER_PROMPT_TEMPLATE` 会把 `message` 包进三引号并声明"非用户对话、禁止改写/寒暄/调用 MCP 工具"以防止提示注入，且消息长度会截断到 `REMINDER_MESSAGE_MAX_LEN`（256 字节）。一次性提醒（`repeat_type=once`）触发后自动删除；周期性提醒（daily/weekly/monthly）触发后保留，由 cron 下一轮自动重触发（`wukong_tm_reminder_fire()`，`wukong_tm_reminder.c:1007-1013`）。
- **提醒（reminder）的 CRUD 与 cron 映射**：`wukong_tm_reminder.c` 提供 `wukong_tm_reminder_add/update/remove/remove_all/get/find_by_time/query_text` 全套 API。每条提醒在 `add`/`update` 时都会同步维护**恰好一个** cron job（`__reminder_sync_cron_job()`，先 `wukong_cron_job_remove()` 旧 job 再 `wukong_cron_job_add()` 新 job）：job 的 JSON-RPC `method` 固定为 `reminder.fire`，`params` 携带 `reminder_id` 与转义后的 `message`；`once` 字段随 `repeat_type` 一起写入 job（`repeat_type=once` 时 `once=1`）。时间字段在模块内部以 `start_time`（`TIME_T`，UTC 秒级时间戳）存储；MCP 层对外已改用 ISO 8601 字符串（`start_time`/`offset_minutes` 二选一），进出模块时做双向转换。
- **`device_alarm_*` / `device_schedule_*` 到本模块 API 的映射**：

  | MCP 工具 | 目标 API | 说明 |
  |---|---|---|
  | `device_alarm_set` | `wukong_tm_alarm_add()` / `wukong_tm_alarm_update()` | 由 `id` 是否已存在自动判定 add/update，不是旧版那种显式 `operation` 整数参数 |
  | `device_alarm_delete` | `wukong_tm_alarm_remove()` | |
  | `device_alarm_delete_all` | `wukong_tm_alarm_remove_all()` | 不可逆 |
  | `device_alarm_ack` | `wukong_tm_alarm_ack()` | 确认/关闭正在响铃的闹钟 |
  | `device_alarm_query` | `wukong_tm_alarm_list()` | 仅响铃类闹钟（一次性+周期），不含日程提醒 |
  | `device_schedule_set` | `wukong_tm_reminder_add()` / `wukong_tm_reminder_update()` | 同样由 `id` 是否已存在自动判定 |
  | `device_schedule_delete` | `wukong_tm_reminder_remove()` | |
  | `device_schedule_delete_all` | `wukong_tm_reminder_remove_all()` | 不可逆 |
  | `device_schedule_query` | `wukong_tm_reminder_query_text()` | 仅朗读类提醒，不含闹钟 |

  完整参数（类型/范围/默认值）见 [`../mcp/README.md`](../mcp/README.md) 的"工具参数参考"。

## 改动指南

- **调整某个能力的行为**：先改对应的 `wukong_tm_<feature>.c`，闹钟/倒计时/秒表/番茄时钟需要同步更新对应的 `README_*.md`（这些文件是详细开发者契约，字段/事件语义变了必须同步改）。
- **调整响铃/贪睡默认值**：改 `wukong_tm_alarm.c` 顶部的 `WUKONG_TM_ALARM_ACK_TIMEOUT_SEC` / `WUKONG_TM_ALARM_SNOOZE_DELAY_SEC` / `WUKONG_TM_ALARM_SNOOZE_MAX_COUNT` 宏，或运行时调用 `wukong_tm_alarm_ring_duration_set()` 等 setter；新值仅在下一轮响铃/贪睡生效。
- **新增一种定时能力**：参照 `wukong_tm_pomodoro.c` 的模式（init/deinit + cron 一次性任务驱动阶段切换），在 `wukong_tm.c` 的 `wukong_time_manage_init()`/`_deinit()` 中补齐初始化/反初始化调用（注意维护失败回滚顺序），并在 `src/wukong/mcp/tools/mcp_tool_tm.c` 增加对应 MCP tool 入口。
- **需要持久化**：照抄 `wukong_tm_alarm.c` 的 `__alarm_store_save/load` + KV key 命名模式（`wk_tm_<feature>s`），并在 `wukong_time_manage_kv_clear()` 中补充清理入口（设备恢复出厂/解绑时调用，避免残留数据占满槽位）。
- **运行测试**：单元测试在 `tests/` 目录（pytest + 自研 TAP 框架），`pytest tests/test_suite.py -v` 跑全部，`-k <name>` 筛选单个模块（`core`/`alarm`/`reminder`/`countdown`/`stopwatch`/`pomodoro`/`iso8601`）；新增测试按 `test_suite.py` 里 `_TESTS` 声明式列表的格式追加一行，并配套 `stubs_X.c` + `test_X.c`。MCP 工具层（`mcp_tool_tm.c`）的测试在 `src/wukong/mcp/tools/tests/`，不在本目录。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
