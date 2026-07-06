/**
* @file ty_display_service.h
* @brief Common process - display service
* @version 0.1
* @date 2025-03-27
*
* @copyright Copyright 2021-2025 Tuya Inc. All Rights Reserved.
*
*/
#ifndef __FRAME_BUFF_H__
#define __FRAME_BUFF_H__

#ifdef __cplusplus
extern "C" {
#endif
#include "tuya_cloud_types.h"

typedef enum  {
    TY_INIT_RST = 0,
    TY_INIT_REG,
    TY_INIT_DELAY,
    TY_INIT_CONF_END
} DISPLAY_INIT_TYPE;

typedef struct {
    UINT8_T r;
    UINT8_T len;
    UINT8_T v[62];
} DISPLAY_REG_DATA_T;

typedef struct {
    UINT16_T delay;
} DISPLAY_RESET_DATA_T;

typedef struct {
    DISPLAY_INIT_TYPE type;
    union {
        UINT32_T delay_time;
        DISPLAY_REG_DATA_T reg;
        DISPLAY_RESET_DATA_T reset[3];
    };
} DISPLAY_INIT_SEQ_T;

typedef enum {
    TYPE_SRAM = 0,
    TYPE_PSRAM,
} RAM_TYPE_E;

typedef void (*frame_buff_free_cb)(UINT8_T *frame_buff);

typedef struct
{
    RAM_TYPE_E type;
	DISPLAY_PIXEL_FORMAT_E fmt;
	uint16_t x_start;
	uint16_t y_start;
	uint16_t width;
	uint16_t height;
    frame_buff_free_cb free_cb;
	UINT32_T len;
	uint8_t *frame;
	uint8_t *pdata;
} ty_frame_buffer_t;

void *ty_frame_buff_malloc(RAM_TYPE_E type, UINT32_T size);

OPERATE_RET ty_frame_buff_free(void *frame_buff);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
