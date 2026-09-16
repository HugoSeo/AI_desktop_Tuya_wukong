/**
 * @file decoder_wav.c
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
#include "tal_memory.h"
#include "decoder_cfg.h"

#define WAV_HEAD_SIZE (44)

/* RIFF chunk 头长度: id(4) + size(4) */
#define WAV_CHUNK_HDR_LEN  8
/* fmt chunk 数据区最小长度(PCM): format(2)+channels(2)+rate(4)+byterate(4)+blockalign(2)+bps(2) */
#define WAV_FMT_MIN_LEN    16
/* 本解码器仅支持 WAVE 中为未压缩 PCM 的 data 区; ADPCM(0x11)/浮点(0x3) 等需另接解码器 */
#define WAVE_FORMAT_PCM    0x0001U

typedef struct {
    DECODER_OUTPUT_T output;
    BOOL_T is_first_frame;
} DECODER_WAV_CTX_T;

/**
 * @brief 从 WAV 头解析 fmt 字段并定位 data chunk 起始偏移
 * @param[in] buf 输入数据(应包含完整 RIFF/WAVE 头及 fmt/data chunk)
 * @param[in] in_len buf 长度
 * @param[out] output 解析出的 channel/sample/datebits 写入此处
 * @return >0 PCM 数据相对 buf 的起始字节偏移; 0 缓冲区中尚未出现完整 data chunk(等待更多输入);
 *         -1 输入非法或 fmt 字段无效
 * @note 必须按 RIFF chunk 链遍历, 不能用固定偏移. 实际 WAV 在 RIFF/WAVE 之后允许出现
 *       JUNK / LIST / bext / FLLR 等任意 padding chunk, 真正的 fmt chunk 位置不固定.
 *       原实现用 22/24/34 这三个固定偏移会读到 JUNK 的 0x00 填充, 把 channel/datebits 解析成 0,
 *       导致后续 samples = in_len*8/(0*0) = 0, decoder 永远不消费数据.
 */
STATIC INT_T __wav_find_data_offset(UCHAR_T *buf, INT_T in_len, DECODER_OUTPUT_T *output)
{
    if (!buf || in_len <= 0 || !output) {
        return -1;
    }

    /* RIFF/WAVE 12 字节固定头 */
    if (in_len < 12) {
        return 0;
    }
    if (memcmp(&buf[0], "RIFF", 4) != 0 || memcmp(&buf[8], "WAVE", 4) != 0) {
        PR_ERR("invalid wav header");
        return -1;
    }

    INT_T pos = 12;
    BOOL_T fmt_parsed = FALSE;
    while (pos + WAV_CHUNK_HDR_LEN <= in_len) {
        UCHAR_T *id = &buf[pos];
        UINT32_T chunk_size = (UINT32_T)buf[pos + 4]         |
                              ((UINT32_T)buf[pos + 5] << 8)  |
                              ((UINT32_T)buf[pos + 6] << 16) |
                              ((UINT32_T)buf[pos + 7] << 24);

        if (id[0] == 'f' && id[1] == 'm' && id[2] == 't' && id[3] == ' ') {
            INT_T data_off = pos + WAV_CHUNK_HDR_LEN;
            if (chunk_size < WAV_FMT_MIN_LEN || data_off + WAV_FMT_MIN_LEN > in_len) {
                /* fmt 数据不完整, 让上层喂更多数据 */
                return 0;
            }
            //判断WAV格式是否为PCM，防止崩溃
            UINT32_T audio_fmt = (UINT32_T)buf[data_off] |
                                 ((UINT32_T)buf[data_off + 1] << 8);
            if (audio_fmt != WAVE_FORMAT_PCM) {
                PR_ERR("unsupported wav format tag %u (need PCM=1); e.g. ADPCM is 17",
                       (UINT_T)audio_fmt);
                return -1;
            }
            UINT32_T channels = (UINT32_T)buf[data_off + 2] |
                                ((UINT32_T)buf[data_off + 3] << 8);
            UINT32_T rate     = (UINT32_T)buf[data_off + 4]        |
                                ((UINT32_T)buf[data_off + 5] << 8) |
                                ((UINT32_T)buf[data_off + 6] << 16)|
                                ((UINT32_T)buf[data_off + 7] << 24);
            UINT32_T bps      = (UINT32_T)buf[data_off + 14] |
                                ((UINT32_T)buf[data_off + 15] << 8);
            if (channels == 0 || rate == 0 || bps == 0) {
                PR_ERR("invalid wav fmt: ch=%u rate=%u bps=%u",
                       (UINT_T)channels, (UINT_T)rate, (UINT_T)bps);
                return -1;
            }
            output->channel  = (UINT8_T)channels;
            output->sample_rate = (UINT32_T)rate;
            output->sample_bits = (UINT8_T)bps;
            fmt_parsed = TRUE;
        } else if (id[0] == 'd' && id[1] == 'a' && id[2] == 't' && id[3] == 'a') {
            if (!fmt_parsed) {
                PR_ERR("wav data chunk before fmt");
                return -1;
            }
            // TAL_PR_NOTICE("wav channel=%d sample=%d datebits=%d", output->channel, output->sample, output->datebits);
            return pos + WAV_CHUNK_HDR_LEN;
        }

        /* 跳过当前 chunk 头 + 数据. 防止 chunk_size 异常导致回退或 32-bit 环绕越界. */
        UINT32_T advance = WAV_CHUNK_HDR_LEN + chunk_size;
        if (advance < (UINT32_T)WAV_CHUNK_HDR_LEN || (UINT32_T)pos + advance < (UINT32_T)pos) {
            PR_ERR("bad chunk size %u at pos %d", (UINT_T)chunk_size, pos);
            return -1;
        }
        pos += (INT_T)advance;
    }

    /* 当前缓冲区中没扫到 data chunk, 等待下一帧补齐 */
    return 0;
}

