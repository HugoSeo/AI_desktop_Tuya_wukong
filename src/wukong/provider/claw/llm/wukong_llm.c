/**
 * @file wukong_llm.c
 * @brief Claw LLM runtime: shared HTTP transport + per-backend format handlers.
 */
#include "wukong_llm.h"
#include "wukong_tool.h"
#include "tuya_simple_http.h"
#include "httpc.h"
#include "ty_cJSON.h"
#include "tal_memory.h"
#include "mix_method.h"   /* mm_strdup */
#include "uni_log.h"
#include <string.h>
#include <stdio.h>

/* LLM responses are slow; allow a generous 120s socket timeout */
#define WUKONG_LLM_HTTP_TIMEOUT_MS (120 * 1000)

struct wukong_llm_runtime {
    WUKONG_LLM_BACKEND_E backend;
    CHAR_T *base_url;
    CHAR_T *api_key;
    CHAR_T *model;
};

/* PR_DEBUG has a ~1024-byte per-line cap; a model reply can exceed it, so print
 * in chunks. Chunk boundaries snap to a UTF-8 character boundary so a multi-byte
 * char (e.g. a 3-byte Chinese char) is never split across two log lines. */
#define WUKONG_LLM_LOG_CHUNK 800
STATIC VOID_T __log_chunked(CONST CHAR_T *tag, CONST CHAR_T *s)
{
    if (!s) { PR_DEBUG("%s (null)", tag); return; }
    SIZE_T len = strlen(s);
    SIZE_T off = 0;
    INT_T part = 0;
    do {
        SIZE_T n = len - off;
        if (n > WUKONG_LLM_LOG_CHUNK) {
            n = WUKONG_LLM_LOG_CHUNK;
            while (n > 0 && ((BYTE_T)s[off + n] & 0xC0) == 0x80) n--;   /* back off to char boundary */
            if (n == 0) n = WUKONG_LLM_LOG_CHUNK;
        }
        PR_DEBUG("%s[%d] %.*s", tag, part, (INT_T)n, s + off);
        off += n;
        part++;
    } while (off < len);
}

/* ════════ shared: format-agnostic HTTP transport ════════ */
STATIC OPERATE_RET __llm_http_post(CONST CHAR_T *url, CONST CHAR_T *body,
                                   HTTP_HEAD_ADD_CB head_cb, VOID_T *head_data,
                                   CHAR_T **out_raw, INT_T *out_code, CHAR_T **err)
{
    simple_http_opts_t opts = {0};
    opts.add_head_cb = head_cb;
    opts.add_head_data = head_data;
    simple_http_response_t hr = {0};

    UINT_T body_len = (UINT_T)strlen(body);
    PR_DEBUG("llm http post url=%s body_len=%u", url, body_len);
    OPERATE_RET rt = tuya_simple_http_post(url, (CONST BYTE_T *)body,
                                           body_len, &opts, &hr);
    if (rt != OPRT_OK) {
        if (err) *err = mm_strdup("http transport failed");
        return OPRT_COM_ERROR;
    }
    *out_code = hr.http_code;
    *out_raw = hr.data ? mm_strdup((CONST CHAR_T *)hr.data) : NULL;
    PR_DEBUG("llm http resp code=%d", hr.http_code);
    if (hr.http_code != 200 && hr.data) {
        PR_ERR("llm http error body: %s", (CONST CHAR_T *)hr.data);
        __log_chunked("llm req sent:", body);   /* what we actually sent */
    }
    if (hr.data) SIMPLE_HTTP_FREE(hr.data);
    return OPRT_OK;
}

/* ════════ OpenAI backend (M1) ════════ */
STATIC VOID_T __openai_headers(http_session_t s, VOID_T *data)
{
    http_set_timeout(s, WUKONG_LLM_HTTP_TIMEOUT_MS);
    CHAR_T buf[160];
    snprintf(buf, sizeof(buf), "Bearer %s", data ? (CONST CHAR_T *)data : "");
    http_add_header(s, NULL, "Authorization", buf);
    http_add_header(s, NULL, "Content-Type", "application/json");
}

