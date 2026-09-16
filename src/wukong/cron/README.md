# Wukong Cron 模块

## 概述

`wukong_cron`（`src/wukong/cron/`）是本地定时任务调度模块：任务以「6 段 cron 表达式 + 一个 JSON-RPC 2.0 请求」的形式登记，到时由模块自动执行该 JSON-RPC 请求。`wukong_tm`（闹钟/提醒/倒计时/番茄时钟）是它目前唯一的业务方，通过 `wukong_cron_method_register()` 注册本地方法（如 `alarm.fire`、`tm.pomodoro.phase_end`），再用 `wukong_cron_job_add()` 登记任务；到点后 cron 会把登记的 JSON-RPC 请求分发给对应 handler。接入点在 `tuya_ai_toy.c`：`__ai_toy_start()` 中先 `wukong_cron_init()` 再 `wukong_time_manage_init()`，后者会订阅 `EVENT_TIME_SYNC`（本地时间/时区就绪后调用 `wukong_cron_time_ready_notify()`，并在初始化完成时立即补做一次就绪检查）；`__ai_toy_stop()` 中先 `wukong_time_manage_deinit()` 再 `wukong_cron_deinit()`。

任务 JSON 示例（`wukong_cron_job_add()` 的入参，字段经 `__job_parse_json()` 核实）：

```json
{
  "id": "job-1",
  "name": "demo",
  "enabled": 1,
  "once": 0,
  "cron": "*/15 * * * * *",
  "request": {
    "jsonrpc": "2.0",
    "id": "req-1",
    "method": "echo",
    "params": {
      "message": "hello"
    }
  }
}
```

字段说明：

| 字段 | 必填 | 说明 |
|---|---|---|
| `id` | 否 | 缺省时由调用方传入 `auto_id=TRUE` 让模块自动生成（`wukong_cron_job_add()` 内部行为） |
| `name` | 否 | 任务显示名，仅用于 `wukong_cron_job_list()` 展示 |
| `enabled` | 否 | `1`/`0`；缺省视为 `1`（启用） |
| `once` | 否 | `1`/`0`；缺省视为 `0`。为 `1` 时该 job **首次触发后由 cron 模块自身移除**（`__cron_timer_cb()` 里 `job->once` 为真时调用 `__job_reset()`），调用方无需手动 `wukong_cron_job_remove()`；`wukong_tm_reminder` 的一次性提醒即用此字段实现 |
| `cron` | 是 | 6 段 cron 表达式，解析失败（如非法字段）整个 `job_add` 直接返回 `OPRT_INVALID_PARM` |
| `request` | 是 | 必须是 JSON-RPC 2.0 object；`params` 字段当前只接受 object，不能是数组或标量 |

## 设计要点与坑

- **单 timer 承载全部任务**：调度基于一个 `tal_sw_timer`，只对准"下一个最近到期的任务"重新计时（`tal_sw_timer_start(..., TAL_TIMER_ONCE)`），而不是每个任务各自一个定时器；新增/删除/更新任务都会触发重新计算最近到期时间并重置这个唯一 timer。任务量大时不要假设有独立定时器资源，行为退化取决于这一个 timer 的精度与调度延迟。
- **执行不在 timer 回调里同步跑**：timer 回调 `__cron_timer_cb()` 只挑出到期任务、复制一份 `request` JSON，真正的方法执行通过 `tal_workq_schedule(WORKQ_SYSTEM, __cron_job_worker, work)` 丢到系统工作队列异步执行，再落到 `wukong_cron_rpc_execute_string()` 做 JSON-RPC 分发。这意味着 handler 是在 `WORKQ_SYSTEM` 线程上下文里跑的，不是调用 `wukong_cron_job_add()` 的线程，也不是 timer 中断/线程本身；写 handler 时不能假设与登记方同线程。
- **纯内存态，重启不恢复**：当前版本任务表完全在内存中，`__store_load()` / `__store_sync()`（`wukong_cron.c`）只是预留钩子，尚未接文件系统，重启后 cron 自身不会恢复任何任务。`wukong_tm` 层的闹钟/提醒能"重启后还在"，是因为它们在自己的模块内单独做了 KV 持久化、重启后重新调用 `wukong_cron_job_add()` 重建任务，并不是 cron 模块本身的能力——不要指望在 cron 层加个任务就能自动跨重启存活。
- **`day` 与 `weekday` 是"或"语义**：`wukong_cron_expr_match()`（`wukong_cron_expr.c`）里，当 `day` 和 `weekday` 两个字段都被限定（不是通配 `*`）时，命中逻辑是**满足任一即可**，这是与主流 cron 实现保持一致的常见约定，而不是要求两者同时满足；只有一方或两方为空/全通配时才退化为对方单独判断。
- **`request.params` 目前只接受 JSON object**：`wukong_cron_job_add()` 校验 `request` 必须是合法 JSON-RPC 2.0 对象，`params` 字段被约定为 object；传数组或标量会在解析阶段失败，不要依赖非 object 的 `params`。

## 改动指南

- **新增一种可被 cron 调度的本地方法**：调用 `wukong_cron_method_register(method_name, handler)` 注册 `handler(CONST ty_cJSON *params, ty_cJSON **result)`，登记任务时把 `request.method` 设为该名字；参考 `wukong_tm_alarm.c` 里 `alarm.fire`/`alarm.snooze.*` 或 `wukong_tm_pomodoro.c` 里 `tm.pomodoro.phase_end` 的注册方式。
- **改 6 段 cron 表达式的语法支持**（`*`/单值/列表/范围/步长）：改 `wukong_cron_expr.c` 的 `wukong_cron_expr_parse()`，同时检查 `wukong_cron_expr_match()` / `wukong_cron_expr_next_fire()` 是否需要同步调整；日/周字段的"或"语义（见上）改动前务必确认不会破坏依赖它的任务。
- **要让任务真正持久化跨重启**：不要指望改 cron 层的 `__store_load()`/`__store_sync()` 就够——目前没有业务方依赖 cron 自身持久化；应仿照 `wukong_tm_alarm.c` 的模式在业务层自己做 KV 存取 + 重启后重新 `job_add()`。若确实要让 cron 层原生支持持久化，需要同时接好 `__store_load()`/`__store_sync()` 到文件系统，并处理好与业务层各自持久化之间的一致性。
- **调试任务是否正常触发**：`wukong_cron_job_list()` 可导出当前任务表 JSON 排查 `next_fire_ts`／`enabled`；`wukong_cron_job_execute()` 可跳过调度直接立即执行一次任务 JSON，用于验证 handler 本身逻辑是否正确。
- **运行测试**：`test_wukong_cron_core.sh`（任务增删改查、调度）、`test_wukong_cron_expr.sh`（表达式解析与匹配）、`test_wukong_cron_rpc.sh`（JSON-RPC 分发）。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
