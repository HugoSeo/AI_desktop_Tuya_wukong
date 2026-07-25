/**
* @file tal_display_rgb.h
* @brief Common process - spi display process
* @version 0.1
* @date 2025-03-27
*
* @copyright Copyright 2021-2025 Tuya Inc. All Rights Reserved.
*
*/
#ifndef __TY_DISPLAY_RGB_H__
#define __TY_DISPLAY_RGB_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"
#include "ty_frame_buff.h"

typedef struct {
    TUYA_GPIO_NUM_E spi_clk;
    TUYA_GPIO_NUM_E spi_sda;
    TUYA_GPIO_NUM_E spi_csx;
    TUYA_GPIO_CTRL_T bl;
    TUYA_GPIO_CTRL_T reset;//如果使用上电复位，也可以不需要这个io（不需要时置为TUYA_GPIO_NUM_MAX）
    TUYA_GPIO_CTRL_T power_ctrl;//需要控制功耗的品类需要这个io（不需要时置为TUYA_GPIO_NUM_MAX）
} ty_display_rgb_cfg;

typedef struct {
    TUYA_RGB_BASE_CFG_T cfg;
    DISPLAY_INIT_SEQ_T *init_seq;
    ty_display_rgb_cfg *display_cfg;
} ty_display_rgb_device_t;

/**
* @brief tal_display_rgb_open
*
* @param[in] device: device info
*
* @note This API is used to open display device.
*
* @return id >= 0 on success. Others on error
*/
OPERATE_RET tal_display_rgb_open(ty_display_rgb_device_t *device);

/**
* @brief tal_display_lcdc_close
*
* @param[in] device: device info
*
* @note This API is used to close display device.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET tal_display_rgb_close(ty_display_rgb_device_t *device);

/**
* @brief tal_display_rgb_flush
*
* @param[in] device: device info
* @param[in] frame_buff: frame buff
*
* @note This API is used to flush display device.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET tal_display_rgb_flush(ty_display_rgb_device_t *device, ty_frame_buffer_t *frame_buff);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
