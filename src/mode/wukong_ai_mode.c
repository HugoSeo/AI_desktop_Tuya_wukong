/**
 * @file wukong_ai_mode.c
 * @brief AI mode management implementation.
 *
 * Implements the two-level mode manager, dispatcher, and switching logic
 * for device modes and chat sub-modes.
 *
 * @copyright Copyright (c) 2023 Tuya Inc. All Rights Reserved.
 */

#include "wukong_ai_mode.h"
#include <stdio.h>
#include <string.h>
#include "tuya_ai_input.h"
#include "tuya_ai_toy.h"

/* Forward declarations: mode register functions (implemented in each mode .c) */
extern OPERATE_RET ai_hold_register(AI_CHAT_MODE_HANDLE_T **cb);
extern OPERATE_RET ai_oneshot_register(AI_CHAT_MODE_HANDLE_T **cb);
extern OPERATE_RET ai_wakeup_register(AI_CHAT_MODE_HANDLE_T **cb);
extern OPERATE_RET ai_free_register(AI_CHAT_MODE_HANDLE_T **cb);
extern OPERATE_RET ai_p2p_register(AI_CHAT_MODE_HANDLE_T **cb);
extern OPERATE_RET ai_translate_register(AI_CHAT_MODE_HANDLE_T **cb);
extern OPERATE_RET ai_record_register(AI_CHAT_MODE_HANDLE_T **cb);
extern OPERATE_RET ai_picture_register(AI_CHAT_MODE_HANDLE_T **cb);
extern OPERATE_RET ai_detection_register(AI_CHAT_MODE_HANDLE_T **cb);

STATIC OPERATE_RET __default_audio_input(VOID *data, INT_T len);

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC AI_MODE_MGR_T s_mode_mgr = {0};
STATIC AI_RECORD_HANDLE_T s_record_handle = {0};

CHAR_T *_chat_sub_str[] = {
    "hold", "oneshot", "wakeup", "free",
};

CHAR_T *_device_mode_str[] = {
    "chat", "translate", "p2p", "record", "picture", "detection",
};

CHAR_T *_state_str[] = {
    "INIT", "IDLE", "LISTEN", "UPLOAD", "THINK", "SPEAK", "UNKNOWN"
};

#if defined(ENABLE_TUYA_UI)
/** Display names for AI_DEVICE_MODE_E (indexed by enum value); keep in sync
 *  with AI_DEVICE_MODE_MAX and the labels used in ui_control_center / ui_device_mode. */
STATIC CONST CHAR_T *s_device_mode_display[AI_DEVICE_MODE_MAX] = {
    "闲聊模式",  /* AI_DEVICE_MODE_CHAT */
    "翻译模式",  /* AI_DEVICE_MODE_TRANSLATE */
    "P2P模式",   /* AI_DEVICE_MODE_P2P */
    "录音模式",  /* AI_DEVICE_MODE_RECORD */
    "生图模式",  /* AI_DEVICE_MODE_PICTURE */
    "侦测模式",  /* AI_DEVICE_MODE_DETECTION */
};

STATIC CONST CHAR_T *s_chat_sub_display[AI_CHAT_SUB_MAX] = {
    "长按",  /* AI_CHAT_SUB_HOLD */
    "按键",  /* AI_CHAT_SUB_ONESHOT */
    "唤醒",  /* AI_CHAT_SUB_WAKEUP */
    "自由",  /* AI_CHAT_SUB_FREE */
};
#endif /* ENABLE_TUYA_UI */

/* ---------------------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------------------- */

/**
 * @brief Post a "已成功切换到XX模式" notification to the chat UI
 * @return none
 * @note No-op when ENABLE_TUYA_UI is not defined. The notification is delivered
 *       asynchronously via tuya_ai_display_msg, which copies the buffer
 *       internally, so passing a stack buffer is safe.
 */
