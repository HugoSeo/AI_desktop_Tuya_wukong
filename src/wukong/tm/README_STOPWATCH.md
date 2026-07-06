# 正计时 / 秒表（Stopwatch）开发者说明

本文说明如何在 Wukong Demo 中**启动与控制正计时（秒表）**，以及如何通过 **`WUKONG_AI_EVENT_CLOCK_MCP_STOPWATCH_TIMER`** 正确处理**开始、暂停、恢复、停止、复位**等事件，并在应用层实现自定义行为（UI、提示音、上报等）。

> 与**倒计时**不同：秒表为**单调递增**，**没有**内置「到时自动结束」；模块也**不会**下发 `TICK` / `FINISH` 类进度事件。需要界面上的连续读数时，请配合 **`wukong_tm_stopwatch_query()`**、MCP **`device_stopwatch_timer_set` `operation=5`（query）** 或自有定时器轮询。

相关源码：

- 实现：`wukong_tm_stopwatch.c`
- 公共 API：`wukong_tm.h`（`wukong_tm_stopwatch_*`、`WUKONG_TM_STOPWATCH_STATE_T`）
- TLV / 操作码定义：`wukong_tm_internal.h`（`WUKONG_TM_TAG_STOPWATCH_OPR`、`WUKONG_TM_TAG_STOPWATCH_ELAPSED_SEC`、`WUKONG_TM_TIMER_OPR_E`）
- 事件投递：`wukong_ai_event_notify(WUKONG_AI_EVENT_CLOCK_MCP_STOPWATCH_TIMER, …)`（见 `wukong_ai_agent.h`）
- 统一初始化：`wukong_time_manage_init()` 会调用 `wukong_tm_stopwatch_init()`（`wukong_tm.c`）

---

## 1. 工作原理（简版）

- **单例**：同一时刻最多一个活跃秒表；再次 `start` 返回 `OPRT_COM_ERROR`（MCP 层会包装为 `already_exists` + 当前快照）。
- **纯本地**：不注册 Cron，完全依赖 POSIX 时间与 API 调用；读数为从本次 `start` 起**运行态**累计的秒数，**暂停时读数冻结**（见 §2.1）。
- **事件载荷**：
  - `START` / `RESUME`：仅含操作码 TLV。
  - **`PAUSE` / `STOP` / `RESET`**：除操作码外，另带一条 **`WUKONG_TM_TAG_STOPWATCH_ELAPSED_SEC`**（累计已跑秒数，小端 `UINT_T`），便于语音/云端在暂停或结束瞬间直接上报，无需再 `query`。

---

## 2. 控制面 API（C）

在 `wukong_tm_stopwatch_init()` 已成功执行的前提下使用（通常随 `wukong_time_manage_init()` 完成）。

| 函数 | 作用 |
|------|------|
| `wukong_tm_stopwatch_start()` | 开始新会话：从 **0** 起算；若已有活跃秒表则失败 |
| `wukong_tm_stopwatch_pause()` | 暂停：冻结累计已跑秒数 |
| `wukong_tm_stopwatch_resume()` | 恢复：从暂停点继续累加 |
| `wukong_tm_stopwatch_stop()` | **停止**会话：发 `STOP` 事件后清除状态（见 §3.3 取最终读数） |
| `wukong_tm_stopwatch_reset()` | **复位**会话：发 `RESET` 事件后清除状态（语义上强调清零，与 `stop` 在「是否展示最终成绩」上可由产品区分） |
| `wukong_tm_stopwatch_query(state)` | 查询快照：`active`、`paused`、`elapsed_sec` |

### 2.1 `WUKONG_TM_STOPWATCH_STATE_T` 与 `elapsed_sec`

- **`elapsed_sec`**（实现见 `__stopwatch_elapsed_sec`）：从本次 `start` 起累计的**跑表读数**。**仅在运行态**随时间增加；**暂停时**保持为暂停瞬间的累计值（墙上暂停时长**不会**加进读数）。运行中 = 已累计秒数 + 当前这一段从 `start_ts` 到「现在」的差。
- 头文件对 `elapsed_sec` 的英文注释若理解为「会话里可能发生暂停」亦可；数值语义以本节与源码为准。

### 2.2 典型返回值

- `OPRT_OK`：成功。
- `OPRT_INVALID_PARM`：`query` 时 `state` 为空。
- `OPRT_NOT_FOUND`：`stop` / `reset` / `query` 时**没有**活跃秒表。
- `OPRT_NOT_SUPPORTED`：`pause` 时未运行或已暂停；`resume` 时未运行或未暂停。
- `OPRT_COM_ERROR`：`start` 时已有活跃实例。
- `OPRT_MALLOC_FAILED`：事件缓冲区分配失败（极少）。

---

## 3. 事件面：如何感知「开始 / 暂停 / 恢复 / 停止 / 复位」

所有秒表通知都通过 **`WUKONG_AI_EVENT_CLOCK_MCP_STOPWATCH_TIMER`** 发给在 `wukong_ai_agent_init(cb)` 里注册的 **`WUKONG_EVENT_OUTPUT`** 回调。

