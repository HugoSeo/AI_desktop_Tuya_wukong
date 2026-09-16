/**
 * @file tuya_device_board.c
 * @author www.tuya.com
 * @brief tuya_device_board module is used to
 * @version 0.1
 * @date 2022-10-28
 *
 * @copyright Copyright (c) tuya.inc 2022
 *
 */
#include "tuya_device_board.h"
#if defined(ENABLE_BT_SERVICE) && (ENABLE_BT_SERVICE == 1)
#include "tuya_bt.h"
#endif

/***********************************************************
************************macro define************************
***********************************************************/

/**
 * @brief evb board initialization
 *
 * @param[in] none
 *
 * @return OPRT_OK on success. Others on error, please refer to "tuya_error_code.h".
 */
OPERATE_RET tuya_device_board_init(VOID_T)
{
    OPERATE_RET rt = OPRT_OK;

#if defined(ENABLE_BT_SERVICE) && (ENABLE_BT_SERVICE == 1)
    /* BLE only for provision */
    tuya_ble_set_startup_attr(TUYA_BLE_ABILITY_NETCFG);
#endif

    return rt;
}

