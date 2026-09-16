/**
 * @file wukong_provider_claw.c
 * @brief Claw provider thin shell — holds an LLM runtime, maps ops.llm_infer.
 */
#include "wukong_ai_provider.h"
#include "wukong_llm.h"
#include "wukong_fc.h"
#include "memory/wukong_memory.h"
#include "memory/wukong_session.h"
#include "context/wukong_profile.h"
#include "tools/memory_tool.h"
#include "tools/profile_tool.h"
#include "config/claw_config.h"
#include "wukong_storage.h"
#include "tuya_app_config.h"
#include "tuya_ws_db.h"
#include "base_event.h"
#include "tal_workq_service.h"
#include "mqc_app.h"
#include "ty_cJSON.h"
#include "tal_log.h"
#include <string.h>

/* Tuya-cloud skill service (e.g. music_list search) replies over MQTT
 * protocol 9000. That handler is normally registered by the Tuya-cloud AI
 * client, which Claw does not run — so responses would be dropped
 * ("MQ Not Find 9000"). Register a slim SKILL-only bridge that forwards to the
 * shared skill dispatcher; other bizTypes on 9000 are none of Claw's business. */
#define CLAW_SKILL_MQ_PROTO 9000

/* LLM credentials, KV-first (see __claw_resolve_kv): a compile-time macro only
 * seeds the KV on first boot, runtime source of truth is the KV store so
 * wukong_provider_claw_set_llm() can rewrite it without a rebuild. */
#define CLAW_KV_LLM_KEY   "claw_llm_key"
#define CLAW_KV_LLM_URL   "claw_llm_base_url"
#define CLAW_KV_LLM_MODEL "claw_llm_model"

STATIC WUKONG_LLM_RT_T *s_chat_rt = NULL;
STATIC BOOL_T s_storage_evt_subscribed = FALSE;

/* The claw data volume comes up asynchronously (wukong_storage mounts on
 * WORKQ_SYSTEM). Init below builds empty tables so the provider works right
 * away; once the volume is usable this catch-up re-reads memory and profile
 * from fs (skill owns its own storage.ready subscription — see
 * wukong_skill_init). Runs on WORKQ_SYSTEM — reloads parse JSON and touch fs,
 * neither belongs on the event-dispatch thread. */
STATIC VOID_T __claw_storage_catchup(VOID_T *data)
{
    (VOID_T)data;
    claw_config_load();   /* must land before reload so mem_max etc. reflect wukong.json */
#if defined(ENABLE_TOOLKITS_MEMORY) && (ENABLE_TOOLKITS_MEMORY == 1)
    wukong_memory_reload();
#endif
    wukong_profile_reload();
    wukong_session_init();
    TAL_PR_NOTICE("claw -> storage ready, fs data reloaded");
}

STATIC INT_T __claw_on_storage_ready(VOID_T *data)
{
    (VOID_T)data;
    if (tal_workq_schedule(WORKQ_SYSTEM, __claw_storage_catchup, NULL) != OPRT_OK) {
        TAL_PR_WARN("claw -> storage catch-up schedule failed, fs data may be stale");
    }
    return OPRT_OK;
}

STATIC OPERATE_RET __claw_skill_mq_handle(ty_cJSON *root)
{
    ty_cJSON *l1  = ty_cJSON_GetObjectItem(root, "data");         /* {bizType,data} */
    ty_cJSON *biz = l1 ? ty_cJSON_GetObjectItem(l1, "bizType") : NULL;
    if (biz && ty_cJSON_IsString(biz) && strcmp(biz->valuestring, "SKILL") == 0) {
        ty_cJSON *inner = ty_cJSON_GetObjectItem(l1, "data");     /* {code,action,...} */
        if (inner) wukong_fc_process(AI_TEXT_SKILL, inner, TRUE);
    }
    return OPRT_OK;
}

/* Runs once wukong_llm_runtime_create() has populated s_chat_rt — called by
 * __claw_init to wire up the runtime. */
