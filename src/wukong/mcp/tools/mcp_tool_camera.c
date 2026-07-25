/**
 * @file mcp_tool_camera.c
 * @brief MCP tool: device_camera_take_photo
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */

#include "mcp_tool_camera.h"
#include "wukong_ai_mcp.h"
#include "tal_log.h"
#include "tal_memory.h"
#include "tal_system.h"
#include "tuya_ai_toy_camera.h"
#if defined(ENABLE_TUYA_PICTURE) && (ENABLE_TUYA_PICTURE == 1)
#include "wukong_picture.h"
#endif
#ifdef ENABLE_TUYA_UI
#include "tuya_ai_display.h"
#endif

STATIC OPERATE_RET __take_photo(CONST CHAR_T *name, CONST ty_cJSON *args,
                                 ty_cJSON **out_content, BOOL_T *is_error,
                                 VOID *user_data)
{
    OPERATE_RET rt = OPRT_OK;
    BYTE_T *image_data = NULL;
    UINT_T image_size = 0;

    (VOID)name;
    (VOID)args;
    (VOID)user_data;

    TUYA_CALL_ERR_LOG(tuya_ai_toy_camera_snapshot(CAM_SNAP_JPEG, &image_data, &image_size, 3000));
    if (image_data == NULL || image_size == 0) {
        TAL_PR_ERR("snapshot failed");
        *is_error = TRUE;
        return OPRT_COM_ERROR;
    }

    *out_content = ty_cJSON_CreateArray();
    if (*out_content)
        ty_cJSON_AddItemToArray(*out_content,
            mcp_content_make_image(MCP_MIME_JPEG, image_data, image_size));

    /* Save to album and display on screen */
#if defined(ENABLE_TUYA_PICTURE) && (ENABLE_TUYA_PICTURE == 1)
    char pic_name[WUKONG_PICTURE_NAME_MAX_LEN + 1] = {0};
    if (wukong_picture_save_to_album(image_data, image_size, pic_name) == OPRT_OK
        && pic_name[0] != '\0') {
#ifdef ENABLE_TUYA_UI
        tuya_ai_display_msg((UINT8_T *)pic_name, strlen(pic_name), TY_DISPLAY_TP_AI_IMAGE);
#endif
    }
#endif

    tal_free(image_data);
    return OPRT_OK;
}

OPERATE_RET mcp_tool_camera_init(VOID)
{
    return MCP_TOOL_ADD(
        "device_camera_take_photo",
        "Captures a NEW photo from the device camera and returns it as an image.\n"
        "ONLY call this tool when NO image has been provided in the current conversation "
        "and the user wants to capture a live photo "
        "(e.g., 'what is this', 'look at this', 'take a photo', 'help me read this').\n"
        "Do NOT call this tool if an image was already uploaded or attached in this conversation — "
        "analyze the existing image directly instead.\n"
        "Do NOT guess visual content when no image is available — call this tool first.",
        __take_photo, NULL
    );
}
