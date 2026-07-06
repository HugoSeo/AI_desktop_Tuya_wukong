## Overview
The `wukong cron` module lives in `src/wukong/cron/` and provides local scheduled job management for the Wukong AI application. Each job carries a JSON-RPC 2.0 request payload that is parsed and executed when the schedule fires.

Current implementation highlights:
- 6-field cron support: `sec min hour day month weekday`
- Supported field syntax: `*`, single value, list, range, step
- In-memory job store for v1
- Single `tal_sw_timer` for nearest-deadline scheduling
- `WORKQ_SYSTEM` dispatch for actual execution
- Local JSON-RPC 2.0 method registry and executor

## Directory Structure
```text
src/wukong/cron/
|-- wukong_cron.h
|-- wukong_cron.c
|-- wukong_cron_expr.h
|-- wukong_cron_expr.c
|-- wukong_cron_rpc.h
`-- wukong_cron_rpc.c
```

## Processing Flow
```text
1. Add job
   wukong_cron_job_add(job_json, ...)
            |
            v
2. Parse job JSON
   - read id / name / enabled / cron / request
   - request must be a JSON-RPC 2.0 object
            |
            v
3. Parse cron expression
   wukong_cron_expr_parse()
            |
            v
4. Compute next fire time
   wukong_cron_expr_next_fire()
            |
            v
5. Rearm the global timer
   tal_sw_timer_start(..., TAL_TIMER_ONCE)
            |
            v
6. Timer callback
   __cron_timer_cb()
   - find due jobs
   - clone request JSON
   - schedule WORKQ_SYSTEM
            |
            v
7. Work queue execution
   __cron_job_worker()
   -> wukong_cron_rpc_execute_string()
            |
            v
8. JSON-RPC dispatch
   method -> registered handler
```

## API Reference
### Cron Service Layer
- `wukong_cron_init()`
  Initializes the cron service, local RPC executor, and scheduling timer.

- `wukong_cron_deinit()`
  Releases the timer, job table, and RPC registry.

- `wukong_cron_method_register()`
  Registers a local method callable by cron jobs.

- `wukong_cron_method_unregister()`
  Unregisters a local method.

- `wukong_cron_job_add()`
  Adds a job from a JSON string. If `id` is omitted, the module generates one.

- `wukong_cron_job_update()`
  Updates an existing job by `id`.

- `wukong_cron_job_remove()`
  Removes a job by `id`.

- `wukong_cron_job_list()`
  Returns the current job list as a JSON string.

- `wukong_cron_job_execute()`
  Parses and executes a job JSON immediately without waiting for the scheduler.

- `wukong_cron_time_ready_notify()`
  Marks local time/timezone as available, recalculates all `next_fire_ts` values, and starts scheduling.

### Cron Expression Layer
- `wukong_cron_expr_parse()`
  Parses a 6-field expression into internal bitmaps.

- `wukong_cron_expr_match()`
  Checks whether a `POSIX_TM_S` matches the expression.

- `wukong_cron_expr_next_fire()`
  Calculates the next matching timestamp after a given epoch.

### JSON-RPC Layer
- `wukong_cron_rpc_method_register()`
  Registers a `method -> handler` mapping.

- `wukong_cron_rpc_execute_json()`
  Executes a `ty_cJSON` request object.

- `wukong_cron_rpc_execute_string()`
  Executes a JSON request string and returns a JSON response string.

## Job Format
Example job payload:

```json
{
  "id": "job-1",
  "name": "demo",
  "enabled": 1,
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

Field summary:
- `id`: optional; auto-generated when omitted
- `name`: optional display name
- `enabled`: optional; `1` enables the job, `0` disables it
- `cron`: required 6-field cron expression
- `request`: required JSON-RPC 2.0 object

## Usage And Configuration
### Build Integration
`local.mk` now includes:
- header path for `src/wukong/cron`
- source collection rule for `src/wukong/cron`

### Application Lifecycle Integration
The module is currently wired into `tuya_ai_toy.c`:
- `__ai_toy_start()` calls `wukong_cron_init()`
- `__ai_toy_start()` then calls `wukong_time_manage_init()`
- `wukong_time_manage_init()` subscribes to `EVENT_TIME_SYNC` and calls `wukong_cron_time_ready_notify()` when local time/timezone become ready
- `wukong_time_manage_init()` also performs one immediate readiness check so already-synchronized devices start scheduling without waiting for a new event
- `__ai_toy_stop()` calls `wukong_cron_deinit()`

### Registering A Method
Register a local handler before adding jobs:

```c
STATIC OPERATE_RET __echo_handler(CONST ty_cJSON *params, ty_cJSON **result)
{
    *result = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(*result, "status", "ok");
    return OPRT_OK;
}

wukong_cron_method_register("echo", __echo_handler);
```

## Current Limitations
- v1 only uses an in-memory store; jobs are not restored after reboot
- `__store_load()` / `__store_sync()` are reserved hooks and are not connected to filesystem persistence yet
- `params` is currently expected to be a JSON object
- `day` and `weekday` follow common cron-compatible behavior:
  if both are restricted, either one may match

## Support
Recommended next extensions:
1. Connect `__store_load()` / `__store_sync()` to filesystem persistence
2. Add richer job management such as import/export and conflict policy
3. Expose cron management through MCP tools or another control surface
