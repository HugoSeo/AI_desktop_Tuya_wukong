<!-- Auto-translated from ../../howto/add-mcp-tool.md. Do not edit manually. -->
# Add a New MCP Tool

## Goal

Add a local MCP (Model Context Protocol) tool to the AI agent in the wukong demo, letting the LLM call it during conversation to control the device. This guide uses adding `device_night_light_set` (turn a night light on/off and set its brightness) as the example, demonstrating the recommended implementation pattern — **convert the MCP command into DPs and inject them into the existing DP processing pipeline** — covering the whole journey from implementing the tool module, registering it, and the build-time switch, to verification:

```
LLM decides to call the MCP tool
   │  handler(args)                          # your tool module
   ▼
build {"dps":{...}} → sf_send_gw_dev_cmd(DP_CMD_MCP)   # async enqueue, merges into the same pipeline as App/cloud-issued DPs
   ▼
__soc_dev_obj_dp_cmd_cb()                    # src/tuya_app_main.c, the existing DP callback
   ▼
business execution + DP status report        # same business logic shared with App control; panel state syncs automatically
```

The benefit: the device's business action is implemented once, in the DP callback — MCP (voice), App, and cloud control entrances behave identically by construction, and status reporting needs no extra code. Which dpids your MCP arguments map to is determined by your product's DP schema; the night-light dpids in this guide are examples only.

See [`src/wukong/toolkits/mcp/README.md`](../../../src/wukong/toolkits/mcp/README.md) for the MCP module's overall architecture, capability list, and existing tools; this guide only covers the specific task of "adding one tool" — for conceptual questions (framework layering, callback extension patterns, etc.) go back to that doc.

## Prerequisites

- You have built, flashed, and can hold a conversation by following the [Quick Start](../quickstart.md).
- You've read the "Architecture" and "Build-time Configuration" sections of [`src/wukong/toolkits/mcp/README.md`](../../../src/wukong/toolkits/mcp/README.md), and understand the difference between the two levels of switches: `MCP_ENABLE_TOOLS` (framework layer, on by default in `mcp_server.h`) and `ENABLE_TOOLKITS_*` (application layer, Kconfig-configured).
- Confirm the master switch is on: `CONFIG_ENABLE_TUYA_TOOLKITS=y` in preset configs like `build/appconfig/T5AI_BOARD` (already on for the default board; confirm it yourself for a new board or a stripped-down config).

## Steps

### 1. Create the tool module files

Add a new `.h`/`.c` pair under `src/wukong/mcp/tools/`; the header only declares one `init` function (copy the style of an existing tool such as `mcp_tool_motion.h`):

```c
// src/wukong/mcp/tools/mcp_tool_night_light.h
#ifndef __MCP_TOOL_NIGHT_LIGHT_H__
#define __MCP_TOOL_NIGHT_LIGHT_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

OPERATE_RET mcp_tool_night_light_init(VOID);

#ifdef __cplusplus
}
#endif

#endif /* __MCP_TOOL_NIGHT_LIGHT_H__ */
```

Implement the handler in the `.c` file and register it with `MCP_TOOL_ADD`. The handler signature is fixed as `MCP_TOOL_HANDLER_CB` (see `mcp_server_tools.h`):

