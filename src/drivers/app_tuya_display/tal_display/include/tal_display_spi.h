/**
* @file tal_display_spi.h
* @brief Common process - spi display process
* @version 0.1
* @date 2025-03-27
*
* @copyright Copyright 2021-2025 Tuya Inc. All Rights Reserved.
*
*/
#ifndef __TY_DISPLAY_SPI_H__
#define __TY_DISPLAY_SPI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"
#include "ty_frame_buff.h"

typedef struct {
    TUYA_SPI_NUM_E port;
    TUYA_GPIO_CTRL_T reset;
    TUYA_GPIO_CTRL_T bl;
    TUYA_GPIO_CTRL_T rs;
    TUYA_GPIO_CTRL_T power_ctrl; 
    TUYA_GPIO_CTRL_T soft_cs;  //模拟 spi 的 CSN 引脚
} ty_display_spi_cfg;

typedef struct {
    UINT16_T width;
    UINT16_T height;
    DISPLAY_PIXEL_FORMAT_E pixel_fmt;
    TUYA_SPI_BASE_CFG_T cfg;
    DISPLAY_INIT_SEQ_T *init_seq;
    ty_display_spi_cfg *display_cfg;
} ty_display_spi_device_t;

/**
* @brief tal_display_spi_open
*
* @param[in] device: device info
*
* @note This API is used to open display device.
*
* @return id >= 0 on success. Others on error
*/
OPERATE_RET tal_display_spi_open(ty_display_spi_device_t *device);

/**
* @brief tal_display_close
*
* @param[in] device: device info
*
* @note This API is used to close display device.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET tal_display_spi_close(ty_display_spi_device_t *device);

/**
* @brief tal_display_spi_flush
*
* @param[in] device: device info
* @param[in] frame_buff: frame buff
*
* @note This API is used to flush display device.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET tal_display_spi_flush(ty_display_spi_device_t *device, ty_frame_buffer_t *frame_buff);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif