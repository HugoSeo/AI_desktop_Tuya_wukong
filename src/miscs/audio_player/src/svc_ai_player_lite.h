/**
 * @file svc_ai_player_lite.h
 * @brief Private header for the lite AI player implementation.
 *        Only included by svc_ai_player.c when AI_PLAYER_LITE is enabled.
 * @version 0.1
 * @date 2025-03-27
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

#ifndef __SVC_AI_PLAYER_LITE_H__
#define __SVC_AI_PLAYER_LITE_H__

#include "svc_ai_player_ctx.h"
#include "svc_ai_player.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Lite service init: creates mutex, opens consumer, inits resample.
 * Does NOT create a thread or message queue.
 */
OPERATE_RET lite_service_init(AI_PLAYER_CFG_T *cfg);

/**
 * Lite service deinit: closes consumer, deinits resample, releases mutex.
 */
OPERATE_RET lite_service_deinit(VOID);

/**
 * Lite player create: inits decoder only (no datasink / ring buffer).
 */
OPERATE_RET lite_player_create(AI_PLAYER_T *player);

/**
 * Lite player destroy: deinits decoder, frees player memory.
 */
OPERATE_RET lite_player_destroy(AI_PLAYER_T *player);

/**
 * Lite player start: direct call (no queue post).
 * Only AI_PLAYER_SRC_MEM is supported in lite mode.
 */
OPERATE_RET lite_player_start(AI_PLAYER_T *player, AI_AUDIO_CODEC_E codec);

/**
 * Lite player stop: direct call (no queue post, no spin-wait).
 */
OPERATE_RET lite_player_stop(AI_PLAYER_T *player);

/**
 * Lite player feed: synchronous decode-in-caller.
 * Accumulates data into framebuf, decodes, and writes PCM to consumer.
 * data==NULL && len==0 signals EOF: flushes remainder and stops.
 */
OPERATE_RET lite_player_feed(AI_PLAYER_T *player, UINT8_T *data, UINT_T len);

#ifdef __cplusplus
}
#endif

#endif /* __SVC_AI_PLAYER_LITE_H__ */
