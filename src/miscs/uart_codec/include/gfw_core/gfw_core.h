/**
 * @file gfw_core.h
 * @brief
 * @version 0.1
 * @date 2023-06-14
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

#ifndef __GFW_CORE_H__
#define __GFW_CORE_H__

#include "base_event.h"
#include "tal_uart.h"
#include "tal_memory.h"
#include "tal_thread.h"
//#include "tuya_device_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EVENT_BAUDRATE_LOCKED   "gfw.evt.br.lock"

#define MAX_DETECT_BAUD_NUM (4)

/*
 * @brief 模组支持的串口波特率列表
 */
#define TY_AUDIO_BAUD_LIST {1000000}

/**
 * @brief 串口缓冲区长度
 */
#define TY_UART_BUF_SIZE (2048)

typedef struct {
    void (*init)(void);
    void (*deinit)(void);
    int (*send)(BYTE_T *data, UINT16_T len);
    int (*recv)(BYTE_T *data, UINT16_T len);
} AUDIO_GFW_TRANS_T;

/**
 * @brief Definition of cfg to initialize generic firmware core.
 */
typedef struct {
    BOOL_T ext_trans; // external tx/rx, SPI, etc.
    AUDIO_GFW_TRANS_T trans;

    TUYA_UART_NUM_E port;
    UINT_T def_baudrate; // default baudrate
    BOOL_T auto_baudrate; // detect baudrate for communication

    THREAD_CFG_T thread_cfg;
} AUDIO_GFW_CORE_CFG_T;

/**
 * @brief Definition of cfg to initialize firmware sw.
 */
// typedef struct {
//     CHAR_T *firmware_key;
//     CHAR_T *sw_ver;
// } GFW_FW_CFG_T;

/**
 * @brief Initialize gfw core.
 *
 * @param[in] cfg config
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET gfw_core_init(AUDIO_GFW_CORE_CFG_T *cfg);

/**
 * @brief Deinitialize gfw core.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET gfw_core_deinit(VOID);

/**
 * @brief Start gfw core.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET gfw_core_start(VOID);

/**
 * @brief
 *
 * @return BYTE_T
 */
BYTE_T gfw_core_get_mcu_protocol_ver(VOID);

/**
 * @brief
 *
 * @param ver
 * @return OPERATE_RET
 */
OPERATE_RET gfw_core_set_mcu_protocol_ver(BYTE_T ver);

/**
 * @brief
 *
 * @return none
 */
VOID gfw_core_dispatcher_pause(VOID);

/**
 * @brief
 *
 * @return none
 */
VOID gfw_core_dispatcher_resume(VOID);

#ifdef __cplusplus
}
#endif

#endif // __GFW_CORE_H__
