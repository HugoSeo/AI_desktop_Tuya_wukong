/**
 * @file svc_ai_player_ctx.h
 * @brief Shared internal context definition for the AI player service.
 *        Included by both svc_ai_player.c and svc_ai_player_lite.c.
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

#ifndef __SVC_AI_PLAYER_CTX_H__
#define __SVC_AI_PLAYER_CTX_H__

#include "tal_queue.h"
#include "tal_thread.h"
#include "svc_ai_player.h"
#include "ai_player.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    QUEUE_HANDLE queue;
    THREAD_HANDLE thread;
    BOOL_T is_playing;
    UINT_T msg_timeout;
    AI_PLAYER_T *player[AI_PLAYER_MODE_MAX];
    AI_PLAYER_T *active_player;
    AI_PLAYER_CONSUMER_T consumer;
    AI_PLAYER_CFG_T cfg;
    INT_T volume;
    BOOL_T mute;
    BOOL_T mixer_mode;
    BOOL_T decoder_mode;
    BYTE_T *mixer_buf; // AI_PLAYER_DECODEBUF_SIZE
    UINT_T mixer_offset;
    UINT_T mixer_flag; // 0-null, 1-fg, 2-bg
} AI_PLAYER_CTX_T;

extern AI_PLAYER_CTX_T s_ai_player_ctx;

#ifdef __cplusplus
}
#endif

#endif /* __SVC_AI_PLAYER_CTX_H__ */
