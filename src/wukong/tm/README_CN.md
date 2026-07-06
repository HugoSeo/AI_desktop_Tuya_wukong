# Wukong Time Manage 模块

## 概述

`wukong_time_manage` 是 Wukong 本地时间管理能力的统一入口，承接闹钟、提醒、倒计时、秒表、番茄时钟五项能力。对外提供统一 service API（`wukong_tm.h`），对内按能力拆分独立源文件，通过 `wukong_cron` 完成定时调度。

## 架构

```text
AI 模型
  │
  ▼
MCP tool (mcp_tool_tm.c)
  │
  ▼
wukong_time_manage (wukong_tm_*.c)
  │
  ├─ alarm / reminder ──► wukong_cron（cron 表达式 + JSON-RPC）
  ├─ countdown ──────────► wukong_cron（动态 once cron）+ 本地事件
  ├─ stopwatch ──────────► 本地事件
  └─ pomodoro ──────────► wukong_cron（once cron 驱动阶段切换）+ 本地事件
```

职责边界：

| 层 | 职责 |
|---|---|
| `wukong_cron` | 通用定时调度、cron 表达式解析、JSON-RPC 执行 |
| `wukong_time_manage` | 时间管理业务对象、cron 映射、本地触发行为 |
| `mcp_tool_tm` | MCP tool 入参解析、转发到 `wukong_time_manage` |

## 文件结构

```text
wukong/tm/
├── wukong_tm.h              # 对外统一头文件
├── wukong_tm.c              # 生命周期入口 init/deinit
├── wukong_tm_internal.h     # 内部共享：TLV 打包 inline、枚举定义
├── wukong_tm_alarm.c        # 闹钟
├── wukong_tm_reminder.c     # 提醒
├── wukong_tm_countdown.c    # 倒计时
├── wukong_tm_stopwatch.c    # 秒表（正计时）
├── wukong_tm_pomodoro.c     # 番茄时钟
├── tests/                   # host 端单元测试（pytest + TAP）
│   ├── stubs/               # stub headers（测试替身）
│   ├── wukong_test.h        # 零依赖测试框架（EXPECT 宏 + TAP 输出）
│   ├── stubs_cjson.c        # 共享 cJSON stub 实现
│   ├── stubs_{module}.c     # 各模块专用 stubs
│   ├── test_{module}.c      # 测试用例（221 个断言）
│   ├── conftest.py          # pytest fixture + TAP 解析
│   └── test_suite.py        # 声明式测试注册表
└── README_CN.md
```

## 生命周期

初始化：

```text
wukong_cron_init()
  └─ wukong_time_manage_init()
       ├─ wukong_tm_alarm_init()
       ├─ wukong_tm_reminder_init()
       ├─ wukong_tm_countdown_init()
       └─ wukong_tm_pomodoro_init()   ← 注册 tm.pomodoro.phase_end RPC
```

反初始化：

```text
wukong_time_manage_deinit()
  ├─ wukong_tm_pomodoro_deinit()
  ├─ wukong_tm_countdown_deinit()
  ├─ wukong_tm_reminder_deinit()
  └─ wukong_tm_alarm_deinit()
wukong_cron_deinit()
```

## 功能说明

### 闹钟

源文件：`wukong_tm_alarm.c`  
开发者集成（API、`alarm.fire`/贪睡 RPC 链、事件 TLV `START`/`STOP`/`PAUSE`/`FINISH` + `alarm_id` + `ring_seq`）：见 [README_ALARM.md](./README_ALARM.md)。

核心能力：

- 闹钟 CRUD：`add` / `update` / `remove` / `get` / `find_by_time` / `list`
- 周期类型：一次性（默认）、每天、每周、每月
- cron 同步：增删改时自动同步 cron job
- 触发动作：`alarm.fire` → 发送 `START` 事件（应用层决定提示音/UI）
- 事件通知：通过 `WUKONG_AI_EVENT_CLOCK_MCP_ALARM` 发送 TLV（`opr` + `alarm_id` + `ring_seq`）
- **默认一次性**：MCP `device_alarm_set` 未指定 `repeat_type` 时默认创建一次性闹钟（`repeat_type=0`），仅当用户明确说"每天""工作日"等才设为周期闹钟
- **重复闹钟去重**：MCP `device_alarm_set` add 操作会先检查是否存在**相同时间**的闹钟，若有则返回 `already_exists` + `existing_id`，不会创建重复条目（最大支持 8 条）

贪睡机制：