OPERATE_RET decoder_wav_start(PVOID_T *handle)
{
    DECODER_WAV_CTX_T *ctx = tal_malloc(sizeof(DECODER_WAV_CTX_T));
    if (!ctx) {
        return OPRT_MALLOC_FAILED;
    }

    memset(ctx, 0, sizeof(DECODER_WAV_CTX_T));
    ctx->is_first_frame = TRUE;
    *handle = (PVOID_T)ctx;
    return OPRT_OK;
}

OPERATE_RET decoder_wav_stop(PVOID_T handle)
{
    DECODER_WAV_CTX_T *ctx = (DECODER_WAV_CTX_T *)handle;
    if (!ctx) {
        return OPRT_INVALID_PARM;
    }

    tal_free(ctx);
    return OPRT_OK;
}

INT_T decoder_wav_process(PVOID_T handle, BYTE_T *in_buf, INT_T in_len, BYTE_T *out_buf, INT_T out_size, DECODER_OUTPUT_T *output)
{
    DECODER_WAV_CTX_T *ctx = (DECODER_WAV_CTX_T *)handle;
    if (!ctx) {
        return -1;
    }

    INT_T offset = 0;
    if(ctx->is_first_frame) {
        if (in_len < WAV_HEAD_SIZE) {
            return in_len;
        }

        offset = __wav_find_data_offset((UCHAR_T *)in_buf, in_len, &ctx->output);
        if (offset < 0) {
            return -2;
        } else if(offset == 0) {
            return in_len;
        }

        in_len -= offset;
        ctx->is_first_frame = FALSE;
    }

    memcpy(output, &ctx->output, sizeof(DECODER_OUTPUT_T));

    if(in_len >= out_size) {
        output->samples = (out_size * 8) / (output->channel * output->sample_bits);
    } else if(in_len > 0) {
        output->samples = (in_len * 8) / (output->channel * output->sample_bits);
    } else {
        output->samples = 0;
        return 0;
    }

    out_size = output->samples * output->channel * output->sample_bits / 8;
    memcpy(out_buf, in_buf + offset, out_size);
    output->used_size = out_size;
    in_len -= out_size;

    return in_len;
}


DECODER_T g_decoder_wav = {
    .start = decoder_wav_start,
    .stop = decoder_wav_stop,
    .process = decoder_wav_process
};