```c
// src/wukong/mcp/tools/mcp_tool_night_light.c
#include "mcp_tool_night_light.h"
#include "wukong_ai_mcp.h"

#include "smart_frame.h"          /* sf_send_gw_dev_cmd / SF_GW_DEV_CMD_S */
#include "tuya_cloud_com_defs.h"  /* DP_CMD_EXT_APP */
#include "tal_log.h"

/* DP command type for the MCP source: the SDK reserves the application
 * extension range DP_CMD_EXT_APP = [100,110] in tuya_cloud_com_defs.h.
 * Pick a value inside that range — no SDK header change needed; if 100 is
 * already taken in your app, use the next value. */
#define DP_CMD_MCP  (DP_CMD_EXT_APP)

/* Example DP mapping — which dpids and types to map to is determined by
 * your product's DP schema */
#define DPID_NIGHT_LIGHT_SWITCH  "20"   /* bool:  night light switch */
#define DPID_NIGHT_LIGHT_BRIGHT  "22"   /* value: brightness */

STATIC ty_cJSON *__make_text_content(CONST CHAR_T *text)
{
    ty_cJSON *arr = ty_cJSON_CreateArray();
    if (arr) {
        ty_cJSON_AddItemToArray(arr, mcp_content_make_text(text));
    }
    return arr;
}

STATIC OPERATE_RET __set_night_light(CONST CHAR_T *name, CONST ty_cJSON *args,
                                      ty_cJSON **out_content, BOOL_T *is_error,
                                      VOID *user_data)
{
    ty_cJSON *j = NULL;

    (VOID)name;
    (VOID)user_data;

    j = args ? ty_cJSON_GetObjectItem(args, "enable") : NULL;
    if (j == NULL || !ty_cJSON_IsBool(j)) {
        *is_error = TRUE;
        *out_content = __make_text_content("Missing or invalid 'enable'");
        return OPRT_INVALID_PARM;
    }
    BOOL_T enable = ty_cJSON_IsTrue(j) ? TRUE : FALSE;

    /* 1. MCP arguments → DP payload. The format must be {"dps":{"<dpid>":<value>}},
     *    identical to the payload of App/cloud-issued DP commands; add a "devId"
     *    field to target a sub-device. */
    ty_cJSON *root = ty_cJSON_CreateObject();
    ty_cJSON *dps  = ty_cJSON_CreateObject();
    if (root == NULL || dps == NULL) {
        ty_cJSON_Delete(root);
        ty_cJSON_Delete(dps);
        *is_error = TRUE;
        *out_content = __make_text_content("Out of memory");
        return OPRT_MALLOC_FAILED;
    }
    ty_cJSON_AddBoolToObject(dps, DPID_NIGHT_LIGHT_SWITCH, enable);
    j = ty_cJSON_GetObjectItem(args, "brightness");
    if (j && ty_cJSON_IsNumber(j)) {
        ty_cJSON_AddNumberToObject(dps, DPID_NIGHT_LIGHT_BRIGHT, j->valueint);
    }
    ty_cJSON_AddItemToObject(root, "dps", dps);

    /* 2. Inject into the existing DP pipeline. sf_send_gw_dev_cmd is async:
     *    it copies the command struct, posts it to the high-priority work
     *    queue and returns — OPRT_OK only means "enqueued"; actual execution
     *    happens in __soc_dev_obj_dp_cmd_cb (src/tuya_app_main.c).
     *    JSON ownership: on success the framework frees it when processing
     *    completes — the caller must not touch it again; only on enqueue
     *    failure does the caller free it. (Same pattern as the SDK
     *    svc_ai_agent's handling of DP_CMD_AI_SKILL.) */
    SF_GW_DEV_CMD_S gd_cmd = {DP_CMD_MCP, root};
    OPERATE_RET rt = sf_send_gw_dev_cmd(&gd_cmd);
    if (rt != OPRT_OK) {
        ty_cJSON_Delete(root);
        TAL_PR_ERR("night light send dp cmd failed: %d", rt);
        *is_error = TRUE;
        *out_content = __make_text_content("Failed to dispatch DP command");
        return rt;
    }

    *out_content = __make_text_content("OK");
    return OPRT_OK;
}

OPERATE_RET mcp_tool_night_light_init(VOID)
{
    return MCP_TOOL_ADD(
        "device_night_light_set",
        "Turn the night light on/off and set its brightness.",
        __set_night_light, NULL,
        MCP_SCHEMA_BOOL("enable", "true to turn on, false to turn off"),
        MCP_SCHEMA_INT_OPT_RANGE("brightness", "Brightness (0-100), only meaningful when enable=true", 0, 100)
    );
}
```

