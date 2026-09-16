/*
 * tdd_lcd.h
 * Copyright (C) 2024 cc <cc@tuya>
 *
 * Distributed under terms of the MIT license.
 */

#ifndef __TDD_LCD_H__
#define __TDD_LCD_H__

#include "tuya_cloud_types.h"
#include "tkl_display.h"
#include "tuya_app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TUYA_LCD_DEVICE_T50P181CQ   1

VOID *tdd_lcd_driver_query(CONST CHAR_T *name, UINT_T type);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* !__TDD_LCD_H__ */
