#include "wukong_agent_loop.h"
#include "wukong_context.h"
#include "wukong_session.h"
#include "wukong_llm.h"
#include "wukong_ai_agent.h"
#include "wukong_fc.h"
#include "wukong_tool.h"
#include "claw_config.h"
#include "ty_cJSON.h"
#include "tal_memory.h"
#include "uni_log.h"
#include <string.h>
#include <stdio.h>

/* Max bytes of a tool result shown in the ReAct "Observation" preview (the full
 * result still goes to the model); longer results are truncated with "...". */
#define WK_OBS_PREVIEW_MAX 1024

/* Fixed user-facing lines (centralized for easy copy updates).
 * REPLY_BLANK_*: stop round ended with blank content and no reasoning — ask
 * the user to retry instead of staying silent. REPLY_FAILED: the LLM request
 * itself failed (network/provider error). */
#define WK_REPLY_BLANK_ZH  "抱歉，刚才没能回复，可以再说一遍吗？"
#define WK_REPLY_BLANK_EN  "Sorry, I couldn't produce a reply — could you say that again?"
#define WK_REPLY_FAILED_ZH "刚没连上，等下再试试"

/* Experimental: push one standalone text message to the active channel so the
 * user sees intermediate progress (thinking / interim reply / tool notices)
 * during a multi-round loop instead of waiting for the final message. Each
 * call produces one IM message (START records routing, empty STOP flushes). */
STATIC VOID_T __push_channel_text(CONST CHAR_T *text)
{
    if (!text || !text[0]) return;
    WUKONG_AI_TEXT_T t = { .data = (CHAR_T *)text, .datalen = (UINT16_T)strlen(text), .timeindex = 0 };
    wukong_ai_event_notify(WUKONG_AI_EVENT_TEXT_STREAM_START, &t);
    WUKONG_AI_TEXT_T eot = { .data = "", .datalen = 0, .timeindex = 0 };
    wukong_ai_event_notify(WUKONG_AI_EVENT_TEXT_STREAM_STOP, &eot);
}

/* glm sometimes leaks its tool-call/reasoning template markup into plain
 * text fields (four distinct on-device sightings). Returns the earliest
 * marker position in @p s, or NULL when the text is clean — callers cut
 * there or refuse the text entirely. */
STATIC CONST CHAR_T *__find_template_marker(CONST CHAR_T *s)
{
    STATIC CONST CHAR_T *CONST marks[] = {
        "<tool_call>", "</tool_call>", "<arg_key>", "</arg_key>",
        "<arg_value>", "</arg_value>", "<think>", "</think>", NULL,
    };
    CONST CHAR_T *first = NULL;
    if (!s) return NULL;
    for (UINT_T i = 0; marks[i] != NULL; i++) {
        CONST CHAR_T *hit = strstr(s, marks[i]);
        if (hit && (first == NULL || hit < first)) first = hit;
    }
    return first;
}

/* Heuristic: any non-ASCII byte means the user wrote CJK (Chinese). Used to
 * localize the intermediate-state prefixes to the user's language. */
STATIC BOOL_T __text_is_cjk(CONST CHAR_T *text)
{
    if (!text) return FALSE;
    for (CONST CHAR_T *p = text; *p; p++)
        if ((BYTE_T)*p >= 0x80) return TRUE;
    return FALSE;
}

