/**
 * @file tool_control.c
 * @brief MCP tools: device control — info, volume, mode
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */

#include "tool_control.h"
#include "wukong_tool.h"

#include <stdio.h>
#include <string.h>
#include "tal_log.h"
#include "tal_memory.h"
#include "tuya_ai_toy.h"
#include "wukong_ai_mode.h"
#include "wukong_audio_player.h"

#ifndef APP_BIN_NAME
#define APP_BIN_NAME "tuyaos_demo_wukong_ai"
#endif
#ifndef USER_SW_VER
#define USER_SW_VER "1.0.0"
#endif

/* ========================================================================== */
/*                       Tool: device_info_get                                */
/* ========================================================================== */

STATIC OPERATE_RET __get_device_info(CONST CHAR_T *name, CONST ty_cJSON *args,
                                      ty_cJSON **out_content,
                                      VOID *user_data)
{
    ty_cJSON *info;
    CHAR_T *info_str;

    (VOID)name;
    (VOID)args;
    (VOID)user_data;

    info = ty_cJSON_CreateObject();
    if (!info) {
        TAL_PR_ERR("Create JSON object failed");
        return OPRT_MALLOC_FAILED;
    }

    ty_cJSON_AddStringToObject(info, "model", APP_BIN_NAME);
    ty_cJSON_AddStringToObject(info, "serialNumber", "123456789");
    ty_cJSON_AddStringToObject(info, "firmwareVersion", USER_SW_VER);

    info_str = ty_cJSON_PrintUnformatted(info);
    ty_cJSON_Delete(info);
    if (!info_str)
        return OPRT_MALLOC_FAILED;

    *out_content = ty_cJSON_CreateArray();
    if (*out_content)
        ty_cJSON_AddItemToArray(*out_content, wukong_tool_make_text(info_str));
    ty_cJSON_FreeBuffer(info_str);

    return OPRT_OK;
}

/* ========================================================================== */
/*                       Tool: device_audio_volume_get                        */
/* ========================================================================== */

/**
 * @brief Query current device volume level
 * @param[in]  name       Tool name
 * @param[in]  args       JSON arguments (unused)
 * @param[out] out_content Result content array
 * @param[in]  user_data  Opaque pointer (unused)
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __get_volume(CONST CHAR_T *name, CONST ty_cJSON *args,
                                 ty_cJSON **out_content,
                                 VOID *user_data)
{
    UINT8_T volume = 0;
    CHAR_T buf[32] = {0};

    (VOID)name;
    (VOID)args;
    (VOID)user_data;

    wukong_audio_player_get_vol(&volume);
    snprintf(buf, sizeof(buf), "%d", (INT_T)volume);
    TAL_PR_DEBUG("get volume: %s", buf);

    *out_content = ty_cJSON_CreateArray();
    if (*out_content) {
        ty_cJSON_AddItemToArray(*out_content, wukong_tool_make_text(buf));
    }

    return OPRT_OK;
}

/* ========================================================================== */
/*                       Tool: device_audio_volume_set                        */
/* ========================================================================== */

STATIC OPERATE_RET __set_volume(CONST CHAR_T *name, CONST ty_cJSON *args,
                                 ty_cJSON **out_content,
                                 VOID *user_data)
{
    INT_T volume = 50;
    ty_cJSON *j;

    (VOID)name;
    (VOID)user_data;

    if (args) {
        j = ty_cJSON_GetObjectItem(args, "volume");
        if (j && ty_cJSON_IsNumber(j))
            volume = j->valueint;
    }

    if (volume < 0)
        volume = 0;
    else if (volume > 100)
        volume = 100;

    tuya_ai_toy_volume_set((UINT8_T)volume);
    TAL_PR_DEBUG("set volume to %d", volume);

    *out_content = ty_cJSON_CreateArray();
    if (*out_content)
        ty_cJSON_AddItemToArray(*out_content, wukong_tool_make_text("OK"));

    return OPRT_OK;
}

/* ========================================================================== */
/*                       Tool: device_audio_mode_set/get                      */
/* ========================================================================== */

/** String-to-enum mapping for chat sub-modes (MCP tool parameter). */
typedef struct {
    CONST CHAR_T          *name;
    AI_CHAT_SUB_MODE_E    mode;
} MCP_MODE_MAP_T;

STATIC CONST MCP_MODE_MAP_T s_mode_map[] = {
    {"hold_to_talk",      AI_CHAT_SUB_HOLD},
    {"press_to_talk",     AI_CHAT_SUB_ONESHOT},
    {"wake_word",         AI_CHAT_SUB_WAKEUP},
    {"free_conversation", AI_CHAT_SUB_FREE},
    {NULL, 0}
};

/**
 * @brief Look up a mode string in the mapping table.
 * @param[in]  name  Mode string (e.g. "wake_word").
 * @param[out] mode  Resolved enum value on success.
 * @return OPRT_OK on match, OPRT_INVALID_PARM if not found.
 */
STATIC OPERATE_RET __resolve_mode(CONST CHAR_T *name, AI_CHAT_SUB_MODE_E *mode)
{
    INT_T i;
    if (!name || !mode) {
        return OPRT_INVALID_PARM;
    }
    for (i = 0; s_mode_map[i].name != NULL; i++) {
        if (strcmp(name, s_mode_map[i].name) == 0) {
            *mode = s_mode_map[i].mode;
            return OPRT_OK;
        }
    }
    return OPRT_INVALID_PARM;
}

