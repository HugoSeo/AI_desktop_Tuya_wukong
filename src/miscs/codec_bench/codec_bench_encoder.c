/**
 * @file codec_bench_encoder.c
 * @brief Codec encoder performance test; adapted from qemu-opus encoder_test.
 * @version 0.1
 * @date 2026-03-06
 * @copyright Copyright (c) 2024 Tuya Inc. All Rights Reserved.
 */

/***********************************************************************
 ** INCLUDE **
 **********************************************************************/
#include <string.h>
#include "codec_bench_encoder.h"
#include "codec_bench_pcm.h"
#include "tal_log.h"
#include "tal_memory.h"
#include "tal_system.h"
#include "tal_thread.h"
#include "tuya_error_code.h"

#if defined(ENABLE_TUYA_CODEC_OPUS) && (ENABLE_TUYA_CODEC_OPUS == 1)
#include "tuya_ai_encoder.h"
#include "tuya_ai_encoder_opus.h"
#include "opus.h"
#endif
#if defined(ENABLE_TUYA_CODEC_OPUS_IPC) && (ENABLE_TUYA_CODEC_OPUS_IPC == 1)
#include "tuya_ai_encoder.h"
#include "tuya_ai_encoder_opus_ipc.h"
#endif
#if defined(ENABLE_TUYA_CODEC_SPEEX) && (ENABLE_TUYA_CODEC_SPEEX == 1)
#include "tuya_ai_encoder.h"
#include "tuya_ai_encoder_speex.h"
#endif

/***********************************************************************
 ** CONSTANT ( MACRO AND ENUM ) **
 **********************************************************************/
#define CODEC_BENCH_FULL_TEST  0    // 0: minimal test, 1: full test
#define CODEC_BENCH_ROUNDS     3    // runs per entry, take median
#define CODEC_BENCH_STACK_DEPTH   (25 * 1024)
#define CODEC_BENCH_THREAD_PRIO   THREAD_PRIO_1

#if (defined(ENABLE_TUYA_CODEC_OPUS) && (ENABLE_TUYA_CODEC_OPUS == 1)) || \
    (defined(ENABLE_TUYA_CODEC_OPUS_IPC) && (ENABLE_TUYA_CODEC_OPUS_IPC == 1)) || \
    (defined(ENABLE_TUYA_CODEC_SPEEX) && (ENABLE_TUYA_CODEC_SPEEX == 1))
#define ENCODER_TEST_ENABLED 1
#endif

/***********************************************************************
 ** STRUCT **
 **********************************************************************/
#if defined(ENCODER_TEST_ENABLED)
typedef struct {
    CONST CHAR_T *name;
    TUYA_AI_ENCODER_T *encoder;
    TUYA_AI_ENCODER_INFO_T info;
    UINT16_T frame_samples;
    UINT_T   frame_bytes;
} ENCODER_ENTRY_T;

typedef struct {
    float ms_per_1s;
    float ratio_pct;
    float avg_frame_bytes;
    UINT_T total_encoded;
} ENCODER_RESULT_T;
#endif

/***********************************************************************
 ** VARIABLE **
 **********************************************************************/
#if defined(ENCODER_TEST_ENABLED)
STATIC UINT_T s_encoded_bytes = 0;

STATIC OPERATE_RET __encode_cb(AI_AUDIO_CODEC_TYPE codec_type, UCHAR_T *data, UINT_T len, VOID *usr_data)
{
    (VOID)codec_type;
    (VOID)usr_data;
    if (data == NULL || len == 0) {
        TAL_PR_ERR("codec_bench: invalid encoded data");
        return OPRT_INVALID_PARM;
    }
    s_encoded_bytes += len;
    TAL_PR_TRACE("codec_bench: frame %u bytes, total %u", len, s_encoded_bytes);
    return OPRT_OK;
}

STATIC float __median3(float a, float b, float c)
{
    if (a > b) { float t = a; a = b; b = t; }
    if (b > c) { b = c; }
    if (a > b) { b = a; }
    return b;
}

STATIC OPERATE_RET __do_encode(ENCODER_ENTRY_T *ent, CONST CHAR_T *pcm, UINT_T pcm_len,
                               float baseline_ms, float *out_ms_per_1s,
                               ENCODER_RESULT_T *result)
{
    if (ent == NULL || ent->encoder == NULL || pcm == NULL) {
        TAL_PR_ERR("codec_bench: invalid param");
        return OPRT_INVALID_PARM;
    }

    OPERATE_RET rt = OPRT_OK;
    UCHAR_T *in_buf = (UCHAR_T *)pcm;
    UINT_T in_len = pcm_len;
    UINT_T frame_bytes = ent->frame_bytes;

    s_encoded_bytes = 0;

    rt = ent->encoder->create(&ent->encoder->handle, &ent->info);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("codec_bench: create failed %d", rt);
        return rt;
    }

    SYS_TIME_T start = tal_system_get_millisecond();
    for (UINT_T offset = 0; offset < in_len; offset += frame_bytes) {
        UINT_T chunk = frame_bytes;
        if (offset + chunk > in_len) {
            chunk = in_len - offset;
        }
        rt = ent->encoder->encode(ent->encoder->handle, in_buf + offset, chunk, __encode_cb, NULL);
        if (rt != OPRT_OK) {
            TAL_PR_ERR("codec_bench: encode failed %d", rt);
            ent->encoder->destroy(ent->encoder->handle);
            return rt;
        }
    }
    SYS_TIME_T end = tal_system_get_millisecond();
    SYS_TIME_T delta = (end >= start) ? (end - start) : 0;
    UINT_T frames = (frame_bytes > 0) ? (pcm_len / frame_bytes) : 0;
    float ms_per_frame = (frames > 0) ? ((float)delta * (float)frame_bytes / (float)pcm_len) : 0.0f;
    float frames_per_sec = (ent->frame_samples > 0) ? ((float)ent->info.sample_rate / (float)ent->frame_samples) : 0.0f;
    float ms_per_1s = ms_per_frame * frames_per_sec;

    rt = ent->encoder->destroy(ent->encoder->handle);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("codec_bench: destroy failed %d", rt);
        return rt;
    }

    if (out_ms_per_1s != NULL) {
        *out_ms_per_1s = ms_per_1s;
    }

    float pct = (baseline_ms > 0.0f) ? (ms_per_1s / baseline_ms * 100.0f) : 100.0f;
    float avg_frame_bytes = (frames > 0) ? ((float)s_encoded_bytes / (float)frames) : 0.0f;
    TAL_PR_DEBUG("[%s] time: %llums, pcm: %uB, frames: %u, speed: %.2fms/f, per_sec: %.2fms, ratio: %.1f%%, avg_frame: %.1fB",
                 ent->name, (unsigned long long)delta, pcm_len, frames,
                 (double)ms_per_frame, (double)ms_per_1s, (double)pct, (double)avg_frame_bytes);

    if (result != NULL) {
        result->ms_per_1s = ms_per_1s;
        result->ratio_pct = pct;
        result->avg_frame_bytes = avg_frame_bytes;
        result->total_encoded = s_encoded_bytes;
    }

    return OPRT_OK;
}
#endif /* ENCODER_TEST_ENABLED */

STATIC VOID __codec_bench_encoder_task(VOID *arg)
{
    BOOL_T *p_done = (BOOL_T *)arg;
    TAL_PR_NOTICE("codec_bench: encoder start");

#if defined(ENCODER_TEST_ENABLED)
    {
        ENCODER_ENTRY_T entries[] = {
#if defined(ENABLE_TUYA_CODEC_OPUS) && (ENABLE_TUYA_CODEC_OPUS == 1)
            /* 16kbps mediumband */
            {"opus_40ms_16k_mb",           &g_tuya_ai_encoder_opus, {AUDIO_CODEC_OPUS, 16000, 1, 16, 160 * 4, 16000, OPUS_BANDWIDTH_MEDIUMBAND, 0, 0, 0}, 160 * 4, 320 * 4},
            {"opus_40ms_16k_mb_vbr",       &g_tuya_ai_encoder_opus, {AUDIO_CODEC_OPUS, 16000, 1, 16, 160 * 4, 16000, OPUS_BANDWIDTH_MEDIUMBAND, 1, 0, 0}, 160 * 4, 320 * 4},
            {"opus_40ms_16k_mb_dtx",       &g_tuya_ai_encoder_opus, {AUDIO_CODEC_OPUS, 16000, 1, 16, 160 * 4, 16000, OPUS_BANDWIDTH_MEDIUMBAND, 0, 1, 0}, 160 * 4, 320 * 4},
            {"opus_40ms_16k_mb_vbr_dtx",   &g_tuya_ai_encoder_opus, {AUDIO_CODEC_OPUS, 16000, 1, 16, 160 * 4, 16000, OPUS_BANDWIDTH_MEDIUMBAND, 1, 1, 0}, 160 * 4, 320 * 4},
            /* 16kbps wideband */
            {"opus_40ms_16k_wb",           &g_tuya_ai_encoder_opus, {AUDIO_CODEC_OPUS, 16000, 1, 16, 160 * 4, 16000, OPUS_BANDWIDTH_WIDEBAND,   0, 0, 0}, 160 * 4, 320 * 4},
            {"opus_40ms_16k_wb_vbr",       &g_tuya_ai_encoder_opus, {AUDIO_CODEC_OPUS, 16000, 1, 16, 160 * 4, 16000, OPUS_BANDWIDTH_WIDEBAND,   1, 0, 0}, 160 * 4, 320 * 4},
            {"opus_40ms_16k_wb_dtx",       &g_tuya_ai_encoder_opus, {AUDIO_CODEC_OPUS, 16000, 1, 16, 160 * 4, 16000, OPUS_BANDWIDTH_WIDEBAND,   0, 1, 0}, 160 * 4, 320 * 4},
            {"opus_40ms_16k_wb_vbr_dtx",   &g_tuya_ai_encoder_opus, {AUDIO_CODEC_OPUS, 16000, 1, 16, 160 * 4, 16000, OPUS_BANDWIDTH_WIDEBAND,   1, 1, 0}, 160 * 4, 320 * 4},
            /* 24kbps mediumband */
            {"opus_40ms_24k_mb",           &g_tuya_ai_encoder_opus, {AUDIO_CODEC_OPUS, 16000, 1, 16, 160 * 4, 24000, OPUS_BANDWIDTH_MEDIUMBAND, 0, 0, 0}, 160 * 4, 320 * 4},
            {"opus_40ms_24k_mb_vbr",       &g_tuya_ai_encoder_opus, {AUDIO_CODEC_OPUS, 16000, 1, 16, 160 * 4, 24000, OPUS_BANDWIDTH_MEDIUMBAND, 1, 0, 0}, 160 * 4, 320 * 4},
            {"opus_40ms_24k_mb_dtx",       &g_tuya_ai_encoder_opus, {AUDIO_CODEC_OPUS, 16000, 1, 16, 160 * 4, 24000, OPUS_BANDWIDTH_MEDIUMBAND, 0, 1, 0}, 160 * 4, 320 * 4},
            {"opus_40ms_24k_mb_vbr_dtx",   &g_tuya_ai_encoder_opus, {AUDIO_CODEC_OPUS, 16000, 1, 16, 160 * 4, 24000, OPUS_BANDWIDTH_MEDIUMBAND, 1, 1, 0}, 160 * 4, 320 * 4},
            /* 24kbps wideband */
            {"opus_40ms_24k_wb",           &g_tuya_ai_encoder_opus, {AUDIO_CODEC_OPUS, 16000, 1, 16, 160 * 4, 24000, OPUS_BANDWIDTH_WIDEBAND,   0, 0, 0}, 160 * 4, 320 * 4},
            {"opus_40ms_24k_wb_vbr",       &g_tuya_ai_encoder_opus, {AUDIO_CODEC_OPUS, 16000, 1, 16, 160 * 4, 24000, OPUS_BANDWIDTH_WIDEBAND,   1, 0, 0}, 160 * 4, 320 * 4},
            {"opus_40ms_24k_wb_dtx",       &g_tuya_ai_encoder_opus, {AUDIO_CODEC_OPUS, 16000, 1, 16, 160 * 4, 24000, OPUS_BANDWIDTH_WIDEBAND,   0, 1, 0}, 160 * 4, 320 * 4},
            {"opus_40ms_24k_wb_vbr_dtx",   &g_tuya_ai_encoder_opus, {AUDIO_CODEC_OPUS, 16000, 1, 16, 160 * 4, 24000, OPUS_BANDWIDTH_WIDEBAND,   1, 1, 0}, 160 * 4, 320 * 4},
            /* 32kbps mediumband */
            {"opus_40ms_32k_mb",           &g_tuya_ai_encoder_opus, {AUDIO_CODEC_OPUS, 16000, 1, 16, 160 * 4, 32000, OPUS_BANDWIDTH_MEDIUMBAND, 0, 0, 0}, 160 * 4, 320 * 4},
            {"opus_40ms_32k_mb_vbr",       &g_tuya_ai_encoder_opus, {AUDIO_CODEC_OPUS, 16000, 1, 16, 160 * 4, 32000, OPUS_BANDWIDTH_MEDIUMBAND, 1, 0, 0}, 160 * 4, 320 * 4},
            {"opus_40ms_32k_mb_dtx",       &g_tuya_ai_encoder_opus, {AUDIO_CODEC_OPUS, 16000, 1, 16, 160 * 4, 32000, OPUS_BANDWIDTH_MEDIUMBAND, 0, 1, 0}, 160 * 4, 320 * 4},
            {"opus_40ms_32k_mb_vbr_dtx",   &g_tuya_ai_encoder_opus, {AUDIO_CODEC_OPUS, 16000, 1, 16, 160 * 4, 32000, OPUS_BANDWIDTH_MEDIUMBAND, 1, 1, 0}, 160 * 4, 320 * 4},
            /* 32kbps wideband */
            {"opus_40ms_32k_wb",           &g_tuya_ai_encoder_opus, {AUDIO_CODEC_OPUS, 16000, 1, 16, 160 * 4, 32000, OPUS_BANDWIDTH_WIDEBAND,   0, 0, 0}, 160 * 4, 320 * 4},
            {"opus_40ms_32k_wb_vbr",       &g_tuya_ai_encoder_opus, {AUDIO_CODEC_OPUS, 16000, 1, 16, 160 * 4, 32000, OPUS_BANDWIDTH_WIDEBAND,   1, 0, 0}, 160 * 4, 320 * 4},
            {"opus_40ms_32k_wb_dtx",       &g_tuya_ai_encoder_opus, {AUDIO_CODEC_OPUS, 16000, 1, 16, 160 * 4, 32000, OPUS_BANDWIDTH_WIDEBAND,   0, 1, 0}, 160 * 4, 320 * 4},
            {"opus_40ms_32k_wb_vbr_dtx",   &g_tuya_ai_encoder_opus, {AUDIO_CODEC_OPUS, 16000, 1, 16, 160 * 4, 32000, OPUS_BANDWIDTH_WIDEBAND,   1, 1, 0}, 160 * 4, 320 * 4},
#if CODEC_BENCH_FULL_TEST == 1
            {"opus_10ms", &g_tuya_ai_encoder_opus, {AUDIO_CODEC_OPUS, 16000, 1, 16, 160 * 1, 0, 0, 0, 0, 0}, 160 * 1, 320 * 1},
            {"opus_20ms", &g_tuya_ai_encoder_opus, {AUDIO_CODEC_OPUS, 16000, 1, 16, 160 * 2, 0, 0, 0, 0, 0}, 160 * 2, 320 * 2},
            {"opus_60ms", &g_tuya_ai_encoder_opus, {AUDIO_CODEC_OPUS, 16000, 1, 16, 160 * 6, 0, 0, 0, 0, 0}, 160 * 6, 320 * 6},
#endif
#endif
#if defined(ENABLE_TUYA_CODEC_OPUS_IPC) && (ENABLE_TUYA_CODEC_OPUS_IPC == 1)
            {"opus_ipc_40ms", &g_tuya_ai_encoder_opus_ipc, {AUDIO_CODEC_OPUS, 16000, 1, 16, 160 * 4, 0, 0, 0, 0, 0}, 160 * 4, 320 * 4},
#endif
#if defined(ENABLE_TUYA_CODEC_SPEEX) && (ENABLE_TUYA_CODEC_SPEEX == 1)
            {"speex_20ms", &g_tuya_ai_encoder_speex, {AUDIO_CODEC_SPEEX, 16000, 1, 16, 160 * 2, 0, 0, 0, 0, 0}, 160 * 2, 320 * 2},
#endif
        };
        UINT_T n = sizeof(entries) / sizeof(entries[0]);
        UINT_T pcm_len = sizeof(codec_bench_pcm_data);
        if (pcm_len > CODEC_BENCH_PCM_DATA_SIZE) {
            pcm_len = CODEC_BENCH_PCM_DATA_SIZE;
        }

#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
        CHAR_T *pcm_data = tal_psram_malloc(pcm_len);
#else
        CHAR_T *pcm_data = tal_malloc(pcm_len);
#endif
        if (pcm_data != NULL) {
            memcpy(pcm_data, codec_bench_pcm_data, pcm_len);
            TAL_PR_NOTICE("codec_bench: pcm size %u (copied to RAM), rounds %d", pcm_len, CODEC_BENCH_ROUNDS);

            ENCODER_RESULT_T results[sizeof(entries) / sizeof(entries[0])];
            memset(results, 0, sizeof(results));

            float baseline_ms = 0.0f;
            for (UINT_T i = 0; i < n; i++) {
                ENCODER_RESULT_T rounds[CODEC_BENCH_ROUNDS];
                memset(rounds, 0, sizeof(rounds));
                float ms_arr[CODEC_BENCH_ROUNDS];

                for (UINT_T r = 0; r < CODEC_BENCH_ROUNDS; r++) {
                    float cur_ms = 0.0f;
                    __do_encode(&entries[i], pcm_data, pcm_len, baseline_ms, &cur_ms, &rounds[r]);
                    ms_arr[r] = cur_ms;
                    // tal_system_sleep(50);
                }

                /* pick the round whose ms_per_1s is the median */
                UINT_T mid = 0;
                if (CODEC_BENCH_ROUNDS >= 3) {
                    float med = __median3(ms_arr[0], ms_arr[1], ms_arr[2]);
                    for (UINT_T r = 0; r < CODEC_BENCH_ROUNDS; r++) {
                        if (ms_arr[r] == med) { mid = r; break; }
                    }
                }
                results[i] = rounds[mid];

                if (i == 0) {
                    baseline_ms = results[i].ms_per_1s;
                }
                /* recalculate ratio against baseline */
                results[i].ratio_pct = (baseline_ms > 0.0f)
                    ? (results[i].ms_per_1s / baseline_ms * 100.0f) : 100.0f;

                // tal_system_sleep(100);
            }

            TAL_PR_NOTICE("codec_bench: ====================== SUMMARY (median of %d) ======================", CODEC_BENCH_ROUNDS);
            TAL_PR_NOTICE("codec_bench: %-32s %8s %8s %8s %8s", "NAME", "ms/1s", "ratio", "B/frame", "total_B");
            TAL_PR_NOTICE("codec_bench: -------------------------------------------------------------------");
            for (UINT_T i = 0; i < n; i++) {
                TAL_PR_NOTICE("codec_bench: %-32s %8.2f %7.1f%% %8.1f %8u",
                              entries[i].name,
                              (double)results[i].ms_per_1s,
                              (double)results[i].ratio_pct,
                              (double)results[i].avg_frame_bytes,
                              results[i].total_encoded);
            }
            TAL_PR_NOTICE("codec_bench: ===================================================================");

#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
            tal_psram_free(pcm_data);
#else
            tal_free(pcm_data);
#endif
        } else {
            TAL_PR_ERR("codec_bench: malloc pcm_data failed");
        }
    }
#else
    TAL_PR_NOTICE("codec_bench: no encoder enabled, skip");
#endif

    TAL_PR_NOTICE("codec_bench: encoder end");
    if (p_done != NULL) {
        *p_done = TRUE;
    }
}

OPERATE_RET codec_bench_encoder_run(VOID)
{
    __codec_bench_encoder_task(NULL);
    return OPRT_OK;
}

VOID codec_bench_encoder_trigger(VOID)
{
#if defined(ENCODER_TEST_ENABLED)
    STATIC BOOL_T running = FALSE;
    if (running) {
        TAL_PR_DEBUG("codec_bench: already running, skip");
        return;
    }
    running = TRUE;

    BOOL_T task_done = FALSE;
    THREAD_CFG_T thrd_cfg = {
        .stackDepth = CODEC_BENCH_STACK_DEPTH,
        .priority   = CODEC_BENCH_THREAD_PRIO,
        .thrdname   = "codec_bench",
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
        .psram_mode = 1,
#endif
    };
    THREAD_HANDLE handle = NULL;
    OPERATE_RET rt = tal_thread_create_and_start(&handle, NULL, NULL,
                         __codec_bench_encoder_task, &task_done, &thrd_cfg);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("codec_bench: create thread failed %d", rt);
        running = FALSE;
        return;
    }
    TAL_PR_NOTICE("codec_bench: triggered");

    while (!task_done) {
        tal_system_sleep(100);
    }
    tal_thread_delete(handle);
    TAL_PR_NOTICE("codec_bench: thread finished");
    running = FALSE;
#else
    (VOID)0;
#endif
}
