#include "wukong_test.h"
#include "wukong_agent_loop.h"
#include "wukong_tool.h"
#include "wukong_session.h"
#include "wukong_llm.h"     /* WUKONG_LLM_RT_T for the direct-summarize fakes */
#include "tal_memory.h"
#include <string.h>

/* Fixed-time stub: wukong_session.c stamps a "ts" field on every line. */
TIME_T tal_time_get_posix(VOID) { return 1772829000; }

/* stub for wukong_llm_resp_free (avoid linking claw/wukong_llm.c http deps).
 * Frees text/reasoning plus the tool_calls array (each field + the array
 * itself), matching the real provider/claw/wukong_llm.c ownership contract. */
VOID_T wukong_llm_resp_free(WUKONG_LLM_RESP_T *r)
{
    if (!r) return;
    if (r->text) { tal_free(r->text); r->text = NULL; }
    if (r->reasoning) { tal_free(r->reasoning); r->reasoning = NULL; }
    if (r->tool_calls) {
        for (UINT32_T i = 0; i < r->tool_call_count; i++) {
            tal_free(r->tool_calls[i].id);
            tal_free(r->tool_calls[i].name);
            tal_free(r->tool_calls[i].arguments_json);
        }
        tal_free(r->tool_calls);
        r->tool_calls = NULL;
    }
    r->tool_call_count = 0;
}

/* stub for wukong_tool_exec -- avoid pulling in the real wukong_tool.c ->
 * mcp/mcp_server_tools.c chain. Counts invocations so the multi-round test can
 * assert the tool loop actually ran. */
STATIC UINT32_T s_tool_exec_calls = 0;
OPERATE_RET wukong_tool_exec(UINT_T caller, CONST CHAR_T *name,
                             CONST ty_cJSON *args, CHAR_T **out_text)
{
    (VOID_T)caller;
    (VOID_T)args;
    s_tool_exec_calls++;
    if (out_text) *out_text = mm_strdup(name ? name : "tool-ok");
    return OPRT_OK;
}

STATIC OPERATE_RET fake_infer(VOID_T *h, CONST WUKONG_LLM_REQ_T *req,
                              WUKONG_LLM_RESP_T *resp, CHAR_T **err)
{
    (VOID_T)h; (VOID_T)req; (VOID_T)err;
    resp->text = mm_strdup("hi there");
    resp->tool_call_count = 0;
    return OPRT_OK;
}
STATIC CONST WUKONG_AI_PROVIDER_OPS_T fake_ops = { .name = "fake", .llm_infer = fake_infer };
STATIC WUKONG_AI_PROVIDER_T fake_prov = { .ops = &fake_ops, .ctx = NULL };

/* multi-round fake: 1st call returns a single tool_call, 2nd call returns
 * plain text -> exercises the tool loop (agent/wukong_agent_loop.c). */
STATIC INT_T s_multi_call = 0;
STATIC OPERATE_RET fake_infer_multi(VOID_T *h, CONST WUKONG_LLM_REQ_T *req,
                                    WUKONG_LLM_RESP_T *resp, CHAR_T **err)
{
    (VOID_T)h; (VOID_T)req; (VOID_T)err;
    s_multi_call++;
    if (s_multi_call == 1) {
        resp->tool_call_count = 1;
        resp->tool_calls = tal_malloc(sizeof(resp->tool_calls[0]) * 1);
        resp->tool_calls[0].id = mm_strdup("call_1");
        resp->tool_calls[0].name = mm_strdup("get_weather");
        resp->tool_calls[0].arguments_json = mm_strdup("{\"city\":\"sz\"}");
    } else {
        resp->text = mm_strdup("final answer after tool");
        resp->tool_call_count = 0;
    }
    return OPRT_OK;
}
STATIC CONST WUKONG_AI_PROVIDER_OPS_T multi_ops = { .name = "fake-multi", .llm_infer = fake_infer_multi };
STATIC WUKONG_AI_PROVIDER_T multi_prov = { .ops = &multi_ops, .ctx = NULL };

