# 番茄钟（Pomodoro）开发者说明

本文说明如何在 Wukong Demo 中**驱动番茄钟**、**接收开始/结束等事件**，并在应用层实现自定义行为（UI、提示音、上报等）。

相关源码：

- 实现：`wukong_tm_pomodoro.c`
- 公共 API：`wukong_tm.h`（`wukong_tm_pomodoro_*`）
- TLV / 操作码定义：`wukong_tm_internal.h`（`WUKONG_TM_TAG_*`、`WUKONG_TM_TIMER_OPR_E`）
- 事件投递：`wukong_ai_event_notify(WUKONG_AI_EVENT_CLOCK_MCP_POMODORO_TIMER, …)`（见 `wukong_ai_agent.h`）
- 统一初始化：`wukong_time_manage_init()` 会调用 `wukong_tm_pomodoro_init()`（`wukong_tm.c`）

---

## 1. 工作原理（简版）

- 番茄钟是**单例会话**：同一时刻最多一个活跃会话。
- 每个阶段（工作 / 短休 / 长休）结束时，由 **一次性 Cron 任务** 触发 JSON-RPC 方法 `tm.pomodoro.phase_end`，内部再切换到下一阶段并注册下一次 Cron。
- **暂停**会取消待执行的 Cron；**恢复**按剩余秒数重新注册 Cron。

因此：**系统时间、Cron 子系统必须可用**，否则阶段无法自动切换。

---

## 2. 控制面 API（C）

在 `wukong_tm_pomodoro_init()` 已成功执行的前提下使用（通常随 `wukong_time_manage_init()` 完成）。

| 函数 | 作用 |
|------|------|
| `wukong_tm_pomodoro_start(cfg)` | 开始新会话：从**工作**阶段起算；若已有活跃会话返回 `OPRT_COM_ERROR` |
| `wukong_tm_pomodoro_pause()` | 暂停：移除 Cron，刷新剩余时间 |
| `wukong_tm_pomodoro_resume()` | 恢复：按 `remaining_sec` 重新调度 Cron |
| `wukong_tm_pomodoro_stop()` | 停止：清空会话与 Cron |
| `wukong_tm_pomodoro_query(state)` | 查询当前快照（阶段、剩余秒数、`session_id` 等） |

### 2.1 配置 `WUKONG_TM_POMODORO_CFG_T`

- `work_duration` / `short_break_duration` / `long_break_duration`：单位均为**分钟**，且必须 **> 0**。
- `work_sessions_before_long_break`：完成多少个**工作阶段**后进入长休；合法范围 **[1, 12]**（宏 `WUKONG_TM_POMODORO_WORK_BEFORE_LONG_MIN/MAX`）。

### 2.2 典型返回值

- `OPRT_OK`：成功。
- `OPRT_INVALID_PARM`：`start` 参数非法。
- `OPRT_NOT_FOUND`：`pause` / `resume` / `stop` / `query` 时无匹配会话或状态不对。
- `OPRT_COM_ERROR`：未初始化、`start` 时已有活跃会话，或 `resume` 时剩余时间为 0。

---

## 3. 事件面：如何感知「开始 / 结束 / 暂停 …」

所有番茄钟相关通知都通过 **`WUKONG_AI_EVENT_CLOCK_MCP_POMODORO_TIMER`** 发给在 `wukong_ai_agent_init(cb)` 里注册的 `WUKONG_EVENT_OUTPUT` 回调。

`event->data` 指向一块 **TLV 二进制缓冲区**（见下节）。**注意：`wukong_ai_event_notify()` 会同步调用你的回调，返回后缓冲区会被模块释放**；若要在其他线程使用，必须在回调内**拷贝**数据。

### 3.1 操作码 `WUKONG_TM_TIMER_OPR_E`（`wukong_tm_internal.h`）

番茄钟实际会出现的值：

| 值 | 符号 | 含义 |
|----|------|------|
| 0 | `WUKONG_TM_TIMER_OPR_START` | 某阶段**开始**（含用户 `start`、阶段自然结束后的下一阶段） |
| 1 | `WUKONG_TM_TIMER_OPR_PAUSE` | 暂停 |
| 2 | `WUKONG_TM_TIMER_OPR_RESUME` | 恢复 |
| 3 | `WUKONG_TM_TIMER_OPR_STOP` | 用户停止 |
| 6 | `WUKONG_TM_TIMER_OPR_FINISH` | **当前阶段正常结束**（紧接着通常会再收到一次 `START` 表示下一阶段） |

