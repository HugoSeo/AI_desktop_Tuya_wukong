/**
 * @file decoder_opus_vbr.c
 * @brief Opus VBR (Variable Bit Rate) decoder implementation
 * @version 0.1
 * @date 2026-01-13
 *
 * @copyright Copyright (c) 2026 Tuya Inc. All Rights Reserved.
 *
 * @description
 * Opus VBR format: [Length(2 bytes, big-endian)] + [Data(Length bytes)]
 * Example: 00 0A xx xx xx xx xx xx xx xx xx xx
 *          ^^^^^  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 *          10     10 bytes of opus data
 */

#include "uni_log.h"
#include "decoder_cfg.h"
#include "tal_memory.h"
#include <opus.h>
#include <opus_defines.h>
#include <string.h>

/* Downlink decode rate, selected by backend: public xiaozhi (tenclass) TTS is 24k,
 * tuya gateway (cube) TTS is 16k. The FULL player resamples decoded PCM to its output
 * rate, so only opus_decoder_init() must match the stream. NOTE: lite mode
 * (AI_PLAYER_LITE) does NOT resample -- there the consumer must be opened at
 * this same rate. */
#ifndef DEFAULT_SAMPLE_RATE
#if defined(CUBE_BACKEND_XIAOZHI) && (CUBE_BACKEND_XIAOZHI == 1)
#define DEFAULT_SAMPLE_RATE 24000
#else
#define DEFAULT_SAMPLE_RATE 16000
#endif
#endif
#define DEFAULT_CHANNELS 1
#define VBR_HEADER_SIZE 2  // 2 bytes for length field
#define MAX_FRAME_SIZE 1500  // Maximum opus frame size

#define OPUS_VBR_PR_DEBUG PR_TRACE

typedef struct {
    OpusDecoder *decoder;
    INT_T sample_rate;
    INT_T channels;
    BOOL_T initialized;
} DECODER_OPUS_VBR_CTX_T;

STATIC OPERATE_RET decoder_opus_vbr_start(PVOID_T *handle)
{
    DECODER_OPUS_VBR_CTX_T *ctx = tal_malloc(sizeof(DECODER_OPUS_VBR_CTX_T));
    if (!ctx) {
        return OPRT_MALLOC_FAILED;
    }

    memset(ctx, 0, sizeof(DECODER_OPUS_VBR_CTX_T));

    ctx->sample_rate = DEFAULT_SAMPLE_RATE;
    ctx->channels = DEFAULT_CHANNELS;

    // Initialize Opus decoder
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM==1)
    ctx->decoder = tal_psram_malloc(opus_decoder_get_size(ctx->channels));
#else
    ctx->decoder = tal_malloc(opus_decoder_get_size(ctx->channels));
#endif
    if (!ctx->decoder) {
        tal_free(ctx);
        return OPRT_MALLOC_FAILED;
    }

    if (opus_decoder_init(ctx->decoder, ctx->sample_rate, ctx->channels) != 0) {
        tal_free(ctx->decoder);
        tal_free(ctx);
        return OPRT_COM_ERROR;
    }

    ctx->initialized = TRUE;
    *handle = ctx;

    OPUS_VBR_PR_DEBUG("Opus VBR decoder started: %dHz, %dch",
                      ctx->sample_rate, ctx->channels);

    return OPRT_OK;
}

STATIC OPERATE_RET decoder_opus_vbr_stop(PVOID_T handle)
{
    DECODER_OPUS_VBR_CTX_T *ctx = (DECODER_OPUS_VBR_CTX_T *)handle;
    if (!ctx) {
        return OPRT_INVALID_PARM;
    }

    if (ctx->decoder) {
        opus_decoder_destroy(ctx->decoder);
        ctx->decoder = NULL;
    }

    tal_free(ctx);
    return OPRT_OK;
}

/**
 * @brief Parse VBR frame length from big-endian 2-byte header
 * @param header Pointer to 2-byte header
 * @return Frame length in bytes
 */
STATIC INLINE INT_T parse_frame_length(BYTE_T *header)
{
    // Big-endian: first byte is high byte, second byte is low byte
    return (header[0] << 8) | header[1];
}

STATIC INT_T decoder_opus_vbr_process(PVOID_T handle, BYTE_T *in_buf, INT_T in_len,
                                       BYTE_T *out_buf, INT_T out_size, DECODER_OUTPUT_T *output)
{
    DECODER_OPUS_VBR_CTX_T *ctx = (DECODER_OPUS_VBR_CTX_T *)handle;
    if (!ctx || !out_buf || out_size <= 0 || !output) {
        return -1;
    }

    memset(output, 0, sizeof(DECODER_OUTPUT_T));

    INT_T total_decoded_samples = 0;
    INT_T total_decoded_bytes = 0;
    INT_T input_offset = 0;

    // Process frames until output buffer full or input exhausted
    while (input_offset < in_len) {
        // Check output buffer space
        INT_T samples_per_channel_available = (out_size - total_decoded_bytes) /
                                               (ctx->channels * sizeof(SHORT_T));
        if (samples_per_channel_available <= 0) {
            break;  // Output buffer full
        }

        // Step 1: Check if we have enough data for frame header (2 bytes)
        if (input_offset + VBR_HEADER_SIZE > in_len) {
            // Not enough data for header, return remaining bytes
            OPUS_VBR_PR_DEBUG("Insufficient data for header: need %d, have %d",
                              VBR_HEADER_SIZE, in_len - input_offset);
            break;
        }

        // Step 2: Parse frame length from header (big-endian)
        INT_T frame_len = parse_frame_length(in_buf + input_offset);

        OPUS_VBR_PR_DEBUG("Parsed frame length: %d bytes at offset %d", frame_len, input_offset);

        // Validate frame length
        if (frame_len <= 0 || frame_len > MAX_FRAME_SIZE) {
            PR_ERR("Invalid frame length: %d, skipping 1 bytes", frame_len);
            // input_offset += VBR_HEADER_SIZE;  // Skip bad header
            input_offset += 1; // Skip 1 byte
            continue;
        }

        // Step 3: Check if we have complete frame data
        if (input_offset + VBR_HEADER_SIZE + frame_len > in_len) {
            // Not enough data for complete frame, return remaining bytes
            OPUS_VBR_PR_DEBUG("Incomplete frame: need %d, have %d",
                              VBR_HEADER_SIZE + frame_len, in_len - input_offset);
            break;
        }

        // Step 4: Decode complete frame (zero-copy from input buffer)
        INT_T samples_decoded = opus_decode(ctx->decoder,
                                            in_buf + input_offset + VBR_HEADER_SIZE,
                                            frame_len,
                                            (SHORT_T *)(out_buf + total_decoded_bytes),
                                            samples_per_channel_available,
                                            0);

        if (samples_decoded < 0) {
            if (samples_decoded == OPUS_BUFFER_TOO_SMALL) {
                // Output buffer full, stop here and preserve current frame
                // External will cache remaining data including this frame
                OPUS_VBR_PR_DEBUG("Output buffer full, preserving frame");
                break;
            }
            PR_ERR("opus_decode failed: %d, frame_len: %d", samples_decoded, frame_len);
            // Skip this bad frame and continue
            input_offset += VBR_HEADER_SIZE + frame_len;
            continue;
        }

        OPUS_VBR_PR_DEBUG("Decoded %d samples from %d-byte frame", samples_decoded, frame_len);

        INT_T decoded_bytes = samples_decoded * ctx->channels * sizeof(SHORT_T);
        total_decoded_samples += samples_decoded;
        total_decoded_bytes += decoded_bytes;

        // Move to next frame
        input_offset += VBR_HEADER_SIZE + frame_len;
    }

    // Only set output if we actually decoded something
    if (total_decoded_samples > 0) {
        output->sample_rate = (UINT32_T)ctx->sample_rate;
        output->sample_bits = 16;
        output->channel = (UINT8_T)ctx->channels;
        output->samples = total_decoded_samples;
        output->used_size = total_decoded_bytes;
    }
    // else: output remains all zeros (from memset)

    // Return remaining bytes in input buffer (external will cache)
    INT_T remaining = in_len - input_offset;
    OPUS_VBR_PR_DEBUG("Processed %d/%d bytes, decoded %d samples, remaining %d bytes",
                      input_offset, in_len, total_decoded_samples, remaining);

    return remaining;
}

DECODER_T g_decoder_opus_vbr = {
    .start = decoder_opus_vbr_start,
    .stop = decoder_opus_vbr_stop,
    .process = decoder_opus_vbr_process,
};