STATIC VOID_T __claw_activate_runtime(VOID_T *handle)
{
    /* handle == the provider (ops->init): publish the runtime as ctx
     * for wukong_compact.c */
    OPERATE_RET rt = OPRT_OK;
    if (handle) ((WUKONG_AI_PROVIDER_T *)handle)->ctx = s_chat_rt;
    mqc_app_register_cb(CLAW_SKILL_MQ_PROTO, __claw_skill_mq_handle);
    /* Claw-fundamental tools: registered by their owning domain (same shape
     * as fc executors / skill_tool), AGENT-only, always on when claw is. */
    TUYA_CALL_ERR_LOG(memory_tool_init());
    TUYA_CALL_ERR_LOG(profile_tool_init());
#if defined(ENABLE_TOOLKITS_MEMORY) && (ENABLE_TOOLKITS_MEMORY == 1)
    /* Allocate the memory cache (loads empty until storage is ready). */
    wukong_memory_init();
#endif
    /* Volume mounts asynchronously: subscribe FIRST, then catch up if
     * storage won the race (canonical consumer pattern, see tm/picture). */
    if (!s_storage_evt_subscribed) {
        if (ty_subscribe_event(EVENT_WUKONG_STORAGE_READY, "claw",
                               __claw_on_storage_ready, SUBSCRIBE_TYPE_NORMAL) != OPRT_OK) {
            TAL_PR_ERR("claw -> subscribe storage.ready failed; fs data will not load");
        } else {
            s_storage_evt_subscribed = TRUE;
        }
    }
    if (s_storage_evt_subscribed && wukong_storage_ready()) {
        (VOID_T)__claw_on_storage_ready(NULL);
    }
}

/* first-boot 三态,照抄 feishu __feishu_resolve_cred:KV 命中用之;未命中且 macro
 * 非空则播种(写回 KV,下次直读);都空则 out="" 交给调用方判断"待配置"。 */
STATIC VOID_T __claw_resolve_kv(CONST CHAR_T *k, CONST CHAR_T *macro, CHAR_T *out, UINT_T cap)
{
    out[0] = '\0';
    BYTE_T *v = NULL;
    UINT_T n = 0;
    if (wd_common_read(k, &v, &n) == OPRT_OK && v && n > 0) {
        UINT_T c = (n < cap - 1) ? n : (cap - 1);
        memcpy(out, v, c);
        out[c] = '\0';
        wd_common_free_data(v);
        return;
    }
    if (v) {
        wd_common_free_data(v);
    }
    if (macro && macro[0]) {
        wd_common_write(k, (CONST BYTE_T *)macro, (UINT_T)strlen(macro));
        snprintf(out, cap, "%s", macro);
    }
}

STATIC OPERATE_RET __claw_init(VOID_T *handle, CONST WUKONG_AI_PROVIDER_CFG_T *cfg)
{
    WUKONG_AI_PROVIDER_CFG_T local = {0};
    if (cfg) {
        local = *cfg;
    } else {
        /* static: KV is read early (no storage.ready dependency), so local's
         * pointers must stay valid for the runtime's lifetime — a stack buffer
         * would dangle once __claw_init returns. Single-instance provider, so
         * one static set is enough (also reused by set_llm's rebuild call).
         * Allocated once via the claw alloc seam; never freed (runtime singleton). */
        STATIC CHAR_T *s_key = NULL, *s_url = NULL, *s_model = NULL;
        if (s_key == NULL) {
            s_key = wukong_claw_malloc(CLAW_FIXED_LLM_BUF);
        }
        if (s_url == NULL) {
            s_url = wukong_claw_malloc(CLAW_FIXED_LLM_BUF);
        }
        if (s_model == NULL) {
            s_model = wukong_claw_malloc(CLAW_FIXED_LLM_BUF);
        }
        if (!s_key || !s_url || !s_model) {
            return OPRT_MALLOC_FAILED;
        }
        __claw_resolve_kv(CLAW_KV_LLM_KEY,   CLAW_LLM_API_KEY,  s_key,   CLAW_FIXED_LLM_BUF);
        __claw_resolve_kv(CLAW_KV_LLM_URL,   CLAW_LLM_BASE_URL, s_url,   CLAW_FIXED_LLM_BUF);
        __claw_resolve_kv(CLAW_KV_LLM_MODEL, CLAW_LLM_MODEL,    s_model, CLAW_FIXED_LLM_BUF);
        if (s_key[0] == '\0') {
            TAL_PR_NOTICE("claw: LLM key 未配置,等待 claw_config set llm");
            return OPRT_OK;
        }
        local.backend  = (WUKONG_LLM_BACKEND_E)CLAW_LLM_BACKEND;
        local.base_url = s_url;
        local.model    = s_model;
        local.api_key  = s_key;
    }
    OPERATE_RET rt = wukong_llm_runtime_create(&local, &s_chat_rt);
    if (rt == OPRT_OK) {
        __claw_activate_runtime(handle);
    }
    return rt;
}

