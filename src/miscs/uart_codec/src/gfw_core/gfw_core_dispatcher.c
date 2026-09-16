/**
 * @file gfw_core_dispatcher.c
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

#include "uni_log.h"
#include "tal_uart.h"
#include "tal_system.h"
#include "tal_workq_service.h"
#include "gfw_core.h"
#include "gfw_core_frame.h"
#include "gfw_core_dispatcher.h"


#define DEF_UART_BUF_SIZE (2048)
#define MAX_DETECT_COUNT (10)

typedef enum {
    DISPATCHER_STATE_SETUP,
    DISPATCHER_STATE_DETECT,
    DISPATCHER_STATE_RECV,
    DISPATCHER_STATE_PROC,
    DISPATCHER_STATE_TEARDOWN,
    DISPATCHER_STATE_IDLE,
    DISPATCHER_STATE_MAX
} DISPATCHER_STATE_E;

typedef struct {
    DISPATCHER_STATE_E state;
    BOOL_T running;
    BYTE_T def_buf[DEF_UART_BUF_SIZE];
    BYTE_T *recv_buf;
    UINT_T recv_in;
    UINT_T buf_size;

    DISPATCHER_CFG_T cfg;
    UINT8_T baud_rate_index;
    UINT8_T detect_cnt;
    TIME_T recv_time;
    BOOL_T incomplete;
} DISPATCHER_CTX_T;

STATIC DISPATCHER_CTX_T s_dispatcher_ctx;

STATIC VOID __dispatcher_locked_cb(VOID *data)
{
    PR_DEBUG("__dispatcher_locked_cb");
    ty_publish_event(EVENT_BAUDRATE_LOCKED, NULL); // GO
}

STATIC VOID __dispatcher_setup(DISPATCHER_CTX_T *ctx)
{
    TAL_PR_DEBUG("__dispatcher_setup");
    if (NULL == ctx->cfg.trans->init) {
        TAL_UART_CFG_T uart_cfg = {0};

        uart_cfg.base_cfg.baudrate = ctx->cfg.baud_rate[ctx->baud_rate_index];
        uart_cfg.base_cfg.databits = TUYA_UART_DATA_LEN_8BIT;
        uart_cfg.base_cfg.flowctrl = TUYA_UART_FLOWCTRL_NONE;
        uart_cfg.base_cfg.parity = TUYA_UART_PARITY_TYPE_NONE;
        uart_cfg.base_cfg.stopbits = TUYA_UART_STOP_LEN_1BIT;
        uart_cfg.rx_buffer_size = DEF_UART_BUF_SIZE; //TY_UART_BUF_SIZE;
        if (ctx->cfg.baud_rate_cnt == 1) {
            uart_cfg.open_mode |= O_BLOCK;
            tal_uart_init(ctx->cfg.port, &uart_cfg);
            ctx->state = DISPATCHER_STATE_RECV;
        } else {
            tal_uart_init(ctx->cfg.port, &uart_cfg);
            ctx->state = DISPATCHER_STATE_DETECT;
            ctx->detect_cnt = MAX_DETECT_COUNT;
        }

    } else {
        ctx->cfg.trans->init();
        ctx->state = DISPATCHER_STATE_RECV;
    }

    if (ctx->cfg.baud_rate_cnt == 1) {
        // OPERATE_RET rt;
        // rt = tal_workq_schedule(WORKQ_SYSTEM, __dispatcher_locked_cb, NULL);
        // PR_DEBUG("__dispatcher_locked:%d",rt);
        ty_publish_event(EVENT_BAUDRATE_LOCKED, NULL);
        PR_DEBUG("__dispatcher_locked");
    }
}

STATIC VOID __dispatcher_detect(DISPATCHER_CTX_T *ctx)
{
    PR_DEBUG("detect %d", ctx->cfg.baud_rate[ctx->baud_rate_index]);

    ctx->cfg.detect_send();
    ctx->state = DISPATCHER_STATE_RECV;
}

STATIC VOID __dispatcher_recv(DISPATCHER_CTX_T *ctx)
{
    if (ctx->detect_cnt) {
        if (--ctx->detect_cnt == 1) {
            ctx->state = DISPATCHER_STATE_TEARDOWN;
            return ;
        }
    }

    INT_T count = 0;

    if (ctx->cfg.trans->recv) {
        count = ctx->cfg.trans->recv(ctx->recv_buf + ctx->recv_in, ctx->buf_size - ctx->recv_in);
    } else {
        count = tal_uart_read(ctx->cfg.port, ctx->recv_buf + ctx->recv_in, ctx->buf_size - ctx->recv_in);
    }
    if (count > 0) {
        //tuya_debug_hex_dump("-----------uart recv ", 64, ctx->recv_buf + ctx->recv_in, count);
        ctx->recv_in += count;
        if (DISPATCHER_STATE_IDLE != ctx->state) {
            ctx->state = DISPATCHER_STATE_PROC;
        }
    }
}

STATIC VOID __dispatcher_proc(DISPATCHER_CTX_T *ctx)
{
    UINT_T offset = 0;
    USHORT_T len = 0;
    AUDIO_GFW_CMD_DATA_T cmd_data;

    while (ctx->recv_in >= (offset + MIN_FRAME_LEN)) {
        if ((0x55 != ctx->recv_buf[offset]) || (0xAA != ctx->recv_buf[offset + 1])) {
            // PR_ERR("Err Head:%02x ctx state%d", ctx->recv_buf[offset], ctx->state);
            if (DISPATCHER_STATE_IDLE == ctx->state) {
                return;
            }
            offset++;
            continue; // magic not match
        }

        len = (ctx->recv_buf[offset + 4] << 8) + ctx->recv_buf[offset + 5];
        if (len + MIN_FRAME_LEN > TY_UART_BUF_SIZE) {
            PR_ERR("Err Len:%u", len);
            offset++;
            continue; // len invalid
        }

        if ((len + MIN_FRAME_LEN) > (ctx->recv_in - offset)) {
            PR_DEBUG("in complete, len:%u, recv_in:%u, offset:%u", len, ctx->recv_in, offset);
            TIME_T now = tal_time_get_posix();
            if (!ctx->incomplete) {
                PR_DEBUG("first incomplete");
                ctx->recv_time = now;
                ctx->incomplete = TRUE;
            } else if ((now - ctx->recv_time >= 2) && (now - ctx->recv_time < 0xFFFF)) {
                PR_NOTICE("discard data");
                offset = 0;
                ctx->recv_in = 0;
                ctx->incomplete = FALSE;
                ctx->recv_time = 0;
            } else {
                PR_DEBUG("wait more data");
                ctx->recv_time = now;
            }
            break; // not complete
        }

        ctx->recv_time = 0;
        ctx->incomplete = FALSE;
        if (ctx->recv_buf[offset + 6 + len] != gfw_core_frame_calc_crc(ctx->recv_buf + offset, 6 + len)) {
            PR_ERR("Err Crc:%02x", ctx->recv_buf[offset + 6 + len]);
            offset++;
            continue; // crc not match
        }

        cmd_data.ver = ctx->recv_buf[offset + 2];
        cmd_data.cmd = ctx->recv_buf[offset + 3];
        cmd_data.data = ctx->recv_buf + offset + 6;
        cmd_data.len = len;
        if (ctx->detect_cnt) {
            if (OPRT_OK == ctx->cfg.detect_recv(ctx->cfg.baud_rate[ctx->baud_rate_index], &cmd_data)) {
                PR_DEBUG("lock baud rate %d", ctx->cfg.baud_rate[ctx->baud_rate_index]);
                ctx->running = FALSE;
                return ;
            }
        } else {

            if (NULL == ctx->cfg.cmd_handler){
                PR_ERR("cfg.cmd_handler is null");
            }else{
                ctx->cfg.cmd_handler(&cmd_data);
            }

            
        }

        offset += (MIN_FRAME_LEN + len);
    }

    if (offset > 0) {
        if (ctx->recv_in > offset) {
            memmove(ctx->recv_buf, ctx->recv_buf + offset, ctx->recv_in - offset);
        }
        ctx->recv_in = ctx->recv_in - offset;
    }

    if ((ctx->recv_in > MIN_FRAME_LEN) && (ctx->recv_buf == ctx->def_buf)) {
        len = (ctx->recv_buf[4] << 8) + ctx->recv_buf[5];
        if ((len + MIN_FRAME_LEN) > DEF_UART_BUF_SIZE) {
            PR_NOTICE("big frame[%d], realloc", len + MIN_FRAME_LEN);
            ctx->recv_buf = Malloc(TY_UART_BUF_SIZE);
            if (!ctx->recv_buf) {
                ctx->recv_buf = ctx->def_buf;
            } else {
                memcpy(ctx->recv_buf, ctx->def_buf, ctx->recv_in);
                ctx->buf_size = TY_UART_BUF_SIZE;
            }
        }
    }

    if (DISPATCHER_STATE_PROC == ctx->state) {
        ctx->state = DISPATCHER_STATE_RECV; // continue to recv more data
    }
}

STATIC VOID __dispatcher_teardown(DISPATCHER_CTX_T *ctx)
{
    ctx->baud_rate_index = (ctx->baud_rate_index + 1) % ctx->cfg.baud_rate_cnt;
    ctx->state = DISPATCHER_STATE_SETUP;
    tal_uart_deinit(ctx->cfg.port);
}

STATIC VOID __dispatcher_idle(DISPATCHER_CTX_T *ctx)
{
    tal_uart_deinit(ctx->cfg.port);
}

STATIC OPERATE_RET __dispatcher_run(DISPATCHER_CTX_T *ctx)
{
    // TAL_PR_DEBUG("__dispatcher_run.state %d", ctx->state);
    
    ctx->recv_buf = ctx->def_buf;
    ctx->buf_size = DEF_UART_BUF_SIZE;
    while (ctx->running) {
        // TAL_PR_DEBUG("__dispatcher_run.state %d", ctx->state);
        switch (ctx->state) {
        case DISPATCHER_STATE_SETUP:
            __dispatcher_setup(ctx);
            break;
        case DISPATCHER_STATE_DETECT:
            __dispatcher_detect(ctx);
            break;
        case DISPATCHER_STATE_RECV:
            __dispatcher_recv(ctx);
            break;
        case DISPATCHER_STATE_PROC:
            __dispatcher_proc(ctx);
            break;
        case DISPATCHER_STATE_TEARDOWN:
            __dispatcher_teardown(ctx);
            break;
        case DISPATCHER_STATE_IDLE:
            __dispatcher_idle(ctx);
            break;
        default:
            break;
        }

        tal_system_sleep(ctx->cfg.cmd_interval);
    }

    // TAL_PR_DEBUG("__dispatcher_running: %d", ctx->running);

    if (ctx->recv_buf != ctx->def_buf) {
        Free(ctx->recv_buf);
    }

    if (ctx->state != DISPATCHER_STATE_TEARDOWN) {
        __dispatcher_teardown(ctx);
    }

    return OPRT_OK;
}

/**
 * @brief
 *
 * @return OPERATE_RET
 */
