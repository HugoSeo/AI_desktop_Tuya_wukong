/**
* @file ty_display_spi.h
* @brief Common process - spi display process
* @version 0.1
* @date 2025-03-27
*
* @copyright Copyright 2021-2025 Tuya Inc. All Rights Reserved.
*
*/
#ifndef __TY_DISPLAY_QSPI_H__
#define __TY_DISPLAY_QSPI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"
#include "ty_frame_buff.h"

typedef struct {
    UINT16_T port;
    TUYA_GPIO_CTRL_T reset;
    TUYA_GPIO_CTRL_T bl;
    TUYA_GPIO_CTRL_T power_ctrl;
} ty_display_qspi_cfg;

typedef struct {
    UINT8_T cmd;
    UINT8_T cmd_lines;
    UINT8_T addr[7];
    UINT8_T addr_size;
    UINT8_T addr_lines;
} qspi_pixel_cmd;

typedef enum {
	QSPI_REFRESH_BY_LINE,
    QSPI_REFRESH_BY_FRAME
} qspi_refresh_method_e;

typedef struct {
	uint8_t hsync_cmd;
	uint8_t vsync_cmd;
	uint8_t vsw;
	uint8_t hfp;
	uint8_t hbp;
    uint16_t line_len;
} qspi_refresh_config_by_line_t;


typedef struct {
    UINT16_T width;
    UINT16_T height;
    DISPLAY_PIXEL_FORMAT_E pixel_fmt;
    UINT8_T reg_write_cmd;
    qspi_refresh_method_e refresh_method;
    qspi_pixel_cmd pixel_pre_cmd;
    UINT8_T has_pixel_memory;//1--has pixel memory; 0--has no pixel momory
    TUYA_QSPI_BASE_CFG_T qspi_cfg;
    DISPLAY_INIT_SEQ_T *init_seq;
    ty_display_qspi_cfg *display_cfg;
} ty_display_qspi_device_t;



/**
* @brief tal_display_qspi_open
*
* @param[in] device: device info
*
* @note This API is used to open display device.
*
* @return id >= 0 on success. Others on error
*/
OPERATE_RET tal_display_qspi_open(ty_display_qspi_device_t *device);

/**
* @brief tal_display_qspi_close
*
* @param[in] device: device info
*
* @note This API is used to close display device.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET tal_display_qspi_close(ty_display_qspi_device_t *device);

/**
* @brief tal_display_qspi_flush
*
* @param[in] device: device info
* @param[in] frame_buff: frame buff
*
* @note This API is used to flush display device.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET tal_display_qspi_flush(ty_display_qspi_device_t *device, ty_frame_buffer_t *frame_buff);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