STATIC OPERATE_RET __claw_deinit(VOID_T *handle)
{
    mqc_app_unregister_cb(CLAW_SKILL_MQ_PROTO, __claw_skill_mq_handle);
    if (s_storage_evt_subscribed) {
        ty_unsubscribe_event(EVENT_WUKONG_STORAGE_READY, "claw", __claw_on_storage_ready);
        s_storage_evt_subscribed = FALSE;
    }
    WUKONG_LLM_RT_T *victim = s_chat_rt;   /* null-before-free: 收窄 __claw_llm_infer 并发读悬空(同 feishu) */
    s_chat_rt = NULL;
    wukong_llm_runtime_destroy(victim);
    if (handle) ((WUKONG_AI_PROVIDER_T *)handle)->ctx = NULL;
    wukong_session_deinit();   /* release session handle; no memory_deinit exists yet */
    return OPRT_OK;
}

STATIC BOOL_T __claw_is_ready(VOID_T *handle)
{
    (VOID_T)handle;
    return s_chat_rt != NULL;
}


STATIC OPERATE_RET __claw_llm_infer(VOID_T *handle, CONST WUKONG_LLM_REQ_T *req,
                                    WUKONG_LLM_RESP_T *resp, CHAR_T **out_error)
{
    (VOID_T)handle;
    /* M1: single runtime; later select s_chat_rt / s_summary_rt by scene */
    return wukong_llm_runtime_chat(s_chat_rt, req, resp, out_error);
}

STATIC CONST WUKONG_AI_PROVIDER_OPS_T s_claw_ops = {
    .name      = "claw",
    .caps      = WUKONG_AI_CAP_TEXT,
    .init      = __claw_init,
    .deinit    = __claw_deinit,
    .is_ready  = __claw_is_ready,
    .send      = NULL,             /* not a Cloud provider; no passthrough */
    .llm_infer = __claw_llm_infer, /* processing thread routes to Claw via this */
    .mcp_send  = NULL,
    .abort     = NULL,
    .ioctl     = NULL,
};

STATIC WUKONG_AI_PROVIDER_T s_claw_provider = { .ops = &s_claw_ops, .ctx = NULL };

WUKONG_AI_PROVIDER_T *wukong_provider_claw_get(VOID_T)
{
    /* claw CLI 随 provider 注册一次性拉起(留在 claw 内,app 不引用 claw 符号)。 */
    STATIC BOOL_T s_cli_registered = FALSE;
    if (!s_cli_registered) {
        extern int claw_cli_init(void);
        claw_cli_init();
        s_cli_registered = TRUE;
    }
    return &s_claw_provider;
}

/* Write the three KV then rebuild the runtime, so a change takes effect
 * without a reboot (mirrors feishu_channel_set_creds's deinit/init restart). */
OPERATE_RET wukong_provider_claw_set_llm(CONST CHAR_T *base_url, CONST CHAR_T *model, CONST CHAR_T *api_key)
{
    if (!base_url || !model || !api_key || !base_url[0] || !model[0] || !api_key[0]) {
        return OPRT_INVALID_PARM;
    }
    wd_common_write(CLAW_KV_LLM_URL,   (CONST BYTE_T *)base_url, (UINT_T)strlen(base_url));
    wd_common_write(CLAW_KV_LLM_MODEL, (CONST BYTE_T *)model,    (UINT_T)strlen(model));
    wd_common_write(CLAW_KV_LLM_KEY,   (CONST BYTE_T *)api_key,  (UINT_T)strlen(api_key));
    __claw_deinit(&s_claw_provider);
    return __claw_init(&s_claw_provider, NULL);   /* 重读三 KV + create */
}

OPERATE_RET wukong_provider_claw_get_llm_masked(CHAR_T *out, UINT_T out_cap)
{
    if (!out || !out_cap) {
        return OPRT_INVALID_PARM;
    }
    CHAR_T url[CLAW_FIXED_LLM_BUF] = {0}, model[CLAW_FIXED_LLM_BUF] = {0}, key[CLAW_FIXED_LLM_BUF] = {0};
    __claw_resolve_kv(CLAW_KV_LLM_URL,   "", url,   sizeof(url));   /* 只读:macro 传空不播种 */
    __claw_resolve_kv(CLAW_KV_LLM_MODEL, "", model, sizeof(model));
    __claw_resolve_kv(CLAW_KV_LLM_KEY,   "", key,   sizeof(key));

    CHAR_T masked[16] = "(unset)";
    if (key[0]) {
        snprintf(masked, sizeof(masked), "%.3s****", key);
    }
    snprintf(out, out_cap, "llm base_url=%s model=%s api_key=%s",
             url[0] ? url : "(unset)", model[0] ? model : "(unset)", masked);
    return OPRT_OK;
}
