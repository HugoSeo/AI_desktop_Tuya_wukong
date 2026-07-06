/**
* @file tal_tp_service.h
* @brief Common process - display service
* @version 0.1
* @date 2025-05-21
*
* @copyright Copyright 2021-2025 Tuya Inc. All Rights Reserved.
*
*/
#ifndef __TP_SERVICE_H__
#define __TP_SERVICE_H__

#include "tuya_cloud_types.h"
#include "uni_log.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TY_TP_DATA_QUEUE_MAX_SIZE (30)
typedef TUYA_GPIO_CTRL_T tp_gpio_ctrl_t;
typedef enum
{
	TP_ID_UNKNOW = 0,
	TP_ID_GT911,
	TP_ID_GT1151,
	TP_ID_FT6336,
	TP_ID_HY4633,
} ty_tp_sensor_id_t;

typedef enum
{
    TP_MIRROR_NONE,
    TP_MIRROR_X_COORD,
    TP_MIRROR_Y_COORD,
    TP_MIRROR_X_Y_COORD,
} ty_tp_mirror_type_t;

typedef enum
{
    TP_EVENT_UNKONW,
    TP_EVENT_PRESS_DOWN,
    TP_EVENT_RELEASE_UP,
    TP_EVENT_CONTACT_MOVE,
} ty_tp_event_t;

typedef struct
{
    uint16_t m_x;
    uint16_t m_y;
    uint16_t m_state;              //0 release 1 press
    uint16_t m_need_continue;
} ty_tp_point_info_t;

typedef struct
{
    uint8_t tp_point_id;
    uint16_t tp_point_x;
    uint16_t tp_point_y;
    ty_tp_event_t tp_point_event;
    uint32_t tp_timestamp;
} ty_tp_read_point_t;

typedef struct
{
    uint8_t point_cnt;
    uint8_t support_get_event;
    ty_tp_read_point_t *point_arr;
} ty_tp_read_data_t;

typedef struct
{
    tp_gpio_ctrl_t tp_i2c_clk;
    tp_gpio_ctrl_t tp_i2c_sda;
    tp_gpio_ctrl_t tp_rst;                // reset gpio
    tp_gpio_ctrl_t tp_intr;               // interrupt gpio
    tp_gpio_ctrl_t tp_pwr_ctrl;
    TUYA_I2C_NUM_E tp_i2c_idx;
} ty_tp_usr_cfg_t;

typedef struct ty_tp_device_cfg
{
	char *name;  /**< sensor name */
	uint8_t id;  /**< sensor type, sensor_id_t */
    uint16_t width;  /**< x coordinate size */
	uint16_t height;  /**< y coordinate size */
    TUYA_GPIO_IRQ_E intr_type;  /**< sensor interrupt trigger type */ 
    ty_tp_mirror_type_t mirror_type; /**< sensor default mirror_type */
	uint8_t refresh_rate;  /**< sensor default refresh rate(unit: ms) */
	uint8_t max_support_tp_num;  /**< sensor default touch number */

    void (*init)(struct ty_tp_device_cfg *cfg);
    void (*read_pont_info)(ty_tp_read_data_t *read_point);
    ty_tp_usr_cfg_t *tp_cfg;
} ty_tp_device_cfg_t;

typedef  ty_tp_device_cfg_t  *TY_TP_HANDLE;

/**
* @brief tal_tp_open
*
* @param[in] device: device info
* @param[in] cfg: gpio cfg
*
* @note This API is used to open touch panel device.
*
* @return !NULL on success. NULL on error
*/
TY_TP_HANDLE tal_tp_open(ty_tp_device_cfg_t *device, ty_tp_usr_cfg_t *cfg);

/**
* @brief tal_tp_close
*
* @param[in] handle: tp_device handle
*
* @note This API is used to close touch panel device.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET tal_tp_close(TY_TP_HANDLE handle);

/**
* @brief tal_tp_read
*
* @param[out] point: buff use to fill tp data
*
* @note This API is used to read point info form queue.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET tal_tp_read(ty_tp_point_info_t *point);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
