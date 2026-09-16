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
#include "tuya_board_config.h"
#include "tal_log.h"
#include "tal_system.h"
#include <stdio.h>
#include <string.h>

#if defined(ENABLE_TUYA_CAMERA) && ENABLE_TUYA_CAMERA == 1
#include "tal_camera.h"
#endif

#if defined(ENABLE_TUYA_NFC) && (ENABLE_TUYA_NFC == 1)
#include "tuya_pn532_hsu.h"
#endif

#if defined(ENABLE_TUYA_UI) && (ENABLE_TUYA_UI == 1)
#include "tuya_display_hw.h"
#include "tuya_boot_splash.h"
#include "ui_app.h"

extern const uint8_t  g_tef_intro_320x240[];
extern const uint32_t g_tef_intro_320x240_len;

const tuya_display_hw_cfg_t *tuya_board_get_display_cfg(void);  /* defined below */
#endif

/***********************************************************
************************macro define************************
***********************************************************/

#if defined(ENABLE_TUYA_NFC) && (ENABLE_TUYA_NFC == 1)
#define NFC_CARD_UUID_MAX_LEN  32
#define NFC_READ_RETRY_MAX     3

STATIC BOOL_T __pn532_read_card_uuid(PN532_CARD_INFO_S *card_info,
                                     CHAR_T uuid[NFC_CARD_UUID_MAX_LEN + 1])
{
    PN532_CARD_INFO_S cur;
    UINT8_T data1[16] = {0};
    UINT8_T data2[16] = {0};
    OPERATE_RET rt;

    if (card_info == NULL || uuid == NULL) {
        return FALSE;
    }
    uuid[0] = '\0';

    if (card_info->type != PN532_CARD_TYPE_MIFARE_1K &&
        card_info->type != PN532_CARD_TYPE_MIFARE_4K) {
        TAL_PR_NOTICE("NFC UUID read skipped: unsupported card type=%d", card_info->type);
        return FALSE;
    }

    memcpy(&cur, card_info, sizeof(cur));
    tuya_pn532_manual_begin();
    tal_system_sleep(20);

    for (INT_T retry = 0; retry < NFC_READ_RETRY_MAX; retry++) {
        rt = tuya_pn532_read_block(&cur, 1, PN532_KEY_A, NULL, data1);
        if (rt != OPRT_OK) {
            rt = tuya_pn532_poll_card(&cur);
            if (rt == OPRT_OK) {
                tal_system_sleep(20);
            }
            continue;
        }

        rt = tuya_pn532_read_block(&cur, 2, PN532_KEY_A, NULL, data2);
        if (rt != OPRT_OK) {
            rt = tuya_pn532_poll_card(&cur);
            if (rt == OPRT_OK) {
                tal_system_sleep(20);
            }
            continue;
        }

        memcpy(uuid, data1, 16);
        memcpy(uuid + 16, data2, 16);
        uuid[NFC_CARD_UUID_MAX_LEN] = '\0';
        tuya_pn532_manual_end();
        return uuid[0] != '\0';
    }

    tuya_pn532_manual_end();
    TAL_PR_NOTICE("NFC UUID read failed");
    return FALSE;
}

/**
 * @brief PN532 card detection callback (invoked from polling thread)
 *
 * Logs detected card info. Extend this to trigger AI events, DP reports, etc.
 *
 * @param[in] card_info  Detected card information
 * @param[in] arg        User argument (unused)
 */
STATIC VOID __pn532_card_detect_cb(PN532_CARD_INFO_S *card_info, VOID *arg)
{
    if (card_info == NULL) {
        TAL_PR_NOTICE("NFC card removed");
        return;
    }

    /* Log UID as hex string */
    CHAR_T uid_hex[24] = {0};
    INT_T pos = 0;
    for (UINT8_T i = 0; i < card_info->uid_len && pos < (INT_T)sizeof(uid_hex) - 1; i++) {
        pos += snprintf(uid_hex + pos, sizeof(uid_hex) - pos, "%02X", card_info->uid[i]);
    }

    TAL_PR_NOTICE("NFC card detected: type=%d, SAK=0x%02X, UID=%s",
                  card_info->type, card_info->sak, uid_hex);

#if defined(ENABLE_TUYA_UI) && (ENABLE_TUYA_UI == 1)
    CHAR_T uuid[NFC_CARD_UUID_MAX_LEN + 1] = {0};
    (VOID)__pn532_read_card_uuid(card_info, uuid);
    ui_app_notify_nfc_card_detected(uuid);
#endif
}

OPERATE_RET tuya_device_board_nfc_init(void)
{
    /* PN532 NFC module over HSU (UART0: P10=GPIO_10=RX, P11=GPIO_11=TX).
     * tuya_pn532_init() is idempotent, so repeated switch-on requests are safe. */
    PN532_CFG_S pn532_cfg = {
        .uart_port      = TUYA_UART_NUM_0,
        .baudrate       = 115200,
        .rx_buf_size    = 512,
        .reset_pin      = 0xFF,            /* no HW reset pin */
        .irq_pin        = 0xFF,            /* no IRQ pin, use polling */
        .use_irq        = FALSE,
        .detect_cb      = __pn532_card_detect_cb,
        .cb_arg         = NULL,
        .poll_interval  = 0,               /* default 300ms */
    };

    OPERATE_RET rt = tuya_pn532_init(&pn532_cfg);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("PN532 init failed: %d", rt);
    }
    return rt;
}
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
    /* 显示硬件 init 收口至 board：从 tuya_app_main.c 移入。不注册 board UI →
     * ui_app_init() 走默认完整 wukong 路径。 */
    const tuya_display_hw_cfg_t *disp_cfg = tuya_board_get_display_cfg();
    if (disp_cfg != NULL) {
        /* tal_tp_open() 的 ~1s 阻塞已下沉到 tp 线程,tuya_display_hw_init_tp()
         * 立即返回,故无需再拆分 lcd/tp 来抢开机动画时间,直接合并 init 即可。
         * 动画在 lcd 就绪后立刻播放(异步,自有线程);tp 在 ui_app_init() 前
         * 完成驱动初始化,UI 取触摸不受影响。 */
        tuya_display_hw_init(disp_cfg);
        tuya_boot_splash_show_tef(g_tef_intro_320x240, g_tef_intro_320x240_len);   /* 失败仅记日志 */
    }
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
            /* 注意: gc2145 驱动没有 480x480 的 fps 档位表(dvp_gc2145_init 仅对
             * 1280x720/864x480/640x480/320x240 处理 fps), 此值当前被忽略,
             * 实际输出 ~17fps 由 480_480 基础表决定; 要降帧需先补传感器档位表 */
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

extern const ty_tp_device_cfg_t tp_gt1151_device;

static const tuya_display_hw_cfg_t s_display_cfg = {
    .lcd = {
        .lcd_name = "rgb_ili9488",
        .width = TUYA_LCD_WIDTH,
        .height = TUYA_LCD_HEIGHT,
        .rotation = TUYA_LCD_ROTATION,
        .lcd_cfg = &s_lcd_gpio_cfg,
    },
    .tp = {
        .tp_device = &tp_gt1151_device,
        .tp_cfg = &s_tp_usr_cfg,
    },
};

const tuya_display_hw_cfg_t *tuya_board_get_display_cfg(void)
{
    return &s_display_cfg;
}
#endif
