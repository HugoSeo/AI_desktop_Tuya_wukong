## 概述
`wukong cron` 模块位于 `src/wukong/cron/`，用于在本地维护定时任务，并在命中时间点后执行一个 JSON-RPC 2.0 请求。

当前实现特点：
- 支持 6 段 cron：`sec min hour day month weekday`
- 支持字段语法：`*`、单值、列表、范围、步长
- 任务数据当前以内存态维护
- 调度基于单个 `tal_sw_timer`
- 实际执行通过 `WORKQ_SYSTEM` 异步下发
- 执行入口基于本地 JSON-RPC 2.0 方法注册表

## 目录结构
```text
src/wukong/cron/
|-- wukong_cron.h
|-- wukong_cron.c
|-- wukong_cron_expr.h
|-- wukong_cron_expr.c
|-- wukong_cron_rpc.h
`-- wukong_cron_rpc.c
```

## 处理流程
```text
1. 注册任务
   wukong_cron_job_add(job_json, ...)
            |
            v
2. 解析任务 JSON
   - 读取 id / name / enabled / cron / request
   - request 必须是 JSON-RPC 2.0 object
            |
            v
3. 解析 cron 表达式
   wukong_cron_expr_parse()
            |
            v
4. 计算下一次触发时间
   wukong_cron_expr_next_fire()
            |
            v
5. 重置全局调度 timer
   tal_sw_timer_start(..., TAL_TIMER_ONCE)
            |
            v
6. timer 命中
   __cron_timer_cb()
   - 找到已到期任务
   - 复制 request JSON
   - 投递到 WORKQ_SYSTEM
            |
            v
7. workq 执行
   __cron_job_worker()
   -> wukong_cron_rpc_execute_string()
            |
            v
8. JSON-RPC 分发
   method -> registered handler
```

## API 参考
### Cron 服务层
- `wukong_cron_init()`
  初始化 cron 服务，创建本地 RPC 执行器和调度 timer。

- `wukong_cron_deinit()`
  释放 timer、任务表和 RPC 方法表。

- `wukong_cron_method_register()`
  注册一个可供 cron 调用的本地方法。

- `wukong_cron_method_unregister()`
  注销本地方法。

- `wukong_cron_job_add()`
  添加任务。输入是任务 JSON；如果未提供 `id`，模块会自动生成。

- `wukong_cron_job_update()`
  按 `id` 更新任务。

- `wukong_cron_job_remove()`
  按 `id` 删除任务。

- `wukong_cron_job_list()`
  返回当前任务列表 JSON 字符串。

- `wukong_cron_job_execute()`
  立即解析并执行一个任务 JSON，不经过调度等待。

- `wukong_cron_time_ready_notify()`
  通知模块“本地时间和时区已可用”，随后会统一计算 `next_fire_ts` 并开始调度。

### Cron 表达式层
- `wukong_cron_expr_parse()`
  将 6 段表达式解析为内部位图结构。

- `wukong_cron_expr_match()`
  判断一个 `POSIX_TM_S` 是否匹配当前表达式。

- `wukong_cron_expr_next_fire()`
  从给定时间点开始，向后计算下一次触发时间。

### JSON-RPC 执行层
- `wukong_cron_rpc_method_register()`
  注册 `method -> handler` 映射。

- `wukong_cron_rpc_execute_json()`
  执行一个 `ty_cJSON` 请求对象。

- `wukong_cron_rpc_execute_string()`
  执行一个 JSON 字符串请求，并返回响应 JSON 字符串。

## 任务格式
任务 JSON 示例：

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

字段说明：
- `id`: 可选；如果缺省，模块内部自动生成
- `name`: 可选；任务显示名
- `enabled`: 可选；`1` 表示启用，`0` 表示禁用
- `cron`: 必填；6 段 cron 表达式
- `request`: 必填；一个 JSON-RPC 2.0 object

## 使用与配置
### 编译接入
`local.mk` 已增加：
- `src/wukong/cron` 头文件路径
- `src/wukong/cron` 源文件收集规则

### 初始化接入
当前接入点在 `tuya_ai_toy.c`：
- `__ai_toy_start()` 中调用 `wukong_cron_init()`
- `__ai_toy_start()` 随后调用 `wukong_time_manage_init()`
- `wukong_time_manage_init()` 订阅 `EVENT_TIME_SYNC`，在本地时间和时区就绪后调用 `wukong_cron_time_ready_notify()`
- `wukong_time_manage_init()` 还会在初始化完成后立即补做一次 ready 检查，避免设备已同步但没有新事件时调度仍然挂起
- `__ai_toy_stop()` 中调用 `wukong_cron_deinit()`

### 注册方法
业务层先注册本地方法，再添加任务。例如：

```c
STATIC OPERATE_RET __echo_handler(CONST ty_cJSON *params, ty_cJSON **result)
{
    *result = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(*result, "status", "ok");
    return OPRT_OK;
}

wukong_cron_method_register("echo", __echo_handler);
```

## 当前限制
- 当前仅实现内存态任务存储，重启后不会恢复任务
- `store load/store` 仅保留为内部抽象钩子，尚未落到文件系统
- `params` 当前要求为 JSON object
- `day` 与 `weekday` 的匹配策略采用常见 cron 兼容逻辑：
  当两者都不是通配时，满足任一即可命中

## 支持
如果后续需要增强，建议按下面顺序扩展：
1. 将 `__store_load()` / `__store_sync()` 接到文件系统持久化
2. 增加任务数量上限、冲突策略和批量导入导出
3. 为 cron 模块补充 MCP tool 或 CLI 管理入口