- On parameter validation failure (e.g. a missing required argument), set `*is_error` to `TRUE` and put an explanatory text in `out_content` — see how `__set_mode` does it in `mcp_tool_control.c`.
- `smart_frame.h` is an SDK `svc_dp` component header with existing usage precedents inside the app (`src/miscs/battery/tuya_ai_battery.c`, `src/wukong/audio/wukong_playback_ctrl.c`) — no extra include path needed.
- The actual DP business execution lives in the existing DP callback `__soc_dev_obj_dp_cmd_cb` (`src/tuya_app_main.c`) — if that dpid's handling is already implemented for App control, the MCP path adds **zero new business code**; to distinguish the command source in the callback, check the `cmd_tp` field of `TY_RECV_OBJ_DP_S`, which will be your `DP_CMD_MCP`.
- The available schema macros (`MCP_SCHEMA_INT` / `_OPT` / `_RANGE`, `STR`, `STR_ENUM`, `BOOL`, `NUM`, and their required/optional variants) are all in `mcp_server_tools.h`; `MCP_TOOL_ADD` automatically appends the `MCP_SCHEMA_END` terminator — use it directly, don't call the lower-level `mcp_server_tool_register` by hand.
- The tool description (second argument) should clearly state **under what scenario the LLM should call it**; follow the description style of existing tools (e.g. `device_camera_take_photo`'s "must be called first whenever the user asks to view, recognize, or describe visual content").

### 2. Register it in `wukong_ai_mcp.c`

Edit `src/wukong/mcp/wukong_ai_mcp.c`, adding the include and an init call guarded by `ENABLE_TOOLKITS_*`:

```c
#include "tools/mcp_tool_night_light.h"

// Inside wukong_ai_mcp_init(), add a block modeled on the other toolkits:
#if defined(ENABLE_TOOLKITS_NIGHT_LIGHT) && (ENABLE_TOOLKITS_NIGHT_LIGHT == 1)
    TUYA_CALL_ERR_LOG(mcp_tool_night_light_init());
#endif
```

### 3. Add a Kconfig option

Edit `src/wukong/mcp/Kconfig`, adding a new entry inside the `if ENABLE_TUYA_TOOLKITS` block (copy the format of an existing one like `ENABLE_TOOLKITS_MOTION`):

```kconfig
    config ENABLE_TOOLKITS_NIGHT_LIGHT
        bool "Night light toolkit"
        default n
        help
          AI can control the night light's on/off state and brightness
```

### 4. Add conditional compilation in `local.mk`

Edit `local.mk` at the app root, adding this inside the `ifeq ($(CONFIG_ENABLE_TUYA_TOOLKITS), y)` block (around line 193, alongside the other `mcp_tool_*` entries):

```makefile
ifeq ($(CONFIG_ENABLE_TOOLKITS_NIGHT_LIGHT), y)
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/mcp/tools/mcp_tool_night_light.c -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif
```

The include search path already covers `src/wukong/mcp` (`local.mk` has `LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/mcp`), so the tool module can `#include "wukong_ai_mcp.h"` directly, without adding an extra include path.

### 5. Turn on the config and regenerate the config header

```bash
cd apps/tuyaos_demo_wukong_ai
make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai
# In the top-level menu, find "Enable MCP toolkits" → check "Night light toolkit"
make app_config APP_NAME=tuyaos_demo_wukong_ai
```

> **Note**: after changing `Kconfig` / `local.mk` you must rerun `make app_config`, otherwise `include/tuya_app_config.h` doesn't update and `ENABLE_TOOLKITS_NIGHT_LIGHT` never takes effect.

### 6. Build

```bash
make app APP_NAME=tuyaos_demo_wukong_ai
```

## Verification

1. **At build time**: confirm `#define ENABLE_TOOLKITS_NIGHT_LIGHT 1` appears in `include/tuya_app_config.h`, `make app` compiles successfully, and `mcp_tool_night_light.c` is linked into the output (no undefined-symbol errors).
2. **Boot log**: after flashing, confirm via the serial log that the tool registered successfully; `mcp_server_tools.c` prints this on successful registration:
   ```
   Tool registered: device_night_light_set
   Description: Turn the night light on/off and set its brightness.
   Input schema: {...}
   ```
   as well as `MCP Server initialized successfully` from `wukong_ai_mcp.c`.
3. **At runtime**: have the AI trigger the tool via voice/conversation (e.g. "turn on the night light"), and follow the three log stages along the pipeline:
   - `mcp_server_tools.c` prints `mcp tool finished name=device_night_light_set is_error=0 eid=...` at the end of every tool call; `is_error=1` means the handler took the error branch — check the `out_content` text in the log to debug the arguments.
   - smart_frame prints `Rev Cmd 100, data:...` on receiving the injected command (`100` is the `DP_CMD_MCP` value; `components/svc_dp/src/smart_frame.c`) — if this line is missing, `sf_send_gw_dev_cmd` was never called or enqueue failed.
   - The existing DP callback `__soc_dev_obj_dp_cmd_cb` receives the dpid — reaching here means the command has merged into the standard DP pipeline; the rest is identical to an App-issued command.
4. **State closure**: after the DP callback executes and reports, the DP's state on the Tuya Smart App panel should update accordingly — direct evidence that "MCP and App share one pipeline" is working.
5. **Unit tests (optional)**: follow the declarative registration style in `src/wukong/mcp/tools/tests/test_suite.py` (the `_TESTS` list already has the `mcp`/`mcp_tools`/`mcp_control` groups); write a set of C tests for the new tool, add it to that list, then run:
   ```bash
   cd apps/tuyaos_demo_wukong_ai
   pytest src/wukong/mcp/tools/tests/test_suite.py -k night_light -v
   ```

## FAQ

**Changed Kconfig / local.mk but the tool has no effect**
You skipped `make app_config APP_NAME=tuyaos_demo_wukong_ai` — every `CONFIG_*` change must go through it to land in `include/tuya_app_config.h`, otherwise the `#if defined(ENABLE_TOOLKITS_XXX)` in `wukong_ai_mcp.c` can't take effect.

**Can't tell `MCP_ENABLE_TOOLS` and `ENABLE_TOOLKITS_XXX` apart**
The former is the framework-layer switch (defined in `mcp_server.h`, on by default, controls whether the whole Tools protocol capability is compiled in); the latter is the application-layer switch (Kconfig-generated, controls whether one specific tool module is compiled/initialized). Adding a tool usually only concerns the latter.

**Which one should I use, `MCP_TOOL_ADD` or `mcp_server_tool_register`**
Use the `MCP_TOOL_ADD` macro — it automatically appends the `MCP_SCHEMA_END` (i.e. `NULL`) terminator to the end of the variadic arguments. Calling the lower-level `mcp_server_tool_register` directly requires adding the terminator by hand, and it's easy to forget, causing variadic-argument parsing to run out of bounds.

**Forgot to set `out_content` in the handler, LLM side gets an empty response**
`out_content` is an output parameter; the framework provides no default. Even for a tool that "just logs something, no return content," it's recommended to put at least `mcp_content_make_text("OK")` in it, to avoid inconsistent client-side behavior when it parses an empty array.

**Parameter validation fails but no error is reported**
Whenever business logic determines the call failed (missing argument, invalid argument, underlying call failure, etc.), you must explicitly set `*is_error = TRUE`; without it, even a non-`OPRT_OK` return may still be treated as success on the LLM side.

**Tool description is too vague, the LLM won't call it proactively**
Follow the description style of each tool in the "Tool List" section of `src/wukong/toolkits/mcp/README.md`, and spell out the trigger scenario clearly in the description ("must be called when the user requests XXX", "call YYY to check first when the current state is uncertain", etc.) rather than just a one-line feature summary.

**Log says `dev null or no dps`**
The injected JSON lacks a `"dps"` object — the payload of `sf_send_gw_dev_cmd` must have the `{"dps":{...}}` structure (enforced by `__sf_handle_recv_dp`); putting dpid key-values at the top level does not work. The same error can also mean the device is not activated and the DP control structures aren't ready — confirm the device is paired and activated first.

**`sf_send_gw_dev_cmd` returns OK but the light doesn't move**
OK only means **enqueued**, not executed. Follow the three log stages in Verification step 3: `Rev Cmd 100` present but the DP callback never fires usually means a malformed JSON or a dpid outside the schema got filtered; callback fires but nothing happens — check that dpid's business branch inside the callback.

**Crash on JSON free, or memory leak**
Remember the ownership rule: once `sf_send_gw_dev_cmd` returns `OPRT_OK`, `cmd_js` is freed by the framework when async processing completes — calling `ty_cJSON_Delete` again is a double-free; on failure the framework does not take ownership and the caller must free it. Also note the handler's `args` belongs to the framework — never attach `args` sub-nodes directly into your `cmd_js` (use `ty_cJSON_Duplicate` if needed).

**What value should `DP_CMD_MCP` take**
Use the SDK's reserved application extension range `DP_CMD_EXT_APP` ([100,110], `tuya_cloud_com_defs.h`); don't occupy the existing values 0–9 and don't modify SDK headers. The SDK also defines `DP_CMD_AI_SKILL` (used by cloud AI-skill DeviceControl delivery) — the point of a separate MCP value is that the DP callback can distinguish "cloud skill issued" from "on-device MCP tool" via `cmd_tp`.

## Support

If you encounter issues during development, you can post on the TuyaOS Developer Forum [Connected Device Section](https://www.tuyaos.com/viewforum.php?f=11) for help.
