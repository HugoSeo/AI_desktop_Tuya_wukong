#include "wukong_test.h"
#include "wukong_context.h"
#include "wukong_session.h"
#include "wukong_llm.h"     /* WUKONG_LLM_RT_T for the compact link stubs */
#include "tal_memory.h"

/* Fixed-time stub: wukong_session.c stamps a "ts" field on every line. */
TIME_T tal_time_get_posix(VOID) { return 1772829000; }

/* Stub for wukong_skill_build_summary (skill module, Task 2); avoids linking
 * the real wukong_skill.c here. Return value controlled per test case. */
static char *g_skill_sum = NULL;   /* set by cases below */
CHAR_T *wukong_skill_build_summary(VOID_T) { return g_skill_sum ? mm_strdup(g_skill_sum) : NULL; }

/* Stub for wukong_memory_build_index (memory module); avoids linking it here. */
static char *g_mem_idx = NULL;     /* set by cases below */
CHAR_T *wukong_memory_build_index(VOID_T) { return g_mem_idx ? mm_strdup(g_mem_idx) : NULL; }

/* Stubs for the llm symbols wukong_compact.c references (linked for
 * wukong_session_summary_for_prompt); its summarize path never runs here. */
OPERATE_RET wukong_llm_runtime_chat(WUKONG_LLM_RT_T *rt, CONST WUKONG_LLM_REQ_T *req,
                                    WUKONG_LLM_RESP_T *resp, CHAR_T **err)
{
    (VOID_T)rt; (VOID_T)req; (VOID_T)resp; (VOID_T)err;
    return OPRT_COM_ERROR;
}
VOID_T wukong_llm_resp_free(WUKONG_LLM_RESP_T *resp) { (VOID_T)resp; }

