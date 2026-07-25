/**
 * @file libjpeg_turbo_encode.h
 *
 */

#ifndef LIBJPEG_TURBO_ENCODE_H
#define LIBJPEG_TURBO_ENCODE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "tuya_app_gui_core_config.h"
#include "tuya_cloud_types.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/

OPERATE_RET jpg_enc_img(const char *out_file_name, uint8_t *img_data, DISPLAY_PIXEL_FORMAT_E fmt, uint32_t img_width, uint32_t img_height, uint32_t quality/*50-80*/);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*LIBJPEG_TURBO_DECODE_H*/
