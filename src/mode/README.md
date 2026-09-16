# 模式模块

## 概述

模式模块管理 Wukong AI 的对话触发与业务模式，是「一次按键/唤醒/云端事件该怎么处理」的唯一决策点。它采用**两级模式层次**：一级**设备模式**（`AI_DEVICE_MODE_E`：闲聊、翻译、P2P、录音、生图、侦测）决定当前整机业务形态；二级**闲聊子模式**（`AI_CHAT_SUB_MODE_E`：长按、单次按键、唤醒、自由）仅在设备模式为「闲聊」时生效，决定语音对话的触发方式。每个模式实现一组回调（`AI_CHAT_MODE_HANDLE_T`），由模式管理器按当前激活模式统一分发（策略模式），对外只暴露单一入口 `wukong_ai_mode_dispatch()`——按键、云端事件、唤醒、VAD、打断等全部经它转发，调用方无需关心当前是哪个模式。

接入方式是纯配置驱动的：每个模式 `.c` 文件整体包在 `#if defined(ENABLE_AI_MODE_XXX) ...#endif` 里，由 `src/mode/Kconfig` 的开关控制是否编译进固件；`local.mk` 已经通配整个目录，新增/裁剪模式不需要碰构建脚本。`wukong_ai_mode_init()` 按开关状态把启用的模式注册进管理器，并从 KV 恢复上次激活的设备模式/闲聊子模式（带降级兜底，见下）。

## 模式行为对照

九个可运行模式（4 个闲聊子模式 + 5 个非闲聊设备模式；「闲聊」设备模式本身是容器，见上）在音频前端与打断能力上的实际行为，逐一核对对应 `wukong_ai_mode_*.c` 的 `on_init`/回调注册得出：

| 模式 | 触发方式 | AEC | VAD | KWS | TTS 播放 | 支持打断 |
|------|----------|-----|-----|-----|----------|----------|
| HOLD（闲聊/长按） | 长按按键 | ✅ | 手动 | 禁用 | ✅ | ✅ |
| ONESHOT（闲聊/单次按键） | 单次按键 | ✅ | 自动 | 禁用 | ✅ | ✅ |
| WAKEUP（闲聊/唤醒） | 关键词唤醒 | ✅ | 自动 | 启用 | ✅ | ✅ |
| FREE（闲聊/自由对话） | 唤醒 + 连续对话 | ✅ | 自动 | 启用 | ✅ | ✅ |
| TRANSLATE（翻译） | 关键词唤醒 | ✅ | 自动 | 启用 | ✅ | ✗ |
| P2P（P2P 通话） | APP 面板触发 | ✅ | 手动 | 禁用 | ✗（仅提示音，音频直通 APP） | ✗ |
| RECORD（录音） | UI 触发 | ✅ | 手动 | 禁用 | ✗（仅提示音，音频直存文件） | ✗ |
| PICTURE（生图/识图） | UI / 按键 / 唤醒触发 | ✅ | 手动 | 禁用 | ✅ | ✗ |
| DETECTION（侦测） | 摄像头事件 | ✅ | 手动 | 禁用 | ✅ | ✗ |

- **AEC** 对所有模式恒为 ✅：回声消除是音频前端（`wukong_audio_frontend.c`）流水线上对每帧都执行的处理，不是按模式开关的，因此不存在任何模式关闭 AEC 的情况。
- **VAD** 列为 `wukong_audio_input_wakeup_mode_set()` 实际传入的 `WUKONG_AUDIO_VAD_MANUAL`（按键即端点）或 `WUKONG_AUDIO_VAD_AUTO`（人声检测端点）。
- **服务器 VAD**（云端侧语音活动检测）只有 HOLD 与 PICTURE 两个模式会显式控制：`on_init` 时 `*_server_vad_ctrl(FALSE)` 关闭、`on_deinit` 时 `TRUE` 恢复；其余 7 个模式不主动调用该接口，沿用当前状态，不构成该模式的稳定行为，因此未单列为表格的一列。
- **打断**列 = 是否注册 `on_interrupt` 回调（`wukong_ai_mode_interrupt_turn`/ADR-0005 的 barge-in idiom）；只有 4 个闲聊子模式注册了它，其余 5 个模式没有实现，`AI_MODE_OP_INTERRUPT` 分发到它们会按“未实现”处理。

> 上表 ONESHOT 的 VAD 已按当前代码核实为**自动**（`wukong_ai_mode_oneshot.c` 的 `on_init` 调用 `wukong_audio_input_wakeup_mode_set(WUKONG_AUDIO_VAD_AUTO)`），旧文档曾记为“手动”，属旧文档遗留误记，本表已修正。

## 状态机

所有模式共享同一套对话状态 `AI_CHAT_STATE_E`（定义于 `wukong_ai_mode.h`）：

```
INIT → IDLE → LISTEN → UPLOAD → THINK → SPEAK → IDLE
                  ↑                               |
                  └───────────────────────────────┘
INVALID：非法/未初始化态，仅作枚举边界
```

