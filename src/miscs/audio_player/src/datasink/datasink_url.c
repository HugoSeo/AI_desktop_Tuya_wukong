/**
 * @file datasink_url.c
 * @brief 
 * @version 0.1
 * @date 2025-09-23
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

#include "uni_log.h"
#include "httpc.h"
#include "mix_method.h"
#include "tal_memory.h"
#include "tal_system.h"
#include "tal_time_service.h"
#include "datasink_cfg.h"
#include "tal_workq_service.h"

typedef enum {
    URLSINK_STATUS_INIT,
    URLSINK_STATUS_CONNECT,
    URLSINK_STATUS_RECV,
    URLSINK_STATUS_EXIT
} URLSINK_STATUS_T;

typedef struct {
    http_session_t session;
    URLSINK_STATUS_T status;
    UINT_T length;
    UINT_T offset;
    BOOL_T chunked;
    BOOL_T eof;
    CHAR_T *url;
    TIME_T download_start_s;
} URL_DATASINK_CTX_T;

STATIC VOID __close_http_session(URL_DATASINK_CTX_T *ctx)
{
    if (ctx && ctx->session) {
        http_close_session(&ctx->session);
        ctx->session = NULL;
    }
}

STATIC VOID __http_connect(VOID *data)
{
    URL_DATASINK_CTX_T *ctx = (URL_DATASINK_CTX_T *)data;
    if(!ctx || (ctx->status == URLSINK_STATUS_INIT)) {
        return;
    }

    OPERATE_RET rt = OPRT_OK;
    http_resp_t *response = NULL;
    http_req_t request = {
        .type        = HTTP_GET,
        .resource    = ctx->url,
        .version     = HTTP_VER_1_1
    };
    http_hdr_field_sel_t field_flags = (HDR_ADD_DEFAULT_USER_AGENT|HDR_ADD_CONN_KEEP_ALIVE);
    if(ctx->offset > 0) {
        request.download_offset = ctx->offset;
        request.download_size = ctx->length;
        field_flags |= HRD_ADD_DOWNLOAD_RANGE;
        PR_DEBUG("url sink resume from %d length %d", ctx->offset, ctx->length);
    }

    // A previous read failure may leave an old session behind.
    __close_http_session(ctx);

    TUYA_CALL_ERR_GOTO(http_open_session(&ctx->session, ctx->url, 0, 0), ERR_EXIT);
    TUYA_CALL_ERR_GOTO(http_prepare_req(ctx->session, &request, field_flags), ERR_EXIT);
    TUYA_CALL_ERR_GOTO(http_send_request(ctx->session, &request, 0), ERR_EXIT);
    TUYA_CALL_ERR_GOTO(http_get_response_hdr(ctx->session, &response), ERR_EXIT);

    if ((response->status_code != 200) && (response->status_code != 206)) {
        if(HTTP_RESP_REDIR(response->status_code) && response->location != NULL) { //redirect
            PR_DEBUG("url sink redirect to %s", response->location);

            if(ctx->url) {
                Free(ctx->url);
                ctx->url = NULL;
            }
            ctx->url = mm_strdup(response->location);
            TUYA_CHECK_NULL_GOTO(ctx->url, ERR_EXIT);

            __close_http_session(ctx);

            TUYA_CALL_ERR_GOTO(tal_workq_schedule(WORKQ_HIGHTPRI, __http_connect, (VOID *)ctx), ERR_EXIT);
        } else {
            PR_ERR("url sink err status_code: %d", response->status_code);
            goto ERR_EXIT;
        }
    } else {
        if(response->status_code == 200) {
            ctx->length = response->content_length;
            ctx->chunked = response->chunked;
            PR_DEBUG("url sink file size %d chunked %d", response->content_length, response->chunked);
        }else if(response->status_code == 206) {
            // in resume mode, server may not return content-length
            if(response->content_length > 0) {
                ctx->length = ctx->offset + response->content_length;
            }
            ctx->chunked = response->chunked;
            PR_DEBUG("url sink resume file size %d chunked %d", ctx->length, response->chunked);
        }

        http_set_timeout(ctx->session, AI_PLAYER_HTTP_YIELD_MS);
        ctx->status = URLSINK_STATUS_RECV;
        ctx->download_start_s = tal_time_get_posix();
    }

    return;

ERR_EXIT:
    __close_http_session(ctx);
    ctx->status = URLSINK_STATUS_EXIT;
}

OPERATE_RET datasink_url_start(CHAR_T *value, PVOID_T *handle)
{
    OPERATE_RET rt = OPRT_OK;
    URL_DATASINK_CTX_T *ctx = (URL_DATASINK_CTX_T *)(*handle);

    if(ctx) {
        __close_http_session(ctx);
        if(ctx->url) {
            Free(ctx->url);
            ctx->url = NULL;
        }
    } else {
        ctx = (URL_DATASINK_CTX_T *)Malloc(sizeof(URL_DATASINK_CTX_T));
        if (ctx == NULL) {
            return OPRT_MALLOC_FAILED;
        }
        *handle = ctx;
    }

    memset(ctx, 0, sizeof(URL_DATASINK_CTX_T));

    ctx->url = mm_strdup(value);
    if(!ctx->url) {
        return OPRT_MALLOC_FAILED;
    }

    ctx->download_start_s = tal_time_get_posix();
    ctx->status = URLSINK_STATUS_CONNECT;
    ctx->eof = FALSE;
    rt = tal_workq_schedule(WORKQ_HIGHTPRI, __http_connect, (VOID *)ctx);
    if (rt != OPRT_OK) {
        ctx->status = URLSINK_STATUS_EXIT;
    }

    return rt;
}

OPERATE_RET datasink_url_stop(PVOID_T handle)
{
    URL_DATASINK_CTX_T *ctx = (URL_DATASINK_CTX_T *)handle;
    if(!ctx) {
        return OPRT_INVALID_PARM;
    }

    while (ctx->status == URLSINK_STATUS_CONNECT) {
        tal_system_sleep(AI_PLAYER_HTTP_YIELD_MS);
    }

    ctx->status = URLSINK_STATUS_INIT;
    ctx->eof = FALSE;
    __close_http_session(ctx);
    if(ctx->url) {
        Free(ctx->url);
        ctx->url = NULL;
    }

    return OPRT_OK;
}

OPERATE_RET datasink_url_exit(PVOID_T handle)
{
    datasink_url_stop(handle);
    Free(handle);

    return OPRT_OK;
}

OPERATE_RET datasink_url_read(PVOID_T handle, UINT8_T *data, UINT_T len, UINT_T *out_len)
{
    URL_DATASINK_CTX_T *ctx = (URL_DATASINK_CTX_T *)handle;
    if(!ctx) {
        return OPRT_INVALID_PARM;
    }

    *out_len = 0;

    if(ctx->status <= URLSINK_STATUS_CONNECT) {
        tal_system_sleep(AI_PLAYER_HTTP_YIELD_MS);
        return OPRT_OK;
    } else if(ctx->status == URLSINK_STATUS_EXIT) {
        if(ctx->chunked) { // in chunked mode, do not support retry
            return OPRT_NETWORK_ERROR;
        } else if((ctx->download_start_s + AI_PLAYER_HTTP_TIMEOUT_S) < tal_time_get_posix()) { // timeout
            return OPRT_NETWORK_ERROR;
        } else {
            ctx->status = URLSINK_STATUS_CONNECT;
            return tal_workq_schedule(WORKQ_HIGHTPRI, __http_connect, (VOID *)ctx);
        }
    }

    if(ctx->eof || ((ctx->offset >= ctx->length) && !ctx->chunked)) {
        if(ctx->session) {
            http_close_session(&ctx->session);
            ctx->session = NULL;
        }
        ctx->eof = TRUE;
        return OPRT_NOT_FOUND; // eof
    }

    INT_T ret = http_read_content(ctx->session, data, len);
    if(ret > 0) {
        *out_len = ret;
        ctx->offset += ret;
        ctx->download_start_s = tal_time_get_posix();
        return OPRT_OK;
    } else if(ret == 0) {
        ctx->eof = TRUE;
        return OPRT_NOT_FOUND; // eof
    } else if(ret == -WM_E_HTTPC_SOCKET_TIMEOUT){
        return OPRT_OK;
    } else {
        PR_ERR("url sink err offset %d length %d start_s %d", ctx->offset, ctx->length, ctx->download_start_s);
        __close_http_session(ctx);
        ctx->status = URLSINK_STATUS_EXIT; // error, let connect again
        return OPRT_OK;
    }
}


/*
 * Query download/playback progress for URL sink
 */
OPERATE_RET datasink_url_get_progress(PVOID_T handle, UINT_T *offset, UINT_T *length)
{
    URL_DATASINK_CTX_T *ctx = (URL_DATASINK_CTX_T *)handle;
    if (!ctx || offset == NULL || length == NULL) {
        return OPRT_INVALID_PARM;
    }

    *offset = ctx->offset;
    *length = ctx->length;
    return OPRT_OK;
}

DATASINK_T g_datasink_url = {
    .start = datasink_url_start,
    .stop  = datasink_url_stop,
    .exit  = datasink_url_exit,
    .feed  = NULL,
    .read  = datasink_url_read,
    .get_progress = datasink_url_get_progress,
};
