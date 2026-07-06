# 时间管理模块语音测试用例

> 适用于闹钟、提醒、番茄时钟、倒计时、正计时五个子功能的端到端语音测试。
>
> **建议执行顺序**：先从每个功能的第一条开始顺序过（A1→R1→C1→S1→P1），确认基本链路通；然后跑边界用例（A8-A10, R7-R10, C6, S5, P7-P8）；最后跑综合场景 X1-X5。

---

## 一、闹钟（device_alarm_set / device_alarm_query）

### 基础 CRUD

| # | 语音指令 | 预期 MCP 行为 | 验证要点 |
|---|---------|--------------|---------|
| A1 | "帮我设一个明早七点的闹钟" | `operation=0, hour=7, minute=0, repeat_type=0(once)` | 返回 `alarm_id`，cron 表达式 `0 0 7 <明天日> <月> *` |
| A2 | "设一个每天早上八点半的闹钟" | `operation=0, hour=8, minute=30, repeat_type=1(daily)` | `once=0`，cron `0 30 8 * * *` |
| A3 | "设一个工作日早上六点四十五的闹钟" | `operation=0, hour=6, minute=45, repeat_type=2(weekly), weekday_mask` 包含周一至周五 | mask 对应 Mon-Fri |
| A4 | "查看我的闹钟" | 调用 `device_alarm_query` | 返回 JSON 列表，包含 A1-A3 |
| A5 | "把八点半的闹钟改成九点" | `operation=2, hour=9, minute=0` 或带 `id` | 更新成功，新 cron `0 0 9 * * *` |
| A6 | "删除明早七点的闹钟" | `operation=1`，按时间或 `id` 匹配 | 返回 true，对应 cron job 被移除 |
| A7 | "关闭所有闹钟" | 逐个 `operation=1` 或一次性删除 | 所有 alarm slot 清空 |

### 边界与贪睡

| # | 语音指令 | 预期 MCP 行为 | 验证要点 |
|---|---------|--------------|---------|
| A8 | "设一个下午两点十分的闹钟" | `hour=14, minute=10` | 确认 AI 正确转换 24 小时制 |
| A9 | （等闹钟响后不说话，30 秒后） | 进入贪睡，5 分钟后再响 | 日志出现 `alarm.snooze.timeout` 和 `alarm.snooze.fire` |
| A10 | （闹钟响时说）"关掉" / "好的" | 触发 ack，停止响铃 | `alarm.ack` 清除 runtime cron |

---

## 二、提醒（device_schedule_set / device_schedule_query）

### 基础 CRUD

| # | 语音指令 | 预期 MCP 行为 | 验证要点 |
|---|---------|--------------|---------|
| R1 | "提醒我今天下午三点开会" | `operation=0, year/month/day=今天, hour=15, minute=0, message="开会"` | cron 表达式 `0 0 15 <日> <月> *`，触发后 AI 播报"开会" |
| R2 | "提醒我明天上午十点半面试" | `year/month/day=明天, hour=10, minute=30, message="面试"` | 日期为明天，hour=10, minute=30 精确保留 |
| R3 | "提醒我今晚八点吃药" | `hour=20, minute=0, message="吃药"` | AI 正确将"今晚"解析为今天 |
| R4 | "查看我的提醒" | 调用 `device_schedule_query` | 返回 R1-R3 列表，含 `start_timestamp` |
| R5 | "把面试的提醒改到下午两点" | `operation=2, reminder_id 或时间匹配, new_hour=14, new_minute=0` | `start_timestamp` 更新，message 不变 |
| R6 | "删除吃药的提醒" | `operation=1, reminder_id` 或时间匹配 | cron job 移除 |

### 时间准确性重点

| # | 语音指令 | 预期 MCP 行为 | 验证要点 |
|---|---------|--------------|---------|
| R7 | "提醒我下午一点十分下班" | `hour=13, minute=10` | **核心**：验证 minute=10 不被篡改（历史 bug） |
| R8 | "提醒我今天下午五点四十五回家" | `hour=17, minute=45` | 确认不会被四舍五入到整点 |
| R9 | "提醒我后天早上九点去银行" | `day=今天+2` | 确认日期偏移计算正确 |

### 触发验证

| # | 语音指令 | 预期 MCP 行为 | 验证要点 |
|---|---------|--------------|---------|
| R10 | "提醒我一分钟后测试" | `hour/minute` 设为当前+1分钟 | 实际等待，验证 AI 语音播报 message 内容 |

---

## 三、倒计时（device_countdown_timer_set）

| # | 语音指令 | 预期 MCP 行为 | 验证要点 |
|---|---------|--------------|---------|
| C1 | "倒计时五分钟" | `operation=0, minutes=5` | 返回 handle，cron tick 开始 |
| C2 | "暂停倒计时" | `operation=1` | state 变为 paused |
| C3 | "继续倒计时" | `operation=2` | 从暂停处继续 |
| C4 | "取消倒计时" | `operation=3` | cron job 移除，slot 清空 |
| C5 | "倒计时十秒" | `operation=0, seconds=10` | 最后 10 秒按秒 tick（验证自适应步长） |
| C6 | （C5 未结束时）"再倒计时三分钟" | `operation=0` 返回 `already_exists` + 当前状态 JSON | 单例约束生效 |
| C7 | "倒计时一个半小时" | `hours=1, minutes=30` | 初始 tick 间隔 60s |

---

## 四、正计时 / 秒表（device_stopwatch_timer_set）

| # | 语音指令 | 预期 MCP 行为 | 验证要点 |
|---|---------|--------------|---------|
| S1 | "开始计时" | `operation=0` | 返回 true；事件 TLV 仅 `opr` |
| S2 | "暂停计时" | `operation=1` | 返回 `{"success":true,"elapsed_sec":N}`；事件 TLV 含 `WUKONG_TM_TAG_STOPWATCH_ELAPSED_SEC` |
| S3 | "继续计时" | `operation=2` | paused=false，时间继续累加 |
| S4 | "停止计时" | `operation=3` | 返回 JSON 含**最终** `elapsed_sec`；设备事件同上 |
| S5 | （S1 未停时）"开始计时" | 返回 `already_exists` + `elapsed_sec` | 单例约束生效 |
| S6 | "重置计时" | `operation=4` | 返回 JSON 含复位前 `elapsed_sec`；active=false |
| S7 | "计时两分钟后停止" → 等 2 分钟后 "停止计时" | 手动验证 | `elapsed_sec ≈ 120` |
| S8 | "现在计了多久" / "秒表跑了多久" | `operation=5`（query） | 返回 `active`/`paused`/`elapsed_sec` |

---

## 五、番茄时钟（device_pomodoro_timer_set）

| # | 语音指令 | 预期 MCP 行为 | 验证要点 |
|---|---------|--------------|---------|
| P1 | "开始番茄时钟" | `operation=0, work_duration=25, short_break=5, long_break=15`（默认值） | 返回 true，phase=work |
| P2 | "查看番茄时钟状态" | `operation=4` | 返回 JSON：active, phase, current_cycle, remaining_sec |
| P3 | "暂停番茄时钟" | `operation=1` | paused=true |
| P4 | "继续番茄时钟" | `operation=2` | paused=false，剩余时间从暂停处续 |
| P5 | "停止番茄时钟" | `operation=3` | active=false，cron 清除 |
| P6 | "开始一个 15 分钟工作、3 分钟休息的番茄时钟" | `work_duration=15, short_break_duration=3` | 参数正确传入 |
| P7 | （P1 未停时）"开始番茄时钟" | 返回 `already_exists` + 当前完整状态 | 单例约束，不会重置当前进度 |
| P8 | （P1 未停时）"把番茄时钟改成三轮" | 返回失败或 `already_exists` | 不支持修改，只能停止后重建 |

---

## 六、综合 / 交叉场景

| # | 语音指令 | 预期 MCP 行为 | 验证要点 |
|---|---------|--------------|---------|
| X1 | "设一个五分钟的闹钟" | 应走闹钟路径（`device_alarm_set`），不是倒计时 | AI 正确区分"闹钟"和"倒计时" |
| X2 | "五分钟后提醒我喝水" | 应走提醒路径（`device_schedule_set`），不是倒计时 | AI 正确区分"提醒"和"倒计时" |
| X3 | "倒计时五分钟"（已有倒计时时） | `already_exists` | 不会影响正在运行的番茄/秒表 |
| X4 | 同时运行闹钟 + 提醒 + 倒计时 + 秒表 + 番茄 | 各自独立运行 | 五个功能互不干扰 |
| X5 | "取消所有定时器" | AI 逐一调用各功能的停止/删除 | 全部清理干净 |

---

## 日志验证要点

测试时关注串口/日志中以下关键字段：

- **MCP 请求**：`tuya_ai_agent.c` 中的 `mcp data:` 行，确认 `name`、`arguments` 是否符合预期
- **MCP 响应**：`mcp_server.c` 中的 `MCP ->` 行，确认返回 `success`/`false` 和具体字段
- **Cron 调度**：`wukong_cron.c` 中的 `cron -> job add:` 行，确认 cron 表达式和 `once` 标志
- **时间字段**：重点检查 `hour`、`minute` 是否与语音指定的完全一致，不被 AI 篡改
- **单例冲突**：`already_exists` + `current` JSON 快照
- **贪睡链路**：`alarm.snooze.timeout` → `alarm.snooze.fire` → 再次播放铃声
