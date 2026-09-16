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
#if defined(ENABLE_TUYA_CAMERA) && ENABLE_TUYA_CAMERA == 1
#include "tal_camera.h"
#endif

#if defined(ENABLE_TUYA_UI) && (ENABLE_TUYA_UI == 1)
#include "tuya_cloud_types.h"
#include "tuya_board_config.h"   /* TUYA_LCD_IC_NAME / WIDTH / HEIGHT / ROTATION */
#include "tkl_pinmux.h"          /* tkl_io_pinmux_config */
#include "tuya_display_hw.h"
#include "tuya_ai_display.h"
#include "ui_app.h"
#endif

/***********************************************************
************************macro define************************
***********************************************************/

#if defined(ENABLE_TUYA_UI) && (ENABLE_TUYA_UI == 1)
/* Display HW config for T5AI_BOARD_EVB_PRO — translated from the old
 * gui/display/tuya_ai_display.c `T5AI_BOARD_EVB_PRO` block (spi_st7789, 240x240).
 * The LCD is wired to SPI0 pin group G2 (CLK 44 / CS 45 / MOSI 46 / MISO 47),
 * so the pinmux must be set explicitly before tuya_display_hw_init (the SPI HAL
 * defaults to group G0). No touch panel. */
static const ty_display_cfg s_lcd_gpio_cfg = {
    .spi_cfg = {
        .port = TUYA_SPI_NUM_0,
        .reset = {
            .pin = TUYA_GPIO_NUM_18,
            .active_level = TUYA_GPIO_LEVEL_LOW,
        },
        .bl = {
            .pin = TUYA_GPIO_NUM_19,
            .active_level = TUYA_GPIO_LEVEL_HIGH,
        },
        .power_ctrl = {
            .pin = TUYA_GPIO_NUM_17,
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
        .lcd_name = TUYA_LCD_IC_NAME,    /* "spi_st7789" */
        .width = TUYA_LCD_WIDTH,         /* 240 */
        .height = TUYA_LCD_HEIGHT,       /* 240 */
        .rotation = TUYA_LCD_ROTATION,   /* 0 */
        .lcd_cfg = &s_lcd_gpio_cfg,
    },
    .tp = {                              /* EVB_PRO has no touch panel */
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

#if defined(ENABLE_TUYA_UI) && (ENABLE_TUYA_UI == 1)
    /* Route SPI0 to pin group G2 (the EVB_PRO LCD's wiring) before HW init. */
    tkl_io_pinmux_config(TUYA_IO_PIN_45, TUYA_SPI0_CS);
    tkl_io_pinmux_config(TUYA_IO_PIN_44, TUYA_SPI0_CLK);
    tkl_io_pinmux_config(TUYA_IO_PIN_46, TUYA_SPI0_MOSI);
    tkl_io_pinmux_config(TUYA_IO_PIN_47, TUYA_SPI0_MISO);

    /* Display HW init收口至 board；注册 EVB_PRO 自有 UI（xiaozhi_app）。 */
    tuya_display_hw_init(tuya_board_get_display_cfg());
    ui_app_register_board_ui(app_ui_init, app_ui_msg_handler);
#endif

    return rt;
}


#if defined(ENABLE_TUYA_CAMERA) && ENABLE_TUYA_CAMERA == 1
/**
 * @brief DVP (GC2145) camera configuration for T5AI_BOARD.
 *        dvp_frame_handle is intentionally NULL; tal_camera_init() fills it.
 */
OPERATE_RET tuya_board_get_camera_cfg(TAL_CAMERA_CFG_T *cfg)
{
    if (!cfg) {
        return OPRT_INVALID_PARM;
    }

    static TUYA_DVP_USR_CFG_T s_dvp_cfg = {
        .dvp_cfg = {
            .fps          = 20,
            .width        = TUYA_AI_TOY_ISP_WIDTH,
            .height       = TUYA_AI_TOY_ISP_HEIGHT,
            .output_mode  = TUYA_CAMERA_OUTPUT_JPEG_YUV422_BOTH,
            .sync_polarity = 0,
            .encoded_quality = {
                .jpeg_cfg = {
                    .enable   = TRUE,
                    .min_size = 10,
                    .max_size = 25,
                },
            },
        },
        .pin_cfg = {
            .dvp_i2c_idx               = TUYA_I2C_NUM_1,
            .dvp_i2c_clk.pin           = TUYA_GPIO_NUM_20,
            .dvp_i2c_sda.pin           = TUYA_GPIO_NUM_21,
            .dvp_rst_ctrl.pin          = TUYA_GPIO_NUM_43,
            .dvp_rst_ctrl.active_level = TUYA_GPIO_LEVEL_LOW,
            .dvp_pwr_ctrl.pin          = TUYA_GPIO_NUM_MAX,
        },

        .dvp_frame_handle = NULL,   /* overwritten by tal_camera_init() */
    };

    cfg->type = TAL_CAMERA_TYPE_DVP;
    cfg->cfg  = &s_dvp_cfg;
    return OPRT_OK;
}
#endif