OPERATE_RET gfw_core_dispatcher_start(DISPATCHER_CFG_T *cfg)
{
    memset(&s_dispatcher_ctx, 0, SIZEOF(DISPATCHER_CTX_T));
    memcpy(&s_dispatcher_ctx.cfg, cfg, SIZEOF(DISPATCHER_CFG_T));
    s_dispatcher_ctx.state = DISPATCHER_STATE_SETUP;
    s_dispatcher_ctx.running = TRUE;

    return __dispatcher_run(&s_dispatcher_ctx);
}

/**
 * @brief
 *
 * @return OPERATE_RET
 */
OPERATE_RET gfw_core_dispatcher_stop(VOID)
{
    s_dispatcher_ctx.running = FALSE;

#if defined(ENABLE_CELLULAR) && (ENABLE_CELLULAR == 1)
    tal_uart_finish_wait_rx(s_dispatcher_ctx.cfg.port);
#else
    tal_uart_deinit(s_dispatcher_ctx.cfg.port);
#endif

    return OPRT_OK;
}

/**
 * @brief
 *
 * @return none
 */
VOID gfw_core_dispatcher_pause(VOID)
{
    TAL_PR_DEBUG("gfw_core_dispatcher_pause");
    s_dispatcher_ctx.state = DISPATCHER_STATE_IDLE;
}

/**
 * @brief
 *
 * @return none
 */
VOID gfw_core_dispatcher_resume(VOID)
{
    if (DISPATCHER_STATE_IDLE == s_dispatcher_ctx.state) {
        TAL_PR_DEBUG("gfw_core_dispatcher_resume");
        s_dispatcher_ctx.state = DISPATCHER_STATE_SETUP;
        do {
            tal_system_sleep(s_dispatcher_ctx.cfg.cmd_interval);
        } while (s_dispatcher_ctx.state == DISPATCHER_STATE_SETUP);
    }
}