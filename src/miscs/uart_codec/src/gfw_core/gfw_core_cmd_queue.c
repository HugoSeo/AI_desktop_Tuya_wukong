/**
 * @file gfw_core_cmd_queue.c
 * @brief
 * @version 0.1
 * @date 2023-06-20
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

#include "uni_log.h"
#include "tuya_list.h"
#include "tal_uart.h"
#include "tal_mutex.h"
#include "tal_memory.h"
#include "tal_system.h"
#include "tal_semaphore.h"
#include "tal_sw_timer.h"
#include "gfw_core_cmd.h"
#include "gfw_core_frame.h"
#include "gfw_core_cmd_queue.h"

#define DEFAULT_CHECK_INTERVAL_MS (50) //ms
#define INSTANT_CHECK_INTERVAL_MS (10) //ms

typedef struct {
    AUDIO_GFW_CMD_TIMEOUT_CB timeout_cb;
    SEM_HANDLE sem;
    OPERATE_RET rt;
} TIMEOUT_CTX_T;

typedef enum {
    ITEM_STATE_IDLE, // invalid
    ITEM_STATE_IN,   // just add in queue
    ITEM_STATE_OUT,  // sent and waiting for ack
    ITEM_STATE_RETRY,// retry
} ITEM_STATE_E;

typedef struct {
    BYTE_T type; //GFW_CMD_TYPE_E
    BYTE_T state;
    USHORT_T timeout_cnt;
    UINT_T timeout_ms;
    UINT_T resend_cnt;
    UINT16_T len;
    BYTE_T *data;
    TIMEOUT_CTX_T *ctx;
} CMD_QUEUE_ITEM_T;

typedef struct {
    MUTEX_HANDLE mutex;
    MUTEX_HANDLE mutex_instant;
    TIMER_ID timer;
    CMD_QUEUE_ITEM_T *queue_items;
    BYTE_T queue_num; // capacity of queue
    BYTE_T queue_in;  // rIndex
    BYTE_T queue_out; // wIndex
    BYTE_T queue_cnt; // count
    CMD_QUEUE_ITEM_T instant_item;
    BYTE_T paused;
    TUYA_UART_NUM_E port;
    AUDIO_GFW_TRANS_T *trans;
} CMD_QUEUE_CTX_T;

STATIC CMD_QUEUE_CTX_T s_cmd_queue_ctx;

STATIC OPERATE_RET __default_cmd_timeout_cb(CMD_QUEUE_ITEM_T *item)
{
    if (OPRT_OK != item->ctx->rt) {
        if (item->ctx->timeout_cb) {
            AUDIO_GFW_CMD_DATA_T data = {0};
            data.ver = item->data[2];
            data.cmd = item->data[3];
            data.len = (item->data[4] << 8) | item->data[5];
            data.data = item->data + 6;
            item->ctx->timeout_cb(&data);
        }
    }

    if (AUDIO_GFW_CMD_TYPE_ZEROCOPY != item->type) {
        Free(item->data);
    }
    item->data = NULL;
    item->len = 0;

    if (AUDIO_GFW_CMD_TYPE_DEFAULT == item->type) {
        Free(item->ctx);
        item->ctx = NULL;
    } else if (item->ctx->sem) {
        tal_semaphore_post(item->ctx->sem);
    }

    return OPRT_OK;
}

STATIC VOID_T __gfw_timer_check_timeout_cb(TIMER_ID timer_id, VOID_T *arg)
{
    CMD_QUEUE_ITEM_T *item = NULL;
    BOOL_T is_instant = FALSE;

    tal_mutex_lock(s_cmd_queue_ctx.mutex);
    if (s_cmd_queue_ctx.instant_item.state != ITEM_STATE_IDLE) {
        is_instant = TRUE;
    }

    if ((s_cmd_queue_ctx.queue_cnt > 0) || is_instant) {
        if (is_instant) {
            item = &s_cmd_queue_ctx.instant_item;
        } else {
            item = &s_cmd_queue_ctx.queue_items[s_cmd_queue_ctx.queue_out];
        }

        if (ITEM_STATE_IN == item->state) {
            // tuya_debug_hex_dump("-----------uart send ", 64, item->data, item->len);
            if (s_cmd_queue_ctx.trans->send) {
                s_cmd_queue_ctx.trans->send(item->data, item->len);
            } else {
                tal_uart_write(s_cmd_queue_ctx.port, item->data, item->len);
            }
            if (item->resend_cnt) {
                item->resend_cnt--;
            }
            item->state = ITEM_STATE_OUT;
        } else if (ITEM_STATE_OUT == item->state) {
            if (item->timeout_cnt) {
                item->state = ITEM_STATE_RETRY;
            } else {
                item->ctx->rt = OPRT_OK; // no need ack
                __default_cmd_timeout_cb(item);
                if (!is_instant) {
                    s_cmd_queue_ctx.queue_cnt --;
                    s_cmd_queue_ctx.queue_out = (s_cmd_queue_ctx.queue_out + 1) % s_cmd_queue_ctx.queue_num;
                }
                item->state = ITEM_STATE_IDLE;
            }
        } else if (ITEM_STATE_RETRY == item->state) {
            if (item->timeout_cnt > 0) {
                if (item->timeout_cnt == 1) {
                    if (item->resend_cnt) {
                        if (is_instant) {
                            item->timeout_cnt = item->timeout_ms / INSTANT_CHECK_INTERVAL_MS;
                        } else {
                            item->timeout_cnt = item->timeout_ms / DEFAULT_CHECK_INTERVAL_MS;
                        }
                        item->resend_cnt--;
                        // tuya_debug_hex_dump("-----------uart retry send ", 64, item->data, item->len);
                        if (s_cmd_queue_ctx.trans->send) {
                            s_cmd_queue_ctx.trans->send(item->data, item->len);
                        } else {
                            tal_uart_write(s_cmd_queue_ctx.port, item->data, item->len);
                        }
                    } else { // resend timeout
                        item->ctx->rt = OPRT_TIMEOUT;
                        __default_cmd_timeout_cb(item);
                        if (!is_instant) {
                            s_cmd_queue_ctx.queue_cnt --;
                            s_cmd_queue_ctx.queue_out = (s_cmd_queue_ctx.queue_out + 1) % s_cmd_queue_ctx.queue_num;
                        }
                        item->state = ITEM_STATE_IDLE;
                    }
                } else {
                    item->timeout_cnt--;
                }
            }
        }
    }
    tal_mutex_unlock(s_cmd_queue_ctx.mutex);

    if (is_instant) {
        tal_sw_timer_start(s_cmd_queue_ctx.timer, INSTANT_CHECK_INTERVAL_MS, TAL_TIMER_ONCE);
    } else if (s_cmd_queue_ctx.queue_cnt > 0) {
        tal_sw_timer_start(s_cmd_queue_ctx.timer, DEFAULT_CHECK_INTERVAL_MS, TAL_TIMER_ONCE);
    }
}

/**
 * @brief Send a CMD with parameters to MCU.
 *
 * @param type The type of the CMD to send.
 * @param cmd The CMD to send.
 * @param data The data to send with the CMD.
 * @param len The length of the data to send.
 * @param timeout_ms The timeout time for sending the CMD every time.
 * @param timeout_cb The callback function for handling timeout.
 * @param resend_cnt The number of times to resend the CMD before timing out.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET gfw_core_cmd_send_with_params(AUDIO_GFW_CMD_TYPE_E type, BYTE_T cmd, BYTE_T *data, UINT16_T len,
                                             UINT_T timeout_ms, AUDIO_GFW_CMD_TIMEOUT_CB timeout_cb, UINT_T resend_cnt)
{
    OPERATE_RET rt = OPRT_OK;
    CMD_QUEUE_ITEM_T item = {0};

    rt = gfw_core_frame_pack(type, cmd, data, len, &item.data, &item.len);
    if (OPRT_OK != rt) {
        PR_ERR("frame pack err");
        return rt;
    }
    // else
    // {
    //     tuya_debug_hex_dump("-----------uart send ", 64, item.data, item.len);   
    // }

    if (AUDIO_GFW_CMD_TYPE_DIRECT == type) {
        // PR_NOTICE("-----------uart derect item.len:%d >>>", item.len);
        tal_mutex_lock(s_cmd_queue_ctx.mutex);
        rt = tal_uart_write(s_cmd_queue_ctx.port, item.data, item.len);
        tal_mutex_unlock(s_cmd_queue_ctx.mutex);
        Free(item.data);
        return rt;
    }

    TIMEOUT_CTX_T *ctx = Malloc(SIZEOF(TIMEOUT_CTX_T));
    if (NULL == ctx) {
        PR_ERR("ctx malloc err");
        return OPRT_MALLOC_FAILED;
    }

    // PR_DEBUG("--cmd enqueue 0x%02x type %d data %p len %d", cmd, type, data, len);

    ctx->timeout_cb = timeout_cb;
    ctx->rt = OPRT_TIMEOUT;

    item.type = type;
    item.state = ITEM_STATE_IN;
    item.ctx = ctx;
    item.resend_cnt = resend_cnt;
    item.timeout_ms = timeout_ms;
    item.timeout_cnt = timeout_ms / DEFAULT_CHECK_INTERVAL_MS;

    if ((AUDIO_GFW_CMD_TYPE_INSTANT == type) || (AUDIO_GFW_CMD_TYPE_ZEROCOPY == type)) {
        item.timeout_cnt = timeout_ms / INSTANT_CHECK_INTERVAL_MS;
        tal_semaphore_create_init(&ctx->sem, 0, 1);
        tal_mutex_lock(s_cmd_queue_ctx.mutex_instant); // make sure only one instant cmd

        tal_mutex_lock(s_cmd_queue_ctx.mutex);
        memcpy(&s_cmd_queue_ctx.instant_item, &item, SIZEOF(CMD_QUEUE_ITEM_T));
        if (!tal_sw_timer_is_running(s_cmd_queue_ctx.timer)) {
            tal_sw_timer_start(s_cmd_queue_ctx.timer, INSTANT_CHECK_INTERVAL_MS, TAL_TIMER_ONCE);
        }
        tal_mutex_unlock(s_cmd_queue_ctx.mutex);

        tal_semaphore_wait(ctx->sem, SEM_WAIT_FOREVER);
        tal_semaphore_release(ctx->sem);
        tal_mutex_unlock(s_cmd_queue_ctx.mutex_instant);
        rt = ctx->rt;
        Free(ctx);
    } else if (AUDIO_GFW_CMD_TYPE_SYNC == type) {
        tal_semaphore_create_init(&ctx->sem, 0, 1);

        tal_mutex_lock(s_cmd_queue_ctx.mutex); // enqueue
        if (s_cmd_queue_ctx.queue_cnt < s_cmd_queue_ctx.queue_num) {
            memcpy(&s_cmd_queue_ctx.queue_items[s_cmd_queue_ctx.queue_in], &item, SIZEOF(CMD_QUEUE_ITEM_T));
            s_cmd_queue_ctx.queue_cnt ++;
            s_cmd_queue_ctx.queue_in = (s_cmd_queue_ctx.queue_in + 1) % s_cmd_queue_ctx.queue_num;
        } else {
            rt = OPRT_EXCEED_UPPER_LIMIT;
            Free(item.data);
        }
        if (!tal_sw_timer_is_running(s_cmd_queue_ctx.timer)) {
            tal_sw_timer_start(s_cmd_queue_ctx.timer, DEFAULT_CHECK_INTERVAL_MS, TAL_TIMER_ONCE);
        }
        tal_mutex_unlock(s_cmd_queue_ctx.mutex);

        if (OPRT_OK == rt) {
            tal_semaphore_wait(ctx->sem, SEM_WAIT_FOREVER);
            rt = ctx->rt;
        }
        tal_semaphore_release(ctx->sem);
        Free(ctx);
    } else {
        ctx->sem = NULL;
        tal_mutex_lock(s_cmd_queue_ctx.mutex); // enqueue
        if (s_cmd_queue_ctx.queue_cnt < s_cmd_queue_ctx.queue_num) {
            memcpy(&s_cmd_queue_ctx.queue_items[s_cmd_queue_ctx.queue_in], &item, SIZEOF(CMD_QUEUE_ITEM_T));
            s_cmd_queue_ctx.queue_cnt ++;
            s_cmd_queue_ctx.queue_in = (s_cmd_queue_ctx.queue_in + 1) % s_cmd_queue_ctx.queue_num;
        } else {
            rt = OPRT_EXCEED_UPPER_LIMIT;
            Free(item.data);
            Free(ctx);
        }
        if (!tal_sw_timer_is_running(s_cmd_queue_ctx.timer)) {
            tal_sw_timer_start(s_cmd_queue_ctx.timer, DEFAULT_CHECK_INTERVAL_MS, TAL_TIMER_ONCE);
        }
        tal_mutex_unlock(s_cmd_queue_ctx.mutex);
    }

    // PR_DEBUG("--cmd enqueue 0x%02x %d", cmd, rt);

    return rt;
}

OPERATE_RET gfw_core_cmd_queue_clear(VOID)
{
    INT_T i = 0;
    CMD_QUEUE_ITEM_T *item = NULL;

    tal_mutex_lock(s_cmd_queue_ctx.mutex);
    for (i = 0; i < s_cmd_queue_ctx.queue_num; i++) {
        item = &s_cmd_queue_ctx.queue_items[i];
        item->ctx->rt = OPRT_TIMEOUT;
        __default_cmd_timeout_cb(item);
    }
    s_cmd_queue_ctx.queue_in = 0;
    s_cmd_queue_ctx.queue_out = 0;
    s_cmd_queue_ctx.queue_cnt = 0;
    memset(s_cmd_queue_ctx.queue_items, 0, s_cmd_queue_ctx.queue_num * SIZEOF(CMD_QUEUE_ITEM_T));
    tal_mutex_unlock(s_cmd_queue_ctx.mutex);

    return OPRT_OK;
}

/**
 * @brief
 *
 * @return OPERATE_RET
 */
OPERATE_RET gfw_core_cmd_queue_ack(UCHAR_T cmd)
{
    BOOL_T is_instant = FALSE;
    CMD_QUEUE_ITEM_T *item = NULL;

    tal_mutex_lock(s_cmd_queue_ctx.mutex);
    if ((s_cmd_queue_ctx.instant_item.state != ITEM_STATE_IDLE)
        && (s_cmd_queue_ctx.instant_item.data[3] == cmd)) {
        item = &s_cmd_queue_ctx.instant_item;
        is_instant = TRUE;
    } else {
        if (s_cmd_queue_ctx.queue_cnt > 0) {
            item = &s_cmd_queue_ctx.queue_items[s_cmd_queue_ctx.queue_out];
            if ((item->state > ITEM_STATE_IN) && (item->data[3] == cmd)) {
                //found it
            } else {
                item = NULL;
            }
        }
    }

    if (item) {
        //PR_DEBUG("-----------ack clean %02x", cmd);

        item->ctx->rt = OPRT_OK;
        __default_cmd_timeout_cb(item);
        item->state = ITEM_STATE_IDLE;

        if (!is_instant) {
            s_cmd_queue_ctx.queue_cnt --;
            s_cmd_queue_ctx.queue_out = (s_cmd_queue_ctx.queue_out + 1) % s_cmd_queue_ctx.queue_num;
        }
    }
    tal_mutex_unlock(s_cmd_queue_ctx.mutex);

    return OPRT_OK;
}

/**
 * @brief
 *
 * @param queue_num
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET gfw_core_cmd_queue_init(UINT_T queue_num, AUDIO_GFW_TRANS_T *trans , TUYA_UART_NUM_E port)
{
    OPERATE_RET rt = OPRT_OK;

    memset(&s_cmd_queue_ctx, 0, SIZEOF(CMD_QUEUE_CTX_T));
    s_cmd_queue_ctx.queue_items = (CMD_QUEUE_ITEM_T *)Malloc(queue_num * SIZEOF(CMD_QUEUE_ITEM_T));
    TUYA_CHECK_NULL_RETURN(s_cmd_queue_ctx.queue_items, OPRT_MALLOC_FAILED);
    memset(s_cmd_queue_ctx.queue_items, 0, queue_num * SIZEOF(CMD_QUEUE_ITEM_T));
    TUYA_CALL_ERR_RETURN(tal_sw_timer_create(__gfw_timer_check_timeout_cb, &s_cmd_queue_ctx, &s_cmd_queue_ctx.timer));
    TUYA_CALL_ERR_RETURN(tal_mutex_create_init(&s_cmd_queue_ctx.mutex));
    TUYA_CALL_ERR_RETURN(tal_mutex_create_init(&s_cmd_queue_ctx.mutex_instant));

    s_cmd_queue_ctx.port = port;
    s_cmd_queue_ctx.queue_num = queue_num;
    s_cmd_queue_ctx.trans = trans;

    TAL_PR_DEBUG("s_cmd_queue_ctx.port : %d ", s_cmd_queue_ctx.port);
    return rt;
}
