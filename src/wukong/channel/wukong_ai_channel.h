/**
 * @file wukong_ai_channel.h
 * @brief Wukong AI Channel abstraction — transport-agnostic message routing.
 *
 * Channels are pure transport adapters (WeChat, Telegram, CLI, etc.).
 * They do not filter message types or know about Providers.
 *
 * Local channel (Mode layer + facade) uses the quick path (direct facade API),
 * bypassing channel_input queue entirely.
 * IM channels go through channel_input → queue → processing thread.
 */
#pragma once

#include "tuya_cloud_types.h"
#include "wukong_ai_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Channel Descriptor (const, ROM-safe) --- */

typedef struct {
    CONST CHAR_T *name;

    OPERATE_RET (*init)(CONST VOID_T *cfg);
    OPERATE_RET (*start)(VOID_T);
    OPERATE_RET (*stop)(VOID_T);
    OPERATE_RET (*deinit)(VOID_T);

    OPERATE_RET (*send)(CONST CHAR_T *chat_id,
                        CONST WUKONG_AI_MSG_T *msg);
} WUKONG_AI_CHAN_T;

/* --- Active Channel (for Cloud Platform async callback routing) --- */

typedef struct {
    CHAR_T channel[32];
    CHAR_T chat_id[64];
    UINT32_T timestamp;
} WUKONG_AI_ACTIVE_CHAN_T;

#define WUKONG_AI_CHAN_MAX         8
#define WUKONG_AI_ACTIVE_CHAN_MAX  8

/* --- Channel Management API --- */

OPERATE_RET wukong_ai_channel_init(VOID_T);

OPERATE_RET wukong_ai_channel_register(CONST WUKONG_AI_CHAN_T *chan, CONST VOID_T *cfg);
OPERATE_RET wukong_ai_channel_unregister(CONST CHAR_T *name);
CONST WUKONG_AI_CHAN_T *wukong_ai_channel_find(CONST CHAR_T *name);

OPERATE_RET wukong_ai_channel_init_all(VOID_T);
OPERATE_RET wukong_ai_channel_start_all(VOID_T);
OPERATE_RET wukong_ai_channel_stop_all(VOID_T);

/* --- Message Flow --- */

OPERATE_RET wukong_ai_channel_input(CONST WUKONG_AI_MSG_T *msg);
OPERATE_RET wukong_ai_channel_output(CONST CHAR_T *channel,
                                     CONST CHAR_T *chat_id,
                                     CONST WUKONG_AI_MSG_T *msg);

/* --- Active Channel Management --- */

OPERATE_RET wukong_ai_channel_set_active(CONST WUKONG_AI_ACTIVE_CHAN_T *active);
CONST WUKONG_AI_ACTIVE_CHAN_T *wukong_ai_channel_get_active(CONST CHAR_T *chat_id);

/* --- Queue Accessor (for processing thread) --- */

VOID_T *wukong_ai_channel_get_queue(VOID_T);

#ifdef __cplusplus
}
#endif
