<!-- Auto-translated from ../../howto/add-dialog-mode.md. Do not edit manually. -->
# Adding or Removing a Dialogue Mode

## Goal

Add a new dialogue mode to the wukong demo (a chat sub-mode or a device mode), or strip an unneeded mode out of a product firmware — covering the whole journey from mode implementation and configuration registration to build verification. This guide uses adding a hypothetical chat sub-mode `AI_CHAT_SUB_MY_MODE` as the example (registering a device mode follows the same steps; differences are called out where they occur), and also covers the corresponding steps for removing an existing mode.

## Prerequisites

- You have built, flashed, and talked to any existing board by following the [Quick Start](../quickstart.md).
- You have read the "Overview" and "State Machine" sections of [`src/mode/README.md`](../../../src/mode/README.md) and understand the mode module's **two-level mode hierarchy**:
  - Level 1 **device mode** `AI_DEVICE_MODE_E` (`src/mode/wukong_ai_mode.h`): Chat / Translate / P2P / Record / Picture / Detection — determines the current business form.
  - Level 2 **chat sub-mode** `AI_CHAT_SUB_MODE_E` (same header): Hold / One-shot Key / Wakeup / Free — only effective when the device mode is "Chat"; determines how a voice dialogue is triggered.
- Decide which level your new mode belongs to first: if it only changes the voice-trigger method (whether to hold, whether to require a wake word) → chat sub-mode; if it introduces a whole new business form for the device (e.g. a new cloud scenario or a new uplink data path) → device mode.

## Steps

### 1. Add a mode switch (Kconfig)

Edit `src/mode/Kconfig` and add a new entry inside the `menu "Enabled AI Mode"` block (where options like `ENABLE_AI_MODE_HOLD` live):

```kconfig
        config ENABLE_AI_MODE_MY_MODE
            bool "Chat-MyMode (one-line description of the trigger method)"
            default n
```

If the new mode also needs to be selectable as the "default mode on power-up," add a corresponding `config AI_CHAT_DEFAULT_MY_MODE` (`depends on ENABLE_AI_MODE_MY_MODE`) to the same file's `choice "Default AI Mode"`, and append a line to `config TUYA_AI_CHAT_DEFAULT_MODE`'s `default N if AI_CHAT_DEFAULT_XXX` list (`N` takes the next unused index — chat sub-modes occupy 0–3, device modes occupy 4–8; follow the 9 existing lines in `src/mode/Kconfig`).

### 2. Implement the mode source file

Create `src/mode/wukong_ai_mode_my_mode.c`, with its entire contents wrapped in `#if defined(ENABLE_AI_MODE_MY_MODE) && (ENABLE_AI_MODE_MY_MODE == 1)` — every mode file in the repo (hold/oneshot/wakeup/free/translate/p2p/record/picture/detection) follows this pattern, so that when the switch is off, the whole `.c` file compiles down to an empty translation unit and produces no code. Skeleton (copied from the real structure of `src/mode/wukong_ai_mode_oneshot.c`, with function names/callback signatures kept consistent — only `oneshot` is swapped for `my_mode`):