/* compaction fakes: wukong_compact.c chats on the runtime published in
 * provider->ctx (direct wukong_llm_runtime_chat, not the provider ops) —
 * ctx carries a non-NULL sentinel, the fake runtime_chat returns
 * s_fake_summary. Ordinary turns still go through provider ops. */
STATIC CONST CHAR_T *s_fake_summary = "COMPACTED-HISTORY";
OPERATE_RET wukong_llm_runtime_chat(WUKONG_LLM_RT_T *rt, CONST WUKONG_LLM_REQ_T *req,
                                    WUKONG_LLM_RESP_T *resp, CHAR_T **err)
{
    (VOID_T)rt; (VOID_T)req; (VOID_T)err;
    resp->text = mm_strdup(s_fake_summary);
    resp->tool_call_count = 0;
    return OPRT_OK;
}
STATIC OPERATE_RET fake_infer_compact(VOID_T *h, CONST WUKONG_LLM_REQ_T *req,
                                      WUKONG_LLM_RESP_T *resp, CHAR_T **err)
{
    (VOID_T)h; (VOID_T)req; (VOID_T)err;
    resp->text = mm_strdup("turn reply");
    resp->tool_call_count = 0;
    return OPRT_OK;
}
STATIC CONST WUKONG_AI_PROVIDER_OPS_T compact_ops = { .name = "fake-compact",
                                                      .llm_infer = fake_infer_compact };
STATIC WUKONG_AI_PROVIDER_T compact_prov = { .ops = &compact_ops,
                                             .ctx = (VOID_T *)0x1 };

/* blank-stop fakes: glm sometimes ends the stop round with blank/whitespace
 * content — the loop must fall back (reasoning, then a fixed line), never
 * return an empty answer. */
STATIC OPERATE_RET fake_infer_blank_reasoning(VOID_T *h, CONST WUKONG_LLM_REQ_T *req,
                                              WUKONG_LLM_RESP_T *resp, CHAR_T **err)
{
    (VOID_T)h; (VOID_T)req; (VOID_T)err;
    resp->text = mm_strdup("\n");
    resp->reasoning = mm_strdup("the answer is 42");
    resp->tool_call_count = 0;
    return OPRT_OK;
}
STATIC CONST WUKONG_AI_PROVIDER_OPS_T blankr_ops = { .name = "fake-blank-r",
                                                     .llm_infer = fake_infer_blank_reasoning };
STATIC WUKONG_AI_PROVIDER_T blankr_prov = { .ops = &blankr_ops, .ctx = NULL };

STATIC OPERATE_RET fake_infer_blank_all(VOID_T *h, CONST WUKONG_LLM_REQ_T *req,
                                        WUKONG_LLM_RESP_T *resp, CHAR_T **err)
{
    (VOID_T)h; (VOID_T)req; (VOID_T)err;
    resp->text = NULL;
    resp->reasoning = NULL;
    resp->tool_call_count = 0;
    return OPRT_OK;
}
STATIC CONST WUKONG_AI_PROVIDER_OPS_T blanka_ops = { .name = "fake-blank-a",
                                                     .llm_infer = fake_infer_blank_all };
STATIC WUKONG_AI_PROVIDER_T blanka_prov = { .ops = &blanka_ops, .ctx = NULL };

/* glm-leak fakes: template markup in text / in reasoning / nameless call */
STATIC OPERATE_RET fake_infer_leak_text(VOID_T *h, CONST WUKONG_LLM_REQ_T *req,
                                        WUKONG_LLM_RESP_T *resp, CHAR_T **err)
{
    (VOID_T)h; (VOID_T)req; (VOID_T)err;
    resp->text = mm_strdup("好的，已经记下。</tool_call>garbage");
    resp->tool_call_count = 0;
    return OPRT_OK;
}
STATIC CONST WUKONG_AI_PROVIDER_OPS_T leakt_ops = { .name = "fake-leak-t",
                                                    .llm_infer = fake_infer_leak_text };
