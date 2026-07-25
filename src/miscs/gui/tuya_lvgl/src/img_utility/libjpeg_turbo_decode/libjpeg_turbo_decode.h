/**
 * @file libjpeg_turbo_decode.h
 *
 */

#ifndef LIBJPEG_TURBO_DECODE_H
#define LIBJPEG_TURBO_DECODE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "tuya_app_gui_core_config.h"

#if defined(TUYA_LIBJPEG_TURBO) && (TUYA_LIBJPEG_TURBO == 1)
#include "tuya_cloud_types.h"
#include "lvgl.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/

OPERATE_RET jpg_dec_img(bool is_file, char *file_name, uint8_t *img_data, uint32_t img_size, lv_img_dsc_t *img_dst);

OPERATE_RET jpg_dec_img_with_scale(bool is_file, char *file_name, uint8_t *img_data, uint32_t img_size, lv_img_dsc_t *img_dst, int target_width, int target_height);

/**********************
 *      MACROS
 **********************/

#endif /*TUYA_LIBJPEG_TURBO*/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*LIBJPEG_TURBO_DECODE_H*/
