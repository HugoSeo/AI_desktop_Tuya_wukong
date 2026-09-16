/**
 * @file joyinside_client.h
 * @brief joyinside client module
 * @version 0.1
 * @date 2025-12-15
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

#ifndef __JOYINSIDE_CLIENT_H__
#define __JOYINSIDE_CLIENT_H__

#include "tuya_cloud_types.h"
#include "joyinside_biz.h"

#if defined ENABLE_JD_DEBUG && (ENABLE_JD_DEBUG == 1)
#define JD_PR_D(...) PR_DEBUG(__VA_ARGS__)
#else
#define JD_PR_D(...) PR_TRACE(__VA_ARGS__)
#endif

/**
 * @brief joyinside client init
 *
 * @param chat_cfg: chat configuration
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET joyinside_client_init(JD_CHAT_CFG_S *chat_cfg);

/**
 * @brief joyinside client deinit
 *
 */
VOID joyinside_client_deinit(VOID);

/**
 * @brief joyinside client close
 *
 */
VOID joyinside_client_close(VOID);

/**
 * @brief check joyinside client is ready
 *
 * @return TRUE: ready; FALSE: not ready
 */
BOOL_T joyinside_client_is_ready(VOID);

/**
 * @brief start joyinside ping
 *
 */
VOID joyinside_start_ping(VOID);

/**
 * @brief handle pong
 *
 */
VOID joyinside_handle_pong(VOID);

/**
 * @brief joyinside transporter write data
 *
 * @param data: data buffer pointer
 * @param len: data length
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET joyinside_transporter_write(CHAR_T *data, UINT_T len);

#endif // __JOYINSIDE_CLIENT_H__