1. 主闹钟命中后执行 `alarm.fire`，发出 `START` 事件，进入 ringing 状态
2. 同时创建响铃超时 once cron（`alarm.snooze.timeout`，默认 30s，可通过 `wukong_tm_alarm_ring_duration_set()` 调整）
3. 响铃时长内未响应 → 发出 `PAUSE` 事件，创建贪睡 once cron（`alarm.snooze.fire`，默认 300s / 5min，可通过 `wukong_tm_alarm_snooze_delay_set()` 调整）
4. 贪睡触发后重新进入响铃（`ring_seq` 递增，再次得到 `START`）
5. 贪睡次数达到上限（默认 3 次，可通过 `wukong_tm_alarm_snooze_max_count_set()` 调整，0 = 不限）后，发出 `FINISH` 事件自动结束

响铃 / 贪睡配置：

- `wukong_tm_alarm_ring_duration_set(seconds)` / `wukong_tm_alarm_ring_duration_get()` — 响铃时长（默认 30s）
- `wukong_tm_alarm_snooze_delay_set(seconds)` / `wukong_tm_alarm_snooze_delay_get()` — 贪睡间隔（默认 300s）
- `wukong_tm_alarm_snooze_max_count_set(count)` / `wukong_tm_alarm_snooze_max_count_get()` — 贪睡最大次数（默认 3，0 = 不限）
- 可在 `wukong_tm_alarm_init()` 前调用；新值在下一轮响铃/贪睡时生效

显式关闭：

- API：`wukong_tm_alarm_ack()` / `wukong_tm_alarm_ack_active()`
- MCP：`device_alarm_set` `operation=3`
- 一次性闹钟 ack 后自动删除，周期闹钟仅关闭当前轮次

### 提醒

源文件：`wukong_tm_reminder.c`

核心能力：

- 提醒 CRUD：`add` / `update` / `remove` / `get` / `find_by_time` / `query_text`
- cron 映射：每条提醒对应一个 once cron job，params 中保存 `reminder_id` 和 `message`
- 时间输入：通过 `start_timestamp`（UTC 秒级时间戳）指定触发时间

触发链路：

```text
cron 到时
  └─ reminder.fire (JSON-RPC)
       └─ wukong_tm_reminder_fire()
            ├─ wukong_tm_reminder_action_notify(message)
            │    ├─ tuya_ai_input_start(TRUE)
            │    ├─ wukong_ai_agent_send_text("[SYS_TTS_NOTIFY] ... triple-quoted <message>")
            │    └─ tuya_ai_input_stop()
            └─ 自动删除该一次性 reminder
```

### 倒计时

源文件：`wukong_tm_countdown.c`  
开发者集成（事件 TLV、`START`/`TICK`/`FINISH`/`STOP` 等）：见 [README_COUNTDOWN.md](./README_COUNTDOWN.md)。

核心能力：

- 单例模式：同一时刻仅允许一个活跃倒计时
- 操作：`create` / `pause` / `resume` / `delete`
- 动态调度：根据剩余时间自动选择 cron 粒度（>1h→1h，>10min→1min，>10s→10s，≤10s→1s）
- 末 10 秒保证秒级精度
- 重复 `create` 不会覆盖现有实例，MCP 层返回 `{"success":false,"reason":"already_exists","current":...}`
- 不支持原地修改；如需改时长请先 `delete` 后再 `create`

### 秒表

源文件：`wukong_tm_stopwatch.c`  
开发者集成（事件 TLV、`START`/`STOP`/`RESET`、**暂停/停止时附带 `elapsed_sec` TLV** 等，**无自动 TICK**）：见 [README_STOPWATCH.md](./README_STOPWATCH.md)。

核心能力：

- 操作：`start` / `pause` / `resume` / `stop` / `reset`；MCP 另支持 **`query`（operation=5）** 查询运行中累计秒数
- `pause` / `stop` / `reset` 对应事件与 MCP 成功返回值均携带**累计已跑秒数**（便于上报）
- 纯本地事件驱动，不依赖 cron
- 单例模式：同一时刻仅允许一个秒表实例；重复 `start` 返回 `already_exists + current`
- 重新开始：可先 `stop` 或 `reset` 再 `start`（详见 `README_STOPWATCH.md`）

### 番茄时钟

源文件：`wukong_tm_pomodoro.c`  
开发者集成（事件 TLV、`START`/`FINISH`/`phase`、与 `query` 配合等）：见 [README_POMODORO.md](./README_POMODORO.md)。

