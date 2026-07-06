/**
 * @file lvgl.h
 * This file exists only to be compatible with Arduino's library structure
 */

#ifndef TUYA_LVGL_SRC_H
#define TUYA_LVGL_SRC_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#if defined(TUYA_LVGL_VERSION) && (TUYA_LVGL_VERSION == 8)
#include "../../src/lvgl_v8/lvgl.h"
#elif defined(TUYA_LVGL_VERSION) && (TUYA_LVGL_VERSION == 9)
#include "../../src/lvgl_v9/lvgl.h"
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

#endif /*TUYA_LVGL_SRC_H*/