- **INIT**：模块刚初始化，尚未进入任何可交互状态。
- **IDLE**：空闲，等待触发（按键/唤醒词/云端事件）。
- **LISTEN**：正在监听用户语音输入（录音/采集中）。
- **UPLOAD**：音频上传云端。
- **THINK**：云端 ASR/LLM 处理中；barge-in（打断/识图）也会先迁移到这个态而非 LISTEN，避免 task 轮询重新给麦克风臂上语音回合导致杂散帧混入。
- **SPEAK**：播放云端 TTS 回复。
- **INVALID**：非法态，仅用于初始值/边界保护，不是运行期会经历的正常状态。

## 设计要点与坑

- **两级拆分的原因**：把「语音怎么触发」（闲聊子模式）和「整机是什么业务形态」（设备模式）解耦，设备模式不需要重复实现按键/VAD 接线；同时每个模式文件被开关整体裁剪为空翻译单元，产品线可以零成本裁掉不需要的模式，不用改 `local.mk`。
- **注册即建锁，但只有 `on_event` 走锁**：`__register_chat_sub`/`__register_device_mode` 为每个 mode entry 各建一把互斥锁；分发时只有 `AI_MODE_OP_EVENT`（云端 ASR/TTS 事件）会 `tal_mutex_lock` 该锁，其余回调（`on_key`/`on_task`/`on_vad`/`on_client`…）都是不加锁直接调用。写新模式时不要假设这些回调之间互斥，如果它们可能被不同线程并发调用（例如按键中断 vs 主循环轮询），需要自己加保护。
- **状态持久化与降级链**：当前设备模式与闲聊子模式落 KV（`tuya_ai_toy_device_mode_get/set`、`tuya_ai_toy_trigger_mode_get/set`），`wukong_ai_mode_init()` 启动时优先恢复上次的模式；若该模式已被 Kconfig 裁掉/未启用，依次降级到 `TUYA_AI_CHAT_DEFAULT_MODE` → `AI_CHAT_SUB_HOLD` → 下一个已启用的闲聊子模式循环查找，不会因为裁掉了保存的模式而初始化失败。
- **设备模式切换与云端会话强耦合**：`wukong_ai_device_mode_switch()` 通过 `__get_scode_by_mode()` 把设备模式映射到 agent scode（闲聊/翻译/生图/侦测各有一个，P2P、录音没有），离开旧模式时会对有 scode 的模式调用 `wukong_ai_agent_del_session()`。新增设备模式时要想清楚它是否需要独立云端会话。
- **同模式切换是空操作**：`wukong_ai_device_mode_switch()` 发现目标模式等于当前模式会直接返回，跳过 DEINIT/del_session/INIT 整套流程；不要指望「重新 switch 到当前模式」来触发一次复位。
- **打断/识图走同一个 barge-in idiom（ADR-0005）**：`wukong_ai_mode_interrupt_turn()` 与 `wukong_ai_mode_picture_recognize()` 都先调用内部 `__barge_in_turn()`（停云端 TTS、停本地播放、复位音频输入、给云端发 chat_break），确保不会跟正在进行的回合抢帧。`picture_recognize` 特意把状态迁到 `THINK` 而不是 `LISTEN`——如果经过 `LISTEN`，模式的 task 轮询会重新给麦克风臂上语音回合，导致杂散帧混入这次纯图片事务、云端返回 `ASR_EMPTY` 而不是识图结果；同时它会在事务期间 `tuya_ai_input_mute_audio(TRUE)` 静音麦克风环形缓冲，即使 free 等模式在 THINK 态仍在监听也不会有音频混入。
- **`on_audio_input` 未实现不是「不处理」而是「默认转发」**：`MODE_DISPATCH_CALL` 对未实现的回调统一打日志 + 返回 `OPRT_NOT_FOUND`，唯独 `AI_MODE_OP_AUDIO_INPUT` 例外——`on_audio_input` 为 `NULL` 时会走 `__default_audio_input()`，把原始麦克风 PCM 直接转发给云端 agent。手动 VAD/自定义音频处理的模式如果忘记实现这个回调，麦克风数据仍会悄悄上行。
- **显示名数组是手工按枚举下标对齐的定长数组，不是按枚举数量自动生成**：`_chat_sub_str[]`/`_device_mode_str[]`（日志用）以及 `ENABLE_TUYA_UI` 下的 `s_chat_sub_display[]`/`s_device_mode_display[]`（UI 通知用）都要求下标与枚举值一一对应；新增枚举值时任何一个数组漏更新都会越界读到脏数据（日志乱码或 UI 通知空文案），编译期不会报错。

## 改动指南

- **新增或裁剪一个对话模式**（闲聊子模式/设备模式）：完整流程（Kconfig 开关、模式 `.c` 骨架、在 `wukong_ai_mode.c` 注册、板型启用、编译验证、裁剪与常见问题排查）见 [新增或裁剪一个对话模式](../../docs/howto/add-dialog-mode.md)，本文不重复步骤。
- **调整某个板型默认启用哪些模式 / 默认模式**：改的是 `build/appconfig/<板型名>` 配置快照，见 [build 配置说明](../../build/README_CN.md)。
- **让新的闲聊子模式可被语音/MCP 指令切换**：在 `src/wukong/mcp/tools/mcp_tool_control.c` 的 `s_mode_map[]` 补一行模式名到枚举的映射，否则 `get_mode`/`set_mode` 认不出新模式。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
