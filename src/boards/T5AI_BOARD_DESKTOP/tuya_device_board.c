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
#include "tal_log.h"
#include "tuya_key.h"
#include "tal_sw_timer.h"

/* 旧 desktop UI 隔离：原本 include 的 desk_event_handle.h 是旧 desktop UI 的"伞头文件"，
 * 会经 desk_home.h → desk_ui_res.h 拖入已退役的 gui_common.h（新框架下不存在该头）。
 * DESKTOP 改用标准 wukong UI，board 文件只需其中的非 UI 符号，故在此直接 include 具体头，
 * 与旧 desktop UI 彻底解耦。 */
#include "tkl_gpio.h"            /* TUYA_GPIO_BASE_CFG_T / tkl_gpio_init / tkl_gpio_write */
#include "tkl_pinmux.h"          /* tkl_io_pinmux_config / TUYA_IO_PIN_* / TUYA_SDIO_* 等 */
#include "tuya_ai_toy_key.h"     /* tuya_ai_toy_key_init */
#include "base_event.h"          /* ty_subscribe_event / SUBSCRIBE_TYPE_NORMAL */
#include "base_event_info.h"     /* EVENT_AI_CLIENT_RUN */
#include "ty_cJSON.h"            /* ty_cJSON / ty_cJSON_Delete */
#include "tuya_motion_ctrl.h"    /* tuya_motion_ctrl_init */

#if defined(ENABLE_TUYA_CAMERA) && ENABLE_TUYA_CAMERA == 1
#include "tal_camera.h"
#endif

#if defined(ENABLE_WIFI_SERVICE) && (ENABLE_WIFI_SERVICE == 1)
#include "tuya_cloud_wifi_defs.h"
#endif

#if defined(TUYA_AI_TOY_BATTERY_ENABLE) && (TUYA_AI_TOY_BATTERY_ENABLE == 1)
#include "tuya_ai_battery.h"
#endif

#if defined(ENABLE_TUYA_UI) && (ENABLE_TUYA_UI == 1)
#include "tuya_display_hw.h"     /* TUYA_LCD_* 宏经 tuya_device_board.h → tuya_board_config.h 提供 */
#endif

/***********************************************************
************************macro define************************
***********************************************************/
#define LONG_KEY_TIME                   400
#define SEQ_KEY_TIME                    200

/***********************************************************
************************function define************************
***********************************************************/
extern OPERATE_RET wukong_ai_agent_translate_list_language(ty_cJSON **result);
extern OPERATE_RET wukong_ai_agent_translate_update_language(CHAR_T *lang, CHAR_T *tts_lang);


STATIC VOID __on_ai_toy_desktop_device_off(UINT_T port, PUSH_KEY_TYPE_E type, INT_T cnt)
{
    static bool flag = true;
    switch (type) 
    {
        case LONG_KEY: 
        {
            TAL_PR_NOTICE("ai toy -> desktop device off");
            tkl_gpio_write(DEVICE_POWER_PIN, 0);
        }
        break;

        case NORMAL_KEY:
        {

        }
        break;

        case SEQ_KEY:
        {

        }
        break;

        case RELEASE_KEY:
        default:
            break;
    }
    return;    
}

STATIC OPERATE_RET __desktop_key_init(VOID)
{
    OPERATE_RET rt = OPRT_OK;

    // 上电瞬间锁住上电管脚电平
    TUYA_GPIO_BASE_CFG_T device_on;
    device_on.direct = TUYA_GPIO_OUTPUT,
    device_on.mode = TUYA_GPIO_PULLUP,
    device_on.level = TUYA_GPIO_LEVEL_HIGH,
    tkl_gpio_init(DEVICE_POWER_PIN, &device_on);
    tkl_gpio_write(DEVICE_POWER_PIN, 1);

    // 初始化关机按键 长按5s关机
    TUYA_CALL_ERR_RETURN(tuya_ai_toy_key_init(DEVICE_POWER_NET_KEY_PIN, TRUE, SEQ_KEY_TIME, 3*1000, __on_ai_toy_desktop_device_off));

    // TUYA_CALL_ERR_RETURN(tuya_imu_init(TUYA_I2C_NUM_0, TUYA_GPIO_NUM_22));
    return rt;
}

STATIC VOID __desktop_io_init(VOID)
{
    // SDIO
    tkl_io_pinmux_config(TUYA_IO_PIN_14, TUYA_SDIO_CLK);
    tkl_io_pinmux_config(TUYA_IO_PIN_15, TUYA_SDIO_CMD);
    tkl_io_pinmux_config(TUYA_IO_PIN_16, TUYA_SDIO_DATA0);
    tkl_io_pinmux_config(TUYA_IO_PIN_17, TUYA_SDIO_DATA1);
    tkl_io_pinmux_config(TUYA_IO_PIN_18, TUYA_SDIO_DATA2);
    tkl_io_pinmux_config(TUYA_IO_PIN_19, TUYA_SDIO_DATA3);

    // I2C0
    tkl_io_pinmux_config(TUYA_IO_PIN_20, TUYA_IIC0_SCL);
    tkl_io_pinmux_config(TUYA_IO_PIN_21, TUYA_IIC0_SDA);

    // SPI0
    tkl_io_pinmux_config(TUYA_IO_PIN_44, TUYA_SPI0_CLK);
    tkl_io_pinmux_config(TUYA_IO_PIN_45, TUYA_SPI0_CS);
    tkl_io_pinmux_config(TUYA_IO_PIN_46, TUYA_SPI0_MOSI);
    tkl_io_pinmux_config(TUYA_IO_PIN_47, TUYA_SPI0_MISO);   
    
    //UART2
    tkl_io_pinmux_config(TUYA_IO_PIN_40, TUYA_UART2_RX);
    tkl_io_pinmux_config(TUYA_IO_PIN_41, TUYA_UART2_TX);
}

STATIC VOID_T __desktop_translate_timer_cb(TIMER_ID timer_id, VOID_T *arg)
{
    TAL_PR_INFO("[%s] enter", __func__);

}

STATIC OPERATE_RET __desktop_network_status(void *data)
{
    TAL_PR_INFO("[%s] enter", __func__);
    ty_cJSON *result = NULL;
    wukong_ai_agent_translate_list_language(&result);
    if (result != NULL) {
        ty_cJSON_Delete(result);
        result = NULL;
    }
    return OPRT_OK;
}

/**
 * @brief evb board initialization
 *
 * @param[in] none
 *
 * @return OPRT_OK on success. Others on error, please refer to "tuya_error_code.h".
 */
#if defined(ENABLE_TUYA_UI) && (ENABLE_TUYA_UI == 1)
/* Display hardware config for T5AI_BOARD_DESKTOP — translated from the old
 * gui/display/tuya_ai_display.c `T5AI_BOARD_DESKTOP` block (spi_st7789v2 + cst816x).
 * SPI-interface LCD: fill ty_display_cfg.spi_cfg (vs T5AI_BOARD's .rgb_cfg). */
static const ty_display_cfg s_lcd_gpio_cfg = {
    .spi_cfg = {
        .port = TUYA_SPI_NUM_0,
        .reset = {
            .pin = TUYA_GPIO_NUM_43,
            .active_level = TUYA_GPIO_LEVEL_LOW,
        },
        .bl = {
            .pin = TUYA_GPIO_NUM_42,
            .active_level = TUYA_GPIO_LEVEL_HIGH,
        },
        .power_ctrl = {
            .pin = TUYA_GPIO_NUM_5,
            .active_level = TUYA_GPIO_LEVEL_HIGH,
        },
        .rs = {
            .pin = TUYA_GPIO_NUM_47,
            .active_level = TUYA_GPIO_LEVEL_LOW,
        },
        .soft_cs = {
            .pin = TUYA_GPIO_NUM_MAX,   /* 不使用必须配成无效 */
            .active_level = TUYA_GPIO_LEVEL_LOW,
        },
    },
};

static const ty_tp_usr_cfg_t s_tp_usr_cfg = {
    .tp_i2c_clk = {
        .pin = TUYA_GPIO_NUM_20,
    },
    .tp_i2c_sda = {
        .pin = TUYA_GPIO_NUM_21,
    },
    .tp_rst = {
        .pin = TUYA_GPIO_NUM_53,
        .active_level = TUYA_GPIO_LEVEL_LOW,
    },
    .tp_intr = {
        .pin = TUYA_GPIO_NUM_54,
    },
    .tp_pwr_ctrl = {
        .pin = TUYA_GPIO_NUM_MAX,
        .active_level = TUYA_GPIO_LEVEL_HIGH,
    },
    .tp_i2c_idx = TUYA_I2C_NUM_0,
};

extern const ty_tp_device_cfg_t tp_cst816x_device;

static const tuya_display_hw_cfg_t s_display_cfg = {
    .lcd = {
        .lcd_name = TUYA_LCD_IC_NAME,    /* "spi_st7789v2" */
        .width = TUYA_LCD_WIDTH,         /* 320 */
        .height = TUYA_LCD_HEIGHT,       /* 240 */
        .rotation = TUYA_LCD_ROTATION,   /* 0 */
        .lcd_cfg = &s_lcd_gpio_cfg,
    },
    .tp = {
        .tp_device = &tp_cst816x_device,
        .tp_cfg = &s_tp_usr_cfg,
    },
};

const tuya_display_hw_cfg_t *tuya_board_get_display_cfg(void)
{
    return &s_display_cfg;
}
#endif

OPERATE_RET tuya_device_board_init(VOID_T)
{
    OPERATE_RET rt = OPRT_OK;

    TAL_PR_NOTICE("ai toy -> init desktop");
    __desktop_io_init();
    __desktop_key_init();

#if defined(ENABLE_TUYA_UI) && (ENABLE_TUYA_UI == 1)
    /* 显示硬件 init 收口至 board（从 tuya_app_main.c 移入）。不注册 board UI →
     * ui_app_init() 走默认完整 wukong 路径，分辨率经 ui_adaptive 自适应。 */
    const tuya_display_hw_cfg_t *disp_cfg = tuya_board_get_display_cfg();
    if (disp_cfg != NULL) {
        tuya_display_hw_init(disp_cfg);
    }
#endif
    
    // motion
    TUYA_CALL_ERR_RETURN(tuya_motion_ctrl_init());

    tuya_ai_toy_charge_level_set(TUYA_GPIO_LEVEL_LOW);

    TUYA_CALL_ERR_LOG(ty_subscribe_event(EVENT_AI_CLIENT_RUN, "ai_desktop_board", __desktop_network_status, SUBSCRIBE_TYPE_NORMAL));

    /* 播放列表存储由新框架的 ui_svc_music 注册（s_music_store_ops），DESKTOP 不再注册
     * 旧 desktop UI 的 g_desktop_playlist_storage_ops。 */

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
            .fps          = TUYA_AI_TOY_ISP_FPS,
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
            .dvp_i2c_idx               = TUYA_I2C_NUM_0,
            .dvp_i2c_clk.pin           = TUYA_GPIO_NUM_20,
            .dvp_i2c_sda.pin           = TUYA_GPIO_NUM_21,
            .dvp_rst_ctrl.pin          = TUYA_GPIO_NUM_50,
            .dvp_rst_ctrl.active_level = TUYA_GPIO_LEVEL_LOW,
            .dvp_pwr_ctrl.pin          = TUYA_GPIO_NUM_49,
        },

        .dvp_frame_handle = NULL,   /* overwritten by tal_camera_init() */
    };

    cfg->type = TAL_CAMERA_TYPE_DVP;
    cfg->cfg  = &s_dvp_cfg;
    return OPRT_OK;
}
#endif