/**
 * @brief Look up a mode enum in the mapping table (reverse lookup).
 * @param[in]  mode  Mode enum value.
 * @return Mode string pointer, or "unknown" if not found.
 */
STATIC CONST CHAR_T *__mode_to_string(AI_CHAT_SUB_MODE_E mode)
{
    INT_T i;
    for (i = 0; s_mode_map[i].name != NULL; i++) {
        if (s_mode_map[i].mode == mode) {
            return s_mode_map[i].name;
        }
    }
    return "unknown";
}

STATIC OPERATE_RET __set_mode(CONST CHAR_T *name, CONST ty_cJSON *args,
                               ty_cJSON **out_content,
                               VOID *user_data)
{
    CONST CHAR_T *mode_str = NULL;
    AI_CHAT_SUB_MODE_E mode;
    ty_cJSON *j;
    OPERATE_RET rt;

    (VOID)name;
    (VOID)user_data;

    if (args) {
        j = ty_cJSON_GetObjectItem(args, "mode");
        if (j && ty_cJSON_IsString(j) && j->valuestring) {
            mode_str = j->valuestring;
        }
    }

    if (mode_str == NULL) {
        *out_content = ty_cJSON_CreateArray();
        if (*out_content) {
            ty_cJSON_AddItemToArray(*out_content,
                wukong_tool_make_text("Missing required parameter: mode"));
        }
        return OPRT_INVALID_PARM;
    }

    rt = __resolve_mode(mode_str, &mode);
    if (rt != OPRT_OK) {
        *out_content = ty_cJSON_CreateArray();
        if (*out_content) {
            ty_cJSON_AddItemToArray(*out_content,
                wukong_tool_make_text("Unknown mode"));
        }
        return OPRT_INVALID_PARM;
    }

    wukong_ai_chat_sub_mode_switch(mode);
    TAL_PR_DEBUG("set mode to %s (%d)", mode_str, (INT_T)mode);

    *out_content = ty_cJSON_CreateArray();
    if (*out_content)
        ty_cJSON_AddItemToArray(*out_content, wukong_tool_make_text("OK"));

    return OPRT_OK;
}

STATIC OPERATE_RET __get_mode(CONST CHAR_T *name, CONST ty_cJSON *args,
                               ty_cJSON **out_content,
                               VOID *user_data)
{
    AI_CHAT_SUB_MODE_E mode;
    CONST CHAR_T *mode_str;

    (VOID)name;
    (VOID)args;
    (VOID)user_data;

    mode = tuya_ai_toy_trigger_mode_get();
    mode_str = __mode_to_string(mode);
    TAL_PR_DEBUG("get mode: %s (%d)", mode_str, (INT_T)mode);

    *out_content = ty_cJSON_CreateArray();
    if (*out_content)
        ty_cJSON_AddItemToArray(*out_content, wukong_tool_make_text(mode_str));

    return OPRT_OK;
}

/* ========================================================================== */
/*                              Init                                          */
/* ========================================================================== */

OPERATE_RET tool_control_init(VOID)
{
    OPERATE_RET rt;

    rt = WUKONG_TOOL_ADD(
        "device_info_get",
        "Get device information such as model, serial number, and firmware version.",
        __get_device_info, NULL, WUKONG_TOOL_MCP | WUKONG_TOOL_AGENT
    );
    if (rt != OPRT_OK)
        return rt;

    rt = WUKONG_TOOL_ADD(
        "device_audio_volume_get",
        "Query the current device volume level (0-100).",
        __get_volume, NULL, WUKONG_TOOL_MCP | WUKONG_TOOL_AGENT
    );
    if (rt != OPRT_OK)
        return rt;

    rt = WUKONG_TOOL_ADD(
        "device_audio_volume_set",
        "Sets the device's volume level.\n"
        "If you don't know the current volume, call device_audio_volume_get first to query it.\n"
        "Returns OK if the volume was set successfully.",
        __set_volume, NULL, WUKONG_TOOL_MCP | WUKONG_TOOL_AGENT,
        TOOL_SCHEMA_INT_RANGE("volume", "The volume level to set (0-100).", 0, 100)
    );
    if (rt != OPRT_OK)
        return rt;

    rt = WUKONG_TOOL_ADD(
        "device_audio_mode_set",
        "Set the device's audio interaction mode. This controls how the device listens for and responds to voice input.\n"
        "If you don't know the current mode, call device_audio_mode_get first to query it.",
        __set_mode, NULL, WUKONG_TOOL_MCP | WUKONG_TOOL_AGENT,
        TOOL_SCHEMA_STR_ENUM("mode",
            "The desired interaction mode: hold_to_talk(长按说话), press_to_talk(按键说话), wake_word(唤醒词对话), free_conversation(自由对话)",
            "hold_to_talk|press_to_talk|wake_word|free_conversation")
    );
    if (rt != OPRT_OK)
        return rt;

    rt = WUKONG_TOOL_ADD(
        "device_audio_mode_get",
        "Get the device's current audio interaction mode. "
        "Call this before switching modes to avoid switching to the same mode.",
        __get_mode, NULL, WUKONG_TOOL_MCP | WUKONG_TOOL_AGENT
    );

    return rt;
}