STATIC CHAR_T *__openai_build_body(CONST WUKONG_LLM_REQ_T *req, CONST CHAR_T *model)
{
    ty_cJSON *body = ty_cJSON_CreateObject();
    if (!body) return NULL;
    ty_cJSON_AddStringToObject(body, "model", model);

    ty_cJSON *msgs = ty_cJSON_CreateArray();
    if (!msgs) { ty_cJSON_Delete(body); return NULL; }
    /* system_prompt → messages[0] */
    if (req->system_prompt) {
        ty_cJSON *sys = ty_cJSON_CreateObject();
        ty_cJSON_AddStringToObject(sys, "role", "system");
        ty_cJSON_AddStringToObject(sys, "content", req->system_prompt);
        ty_cJSON_AddItemToArray(msgs, sys);
    }
    /* req->messages is already OpenAI-shaped; deep-copy each item into body.
     * (host stub cJSON lacks ty_cJSON_DetachItemViaPointer, so append per-index
     * Duplicate instead of duplicating the whole array then splitting) */
    if (req->messages) {
        INT_T n = ty_cJSON_GetArraySize(req->messages);
        for (INT_T i = 0; i < n; i++) {
            ty_cJSON *item = ty_cJSON_GetArrayItem(req->messages, i);
            ty_cJSON *dup = ty_cJSON_Duplicate(item, 1);
            if (dup) ty_cJSON_AddItemToArray(msgs, dup);
        }
    }
    ty_cJSON_AddItemToObject(body, "messages", msgs);

    /* tools: 由 backend 构建(它才知道 OpenAI 格式);无工具则不带该字段
     * TODO(接口债): req->tools_json 在此被忽略——context 的 __build_tools_json
     * 仍是 M2a 的 NULL 桩, 工具全靠这里无条件构建兜底; 压缩等无工具请求也因此
     * 背上全部工具 schema(~15-20KB)。收拾时机: 供货责任还给 context(牵动
     * context/agent 测试套件的链接与桩), 见 ledger 2026-08-17。 */
    CHAR_T *tools = wukong_tool_build_schema(WUKONG_LLM_BACKEND_OPENAI);
    INT_T tool_count = 0;
    if (tools) {
        ty_cJSON *tj = ty_cJSON_Parse(tools);
        if (tj) { tool_count = ty_cJSON_GetArraySize(tj); ty_cJSON_AddItemToObject(body, "tools", tj); }
        tal_free(tools);
    }

    PR_NOTICE("llm req: model=%s tools=%d msgs=%d",
              model ? model : "?", tool_count, ty_cJSON_GetArraySize(msgs));

    CHAR_T *s = ty_cJSON_PrintUnformatted(body);
    ty_cJSON_Delete(body);
    return s;
}

STATIC OPERATE_RET __openai_parse(CONST CHAR_T *raw, WUKONG_LLM_RESP_T *resp)
{
    ty_cJSON *root = ty_cJSON_Parse(raw);
    if (!root) return OPRT_COM_ERROR;
    OPERATE_RET rt = OPRT_COM_ERROR;
    ty_cJSON *choices = ty_cJSON_GetObjectItem(root, "choices");
    if (ty_cJSON_IsArray(choices)) {
        ty_cJSON *c0 = ty_cJSON_GetArrayItem(choices, 0);
        ty_cJSON *msg = c0 ? ty_cJSON_GetObjectItem(c0, "message") : NULL;
        ty_cJSON *content = msg ? ty_cJSON_GetObjectItem(msg, "content") : NULL;
        /* host stub cJSON lacks ty_cJSON_GetStringValue; read valuestring directly */
        CONST CHAR_T *txt = (content && ty_cJSON_IsString(content)) ? content->valuestring : NULL;
        if (txt) { resp->text = mm_strdup(txt); rt = OPRT_OK; }
        if (resp->text && resp->text[0]) __log_chunked("llm reply:", resp->text);

        ty_cJSON *rc = msg ? ty_cJSON_GetObjectItem(msg, "reasoning_content") : NULL;
        if (rc && ty_cJSON_IsString(rc) && rc->valuestring[0]) resp->reasoning = mm_strdup(rc->valuestring);

        ty_cJSON *tcs = msg ? ty_cJSON_GetObjectItem(msg, "tool_calls") : NULL;
        if (ty_cJSON_IsArray(tcs)) {
            INT_T tn = ty_cJSON_GetArraySize(tcs);
            if (tn > 0) {
                resp->tool_calls = tal_calloc((SIZE_T)tn, sizeof(*resp->tool_calls));
                if (resp->tool_calls) {
                    for (INT_T i = 0; i < tn; i++) {
                        ty_cJSON *tc = ty_cJSON_GetArrayItem(tcs, i);
                        ty_cJSON *idj = tc ? ty_cJSON_GetObjectItem(tc, "id") : NULL;
                        ty_cJSON *fnj = tc ? ty_cJSON_GetObjectItem(tc, "function") : NULL;
                        ty_cJSON *nmj = fnj ? ty_cJSON_GetObjectItem(fnj, "name") : NULL;
                        ty_cJSON *agj = fnj ? ty_cJSON_GetObjectItem(fnj, "arguments") : NULL;
                        resp->tool_calls[i].id   = (idj && ty_cJSON_IsString(idj)) ? mm_strdup(idj->valuestring) : mm_strdup("");
                        resp->tool_calls[i].name = (nmj && ty_cJSON_IsString(nmj)) ? mm_strdup(nmj->valuestring) : mm_strdup("");
                        resp->tool_calls[i].arguments_json = (agj && ty_cJSON_IsString(agj)) ? mm_strdup(agj->valuestring) : mm_strdup("{}");
                        PR_NOTICE("llm tool_call: %s args=%s",
                                  resp->tool_calls[i].name, resp->tool_calls[i].arguments_json);
                    }
                    resp->tool_call_count = (UINT32_T)tn;
                    rt = OPRT_OK;   /* 有 tool_calls 也算成功返回 */
                }
            }
        }
    }

    /* wire summary: why the round ended + token usage */
    ty_cJSON *c0s   = ty_cJSON_IsArray(choices) ? ty_cJSON_GetArrayItem(choices, 0) : NULL;
    ty_cJSON *fr    = c0s ? ty_cJSON_GetObjectItem(c0s, "finish_reason") : NULL;
    ty_cJSON *usage = ty_cJSON_GetObjectItem(root, "usage");
    ty_cJSON *pt    = usage ? ty_cJSON_GetObjectItem(usage, "prompt_tokens") : NULL;
    ty_cJSON *ct    = usage ? ty_cJSON_GetObjectItem(usage, "completion_tokens") : NULL;
    PR_NOTICE("llm resp: finish=%s tokens=%d/%d",
              (fr && ty_cJSON_IsString(fr)) ? fr->valuestring : "?",
              pt ? pt->valueint : 0, ct ? ct->valueint : 0);

    ty_cJSON_Delete(root);
    return rt;
}