OPERATE_RET wukong_agent_loop_run(CONST CHAR_T *chat_id, CONST CHAR_T *text,
                                  WUKONG_AI_PROVIDER_T *provider, CHAR_T **out_text)
{
    if (!provider || !provider->ops || !provider->ops->llm_infer || !out_text)
        return OPRT_INVALID_PARM;
    *out_text = NULL;

    ty_cJSON *runtime = NULL;   /* M2a: no tool history */
    UINT32_T iter = 0;
    OPERATE_RET rt = OPRT_COM_ERROR;

    UINT_T hist_msgs = 0, hist_bytes = 0;
    wukong_session_stat(chat_id, &hist_msgs, &hist_bytes);
    PR_NOTICE("agent: chat=%s user=%s", chat_id ? chat_id : "?", text ? text : "");
    PR_NOTICE("agent history: msgs=%u bytes=%u", hist_msgs, hist_bytes);

    /* ReAct trace to the channel: the model's short plan sentence (💭), then one
     * message per tool that combines action (🎬) + observation (👀) — the
     * original two lines, now a single IM send. zh selects the labels. */
    BOOL_T zh = __text_is_cjk(text);
    CONST CHAR_T *pfx_act = zh ? "🎬 行动: " : "🎬 Action: ";
    CONST CHAR_T *pfx_obs = zh ? "👀 观察: " : "👀 Observation: ";

    while (iter < (UINT32_T)claw_config_get()->agent_max_iter) {
        PR_DEBUG("agent round=%u", iter);
        WUKONG_LLM_REQ_T req = {0};
        if (wukong_context_build(chat_id, text, runtime, &req) != OPRT_OK) break;

        WUKONG_LLM_RESP_T resp = {0};
        CHAR_T *err = NULL;
        OPERATE_RET ir = provider->ops->llm_infer(provider, &req, &resp, &err);
        wukong_context_free(&req);

        if (ir != OPRT_OK) {
            PR_ERR("claw llm_infer failed rt=%d err=%s", ir, err ? err : "");
            rt = ir;   /* propagate the provider error to the caller */
            tal_free(err); wukong_llm_resp_free(&resp);
            break;
        }
        /* A round whose tool_calls are all nameless (glm jitter) cannot be
         * executed — treat it as a final answer instead of spinning. */
        BOOL_T any_valid_call = FALSE;
        for (UINT32_T vi = 0; vi < resp.tool_call_count; vi++) {
            CONST CHAR_T *nm = resp.tool_calls[vi].name;
            if (nm && nm[0]) { any_valid_call = TRUE; break; }
        }
        if (!any_valid_call) {                           /* stop round (M2a always here) */
            /* Final answer, with a fallback chain — glm sometimes leaves the
             * stop-round content blank/whitespace (everything sits in
             * reasoning); a blank reply must never end the turn as silence.
             * Leaked template markup is cut off (or disqualifies the text). */
            if (resp.text) {
                CHAR_T *mark = (CHAR_T *)__find_template_marker(resp.text);
                if (mark) *mark = '\0';
            }
            CONST CHAR_T *t = resp.text ? resp.text : "";
            while (*t == ' ' || *t == '\n' || *t == '\r' || *t == '\t') t++;
            if (*t != '\0') {
                *out_text = mm_strdup(t);
            } else if (resp.reasoning && resp.reasoning[0] &&
                       __find_template_marker(resp.reasoning) == NULL) {
                /* the answer usually lives here — but reasoning carrying a
                 * tool-call template is an unparsed action, not an answer */
                *out_text = mm_strdup(resp.reasoning);
            } else {
                *out_text = mm_strdup(zh ? WK_REPLY_BLANK_ZH : WK_REPLY_BLANK_EN);
            }
            rt = (*out_text != NULL) ? OPRT_OK : OPRT_MALLOC_FAILED;
            tal_free(err); wukong_llm_resp_free(&resp);
            break;
        }
        /* Thought bubble: prefer the model's reasoning (reasoning models put
         * the plan there and leave content blank/whitespace); fall back to a
         * non-blank resp.text. Sent whole, no truncation. */
        if (resp.reasoning && resp.reasoning[0]) PR_DEBUG("reasoning: %s", resp.reasoning);
        {
            CONST CHAR_T *thought = NULL;
            if (resp.reasoning && resp.reasoning[0]) {
                thought = resp.reasoning;
            } else if (resp.text) {
                CONST CHAR_T *t = resp.text;
                while (*t == ' ' || *t == '\n' || *t == '\r' || *t == '\t') t++;
                if (*t != '\0') thought = resp.text;   /* 纯空白不算话术 */
            }
            if (thought) {
                /* leaked template markup never reaches the channel: cut at the
                 * first marker (UTF-8-safe), drop the bubble if nothing is left */
                CONST CHAR_T *mark = __find_template_marker(thought);
                UINT_T tlen = mark ? (UINT_T)(mark - thought) : (UINT_T)strlen(thought);
                while (tlen > 0 && ((BYTE_T)thought[tlen - 1] & 0xC0) == 0x80) tlen--;
                while (tlen > 0 && (thought[tlen - 1] == ' ' || thought[tlen - 1] == '\n' ||
                                    thought[tlen - 1] == '\r' || thought[tlen - 1] == '\t')) tlen--;
                if (tlen > 0) {
                    CHAR_T *tmsg = tal_malloc(strlen("💭 ") + tlen + 1);
                    if (tmsg) { sprintf(tmsg, "💭 %.*s", (INT_T)tlen, thought); __push_channel_text(tmsg); tal_free(tmsg); }
                }
            }
        }

        /* Has tool_calls: append assistant(tool_calls) + each tool result to
         * runtime (OpenAI wire format), then loop for the next round. */
        if (!runtime) runtime = ty_cJSON_CreateArray();

        /* 1) assistant message carrying the tool_calls the model requested */
        ty_cJSON *am = ty_cJSON_CreateObject();
        ty_cJSON_AddStringToObject(am, "role", "assistant");
        ty_cJSON_AddStringToObject(am, "content", "");
        ty_cJSON *tcarr = ty_cJSON_CreateArray();
        for (UINT32_T i = 0; i < resp.tool_call_count; i++) {
            CONST CHAR_T *tid  = resp.tool_calls[i].id;
            CONST CHAR_T *tnm  = resp.tool_calls[i].name;
            CONST CHAR_T *targ = resp.tool_calls[i].arguments_json;

            if (!tnm || !tnm[0]) continue;   /* nameless jitter: skip everywhere */
            ty_cJSON *tc = ty_cJSON_CreateObject();
            ty_cJSON_AddStringToObject(tc, "id", tid ? tid : "");
            ty_cJSON_AddStringToObject(tc, "type", "function");
            ty_cJSON *fn = ty_cJSON_CreateObject();
            ty_cJSON_AddStringToObject(fn, "name", tnm ? tnm : "");
            ty_cJSON_AddStringToObject(fn, "arguments", targ ? targ : "{}");
            ty_cJSON_AddItemToObject(tc, "function", fn);
            ty_cJSON_AddItemToArray(tcarr, tc);
        }
        ty_cJSON_AddItemToObject(am, "tool_calls", tcarr);
        ty_cJSON_AddItemToArray(runtime, am);

        /* 2) execute each tool call synchronously, append its result */
        for (UINT32_T i = 0; i < resp.tool_call_count; i++) {
            CONST CHAR_T *tid  = resp.tool_calls[i].id;
            CONST CHAR_T *tnm  = resp.tool_calls[i].name;
            CONST CHAR_T *targ = resp.tool_calls[i].arguments_json;

            if (!tnm || !tnm[0]) continue;   /* mirrors the tcarr skip above */
            ty_cJSON *args = targ ? ty_cJSON_Parse(targ) : NULL;
            CHAR_T *tout = NULL;
            /* Agent caller: exec flattens content to text. A non-OK rt with a
             * non-NULL tout is a business error whose message we still surface;
             * only when tout is NULL (rt != OK, no message) do we synthesize a
             * code-named fallback so the LLM always has something to read. */
            OPERATE_RET tr = wukong_tool_exec(WUKONG_TOOL_AGENT, tnm, args, &tout);
            if (args) ty_cJSON_Delete(args);
            if (tout == NULL) {
                CHAR_T ebuf[48];
                (VOID)snprintf(ebuf, sizeof(ebuf), "Error: tool failed (%d)", tr);
                tout = mm_strdup(ebuf);
            }

            PR_NOTICE("agent tool: %s -> %s (%u bytes)", tnm ? tnm : "?",
                      (tr == OPRT_OK && tout) ? "ok" : "fail",
                      tout ? (UINT_T)strlen(tout) : 0);

            /* Action + observation in ONE message: "🎬 行动: tool(args)\n
             * 👀 观察: result" (result truncated at a UTF-8 boundary + "..."),
             * sent right after the tool runs — halves the per-tool IM sends. */
            {
                CONST CHAR_T *obs = tout;   /* always set (real result, business error, or fallback) */
                SIZE_T n = strlen(obs);
                BOOL_T cut = FALSE;
                if (n > WK_OBS_PREVIEW_MAX) {
                    n = WK_OBS_PREVIEW_MAX;
                    while (n > 0 && ((BYTE_T)obs[n] & 0xC0) == 0x80) n--;   /* back off to char boundary */
                    cut = TRUE;
                }
                CHAR_T obuf[WK_OBS_PREVIEW_MAX + 256];
                snprintf(obuf, sizeof(obuf), "%s%s(%s)\n%s%.*s%s",
                         pfx_act, tnm ? tnm : "?", targ ? targ : "",
                         pfx_obs, (INT_T)n, obs, cut ? "..." : "");
                __push_channel_text(obuf);
            }

            CONST CHAR_T *content = tout;   /* surface business-error text to the LLM too */

            ty_cJSON *tm = ty_cJSON_CreateObject();
            ty_cJSON_AddStringToObject(tm, "role", "tool");
            ty_cJSON_AddStringToObject(tm, "tool_call_id", tid ? tid : "");
            ty_cJSON_AddStringToObject(tm, "content", content);
            ty_cJSON_AddItemToArray(runtime, tm);
            tal_free(tout);
        }

        tal_free(err); wukong_llm_resp_free(&resp);
        iter++;
    }
    if (runtime) {
        /* Flush this turn's tool trace into the cross-turn history (after the
         * user turn written by the caller, before the final reply). Later
         * turns then see what was actually called — fixes the model denying
         * its own past tool use. In-loop requests carry the trace via
         * runtime, so history stays clean of it until the turn ends. */
        INT_T n = ty_cJSON_GetArraySize(runtime);
        for (INT_T i = 0; i < n; i++) {
            wukong_session_append_msg(chat_id, ty_cJSON_GetArrayItem(runtime, i));
        }
        ty_cJSON_Delete(runtime);
    }
    return rt;
}

