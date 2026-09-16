/**
* @file tal_tp_i2c.h
* @brief Common process - tp process
* @version 0.1
* @date 2025-05-29
*
* @copyright Copyright 2021-2025 Tuya Inc. All Rights Reserved.
*
*/
#ifndef __TKL_TP_I2C_H__
#define __TKL_TP_I2C_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"

#define TP_I2C_MSG_MAX_LEN (256)

/**
 * @brief tp i2c init
 * 
 * @param[in] clk: i2c clk gpio
 *
 * @param[in] sda: i2c sda gpio
 *
 * @param[in] idx: i2x idx
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tal_tp_i2c_init(uint8_t clk, uint8_t sda, TUYA_I2C_NUM_E idx);

/**
 * @brief tp i2c deinit
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tal_tp_i2c_deinit(void);

/**
 * @brief tp i2c read value form salve reg
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
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tal_tp_i2c_read(uint8_t addr, uint16_t reg, uint8_t *buf, uint16_t buf_len, uint8_t is_16_reg);

/**
 * @brief tp i2c read value form salve reg
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
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tal_tp_i2c_write(uint8_t addr, uint16_t reg, uint8_t *buf, uint16_t buf_len, uint8_t is_16_reg);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif