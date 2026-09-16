/**
* @file ty_soft_spi.h
* @brief Common process - soft spi process
* @version 0.1
* @date 2025-03-27
*
* @copyright Copyright 2021-2025 Tuya Inc. All Rights Reserved.
*
*/
#ifndef __TY_SOFT_SPI_H__
#define __TY_SOFT_SPI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"

typedef struct {
    TUYA_GPIO_NUM_E spi_clk;
    TUYA_GPIO_NUM_E spi_sda;
    TUYA_GPIO_NUM_E spi_csx;
} soft_spi_gpio_cfg_t;

OPERATE_RET ty_soft_spi_init(soft_spi_gpio_cfg_t *gpio_cfg);

void ty_soft_spi_write_cmd(soft_spi_gpio_cfg_t *gpio_cfg, UINT8_T cmd);

void ty_soft_spi_write_data(soft_spi_gpio_cfg_t *gpio_cfg, UINT8_T data);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif