/**
 * @file tuya_device_board.c
 * @author www.tuya.com
 * @brief tuya_device_board module is used to
 * @version 0.1
 * @date 2022-10-28
 *
 * @copyright Copyright (c) tuya.inc 2022
 *
 */
#include "tuya_device_board.h"
#include "tuya_cloud_types.h"
#include "app_gesture.h"
#include "tuya_robot_actions.h"
#include "tuya_ai_display.h"
#include "tal_log.h"

#if defined(ENABLE_TUYA_CAMERA) && ENABLE_TUYA_CAMERA == 1
#include "tal_camera.h"
#endif

/***********************************************************
************************macro define************************
***********************************************************/

OPERATE_RET __emoji_show(uint8_t dir)
{
    STATIC CONST CHAR_T *emotion[] = {
        "neutral",
        "annoyed",
        "cool",
        "delicious",
        "fearful",
        "lovestruck",
        "loving",
        "unamused",
        "winking",
        "zany",
        /*----------------- */
        "crying",
        "angry",
        "confused",
        "disappointed",
        "embarrassed",
        "happy",
        "laughing",
        "relaxed",
        "sad",
        "surprise",
        "thinking",
    };
    STATIC UINT8_T index = 0;
    UINT8_T num = sizeof(emotion) / sizeof(emotion[0]);


    if (dir) {
        index = (index + 1) % num;
    } else {
        index = (0 == index) ? num - 1 : index - 1;
    }

    if (index > num) {
        return OPRT_NOT_EXIST;
    }

    return tuya_ai_display_msg(emotion[index], strlen(emotion[index]), TY_DISPLAY_TP_EMOJI);
}

 VOID ai_robot_gesture_cb(GESTURE_TYPE_E gesture)
 {
    //TAL_PR_NOTICE("ai_toy_gesture_cb gesture %d", gesture);
    if (gesture == GESTURE_LEFT) {
        __emoji_show(0);
    } else if (gesture == GESTURE_RIGHT) {
        __emoji_show(1);
    }
} 

/**
 * @brief evb board initialization
 *
 * @param[in] none
 *
 * @return OPRT_OK on success. Others on error, please refer to "tuya_error_code.h".
 */
OPERATE_RET tuya_device_board_init(VOID_T)
{
    OPERATE_RET rt = OPRT_OK;

    TAL_PR_NOTICE("ai toy -> init action");
    TUYA_CALL_ERR_LOG(tuya_robot_action_init());
    TUYA_CALL_ERR_LOG(app_gesture_init(ai_robot_gesture_cb));

    return rt;
}

#if defined(ENABLE_TUYA_CAMERA) && ENABLE_TUYA_CAMERA == 1
/**
 * @brief UVC camera configuration for T5AI_BOARD_ROBOT.
 *        frame_cb is intentionally NULL; tal_camera_init() fills it.
 */
OPERATE_RET tuya_board_get_camera_cfg(TAL_CAMERA_CFG_T *cfg)
{
    if (!cfg) {
        return OPRT_INVALID_PARM;
    }

    static TAL_UVC_CFG_T s_uvc_cfg = {
        .width        = TUYA_AI_TOY_ISP_WIDTH,
        .height       = TUYA_AI_TOY_ISP_HEIGHT,
        .output_mode  = TUYA_CAMERA_OUTPUT_JPEG,
        .fps          = TUYA_AI_TOY_ISP_FPS,
        .power_pin    = TUYA_AI_TOY_POWER_PIN,
        .active_level = TUYA_AI_TOY_ACTV_LEVEL,
        .frame_cb     = NULL,   /* overwritten by tal_camera_init() */
        .args         = NULL,
    };

    cfg->type = TAL_CAMERA_TYPE_UVC;
    cfg->cfg  = &s_uvc_cfg;
    return OPRT_OK;
}
#endif
