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

#if defined(ENABLE_TUYA_DISPLAY) && (ENABLE_TUYA_DISPLAY == 1)
#include "tuya_board_config.h"   /* TUYA_LCD_IC_NAME / WIDTH / HEIGHT / ROTATION */
#include "tuya_display_hw.h"
#include "tuya_ai_display.h"
#if defined(ENABLE_TUYA_UI) && (ENABLE_TUYA_UI == 1)
#include "ui_app.h"
#endif
#endif

/***********************************************************
************************macro define************************
***********************************************************/

#if defined(ENABLE_TUYA_DISPLAY) && (ENABLE_TUYA_DISPLAY == 1)
/* Dual-screen config for T5AI_BOARD_EYES — translated from the old
 * gui/display/tuya_ai_display.c `T5AI_BOARD_EYES` block: two spi_st7735s
 * 128x128 panels on SPI2 (left eye) and SPI3 (right eye). They are stacked into
 * one logical 128x256 display — panel 0 = top half (left eye), panel 1 = bottom
 * half (right eye); the render path slices frames per panel. The 0x20 GRAM
 * offset (old tkl_lvgl_display_offset_set(0,0x20)) is carried by the
 * spi_st7735s driver (.y_offset). SPI2/SPI3 use default pins (no pinmux,
 * matching the old block). No touch panel. */
static const ty_display_cfg s_lcd_gpio_cfg = {      /* left eye / top */
    .spi_cfg = {
        .port = TUYA_SPI_NUM_2,
        .reset = {
            .pin = TUYA_GPIO_NUM_6,
            .active_level = TUYA_GPIO_LEVEL_LOW,
        },
        .bl = {
            .pin = TUYA_GPIO_NUM_25,
            .active_level = TUYA_GPIO_LEVEL_HIGH,
        },
        .power_ctrl = {
            .pin = TUYA_GPIO_NUM_MAX,
        },
        .rs = {
            .pin = TUYA_GPIO_NUM_7,
            .active_level = TUYA_GPIO_LEVEL_LOW,
        },
        .soft_cs = {
            .pin = TUYA_GPIO_NUM_MAX,
            .active_level = TUYA_GPIO_LEVEL_LOW,
        },
    },
};

static const ty_display_cfg s_lcd_gpio_cfg2 = {     /* right eye / bottom */
    .spi_cfg = {
        .port = TUYA_SPI_NUM_3,
        .reset = {
            .pin = TUYA_GPIO_NUM_45,
            .active_level = TUYA_GPIO_LEVEL_LOW,
        },
        .bl = {
            .pin = TUYA_GPIO_NUM_25,
            .active_level = TUYA_GPIO_LEVEL_HIGH,
        },
        .power_ctrl = {
            .pin = TUYA_GPIO_NUM_MAX,
        },
        .rs = {
            .pin = TUYA_GPIO_NUM_5,
            .active_level = TUYA_GPIO_LEVEL_LOW,
        },
        .soft_cs = {
            .pin = TUYA_GPIO_NUM_MAX,
            .active_level = TUYA_GPIO_LEVEL_LOW,
        },
    },
};

static const tuya_display_hw_cfg_t s_display_cfg = {
    .lcd = {                             /* panel 0 — top / left eye */
        .lcd_name = TUYA_LCD_IC_NAME,    /* "spi_st7735s" */
        .width = TUYA_LCD_WIDTH,         /* 128 */
        .height = TUYA_LCD_HEIGHT,       /* 128 */
        .rotation = TUYA_LCD_ROTATION,   /* 0 */
        .lcd_cfg = &s_lcd_gpio_cfg,
    },
    .lcd2 = {                            /* panel 1 — bottom / right eye */
        .lcd_name = TUYA_LCD_IC_NAME,    /* "spi_st7735s" */
        .width = TUYA_LCD_WIDTH,         /* 128 */
        .height = TUYA_LCD_HEIGHT,       /* 128 -> logical display becomes 128x256 */
        .rotation = TUYA_LCD_ROTATION,
        .lcd_cfg = &s_lcd_gpio_cfg2,
    },
    .tp = {                              /* EYES has no touch panel */
        .tp_device = NULL,
        .tp_cfg = NULL,
    },
};

const tuya_display_hw_cfg_t *tuya_board_get_display_cfg(void)
{
    return &s_display_cfg;
}

#if defined(ENABLE_TUYA_UI) && (ENABLE_TUYA_UI == 1)
extern void app_ui_init(void);
extern void app_ui_msg_handler(TY_DISPLAY_MSG_T *msg);
#else
extern OPERATE_RET eyes_ui_direct_init(void);   /* eyes_app.c 无 UI 框架直连入口 */
#endif
#endif /* ENABLE_TUYA_DISPLAY */

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

#if defined(ENABLE_TUYA_DISPLAY) && (ENABLE_TUYA_DISPLAY == 1)
    /* Open both eye panels (128x256 logical). */
    tuya_display_hw_init(tuya_board_get_display_cfg());

#if defined(ENABLE_TUYA_UI) && (ENABLE_TUYA_UI == 1)
    /* UI 框架在:注册板级 UI,由 ui_app_init() 统一时机调用(含 display 总线 init、
     * 消息 shim、背光)。 */
    ui_app_register_board_ui(app_ui_init, app_ui_msg_handler);
#else
    /* 无 UI 框架:直接对接 —— display 总线 init + TEF 播放器 + 消息回调 + 背光,
     * 全部在 eyes_app.c 的直连入口完成。 */
    eyes_ui_direct_init();
#endif
#endif

    return rt;
}

/* EYES has no camera. The unified camera input source (video_input_camera.c)
 * asks the board for its config at init; answering NOT_SUPPORTED keeps the
 * video service in degraded mode. */
#if defined(ENABLE_TUYA_CAMERA) && (ENABLE_TUYA_CAMERA == 1)
#include "tal_camera.h"
OPERATE_RET tuya_board_get_camera_cfg(TAL_CAMERA_CFG_T *cfg)
{
    (VOID)cfg;
    return OPRT_NOT_SUPPORTED;
}
#endif
