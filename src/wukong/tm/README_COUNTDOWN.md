# 倒计时（Countdown）开发者说明

本文说明如何在 Wukong Demo 中**创建与控制倒计时**，以及如何通过 **`WUKONG_AI_EVENT_CLOCK_MCP_COUNTDOWN_TIMER`** 正确处理**开始、进度、自然结束、用户取消、暂停/恢复**等事件，并在应用层实现自定义行为（UI、提示音、语音播报、上报等）。

相关源码：

- 实现：`wukong_tm_countdown.c`
- 公共 API：`wukong_tm.h`（`wukong_tm_countdown_*`、`WUKONG_TM_COUNTDOWN_*`）
- TLV / 操作码定义：`wukong_tm_internal.h`（`WUKONG_TM_TAG_COUNTDOWN_*`、`WUKONG_TM_TIMER_OPR_E`）
- 事件投递：`wukong_ai_event_notify(WUKONG_AI_EVENT_CLOCK_MCP_COUNTDOWN_TIMER, …)`（见 `wukong_ai_agent.h`）
- 统一初始化：`wukong_time_manage_init()` 会调用 `wukong_tm_countdown_init()`（`wukong_tm.c`）

---

## 1. 工作原理（简版）

- 倒计时是**全局单例**：同一时刻最多一个活跃实例；再次 `create` 会返回 `OPRT_COM_ERROR`，需先 `delete` 或等其自然结束。
- **运行中**由 **一次性 Cron** 按剩余时长动态选择步进，到期后回调 JSON-RPC 方法 **`tm.countdown.tick`**；处理器内根据剩余时间发出 **`TICK`** 或 **`FINISH`**，并重新注册下一次 Cron（若仍未结束）。
- **暂停**会移除已绑定的 Cron 并保存剩余秒数；**恢复**按剩余秒数重算 `target_ts` 并重新调度。

因此：**系统时间、Cron 子系统必须可用**，否则进度 tick 与自然结束无法可靠触发。

### 1.1 进度步进（与实现一致）

在每次调度时，根据**当前剩余秒数**选择下一次 Cron 间隔（见 `__countdown_step_from_remaining`）：

| 剩余时间 | 下次 tick 间隔 |
|----------|----------------|
| > 3600 s | 3600 s（1 小时） |
| > 600 s  | 60 s（1 分钟） |
| > 10 s   | 10 s |
| ≤ 10 s   | 1 s（末段秒级） |

---

## 2. 控制面 API（C）

在 `wukong_tm_countdown_init()` 已成功执行的前提下使用（通常随 `wukong_time_manage_init()` 完成）。

| 函数 | 作用 |
|------|------|
| `wukong_tm_countdown_create(hours, minutes, seconds)` | 创建唯一倒计时并开始运行；总时长须 **> 0**，且当前无其他活跃倒计时 |
| `wukong_tm_countdown_query(snapshot)` | 查询快照：`active`、`state`、`remaining_sec`、`duration_sec`、`elapsed_sec` |
| `wukong_tm_countdown_pause()` | 暂停：移除 Cron，发出 `PAUSE` 事件 |
| `wukong_tm_countdown_resume()` | 恢复：若剩余为 0 则直接走**结束**逻辑并发 `FINISH`；否则重新调度并发 `RESUME` |
| `wukong_tm_countdown_delete()` | 用户取消：移除 Cron，发出 `STOP` 事件并清空实例 |

### 2.1 状态 `WUKONG_TM_COUNTDOWN_STATE_E`

- `IDLE`：无活跃倒计时。
- `RUNNING`：运行中（可能有待触发的 Cron tick）。
- `PAUSED`：已暂停，无 Cron，剩余时间保存在内部。

### 2.2 典型返回值

- `OPRT_OK`：成功。
- `OPRT_INVALID_PARM`：`create` 时长非法或 `query` 指针为空。
- `OPRT_NOT_FOUND`：无活跃实例时调用 `query` / `pause` / `resume` / `delete`。
- `OPRT_COM_ERROR`：子模块未初始化、`create` 时已有实例；`pause` 时非 `RUNNING`；`resume` 时非 `PAUSED`。
- `OPRT_MALLOC_FAILED`：事件缓冲区分配失败（极少）。

### 2.3 与 MCP 的对应关系（可选）

云端 / MCP 侧 `device_countdown_timer_set` 的 `operation` 会映射到上述 API（详见 `mcp_tool_tm.c` 与 `README_CN.md`）。

---

## 3. 事件面：如何感知「开始 / 进度 / 结束 / 取消 / 暂停 / 恢复」

所有倒计时通知都通过 **`WUKONG_AI_EVENT_CLOCK_MCP_COUNTDOWN_TIMER`** 发给在 `wukong_ai_agent_init(cb)` 里注册的 **`WUKONG_EVENT_OUTPUT`** 回调。

`event->data` 指向一块 **TLV 二进制缓冲区**（小端：**Type 2 字节 + Length 2 字节 + Value**）。**注意：`wukong_ai_event_notify()` 会同步调用你的回调，返回后缓冲区会被模块释放**；若要在其他线程或延迟处理，必须在回调内**拷贝**整块 buffer。

