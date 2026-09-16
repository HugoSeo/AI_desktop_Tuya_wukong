# 闹钟（Alarm）开发者说明

本文说明 Wukong Demo 中**闹钟的配置、触发、贪睡与确认**相关 API，以及如何通过 **`WUKONG_AI_EVENT_CLOCK_MCP_ALARM`** 正确处理**开始响铃、用户确认、进入贪睡、超时结束**等事件，并在应用层实现自定义行为（提示音、全屏提醒、语音播报 `message` 等）。

相关源码：

- 实现：`wukong_tm_alarm.c`
- 公共 API 与配置类型：`wukong_tm.h`（`wukong_tm_alarm_*`、`WUKONG_TM_ALARM_CFG_T`、`WUKONG_TM_ALARM_REPEAT_TYPE_E`）
- TLV / 操作码定义：`wukong_tm_internal.h`（`WUKONG_TM_TAG_ALARM_*`、`WUKONG_TM_TIMER_OPR_E`）
- 事件投递：`wukong_ai_event_notify(WUKONG_AI_EVENT_CLOCK_MCP_ALARM, …)`（见 `wukong_ai_agent.h`）
- 统一初始化：`wukong_time_manage_init()` → `wukong_tm_alarm_init()`（`wukong_tm.c`）

---

## 1. 工作原理（简版）

- **多实例**：内存表最多 **`WUKONG_TM_ALARM_MAX_COUNT`（8）** 条；每条由调用方提供唯一 `alarm_id`（长度见 `WUKONG_TM_ALARM_ID_LEN`）。
- **Cron 映射**：`add` / `update` 会为每条闹钟创建或更新 **cron job**，到时发起 JSON-RPC **`alarm.fire`**（`once` 标志与 `repeat_type` 一致：一次性闹钟为 `once=1`）。
- **响铃一轮**：`wukong_tm_alarm_fire()` → 内部 `__alarm_start_ringing()`：递增 `ring_seq`、注册未应答超时任务 **`alarm.snooze.timeout`**（默认 **30 秒**，可通过 `wukong_tm_alarm_ring_duration_set()` 调整），成功后发出 **`START`** 事件。**模块本身不播放提示音**；应用层在收到 `START` 时自行调用 `wukong_audio_player_alert()` 或其它提示（见 §5）。
- **贪睡**：响铃时长内未 `ack` 且贪睡开关开启时，发出 **`PAUSE`** 事件，并调度贪睡延迟（默认 **300 秒 / 5 分钟**，可通过 `wukong_tm_alarm_snooze_delay_set()` 调整）后的 **`alarm.snooze.fire`**；到时再次进入响铃一轮（`ring_seq` 递增，再次得到 `START`）。贪睡最多重复 **`WUKONG_TM_ALARM_SNOOZE_MAX_COUNT`（默认 3）** 轮（可通过 `wukong_tm_alarm_snooze_max_count_set()` 调整，0 = 不限次数），超出后发出 **`FINISH`** 事件自动结束。可通过 **`wukong_tm_alarm_snooze_enable_set(FALSE)`** 完全关闭贪睡（并移除已挂起的贪睡 cron）。
- **未应答且贪睡未开启或已达上限**：发出 **`FINISH`** 事件，表示该轮响铃自然结束。

依赖：**系统时间、`wukong_cron`** 可用。

---

## 2. 控制面 API（C）

在 `wukong_tm_alarm_init()` 已成功执行的前提下使用（通常随 `wukong_time_manage_init()` 完成）。

| 函数 | 作用 |
|------|------|
| `wukong_tm_alarm_add(cfg, alarm_id)` | 新增闹钟并同步 cron |
| `wukong_tm_alarm_update(alarm_id, cfg)` | 更新配置并重建 cron（会清理该条运行时响铃/贪睡状态后重绑） |
| `wukong_tm_alarm_get(alarm_id, cfg)` | 按 id 读取配置快照（含 `message`、`cron_job_id` 等） |
| `wukong_tm_alarm_remove(alarm_id)` | 删除闹钟并移除 cron |
| `wukong_tm_alarm_remove_by_time(cfg, &removed_count)` | 按「时间语义」批量删除（含与「每天 vs 每周全周」等价的匹配，见 `__alarm_semantic_match`） |
| `wukong_tm_alarm_list(&json)` | 导出当前闹钟列表 JSON（**不含** `is_ringing` 等运行时字段，见 §4） |
| `wukong_tm_alarm_find_by_time(cfg, id_buf, len)` | 按时间描述查找**唯一** id；重复则 `OPRT_COM_ERROR` |
| `wukong_tm_alarm_fire(alarm_id)` | **立即**按 id 触发一轮响铃（与 cron 到时路径相同入口） |
| `wukong_tm_alarm_ack(alarm_id)` | 确认该 id：结束当前响铃/待贪睡，一次性闹钟会 **remove** |
| `wukong_tm_alarm_ack_active()` | 确认**当前**正在响铃或处于待贪睡窗口的那一条（同时仅处理一条；多路并发返回 `OPRT_COM_ERROR`） |
| `wukong_tm_alarm_snooze_enable_set(enable)` | 开/关贪睡；关时会去掉已挂起的贪睡 job |
| `wukong_tm_alarm_ring_duration_set(seconds)` | 设置响铃时长（未应答超时窗口），单位秒，默认 30；可在 init 前调用 |
| `wukong_tm_alarm_ring_duration_get()` | 获取当前响铃时长（秒） |
| `wukong_tm_alarm_snooze_delay_set(seconds)` | 设置贪睡间隔，单位秒，默认 300；可在 init 前调用 |
| `wukong_tm_alarm_snooze_delay_get()` | 获取当前贪睡间隔（秒） |
| `wukong_tm_alarm_snooze_max_count_set(count)` | 设置贪睡最大次数，默认 3；0 = 不限次数；可在 init 前调用 |
| `wukong_tm_alarm_snooze_max_count_get()` | 获取当前贪睡最大次数 |

