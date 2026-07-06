/**
 * @file codec_bench.h
 * @brief Codec bench mode: key-init only, short press triggers encoder test (no AI toy).
 * @version 0.1
 * @date 2026-03-06
 * @copyright Copyright (c) 2024 Tuya Inc. All Rights Reserved.
 */

#ifndef __CODEC_BENCH_H__
#define __CODEC_BENCH_H__

#include "tuya_cloud_types.h"
#include "tuya_ai_toy.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start codec bench mode: init key only, callback triggers codec_bench_encoder_trigger on short press.
 * @param cfg Board config (uses audio_trigger_pin; seq 200ms, long 400ms).
 * @return OPRT_OK on success.
 */
OPERATE_RET codec_bench_start(CONST TY_AI_TOY_CFG_T *cfg);

#ifdef __cplusplus
}
#endif

#endif /* __CODEC_BENCH_H__ */
