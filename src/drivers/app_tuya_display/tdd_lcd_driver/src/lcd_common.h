/*
 * lcd_common.h
 * Copyright (C) 2024 cc <cc@tuya>
 *
 * Distributed under terms of the MIT license.
 */

#include "tdd_lcd.h"

#ifdef TUYA_MULTI_TYPES_LCD
#include "tal_display_service.h"
#endif

#ifndef LCD_COMMON_H
#define LCD_COMMON_H

#ifdef TUYA_MULTI_TYPES_LCD
#if defined(LCD_MODULE_T35P128CQ) || defined(T5AI_BOARD)
    extern const ty_display_device_s lcd_rgb_ili9488_device;
    extern const ty_display_device_s tuya_lcd_device_t35p128cq;
#elif defined(LCD_MODULE_T50P181CQ)
    extern const ty_display_device_s lcd_rgb_st7701sn_device;
#elif defined(LCD_MODULE_FRD240B28009_A)
    extern const ty_display_device_s lcd_i8080_st7789P3_device;
#elif defined(TUYA_LCD_INTERFACE_8080_ST7796S)
    extern const ty_display_device_s lcd_i8080_st7796S_device;
#elif defined(LCD_MODULE_GMT154_7P) || defined(T5AI_BOARD_EVB) || defined(T5AI_BOARD_DESKTOP) || defined(T5AI_BOARD_EVB_PRO)
    extern const ty_display_device_s lcd_spi_st7789_device;
    extern const ty_display_device_s lcd_spi_st7789v2_device;
#elif defined(LCD_MODULE_MTF018XD01A_V1)
    extern const ty_display_device_s lcd_spi_st77916_device;
#elif defined(LCD_MODULE_NV3041)
    extern const ty_display_device_s lcd_qspi_nv3041_device;
#elif defined(T5AI_BOARD_ROBOT)
    extern const ty_display_device_s lcd_spi_st7789p3_device;
#elif defined(T5AI_BOARD_EYES)
    extern const ty_display_device_s lcd_spi_st7735s_device;
#endif

#else
#ifdef  TUYA_LCD_DEVICE_T50P181CQ
extern const TUYA_LCD_DEVICE_T tuya_lcd_device_t50p181cq;
#endif // TUYA_LCD_DEVICE_T50P181CQ
#endif

#endif /* !LCD_COMMON_H */
