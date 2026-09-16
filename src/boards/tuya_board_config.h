/**
 * @file tuya_board_config.h
 * @brief Unified board hardware configuration translated from Kconfig (menuconfig).
 *
 * All configurable hardware parameters (GPIO pins, display, camera, encoder,
 * battery, UART codec, etc.) are defined here based on the values generated
 * by the build system into tuya_app_config.h.
 *
 * Individual board headers (tuya_device_board.h) should include this file
 * instead of defining these macros themselves.
 */

#ifndef __TUYA_BOARD_CONFIG_H__
#define __TUYA_BOARD_CONFIG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"
#include "tuya_app_config.h"

/* ----------------------------------------------------------------
 * GPIO helper: Kconfig stores pin numbers as plain integers.
 * TUYA_GPIO_NUM_E is a contiguous enum starting at 0, with
 * TUYA_GPIO_NUM_MAX (64) meaning "disabled / not connected".
 * ---------------------------------------------------------------- */
#define _BOARD_GPIO(n)  ((TUYA_GPIO_NUM_E)(n))

/* ================================================================
 * Pin Configuration
 * ================================================================ */
#define TUYA_AI_TOY_AUDIO_TRIGGER_PIN   _BOARD_GPIO(TUYA_AI_TOY_AUDIO_TRIGGER_PIN_NUM)
#define TUYA_AI_TOY_SPK_EN_PIN          _BOARD_GPIO(TUYA_AI_TOY_SPK_EN_PIN_NUM)
#define TUYA_AI_TOY_LED_PIN             _BOARD_GPIO(TUYA_AI_TOY_LED_PIN_NUM)
#define TUYA_AI_TOY_NET_PIN             _BOARD_GPIO(TUYA_AI_TOY_NET_PIN_NUM)

/* ================================================================
 * Display Configuration
 * ================================================================ */
#define TUYA_LCD_IC_NAME    TUYA_LCD_IC_NAME_STR
#define TUYA_LCD_WIDTH      TUYA_LCD_WIDTH_VAL
#define TUYA_LCD_HEIGHT     TUYA_LCD_HEIGHT_VAL
#define LCD_FPS             LCD_FPS_VAL
#define TUYA_LCD_ROTATION   TUYA_LCD_ROTATION_VAL

/* ================================================================
 * Camera Configuration
 * ================================================================ */
#if defined(ENABLE_TUYA_CAMERA) && (ENABLE_TUYA_CAMERA == 1)

#if defined(CAMERA_TYPE_UVC) && (CAMERA_TYPE_UVC == 1)
#define TUYA_AI_TOY_CAMERA_TYPE     TKL_VI_CAMERA_TYPE_UVC
#else
#define TUYA_AI_TOY_CAMERA_TYPE     TKL_VI_CAMERA_TYPE_DVP
#endif

#define TUYA_AI_TOY_CAMERA_FMT      TKL_CODEC_VIDEO_MJPEG
#define TUYA_AI_TOY_ACTV_LEVEL      TUYA_GPIO_LEVEL_HIGH
#define TUYA_AI_TOY_ISP_WIDTH       TUYA_AI_TOY_ISP_WIDTH_VAL
#define TUYA_AI_TOY_ISP_HEIGHT      TUYA_AI_TOY_ISP_HEIGHT_VAL
#define TUYA_AI_TOY_ISP_FPS         TUYA_AI_TOY_ISP_FPS_VAL
#define TUYA_AI_TOY_POWER_PIN       _BOARD_GPIO(TUYA_AI_TOY_POWER_PIN_NUM)
#define TUYA_AI_TOY_I2C_CLK         _BOARD_GPIO(TUYA_AI_TOY_I2C_CLK_PIN_NUM)
#define TUYA_AI_TOY_I2C_SDA         _BOARD_GPIO(TUYA_AI_TOY_I2C_SDA_PIN_NUM)

#endif /* ENABLE_TUYA_CAMERA */

/* ================================================================
 * Encoder Configuration
 * ================================================================ */
#if defined(ENABLE_APP_OPUS_ENCODER) && (ENABLE_APP_OPUS_ENCODER == 1)
#define ENABLE_APP_OPUS_ENCODER_VAL     1
#else
#define ENABLE_APP_OPUS_ENCODER_VAL     0
#endif
/* Re-define as integer macros that existing code expects (0 / 1) */
#undef  ENABLE_APP_OPUS_ENCODER
#define ENABLE_APP_OPUS_ENCODER         ENABLE_APP_OPUS_ENCODER_VAL

