# 新增一个 AI 技能

## 目标

在 wukong demo 里新增一个由云端下发的 `SKILL` 消息触发的自定义技能（本文以新增 `code="my_skill"` 为例），完成从认识数据流、写解析/处理代码、接入分发入口到验证的全过程。

技能模块的架构、既有技能（音乐故事、情绪、云事件）说明见 [`src/wukong/toolkits/fc/README.md`](../../src/wukong/toolkits/fc/README.md)；本文只讲“怎么加一个新的”。

## 前置条件

- 已按[快速开始](../quickstart.md)跑通编译、烧录、能正常语音对话。
- 云端/技能配置平台一侧已经配置好会下发 `bizType="SKILL"`、`data.code="my_skill"` 的技能内容——这是云端侧配置，不在本仓库范围内，本文只覆盖设备端代码。
- 了解[整体架构](../architecture.md)里 `wukong/` 的角色定位。

## 步骤

### 1. 认识数据流

云端文本流的落地路径（以默认 tuya provider 为例）：

```
云端 SKILL 消息
   │
   ▼
__tuya_text_cb()                              # src/wukong/provider/tuya/wukong_provider_tuya.c:135
   │  wukong_ai_text_process(type, root, eof)
   ▼
wukong_ai_text_process()                      # src/wukong/skills/wukong_ai_skills.c:182
   │  type == AI_TEXT_SKILL
   ▼
__wukong_ai_skill_process()                   # src/wukong/skills/wukong_ai_skills.c:18
   │  按 root 里的 "code" 字段二次分发
   ├─ code == "music" / "story"       → skill_music_story
   ├─ code == "PlayControl"           → skill_music_story / wukong_playback_ctrl
   └─ 其他                             → wukong_ai_event_notify(WUKONG_AI_EVENT_SKILL, root)
```

`__wukong_ai_skill_process()` 收到的 `root` 就是消息里的 `data` 对象（函数内注释 `//! root is data:{}, parse code`），新技能直接从 `root` 上取字段即可，不需要再剥一层 `data`。

若用的是非默认 provider（`CONFIG_ENABLE_PROVIDER_JD` / `CONFIG_ENABLE_PROVIDER_CUBE`），文本流分别经 `src/wukong/provider/jd/wukong_provider_jd.c` 或 `src/wukong/provider/cube/wukong_provider_cube.c` 调用同一个 `wukong_ai_text_process()`，后续步骤不变。

### 2. 新建技能源文件

在 `src/wukong/skills/` 下新建一对 `.h/.c`（命名沿用 `skill_<name>` 前缀）：

```bash
cd apps/tuyaos_demo_wukong_ai/src/wukong/skills
```

`skill_my_skill.h`：

```c
#ifndef __SKILL_MY_SKILL_H__
#define __SKILL_MY_SKILL_H__

#include "tuya_cloud_types.h"
#include "ty_cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

OPERATE_RET wukong_ai_parse_my_skill(ty_cJSON *json);

#ifdef __cplusplus
}
#endif

#endif  /* __SKILL_MY_SKILL_H__ */
```

`skill_my_skill.c`（最小骨架，字段解析、错误处理照抄 `skill_emotion.c` / `wukong_ai_skills.c` 里已有的写法）：

```c
#include "wukong_ai_skills.h"
#include "skill_my_skill.h"
#include "tal_log.h"

OPERATE_RET wukong_ai_parse_my_skill(ty_cJSON *json)
{
    TUYA_CHECK_NULL_RETURN(json, OPRT_INVALID_PARM);

    ty_cJSON *content = ty_cJSON_GetObjectItem(json, "skillContent");
    CHAR_T *text = ty_cJSON_GetStringValue(ty_cJSON_GetObjectItem(content, "text"));

    TAL_PR_NOTICE("my_skill content: %s", text ? text : "(null)");

    // TODO: 在这里触发业务逻辑（播放音频、点亮 LED、切换 UI 状态等）。
    // 如果要让 UI / 对话模式层感知这次技能，走通用事件：
    // wukong_ai_event_notify(WUKONG_AI_EVENT_SKILL, json);

    return OPRT_OK;
}
```

不需要改 `local.mk` 或 Kconfig：`local.mk` 用 `find` 通配整个 `src/wukong/skills/` 目录下的 `*.c`（`local.mk:192`），头文件搜索路径也已经整目录加入（`local.mk:136`），新文件放进去即自动参与编译。

### 3. 在分发入口里接入

编辑 `src/wukong/skills/wukong_ai_skills.c`：引入头文件，并在 `__wukong_ai_skill_process()` 的 `if/else if` 链里加一个分支（放在已有的 `music`/`story`/`PlayControl` 判断之后、最终的 `else` 之前）：

```c
#include "skill_my_skill.h"          // 加在文件顶部，和其它 skill_*.h 放在一起
```

