/**
 * @file wukong_ai_agent.h
 * @brief Wukong AI agent wrapper: event types and high-level send/control APIs.
 *
 * This module provides a thin wrapper around the Tuya AI agent. It exposes a unified
 * event model (`WUKONG_AI_EVENT_T`) and convenience APIs to send text/media, request
 * cloud alerts, switch roles, and notify application-level events.
 *
 * The event system covers:
 * - ASR (speech recognition) events: empty, OK, error
 * - TTS (text-to-speech) events: pre, start, data chunks, stop, abort, error
 * - Text stream events: start, data, stop, abort
 * - Playback control: play, pause, resume, replay, prev/next, loop modes
 * - System events: chat break, server VAD, exit, alerts
 * - MCP (Model Context Protocol) clock/timer events
 *
 * @see wukong_ai_agent.c
 */

#ifndef __WUKONG_AI_AGENT_H__
#define __WUKONG_AI_AGENT_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Wukong AI event types (ASR, TTS, text stream, playback control, system, MCP). */
typedef enum {
    WUKONG_AI_EVENT_IDLE = -1,              /**< Idle state. */
    WUKONG_AI_EVENT_ASR_EMPTY = 0,         /**< ASR result empty. */
    WUKONG_AI_EVENT_ASR_OK,                /**< ASR success. */
    WUKONG_AI_EVENT_ASR_ERROR,             /**< ASR error. */
    WUKONG_AI_EVENT_TTS_PRE,               /**< TTS pre-start. */
    WUKONG_AI_EVENT_TTS_START,             /**< TTS stream start. */
    WUKONG_AI_EVENT_TTS_DATA,              /**< TTS data chunk. */
    WUKONG_AI_EVENT_TTS_STOP,              /**< TTS stream stop. */
    WUKONG_AI_EVENT_TTS_ABORT,             /**< TTS stream abort. */
    WUKONG_AI_EVENT_TTS_ERROR,             /**< TTS error. */
    WUKONG_AI_EVENT_VAD_TIMEOUT,           /**< VAD timeout. */
    WUKONG_AI_EVENT_TEXT_STREAM_START,     /**< Text stream start. */
    WUKONG_AI_EVENT_TEXT_STREAM_DATA,      /**< Text stream data chunk. */
    WUKONG_AI_EVENT_TEXT_STREAM_STOP,      /**< Text stream stop. */
    WUKONG_AI_EVENT_TEXT_STREAM_ABORT,     /**< Text stream abort. */
    WUKONG_AI_EVENT_EMOTION,               /**< Emotion detected. */
    WUKONG_AI_EVENT_LLM_EMOTION,           /**< LLM emotion. */
    WUKONG_AI_EVENT_SKILL,                 /**< Skill event. */
    WUKONG_AI_EVENT_CHAT_BREAK,            /**< Chat interrupted/break. */
    WUKONG_AI_EVENT_SERVER_VAD,            /**< Server-side VAD detected. */
    WUKONG_AI_EVENT_END,                   /**< Session end. */
    WUKONG_AI_EVENT_PLAY_CTL_PLAY,         /**< Playback: play. */
    WUKONG_AI_EVENT_PLAY_CTL_RESUME,       /**< Playback: resume. */
    WUKONG_AI_EVENT_PLAY_CTL_PAUSE,        /**< Playback: pause. */
    WUKONG_AI_EVENT_PLAY_CTL_REPLAY,       /**< Playback: replay. */
    WUKONG_AI_EVENT_PLAY_CTL_PREV,         /**< Playback: previous. */
    WUKONG_AI_EVENT_PLAY_CTL_NEXT,         /**< Playback: next. */
    WUKONG_AI_EVENT_PLAY_CTL_SEQUENTIAL,   /**< Playback: sequential mode. */
    WUKONG_AI_EVENT_PLAY_CTL_SEQUENTIAL_LOOP, /**< Playback: sequential loop. */
    WUKONG_AI_EVENT_PLAY_CTL_SINGLE_LOOP,  /**< Playback: single loop. */
    WUKONG_AI_EVENT_PLAY_CTL_END,          /**< Playback control end. */
    WUKONG_AI_EVENT_PLAY_END,              /**< Playback finished. */
    WUKONG_AI_EVENT_PLAY_ALERT,            /**< Play alert sound. */
    WUKONG_AI_EVENT_CLOCK_MCP_COUNTDOWN_TIMER, /**< MCP countdown timer. */
    WUKONG_AI_EVENT_CLOCK_MCP_STOPWATCH_TIMER, /**< MCP stopwatch timer. */
    WUKONG_AI_EVENT_CLOCK_MCP_POMODORO_TIMER, /**< MCP Pomodoro timer. */
    WUKONG_AI_EVENT_CLOCK_MCP_SCHEDULE,    /**< MCP schedule. */
    WUKONG_AI_EVENT_CLOCK_MCP_ALARM,       /**< MCP alarm event. */
    
    WUKONG_AI_EVENT_ACCEPT_PICTURE,
    WUKONG_AI_EVENT_SEND_PICTURE_END,

    WUKONG_AI_EVENT_EXIT,                  /**< Exit/quit. */
} WUKONG_AI_EVENT_TYPE_E;

