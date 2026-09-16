<!-- Auto-translated from ../../howto/add-ai-skill.md. Do not edit manually. -->
# Add a New AI Skill

## Goal

Add a custom skill to the wukong demo, triggered by a cloud-pushed `SKILL` message (this guide uses adding `code="my_skill"` as the example), covering the whole journey from understanding the data flow, writing the parsing/handling code, and wiring it into the dispatch entry point, to verification.

See [`src/wukong/toolkits/fc/README.md`](../../../src/wukong/toolkits/fc/README.md) for the skills module's architecture and its existing skills (music/story, emotion, cloud events); this guide only covers "how to add a new one."

## Prerequisites

- You have built, flashed, and can hold a normal voice conversation by following the [Quick Start](../quickstart.md).
- The cloud / skill configuration platform side is already set up to push skill content with `bizType="SKILL"` and `data.code="my_skill"` — this is cloud-side configuration, out of scope for this repo; this guide covers only device-side code.
- You understand the role of `wukong/` from [Architecture](../architecture.md).

## Steps

### 1. Understand the data flow

The landing path of the cloud text stream (using the default tuya provider as an example):

```
Cloud SKILL message
   │
   ▼
__tuya_text_cb()                              # src/wukong/provider/tuya/wukong_provider_tuya.c:135
   │  wukong_ai_text_process(type, root, eof)
   ▼
wukong_ai_text_process()                      # src/wukong/skills/wukong_ai_skills.c:182
   │  type == AI_TEXT_SKILL
   ▼
__wukong_ai_skill_process()                   # src/wukong/skills/wukong_ai_skills.c:18
   │  Dispatches further by the "code" field in root
   ├─ code == "music" / "story"       → skill_music_story
   ├─ code == "PlayControl"           → skill_music_story / wukong_playback_ctrl
   └─ other                            → wukong_ai_event_notify(WUKONG_AI_EVENT_SKILL, root)
```

The `root` that `__wukong_ai_skill_process()` receives is exactly the message's `data` object (see the in-function comment `//! root is data:{}, parse code`) — a new skill can read fields directly off `root`, no need to peel off another `data` layer.

If you're using a non-default provider (`CONFIG_ENABLE_PROVIDER_JD` / `CONFIG_ENABLE_PROVIDER_CUBE`), the text stream instead goes through `src/wukong/provider/jd/wukong_provider_jd.c` or `src/wukong/provider/cube/wukong_provider_cube.c`, both of which call the same `wukong_ai_text_process()` — the rest of the steps are unchanged.

### 2. Create the skill source files

Create a new `.h`/`.c` pair under `src/wukong/skills/` (name it with the `skill_<name>` prefix convention):

```bash
cd apps/tuyaos_demo_wukong_ai/src/wukong/skills
```

`skill_my_skill.h`:

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

`skill_my_skill.c` (minimal skeleton; copy the field-parsing / error-handling style already used in `skill_emotion.c` / `wukong_ai_skills.c`):

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

    // TODO: trigger your business logic here (play audio, light an LED, switch UI state, etc.).
    // If you want the UI / dialogue-mode layer to be aware of this skill, use the generic event:
    // wukong_ai_event_notify(WUKONG_AI_EVENT_SKILL, json);

    return OPRT_OK;
}
```

No need to touch `local.mk` or Kconfig: `local.mk` glob-matches every `*.c` under the whole `src/wukong/skills/` directory (`local.mk:192`), and its include search path already covers the whole directory too (`local.mk:136`) — dropping the new file in is enough for it to be built automatically.

### 3. Wire it into the dispatch entry point

Edit `src/wukong/skills/wukong_ai_skills.c`: include the header, and add a branch to the `if/else if` chain in `__wukong_ai_skill_process()` (after the existing `music`/`story`/`PlayControl` checks, before the final `else`):

```c
#include "skill_my_skill.h"          // add at the top of the file, alongside the other skill_*.h includes
```

```c
    } else if (strcmp(code, "my_skill") == 0) {
        wukong_ai_parse_my_skill(root);
    } else {
        TAL_PR_NOTICE("skill %s not handled", code);
        wukong_ai_event_notify(WUKONG_AI_EVENT_SKILL, root);
    }
