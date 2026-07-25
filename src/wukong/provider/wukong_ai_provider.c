/**
 * @file wukong_ai_provider.c
 * @brief Provider management framework, processing thread, and facade API.
 *
 * - Processing thread consumes IM channel queue, dispatches to active Provider
 * - Facade functions implement wukong_ai_agent.h public API via Provider ops/ioctl
 * - Local channel uses quick path (direct facade calls, no queue)
 */

#include "wukong_ai_provider.h"
#include "wukong_ai_channel.h"
#include "wukong_ai_agent.h"
#include "tal_thread.h"
#include "tal_queue.h"
#include "tal_memory.h"
#include "uni_log.h"
#include <string.h>

/* --- Default compile macros --- */

#if !defined(ENABLE_PROVIDER_TUYA)
#define ENABLE_PROVIDER_TUYA 1
#endif

#if !defined(WUKONG_DEFAULT_PROVIDER)
#define WUKONG_DEFAULT_PROVIDER "tuya"
#endif

/* --- Provider Manager Context --- */

typedef struct {
    WUKONG_AI_PROVIDER_T *providers[WUKONG_PROVIDER_MAX];
    UINT32_T              provider_cnt;
    WUKONG_AI_PROVIDER_T *cur_provider;
    WUKONG_EVENT_OUTPUT    event_cb;
    THREAD_HANDLE          thread;
} WUKONG_PROVIDER_MGR_T;

STATIC WUKONG_PROVIDER_MGR_T s_prov_mgr = {0};

/* --- Processing Thread --- */

STATIC VOID_T __agent_loop_thread(VOID_T *arg)
{
    (VOID_T)arg;
    QUEUE_HANDLE queue = (QUEUE_HANDLE)wukong_ai_channel_get_queue();
    if (queue == NULL) {
        PR_ERR("provider: channel queue not initialized");
        return;
    }

    while (TRUE) {
        WUKONG_AI_MSG_T *msg = NULL;
        OPERATE_RET rt = tal_queue_fetch(queue, &msg, 0xFFFFffff);
        if (rt != OPRT_OK || msg == NULL) {
            continue;
        }

        WUKONG_AI_PROVIDER_T *prov = s_prov_mgr.cur_provider;
        if (prov == NULL || prov->ops == NULL) {
            tal_free(msg);
            continue;
        }

        if (msg->channel && msg->chat_id) {
            WUKONG_AI_ACTIVE_CHAN_T active = {0};
            strncpy(active.channel, msg->channel, sizeof(active.channel) - 1);
            strncpy(active.chat_id, msg->chat_id, sizeof(active.chat_id) - 1);
            wukong_ai_channel_set_active(&active);
        }

        if (prov->ops->llm_infer != NULL) {
            /* Claw Provider: TODO — call wukong_agent_process() when Agent Loop is ready */
            PR_WARN("Claw provider agent loop not yet implemented");
        } else if (prov->ops->send) {
            prov->ops->send(prov, msg);
        }

        tal_free(msg);
    }
}

/* ================================================================
 * Provider Registry
 * ================================================================ */