int main(void)
{
    WUKONG_LLM_REQ_T req = {0};
    /* The current user turn reaches the request via session history (the
     * caller appends it before building). */
    wukong_session_clear("cli");
    wukong_session_append("cli", "user", "hello");
    EXPECT_OK(wukong_context_build("cli", "hello", NULL, &req), "build ok");

    EXPECT_NOT_NULL(req.system_prompt, "has system_prompt");
    EXPECT_STR_CONTAINS(req.system_prompt, "Persona", "has persona section");
    EXPECT_STR_CONTAINS(req.system_prompt, "Wukong", "soul content");
    EXPECT_STR_CONTAINS(req.system_prompt, "主人", "default address in persona");
    EXPECT_STR_CONTAINS(req.system_prompt, "tools", "behavior rule in persona");
    EXPECT_STR_CONTAINS(req.system_prompt, "User", "has user section");
    EXPECT_STR_CONTAINS(req.system_prompt, "Device", "has device section");
    EXPECT_STR_CONTAINS(req.system_prompt, "T5", "device platform from macro");
    EXPECT_STR_CONTAINS(req.system_prompt, "touchscreen", "device peripherals");

    EXPECT_NOT_NULL(req.messages, "has messages");
    EXPECT_EQ(ty_cJSON_GetArraySize(req.messages), 1, "one user message (via history)");
    ty_cJSON *m0 = ty_cJSON_GetArrayItem(req.messages, 0);
    ty_cJSON *role = ty_cJSON_GetObjectItem(m0, "role");
    ty_cJSON *content = ty_cJSON_GetObjectItem(m0, "content");
    EXPECT(role && role->valuestring && strcmp(role->valuestring, "user") == 0, "role=user");
    EXPECT(content && content->valuestring && strcmp(content->valuestring, "hello") == 0, "content=hello");

    EXPECT_NULL(req.tools_json, "no tools in M2a");
    wukong_context_free(&req);
    EXPECT_NULL(req.system_prompt, "freed system_prompt nulled");
    EXPECT_NULL(req.messages, "freed messages nulled");

    /* history injection: prior turns land before the current user message */
    wukong_session_clear("hist");
    wukong_session_append("hist", "user", "q1");
    wukong_session_append("hist", "assistant", "a1");
    wukong_session_append("hist", "user", "q2");
    WUKONG_LLM_REQ_T r2 = {0};
    EXPECT_OK(wukong_context_build("hist", "q2", NULL, &r2), "build with history");
    EXPECT_EQ(ty_cJSON_GetArraySize(r2.messages), 3, "history(2) + current(1)");
    ty_cJSON *h0 = ty_cJSON_GetArrayItem(r2.messages, 0);
    ty_cJSON *h2 = ty_cJSON_GetArrayItem(r2.messages, 2);
    EXPECT_STR_EQ(ty_cJSON_GetObjectItem(h0, "content")->valuestring, "q1", "history first");
    EXPECT_STR_EQ(ty_cJSON_GetObjectItem(h2, "content")->valuestring, "q2", "current last");
    wukong_context_free(&r2);

    /* skills present -> system prompt gets an "Available skills" section */
    g_skill_sum = "- morning_routine: Morning briefing across tools";
    {
        WUKONG_LLM_REQ_T req3 = {0};
        EXPECT_OK(wukong_context_build("c1", "hi", NULL, &req3), "build ok w/ skills");
        EXPECT_STR_CONTAINS(req3.system_prompt, "## Available skills", "skills section present");
        EXPECT_STR_CONTAINS(req3.system_prompt, "morning_routine", "skill line present");
        wukong_context_free(&req3);
    }
    /* no skills -> section skipped */
    g_skill_sum = NULL;
    {
        WUKONG_LLM_REQ_T req4 = {0};
        EXPECT_OK(wukong_context_build("c1", "hi", NULL, &req4), "build ok w/o skills");
        EXPECT(strstr(req4.system_prompt, "Available skills") == NULL, "no skills section when empty");
        wukong_context_free(&req4);
    }

    /* memory index present -> "## Memory" section; empty -> skipped */
    g_mem_idx = "咖啡: mem_0001, mem_0003";
    {
        WUKONG_LLM_REQ_T req5 = {0};
        EXPECT_OK(wukong_context_build("c1", "hi", NULL, &req5), "build ok w/ memory");
        EXPECT_STR_CONTAINS(req5.system_prompt, "## Memory", "memory section present");
        EXPECT_STR_CONTAINS(req5.system_prompt, "mem_0001", "memory index line present");
        wukong_context_free(&req5);
    }
    g_mem_idx = NULL;
    {
        WUKONG_LLM_REQ_T req6 = {0};
        EXPECT_OK(wukong_context_build("c1", "hi", NULL, &req6), "build ok w/o memory");
        EXPECT(strstr(req6.system_prompt, "## Memory") == NULL, "no memory section when empty");
        wukong_context_free(&req6);
    }

    /* conversation summary section: present iff the session has a summary */
    {
        WUKONG_LLM_REQ_T r3 = {0};
        EXPECT_OK(wukong_context_build("nosum", "hi", NULL, &r3), "build wo summary");
        EXPECT_NULL(strstr(r3.system_prompt, "Conversation summary"),
                    "no summary -> no section");
        wukong_context_free(&r3);

        /* seed a summary through the real primitives (covered=1 is legal:
         * apply trusts the boundary its caller derived) */
        wukong_session_append("sum", "user", "old question");
        wukong_session_append("sum", "user", "new question");
        EXPECT_OK(wukong_session_apply_summary("sum", 1, "USER-LIKES-TEA"),
                  "seed summary");
        WUKONG_LLM_REQ_T r4 = {0};
        EXPECT_OK(wukong_context_build("sum", "hi", NULL, &r4), "build w summary");
        EXPECT_NOT_NULL(strstr(r4.system_prompt, "## Conversation summary"),
                        "summary section present");
        EXPECT_NOT_NULL(strstr(r4.system_prompt, "USER-LIKES-TEA"),
                        "summary text injected");
        EXPECT_NOT_NULL(strstr(r4.system_prompt, "never recite"),
                        "usage note precedes summary");
        wukong_context_free(&r4);
    }

    TEST_END();
}
