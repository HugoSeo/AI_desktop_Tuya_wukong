/**
* @file ty_display_service.h
* @brief Common process - display service
* @version 0.1
* @date 2025-03-27
*
* @copyright Copyright 2021-2025 Tuya Inc. All Rights Reserved.
*
*/
#ifndef __DISPLAY_SERVICE_H__
#define __DISPLAY_SERVICE_H__

#include "tuya_cloud_types.h"
#include "tal_display_spi.h"
#include "tal_display_qspi.h"
#include "tal_display_rgb.h"
#include "tal_display_8080.h"
#include "ty_frame_buff.h"

#ifdef __cplusplus
extern "C" {
#endif



typedef struct {
	union {
		ty_display_rgb_cfg  rgb_cfg;
		ty_display_8080_cfg  mcu8080_cfg;
		ty_display_qspi_cfg qspi_cfg;
		ty_display_spi_cfg  spi_cfg;
	};
} ty_display_cfg;

typedef struct {
	TUYA_DISPLAY_TYPE_E type;
	char *name;
	union {
		ty_display_rgb_device_t rgb;  /**< RGB interface lcd device config */
		ty_display_8080_device_t mcu8080;  /**< MCU 8080 interface lcd device config */
		ty_display_qspi_device_t qspi;/**< QSPI interface lcd device config */
		ty_display_spi_device_t spi;  /**< SPI interface lcd device config */
	};
} ty_display_device_s;

typedef  ty_display_device_s  *TY_DISPLAY_HANDLE;

/**
* @brief tal_display_open
*
* @param[in] device: device info
* @param[in] cfg: gpio cfg
*
* @note This API is used to open display device.
*
* @return !NULL on success. NULL on error
*/
TY_DISPLAY_HANDLE tal_display_open(ty_display_device_s *device, ty_display_cfg *cfg);

/**
* @brief tal_display_close
*
* @param[in] handle: display_device handle
*
* @note This API is used to close display device.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET tal_display_close(TY_DISPLAY_HANDLE handle);

/**
* @brief tal_display_flush
*
* @param[in] handle: display_device handle
* @param[in] frame_buff: frame buffer 
*
* @note This API is used to flush display device.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET tal_display_flush(TY_DISPLAY_HANDLE handle, ty_frame_buffer_t *frame_buff);

/**
* @brief tal_display_bl_open
*
* @param[in] handle: display_device handle
*
* @note This API is used to open bl.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET tal_display_bl_open(TY_DISPLAY_HANDLE handle);

/**
* @brief tal_display_bl_open
*
* @param[in] handle: display_device handle
* @param[in] brightness: brightness  0~100
*
* @note This API is used to set bl brightness.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET tal_display_bl_set(ty_display_device_s *device, UINT8_T brightness);

/**
* @brief tal_display_bl_close
*
* @param[in] handle: display_device handle
*
* @note This API is used to close bl.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET tal_display_bl_close(ty_display_device_s *device);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
