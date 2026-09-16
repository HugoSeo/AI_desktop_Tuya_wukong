/**
* @file tal_camera_dvp.h
* @brief Common process - camera dvp process
* @version 0.1
* @date 2025-06-06
*
* @copyright Copyright 2021-2025 Tuya Inc. All Rights Reserved.
*
*/
#ifndef __TY_CAMERA_DVP_H__
#define __TY_CAMERA_DVP_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"


#define DVP_I2C_MSG_MAX_LEN         (8)
#if ENABLE_DVP_SPLIT_FRAME
#define DVP_FRAME_CNT               (DVP_SPLIT_FRAME_SEG_CNT)
#else 
#define DVP_FRAME_CNT               (2)
#endif
#define DVP_NODE_POOL_SIZE          (DVP_FRAME_CNT)

#define SENSOR_HAS_INTERNAL_CLOCK   (0)

typedef VOID (*DVP_FRAME_HANDLE)(TUYA_DVP_FRAME_MANAGE_T *output_frame, VOID *user_data);

typedef struct {
    TUYA_I2C_NUM_E dvp_i2c_idx;
    TUYA_GPIO_CTRL_T dvp_i2c_clk;
    TUYA_GPIO_CTRL_T dvp_i2c_sda;
    TUYA_GPIO_CTRL_T dvp_pwr_ctrl;
	TUYA_GPIO_CTRL_T dvp_rst_ctrl;
} TUYA_DVP_PIN_CFG_T;

typedef struct {
    TUYA_DVP_CFG_T dvp_cfg;
    TUYA_DVP_PIN_CFG_T pin_cfg;
    DVP_FRAME_HANDLE dvp_frame_handle;
    VOID *frame_user_data;   /* passed as-is to dvp_frame_handle */
} TUYA_DVP_USR_CFG_T;

typedef struct {
    UINT32_T clk;
    UINT16_T max_fps;
    UINT32_T max_width;
    UINT32_T max_height;
    TUYA_FRAME_FMT_E fmt;
    OPERATE_RET (*init)(TUYA_DVP_CFG_T *cfg);
    OPERATE_RET (*detect)(void);
} TUYA_DVP_SENSOR_CFG_T;

typedef struct {
    TUYA_DVP_SENSOR_CFG_T sensor_cfg;
    TUYA_DVP_USR_CFG_T usr_cfg;
} TUYA_DVP_DEVICE_T;

/**
 * @brief tal_dvp_init
 *
 * @param[in] device: device info
 *
 * @note This API is used to init camera device.
 *
 * @return id >= 0 on success. Others on error
 */
TUYA_DVP_DEVICE_T *tal_dvp_init(TUYA_DVP_SENSOR_CFG_T *sensor_cfg, TUYA_DVP_USR_CFG_T *usr_cfg);

/**
 * @brief tal_dvp_deinit
 *
 * @param[in] device: device info
 *
 * @note This API is used to deinit camera device.
 *
 * @return id >= 0 on success. Others on error
 */
OPERATE_RET tal_dvp_deinit(TUYA_DVP_DEVICE_T *device);

/**
 * @brief tal_dvp_start
 *
 * @param[in] device: device info
 *
 * @note This API is used to start camera device.
 *
 * @return OPRT_OK on success. Others on error
 */
OPERATE_RET tal_dvp_start(TUYA_DVP_DEVICE_T *device);

/**
 * @brief tal_dvp_stop
 *
 * @param[in] device: device info
 *
 * @note This API is used to stop camera device.
 *
 * @return OPRT_OK on success. Others on error
 */
OPERATE_RET tal_dvp_stop(TUYA_DVP_DEVICE_T *device);

/**
 * @brief tal_dvp_i2c_read
 *
 * @param[in] addr: i2c write/read address
 *
 * @param[in] reg: get value form salve reg
 *
 * @param[out] buf: get value wirte into buf
 *
 * @param[out] buf: len of buf 
 *
 * @param[in] is_16_reg: if 16-bit reg, 1; if 8-bit reg, 0
 *
 * @note This API is camera capture module used to i2c communicate with dvp when init.
 *
 * @return id >= 0 on success. Others on error
 */
OPERATE_RET tal_dvp_i2c_read(UINT8_T addr, UINT16_T reg, UINT8_T *buf, UINT16_T buf_len, UINT8_T is_16_reg);

/**
 * @brief tal_dvp_i2c_write
 *
 * @param[in] addr: i2c write/read address
 *
 * @param[in] reg: write value into salve reg
 *
 * @param[out] buf: write value form buf
 *
 * @param[out] buf: len of buf 
 *
 * @param[in] is_16_reg: if 16-bit reg, 1; if 8-bit reg, 0
 *
 * @note This API is camera capture module used to i2c communicate with dvp when init.
 * 
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tal_dvp_i2c_write(UINT8_T addr, UINT16_T reg, UINT8_T *buf, UINT16_T buf_len, UINT8_T is_16_reg);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif