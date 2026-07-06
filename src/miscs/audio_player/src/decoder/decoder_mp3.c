/**
 * @file decoder_mp3.c
 * @brief 
 * @version 0.1
 * @date 2025-11-06
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
#include "decoder_cfg.h"
#include "tal_memory.h"
#include "./helix/include/mp3dec.h"


#define MP3_HEAD_SIZE (10)

typedef struct {
    BOOL_T is_first_frame;
    HMP3Decoder decoder;
    UINT_T id3_size;
} DECODER_MP3_CTX_T;

STATIC INT_T __mp3_find_id3(UCHAR_T *buf)
{
    CHAR_T tag_header[MP3_HEAD_SIZE];
    INT_T tag_size = 0;

    memcpy(tag_header, buf, sizeof(tag_header));

    if (tag_header[0] == 'I' && tag_header[1] == 'D' && tag_header[2] == '3') {
        tag_size = ((tag_header[6] & 0x7F) << 21) | ((tag_header[7] & 0x7F) << 14) | ((tag_header[8] & 0x7F) << 7) | (tag_header[9] & 0x7F);
        PR_DEBUG("ID3 tag_size = %d", tag_size);
        return tag_size + sizeof(tag_header);
    } else {
        // tag_header ignored
        PR_DEBUG("ID3 tag_size n/a");
        return 0;
    }
}

OPERATE_RET decoder_mp3_start(PVOID_T *handle)
{
    DECODER_MP3_CTX_T *ctx = tal_malloc(sizeof(DECODER_MP3_CTX_T));
    if (!ctx) {
        return OPRT_MALLOC_FAILED;
    }

    memset(ctx, 0, sizeof(DECODER_MP3_CTX_T));
    ctx->is_first_frame = TRUE;
    ctx->id3_size = 0;
    ctx->decoder = MP3InitDecoder();
    if (!ctx->decoder) {
        tal_free(ctx);
        return OPRT_COM_ERROR;
    }
    *handle = ctx;
    return OPRT_OK;
}

OPERATE_RET decoder_mp3_stop(PVOID_T handle)
{
    DECODER_MP3_CTX_T *ctx = (DECODER_MP3_CTX_T *)handle;
    if (!ctx) {
        return OPRT_INVALID_PARM;
    }

    if (ctx->decoder) {
        MP3FreeDecoder(ctx->decoder);
        ctx->decoder = NULL;
    }

    tal_free(ctx);

    return OPRT_OK;
}

INT_T decoder_mp3_process(PVOID_T handle, BYTE_T *in_buf, INT_T in_len, BYTE_T *out_buf, INT_T out_size, DECODER_OUTPUT_T *output)
{
    DECODER_MP3_CTX_T *ctx = (DECODER_MP3_CTX_T *)handle;

    if (!ctx) {
        return -1;
    }

    INT_T ret = 0;
    INT_T offset = 0;
    INT_T temp_len = (INT_T)in_len;
    MP3FrameInfo frame_info = {0};

    if(ctx->is_first_frame) {
        if(in_len < MP3_HEAD_SIZE) {
            return in_len;
        }

        if (0 == ctx->id3_size) {
            ctx->id3_size = __mp3_find_id3(in_buf);
        }

        if(in_len < ctx->id3_size) {
            ctx->id3_size -= in_len;
            return 0;
        }

        in_buf += ctx->id3_size;
        in_len -= ctx->id3_size;
        ctx->is_first_frame = FALSE;
    }

DECODE_MORE:
    offset = MP3FindSyncWord(in_buf, in_len);
    if(offset < 0) {
        TAL_PR_ERR("MP3FindSyncWord not find!");
        return 0;
    }

    in_buf += offset;
    in_len -= offset;
    ret = MP3Decode(ctx->decoder, &in_buf, (INT_T *)&in_len, (SHORT_T *)out_buf, 0);
    if(ret != ERR_MP3_NONE) {
        if(ret == ERR_MP3_INDATA_UNDERFLOW || ret == ERR_MP3_MAINDATA_UNDERFLOW) {
            return temp_len;
        } else {
            TAL_PR_ERR("MP3Decode failed, ret=%d, offset=%d, in_len=%d", ret, offset, in_len);
            return 0;
        }
    }

    MP3GetLastFrameInfo(ctx->decoder, &frame_info);
    if(frame_info.nChans) {
        UINT_T used_size = (frame_info.outputSamps * (frame_info.bitsPerSample / 8));

        output->used_size += used_size;
        output->samples += (frame_info.outputSamps/frame_info.nChans);
        output->sample = frame_info.samprate;
        output->datebits = frame_info.bitsPerSample;
        output->channel = frame_info.nChans;

        out_size -= used_size;
        out_buf += used_size;
        if((out_size > used_size) && (in_len > 2)) { // at least 2 bytes for next frame
            temp_len = in_len;
            goto DECODE_MORE;
        }
    }

    return in_len;
}


DECODER_T g_decoder_mp3 = {
    .start = decoder_mp3_start,
    .stop = decoder_mp3_stop,
    .process = decoder_mp3_process
};