```c
/**
 * @file wukong_ai_mode_my_mode.c
 * @brief MyMode implementation.
 */
#include "tuya_cloud_types.h"
#include "wukong_ai_mode.h"
#include "wukong_kws.h"
#include "tuya_ai_toy.h"

#if defined(ENABLE_AI_MODE_MY_MODE) && (ENABLE_AI_MODE_MY_MODE == 1)

STATIC AI_CHAT_MODE_HANDLE_T s_ai_my_mode_cb = {0};
STATIC AI_CHAT_MODE_PARAM_T  s_ai_my_mode = {0};
STATIC AI_CHAT_STATE_E       s_ai_cur_state = AI_CHAT_INVALID;

STATIC OPERATE_RET wukong_ai_my_mode_int_cb(VOID *data, INT_T len)
{
    /* Set the VAD mode (WUKONG_AUDIO_VAD_MANUAL manual / WUKONG_AUDIO_VAD_AUTO automatic) */
    wukong_audio_input_wakeup_mode_set(WUKONG_AUDIO_VAD_AUTO);
    wukong_kws_enable();   /* modes that don't need a wake word should use wukong_kws_disable() */

    CHAT_SUB_STATE_CHANGE(AI_CHAT_SUB_MY_MODE, s_ai_my_mode.state, AI_CHAT_IDLE);
    s_ai_my_mode.wakeup_stat = FALSE;
    return OPRT_OK;
}

STATIC OPERATE_RET wukong_ai_my_mode_deint_cb(VOID *data, INT_T len)
{
    wukong_ai_agent_input_stop();
    s_ai_cur_state = AI_CHAT_INVALID;
    return OPRT_OK;
}

STATIC OPERATE_RET wukong_ai_my_mode_task_cb(VOID *data, INT_T len)
{
    /* Drive the state machine off s_ai_my_mode.state; follow wukong_ai_oneshot_task_cb's implementation */
    return OPRT_OK;
}

STATIC OPERATE_RET wukong_ai_my_mode_event_cb(VOID *data, INT_T len)
{
    TUYA_CHECK_NULL_RETURN(data, OPRT_INVALID_PARM);
    WUKONG_AI_EVENT_T *event = (WUKONG_AI_EVENT_T *)data;

    switch (event->type) {
    case WUKONG_AI_EVENT_ASR_OK:
        CHAT_SUB_STATE_CHANGE(AI_CHAT_SUB_MY_MODE, s_ai_my_mode.state, AI_CHAT_THINK);
        break;
    case WUKONG_AI_EVENT_TTS_PRE:
        CHAT_SUB_STATE_CHANGE(AI_CHAT_SUB_MY_MODE, s_ai_my_mode.state, AI_CHAT_SPEAK);
        break;
    default:
        break;
    }
    return OPRT_OK;
}

STATIC OPERATE_RET wukong_ai_my_mode_key_cb(VOID *data, INT_T len)
{
    /* Physical key event: PUSH_KEY_TYPE_E; follow wukong_ai_oneshot_key_cb's trigger logic */
    return OPRT_OK;
}

/* Unimplemented callbacks should stay NULL: the dispatcher prints
 * "does not support: xxx" and returns OPRT_NOT_FOUND, which is a safe
 * default (see the MODE_DISPATCH_CALL macro in src/mode/wukong_ai_mode.c) —
 * you don't need to implement every callback. */

OPERATE_RET ai_my_mode_register(AI_CHAT_MODE_HANDLE_T **cb)
{
    s_ai_my_mode_cb.on_init  = wukong_ai_my_mode_int_cb;
    s_ai_my_mode_cb.on_deinit = wukong_ai_my_mode_deint_cb;
    s_ai_my_mode_cb.on_key   = wukong_ai_my_mode_key_cb;
    s_ai_my_mode_cb.on_task  = wukong_ai_my_mode_task_cb;
    s_ai_my_mode_cb.on_event = wukong_ai_my_mode_event_cb;
    /* Add on_wakeup / on_vad / on_client / on_notify_idle / on_audio_input /
     * on_picture / on_interrupt as needed — signatures are in
     * AI_CHAT_MODE_HANDLE_T in src/mode/wukong_ai_mode.h */
    *cb = &s_ai_my_mode_cb;
    return OPRT_OK;
}

#endif /* ENABLE_AI_MODE_MY_MODE */
```

A device mode is written exactly the same way, except the state-change macro becomes `DEVICE_MODE_STATE_CHANGE(AI_DEVICE_MODE_MY_MODE, ...)`; the registration function is conventionally named `ai_my_mode_register` (a convention, not a hard requirement).

No need to touch `local.mk`: the app root's `local.mk` already has `LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/mode -name "*.c" ...)`, which globs the entire `src/mode/` directory — the new file is picked up automatically.

### 3. Register it with the mode manager

Edit `src/mode/wukong_ai_mode.h`:

- Add a new enum value in `AI_CHAT_SUB_MODE_E` (chat sub-modes) or `AI_DEVICE_MODE_E` (device modes), right before the corresponding `_MAX`, e.g. `AI_CHAT_SUB_MY_MODE`.

Edit `src/mode/wukong_ai_mode.c`:

1. In the `extern` declaration block at the top of the file (lines 18–26, where `extern OPERATE_RET ai_hold_register(...)` etc. live), add a line: `extern OPERATE_RET ai_my_mode_register(AI_CHAT_MODE_HANDLE_T **cb);`.
2. In `_chat_sub_str[]` (lines 36–38) or `_device_mode_str[]` (lines 40–42), append a string in enum order, used for log printing.
3. If `ENABLE_TUYA_UI` is on, `s_chat_sub_display[]` / `s_device_mode_display[]` (lines 51–65) also need a matching Chinese display name appended, otherwise the UI notification will read out-of-bounds garbage data.
4. In `wukong_ai_mode_init()` (lines 511–545 are the registration blocks for each mode), add a new block gated by the new switch:

   ```c
   #if defined(ENABLE_AI_MODE_MY_MODE) && (ENABLE_AI_MODE_MY_MODE == 1)
       __register_chat_sub(AI_CHAT_SUB_MY_MODE, ai_my_mode_register);
   #endif
   ```

   For a device mode, use `__register_device_mode(AI_DEVICE_MODE_MY_MODE, ai_my_mode_register)` instead.

### 4. Enable it on a board / make it the default

`ENABLE_AI_MODE_XXX` defaults to `n` and must be explicitly turned on to be compiled into a given board's firmware. There are two paths:

- **One-off debugging on a single board**: `make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai` → `AI Mode Configuration` → `Enabled AI Mode`, check the new option; if it should also be the default mode, select it under `Default AI Mode` in the same menu. Saving automatically triggers `make app_config` (or run `make app_config APP_NAME=tuyaos_demo_wukong_ai` manually to regenerate `include/tuya_app_config.h`).
- **Baking it into a specific board for a product**: edit that board's preset config snapshot `build/appconfig/<board name>` (e.g. `build/appconfig/T5AI_BOARD`), change `# CONFIG_ENABLE_AI_MODE_MY_MODE is not set` to `CONFIG_ENABLE_AI_MODE_MY_MODE=y` (and also change `CONFIG_TUYA_AI_CHAT_DEFAULT_MODE=N` if it needs to be the default mode), then run `make app_config_choice APP_NAME=tuyaos_demo_wukong_ai` to select that board and `make app_config APP_NAME=tuyaos_demo_wukong_ai` to apply it. See the table in section 2.3 of [`build/README_CN.md`](../../../build/README_CN.md) for which modes each board currently enables.

> **Note**: after changing `APPconfig` / `Kconfig` / appconfig files you must rerun `make app_config`, otherwise the config header is not regenerated and your changes take no effect.

> If the new mode is a chat sub-mode and you want it switchable by voice / MCP command (e.g. "switch to free dialogue"), you also need to add a line to `s_mode_map[]` (the mode-name ↔ `AI_CHAT_SUB_MODE_E` mapping table) in `src/wukong/mcp/tools/mcp_tool_control.c`, otherwise the `get_mode`/`set_mode` MCP tools won't recognize the new mode.

### 5. Build

```bash
make app APP_NAME=tuyaos_demo_wukong_ai
```

## Verification

1. After flashing, watch the serial log: during `wukong_ai_mode_init` it should print `init chat sub-mode N (my_mode)` (chat sub-mode, from the init branch in `src/mode/wukong_ai_mode.c`) or `init device mode N (my_mode)` (device mode); switching modes should print `switch chat sub-mode to N (my_mode)` / `switch device mode to N (my_mode)`.
2. Walk through one trigger round-trip: watch the `mode %s state change from %s to %s` logs printed by `CHAT_SUB_STATE_CHANGE` / `DEVICE_MODE_STATE_CHANGE`, and confirm the transition follows `INIT → IDLE → LISTEN → UPLOAD → THINK → SPEAK → IDLE`.
3. If `ENABLE_TUYA_UI` is on: switching to the new mode should pop up a "Switched to XX" notification (produced by `__post_mode_switched_notify`); a full dialogue round should complete normally with TTS playback working.
4. If exposed to MCP via `s_mode_map[]`: call the `get_mode` tool and confirm it returns the new mode's name, and that `set_mode` can switch to it.
5. Power-cycle the device: confirm the mode correctly restores per `TUYA_AI_CHAT_DEFAULT_MODE` or the last-saved trigger mode (`wukong_ai_toy_trigger_mode_get/set` persists to KV — see `src/tuya_ai_toy.c`).

## Removing a Mode

Most scenarios **don't require deleting code** — just turn off the Kconfig switch:

- Single-board debugging: `make app_menuconfig`, turn off the corresponding `ENABLE_AI_MODE_XXX`, then `make app_config` to apply.
- Baked into a product: in that board's `build/appconfig/<board name>` snapshot, change `CONFIG_ENABLE_AI_MODE_XXX=y` to `# CONFIG_ENABLE_AI_MODE_XXX is not set`, then `make app_config_choice` + `make app_config`.

Why this works: every mode `.c` file has its entire contents wrapped in `#if defined(ENABLE_AI_MODE_XXX) && (ENABLE_AI_MODE_XXX == 1)` (the corresponding registration call in `wukong_ai_mode.c` is wrapped the same way), so turning off the switch compiles that file down to an empty translation unit and preprocesses the registration call out entirely — no runtime resources are consumed, and `local.mk` doesn't need to be touched.

If you're certain a given product line will never use a particular mode, you can go further and physically clean it up (not required): delete the corresponding `wukong_ai_mode_xxx.c`, remove it from the enum in `wukong_ai_mode.h`, and remove the matching lines from the `extern` declarations / registration calls / display-name arrays in `wukong_ai_mode.c`. Since deleting an enum value shifts all subsequent values down by one, make sure to double-check that `_chat_sub_str[]` / `_device_mode_str[]` / the UI display-name arrays still line up index-for-index.

If the removed mode happens to be a board's `TUYA_AI_CHAT_DEFAULT_MODE` or the user's last-saved trigger mode, `wukong_ai_mode_init()` automatically falls back to the next enabled chat sub-mode (it won't crash — see lines 552–572 of `src/mode/wukong_ai_mode.c`), but for a good experience remember to pick an actually-remaining mode as the new default — see section 3.11 of [`build/README_CN.md`](../../../build/README_CN.md).

## FAQ

**The new `ENABLE_AI_MODE_XXX` doesn't show up in `make app_menuconfig`**
The `config` block in `src/mode/Kconfig` has the wrong indentation/placement and isn't actually inside `menu "Enabled AI Mode"`; or you forgot to save and re-enter menuconfig.

**Changed Kconfig / the appconfig snapshot, but firmware behavior didn't change**
You didn't run `make app_config APP_NAME=tuyaos_demo_wukong_ai` to regenerate `include/tuya_app_config.h` — every `CONFIG_*` change must go through it to take effect. Same pitfall as when porting a new board.

**The new mode never activates; it keeps running the old mode**
Check two things: whether the `__register_chat_sub`/`__register_device_mode` call in `wukong_ai_mode_init()` is really wrapped by the matching `#if ENABLE_AI_MODE_XXX` and whether that macro is actually 1 (check whether `CONFIG_ENABLE_AI_MODE_XXX` is `1` in `include/tuya_app_config.h`); and whether `TUYA_AI_CHAT_DEFAULT_MODE` or the trigger mode saved in KV still points at the old mode.

**Switching mode returns `OPRT_NOT_SUPPORTED`**
The target mode's `enabled` flag isn't set — usually the Kconfig switch isn't turned on, or the enum value is out of the `AI_CHAT_SUB_MAX` / `AI_DEVICE_MODE_MAX` bounds.

**The mode name shows up garbled or as an empty string in logs**
`_chat_sub_str[]` / `_device_mode_str[]` (and, under `ENABLE_TUYA_UI`, `s_chat_sub_display[]` / `s_device_mode_display[]`) weren't extended to match the new enum value, so the array index reads uninitialized memory out of bounds.

**MCP's `set_mode`/`get_mode` don't recognize the new chat sub-mode**
Only relevant if you need voice/MCP control — add a line `{"my_mode_name", AI_CHAT_SUB_MY_MODE}` to `s_mode_map[]` in `src/wukong/mcp/tools/mcp_tool_control.c`.

**Added a device mode that needs the camera (e.g. modeled on P2P/Picture/Detection), and some boards fail to build or misbehave at runtime**
Confirm that board has `ENABLE_TUYA_CAMERA` turned on; P2P mode additionally needs `ENABLE_AI_MODE_P2P` turned on as a sub-item (see section 2.2 of [`build/README_CN.md`](../../../build/README_CN.md)).

## Support

If you encounter issues during development, you can post on the TuyaOS Developer Forum [Connected Device Section](https://www.tuyaos.com/viewforum.php?f=11) for help.
