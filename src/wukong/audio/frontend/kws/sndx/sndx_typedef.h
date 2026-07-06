/*
 * Copyright (c) 2025 ShenZhen ZJ-Tech. All rights reserved.
 */

#ifndef __SNDX_DEF_H__
#define __SNDX_DEF_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*****************************************************************************/
/*                                 Notice:                                   */
/*****************************************************************************/
/*  Audio Sampling: 16000Hz, 16bits                                          */
/*                                                                           */
/*  ASR API:                                                                 */
/*    The input signal should be 1 channel mono data.                        */
/*    The frame size does NOT have 16ms alignment limit,                     */
/*    10ms ~ 40ms recommended.                                               */
/*****************************************************************************/

/*****************************************************************************/
/*                                 ASR API                                   */
/*****************************************************************************/

#define	SNDX_ASR_NO_ERROR                   0
#define SNDX_ASR_INVALID_PARAM				-1
#define SNDX_ASR_INVALID_MODEL				-2
#define SNDX_ASR_MODEL_NOT_EXIST			-3
#define SNDX_ASR_NO_MEMORY					-4
#define SNDX_ASR_NOT_INITIALIZED			-5
#define SNDX_ASR_ALREADY_INITIALIZED		-6
#define SNDX_ASR_NOT_RUNNING				-7
#define SNDX_ASR_ALREADY_RUNNING			-8
#define SNDX_ASR_TIME_OUT					-9
#define SNDX_ASR_UNKNOWN_ERROR				-10

/**
 * @brief Initialize asr engine
 * @param silence_ms Command ended if there's no signal during this period
 * @return error number
*/
int sndx_asr_init(size_t silence_ms);

/**
 * @brief Process audio samples
 * @param samples Pointer to samples to be processed
 * @param size Buffer size in samples, not bytes
 * @return Command Id (greater than 0, 0 means no result, less than 0 means error)
*/
int sndx_asr_process(short *samples, size_t size);

/**
 * @brief Destroy asr engine, all resources will be released
 * @param none
 * @return error number
*/
int sndx_asr_release(void);

#ifdef __cplusplus
}
#endif

#endif