OPERATE_RET wukong_ai_provider_register(WUKONG_AI_PROVIDER_T *provider)
{
    TUYA_CHECK_NULL_RETURN(provider, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(provider->ops, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(provider->ops->name, OPRT_INVALID_PARM);

    for (UINT32_T i = 0; i < s_prov_mgr.provider_cnt; i++) {
        if (strcmp(s_prov_mgr.providers[i]->ops->name, provider->ops->name) == 0) {
            return OPRT_RESOURCE_NOT_READY;
        }
    }

    if (s_prov_mgr.provider_cnt >= WUKONG_PROVIDER_MAX) {
        PR_ERR("provider registry full");
        return OPRT_RESOURCE_NOT_READY;
    }

    s_prov_mgr.providers[s_prov_mgr.provider_cnt++] = provider;
    PR_NOTICE("provider registered: %s", provider->ops->name);
    return OPRT_OK;
}

WUKONG_AI_PROVIDER_T *wukong_ai_provider_find(CONST CHAR_T *name)
{
    if (name == NULL) return NULL;

    for (UINT32_T i = 0; i < s_prov_mgr.provider_cnt; i++) {
        if (strcmp(s_prov_mgr.providers[i]->ops->name, name) == 0) {
            return s_prov_mgr.providers[i];
        }
    }
    return NULL;
}

/* ================================================================
 * Provider Framework Init
 * ================================================================ */

OPERATE_RET wukong_ai_provider_init(CONST WUKONG_AI_PROVIDER_CFG_T *cfg)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CALL_ERR_RETURN(wukong_ai_channel_init());

    THREAD_CFG_T thcfg = {
        .priority = THREAD_PRIO_3,
        .stackDepth = 4096,
        .thrdname = "wk_provider",
    };
    TUYA_CALL_ERR_RETURN(tal_thread_create_and_start(&s_prov_mgr.thread, NULL, NULL,
                                                      __agent_loop_thread, NULL, &thcfg));

    /* Register providers controlled by compile macros */
#if (ENABLE_PROVIDER_TUYA == 1)
    extern WUKONG_AI_PROVIDER_T *wukong_provider_tuya_get(VOID_T);
    wukong_ai_provider_register(wukong_provider_tuya_get());
#endif

    /* Select and activate default provider */
    WUKONG_AI_PROVIDER_T *def = wukong_ai_provider_find(WUKONG_DEFAULT_PROVIDER);
    if (def) {
        TUYA_CALL_ERR_RETURN(wukong_ai_agent_switch_provider(def, cfg));
    } else {
        PR_ERR("no provider available");
        return OPRT_COM_ERROR;
    }

    return rt;
}

/* ================================================================
 * Provider Switch
 * ================================================================ */

OPERATE_RET wukong_ai_agent_switch_provider(WUKONG_AI_PROVIDER_T *provider,
                                             CONST WUKONG_AI_PROVIDER_CFG_T *cfg)
{
    WUKONG_AI_PROVIDER_T *old = s_prov_mgr.cur_provider;
    if (old != NULL && old->ops != NULL) {
        if (old->ops->abort) old->ops->abort(old, NULL);
        if (old->ops->deinit) old->ops->deinit(old);
    }

    s_prov_mgr.cur_provider = provider;

    if (provider && provider->ops && provider->ops->init) {
        return provider->ops->init(provider, cfg);
    }
    return OPRT_OK;
}

/* ================================================================
 * Facade API — wukong_ai_agent.h implementation
 * ================================================================ */

OPERATE_RET wukong_ai_agent_init(WUKONG_EVENT_OUTPUT cb)
{
    OPERATE_RET rt = OPRT_OK;

    s_prov_mgr.event_cb = cb;

    TUYA_CALL_ERR_RETURN(wukong_ai_provider_init(NULL));

    return rt;
}

OPERATE_RET wukong_ai_agent_deinit(VOID)
{
    s_prov_mgr.event_cb = NULL;

    if (s_prov_mgr.cur_provider != NULL && s_prov_mgr.cur_provider->ops != NULL) {
        if (s_prov_mgr.cur_provider->ops->deinit) s_prov_mgr.cur_provider->ops->deinit(s_prov_mgr.cur_provider);
    }
    s_prov_mgr.cur_provider = NULL;

    return OPRT_OK;
}

BOOL_T wukong_ai_agent_is_ready(VOID)
{
    if (s_prov_mgr.cur_provider == NULL || s_prov_mgr.cur_provider->ops == NULL) return FALSE;
    if (s_prov_mgr.cur_provider->ops->is_ready == NULL) return TRUE;
    return s_prov_mgr.cur_provider->ops->is_ready(s_prov_mgr.cur_provider);
}

OPERATE_RET wukong_ai_agent_input_start(BOOL_T interrupt)
{
    if (s_prov_mgr.cur_provider == NULL || s_prov_mgr.cur_provider->ops == NULL) return OPRT_COM_ERROR;
    if (s_prov_mgr.cur_provider->ops->ioctl == NULL) return OPRT_OK;
    return s_prov_mgr.cur_provider->ops->ioctl(s_prov_mgr.cur_provider, WUKONG_PROVIDER_CMD_INPUT_START, &interrupt);
}

OPERATE_RET wukong_ai_agent_input_stop(VOID)
{
    if (s_prov_mgr.cur_provider == NULL || s_prov_mgr.cur_provider->ops == NULL) return OPRT_COM_ERROR;
    if (s_prov_mgr.cur_provider->ops->ioctl == NULL) return OPRT_OK;
    return s_prov_mgr.cur_provider->ops->ioctl(s_prov_mgr.cur_provider, WUKONG_PROVIDER_CMD_INPUT_STOP, NULL);
}

OPERATE_RET wukong_ai_agent_output_stop(BOOL_T force)
{
    if (s_prov_mgr.cur_provider == NULL || s_prov_mgr.cur_provider->ops == NULL) return OPRT_COM_ERROR;
    if (s_prov_mgr.cur_provider->ops->ioctl == NULL) return OPRT_OK;
    return s_prov_mgr.cur_provider->ops->ioctl(s_prov_mgr.cur_provider, WUKONG_PROVIDER_CMD_OUTPUT_STOP, &force);
}

OPERATE_RET wukong_ai_agent_chat_break(CONST CHAR_T *chat_id)
{
    if (s_prov_mgr.cur_provider == NULL || s_prov_mgr.cur_provider->ops == NULL) return OPRT_COM_ERROR;
    if (s_prov_mgr.cur_provider->ops->abort == NULL) return OPRT_OK;
    return s_prov_mgr.cur_provider->ops->abort(s_prov_mgr.cur_provider, chat_id);
}

OPERATE_RET wukong_ai_agent_set_scene(CONST CHAR_T *scene)
{
    if (s_prov_mgr.cur_provider == NULL || s_prov_mgr.cur_provider->ops == NULL) return OPRT_COM_ERROR;
    if (s_prov_mgr.cur_provider->ops->ioctl == NULL) return OPRT_OK;
    return s_prov_mgr.cur_provider->ops->ioctl(s_prov_mgr.cur_provider, WUKONG_PROVIDER_CMD_SET_SCENE, (VOID_T *)scene);
}

OPERATE_RET wukong_ai_agent_server_vad_ctrl(BOOL_T enable)
{
    if (s_prov_mgr.cur_provider == NULL || s_prov_mgr.cur_provider->ops == NULL) return OPRT_COM_ERROR;
    if (s_prov_mgr.cur_provider->ops->ioctl == NULL) return OPRT_NOT_SUPPORTED;
    return s_prov_mgr.cur_provider->ops->ioctl(s_prov_mgr.cur_provider, WUKONG_PROVIDER_CMD_SERVER_VAD_CTRL, &enable);
}

OPERATE_RET wukong_ai_agent_set_event_param(VOID_T *param)
{
    if (s_prov_mgr.cur_provider == NULL || s_prov_mgr.cur_provider->ops == NULL) return OPRT_COM_ERROR;
    if (s_prov_mgr.cur_provider->ops->ioctl == NULL) return OPRT_OK;
    return s_prov_mgr.cur_provider->ops->ioctl(s_prov_mgr.cur_provider, WUKONG_PROVIDER_CMD_SET_EVENT_PARAM, param);
}

OPERATE_RET wukong_ai_agent_get_session(CONST CHAR_T *scode, VOID_T **session_out)
{
    if (s_prov_mgr.cur_provider == NULL || s_prov_mgr.cur_provider->ops == NULL) return OPRT_COM_ERROR;
    if (s_prov_mgr.cur_provider->ops->ioctl == NULL) return OPRT_NOT_SUPPORTED;
    WUKONG_PROVIDER_GET_SESSION_ARG_T arg = { .scode = scode, .session = NULL };
    OPERATE_RET rt = s_prov_mgr.cur_provider->ops->ioctl(s_prov_mgr.cur_provider,
                                             WUKONG_PROVIDER_CMD_GET_SESSION, &arg);
    if (rt == OPRT_OK && session_out) *session_out = arg.session;
    return rt;
}

/* --- Local channel quick path send functions --- */

OPERATE_RET wukong_ai_agent_send_text(CHAR_T *content)
{
    if (s_prov_mgr.cur_provider == NULL || s_prov_mgr.cur_provider->ops == NULL || s_prov_mgr.cur_provider->ops->send == NULL)
        return OPRT_COM_ERROR;
    TUYA_CHECK_NULL_RETURN(content, OPRT_INVALID_PARM);
    WUKONG_AI_MSG_T msg = {
        .channel = "local", .chat_id = "local",
        .type = WUKONG_AI_MSG_TYPE_TEXT,
        .data = (CONST BYTE_T *)content, .data_len = strlen(content),
    };
    return s_prov_mgr.cur_provider->ops->send(s_prov_mgr.cur_provider, &msg);
}

OPERATE_RET wukong_ai_agent_send_audio(UINT8_T *data, UINT_T len)
{
    if (s_prov_mgr.cur_provider == NULL || s_prov_mgr.cur_provider->ops == NULL || s_prov_mgr.cur_provider->ops->send == NULL)
        return OPRT_COM_ERROR;
    TUYA_CHECK_NULL_RETURN(data, OPRT_INVALID_PARM);
    WUKONG_AI_MSG_T msg = {
        .channel = "local", .chat_id = "local",
        .type = WUKONG_AI_MSG_TYPE_AUDIO,
        .data = (CONST BYTE_T *)data, .data_len = len,
    };
    return s_prov_mgr.cur_provider->ops->send(s_prov_mgr.cur_provider, &msg);
}

OPERATE_RET wukong_ai_agent_send_image(UINT8_T *data, UINT_T len)
{
    if (s_prov_mgr.cur_provider == NULL || s_prov_mgr.cur_provider->ops == NULL || s_prov_mgr.cur_provider->ops->send == NULL)
        return OPRT_COM_ERROR;
    WUKONG_AI_MSG_T msg = {
        .channel = "local", .chat_id = "local",
        .type = WUKONG_AI_MSG_TYPE_IMAGE,
        .data = (CONST BYTE_T *)data, .data_len = len,
    };
    return s_prov_mgr.cur_provider->ops->send(s_prov_mgr.cur_provider, &msg);
}

OPERATE_RET wukong_ai_agent_send_video(UINT8_T *data, UINT_T len)
{
    if (s_prov_mgr.cur_provider == NULL || s_prov_mgr.cur_provider->ops == NULL || s_prov_mgr.cur_provider->ops->send == NULL)
        return OPRT_COM_ERROR;
    WUKONG_AI_MSG_T msg = {
        .channel = "local", .chat_id = "local",
        .type = WUKONG_AI_MSG_TYPE_VIDEO,
        .data = (CONST BYTE_T *)data, .data_len = len,
    };
    return s_prov_mgr.cur_provider->ops->send(s_prov_mgr.cur_provider, &msg);
}

OPERATE_RET wukong_ai_agent_send_file(UINT8_T *data, UINT_T len)
{
    if (s_prov_mgr.cur_provider == NULL || s_prov_mgr.cur_provider->ops == NULL || s_prov_mgr.cur_provider->ops->send == NULL)
        return OPRT_COM_ERROR;
    WUKONG_AI_MSG_T msg = {
        .channel = "local", .chat_id = "local",
        .type = WUKONG_AI_MSG_TYPE_FILE,
        .data = (CONST BYTE_T *)data, .data_len = len,
    };
    return s_prov_mgr.cur_provider->ops->send(s_prov_mgr.cur_provider, &msg);
}

OPERATE_RET wukong_ai_agent_cloud_alert(INT_T type)
{
    if (s_prov_mgr.cur_provider == NULL || s_prov_mgr.cur_provider->ops == NULL) return OPRT_COM_ERROR;
    if (s_prov_mgr.cur_provider->ops->ioctl == NULL) return OPRT_NOT_SUPPORTED;
    return s_prov_mgr.cur_provider->ops->ioctl(s_prov_mgr.cur_provider, WUKONG_PROVIDER_CMD_ALERT, &type);
}

OPERATE_RET wukong_ai_agent_role_switch(CONST CHAR_T *role)
{
    if (s_prov_mgr.cur_provider == NULL || s_prov_mgr.cur_provider->ops == NULL) return OPRT_COM_ERROR;
    if (s_prov_mgr.cur_provider->ops->ioctl == NULL) return OPRT_NOT_SUPPORTED;
    return s_prov_mgr.cur_provider->ops->ioctl(s_prov_mgr.cur_provider, WUKONG_PROVIDER_CMD_SWITCH_TARGET, (VOID_T *)role);
}

/* --- Event Notify --- */

VOID wukong_ai_event_notify(WUKONG_AI_EVENT_TYPE_E type, VOID *data)
{
    WUKONG_AI_EVENT_T event = {0};

    if (s_prov_mgr.event_cb) {
        event.data = data;
        event.type = type;
        s_prov_mgr.event_cb(&event);
    }
}

VOID wukong_ai_event_notify_ext(CONST WUKONG_AI_EVENT_T *event)
{
    if (s_prov_mgr.event_cb && event) {
        s_prov_mgr.event_cb((WUKONG_AI_EVENT_T *)event);
    }
}