/** Generic Wukong AI event payload delivered to application. */
typedef struct {
    WUKONG_AI_EVENT_TYPE_E type;  /**< Event type. */
    VOID *data;                    /**< Optional payload; type-dependent (e.g. alert type, text JSON). */
    CONST CHAR_T *chat_id;        /**< Session ID (routing). */
    CONST CHAR_T *channel;        /**< Source channel name (routing). */
} WUKONG_AI_EVENT_T;

/** Application event output callback: receives events from wukong AI agent. */
typedef VOID (*WUKONG_EVENT_OUTPUT)(WUKONG_AI_EVENT_T *event);

typedef enum {
    AI_AGENT_PAR_VAL_TYPE_STRING = 0,
    AI_AGENT_PAR_VAL_TYPE_INT,
}AI_AIGENT_PAR_VAL_TP_E;

typedef struct {
    CHAR_T *key;
    AI_AIGENT_PAR_VAL_TP_E type;
    union {
        CHAR_T *str_val;
        INT_T  int_val;
        BOOL_T bool_val;
    } value;
    BOOL_T is_effect_once; // whether the parameter is effective for one time
}WUKONG_AI_CUSTOM_PAR_T;

/**
 * @brief Initialize the Wukong AI agent wrapper and underlying Tuya AI agent.
 *
 * Configures audio/video codec attributes, registers callbacks (event, media, text, alert),
 * and sets codec enable based on input type (UART vs board). On success, stores the
 * application event callback for later use by wukong_ai_event_notify().
 *
 * @param[in] cb Application callback to receive events (may be NULL; events still work internally).
 * @return OPRT_OK on success. See tuya_error_code.h for errors.
 */
OPERATE_RET wukong_ai_agent_init(WUKONG_EVENT_OUTPUT cb);

/**
 * @brief De-initialize the Wukong AI agent wrapper and underlying Tuya AI agent.
 *
 * Clears the event callback and calls tuya_ai_agent_deinit().
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_ai_agent_deinit(VOID);

/**
 * @brief Send a text command/content to the AI agent.
 *
 * Starts input session, sends text via tuya_ai_text_input(), then stops input.
 * The text is processed by the cloud AI service and may trigger TTS or other responses.
 *
 * @param[in] content Null-terminated text string. Must not be NULL.
 * @return OPRT_OK on success. See tuya_error_code.h for errors.
 */
OPERATE_RET wukong_ai_agent_send_text(CHAR_T *content);

/**
 * @brief Send a generic file payload to the AI agent.
 *
 * @param[in] data Pointer to payload bytes. Must not be NULL when len > 0.
 * @param[in] len  Payload length in bytes.
 * @return OPRT_OK on success. See tuya_error_code.h for errors.
 */
OPERATE_RET wukong_ai_agent_send_file(UINT8_T *data, UINT_T len);

