/**
 * @file ai_player_resample.c
 * @brief 
 * @version 0.1
 * @date 2025-09-25
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
#include "tal_memory.h"
#include "ai_player_resample.h"
#include "resample_fixed.h"

typedef struct {
    UINT32_T sample_rate;
    UINT8_T sample_bits;
    UINT8_T channel;
} RESAMPLE_CTX_T;

STATIC RESAMPLE_CTX_T s_resample_ctx = {0};

OPERATE_RET ai_player_resample_init(UINT32_T sample_rate, UINT8_T sample_bits, UINT8_T channel)
{
    s_resample_ctx.sample_rate = sample_rate;
    s_resample_ctx.sample_bits = sample_bits;
    s_resample_ctx.channel = channel;

    return OPRT_OK;
}

OPERATE_RET ai_player_resample_deinit(VOID)
{
    return OPRT_OK;
}

OPERATE_RET ai_player_resample_process(BYTE_T *in_buf, DECODER_OUTPUT_T *in_cfg, BYTE_T *out_buf, INT_T *out_size)
{
    if(in_buf == NULL || out_buf == NULL || out_size == NULL || *out_size <= 0 || in_cfg == NULL) {
        return OPRT_INVALID_PARM;
    }
    if(in_cfg->samples == 0 || in_cfg->channel == 0 || in_cfg->sample_rate == 0) {
        return OPRT_INVALID_PARM;
    }

    size_t out_frames_out = 0;
    OPERATE_RET ret = -1;
    BYTE_T *src = in_buf;
    BYTE_T *src_alloc = NULL;

    /* 原地重叠保护:
     * 升采样 (in_rate < 16000) 时 out_frames > in_frames, in_buf 与 out_buf 若指向同一缓冲区,
     * resample_to_16k_fixed 正向写出会先覆盖尚未读完的输入样本, 导致级联失真 (听感"叭叭"杂音).
     * 这里在重叠且为升采样时分配输入副本, 让读写互不干扰. */
    if (in_buf == out_buf && in_cfg->sample_rate < 16000) {
        UINT_T in_bytes = (UINT_T)in_cfg->samples * (UINT_T)in_cfg->channel * sizeof(int16_t);
        src_alloc = (BYTE_T *)tal_malloc(in_bytes);
        if (src_alloc == NULL) {
            return OPRT_MALLOC_FAILED;
        }
        memcpy(src_alloc, in_buf, in_bytes);
        src = src_alloc;
    }

    ret = resample_to_16k_fixed((const int16_t *)src, in_cfg->samples, in_cfg->sample_rate, in_cfg->channel, (int16_t *)out_buf, &out_frames_out);
    if(ret == 0) {
        *out_size = out_frames_out * 2;
    }

    if (src_alloc != NULL) {
        tal_free(src_alloc);
    }

    // PR_DEBUG("resample in %d out %d", in_cfg->samples, out_frames_out);

    return ret;
}
