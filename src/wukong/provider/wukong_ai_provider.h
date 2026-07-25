/**
 * @file wukong_ai_provider.h
 * @brief Wukong AI Provider abstraction — ops + handle separation pattern.
 *
 * Defines the unified Provider vtable, message types, and management API.
 * Processing thread uses ops->llm_infer to distinguish Cloud Platform (NULL)
 * from Claw Provider (non-NULL) execution paths.
 */
#pragma once

#include "tuya_cloud_types.h"
#include "ty_cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── Unified Message Structure ─── */

typedef enum {
    WUKONG_AI_MSG_TYPE_TEXT  = 0,
    WUKONG_AI_MSG_TYPE_AUDIO,
    WUKONG_AI_MSG_TYPE_IMAGE,
    WUKONG_AI_MSG_TYPE_VIDEO,
    WUKONG_AI_MSG_TYPE_FILE,
} WUKONG_AI_MSG_TYPE_E;

#define WUKONG_AI_MSG_FLAG_OWNED     (1 << 0)
#define WUKONG_AI_MSG_FLAG_URGENT    (1 << 1)
#define WUKONG_AI_MSG_FLAG_SILENT    (1 << 2)

typedef struct {
    CONST CHAR_T          *channel;
    CONST CHAR_T          *chat_id;
    WUKONG_AI_MSG_TYPE_E   type;
    CONST BYTE_T          *data;
    UINT_T                 data_len;
    UINT32_T               flags;
} WUKONG_AI_MSG_T;

/* ─── Provider Capabilities ─── */

typedef enum {
    WUKONG_AI_CAP_AUDIO       = 1 << 0,
    WUKONG_AI_CAP_TEXT        = 1 << 1,
    WUKONG_AI_CAP_IMAGE       = 1 << 2,
    WUKONG_AI_CAP_VIDEO       = 1 << 3,
    WUKONG_AI_CAP_TTS_BUILTIN = 1 << 4,
} WUKONG_AI_CAP_E;

/* ─── Provider Configuration ─── */

typedef struct {
    CONST CHAR_T *base_url;
    CONST CHAR_T *api_key;
    CONST CHAR_T *model;
    CONST CHAR_T *extra_json;
} WUKONG_AI_PROVIDER_CFG_T;

/* ─── Provider ioctl Commands ─── */

typedef enum {
    WUKONG_PROVIDER_CMD_INPUT_START = 0,
    WUKONG_PROVIDER_CMD_INPUT_STOP,
    WUKONG_PROVIDER_CMD_OUTPUT_STOP,
    WUKONG_PROVIDER_CMD_SET_SCENE,
    WUKONG_PROVIDER_CMD_SET_EVENT_PARAM,
    WUKONG_PROVIDER_CMD_SERVER_VAD_CTRL,
    WUKONG_PROVIDER_CMD_ALERT,
    WUKONG_PROVIDER_CMD_SWITCH_TARGET,
    WUKONG_PROVIDER_CMD_GET_SESSION,
} WUKONG_PROVIDER_CMD_E;

typedef struct {
    CONST CHAR_T *scode;
    VOID_T *session;
} WUKONG_PROVIDER_GET_SESSION_ARG_T;

/* ─── LLM Request / Response (Claw Provider, Phase 4) ─── */

typedef struct {
    CONST CHAR_T *system_prompt;
    ty_cJSON *messages;
    CONST CHAR_T *tools_json;
} WUKONG_LLM_REQ_T;

typedef struct {
    CHAR_T *text;
    CHAR_T *reasoning;
    struct {
        CHAR_T *id;
        CHAR_T *name;
        CHAR_T *arguments_json;
    } *tool_calls;
    UINT32_T tool_call_count;
} WUKONG_LLM_RESP_T;

/* ─── Provider OPS Table (const, ROM-safe) ─── */

typedef struct wukong_ai_provider WUKONG_AI_PROVIDER_T;

typedef struct {
    CONST CHAR_T *name;
    UINT32_T caps;

    OPERATE_RET (*init)(VOID_T *handle, CONST WUKONG_AI_PROVIDER_CFG_T *cfg);
    OPERATE_RET (*deinit)(VOID_T *handle);
    BOOL_T      (*is_ready)(VOID_T *handle);

    OPERATE_RET (*send)(VOID_T *handle, CONST WUKONG_AI_MSG_T *msg);

    OPERATE_RET (*llm_infer)(VOID_T *handle,
                             CONST WUKONG_LLM_REQ_T *request,
                             WUKONG_LLM_RESP_T *response,
                             CHAR_T **out_error);

    OPERATE_RET (*abort)(VOID_T *handle, CONST CHAR_T *chat_id);
    OPERATE_RET (*ioctl)(VOID_T *handle, INT_T cmd, VOID_T *arg);
} WUKONG_AI_PROVIDER_OPS_T;

/* ─── Provider Instance ─── */

struct wukong_ai_provider {
    CONST WUKONG_AI_PROVIDER_OPS_T *ops;
    VOID_T *ctx;
};

/* ─── Provider Management API ─── */

#define WUKONG_PROVIDER_MAX  4

OPERATE_RET wukong_ai_provider_init(CONST WUKONG_AI_PROVIDER_CFG_T *cfg);
OPERATE_RET wukong_ai_provider_register(WUKONG_AI_PROVIDER_T *provider);
WUKONG_AI_PROVIDER_T *wukong_ai_provider_find(CONST CHAR_T *name);
OPERATE_RET wukong_ai_agent_switch_provider(WUKONG_AI_PROVIDER_T *provider,
                                             CONST WUKONG_AI_PROVIDER_CFG_T *cfg);

#ifdef __cplusplus
}
#endif
