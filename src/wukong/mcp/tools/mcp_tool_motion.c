/**
 * @file mcp_tool_motion.c
 * @brief MCP tool: device_motion_control_set
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */

#include "mcp_tool_motion.h"
#include "wukong_ai_mcp.h"

#include "tal_log.h"
#include "tuya_motion_ctrl.h"

STATIC OPERATE_RET __set_motion_control(CONST CHAR_T *name, CONST ty_cJSON *args,
                                         ty_cJSON **out_content, BOOL_T *is_error,
                                         VOID *user_data)
{
    UINT_T mode = 0, angle = 0;
    ty_cJSON *j;

    (VOID)name;
    (VOID)user_data;

    TAL_PR_DEBUG("__set_motion_control enter");

    if (args) {
        j = ty_cJSON_GetObjectItem(args, "motion_mode");
        if (j && ty_cJSON_IsNumber(j)) mode = (UINT_T)j->valueint;
        j = ty_cJSON_GetObjectItem(args, "rotate_value");
        if (j && ty_cJSON_IsNumber(j)) angle = (UINT_T)j->valueint;
    }

    BOOL_T ok = (tuya_motion_send_msg(mode, angle) == OPRT_OK);

    *out_content = ty_cJSON_CreateArray();
    if (*out_content)
        ty_cJSON_AddItemToArray(*out_content,
            mcp_content_make_text(ok ? "true" : "false"));

    TAL_PR_DEBUG("__set_motion_control exit");
    return OPRT_OK;
}

OPERATE_RET mcp_tool_motion_init(VOID)
{
    return MCP_TOOL_ADD(
        "device_motion_control_set",
        "Motion rotation control, support left/right rotation, clockwise/counterclockwise rotation, "
        "point move, reset to zero and stop.\n"
        "Returns true if the motion control was set successfully.",
        __set_motion_control, NULL,
        MCP_SCHEMA_INT_RANGE("motion_mode",
            "Motion mode (0=left, 1=right, 2=appoint angle, 3=clockwise, 4=counterclockwise, 5=point move, 6=reset, 7=stop)",
            0, 7),
        MCP_SCHEMA_INT_RANGE("rotate_value",
            "Rotation value (0-3600, <=360=degree, >360=circle)", 0, 3600)
    );
}

