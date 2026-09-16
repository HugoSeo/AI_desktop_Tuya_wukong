/**
 * @file datasink_mem.c
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
#include "datasink_cfg.h"
#include "tal_memory.h"
#include "tal_mutex.h"
#include "tal_system.h"
#include "tuya_ringbuf.h"

typedef struct {
    TUYA_RINGBUFF_T ringbuf;
    MUTEX_HANDLE mutex;
    BOOL_T eof;
    BOOL_T buffering;         // 蓄水中: read() 暂不出数据
    SYS_TIME_T buffering_ts;  // 进入蓄水的时刻, 用于超时兜底
} MEM_DATASINK_CTX_T;

/* 调用方需持有 ctx->mutex */
STATIC VOID __mem_prebuf_arm(MEM_DATASINK_CTX_T *ctx)
{
#if (AI_PLAYER_MEM_PREBUF_BYTES > 0)
    ctx->buffering = TRUE;
    ctx->buffering_ts = tal_system_get_millisecond();
#endif
}

OPERATE_RET datasink_mem_start(CHAR_T *value, PVOID_T *handle)
{
    OPERATE_RET rt = OPRT_OK;
    MEM_DATASINK_CTX_T *ctx = (MEM_DATASINK_CTX_T *)(*handle);

    if(ctx != NULL) {
        tal_mutex_lock(ctx->mutex);
        tuya_ring_buff_reset(ctx->ringbuf);
        ctx->eof = FALSE;
        __mem_prebuf_arm(ctx);
        tal_mutex_unlock(ctx->mutex);

        return OPRT_OK;
    }

    ctx = (MEM_DATASINK_CTX_T *)Malloc(sizeof(MEM_DATASINK_CTX_T));
    if (ctx == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    memset(ctx, 0, sizeof(MEM_DATASINK_CTX_T));
    TUYA_CALL_ERR_RETURN(tal_mutex_create_init(&ctx->mutex));
    #ifdef ENABLE_EXT_RAM
    TUYA_CALL_ERR_RETURN(tuya_ring_buff_create(AI_PLAYER_RINGBUF_SIZE, OVERFLOW_PSRAM_STOP_TYPE, &ctx->ringbuf));
    #else
    TUYA_CALL_ERR_RETURN(tuya_ring_buff_create(AI_PLAYER_RINGBUF_SIZE, OVERFLOW_STOP_TYPE, &ctx->ringbuf));
    #endif

    __mem_prebuf_arm(ctx); // ctx 尚未发布, 无需持锁
    *handle = (PVOID_T)ctx;
    return OPRT_OK;
}

OPERATE_RET datasink_mem_stop(PVOID_T handle)
{
    MEM_DATASINK_CTX_T *ctx = (MEM_DATASINK_CTX_T *)handle;
    if (ctx == NULL) {
        return OPRT_INVALID_PARM;
    }

    tal_mutex_lock(ctx->mutex);
    tuya_ring_buff_reset(ctx->ringbuf);
    tal_mutex_unlock(ctx->mutex);

    return OPRT_OK;
}

OPERATE_RET datasink_mem_exit(PVOID_T handle)
{
    MEM_DATASINK_CTX_T *ctx = (MEM_DATASINK_CTX_T *)handle;
    if (ctx == NULL) {
        return OPRT_INVALID_PARM;
    }

    tal_mutex_lock(ctx->mutex);
    tuya_ring_buff_free(ctx->ringbuf);
    ctx->ringbuf = NULL;
    tal_mutex_unlock(ctx->mutex);

    tal_mutex_release(ctx->mutex);
    Free(ctx);

    return OPRT_OK;
}

OPERATE_RET datasink_mem_feed(PVOID_T handle, UINT8_T *data, UINT_T len)
{
    MEM_DATASINK_CTX_T *ctx = (MEM_DATASINK_CTX_T *)handle;
    if (ctx == NULL) {
        return OPRT_INVALID_PARM;
    }

    if((data == NULL) && (len == 0)) { // eof
        tal_mutex_lock(ctx->mutex);
        ctx->eof = TRUE;
        tal_mutex_unlock(ctx->mutex);
        return OPRT_OK;
    }

    if(data == NULL || len == 0) {
        return OPRT_INVALID_PARM;
    }

    INT_T rt = 0;
    INT_T cnt = 0;

    tal_mutex_lock(ctx->mutex);
    rt = tuya_ring_buff_write(ctx->ringbuf, (CHAR_T *)data, len);
    tal_mutex_unlock(ctx->mutex);

    while(rt != len) {
        tal_system_sleep(10);

        data += rt;
        len -= rt;
        rt = 0;

        tal_mutex_lock(ctx->mutex);
        rt = tuya_ring_buff_write(ctx->ringbuf, data, len);
        tal_mutex_unlock(ctx->mutex);

        if(cnt ++ > 500) {
            PR_ERR("player ring buf write failed %d", rt);
            break;
        }
    }

    return OPRT_OK;
}

OPERATE_RET datasink_mem_read(PVOID_T handle, UINT8_T *data, UINT_T len, UINT_T *out_len)
{
    MEM_DATASINK_CTX_T *ctx = (MEM_DATASINK_CTX_T *)handle;
    if (ctx == NULL || data == NULL || len == 0 || out_len == NULL) {
        return OPRT_INVALID_PARM;
    }

    INT_T rt = 0;
#if (AI_PLAYER_MEM_PREBUF_BYTES > 0)
    UINT_T prebuf_used = 0;
    UINT_T prebuf_wait_ms = 0;
    BOOL_T prebuf_done = FALSE;
    BOOL_T prebuf_eof = FALSE;
#endif

    tal_mutex_lock(ctx->mutex);
#if (AI_PLAYER_MEM_PREBUF_BYTES > 0)
    if(ctx->buffering) {
        UINT_T used = tuya_ring_buff_used_size_get(ctx->ringbuf);
        UINT_T wait_ms = (UINT_T)(tal_system_get_millisecond() - ctx->buffering_ts);
        if(ctx->eof || (used >= AI_PLAYER_MEM_PREBUF_BYTES) || (wait_ms >= AI_PLAYER_MEM_PREBUF_TIMEOUT_MS)) {
            ctx->buffering = FALSE;
            prebuf_done = TRUE; // 打印移到锁外, 不阻塞 feed 侧生产者
            prebuf_used = used;
            prebuf_wait_ms = wait_ms;
            prebuf_eof = ctx->eof;
        } else { // 水位未到: 谎称无数据, 播放线程走既有 sleep 空转路径
            tal_mutex_unlock(ctx->mutex);
            *out_len = 0;
            return OPRT_OK;
        }
    }
#endif
    rt = tuya_ring_buff_read(ctx->ringbuf, (CHAR_T *)data, len);
    if((rt == 0) && !ctx->eof) { // 中途断流: 重新蓄水, 把碎卡合并成一次停顿
        __mem_prebuf_arm(ctx);
    }
    tal_mutex_unlock(ctx->mutex);

#if (AI_PLAYER_MEM_PREBUF_BYTES > 0)
    /* 空超时放行(上游 stall, 随即重新蓄水)不打印, 避免每超时周期刷一行 */
    if(prebuf_done && ((prebuf_used > 0) || prebuf_eof)) {
        PR_NOTICE("mem sink prebuf done: %u bytes in %u ms%s", prebuf_used, prebuf_wait_ms, prebuf_eof ? " (eof)" : "");
    }
#endif

    *out_len = rt;

    if((rt == 0) && (ctx->eof == TRUE)) {
        return OPRT_NOT_FOUND; // eof
    } else {
        return OPRT_OK;
    }
}

DATASINK_T g_datasink_mem = {
    .start = datasink_mem_start,
    .stop  = datasink_mem_stop,
    .exit  = datasink_mem_exit,
    .feed  = datasink_mem_feed,
    .read  = datasink_mem_read,
};