STATIC WUKONG_AI_PROVIDER_T leakt_prov = { .ops = &leakt_ops, .ctx = NULL };

STATIC OPERATE_RET fake_infer_leak_reason(VOID_T *h, CONST WUKONG_LLM_REQ_T *req,
                                          WUKONG_LLM_RESP_T *resp, CHAR_T **err)
{
    (VOID_T)h; (VOID_T)req; (VOID_T)err;
    resp->text = mm_strdup("\n");
    resp->reasoning = mm_strdup("device_audio_mode_set<arg_key>mode</arg_key></tool_call>");
    resp->tool_call_count = 0;
    return OPRT_OK;
}
STATIC CONST WUKONG_AI_PROVIDER_OPS_T leakr_ops = { .name = "fake-leak-r",
                                                    .llm_infer = fake_infer_leak_reason };
STATIC WUKONG_AI_PROVIDER_T leakr_prov = { .ops = &leakr_ops, .ctx = NULL };

STATIC OPERATE_RET fake_infer_nameless(VOID_T *h, CONST WUKONG_LLM_REQ_T *req,
                                       WUKONG_LLM_RESP_T *resp, CHAR_T **err)
{
    (VOID_T)h; (VOID_T)req; (VOID_T)err;
    resp->tool_call_count = 1;
    resp->tool_calls = tal_malloc(sizeof(resp->tool_calls[0]) * 1);
    resp->tool_calls[0].id = mm_strdup("call_x");
    resp->tool_calls[0].name = mm_strdup("");        /* nameless jitter */
    resp->tool_calls[0].arguments_json = mm_strdup("{}");
    resp->text = mm_strdup("先这样吧");
    return OPRT_OK;
}
STATIC CONST WUKONG_AI_PROVIDER_OPS_T namel_ops = { .name = "fake-nameless",
                                                    .llm_infer = fake_infer_nameless };
STATIC WUKONG_AI_PROVIDER_T namel_prov = { .ops = &namel_ops, .ctx = NULL };

