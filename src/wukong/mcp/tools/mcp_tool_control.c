/**
 * @file mcp_tool_control.c
 * @brief MCP tools: device control — info, volume, mode
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */

#include "mcp_tool_control.h"
#include "wukong_ai_mcp.h"

#include "tal_log.h"
#include "tal_memory.h"
#include "tuya_iot_com_api.h"
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
                                      ty_cJSON **out_content, BOOL_T *is_error,
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
        ty_cJSON_AddItemToArray(*out_content, mcp_content_make_text(info_str));
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
 * @param[out] is_error   Error flag
 * @param[in]  user_data  Opaque pointer (unused)
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __get_volume(CONST CHAR_T *name, CONST ty_cJSON *args,
                                 ty_cJSON **out_content, BOOL_T *is_error,
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
        ty_cJSON_AddItemToArray(*out_content, mcp_content_make_text(buf));
    }

    return OPRT_OK;
}

/* ========================================================================== */
/*                       Tool: device_audio_volume_set                        */
/* ========================================================================== */

STATIC OPERATE_RET __report_volume(INT_T volume)
{
    CHAR_T *devid = tuya_iot_get_gw_id();
    TY_OBJ_DP_S dp = {
        .dpid = 3,
        .type = PROP_VALUE,
        .value.dp_value = volume,
    };
    return tuya_report_dp_async(devid, &dp, 1, NULL);
}

STATIC OPERATE_RET __set_volume(CONST CHAR_T *name, CONST ty_cJSON *args,
                                 ty_cJSON **out_content, BOOL_T *is_error,
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

    wukong_audio_player_set_vol(volume);
    __report_volume(volume);
    TAL_PR_DEBUG("set volume to %d", volume);

    *out_content = ty_cJSON_CreateArray();
    if (*out_content)
        ty_cJSON_AddItemToArray(*out_content, mcp_content_make_text("OK"));

    return OPRT_OK;
}

/* ========================================================================== */
/*                       Tool: device_audio_mode_set                          */
/* ========================================================================== */

STATIC OPERATE_RET __set_mode(CONST CHAR_T *name, CONST ty_cJSON *args,
                               ty_cJSON **out_content, BOOL_T *is_error,
                               VOID *user_data)
{
    INT_T mode;
    ty_cJSON *j;

    (VOID)name;
    (VOID)user_data;

    mode = tuya_ai_toy_trigger_mode_get();

    if (args) {
        j = ty_cJSON_GetObjectItem(args, "mode");
        if (j && ty_cJSON_IsNumber(j))
            mode = j->valueint;
    }

    wukong_ai_chat_sub_mode_switch((AI_CHAT_SUB_MODE_E)mode);
    TAL_PR_DEBUG("set mode to %d", mode);

    *out_content = ty_cJSON_CreateArray();
    if (*out_content)
        ty_cJSON_AddItemToArray(*out_content, mcp_content_make_text("OK"));

    return OPRT_OK;
}

/* ========================================================================== */
/*                              Init                                          */
/* ========================================================================== */

OPERATE_RET mcp_tool_control_init(VOID)
{
    OPERATE_RET rt;

    rt = MCP_TOOL_ADD(
        "device_info_get",
        "Get device information such as model, serial number, and firmware version.",
        __get_device_info, NULL
    );
    if (rt != OPRT_OK)
        return rt;

    rt = MCP_TOOL_ADD(
        "device_audio_volume_get",
        "Query the current device volume level (0-100).",
        __get_volume, NULL
    );
    if (rt != OPRT_OK)
        return rt;

    rt = MCP_TOOL_ADD(
        "device_audio_volume_set",
        "Sets the device's volume level.\n"
        "If you don't know the current volume, call device_audio_volume_get first to query it.\n"
        "Returns OK if the volume was set successfully.",
        __set_volume, NULL,
        MCP_SCHEMA_INT_RANGE("volume", "The volume level to set (0-100).", 0, 100)
    );
    if (rt != OPRT_OK)
        return rt;

    rt = MCP_TOOL_ADD(
        "device_audio_mode_set",
        "Set the device's audio interaction mode. This controls how the device listens for and responds to voice input.",
        __set_mode, NULL,
        MCP_SCHEMA_INT_RANGE("mode",
            "The desired interaction mode: 0=hold_to_talk, 1=press_to_talk, 2=wake_word, 3=free_conversation",
            0, 3)
    );

    return rt;
}