STATIC OPERATE_RET __openai_chat(WUKONG_LLM_RT_T *rt, CONST WUKONG_LLM_REQ_T *req,
                                 WUKONG_LLM_RESP_T *resp, CHAR_T **err)
{
    CHAR_T url[256];
    snprintf(url, sizeof(url), "%s/chat/completions", rt->base_url);
    CHAR_T *body = __openai_build_body(req, rt->model);
    if (!body) return OPRT_MALLOC_FAILED;

    CHAR_T *raw = NULL; INT_T code = 0;
    OPERATE_RET ret = __llm_http_post(url, body, __openai_headers, rt->api_key,
                                      &raw, &code, err);
    tal_free(body);
    if (ret != OPRT_OK) { tal_free(raw); return ret; }
    if (code != 200) {
        if (err && !*err) *err = mm_strdup(raw ? raw : "http error");
        tal_free(raw);
        return OPRT_COM_ERROR;
    }
    ret = __openai_parse(raw, resp);
    tal_free(raw);
    return ret;
}

/* ════════ public API ════════ */
OPERATE_RET wukong_llm_runtime_create(CONST WUKONG_AI_PROVIDER_CFG_T *cfg,
                                      WUKONG_LLM_RT_T **out_rt)
{
    if (!cfg || !out_rt) return OPRT_INVALID_PARM;
    if (!cfg->api_key || !cfg->api_key[0]) {
        PR_ERR("claw llm: api_key missing");
        return OPRT_INVALID_PARM;
    }
    WUKONG_LLM_RT_T *rt = tal_calloc(1, sizeof(*rt));
    if (!rt) return OPRT_MALLOC_FAILED;
    rt->backend  = cfg->backend;
    rt->base_url = mm_strdup(cfg->base_url ? cfg->base_url : "https://api.openai.com/v1");
    rt->api_key  = mm_strdup(cfg->api_key);
    rt->model    = mm_strdup(cfg->model ? cfg->model : "gpt-4o");
    *out_rt = rt;
    return OPRT_OK;
}

OPERATE_RET wukong_llm_runtime_chat(WUKONG_LLM_RT_T *rt, CONST WUKONG_LLM_REQ_T *req,
                                    WUKONG_LLM_RESP_T *resp, CHAR_T **out_error)
{
    if (!rt || !req || !resp) return OPRT_INVALID_PARM;
    switch (rt->backend) {
    case WUKONG_LLM_BACKEND_OPENAI:    return __openai_chat(rt, req, resp, out_error);
    case WUKONG_LLM_BACKEND_ANTHROPIC: return OPRT_NOT_SUPPORTED;   /* later: __anthropic_chat */
    default:                           return OPRT_NOT_SUPPORTED;
    }
}

VOID_T wukong_llm_runtime_destroy(WUKONG_LLM_RT_T *rt)
{
    if (!rt) return;
    tal_free(rt->base_url); tal_free(rt->api_key); tal_free(rt->model);
    tal_free(rt);
}

VOID_T wukong_llm_resp_free(WUKONG_LLM_RESP_T *resp)
{
    if (!resp) return;
    tal_free(resp->text);      resp->text = NULL;
    tal_free(resp->reasoning); resp->reasoning = NULL;
    for (UINT32_T i = 0; i < resp->tool_call_count; i++) {
        tal_free(resp->tool_calls[i].id);
        tal_free(resp->tool_calls[i].name);
        tal_free(resp->tool_calls[i].arguments_json);
    }
    tal_free(resp->tool_calls); resp->tool_calls = NULL;
    resp->tool_call_count = 0;
}
