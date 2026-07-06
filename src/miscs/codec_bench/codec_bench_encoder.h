/**
 * @file codec_bench_encoder.h
 * @brief Codec encoder performance test: run / trigger from key.
 * @version 0.1
 * @date 2026-03-06
 * @copyright Copyright (c) 2024 Tuya Inc. All Rights Reserved.
 */

#ifndef __CODEC_BENCH_ENCODER_H__
#define __CODEC_BENCH_ENCODER_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run codec encode performance test (blocking).
 * @return OPRT_OK on success.
 */
OPERATE_RET codec_bench_encoder_run(VOID);

/**
 * @brief Trigger encoder test in background thread (non-blocking).
 * Call on short key press to run one encode test.
 */
VOID codec_bench_encoder_trigger(VOID);

#ifdef __cplusplus
}
#endif

#endif /* __CODEC_BENCH_ENCODER_H__ */
