/**
 * @file gfw_mcu_ota.h
 * @brief
 * @version 0.1
 * @date 2023-06-15
 *
 * @copyright Copyright (c) 2023 Tuya Inc. All Rights Reserved.
 *
 * Permission is hereby granted, to any person obtaining a copy of this software and
 * associated documentation files (the "Software"), Under the premise of complying
 * with the license of the third-party open source software contained in the software,
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software.
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 */

#ifndef __GFW_MCU_OTA_H__
#define __GFW_MCU_OTA_H__

#include "tuya_cloud_types.h"
#include "tuya_cloud_com_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GFW_EVENT_AUDIO_OTA_END   "gfw.evt.ota.end"
#define GFW_EVENT_VERSION_UPDATE    "gfw.evt.ver.upd"

/**
 * @brief is in mcu ota process
 * @return TRUE in mcu ota, FALSE not in mcu ota
 */
BOOL_T gfw_is_in_mcu_ota_reset();

/**
 * @brief is in mcu ota process
 * @return TRUE in mcu ota, FALSE not in mcu ota
 */
BOOL_T gfw_is_in_mcu_ota_upgrade();

/**
 * @brief Handler to process download result
 * @param[in] download_result download result, 0 indicates success
 * @return 执行结果,返回 OPRT_OK表示成功，返回 其他 则表示失败
 */
OPERATE_RET gfw_mcu_ota_dev_ug_notify_cb(IN CONST INT_T download_result);

/**
 * @brief mcu ota pre upgrade cb
 * @param[in] fw ota info
 * @return TUS_RD:success, other refer to TI_UPGRD_STAT_S
 */
INT_T gfw_mcu_ota_pre_ug_inform_cb(IN CONST FW_UG_S *fw);

/**
 * @brief Handler to process gateway upgrade inform
 * @param[in] fw Firmware info, see FW_UG_S
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
INT_T gfw_mcu_ota_ug_inform_cb(IN CONST FW_UG_S *fw);

/**
 * @brief mcu ota init
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET gfw_mcu_ota_init(VOID);

#ifdef __cplusplus
}
#endif

#endif // __GFW_MCU_OTA_H__