#if defined(ENABLE_APP_SPEEX_ENCODER) && (ENABLE_APP_SPEEX_ENCODER == 1)
#define ENABLE_APP_SPEEX_ENCODER_VAL    1
#else
#define ENABLE_APP_SPEEX_ENCODER_VAL    0
#endif
#undef  ENABLE_APP_SPEEX_ENCODER
#define ENABLE_APP_SPEEX_ENCODER        ENABLE_APP_SPEEX_ENCODER_VAL

/* ================================================================
 * Battery Configuration
 * ================================================================ */
#if defined(ENABLE_BATTERY) && (ENABLE_BATTERY == 1)
#define TUYA_AI_TOY_BATTERY_ENABLE      1
#define TUYA_AI_TOY_CHARGE_PIN          _BOARD_GPIO(TUYA_AI_TOY_CHARGE_PIN_NUM)
#define TUYA_AI_TOY_BATTERY_CAP_PIN     _BOARD_GPIO(TUYA_AI_TOY_BATTERY_CAP_PIN_NUM)
#else
#define TUYA_AI_TOY_BATTERY_ENABLE      0
#endif

/* ================================================================
 * UART Codec Configuration
 * ================================================================ */
#if defined(USING_UART_AUDIO_INPUT) && (USING_UART_AUDIO_INPUT == 1)

#if defined(UART_CODEC_VENDOR_GX8006) && (UART_CODEC_VENDOR_GX8006 == 1)
#define UART_CODEC_VENDOR_ID            0   /* GX8006 */
#define UART_CODEC_DOWNLOAD_FORMAT      0   /* PCM */
#else
#define UART_CODEC_VENDOR_ID            1   /* CI1302 */
#define UART_CODEC_DOWNLOAD_FORMAT      3   /* MP3 */
#endif

#if (UART_CODEC_UART_PORT_NUM == 0)
#define UART_CODEC_UART_PORT            TUYA_UART_NUM_0
#elif (UART_CODEC_UART_PORT_NUM == 1)
#define UART_CODEC_UART_PORT            TUYA_UART_NUM_1
#else
#define UART_CODEC_UART_PORT            TUYA_UART_NUM_2
#endif

#define UART_CODEC_BOOT_IO              _BOARD_GPIO(UART_CODEC_BOOT_IO_NUM)
#define UART_CODEC_POWER_IO             _BOARD_GPIO(UART_CODEC_POWER_IO_NUM)
#define UART_CODEC_SPK_FLOWCTL_IO       _BOARD_GPIO(UART_CODEC_SPK_FLOWCTL_IO_NUM)

#if defined(UART_CODEC_SPK_FLOWCTL_ACTIVE_HIGH) && (UART_CODEC_SPK_FLOWCTL_ACTIVE_HIGH == 1)
#define UART_CODEC_SPK_FLOWCTL_IO_LEVEL TUYA_GPIO_LEVEL_HIGH
#else
#define UART_CODEC_SPK_FLOWCTL_IO_LEVEL TUYA_GPIO_LEVEL_LOW
#endif

#define UART_CODEC_MUTE_IO_LEVEL        TUYA_GPIO_LEVEL_LOW

#if defined(UART_CODEC_FMT_OPUS) && (UART_CODEC_FMT_OPUS == 1)
#define UART_CODEC_UPLOAD_FORMAT        2   /* OPUS */
#else
#define UART_CODEC_UPLOAD_FORMAT        1   /* SPEEX */
#endif

#endif /* USING_UART_AUDIO_INPUT */

/* ================================================================
 * Board-specific Features
 * ================================================================ */

/* Device power control */
#if defined(DEVICE_POWER_CONTROL) && (DEVICE_POWER_CONTROL == 1)
#define DEVICE_POWER_NET_KEY_PIN        _BOARD_GPIO(DEVICE_POWER_NET_KEY_PIN_NUM)
#define DEVICE_POWER_PIN                _BOARD_GPIO(DEVICE_POWER_PIN_NUM)
#endif

/* Motion rotation MCP: ENABLE_APP_MOTION_ROTATION_MCP comes from Kconfig directly */

#ifdef __cplusplus
}
#endif

#endif /* __TUYA_BOARD_CONFIG_H__ */