### 2.1 配置 `WUKONG_TM_ALARM_CFG_T`

| 字段 | 说明 |
|------|------|
| `enabled` | 是否启用（同步进 cron job 的 `enabled` 字段） |
| `repeat_type` | `ONCE`（默认） / `DAILY` / `WEEKLY` / `MONTHLY`（枚举见 `wukong_tm.h`）；MCP 未指定时默认 `ONCE` |
| `hour` / `minute` | 本地触发时分，`hour`∈[0,23]，`minute`∈[0,59] |
| `weekday_mask` | 周重复时的星期位掩码；周类型下不可为 0 |
| `month_day` | 月重复的日，1–31 |
| `start_time` | 一次性闹钟可用绝对 UTC POSIX 秒（实现中参与 cron 构建，具体以 `__alarm_build_cron_expr` 为准） |
| `message` | 语义文案；会写入 cron 请求 JSON 的 `params.message`（便于云端/调试）；**当前** `alarm.fire` 的 RPC 处理仅解析 `alarm_id`，业务若要用文案需 **`get` 读取 `cfg.message`** |
| `cron_job_id` | 绑定 cron id，由模块维护，调用方一般只读 |

### 2.2 典型返回值

- `OPRT_OK`：成功。
- `OPRT_INVALID_PARM`：参数非法、配置校验失败等。
- `OPRT_NOT_FOUND`：id 不存在、`remove_by_time` 无匹配、`ack_active` 无活跃会话等。
- `OPRT_COM_ERROR`：未初始化、槽满、id 重复、`find_by_time` 多匹配、`ack_active` 多路活跃等。

---

## 3. 事件面：如何感知「开始响铃 / 用户确认 / 进入贪睡 / 超时结束」

所有闹钟通知都通过 **`WUKONG_AI_EVENT_CLOCK_MCP_ALARM`** 发给在 `wukong_ai_agent_init(cb)` 里注册的 **`WUKONG_EVENT_OUTPUT`** 回调。

`event->data` 指向一块 **TLV 二进制缓冲区**（小端：**Type 2 字节 + Length 2 字节 + Value**）。**注意：`wukong_ai_event_notify()` 会同步调用你的回调，返回后缓冲区会被模块释放**；若要在其他线程或延迟处理，必须在回调内**拷贝**整块 buffer。

### 3.1 操作码 `WUKONG_TM_TIMER_OPR_E`（闹钟实际会出现的值）

| 值 | 符号 | 含义 | 典型处理建议 |
|----|------|------|----------------|
| 0 | `WUKONG_TM_TIMER_OPR_START` | **开始响铃**（含 Cron 首次触发与贪睡后再次响铃） | 播放提示音（如 `wukong_audio_player_alert`）、展示全屏提醒 UI |
| 1 | `WUKONG_TM_TIMER_OPR_PAUSE` | **进入贪睡**（响铃时长内未应答，已排程贪睡回放） | 切换 UI 为「贪睡等待」；贪睡延迟后将再次收到 `START` |
| 3 | `WUKONG_TM_TIMER_OPR_STOP` | **用户确认/关闭**（`ack` / `ack_active`） | 清除提醒 UI；一次性闹钟此后自动 remove |
| 6 | `WUKONG_TM_TIMER_OPR_FINISH` | **未应答窗口结束且贪睡未开启，或贪睡次数已达上限** | 停止提示音、清除 UI |

### 3.2 如何区分初次响铃与贪睡后的再次响铃

TLV 中包含 **`ring_seq`**（`WUKONG_TM_TAG_ALARM_RING_SEQ`）：

- **`ring_seq == 1`**：Cron 首次触发的初始响铃。
- **`ring_seq > 1`**：贪睡后的第 N 轮响铃。

### 3.3 TLV 载荷（Tag 与含义）

| Tag | 长度 | 含义 |
|-----|------|------|
| `WUKONG_TM_TAG_ALARM_OPR` | 1 | `WUKONG_TM_TIMER_OPR_E` |
| `WUKONG_TM_TAG_ALARM_ID` | `strlen(alarm_id) + 1` | 以 `\0` 结尾的闹钟 ID 字符串 |
| `WUKONG_TM_TAG_ALARM_RING_SEQ` | `sizeof(UINT_T)` | 当前响铃轮次（小端）；1 = 初次，>1 = 贪睡后 |

