/**
 * @file lv_conf.h
 * This file exists only to be compatible with Arduino's library structure
 */

#ifndef TY_LV_CONF_H
#define TY_LV_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#if defined(TUYA_LVGL_VERSION) && (TUYA_LVGL_VERSION == 8)
#include "../src/lvgl_v8/lv_conf.h"
#elif defined(TUYA_LVGL_VERSION) && (TUYA_LVGL_VERSION == 9)
#include "../src/lvgl_v9/lv_conf.h"
#else
#error "undefine lvgl version"
#endif
/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*TY_LV_CONF_H*/
