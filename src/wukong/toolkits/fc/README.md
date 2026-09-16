# 技能模块

## 概述

技能模块解析云端下发的 JSON 文本流并路由到对应处理逻辑，覆盖 ASR 识别结果、NLG 回复文本流、SKILL 技能内容（音乐/故事/播放控制等）、CLOUD_EVENT 云端任务（时钟/提醒/铃声/通话等）四类消息。入口是 `wukong_ai_text_process(type, root, eof)`，各 provider（`provider/tuya`、`provider/jd`、`provider/cube`）在收到云端回调时统一调用它；处理结果经 `wukong_ai_event_notify()` 回送给模式层与 UI。

在应用中的角色：介于 AI agent SDK（负责解析 `bizType` 并给出 `AI_TEXT_TYPE_E`）和模式层/UI 之间的文本内容翻译层——把云端协议格式转成设备侧事件。

## 设计要点与坑

- **CLOUD_EVENT 不是 SKILL 的一种**：两者是 `AI_TEXT_TYPE_E` 平级的两个类型，`wukong_ai_text_process()` 里各自独立分发。`skill_cloudevent.c` 处理的时钟/提醒/铃声/通话任务走的是 `AI_TEXT_CLOUD_EVENT` 分支（直接调用 `wukong_ai_parse_cloud_event()`），**不经过** SKILL 消息的 `code` 字段分发。写新代码或读旧注释时不要把两者混为一谈。
- **情绪不是走 SKILL/`code=emo` 分发的**：`wukong_ai_skills.h` 头部注释里给的 `{"bizType":"SKILL","data":{"code":"emo",...}}` 示例目前不对应实际代码路径——`__wukong_ai_skill_process()` 只特殊处理 `code == "music"/"story"/"PlayControl"`，其余 code（含 `emo`）一律落到兜底分支，仅原样转发 `WUKONG_AI_EVENT_SKILL` 事件，不会被解析成表情。真正的表情来源是 NLG 回复消息里的 `tags` 数组第一项（在 `__wukong_ai_nlg_process()` 内联处理），`skill_emotion.c` 提供的只是 emoji↔名称的映射查表函数，被这段 NLG 处理逻辑调用。改情绪相关行为要去改 NLG 分支，别去找 SKILL/emo 分支。
- **`PlayControl` 分两条路**：开启 `ENABLE_TOOLKITS_PLAYBACK` 后，`action` 为 `music_list`/`refresh_play_url` 的 `PlayControl` 消息被当作异步响应，转给 `wukong_playback_ctrl_dispatch_response()` 匹配之前发出的请求；其余情况（含关闭该开关时的全部 `PlayControl`）走旧的直接解析播放列表逻辑。两条路径共存是为了让新播放控制链路和老的直接下发播放列表兼容，改动前先确认开关状态和消息里的 `action` 字段，否则容易改错分支。
- **NLG 流式拼接靠一个静态状态机**：`__wukong_ai_nlg_process()` 用文件内静态变量 `event_type`/`__s_chat_break` 判断该发 START/DATA/STOP 里的哪个事件，`eof` 单独控制流是否结束、`__s_chat_break` 决定打断后如何重新起播。这段状态机没有做多会话隔离，同一时刻只支持一路 NLG 流。
- **`wukong_skill_notify_chat_break()` 只置一个标志位**：调用后不会立即中断正在处理的内容，只是让下一次 NLG 回调按“打断后重新开始”的路径处理。

## 消息格式参考

云端下发的 JSON 消息示例（核对自 `wukong_ai_skills.h` 头部注释，仍与 `__wukong_ai_skill_process()`/`__wukong_ai_nlg_process()` 的解析路径一致；`SKILL emo` 示例除外——见下方说明）：

```jsonc
// ASR：用户语音识别结果
{"bizId":"asr-...","bizType":"ASR","eof":1,"data":{"text":"这是ASR文本！"}}

// NLG：AI 回复文本流（追加模式，eof=0 表示流式未结束）
{"bizId":"nlg-...","bizType":"NLG","eof":0,"data":{"content":"这是NLG响应文本！","appendMode":"append","finish":false}}

// SKILL music：音乐播放列表
{"bizId":"skill-...","bizType":"SKILL","eof":1,"data":{"code":"music","skillContent":{"playList":[item1,item2]}}}
```

> `{"bizType":"SKILL","data":{"code":"emo",...}}` 这条历史协议注释仍留在 `wukong_ai_skills.h` 里，但**不对应任何现有代码路径**（见上面"设计要点与坑"第二条）——情绪走 NLG 的 `tags` 字段，不要照着这条注释去接 `code=emo` 分支。

音乐播放状态经事件 `EVENT_MUSIC_PLAYER`（字符串 `"ai.music.player"`）上报，打断经事件 `EVENT_MUSIC_BREAK`（字符串 `"ai.music.break"`）通知（均定义于 `wukong_ai_skills.h`，`skill_music_story.c` 内 `ty_publish_event(EVENT_MUSIC_PLAYER, ...)` 发布）。

云事件（`skill_cloudevent.c`）承载的任务类型枚举 `WUKONG_AI_TASK_TYPE_E`（`skill_cloudevent.h`）：

| 枚举 | 值 | 任务 |
|---|---|---|
| `WUKONG_AI_TASK_NORMAL` | 0 | 普通（音乐/故事等） |
| `WUKONG_AI_TASK_CLOCK` | 1 | 时钟 |
| `WUKONG_AI_TASK_ALERT` | 2 | 提醒 |
| `WUKONG_AI_TASK_RING_TONE` | 3 | 铃声 |
| `WUKONG_AI_TASK_CALL` | 4 | 通话 |
| `WUKONG_AI_TASK_CALL_TTS` | 5 | 通话 TTS |

配套 HTTP 方法枚举 `WUKONG_AI_HTTP_METHOD_E`：`WUKONG_AI_HTTP_GET` / `POST` / `PUT`。

## 改动指南

新增一个由云端 SKILL 消息触发的自定义技能（新建 `code`、写解析代码、接入分发入口、验证）的完整步骤见 [`../../../docs/howto/add-ai-skill.md`](../../../docs/howto/add-ai-skill.md)，本文不重复。

修改已有技能（音乐/故事播放逻辑、情绪映射表、云端任务类型）时，先读对应 `skill_music_story.c` / `skill_emotion.c` / `skill_cloudevent.c` 的实现；涉及消息路由本身的改动（如新增一个 `AI_TEXT_TYPE_E` 分支）要改 `wukong_ai_skills.c` 的 `wukong_ai_text_process()`。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