STATIC VOID_T __post_mode_switched_notify(VOID_T)
{
#if defined(ENABLE_TUYA_UI)
    AI_DEVICE_MODE_E mode = s_mode_mgr.device_mode;
    CHAR_T buf[64];
    INT_T len;

    if (mode >= AI_DEVICE_MODE_MAX) {
        return;
    }

    if (mode == AI_DEVICE_MODE_CHAT) {
        AI_CHAT_SUB_MODE_E sub = s_mode_mgr.chat_sub_mode;
        CONST CHAR_T *sub_name = (sub < AI_CHAT_SUB_MAX) ? s_chat_sub_display[sub] : "";
        len = snprintf(buf, sizeof(buf), "已成功切换到%s: %s",
                       s_device_mode_display[mode], sub_name);
    } else {
        len = snprintf(buf, sizeof(buf), "已成功切换到%s",
                       s_device_mode_display[mode]);
    }

    if (len <= 0) {
        return;
    }
    if ((UINT32_T)len >= sizeof(buf)) {
        len = (INT_T)sizeof(buf) - 1;
    }

    tuya_ai_display_msg((UINT8_T *)buf, len, TY_DISPLAY_TP_MODE_NOTIFY);
#endif
}

/**
 * @brief Resolve the active mode entry based on current device_mode and chat_sub_mode.
 * @return Pointer to the active AI_MODE_ENTRY_T, or NULL if not enabled.
 */
STATIC AI_MODE_ENTRY_T *__resolve_active(VOID)
{
    if (s_mode_mgr.device_mode == AI_DEVICE_MODE_CHAT) {
        AI_CHAT_SUB_MODE_E sub = s_mode_mgr.chat_sub_mode;
        if (sub < AI_CHAT_SUB_MAX && s_mode_mgr.chat_subs[sub].enabled) {
            return &s_mode_mgr.chat_subs[sub];
        }
        return NULL;
    }

    AI_DEVICE_MODE_E dev = s_mode_mgr.device_mode;
    if (dev < AI_DEVICE_MODE_MAX && s_mode_mgr.device_modes[dev].enabled) {
        return &s_mode_mgr.device_modes[dev];
    }
    return NULL;
}

/**
 * @brief Map device mode to its corresponding solution code (scode).
 * @param[in] mode Device mode.
 * @return Scode string pointer, or NULL if the mode has no scode (e.g. P2P, record).
 */
STATIC CHAR_T *__get_scode_by_mode(AI_DEVICE_MODE_E mode)
{
    switch (mode) {
    case AI_DEVICE_MODE_CHAT:
        return AI_AGENT_SCODE_CHAT;
    case AI_DEVICE_MODE_TRANSLATE:
        return AI_AGENT_SCODE_TRANSLATE;
    case AI_DEVICE_MODE_PICTURE:
        return AI_AGENT_SCODE_PICTURE;
    case AI_DEVICE_MODE_DETECTION:
        return AI_AGENT_SCODE_DETECTION;
    default:
        return NULL;
    }
}

/**
 * @brief Generic dispatch: check handle and callback, then call.
 */
#define MODE_DISPATCH_CALL(h, member, data, len, op_name) \
    do { \
        if ((h) == NULL || (h)->member == NULL) { \
            TAL_PR_WARN("[ai_mode] active mode does not support: %s", (op_name)); \
            return OPRT_NOT_FOUND; \
        } \
        return (h)->member(data, len); \
    } while (0)

/**
 * @brief Internal: dispatch one operation to current active mode.
 * @param[in] op   Operation id.
 * @param[in] data Payload (may be NULL).
 * @param[in] len  Payload length.
 * @return Handler result or OPRT_NOT_FOUND when no handler.
 */
STATIC OPERATE_RET __mode_dispatch(AI_MODE_OP_E op, VOID *data, INT_T len)
{
    AI_MODE_ENTRY_T *entry = s_mode_mgr.active;
    AI_CHAT_MODE_HANDLE_T *h = entry ? entry->handle : NULL;

    switch (op) {
    case AI_MODE_OP_INIT:
        MODE_DISPATCH_CALL(h, on_init, data, len, "init");
    case AI_MODE_OP_DEINIT:
        MODE_DISPATCH_CALL(h, on_deinit, data, len, "deinit");
    case AI_MODE_OP_KEY:
        MODE_DISPATCH_CALL(h, on_key, data, len, "key");
    case AI_MODE_OP_TASK:
        MODE_DISPATCH_CALL(h, on_task, data, len, "task");
    case AI_MODE_OP_WAKEUP:
        MODE_DISPATCH_CALL(h, on_wakeup, data, len, "wakeup");
    case AI_MODE_OP_VAD:
        MODE_DISPATCH_CALL(h, on_vad, data, len, "vad");
    case AI_MODE_OP_CLIENT:
        MODE_DISPATCH_CALL(h, on_client, data, len, "client");
    case AI_MODE_OP_NOTIFY_IDLE:
        MODE_DISPATCH_CALL(h, on_notify_idle, data, len, "notify_idle");

    case AI_MODE_OP_EVENT:
        if (h == NULL || h->on_event == NULL) {
            TAL_PR_WARN("[ai_mode] active mode does not support: event");
            return OPRT_NOT_FOUND;
        }
        if (entry->mutex != NULL) {
            tal_mutex_lock(entry->mutex);
            h->on_event(data, len);
            tal_mutex_unlock(entry->mutex);
        } else {
            h->on_event(data, len);
        }
        return OPRT_OK;

    case AI_MODE_OP_AUDIO_INPUT:
        if (h == NULL || h->on_audio_input == NULL) {
            return __default_audio_input(data, len);
        }
        return h->on_audio_input(data, len);

    default:
        return OPRT_NOT_FOUND;
    }
}

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */

/**
 * @brief Dispatch an operation to the current AI mode.
 * @param[in] op   The operation type to dispatch.
 * @param[in] data Pointer to operation-specific data payload.
 * @param[in] len  Length of the data payload.
 * @return Result of the mode handler, or OPRT_NOT_FOUND if unsupported.
 */
OPERATE_RET wukong_ai_mode_dispatch(AI_MODE_OP_E op, VOID *data, INT_T len)
{
    if (op >= AI_MODE_OP_MAX) {
        return OPRT_INVALID_PARM;
    }
    return __mode_dispatch(op, data, len);
}

/**
 * @brief Switch to a specific device mode.
 * @param[in] mode Target device mode.
 * @return OPRT_OK on success, or OPRT_NOT_SUPPORTED if the mode is not enabled.
 */
OPERATE_RET wukong_ai_device_mode_switch(AI_DEVICE_MODE_E mode)
{
    if (mode >= AI_DEVICE_MODE_MAX) {
        TAL_PR_ERR("[ai_mode] invalid device mode %d", mode);
        return OPRT_INVALID_PARM;
    }

    AI_MODE_ENTRY_T *target = NULL;

    if (mode == AI_DEVICE_MODE_CHAT) {
        target = &s_mode_mgr.chat_subs[s_mode_mgr.chat_sub_mode];
    } else {
        target = &s_mode_mgr.device_modes[mode];
    }

    if (target == NULL || !target->enabled) {
        TAL_PR_ERR("[ai_mode] device mode %d (%s) not enabled", mode, _device_mode_str[mode]);
        return OPRT_NOT_SUPPORTED;
    }

    TAL_PR_DEBUG("[ai_mode] switch device mode to %d (%s)", mode, _device_mode_str[mode]);
    wukong_ai_mode_dispatch(AI_MODE_OP_DEINIT, NULL, 0);

    CHAR_T *old_scode = __get_scode_by_mode(s_mode_mgr.device_mode);
    if (old_scode != NULL) {
        TAL_PR_DEBUG("[ai_mode] del session for old scode: %s", old_scode);
        tuya_ai_agent_del_session(old_scode);
    }

    s_mode_mgr.device_mode = mode;
    s_mode_mgr.active = target;
    tuya_ai_toy_device_mode_set(mode);
    wukong_ai_mode_dispatch(AI_MODE_OP_INIT, NULL, 0);

    __post_mode_switched_notify();

    return OPRT_OK;
}

/**
 * @brief Switch to a specific chat sub-mode (only valid when device_mode == CHAT).
 * @param[in] sub Target chat sub-mode.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_ai_chat_sub_mode_switch(AI_CHAT_SUB_MODE_E sub)
{
    if (sub >= AI_CHAT_SUB_MAX) {
        TAL_PR_ERR("[ai_mode] invalid chat sub-mode %d", sub);
        return OPRT_INVALID_PARM;
    }

    if (!s_mode_mgr.chat_subs[sub].enabled) {
        TAL_PR_ERR("[ai_mode] chat sub-mode %d (%s) not enabled", sub, _chat_sub_str[sub]);
        return OPRT_NOT_SUPPORTED;
    }

    TAL_PR_DEBUG("[ai_mode] switch chat sub-mode to %d (%s)", sub, _chat_sub_str[sub]);
    wukong_ai_mode_dispatch(AI_MODE_OP_DEINIT, NULL, 0);

    s_mode_mgr.device_mode = AI_DEVICE_MODE_CHAT;
    s_mode_mgr.chat_sub_mode = sub;
    s_mode_mgr.active = &s_mode_mgr.chat_subs[sub];
    tuya_ai_toy_trigger_mode_set(sub);

    wukong_ai_mode_dispatch(AI_MODE_OP_INIT, NULL, 0);

    __post_mode_switched_notify();

    return OPRT_OK;
}

/**
 * @brief Cycle to next enabled chat sub-mode (for double-press in chat mode).
 * @return OPRT_OK on success, or OPRT_NOT_SUPPORTED.
 */
