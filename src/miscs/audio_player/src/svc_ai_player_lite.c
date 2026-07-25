/**
 * @file svc_ai_player_lite.c
 * @brief Lightweight AI audio player for TTS streaming.
 *
 * Replaces the thread + queue + ring-buffer pipeline with a synchronous
 * decode-in-feed model.  Only AI_PLAYER_SRC_MEM is supported.
 *
 * Memory savings vs full player (per session):
 *   - Thread stack    : 6–12 KB eliminated
 *   - Ring buffer     : 16 KB eliminated
 *   - Message queue   : eliminated
 *   - datasink layer  : eliminated
 *
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

#include "uni_log.h"
#include "base_event.h"
#include "tal_mutex.h"
#include "tal_memory.h"
#include "svc_ai_player_ctx.h"
#include "svc_ai_player_lite.h"

typedef struct {
    MUTEX_HANDLE mutex; /* Serialises start/stop against each other */
} AI_PLAYER_LITE_CTX_T;

STATIC AI_PLAYER_LITE_CTX_T s_lite_ctx = {0};

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

/**
 * Core decode-and-output loop.  Processes whatever is currently in
 * player->framebuf (up to player->offset bytes) through the decoder
 * and writes PCM to the consumer.
 *
 * When is_eof is TRUE the final partial frame is discarded gracefully
 * instead of looping forever waiting for more data.
 *
 * Returns OPRT_OK on success, or an error code.
 * Returns early (OPRT_OK) if player state changes to non-PLAYING mid-loop.
 */
STATIC OPERATE_RET __lite_decode_and_output(AI_PLAYER_T *player, BOOL_T is_eof)
{
    while (player->offset > 0 || player->has_pending_output) {
        /* Check if stop() raced in from another thread */
        if (player->state != AI_PLAYER_PLAYING) {
            return OPRT_OK;
        }

        DECODER_OUTPUT_T output;
        memset(&output, 0, sizeof(DECODER_OUTPUT_T));

        INT_T remaining = ai_player_decoder_process(
            player->decoder,
            player->framebuf, (INT_T)player->offset,
            player->decode_buf, AI_PLAYER_DECODEBUF_SIZE,
            &output);

        player->has_pending_output = (remaining == OPRT_BUFFER_NOT_ENOUGH);

        /* Update framebuf: memmove unconsumed bytes to front */
        if (remaining > 0 && (UINT_T)remaining < player->offset) {
            memmove(player->framebuf,
                    player->framebuf + (player->offset - (UINT_T)remaining),
                    (UINT_T)remaining);
            player->offset = (UINT_T)remaining;
        } else if (remaining <= 0) {
            player->offset = 0;
        } else {
            /* remaining == player->offset: not enough data for the decoder */
            if (is_eof) {
                /* No more data coming — discard this partial frame */
                player->offset = 0;
            }
            break; /* Need more feed data */
        }

        if (output.sample == 0) {
            continue; /* ID3 tag or header-only frame */
        }

        UINT_T decode_size = output.used_size;

        if (decode_size > 0 && s_ai_player_ctx.consumer.write) {
            s_ai_player_ctx.consumer.write(player, player->decode_buf, decode_size);
        }

        /* Re-check state after consumer.write() — stop() may have fired */
        if (player->state != AI_PLAYER_PLAYING) {
            return OPRT_OK;
        }

        /* If no pending output and framebuf is empty, wait for more data */
        if (!player->has_pending_output && player->offset == 0) {
            break;
        }
    }

    return OPRT_OK;
}

/* ------------------------------------------------------------------ */
/* Lite API implementation                                              */
/* ------------------------------------------------------------------ */

OPERATE_RET lite_service_init(AI_PLAYER_CFG_T *cfg)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CALL_ERR_RETURN(tal_mutex_create_init(&s_lite_ctx.mutex));
    /* Consumer open is performed by the caller (tuya_ai_player_service_init) */

    return rt;
}

OPERATE_RET lite_service_deinit(VOID)
{
    if (s_ai_player_ctx.consumer.close) {
        s_ai_player_ctx.consumer.close();
    }

    if (s_lite_ctx.mutex) {
        tal_mutex_release(s_lite_ctx.mutex);
        s_lite_ctx.mutex = NULL;
    }

    memset(&s_lite_ctx, 0, sizeof(AI_PLAYER_LITE_CTX_T));
    return OPRT_OK;
}

OPERATE_RET lite_player_create(AI_PLAYER_T *player)
{
    /* datasink is not used in lite mode — leave player->sink as NULL */
    return ai_player_decoder_init(&player->decoder);
}

OPERATE_RET lite_player_destroy(AI_PLAYER_T *player)
{
    ai_player_decoder_deinit(player->decoder);
    s_ai_player_ctx.player[player->mode] = NULL;

    Free(player->framebuf);
    Free(player->decode_buf);
    Free(player);
    return OPRT_OK;
}

OPERATE_RET lite_player_start(AI_PLAYER_T *player, AI_AUDIO_CODEC_E codec)
{
    OPERATE_RET rt = OPRT_OK;

    tal_mutex_lock(s_lite_ctx.mutex);

    TUYA_CALL_ERR_GOTO(ai_player_decoder_start(player->decoder, codec), _exit);

    player->state            = AI_PLAYER_PLAYING;
    player->offset           = 0;
    player->has_pending_output = FALSE;

    if (s_ai_player_ctx.decoder_mode && s_ai_player_ctx.consumer.start) {
        s_ai_player_ctx.consumer.start(player);
    }

    s_ai_player_ctx.active_player = player;
    s_ai_player_ctx.is_playing    = TRUE;

    AI_PLAYER_EVT_T evt = {.handle = player, .state = player->state};
    ty_publish_event(EVENT_AI_PLAYER_STATE, &evt);

_exit:
    tal_mutex_unlock(s_lite_ctx.mutex);
    return rt;
}

OPERATE_RET lite_player_stop(AI_PLAYER_T *player)
{
    tal_mutex_lock(s_lite_ctx.mutex);

    /* Set state first — feed() observes this sentinel and exits early */
    player->state = AI_PLAYER_STOPPED;

    ai_player_decoder_stop(player->decoder);
    player->offset             = 0;
    player->has_pending_output = FALSE;

    if (s_ai_player_ctx.decoder_mode && s_ai_player_ctx.consumer.stop) {
        s_ai_player_ctx.consumer.stop(player);
    }

    s_ai_player_ctx.active_player = NULL;
    s_ai_player_ctx.is_playing    = FALSE;

    if (player->playlist && player->playlist_cb) {
        player->playlist_cb(player->playlist, player->state);
    }

    AI_PLAYER_EVT_T evt = {.handle = player, .state = player->state};
    ty_publish_event(EVENT_AI_PLAYER_STATE, &evt);

    tal_mutex_unlock(s_lite_ctx.mutex);
    return OPRT_OK;
}

OPERATE_RET lite_player_feed(AI_PLAYER_T *player, UINT8_T *data, UINT_T len)
{
    OPERATE_RET rt = OPRT_OK;

    /* EOF sentinel: flush remaining framebuf bytes then stop */
    if (data == NULL && len == 0) {
        if (player->state == AI_PLAYER_PLAYING) {
            __lite_decode_and_output(player, TRUE);
            lite_player_stop(player);
        }
        return OPRT_OK;
    }

    if (player->state != AI_PLAYER_PLAYING) {
        return OPRT_INVALID_PARM;
    }

    /* Accumulate incoming bytes into framebuf, decoding as the buffer fills */
    while (len > 0) {
        UINT_T space     = AI_PLAYER_FRAMEBUF_SIZE - player->offset;
        UINT_T copy_len  = (len < space) ? len : space;

        memcpy(player->framebuf + player->offset, data, copy_len);
        player->offset += copy_len;
        data           += copy_len;
        len            -= copy_len;

        rt = __lite_decode_and_output(player, FALSE);
        if (OPRT_OK != rt) {
            return rt;
        }

        /* If state changed (e.g. stop called mid-feed), bail out cleanly */
        if (player->state != AI_PLAYER_PLAYING) {
            return OPRT_OK;
        }
    }

    return OPRT_OK;
}
