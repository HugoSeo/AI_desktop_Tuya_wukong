# KWS（关键词唤醒）模块

## 概述

KWS 模块为 Wukong AI 提供关键词唤醒检测，位于 `audio/frontend/kws/`，是音频前端处理管线的最后一环：`frontend/` 调度器做完 AEC/VAD 后自动把结果喂给它（`wukong_kws_feed_with_vad()`）。支持两种接入方式，由 `CONFIG_USING_BOARD_AUDIO_INPUT` 编译宏整体二选一：板载麦克风模式下可在 TUTUClear、SNDX 两种唤醒引擎间切换；UART 外接模式下唤醒由外部语音芯片检测并上报，本地不做 PCM 识别。

唤醒命中后发布 `EVENT_WUKONG_KWS_WAKEUP` 事件（数据为 `WUKONG_KWS_INDEX_E` 唤醒词索引），`mode/` 订阅该事件驱动对话状态切换。

**输入格式契约**：喂给 `wukong_kws_feed_with_vad()` 的 PCM 必须是 16kHz / 16-bit / 单声道，帧长 320 采样（20ms）。验证依据：`wukong_kws.c:35` 定义 `WUKONG_KWS_ONE_FRAME = (320 * 2)` 字节，即 320 个 16-bit 采样点为一帧，与该格式换算一致。

订阅唤醒事件的最小示例：

```c
ty_subscribe_event(EVENT_WUKONG_KWS_WAKEUP, "my_module", on_kws_wakeup, SUBSCRIBE_TYPE_NORMAL);
```

## 设计要点与坑

1. **环形缓冲实际容量约 1.6s**，不是更早文档里写的 2s：`WUKONG_KWS_BUFSZ = (WUKONG_KWS_ONE_FRAME(640B) × 40) × 2 ≈ 51200` 字节，按 16kHz/16bit/单声道换算约 1.6 秒。检测线程要凑够 `WUKONG_KWS_DETECT_FRAME`（100ms）数据、或 VAD 结束时强制触发，才会 `post` 一次信号量。
2. **VAD 结束时只保留最近 1s**（`WUKONG_KWS_VAD_FRAME` = 50 帧 ≈ 1000ms）音频，之前静音期间攒的数据整帧丢弃，避免检测线程在长时间静音里越攒越多、每次 detect 都要处理很长的缓冲。
3. **唤醒事件是异步发布的，不是同步 `ty_publish_event`**：`wukong_kws_event()` 把发布动作丢到 `WORKQ_HIGHTPRI` 上异步执行，源码注释明确写明原因——事件订阅者是同步跑在发布者调用栈里的，如果检测线程直接同步发布，会被下游逻辑（比如打断 TTS、复位识别状态）拖住阻塞。改动唤醒事件的投递方式前务必保留这条"必须异步"的约束。
4. **唤醒事件由引擎自己发，不是 KWS 调度代码发的**：`detect()` 回调（`tutuclear/tutuclear.c`、`sndx/sndx.c`、`uart/uart.c` 各自实现）在命中唤醒词时自己调用 `wukong_kws_event(index)`；KWS 主循环看到 `detect()` 返回 `OPRT_OK` 时只做"重置环形缓冲 + 调用 `cfg.reset()`"，并不负责发布事件。新增引擎时如果忘记在 `detect()` 里调用 `wukong_kws_event()`，唤醒会被静默吃掉（环缓冲照样复位，但没有事件发出）。
5. **`WUKONG_KWS_CFG_T.detect_sample_bytes` 控制读取对齐**：为 0 时把环缓冲里现有的全部数据整块喂给 `detect()`；非 0 时按该字节数的整数倍对齐读取，用于要求定长输入的引擎。历史接入代码不设置这个字段等价于全量读取，行为向后兼容。
6. **板载模式与 UART 模式是编译期二选一**，不是运行时可切换：`wukong_kws_init/enable/disable/feed_with_vad` 在两种模式下是完全不同的实现，由 `USING_BOARD_AUDIO_INPUT` 宏整体切换，board_init 阶段两条路径不会同时存在于固件里。

## 改动指南

- 更换预置唤醒词模型、切换 KWS 引擎（TUTUClear ↔ SNDX）、评估误唤醒/漏唤醒调参，以及真机验证方法：见应用级指南 [更换唤醒词 / 调整 KWS](../../../../../docs/howto/customize-wakeword.md)，本文档不重复其步骤。
- 新增完全自定义的唤醒词（新模型）：在 `WUKONG_KWS_INDEX_E` 里加索引，把模型编译进 `lib<name>.a` 放到 `libs/`，在对应引擎的 `create/detect/reset/deinit` 实现里接入，并按上面第 4 条在命中处调用 `wukong_kws_event(index)`。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