OPERATE_RET wukong_ai_chat_sub_mode_cycle(VOID)
{
    AI_CHAT_SUB_MODE_E cur = s_mode_mgr.chat_sub_mode;
    AI_CHAT_SUB_MODE_E next = (cur + 1) % AI_CHAT_SUB_MAX;
    INT_T count = 0;

    while (count++ < AI_CHAT_SUB_MAX) {
        if (s_mode_mgr.chat_subs[next].enabled) {
            return wukong_ai_chat_sub_mode_switch(next);
        }
        next = (next + 1) % AI_CHAT_SUB_MAX;
    }

    TAL_PR_ERR("[ai_mode] no enabled chat sub-mode found");
    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief Get the mode manager (read-only access for query).
 * @return Pointer to the mode manager.
 */
CONST AI_MODE_MGR_T *wukong_ai_mode_mgr_get(VOID)
{
    return &s_mode_mgr;
}

/**
 * @brief Default fallback handler for audio input.
 * @param[in] data Pointer to PCM audio data.
 * @param[in] len  Length of the audio data in bytes.
 * @return OPRT_OK on success, or an error code.
 */
STATIC OPERATE_RET __default_audio_input(VOID *data, INT_T len)
{
    OPERATE_RET rt = OPRT_OK;
    UINT64_T   pts = 0;
    UINT64_T   timestamp = 0;

    TAL_PR_NOTICE("ai toy -> recv wukong mic data: %d", len);

    if (!wukong_ai_agent_is_ready()) {
        TAL_PR_DEBUG("ai agent is not ready, ignore audio input");
        return OPRT_OK;
    }

    timestamp = pts = tal_system_get_millisecond();
    TUYA_CALL_ERR_LOG(tuya_ai_audio_input(timestamp, pts, (UINT8_T *)data, len, len));

    return OPRT_OK;
}

/**
 * @brief Set the record handle callbacks.
 * @param[in] handle Pointer to the record handle to copy from.
 * @return OPRT_OK on success, OPRT_INVALID_PARM if handle is NULL.
 */
OPERATE_RET wukong_ai_record_handle_set(CONST AI_RECORD_HANDLE_T *handle)
{
    if (handle == NULL) {
        return OPRT_INVALID_PARM;
    }
    memcpy(&s_record_handle, handle, sizeof(AI_RECORD_HANDLE_T));
    return OPRT_OK;
}

/**
 * @brief Get the current record handle.
 * @return Pointer to the internal record handle structure.
 */
AI_RECORD_HANDLE_T *wukong_ai_record_handle_get(VOID)
{
    return &s_record_handle;
}

/**
 * @brief Register a chat sub-mode entry.
 * @param[in] sub     Chat sub-mode index.
 * @param[in] reg_fn  Registration function that provides mode callbacks.
 * @return none
 */
STATIC VOID __register_chat_sub(AI_CHAT_SUB_MODE_E sub,
                                 OPERATE_RET (*reg_fn)(AI_CHAT_MODE_HANDLE_T **))
{
    s_mode_mgr.chat_subs[sub].enabled = TRUE;
    tal_mutex_create_init(&s_mode_mgr.chat_subs[sub].mutex);
    reg_fn(&s_mode_mgr.chat_subs[sub].handle);
}

/**
 * @brief Register a device mode entry.
 * @param[in] mode    Device mode index.
 * @param[in] reg_fn  Registration function that provides mode callbacks.
 * @return none
 */
STATIC VOID __register_device_mode(AI_DEVICE_MODE_E mode,
                                    OPERATE_RET (*reg_fn)(AI_CHAT_MODE_HANDLE_T **))
{
    s_mode_mgr.device_modes[mode].enabled = TRUE;
    tal_mutex_create_init(&s_mode_mgr.device_modes[mode].mutex);
    reg_fn(&s_mode_mgr.device_modes[mode].handle);
}

/**
 * @brief Initialize the AI mode module.
 * @return OPRT_OK on success, or an error code.
 */
OPERATE_RET wukong_ai_mode_init(VOID)
{
    memset(&s_mode_mgr, 0, sizeof(AI_MODE_MGR_T));

#if defined(ENABLE_AI_MODE_HOLD) && (ENABLE_AI_MODE_HOLD == 1)
    __register_chat_sub(AI_CHAT_SUB_HOLD, ai_hold_register);
#endif

#if defined(ENABLE_AI_MODE_ONESHOT) && (ENABLE_AI_MODE_ONESHOT == 1)
    __register_chat_sub(AI_CHAT_SUB_ONESHOT, ai_oneshot_register);
#endif

#if defined(ENABLE_AI_MODE_WAKEUP) && (ENABLE_AI_MODE_WAKEUP == 1)
    __register_chat_sub(AI_CHAT_SUB_WAKEUP, ai_wakeup_register);
#endif

#if defined(ENABLE_AI_MODE_FREE) && (ENABLE_AI_MODE_FREE == 1)
    __register_chat_sub(AI_CHAT_SUB_FREE, ai_free_register);
#endif

#if defined(ENABLE_AI_MODE_TRANSLATE) && (ENABLE_AI_MODE_TRANSLATE == 1)
    __register_device_mode(AI_DEVICE_MODE_TRANSLATE, ai_translate_register);
#endif

#if defined(ENABLE_AI_MODE_P2P) && (ENABLE_AI_MODE_P2P == 1)
    __register_device_mode(AI_DEVICE_MODE_P2P, ai_p2p_register);
#endif

#if defined(ENABLE_AI_MODE_RECORD) && (ENABLE_AI_MODE_RECORD == 1)
    __register_device_mode(AI_DEVICE_MODE_RECORD, ai_record_register);
#endif

#if defined(ENABLE_AI_MODE_PICTURE) && (ENABLE_AI_MODE_PICTURE == 1)
    __register_device_mode(AI_DEVICE_MODE_PICTURE, ai_picture_register);
#endif

#if defined(ENABLE_AI_MODE_DETECTION) && (ENABLE_AI_MODE_DETECTION == 1)
    __register_device_mode(AI_DEVICE_MODE_DETECTION, ai_detection_register);
#endif

    s_mode_mgr.device_modes[AI_DEVICE_MODE_CHAT].enabled = TRUE;

    AI_CHAT_SUB_MODE_E stored_sub = tuya_ai_toy_trigger_mode_get();
    AI_DEVICE_MODE_E stored_dev = tuya_ai_toy_device_mode_get();

    if (stored_dev == AI_DEVICE_MODE_CHAT) {
        if (stored_sub >= AI_CHAT_SUB_MAX || !s_mode_mgr.chat_subs[stored_sub].enabled) {
            stored_sub = TUYA_AI_CHAT_DEFAULT_MODE;
        }
        if (stored_sub >= AI_CHAT_SUB_MAX || !s_mode_mgr.chat_subs[stored_sub].enabled) {
            stored_sub = AI_CHAT_SUB_HOLD;
        }
        INT_T i = 0;
        AI_CHAT_SUB_MODE_E try_sub = stored_sub;
        do {
            if (s_mode_mgr.chat_subs[try_sub].enabled) {
                s_mode_mgr.device_mode = AI_DEVICE_MODE_CHAT;
                s_mode_mgr.chat_sub_mode = try_sub;
                s_mode_mgr.active = &s_mode_mgr.chat_subs[try_sub];
                tuya_ai_toy_trigger_mode_set(try_sub);
                TAL_PR_DEBUG("[ai_mode] init chat sub-mode %d (%s)", try_sub, _chat_sub_str[try_sub]);
                wukong_ai_mode_dispatch(AI_MODE_OP_INIT, NULL, 0);
                return OPRT_OK;
            }
            try_sub = (try_sub + 1) % AI_CHAT_SUB_MAX;
        } while (i++ < AI_CHAT_SUB_MAX);
    } else if (stored_dev < AI_DEVICE_MODE_MAX &&
               s_mode_mgr.device_modes[stored_dev].enabled) {
        s_mode_mgr.device_mode = stored_dev;
        s_mode_mgr.active = &s_mode_mgr.device_modes[stored_dev];
        TAL_PR_DEBUG("[ai_mode] init device mode %d (%s)", stored_dev, _device_mode_str[stored_dev]);
        wukong_ai_mode_dispatch(AI_MODE_OP_INIT, NULL, 0);
        return OPRT_OK;
    }

    TAL_PR_ERR("[ai_mode] no valid mode found at init");
    return OPRT_NOT_SUPPORTED;
}
