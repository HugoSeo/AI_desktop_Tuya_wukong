/**
 * @file datasink_file.c
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
#include "tal_fs.h"
#include "tal_memory.h"
#include "datasink_cfg.h"

typedef struct {
    TUYA_FILE file;
    INT_T file_size;
    INT_T offset;
} FILE_DATASINK_CTX_T;

OPERATE_RET datasink_file_start(CHAR_T *value, PVOID_T *handle)
{
    FILE_DATASINK_CTX_T *ctx = (FILE_DATASINK_CTX_T *)(*handle);

    if(ctx) {
        if(ctx->file) {
            tal_fclose(ctx->file);
            ctx->file = NULL;
            ctx->file_size = 0;
            ctx->offset = 0;
        }
    } else {
        ctx = (FILE_DATASINK_CTX_T *)Malloc(sizeof(FILE_DATASINK_CTX_T));
        if (ctx == NULL) {
            return OPRT_MALLOC_FAILED;
        }
    }

    memset(ctx, 0, sizeof(FILE_DATASINK_CTX_T));

    ctx->file_size = tal_fgetsize(value);
    ctx->file = tal_fopen(value, "r");
    *handle = ctx;
    return ctx->file ? OPRT_OK : OPRT_FILE_OPEN_FAILED;
}

OPERATE_RET datasink_file_stop(PVOID_T handle)
{
    FILE_DATASINK_CTX_T *ctx = (FILE_DATASINK_CTX_T *)handle;
    if(!ctx) {
        return OPRT_INVALID_PARM;
    }

    if(ctx->file) {
        tal_fclose(ctx->file);
        ctx->file = NULL;
        ctx->file_size = 0;
    }

    return OPRT_OK;
}

OPERATE_RET datasink_file_exit(PVOID_T handle)
{
    datasink_file_stop(handle);
    Free(handle);

    return OPRT_OK;
}

OPERATE_RET datasink_file_read(PVOID_T handle, UINT8_T *data, UINT_T len, UINT_T *out_len)
{
    FILE_DATASINK_CTX_T *ctx = (FILE_DATASINK_CTX_T *)handle;
    if(!ctx) {
        return OPRT_INVALID_PARM;
    }

    INT_T rt = 0;

    rt = tal_fread(data, len, ctx->file);
    if(rt == 0) {
        *out_len = 0;
        PR_DEBUG("eof file size %d offset %d", ctx->file_size, ctx->offset);
        return OPRT_NOT_FOUND; // eof
    } else if(rt > 0) {
        *out_len = rt;
        ctx->offset += rt;
        return OPRT_OK;
    } else {
        PR_ERR("file read err %d", rt);
        return OPRT_FILE_READ_FAILED;
    }
}


/* provide offset/length progress for file sink */
OPERATE_RET datasink_file_get_progress(PVOID_T handle, UINT_T *offset, UINT_T *length)
{
    FILE_DATASINK_CTX_T *ctx = (FILE_DATASINK_CTX_T *)handle;
    if(!ctx || offset == NULL || length == NULL) {
        return OPRT_INVALID_PARM;
    }

    *offset = ctx->offset;
    *length = ctx->file_size;

    return OPRT_OK;
}

DATASINK_T g_datasink_file = {
    .start = datasink_file_start,
    .stop  = datasink_file_stop,
    .exit  = datasink_file_exit,
    .feed  = NULL,
    .read  = datasink_file_read,
    .get_progress = datasink_file_get_progress,
};