int main(void)
{
    CHAR_T *out = NULL;
    EXPECT_OK(wukong_agent_loop_run("cli", "hello", &fake_prov, &out), "loop_run ok");
    EXPECT_NOT_NULL(out, "out_text produced");
    EXPECT_STR_EQ(out, "hi there", "out_text is llm reply");
    tal_free(out);

    /* provider without llm_infer -> invalid */
    STATIC CONST WUKONG_AI_PROVIDER_OPS_T noinfer = { .name = "x" };
    STATIC WUKONG_AI_PROVIDER_T bad = { .ops = &noinfer };
    CHAR_T *o2 = NULL;
    EXPECT_ERR(wukong_agent_loop_run("cli", "hi", &bad, &o2), OPRT_INVALID_PARM, "no llm_infer -> invalid");

    /* multi-round: 1 tool_call round, then a text round */
    CHAR_T *o3 = NULL;
    EXPECT_OK(wukong_agent_loop_run("cli", "what's the weather", &multi_prov, &o3), "multi-round loop_run ok");
    EXPECT_NOT_NULL(o3, "multi-round out_text produced");
    EXPECT_STR_EQ(o3, "final answer after tool", "multi-round out_text is 2nd round's text");
    EXPECT(s_tool_exec_calls == 1, "wukong_tools_exec invoked exactly once");
    tal_free(o3);

    /* blank stop content, reasoning present -> reasoning is the answer */
    CHAR_T *o4 = NULL;
    EXPECT_OK(wukong_agent_loop_run("cli", "hello", &blankr_prov, &o4), "blank+reasoning loop_run ok");
    EXPECT_NOT_NULL(o4, "blank+reasoning out_text produced");
    EXPECT_STR_EQ(o4, "the answer is 42", "reasoning used as the answer fallback");
    tal_free(o4);

    /* blank stop content and no reasoning -> fixed fallback, never silence.
     * Input is non-CJK, so the English line is expected. */
    CHAR_T *o5 = NULL;
    EXPECT_OK(wukong_agent_loop_run("cli", "hello", &blanka_prov, &o5), "blank-all loop_run ok");
    EXPECT_NOT_NULL(o5, "blank-all out_text produced");
    EXPECT_STR_CONTAINS(o5, "say that again", "fixed fallback line used");
    tal_free(o5);

    /* end-of-turn compaction: a successful turn past the trigger compacts
     * the session through the claw runtime (direct wukong_llm_runtime_chat) */
    {
        CHAR_T ubuf[32];
        for (int t = 0; t < 45; t++) {            /* 90 msgs on chat "cmp" */
            snprintf(ubuf, sizeof(ubuf), "q%03d", t);
            wukong_session_append("cmp", "user", ubuf);
            wukong_session_append("cmp", "assistant", "ok");
        }
        EXPECT_OK(wukong_agent_process("cmp", "cli", "and one more", &compact_prov),
                  "turn with compaction ok");
        CHAR_T *sum = wukong_session_get_summary("cmp");
        EXPECT_NOT_NULL(sum, "summary produced at end of turn");
        EXPECT_STR_EQ(sum, "COMPACTED-HISTORY", "summary came from the llm fake");
        tal_free(sum);
        UINT_T cnt = 0;
        wukong_session_stat("cmp", &cnt, NULL);
        EXPECT(cnt < 90, "window shrunk after compaction");
    }

    /* B1: leaked template markup in the final text is cut off */
    {
        CHAR_T *ol = NULL;
        EXPECT_OK(wukong_agent_loop_run("cli", "hello", &leakt_prov, &ol), "leak-text run ok");
        EXPECT_STR_EQ(ol, "好的，已经记下。", "text cut at the first template marker");
        tal_free(ol);
    }

    /* B3: reasoning carrying a tool-call template is not an answer */
    {
        CHAR_T *ol = NULL;
        EXPECT_OK(wukong_agent_loop_run("cli", "hello", &leakr_prov, &ol), "leak-reason run ok");
        EXPECT_STR_CONTAINS(ol, "say that again", "template reasoning demoted to the fixed line");
        tal_free(ol);
    }

    /* B5: an all-nameless tool round becomes a final answer, no tool runs */
    {
        UINT32_T before = s_tool_exec_calls;
        CHAR_T *ol = NULL;
        EXPECT_OK(wukong_agent_loop_run("cli", "hello", &namel_prov, &ol), "nameless run ok");
        EXPECT_STR_EQ(ol, "先这样吧", "nameless round treated as the final answer");
        EXPECT(s_tool_exec_calls == before, "no tool executed for a nameless call");
        tal_free(ol);
    }

    /* B2: the summary keeps only what follows a leaked </think> */
    {
        CHAR_T ubuf[32];
        for (int t = 0; t < 45; t++) {
            snprintf(ubuf, sizeof(ubuf), "m%03d", t);
            wukong_session_append("thk", "user", ubuf);
            wukong_session_append("thk", "assistant", "ok");
        }
        s_fake_summary = "draft speak</think>REAL-SUMMARY";
        EXPECT_OK(wukong_agent_process("thk", "cli", "one more", &compact_prov),
                  "compact-think turn ok");
        s_fake_summary = "COMPACTED-HISTORY";
        CHAR_T *sum = wukong_session_get_summary("thk");
        EXPECT_NOT_NULL(sum, "summary produced");
        EXPECT_STR_EQ(sum, "REAL-SUMMARY", "thinking draft stripped from the summary");
        tal_free(sum);
    }

    TEST_END();
}
