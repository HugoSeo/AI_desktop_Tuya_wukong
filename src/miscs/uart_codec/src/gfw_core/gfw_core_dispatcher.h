/**
 * @file gfw_core_dispatcher.h
 * @brief
 * @version 0.1
 * @date 2023-06-16
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

#ifndef __GFW_CORE_DISPATCHER_H__
#define __GFW_CORE_DISPATCHER_H__

#include "gfw_core_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief
 *
 */
typedef OPERATE_RET(*GFW_DETECT_SEND_CB)(VOID);

/**
 * @brief
 *
 */
typedef OPERATE_RET(*GFW_DETECT_RECV_CB)(UINT_T baudrate, AUDIO_GFW_CMD_DATA_T *data);

/**
 * @brief
 *
 */
typedef struct {
    TUYA_UART_NUM_E port;
    UINT_T baud_rate[MAX_DETECT_BAUD_NUM];
    UINT_T baud_rate_cnt;
    UINT_T cmd_interval;
    GFW_DETECT_SEND_CB detect_send;
    GFW_DETECT_RECV_CB detect_recv;
    AUDIO_GFW_CMD_CB cmd_handler;
    AUDIO_GFW_TRANS_T *trans;
} DISPATCHER_CFG_T;

/**
 * @brief
 *
 * @return OPERATE_RET
 */
OPERATE_RET gfw_core_dispatcher_start(DISPATCHER_CFG_T *cfg);

/**
 * @brief
 *
 * @return OPERATE_RET
 */
OPERATE_RET gfw_core_dispatcher_stop(VOID);

/**
 * @brief
 *
 * @param data
 * @return OPERATE_RET
 */
OPERATE_RET gfw_core_cmd_dispatch(AUDIO_GFW_CMD_DATA_T *data);

#ifdef __cplusplus
}
#endif

#endif // __GFW_CORE_DISPATCHER_H__