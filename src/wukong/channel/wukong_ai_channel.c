/**
 * @file wukong_ai_channel.c
 * @brief Wukong AI Channel infrastructure — registry, input queue, output routing.
 */

#include "wukong_ai_channel.h"
#include "tal_queue.h"
#include "tal_memory.h"
#include "tal_system.h"
#include "uni_log.h"
#include <string.h>

/* --- Channel Registry --- */

STATIC CONST WUKONG_AI_CHAN_T *s_chan_table[WUKONG_AI_CHAN_MAX] = {NULL};
STATIC CONST VOID_T *s_chan_cfg[WUKONG_AI_CHAN_MAX] = {NULL};
STATIC INT_T s_chan_count = 0;

/* --- Input Queue --- */

STATIC QUEUE_HANDLE s_chan_queue = NULL;

/* --- Active Channel Table --- */

STATIC WUKONG_AI_ACTIVE_CHAN_T s_active_table[WUKONG_AI_ACTIVE_CHAN_MAX] = {0};
STATIC INT_T s_active_latest = -1;

/* ================================================================
 * Channel Init
 * ================================================================ */

OPERATE_RET wukong_ai_channel_init(VOID_T)
{
    if (s_chan_queue != NULL) {
        return OPRT_OK;
    }
    return tal_queue_create_init(&s_chan_queue, sizeof(WUKONG_AI_MSG_T *), 8);
}

/* ================================================================
 * Channel Registry
 * ================================================================ */

OPERATE_RET wukong_ai_channel_register(CONST WUKONG_AI_CHAN_T *chan, CONST VOID_T *cfg)
{
    if (chan == NULL || chan->name == NULL) return OPRT_INVALID_PARM;
    if (s_chan_count >= WUKONG_AI_CHAN_MAX) return OPRT_RESOURCE_NOT_READY;
    if (wukong_ai_channel_find(chan->name) != NULL) return OPRT_RESOURCE_NOT_READY;

    s_chan_table[s_chan_count] = chan;
    s_chan_cfg[s_chan_count] = cfg;
    s_chan_count++;
    return OPRT_OK;
}

OPERATE_RET wukong_ai_channel_unregister(CONST CHAR_T *name)
{
    if (name == NULL) return OPRT_INVALID_PARM;

    for (INT_T i = 0; i < s_chan_count; i++) {
        if (strcmp(s_chan_table[i]->name, name) == 0) {
            s_chan_count--;
            s_chan_table[i] = s_chan_table[s_chan_count];
            s_chan_cfg[i] = s_chan_cfg[s_chan_count];
            s_chan_table[s_chan_count] = NULL;
            s_chan_cfg[s_chan_count] = NULL;
            return OPRT_OK;
        }
    }
    return OPRT_NOT_FOUND;
}

CONST WUKONG_AI_CHAN_T *wukong_ai_channel_find(CONST CHAR_T *name)
{
    if (name == NULL) return NULL;

    for (INT_T i = 0; i < s_chan_count; i++) {
        if (strcmp(s_chan_table[i]->name, name) == 0) {
            return s_chan_table[i];
        }
    }
    return NULL;
}

/* ================================================================
 * Channel Lifecycle
 * ================================================================ */

OPERATE_RET wukong_ai_channel_init_all(VOID_T)
{
    for (INT_T i = 0; i < s_chan_count; i++) {
        if (s_chan_table[i]->init) {
            OPERATE_RET rt = s_chan_table[i]->init(s_chan_cfg[i]);
            if (rt != OPRT_OK) {
                PR_WARN("channel [%s] init failed: %d", s_chan_table[i]->name, rt);
            }
        }
    }
    return OPRT_OK;
}

OPERATE_RET wukong_ai_channel_start_all(VOID_T)
{
    for (INT_T i = 0; i < s_chan_count; i++) {
        if (s_chan_table[i]->start) {
            OPERATE_RET rt = s_chan_table[i]->start();
            if (rt != OPRT_OK) {
                PR_WARN("channel [%s] start failed: %d", s_chan_table[i]->name, rt);
            }
        }
    }
    return OPRT_OK;
}

OPERATE_RET wukong_ai_channel_stop_all(VOID_T)
{
    for (INT_T i = 0; i < s_chan_count; i++) {
        if (s_chan_table[i]->stop) {
            s_chan_table[i]->stop();
        }
    }
    return OPRT_OK;
}

