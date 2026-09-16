/**
 * @file codec_bench_pcm.h
 * @brief PCM data for codec bench (16 kHz, 16 bit, mono).
 * @version 0.1
 * @date 2026-03-06
 * @copyright Copyright (c) 2024 Tuya Inc. All Rights Reserved.
 */

#ifndef __CODEC_BENCH_PCM_H__
#define __CODEC_BENCH_PCM_H__

#include "tuya_cloud_types.h"

/** 2 second @ 16 kHz, 16 bit, mono = 64000 bytes */
#define CODEC_BENCH_PCM_DATA_SIZE  (64000U)

extern CONST CHAR_T codec_bench_pcm_data[CODEC_BENCH_PCM_DATA_SIZE];

#endif /* __CODEC_BENCH_PCM_H__ */