自动阶段切换时的顺序是：**先 `FINISH`（刚结束的阶段），再 `START`（新阶段）**。

### 3.2 TLV 载荷格式

与倒计时等模块一致：**Type 2 字节（小端） + Length 2 字节（小端） + Value**（见 `WUKONG_TM_TLV_*`）。

番茄钟仅携带 **操作码 + 阶段**，会话级配置（各阶段分钟数、`work_sessions_before_long_break`）与 `wukong_tm_pomodoro_query()` 中的 `cfg` 重复，已从事件中去掉；需要配置或剩余时间请 **`wukong_tm_pomodoro_query()`**。

| Tag | 长度 | 含义 |
|-----|------|------|
| `WUKONG_TM_TAG_POMODORO_OPR` | 1 | `WUKONG_TM_TIMER_OPR_E` |
| `WUKONG_TM_TAG_POMODORO_PHASE` | 1 | `WUKONG_TM_POMODORO_PHASE_E`（`wukong_tm.h`） |

**`FINISH` 与 `phase`：** 收到 `WUKONG_TM_TIMER_OPR_FINISH` 时，`POMODORO_PHASE` 表示**刚刚正常结束的那一段**（阶段切换前快照）；随后若进入新阶段，会再收到一次 `START`，此时 `phase` 为**新阶段**。

### 3.3 仍需用 `query` 补充的字段

TLV **仍不包含** `session_id`、`remaining_sec`、是否暂停、完整 `cfg` 等。推荐做法：

1. 在收到 `WUKONG_AI_EVENT_CLOCK_MCP_POMODORO_TIMER` 时，根据 `opr` + `phase` 做提示或动画；
2. 需要刷新 UI（圆环、倒计时数字）或读取配置时调用 **`wukong_tm_pomodoro_query()`**。

---

## 4. 阶段与长休规则（与实现一致）

- 阶段枚举：`WUKONG_TM_POMODORO_PHASE_WORK` / `_SHORT_BREAK` / `_LONG_BREAK`（`wukong_tm.h`）。
- 工作阶段结束：`completed_work_count` 自增。
- 若「已完成工作次数」为 `work_sessions_before_long_break` 的整数倍 → 下一阶段为**长休**；否则 → **短休**。
- 休息阶段结束 → 下一阶段总是**工作**；从休息回到工作时 `current_cycle` 自增。

---

## 5. 实现自定义行为的建议流程

1. **注册 AI 事件回调**（若尚未注册）：`wukong_ai_agent_init(your_handler)`。
2. 在 `your_handler` 中分支处理 `WUKONG_AI_EVENT_CLOCK_MCP_POMODORO_TIMER`：解析 TLV 得到 `opr` 与 `phase`，播放音效、震动、刷新图标等。
3. 对依赖**剩余时间 / session_id / 完整 cfg** 的逻辑，在回调内（或主线程排队后）调用 **`wukong_tm_pomodoro_query()`** 更新 UI。
4. 若需要**周期性 UI 刷新**（例如每秒更新数字），应用可用自己的定时器轮询 `query`；番茄钟模块仅在阶段切换与控制操作时发事件，**不发送每秒 TICK**。

---

## 6. MCP / 语音入口（可选）

云端或语音可通过 MCP 工具 **`device_pomodoro_timer_set`** 间接调用同一套 `wukong_tm_pomodoro_*` API（见 `mcp_tool_tm.c`）。设备侧业务逻辑仍应以上述 **AI 事件 + `query`** 为准，与触发来源解耦。

---

## 7. 调试提示

- Shell 自测脚本可参考同目录下 `test_wukong_tm_pomodoro.sh`（若环境已接入相同事件枚举）。
- 若阶段从不切换：检查 Cron 是否运行、`tm.pomodoro.phase_end` 是否已注册、系统时间是否跳变过大。

---

## 8. 头文件依赖小结

| 用途 | 头文件 |
|------|--------|
| `start` / `query` 等 API 与 `WUKONG_TM_POMODORO_*` 类型 | `wukong_tm.h` |
| 解析 TLV、`WUKONG_TM_TIMER_OPR_E`、Tag 枚举 | `wukong_tm_internal.h` |
| 事件类型 `WUKONG_AI_EVENT_CLOCK_MCP_POMODORO_TIMER`、回调类型 | `wukong_ai_agent.h` |