```

The `if/else if` chain matches first-come-first-served — once `code` is caught by an earlier branch it never reaches yours, so your new branch needs a `code` value that doesn't collide with `music`/`story`/`PlayControl` (and it must be agreed with the cloud / skill configuration platform side).

### 4. (Optional) Make the UI or a dialogue mode aware of this skill

If the skill only needs to trigger a local action on the device (play a sound effect, turn on a light, etc.), steps 2 and 3 are already enough. If you also need the UI or a dialogue mode's state machine to react, there are two approaches:

- **Reuse the generic event**: call `wukong_ai_event_notify(WUKONG_AI_EVENT_SKILL, root)` (this is exactly what the current "other code" branch already does). This event is delivered to the `case WUKONG_AI_EVENT_SKILL:` branch in each of all 6 dialogue-mode files (`src/mode/wukong_ai_mode_{hold,oneshot,wakeup,free,translate,picture}.c`) — currently all of these branches are empty stubs; fill in logic as needed in whichever mode file(s) must respond.
- **Add a dedicated event type**: if you don't want to disambiguate by `code` mixed in with other unhandled SKILL messages, append a new value to the `WUKONG_AI_EVENT_TYPE_E` enum in `src/wukong/wukong_ai_agent.h` (append it before `WUKONG_AI_EVENT_EXIT`, consistent with how this enum has been incrementally extended before), then have your skill handler call `wukong_ai_event_notify(your_new_event, root)` instead.

### 5. Build

```bash
make app APP_NAME=tuyaos_demo_wukong_ai
```

## Verification

1. The existing `TAL_PR_NOTICE("wukong text -> skill code: %s", ...)` in `wukong_ai_skills.c` (at the start of `__wukong_ai_skill_process`) prints the full SKILL JSON received — first confirm in the serial log that the `code` field really is the value you agreed on.
2. The `TAL_PR_NOTICE` you added in `wukong_ai_parse_my_skill()` should print after triggering the corresponding cloud skill once; if it doesn't print, your branch was never reached (recheck the `code` match in step 3, and whether the provider actually calls `wukong_ai_text_process`).
3. If you went through the `wukong_ai_event_notify(WUKONG_AI_EVENT_SKILL, root)` path, temporarily add a log line inside the corresponding mode file's `case WUKONG_AI_EVENT_SKILL:` to confirm the mode receives the event.
4. Verify the full chain end to end: with the device mid-conversation, trigger the cloud push for this skill, and confirm the device-side action (playback/display/reporting/etc.) matches expectations.

## FAQ

**Nothing prints on serial for `wukong text -> skill code`**
First confirm which provider is in use — when `CONFIG_ENABLE_PROVIDER_JD` / `CONFIG_ENABLE_PROVIDER_CUBE` is enabled, the text stream goes through `wukong_provider_jd.c` / `wukong_provider_cube.c` instead of the default `wukong_provider_tuya.c`; confirm the corresponding provider really calls `wukong_ai_text_process()` with type `AI_TEXT_SKILL`.

**`code` is correct in the log, but my branch isn't reached**
`__wukong_ai_skill_process()` is a sequential `if/else if` check; a `code` that collides with an existing branch (`music`/`story`/`PlayControl`) gets intercepted first. Pick a `code` value that isn't already taken, and align it with the cloud / skill configuration platform side.

**Can't parse the field I want out of `skillContent`**
The concrete structure of `skillContent` is defined by the cloud / skill configuration platform side; the device can only parse whatever field names were agreed on. See `wukong_ai_parse_music()` in `skill_music_story.c` for reference — it supports both the `general` and `custom` legacy JSON layouts at once; if your skill's JSON structure is also expected to evolve, keep a forward-compatible branch for it.

> **Note**: `skillContent` is the field name given in the message example in the header comment of `wukong_ai_skills.h`; for the actual JSON layout pushed by any specific cloud skill configuration, go by what the skill configuration platform actually sends.

**Want to reuse existing capabilities like the music player / picture saving**
Follow `skill_music_story.c` and directly `#include "wukong_audio_player.h"` to call the existing API. Note some capabilities are gated by Kconfig switches: `wukong_picture_output.h` only exists when `ENABLE_TUYA_PICTURE == 1`, and `wukong_playback_ctrl.h` only exists when `ENABLE_TOOLKITS_PLAYBACK == 1` (you can copy the `#if defined(...)` wrapping style used at the top of `wukong_ai_skills.c`).

## Support

If you encounter issues during development, you can post on the TuyaOS Developer Forum [Connected Device Section](https://www.tuyaos.com/viewforum.php?f=11) for help.