/* ================================================================
 * Input Message Flow
 * ================================================================ */

OPERATE_RET wukong_ai_channel_input(CONST WUKONG_AI_MSG_T *msg)
{
    if (msg == NULL) return OPRT_INVALID_PARM;
    if (s_chan_queue == NULL) return OPRT_COM_ERROR;

    WUKONG_AI_MSG_T *copy = NULL;

    if (msg->flags & WUKONG_AI_MSG_FLAG_OWNED) {
        copy = tal_malloc(sizeof(WUKONG_AI_MSG_T));
        if (copy == NULL) return OPRT_MALLOC_FAILED;
        memcpy(copy, msg, sizeof(WUKONG_AI_MSG_T));
    } else {
        UINT_T chan_len = msg->channel ? strlen(msg->channel) + 1 : 0;
        UINT_T cid_len = msg->chat_id ? strlen(msg->chat_id) + 1 : 0;
        UINT_T total = sizeof(WUKONG_AI_MSG_T) + msg->data_len + 1 + chan_len + cid_len;

        copy = tal_malloc(total);
        if (copy == NULL) return OPRT_MALLOC_FAILED;

        BYTE_T *tail = (BYTE_T *)copy + sizeof(WUKONG_AI_MSG_T);

        memcpy(tail, msg->data, msg->data_len);
        tail[msg->data_len] = '\0';
        copy->data = (CONST BYTE_T *)tail;
        copy->data_len = msg->data_len;
        tail += msg->data_len + 1;

        if (chan_len > 0) {
            memcpy(tail, msg->channel, chan_len);
            copy->channel = (CONST CHAR_T *)tail;
            tail += chan_len;
        } else {
            copy->channel = NULL;
        }

        if (cid_len > 0) {
            memcpy(tail, msg->chat_id, cid_len);
            copy->chat_id = (CONST CHAR_T *)tail;
        } else {
            copy->chat_id = NULL;
        }

        copy->type = msg->type;
        copy->flags = msg->flags | WUKONG_AI_MSG_FLAG_OWNED;
    }

    OPERATE_RET rt = tal_queue_post(s_chan_queue, &copy, 0);
    if (rt != OPRT_OK) {
        tal_free(copy);
    }
    return rt;
}

/* ================================================================
 * Output Message Routing
 * ================================================================ */

OPERATE_RET wukong_ai_channel_output(CONST CHAR_T *channel,
                                     CONST CHAR_T *chat_id,
                                     CONST WUKONG_AI_MSG_T *msg)
{
    CONST WUKONG_AI_CHAN_T *chan = wukong_ai_channel_find(channel);
    if (chan == NULL) return OPRT_NOT_FOUND;
    if (chan->send == NULL) return OPRT_NOT_SUPPORTED;

    return chan->send(chat_id, msg);
}

/* ================================================================
 * Active Channel Management
 * ================================================================ */

OPERATE_RET wukong_ai_channel_set_active(CONST WUKONG_AI_ACTIVE_CHAN_T *active)
{
    if (active == NULL) return OPRT_INVALID_PARM;

    INT_T slot = -1;
    for (INT_T i = 0; i < WUKONG_AI_ACTIVE_CHAN_MAX; i++) {
        if (s_active_table[i].chat_id[0] == '\0') {
            if (slot < 0) slot = i;
        } else if (strcmp(s_active_table[i].chat_id, active->chat_id) == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return OPRT_RESOURCE_NOT_READY;

    memcpy(&s_active_table[slot], active, sizeof(WUKONG_AI_ACTIVE_CHAN_T));
    s_active_table[slot].timestamp = tal_system_get_millisecond();
    s_active_latest = slot;

    return OPRT_OK;
}

CONST WUKONG_AI_ACTIVE_CHAN_T *wukong_ai_channel_get_active(CONST CHAR_T *chat_id)
{
    if (chat_id == NULL) {
        return (s_active_latest >= 0) ? &s_active_table[s_active_latest] : NULL;
    }
    for (INT_T i = 0; i < WUKONG_AI_ACTIVE_CHAN_MAX; i++) {
        if (strcmp(s_active_table[i].chat_id, chat_id) == 0) {
            return &s_active_table[i];
        }
    }
    return NULL;
}

/* ================================================================
 * Queue Accessor
 * ================================================================ */

VOID_T *wukong_ai_channel_get_queue(VOID_T)
{
    return s_chan_queue;
}
