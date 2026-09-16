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

#if defined(ENABLE_TUYA_UI) && (ENABLE_TUYA_UI == 1)
#include "tuya_board_config.h"   /* TUYA_LCD_IC_NAME / WIDTH / HEIGHT / ROTATION */
#include "tkl_pinmux.h"          /* tkl_io_pinmux_config */
#include "tuya_display_hw.h"
#include "ui_app.h"
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
#if defined(ENABLE_TUYA_UI) && (ENABLE_TUYA_UI == 1)
/* Display HW config for T5AI_BOARD_ROBOT — translated from the old
 * gui/display/tuya_ai_display.c `T5AI_BOARD_ROBOT` block (spi_st7789p3, 320x172).
 * The 0x22 GRAM y-offset is now carried by the lcd_spi_st7789p3 driver
 * (.y_offset), replacing the old tkl_lvgl_display_offset_set(0,0x22). No TP. */
static const ty_display_cfg s_lcd_gpio_cfg = {
    .spi_cfg = {
        .port = TUYA_SPI_NUM_0,
        .reset = {
            .pin = TUYA_GPIO_NUM_16,
            .active_level = TUYA_GPIO_LEVEL_LOW,
        },
        .bl = {
            .pin = TUYA_GPIO_NUM_14,
            .active_level = TUYA_GPIO_LEVEL_HIGH,
        },
        .power_ctrl = {
            .pin = TUYA_GPIO_NUM_19,
            .active_level = TUYA_GPIO_LEVEL_HIGH,
        },
        .rs = {
            .pin = TUYA_GPIO_NUM_47,
            .active_level = TUYA_GPIO_LEVEL_LOW,
        },
        .soft_cs = {
            .pin = TUYA_GPIO_NUM_MAX,
            .active_level = TUYA_GPIO_LEVEL_LOW,
        },
    },
};

static const tuya_display_hw_cfg_t s_display_cfg = {
    .lcd = {
        .lcd_name = TUYA_LCD_IC_NAME,    /* "spi_st7789p3" */
        .width = TUYA_LCD_WIDTH,         /* 320 */
        .height = TUYA_LCD_HEIGHT,       /* 172 */
        .rotation = TUYA_LCD_ROTATION,   /* 0 */
        .lcd_cfg = &s_lcd_gpio_cfg,
    },
    .tp = {                              /* ROBOT has no touch panel */
        .tp_device = NULL,
        .tp_cfg = NULL,
    },
};

const tuya_display_hw_cfg_t *tuya_board_get_display_cfg(void)
{
    return &s_display_cfg;
}

extern void app_ui_init(void);
extern void app_ui_msg_handler(TY_DISPLAY_MSG_T *msg);
#endif

OPERATE_RET tuya_device_board_init(VOID_T)
{
    OPERATE_RET rt = OPRT_OK;

    TAL_PR_NOTICE("ai toy -> init action");
    TUYA_CALL_ERR_LOG(tuya_robot_action_init());
    TUYA_CALL_ERR_LOG(app_gesture_init(ai_robot_gesture_cb));

#if defined(ENABLE_TUYA_UI) && (ENABLE_TUYA_UI == 1)
    /* Route SPI0 to pin group G2 (CLK 44 / CS 45 / MOSI 46 / MISO 47) before HW
     * init: the SPI HAL defaults to group G0 (SCK 14 / MOSI 16), which would
     * collide with the display's bl (14) and reset (16) GPIOs. */
    tkl_io_pinmux_config(TUYA_IO_PIN_45, TUYA_SPI0_CS);
    tkl_io_pinmux_config(TUYA_IO_PIN_44, TUYA_SPI0_CLK);
    tkl_io_pinmux_config(TUYA_IO_PIN_46, TUYA_SPI0_MOSI);
    tkl_io_pinmux_config(TUYA_IO_PIN_47, TUYA_SPI0_MISO);

    /* Display HW init收口至 board；注册 ROBOT 自有 UI（robot_app）。 */
    tuya_display_hw_init(tuya_board_get_display_cfg());
    ui_app_register_board_ui(app_ui_init, app_ui_msg_handler);
#endif

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