/* Deliver a complete text reply to the active IM channel via the text-stream
 * contract: START records routing + full text, STOP (empty) triggers the flush. */
STATIC VOID_T __notify_reply(CONST CHAR_T *text)
{
    WUKONG_AI_TEXT_T reply = { .data      = (CHAR_T *)text,
                               .datalen   = (UINT16_T)strlen(text),
                               .timeindex = 0 };
    wukong_ai_event_notify(WUKONG_AI_EVENT_TEXT_STREAM_START, &reply);
    WUKONG_AI_TEXT_T eot = { .data = "", .datalen = 0, .timeindex = 0 };
    wukong_ai_event_notify(WUKONG_AI_EVENT_TEXT_STREAM_STOP, &eot);
}

OPERATE_RET wukong_agent_process(CONST CHAR_T *chat_id, CONST CHAR_T *channel,
                                 CONST CHAR_T *text, WUKONG_AI_PROVIDER_T *provider)
{
    (VOID_T)channel;  /* routing uses the active channel */
    CHAR_T *out_text = NULL;
    /* Record the user turn up front so tool-round messages appended inside
     * the loop land after it in order; a failed turn thus keeps its user
     * text + tool trace in history (only the final reply is missing). */
    wukong_session_append(chat_id, "user", text);
    OPERATE_RET rt = wukong_agent_loop_run(chat_id, text, provider, &out_text);
    /* out_text[0]: defense in depth — an empty reply must neither pollute the
     * history with a blank assistant turn nor be "delivered" as silence. */
    if (rt == OPRT_OK && out_text && out_text[0]) {
        wukong_session_append(chat_id, "assistant", out_text);
        __notify_reply(out_text);
        /* End-of-turn compaction: only on a successful turn (the LLM is
         * alive), in the conversation gap — never on the answer path. On
         * failure: silent, the next successful turn retries. */
        if (wukong_session_needs_compact(chat_id)) {
            /* provider->ctx = claw chat runtime */
            wukong_session_compact_run(chat_id, provider->ctx, __text_is_cjk(text),
                                       __push_channel_text);
        }
    } else {
        /* don't leave the user hanging on failure */
        __notify_reply(WK_REPLY_FAILED_ZH);
    }
    tal_free(out_text);
    return rt;
}