/**
 * @brief Send an image payload to the AI agent.
 *
 * Uses current timestamp from tal_system_get_millisecond().
 *
 * @param[in] data Pointer to image bytes (JPEG, PNG, etc.). Must not be NULL when len > 0.
 * @param[in] len  Image length in bytes.
 * @return OPRT_OK on success. See tuya_error_code.h for errors.
 */
OPERATE_RET wukong_ai_agent_send_image(UINT8_T *data, UINT_T len);

/**
 * @brief Send a video payload to the AI agent.
 *
 * Uses current timestamp for both PTS and timestamp.
 *
 * @param[in] data Pointer to video bytes (H264, etc.). Must not be NULL when len > 0.
 * @param[in] len  Video length in bytes.
 * @return OPRT_OK on success. See tuya_error_code.h for errors.
 */
OPERATE_RET wukong_ai_agent_send_video(UINT8_T *data, UINT_T len);

/**
 * @brief Send a generic audio payload to the AI agent.
 *
 * @param[in] data Pointer to payload bytes. Must not be NULL when len > 0.
 * @param[in] len  Payload length in bytes.
 * @return OPRT_OK on success. See tuya_error_code.h for errors.
 */
OPERATE_RET wukong_ai_agent_send_audio(UINT8_T *data, UINT_T len);

/**
 * @brief Request a cloud alert by type (implementation maps to a text command).
 *
 * Maps alert types to simple text commands (cmd:0~5) and sends via send_text().
 * Supported types: AT_NETWORK_CONNECTED, AT_WAKEUP, AT_LONG_KEY_TALK, AT_KEY_TALK,
 * AT_WAKEUP_TALK, AT_RANDOM_TALK. AT_PLEASE_AGAIN is ignored in alert callback.
 *
 * @param[in] type Alert type (AI_ALERT_TYPE_E).
 * @return OPRT_OK on success; OPRT_NOT_SUPPORTED for unsupported type.
 */
OPERATE_RET wukong_ai_agent_cloud_alert(INT_T type);

/**
 * @brief Switch AI agent role by sending a DevOS RPC request.
 *
 * Sends HTTP POST to "thing.ai.agent.switch.role" with JSON payload containing role name.
 * The role name is embedded in a "commandInfo" field.
 *
 * @param[in] role Role name string (null-terminated). Must not be NULL.
 * @return OPRT_OK on success. See tuya_error_code.h for errors.
 */
OPERATE_RET wukong_ai_agent_role_switch(CONST CHAR_T *role);

/**
 * @brief Notify an application-level event to the registered output callback.
 *
 * Builds a WUKONG_AI_EVENT_T and calls the callback if registered. Used internally
 * by callbacks and can be called by application code to inject events.
 *
 * @param[in] type Event type.
 * @param[in] data Optional payload pointer (type-dependent; e.g. alert type cast to VOID*).
 */
VOID wukong_ai_event_notify(WUKONG_AI_EVENT_TYPE_E type, VOID *data);
VOID wukong_ai_event_notify_ext(CONST WUKONG_AI_EVENT_T *event);

/* --- Facade API (Provider-backed, replaces direct SDK calls in mode layer) --- */

BOOL_T wukong_ai_agent_is_ready(VOID);

OPERATE_RET wukong_ai_agent_input_start(BOOL_T interrupt);
OPERATE_RET wukong_ai_agent_input_stop(VOID);
OPERATE_RET wukong_ai_agent_output_stop(BOOL_T force);

OPERATE_RET wukong_ai_agent_chat_break(CONST CHAR_T *chat_id);
OPERATE_RET wukong_ai_agent_set_scene(CONST CHAR_T *scene);
OPERATE_RET wukong_ai_agent_server_vad_ctrl(BOOL_T enable);
OPERATE_RET wukong_ai_agent_set_event_param(VOID_T *param);
OPERATE_RET wukong_ai_agent_get_session(CONST CHAR_T *scode, VOID_T **session_out);

#ifdef __cplusplus
}
#endif

#endif /* __WUKONG_AI_AGENT_H__ */