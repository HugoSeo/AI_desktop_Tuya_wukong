/**
 * @file uart.h
 * @brief 
 * @version 0.1
 * @date 2025-12-11
 * 
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
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

#ifndef __UART_H__
#define __UART_H__
#include "tuya_cloud_types.h"
#ifdef __cplusplus
extern "C" {
#endif

OPERATE_RET wukong_kws_uart_init(VOID);
OPERATE_RET wukong_kws_uart_deinit(VOID);
OPERATE_RET wukong_kws_uart_start(VOID);
OPERATE_RET wukong_kws_uart_stop(VOID);

#ifdef __cplusplus
}
#endif
#endif  /* __UART_H__ */
