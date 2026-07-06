/*
 * lcd_common.c
 * Copyright (C) 2024 cc <cc@tuya>
 *
 * Distributed under terms of the MIT license.
 */

#include "string.h"
#include "tuya_cloud_types.h"
#include "tdd_lcd.h"
#include "tkl_system.h"
#include "lcd_common.h"
#include "tal_log.h"

#ifdef TUYA_MULTI_TYPES_LCD
static const ty_display_device_s *tuya_lcd_devices [] = {
#if defined(LCD_MODULE_T35P128CQ) || defined(T5AI_BOARD)
    &lcd_rgb_ili9488_device,
    &tuya_lcd_device_t35p128cq,
#elif defined(LCD_MODULE_T50P181CQ)
    &lcd_rgb_st7701sn_device,
#elif defined(LCD_MODULE_FRD240B28009_A)
    &lcd_i8080_st7789P3_device,
#elif defined(TUYA_LCD_INTERFACE_8080_ST7796S)
    &lcd_i8080_st7796S_device,
#elif defined(LCD_MODULE_GMT154_7P) || defined(T5AI_BOARD_EVB) || defined(T5AI_BOARD_DESKTOP) || defined(T5AI_BOARD_EVB_PRO)
    &lcd_spi_st7789_device,
    &lcd_spi_st7789v2_device,
#elif defined(LCD_MODULE_MTF018XD01A_V1)
    &lcd_spi_st77916_device,
#elif defined(LCD_MODULE_NV3041)
    &lcd_qspi_nv3041_device,
#elif defined(T5AI_BOARD_ROBOT)
    &lcd_spi_st7789p3_device,
#elif defined(T5AI_BOARD_EYES)
    &lcd_spi_st7735s_device,
#else
#error "undefine lcd device"
#endif
};

#else
static const TUYA_LCD_DEVICE_T *tuya_lcd_devices [] = {
#ifdef  TUYA_LCD_DEVICE_T50P181CQ
    &tuya_lcd_device_t50p181cq,
#endif // TUYA_LCD_DEVICE_T50P181CQ
};
#endif

VOID *tdd_lcd_driver_query(CONST CHAR_T *name, UINT_T type)
{
    if (name == NULL) {
        return NULL;
    }

#ifdef TUYA_MULTI_TYPES_LCD
    for (int i = 0; i < sizeof(tuya_lcd_devices)/sizeof(ty_display_device_s *); i++) {
        if (0 == strcmp(name, tuya_lcd_devices[i]->name) && (TUYA_DISPLAY_TYPE_E)type == tuya_lcd_devices[i]->type) {
            return (VOID *)tuya_lcd_devices[i];
        }
    }
#else
    for (int i = 0; i < sizeof(tuya_lcd_devices)/sizeof(TUYA_LCD_DEVICE_T *); i++) {
        if (tuya_lcd_devices[i] == NULL)
            break;
        if (0 == strcmp(name, tuya_lcd_devices[i]->name)) {
            return tuya_lcd_devices[i];
        }
    }
#endif
    TAL_PR_WARN("not found lcd driver: %s", name);
    return NULL;
}