`event->data` 指向 **TLV 二进制缓冲区**（小端：Type 2 字节 + Length 2 字节 + Value）。**`wukong_ai_event_notify()` 会同步调用回调，返回后缓冲区会被释放**；异步使用请**拷贝** buffer。

### 3.1 操作码 `WUKONG_TM_TIMER_OPR_E`（秒表实际会出现的值）

| 值 | 符号 | 含义 | 典型处理建议 |
|----|------|------|----------------|
| 0 | `WUKONG_TM_TIMER_OPR_START` | **`start` 成功**，新会话开始 | 秒表 UI 归零并开始跑表动画；可立即 `query` 得 `elapsed_sec == 0` |
| 1 | `WUKONG_TM_TIMER_OPR_PAUSE` | **暂停** | 冻结显示；事件内带 **累计秒数** TLV，或再 `query` |
| 2 | `WUKONG_TM_TIMER_OPR_RESUME` | **恢复** | 继续跑表；可 `query` 校验状态 |
| 3 | `WUKONG_TM_TIMER_OPR_STOP` | **用户 `stop`**，会话结束 | 收起秒表、提示「已停止」；事件内带 **最终累计秒数**；见 §3.3 |
| 4 | `WUKONG_TM_TIMER_OPR_RESET` | **用户 `reset`**，会话结束 | 强调「已清零」；事件内带复位前 **累计秒数**；见 §3.3 |

秒表实现**不使用** `WUKONG_TM_TIMER_OPR_TICK`（5）与 `WUKONG_TM_TIMER_OPR_FINISH`（6）。

### 3.2 TLV 载荷

**`START`（0）与 `RESUME`（2）**：仅 **一个** TLV：

| Tag | 长度 | 含义 |
|-----|------|------|
| `WUKONG_TM_TAG_STOPWATCH_OPR` | 1 | `WUKONG_TM_TIMER_OPR_E` |

**`PAUSE`（1）、`STOP`（3）、`RESET`（4）**：**两个** TLV（顺序固定）：

| Tag | 长度 | 含义 |
|-----|------|------|
| `WUKONG_TM_TAG_STOPWATCH_OPR` | 1 | `WUKONG_TM_TIMER_OPR_E` |
| `WUKONG_TM_TAG_STOPWATCH_ELAPSED_SEC` | `sizeof(UINT_T)` | 小端无符号整数，**累计已跑秒数**（暂停/停止/复位瞬间的快照） |

其它依赖当前读数的逻辑（语音播报「已计 X 秒」、大屏数字）仍可使用 **`wukong_tm_stopwatch_query()`** 或解析第二条 TLV。

### 3.3 在 `STOP` / `RESET` 回调里取「最终成绩」

`wukong_tm_stopwatch_stop()` / `reset()` 的实现顺序是：**先** `wukong_ai_event_notify()`（同步进入你的回调，且 **TLV 已含 `elapsed_sec`**），**再**将 `active` 置为 `FALSE`。

因此：在收到 **`STOP` 或 `RESET`** 的回调里，**仍可调用** `wukong_tm_stopwatch_query()`，此时会话尚未销毁，可读到**最终** `elapsed_sec`；回调返回后实例即清空，再次 `query` 会得到 `OPRT_NOT_FOUND`。

### 3.4 平滑 UI 刷新

因无 `TICK` 事件，常见做法是：

1. 在收到 `START` / `RESUME` 后启动 UI 定时器（如每 100ms～1s）调用 `query` 更新显示；`PAUSE` 后停止递增或仅刷新一次；`STOP`/`RESET` 后停止定时器。
2. 或仅在用户可见时轮询，以降低功耗。

---

## 4. 与「结束」相关的语义说明

秒表**没有**倒计时式的「自然到期」：

- 若产品上的「结束」指**用户按下停止**：对应 **`STOP`**（`wukong_tm_stopwatch_stop`）。
- 若指**清零并结束当前会话**：对应 **`RESET`**（`wukong_tm_stopwatch_reset`）。
- 若需要「到某一刻自动停表」，需在应用层根据 `query` 的 `elapsed_sec` 自行判断并调用 `stop()`（或只停 UI，视需求而定）。

---

## 5. 与其它模块的衔接

- **倒计时**：事件码为 `WUKONG_AI_EVENT_CLOCK_MCP_COUNTDOWN_TIMER`，TLV 含剩余时间等，勿与秒表解析混用。
- **显示层**：可将事件映射到 `TY_DISPLAY_TP_CLOCK_MCP_STOPWATCH_TIMER`（`tuya_ai_display.h`），与 `query` 读数配合刷新界面。

---

## 6. MCP 与自测

- MCP：`device_stopwatch_timer_set`，`operation` **0–5**：`start` / `pause` / `resume` / `stop` / `reset` / **`query`**（见 `mcp_tool_tm.c`、`README_CN.md`）。
- **`pause` / `stop` / `reset` 成功**时返回 JSON：`{"success":true,"elapsed_sec":N}`（秒）。
- **`query`（5）** 成功时返回：`{"success":true,"active":...,"paused":...,"elapsed_sec":...}`；无活跃秒表为 `{"success":true,"active":false}`。
- Host 测试：`test_wukong_tm_stopwatch.sh`（秒表逻辑与 TLV）；`test_wukong_mcp_tm_tools.sh`（MCP 封装与 query）。