核心能力：

- 操作：`start` / `pause` / `resume` / `stop` / `query`
- 基于 cron 驱动阶段切换：工作→短休→工作→...→长休 循环
- 配置项：工作时长、短休时长、长休时长（分钟）、长休前需完成的工作段数 `work_sessions_before_long_break`（默认 4，与经典番茄钟一致；范围 1–12，由 MCP `device_pomodoro_timer_set` 在 `start` 时下发）
- `query` 返回当前阶段、剩余时间、已完成工作轮数等快照
- 上报应用的 AI 事件中，TLV 含 `opr` + `phase`（`FINISH` 时 `phase` 为刚结束的阶段）；时长与 `work_sessions_before_long_break` 等请用 `query`（详见上文开发者文档）
- 每完成 N 个「工作段」后进入长休息（N 默认为 4）
- 单例模式：同一时刻仅允许一个番茄时钟实例；重复 `start` 返回 `already_exists + current`
- 不支持运行中修改；如需改配置请先 `stop` 后再 `start`

阶段切换机制：

```text
start(cfg)
  └─ 进入 WORK 阶段 → 创建 once cron（work_duration * 60 秒后触发）
       └─ cron 到时 → tm.pomodoro.phase_end (JSON-RPC)
            ├─ completed_work_count++
            ├─ emit FINISH 事件
            ├─ 计算下一阶段（SHORT_BREAK / LONG_BREAK / WORK）
            └─ 进入下一阶段 → 创建新的 once cron → 循环
```

暂停/恢复：

- `pause` 快照剩余时间并移除当前 cron job
- `resume` 用快照的剩余时间重新创建 once cron job

## MCP 工具映射

| MCP tool | 方向 | 目标 API | 说明 |
|---|---|---|---|
| `device_alarm_set` | op=0/1/2/3 | `alarm_add` / `alarm_remove` / `alarm_update` / `alarm_ack` | add 时若相同时间闹钟已存在返回 `already_exists`+`existing_id` |
| `device_alarm_query` | 查询 | `alarm_list` | 查询**所有闹钟及重复提醒**（daily/weekly/monthly）；不含一次性 reminder |
| `device_schedule_set` | op=0/1/2 | `reminder_add` / `reminder_remove` / `reminder_update` | 仅管理一次性日程提醒 |
| `device_schedule_query` | 查询 | `reminder_query_text` | 仅查一次性提醒；重复提醒请用 `device_alarm_query` |
| `device_countdown_timer_set` | op=0/1/2/3/4 | `create` / `pause` / `resume` / `delete` / **`query`**；`pause`/`delete` 成功返回 `remaining_sec`+`elapsed_sec`，`query` 返回完整快照 |
| `device_stopwatch_timer_set` | op=0/1/2/3/4/5 | `stopwatch_start` / `pause` / `resume` / `stop` / `reset` / **`query`**；`pause`/`stop`/`reset` 成功返回 `elapsed_sec`，`query` 返回完整快照 |
| `device_pomodoro_timer_set` | op=0/1/2/3/4 | `pomodoro_start` / `pomodoro_pause` / `pomodoro_resume` / `pomodoro_stop` / `pomodoro_query` |

## 测试

单元测试位于 `wukong/tm/tests/` 目录，基于 pytest + 自研 TAP 框架（`wukong_test.h`）。

| 测试文件 | 断言数 | 覆盖范围 |
|---|:---:|---|
| `test_core.c` | 7 | init/deinit 生命周期、时间同步事件 |
| `test_alarm.c` | 39 | 闹钟 CRUD、cron 同步、贪睡、remove-by-time |
| `test_reminder.c` | 14 | 提醒 CRUD、fire、AI 发送、cron 集成 |
| `test_countdown.c` | 31 | 倒计时创建/暂停/恢复/删除、动态调度 |
| `test_stopwatch.c` | 20 | 秒表逻辑与 TLV（含 `elapsed_sec`） |
| `test_pomodoro.c` | 78 | 番茄时钟 cron 阶段切换、暂停恢复、长休触发 |
| `test_mcp.c` | 52 | MCP tool：alarm/schedule/countdown 全链路 |

运行方式：

```bash
# 全部测试
pytest tests/test_suite.py -v

# 筛选单个模块
pytest tests/test_suite.py -k alarm -v
```

新增测试：创建 `tests/stubs_X.c` + `tests/test_X.c`，在 `tests/test_suite.py` 的 `_TESTS` 列表追加一行即可。
