/**
 * @file tuya_device_board.c
 * @author www.tuya.com
 * @brief QEMU_M33_BOARD board initialization.
 *
 * Cut down from T5AI_BOARD/tuya_device_board.c for the QEMU (mps2-an521)
 * simulation board:
 *   - Display: same rgb_ili9488 tdd driver + T5AI lcd cfg values (the soft-SPI/
 *     GPIO init sequence runs harmlessly against the QEMU GPIO stubs; the real
 *     tal_display_rgb_open() path lands in the platform's virtual tkl_rgb_*).
 *   - Touch: tp_qemu_mouse_device (defined in tp_qemu_mouse_device.c) instead
 *     of the real gt1151 I2C touch controller.
 *   - Boot splash and camera keep the same shape as T5AI_BOARD (boot splash
 *     renders through the real display path now that ENABLE_RGB_DISPLAY is
 *     on; camera attempts DVP init and fails gracefully, same as today).
 *
 * This directory is QEMU-specific by construction (selected only when
 * CONFIG_WUKONG_BOARD_QEMU_M33=y) — no #ifdef PLATFORM_QEMU_M33 needed here.
 *
 * @copyright Copyright (c) tuya.inc 2026
 *
 */
#include "tuya_device_board.h"
#include "tuya_cloud_types.h"
#include "tuya_board_config.h"

#if defined(ENABLE_TUYA_CAMERA) && ENABLE_TUYA_CAMERA == 1
#include "tal_camera.h"
#endif

#if defined(ENABLE_TUYA_UI) && (ENABLE_TUYA_UI == 1)
#include "tuya_display_hw.h"
#include "tuya_boot_splash.h"

extern const uint8_t  g_tef_intro_320x240[];
extern const uint32_t g_tef_intro_320x240_len;

const tuya_display_hw_cfg_t *tuya_board_get_display_cfg(void);  /* defined below */
#endif

/***********************************************************
************************macro define************************
***********************************************************/

/**
 * @brief QEMU_M33_BOARD board initialization
 *
 * @param[in] none
 *
 * @return OPRT_OK on success. Others on error, please refer to "tuya_error_code.h".
 */
OPERATE_RET tuya_device_board_init(VOID_T)
{
    OPERATE_RET rt = OPRT_OK;
#if defined(ENABLE_TUYA_UI) && (ENABLE_TUYA_UI == 1)
    /* Same shape as T5AI_BOARD: no board UI registered, so ui_app_init() falls
     * through to the default full wukong page set. */
    const tuya_display_hw_cfg_t *disp_cfg = tuya_board_get_display_cfg();
    if (disp_cfg != NULL) {
        tuya_display_hw_init(disp_cfg);
        /* 与 T5AI_BOARD 行为一致：无条件播开机动画（TEF 单轮 ~3.6s），
         * ui_app_init() 的 tuya_boot_splash_stop() 等整轮播完后 LVGL 接管进主页。
         * 注意：开机到主页的耗时大头从来不是开机动画，而是无 AT 后端时网络/蓝牙栈
         * 对空 UART 的串行超时链（实测 ~40-80s）——run_display_sim.sh 默认拉起
         * esp_at_sim 后整链 ~1s。 */
        tuya_boot_splash_show_tef(g_tef_intro_320x240, g_tef_intro_320x240_len);   /* 失败仅记日志 */
    }
#endif
    return rt;
}

#if defined(ENABLE_TUYA_CAMERA) && ENABLE_TUYA_CAMERA == 1
/**
 * @brief DVP (GC2145) camera configuration for QEMU_M33_BOARD.
 *        Same pins as T5AI_BOARD; on QEMU there is no real DVP peripheral, so
 *        tal_camera_init() is expected to fail cleanly (graceful-fail shape,
 *        camera simulation is out of scope per the v2 spec).
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
            .dvp_i2c_clk.pin           = TUYA_GPIO_NUM_13,
            .dvp_i2c_sda.pin           = TUYA_GPIO_NUM_15,
            .dvp_rst_ctrl.pin          = TUYA_GPIO_NUM_51,
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

#if defined(ENABLE_TUYA_UI) && (ENABLE_TUYA_UI == 1)
static const ty_display_cfg s_lcd_gpio_cfg = {
    .rgb_cfg = {
        .spi_clk = TUYA_GPIO_NUM_49,
        .spi_csx = TUYA_GPIO_NUM_48,
        .spi_sda = TUYA_GPIO_NUM_50,
        .bl = {
            .pin = TUYA_GPIO_NUM_9,
            .active_level = TUYA_GPIO_LEVEL_HIGH,
        },
        .reset = {
            .pin = TUYA_GPIO_NUM_53,
        },
        .power_ctrl = {
            .pin = TUYA_GPIO_NUM_MAX,
        },
    },
};

/* No I2C involved for the virtual mouse touch device; these GPIO/I2C numbers
 * only drive tuya_tp_driver_init()'s generic pin/i2c bring-up sequence, which
 * runs harmlessly against the QEMU GPIO stubs (same rationale as the LCD's
 * soft-SPI sequence above). Reused from T5AI_BOARD for consistency. */
static const ty_tp_usr_cfg_t s_tp_usr_cfg = {
    .tp_i2c_clk = {
        .pin = TUYA_GPIO_NUM_13,
    },
    .tp_i2c_sda = {
        .pin = TUYA_GPIO_NUM_15,
    },
    .tp_rst = {
        .pin = TUYA_GPIO_NUM_54,
        .active_level = TUYA_GPIO_LEVEL_HIGH,
    },
    .tp_intr = {
        .pin = TUYA_GPIO_NUM_55,
    },
    .tp_pwr_ctrl = {
        .pin = TUYA_GPIO_NUM_MAX,
        .active_level = TUYA_GPIO_LEVEL_HIGH,
    },
    .tp_i2c_idx = TUYA_I2C_NUM_2,
};

extern const ty_tp_device_cfg_t tp_qemu_mouse_device;

static const tuya_display_hw_cfg_t s_display_cfg = {
    .lcd = {
        .lcd_name = "rgb_ili9488",
        .width = TUYA_LCD_WIDTH,
        .height = TUYA_LCD_HEIGHT,
        .rotation = TUYA_LCD_ROTATION,
        .lcd_cfg = &s_lcd_gpio_cfg,
    },
    .tp = {
        .tp_device = &tp_qemu_mouse_device,
        .tp_cfg = &s_tp_usr_cfg,
    },
};

const tuya_display_hw_cfg_t *tuya_board_get_display_cfg(void)
{
    return &s_display_cfg;
}
#endif