### 3.1 操作码 `WUKONG_TM_TIMER_OPR_E`（倒计时实际会出现的值）

| 值 | 符号 | 含义 | 典型处理建议 |
|----|------|------|----------------|
| 0 | `WUKONG_TM_TIMER_OPR_START` | **`create` 成功**，倒计时开始 | 展示总时长、开始 UI/动画；`remaining` 为初始总秒数 |
| 1 | `WUKONG_TM_TIMER_OPR_PAUSE` | **暂停** | 冻结 UI；`remaining` 为暂停瞬间剩余秒数 |
| 2 | `WUKONG_TM_TIMER_OPR_RESUME` | **恢复** | 继续 UI；`remaining` 为恢复时剩余秒数 |
| 3 | `WUKONG_TM_TIMER_OPR_STOP` | **用户 `delete` 取消** | 清除 UI；`remaining` 为取消时剩余秒数（未走完） |
| 5 | `WUKONG_TM_TIMER_OPR_TICK` | **Cron 进度回调**（未到点） | 刷新剩余时间；间隔见 §1.1，**不是每秒一条**（除末 ≤10s） |
| 6 | `WUKONG_TM_TIMER_OPR_FINISH` | **自然结束**（或暂停过久后 `resume` 时发现已到期） | 播放结束铃/播报；清除倒计时 UI；实例已清空，勿再 `query` 预期有活跃实例 |

当前实现**不会**对倒计时发送 `WUKONG_TM_TIMER_OPR_RESET`（4）。

### 3.2 自然结束 vs 用户取消

- **`FINISH`（6）**：时间走完，或 `resume` 时剩余已为 0。之后内部已 `__countdown_reset()`，无活跃实例。
- **`STOP`（3）**：仅来自 **`wukong_tm_countdown_delete()`**，表示用户主动停止。

### 3.3 TLV 载荷（Tag 与含义）

| Tag | 长度 | 含义 |
|-----|------|------|
| `WUKONG_TM_TAG_COUNTDOWN_OPR` | 1 | `WUKONG_TM_TIMER_OPR_E` |
| `WUKONG_TM_TAG_COUNTDOWN_HOUR` | 1 | 由**当前事件携带的剩余秒数**拆出的时（`INT_T`，可为 0） |
| `WUKONG_TM_TAG_COUNTDOWN_MINUTE` | 1 | 分 |
| `WUKONG_TM_TAG_COUNTDOWN_SECOND` | 1 | 秒 |
| `WUKONG_TM_TAG_COUNTDOWN_HANDLE` | `sizeof(UINT_T)` | 本倒计时实例句柄；每次 `create` 递增，可用于区分新旧会话 |
| `WUKONG_TM_TAG_COUNTDOWN_REMAINING_SEC` | `sizeof(UINT_T)` | **与 H/M/S 一致的剩余总秒数**（推荐使用此字段做逻辑，H/M/S 便于展示） |
| `WUKONG_TM_TAG_COUNTDOWN_ELAPSED_SEC` | `sizeof(UINT_T)` | **仅 `PAUSE`（1）**：自开始以来已走过秒数，**`duration_sec - remaining_sec`**（小端 `UINT_T`） |

**`PAUSE`：** 在以上字段之后追加 **`COUNTDOWN_ELAPSED_SEC`**，便于与剩余时间一起上报。

**`FINISH`：** `remaining` 与 H/M/S 均为 **0**。

**`TICK`：** `remaining` 为 **该次 tick 时刻**计算出的剩余秒数（实现上为 `__countdown_remaining()` 在 tick 处理中的结果）。因步进可能是 10s/60s 等，**相邻两次 `TICK` 的差值等于上次调度选用的步长**，不要假设固定 1 秒。

### 3.4 推荐的应用层写法

1. **在事件回调中**：解析 TLV，读取 `opr`、`handle`、`remaining_sec`（及 H/M/S），按上表分支处理 UI / 音效 / 播报。
2. **需要与 UI 帧率一致的刷新**：不要仅依赖 `TICK`；可在界面定时器中调用 **`wukong_tm_countdown_query()`** 读取 `remaining_sec`（尤其在长间隔 tick 阶段）。
3. **区分会话**：使用 TLV 中的 **`handle`**；新 `create` 会得到新 `handle`，避免旧事件误更新新 UI。
4. **失败与边界**：若 `create` 后 Cron 注册失败，实现会 **`__countdown_reset()`** 且**不**保证发出 `STOP`/`FINISH`；若需严谨，可在 `create` 返回非 `OPRT_OK` 时自行清理 UI，或结合 `query` 判断 `active`。

---

## 4. 与其它模块的衔接

- **显示层**：若工程将时钟事件映射到 GUI，可参考 `TY_DISPLAY_TP_CLOCK_MCP_COUNTDOWN_TIMER`（`tuya_ai_display.h`），将同一语义同步到屏端。
- **番茄钟 / 秒表**：事件码与 TLV 结构不同，请勿混用解析逻辑；番茄钟见 `README_POMODORO.md`。

---

## 5. 自测脚本

Host 侧行为测试：`test_wukong_tm_countdown.sh`（创建 / 暂停 / 恢复 / 删除、步进、到期折叠等）。