```c
    } else if (strcmp(code, "my_skill") == 0) {
        wukong_ai_parse_my_skill(root);
    } else {
        TAL_PR_NOTICE("skill %s not handled", code);
        wukong_ai_event_notify(WUKONG_AI_EVENT_SKILL, root);
    }
```

`if/else if` 链先到先得——`code` 一旦被前面的分支命中就不会再进入你的分支，所以新分支要用一个不与 `music`/`story`/`PlayControl` 冲突的 `code` 值（需与云端/技能配置平台约定一致）。

### 4.（可选）让 UI 或对话模式感知这次技能

如果技能只需要在设备端本地触发一个动作（播放音效、点灯等），第 2、3 步已经够用。如果还需要让 UI 或某个对话模式的状态机响应，有两种做法：

- **复用通用事件**：调用 `wukong_ai_event_notify(WUKONG_AI_EVENT_SKILL, root)`（就是目前"其他 code"分支已经在做的事）。这个事件会被送到全部 6 个对话模式文件（`src/mode/wukong_ai_mode_{hold,oneshot,wakeup,free,translate,picture}.c`）里各自的 `case WUKONG_AI_EVENT_SKILL:` 分支——目前这些分支都是空的 stub，在需要响应的模式文件里按需补逻辑。
- **新增专属事件类型**：如果不想和其它未处理的 SKILL 消息混在一起分辨 `code`，可以在 `src/wukong/wukong_ai_agent.h` 的 `WUKONG_AI_EVENT_TYPE_E` 枚举里追加一个新值（追加在 `WUKONG_AI_EVENT_EXIT` 之前，和这个枚举里已有的历次增量方式一致），然后在你的技能处理函数里改用 `wukong_ai_event_notify(你的新事件, root)` 通知。

### 5. 编译

```bash
make app APP_NAME=tuyaos_demo_wukong_ai
```

## 验证方法

1. `wukong_ai_skills.c` 里已有的 `TAL_PR_NOTICE("wukong text -> skill code: %s", ...)`（`__wukong_ai_skill_process` 开头）会打印收到的完整 SKILL JSON——先确认串口日志里 `code` 字段确实是你约定的值。
2. 你在 `wukong_ai_parse_my_skill()` 里加的 `TAL_PR_NOTICE` 应该在触发一次对应云端技能后打印出来；打印不出来说明没走到你的分支（回看步骤 3 的 `code` 匹配、以及 provider 是否正确调用了 `wukong_ai_text_process`）。
3. 若走了 `wukong_ai_event_notify(WUKONG_AI_EVENT_SKILL, root)` 路径，在对应模式文件的 `case WUKONG_AI_EVENT_SKILL:` 里临时加一行日志，确认事件被该模式收到。
4. 走完整链路验证：设备处于对话中，触发云端下发这个技能，确认设备侧的实际动作（播放/显示/上报等）符合预期。

## 常见问题

**串口完全没打印 `wukong text -> skill code`**
先确认走的是哪个 provider——`CONFIG_ENABLE_PROVIDER_JD` / `CONFIG_ENABLE_PROVIDER_CUBE` 开启时，文本流分别经 `wukong_provider_jd.c` / `wukong_provider_cube.c`，而不是默认的 `wukong_provider_tuya.c`；确认对应 provider 确实以 `AI_TEXT_SKILL` 类型调用了 `wukong_ai_text_process()`。

**日志里 `code` 是对的，但没进我的分支**
`__wukong_ai_skill_process()` 是顺序 `if/else if` 判断，`code` 与前面已有分支（`music`/`story`/`PlayControl`）重名会被拦截；换一个未被占用的 `code` 值，并和云端/技能配置平台对齐。

**`skillContent` 里解不出想要的字段**
`skillContent` 的具体结构由云端/技能配置平台一侧定义，设备端只能按约定字段名解析。可以参考 `skill_music_story.c` 的 `wukong_ai_parse_music()`——它同时兼容了 `general`/`custom` 两种历史遗留 JSON 布局，如果你的技能后续也要迭代 JSON 结构，留好向前兼容的判断分支。

> **说明**：`skillContent` 是 `wukong_ai_skills.h` 头部注释里给出的消息示例字段名；具体到某个云端技能配置下发的真实 JSON 布局，请以技能配置平台的实际下发内容为准。

**想复用音乐播放器 / 图片保存等现成能力**
参照 `skill_music_story.c` 直接 `#include "wukong_audio_player.h"` 调用现成 API。注意部分能力受 Kconfig 开关限制：`wukong_picture_output.h` 只在 `ENABLE_TUYA_PICTURE == 1` 时存在，`wukong_playback_ctrl.h` 只在 `ENABLE_TOOLKITS_PLAYBACK == 1` 时存在（`wukong_ai_skills.c` 顶部的 `#if defined(...)` 包裹写法可以照抄）。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