### 3.4 推荐的应用层写法

1. **在事件回调中**：解析 TLV，读取 `opr`、`alarm_id`、`ring_seq`，按上表分支处理提示音 / UI / 播报。
2. **播放提示音**：在 `opr == START` 时调用 `wukong_audio_player_alert(AI_TOY_ALERT_TYPE_NOT_ACTIVE, FALSE)` 或自定义音效。
3. **语音播报 `message`**：在 `START` 回调中根据 `alarm_id` 调用 `wukong_tm_alarm_get()` 读取 `cfg.message`，再走 TTS 管线。
4. **确认按钮**：调用 `wukong_tm_alarm_ack(alarm_id)` / `wukong_tm_alarm_ack_active()`（与 MCP `device_alarm_set` operation=3 一致）。

---

## 4. Cron / JSON-RPC 方法（与实现一致）

`wukong_tm_alarm_init()` 注册：

| 方法 | 作用 |
|------|------|
| `alarm.fire` | 主触发：params 含 `alarm_id`（及 cron JSON 中的 `message` 字符串）；handler 调用 `wukong_tm_alarm_fire(alarm_id)` |
| `alarm.snooze.timeout` | 响铃时长（默认 30s）内未应答：校验 `ring_seq` 后进入贪睡调度或结束本轮 |
| `alarm.snooze.fire` | 贪睡后再响：校验 `ring_seq` 后再次 `__alarm_start_ringing` |

运行时 job 的 params 含 **`alarm_id`** 与 **`ring_seq`**，用于丢弃过期回调。

---

## 5. 列表 JSON 与运行时状态

`wukong_tm_alarm_list()` 根对象包含 **`device_time`**（当前设备本地时间，格式 `"YYYY-MM-DD HH:MM:SS"`，系统时间可用时输出）和 **`alarms`** 数组。

每条闹钟包含：

| 字段 | 说明 |
|------|------|
| `id` | 闹钟唯一标识，操作时作为 `device_alarm_set` 的 `id` 参数 |
| `enabled` | 是否启用（1/0） |
| `repeat_type` | 重复类型（0=once, 1=daily, 2=weekly, 3=monthly） |
| `time` | 合并后的本地时间字符串：一次性闹钟为 `"YYYY-MM-DD HH:MM"`，其他类型为 `"HH:MM"` |
| `expired` | 最近一次计划触发时刻是否已过（1=已过，0=未过）；系统时间不可用时恒为 0 |
| `weekday_mask` | 仅周重复且非零时输出；位掩码，bit0=周日…bit6=周六 |
| `month_day` | 仅月重复且非零时输出；月内日期（1–31） |
| `message` | 闹钟备注文案 |

**不包含** `is_ringing`、`ring_seq`、`cron_job_id` 等内部运行时字段。运行时状态通过 **事件回调**（§3）感知。

---

## 6. MCP 与测试

MCP 工具：`device_alarm_set` / `device_alarm_query`

| 操作 | 工具 | 说明 |
|------|------|------|
| 新增 | `device_alarm_set` op=0 | 未指定 `repeat_type` 时默认**一次性**（`repeat_type=0`）。若已存在**相同时间**的闹钟，返回 `{"success":false,"reason":"already_exists","existing_id":"<id>"}` 而非重复创建 |
| 删除 | `device_alarm_set` op=1 | 按 id 删除 |
| 更新 | `device_alarm_set` op=2 | 按 id 更新，仅合并提供的字段 |
| 确认响铃 | `device_alarm_set` op=3 | ack 当前闹钟 |
| 查询 | `device_alarm_query` | 返回所有闹钟（含每天/每周/每月等**重复提醒**），不含 once reminder |

> **注意**：`device_alarm_query` 查询的是闹钟（含重复提醒）；一次性日程提醒请用 `device_schedule_query`。

Host 测试：`test_wukong_tm_alarm.sh`。

---

## 7. 常量速查（`wukong_tm_alarm.c`）

| 常量 | 默认值 | 含义 | 运行时可配 |
|------|--------|------|------------|
| `WUKONG_TM_ALARM_MAX_COUNT` | 8 | 最大闹钟条数 | 否 |
| `WUKONG_TM_ALARM_ACK_TIMEOUT_SEC` | 30 | 响铃时长 / 未应答超时（秒） | 是，`wukong_tm_alarm_ring_duration_set()` |
| `WUKONG_TM_ALARM_SNOOZE_DELAY_SEC` | 300 | 贪睡间隔（秒） | 是，`wukong_tm_alarm_snooze_delay_set()` |
| `WUKONG_TM_ALARM_SNOOZE_MAX_COUNT` | 3 | 贪睡最大次数（0 = 不限） | 是，`wukong_tm_alarm_snooze_max_count_set()` |
