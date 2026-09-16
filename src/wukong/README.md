# Wukong AI 核心域

## 概述

`wukong/` 是 Wukong AI 的核心域：承载 AI 对话回路（多供应商 Provider 抽象）、音频全链路（输入/AEC-VAD 前端/KWS 唤醒/播放与播放列表）、多模态输入（图片、视频）、技能路由、MCP、时间管理、本地定时任务与消息通道等能力。它**不**包含对话状态机（在 `src/mode/`）和具体开发板适配（在 `src/boards/`、`src/drivers/`）——`wukong/` 只依赖 TAL/TKL 抽象层与事件总线，向上通过事件通知，不直接管 UI/LED/按键状态。

上层（`mode/`、板级 UI、技能内部）通过 `wukong_ai_agent.h` 定义的门面 API（`wukong_ai_agent_init`、`wukong_ai_agent_send_text/send_audio/send_image/send_video/send_file`、`wukong_ai_agent_cloud_alert`、`wukong_ai_agent_role_switch`、事件回调 `WUKONG_EVENT_OUTPUT`）接入本域，不需要关心当前激活的是哪个云端供应商。

## 子模块一览

- **[audio/](audio/README.md)** — 音频输入输出、AEC/VAD 前端、KWS 唤醒、TTS/音乐播放与本地播放列表控制。
- **provider/** — AI 服务供应商适配（涂鸦云 `tuya`、京东 `jd`/joyinside、`cube`/本地小智协议），实现 `wukong_ai_agent` 门面的实际收发；无独立 README，接口定义见 `provider/wukong_ai_provider.h`。
- **channel/** — 消息通道抽象（IM 微信等），按 `channel`/`chat_id` 做多会话路由；无 README。
- **cli/** — CLI 文本输入通道，供自动化测试代替语音输入；无 README。
- **[storage/](storage/README.md)** — 外部大容量存储统一层（SD 卡 / 外挂 QSPI Flash / 无，编译期单选）：全 app 唯一一次开机异步挂载、`EVENT_WUKONG_STORAGE_READY` 事件通知就绪、统一根路径宏 `WUKONG_STORAGE_ROOT`；`(ns, name)` 两级键的小记录持久化（记录层）也在这层。
- **[skills/](skills/README.md)** — 技能路由系统（音乐故事、情绪、云端事件）。
- **[mcp/](mcp/README.md)** — MCP (Model Context Protocol) server，设备端作为 MCP server 接受云端 LLM 调用。
- **[tm/](tm/README.md)** — 时间管理：闹钟、提醒、倒计时、秒表、番茄钟。
- **[cron/](cron/README.md)** — 本地定时任务与 JSON-RPC 调度。
- **[picture/](picture/README.md)** — 图像输入输出（AI 生图/识图、相册）。
- **[video/](video/README.md)** — 视频输入输出处理。
- **utility/** — 通用工具函数（如秒表底层实现）；无 README。
- **[assets/](assets/README.md)** — 提示音等资源文件。
- **wukong_ai_agent.c/.h** — 域门面头文件声明 + 空实现桩，见下方设计要点第 1 条。

> 对话模式状态机不在 `wukong/` 下，在同级目录 [`src/mode/`](../mode/README.md)（hold/oneshot/wakeup/free + translate/p2p/record/picture/detection）；`mode/` 订阅 wukong 发出的事件驱动状态切换与 UI 展示。

## 业务流程

一次完整的多模态对话请求，按下面 4 个阶段流转（阶段 1 的实现归属已按当前源码修正——真正干活的是 provider 层，`wukong_ai_agent.c` 只是门面声明）：

1. **初始化阶段**：`wukong_ai_agent_init(cb)` 是门面入口，本身不做业务逻辑；实际工作在 `provider/wukong_ai_provider.c` 里——按编译宏 `ENABLE_PROVIDER_JD`/`ENABLE_PROVIDER_CUBE` 选出 `WUKONG_DEFAULT_PROVIDER`（默认 `tuya`），调用 `wukong_ai_agent_switch_provider()` 激活该 provider，走到具体 provider（如 `provider/tuya/wukong_provider_tuya.c`）的 `ops->init()`，由它注册音视频编解码器配置和云端回调。之后再进行音频/KWS/`mode` 的初始化。
2. **输入处理阶段**：用户输入分流进入不同通路——语音经 [音频输入](audio/README.md) → 前端 AEC/VAD → [KWS](audio/frontend/kws/README.md) 唤醒 → ASR；文本直接处理；图像经 [图像输入](picture/README.md) 识别；视频经 [视频输入](video/README.md) 分析。这些输入最终都经由 `wukong_ai_agent_send_*` 系列门面函数送到当前激活的 provider。
3. **云端处理阶段**：云端 AI 服务完成 ASR/LLM/技能识别/TTS 生成后，通过云端回调（文本流回调、媒体数据回调、事件回调、MCP request）把结果传回 provider 层。
4. **输出处理阶段**：[技能处理](skills/README.md) 解析回调数据并路由到具体技能（音乐/故事、时钟/定时器、情绪、云端事件、MCP request）；随后统一经 `wukong_ai_event_notify()` 发布事件（ASR/TTS/技能/播放控制/MCP request 等类型）；[`mode/` 状态机](../mode/README.md) 订阅这些事件驱动状态切换、LED、定时器；最终落到 [音频输出](audio/README.md)（TTS/音乐/提示音播放）或 MCP/文本输出。

## 设计要点与坑

1. **`wukong_ai_agent.c` 是空桩文件**：其注释明确写着"stub — all implementation moved to provider/"，build 通过 `find src/wukong -maxdepth 1` 捡到它但编译不出任何符号。`wukong_ai_agent_init/send_text/cloud_alert` 等门面函数的真正实现在 `provider/wukong_ai_provider.c`（门面 API + Provider 管理器 + 处理线程），具体云端逻辑在 `provider/tuya/wukong_provider_tuya.c` 等各供应商文件里。改门面行为不要在 `wukong_ai_agent.c` 里找。
2. **Provider 是 ops+handle 抽象**：`tuya`/`jd`/`cube` 各自注册一个 `WUKONG_AI_PROVIDER_T`，携带 `init/deinit/is_ready/send/llm_infer/mcp_send/abort/ioctl` 的 ops 表。启动时按 `ENABLE_PROVIDER_JD`/`ENABLE_PROVIDER_CUBE` 编译宏选出 `WUKONG_DEFAULT_PROVIDER`（默认 `tuya`）；也可运行时用 `wukong_ai_agent_switch_provider()` 切换，切换时会先 `abort`+`deinit` 旧 provider 再 `init` 新的。当前 provider 指针本身没有加锁，切换应避开有并发对话请求的时间窗口，否则和处理线程/门面调用之间存在竞态。
3. **两条发送路径**：IM 等外部通道的消息经队列 + 处理线程（`wk_provider`，`THREAD_PRIO_3`）异步分发给 `ops->send`/`ops->llm_infer`；本地调用（`wukong_ai_agent_send_text/audio/image/video/file`）走"quick path"，绕过队列直接调 `ops->send`，`channel`/`chat_id` 固定写死为 `"local"`。改本地发送逻辑要改 provider 层的 quick path 函数，不要以为要走队列。
4. **文本流事件可能被 IM 通道截胡**：`wukong_ai_event_notify()` 对 `TEXT_STREAM_*` 系列事件会先尝试 `wukong_ai_channel_dispatch_reply()` 投给 IM 通道；一旦投递成功就直接返回，**不再**回调 `wukong_ai_agent_init()` 注册的 board `event_cb`。调试"UI 收不到 AI 回复文本"时先确认当前活跃 `chat_id` 是不是被 IM 通道占用。
5. **`llm_infer` 路径尚未真正接入**：ops 表里 `llm_infer` 非空的 Provider（为 Cube/自定义 LLM 预留的 "Claw Provider"）在处理线程里目前只打一条 `PR_WARN` 占位（"agent loop not yet implemented"），不会真正调用 LLM；现状唯一可用的是 `ops->send`（tuya/jd 云端）路径。

## 使用示例

一次典型的接入顺序：初始化 AI 代理 → 初始化音频输入/输出 → 初始化 KWS → 初始化对话模式状态机；之后即可通过门面 API 发送文本/图像、请求云端提示音、切换角色。

```c
#include "wukong_ai_agent.h"
#include "wukong_ai_mode.h"
#include "wukong_audio_input.h"
#include "wukong_audio_output.h"
#include "wukong_kws.h"

/* 事件回调：根据事件类型处理 AI 输出 */
VOID ai_event_handler(WUKONG_AI_EVENT_T *event)
{
    switch (event->type) {
    case WUKONG_AI_EVENT_ASR_OK:
        printf("[ASR] 用户: %s\n", ((WUKONG_AI_TEXT_T *)event->data)->data);
        break;
    case WUKONG_AI_EVENT_TEXT_STREAM_DATA:
        printf("[AI] %s", ((WUKONG_AI_TEXT_T *)event->data)->data);
        break;
    default:
        break;
    }
}

/* 系统初始化 */
OPERATE_RET wukong_system_init(VOID)
{
    /* 1. 初始化 AI 代理（内部按 WUKONG_DEFAULT_PROVIDER 选择并激活云端 provider） */
    wukong_ai_agent_init(ai_event_handler);

    /* 2. 初始化音频（参见 [音频模块](audio/README.md)） */
    wukong_audio_input_init(&input_cfg);
    wukong_audio_output_init(&output_cfg);

    /* 3. 初始化 KWS（参见 [KWS 模块](audio/frontend/kws/README.md)） */
    wukong_kws_init(&kws_cfg);

    /* 4. 初始化对话模式状态机（参见 [模式模块](../mode/README.md)） */
    wukong_ai_mode_init();

    return OPRT_OK;
}

/* 发送数据到 AI */
VOID wukong_send_demo(VOID)
{
    /* 发送文本查询 */
    wukong_ai_agent_send_text("今天天气怎么样？");

    /* 发送图像进行识别 */
    wukong_ai_agent_send_image(image_data, image_len);

    /* 请求云端提示音 */
    wukong_ai_agent_cloud_alert(AT_NETWORK_CONNECTED);

    /* 切换 AI 角色 */
    wukong_ai_agent_role_switch("teacher");
}
```

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
