/* Directly include the unit under test so we can exercise static helpers. */
#include "wukong_test.h"
#include "wukong_llm.h"
#include <stdlib.h>

/* Stubbed transport injects this canned OpenAI response body. */
extern void __test_set_http_response(const char *body, int code);
/* Stubbed transport captures the last POST body sent by build_body. */
extern const char *__test_get_last_body(void);

int main(void)
{
    /* --- build_body: system + user 都进 messages, model 正确 --- */
    ty_cJSON *msgs = ty_cJSON_CreateArray();
    ty_cJSON *u = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(u, "role", "user");
    ty_cJSON_AddStringToObject(u, "content", "hello");
    ty_cJSON_AddItemToArray(msgs, u);

    WUKONG_AI_PROVIDER_CFG_T cfg = {
        .backend = WUKONG_LLM_BACKEND_OPENAI,
        .base_url = "https://api.openai.com/v1",
        .api_key = "sk-test", .model = "gpt-4o",
    };
    WUKONG_LLM_RT_T *rt = NULL;
    EXPECT_OK(wukong_llm_runtime_create(&cfg, &rt), "runtime create");
    EXPECT_NOT_NULL(rt, "runtime not null");

    /* --- chat: stub 返回固定响应, 验证解析出 text --- */
    __test_set_http_response(
        "{\"choices\":[{\"message\":{\"role\":\"assistant\","
        "\"content\":\"hi there\"}}]}", 200);

    WUKONG_LLM_REQ_T req = { .system_prompt = "You are helpful.",
                             .messages = msgs, .tools_json = NULL };
    WUKONG_LLM_RESP_T resp = {0};
    CHAR_T *err = NULL;
    EXPECT_OK(wukong_llm_runtime_chat(rt, &req, &resp, &err), "chat ok");
    EXPECT_STR_EQ(resp.text, "hi there", "parsed content");
    EXPECT_EQ(resp.tool_call_count, 0, "no tool calls in M1");

    /* --- build_body: 校验发出的请求体内容与顺序 (system 在前, user 深拷贝在后) --- */
    const char *body = __test_get_last_body();
    EXPECT_STR_CONTAINS(body, "\"model\":\"gpt-4o\"", "body has model");
    EXPECT_STR_CONTAINS(body, "\"role\":\"system\"", "body has system role");
    EXPECT_STR_CONTAINS(body, "You are helpful.", "body has system prompt");
    EXPECT_STR_CONTAINS(body, "\"role\":\"user\"", "body has user role");
    EXPECT_STR_CONTAINS(body, "hello", "body has user content");
    EXPECT(strstr(body, "system") < strstr(body, "hello"), "system before user");
    EXPECT_STR_CONTAINS(body, "\"tools\"", "body has tools (from wukong_tools_build_schema)");

    /* --- chat: 响应带 tool_calls → 解析进 resp.tool_calls --- */
    __test_set_http_response(
        "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":null,"
        "\"tool_calls\":[{\"id\":\"call_1\",\"type\":\"function\",\"function\":"
        "{\"name\":\"play_music\",\"arguments\":\"{\\\"song\\\":\\\"foo\\\"}\"}}]}}]}",
        200);
    WUKONG_LLM_RESP_T resp_tc = {0};
    CHAR_T *err_tc = NULL;
    EXPECT_OK(wukong_llm_runtime_chat(rt, &req, &resp_tc, &err_tc), "chat ok (tool_calls)");
    EXPECT_EQ(resp_tc.tool_call_count, 1, "tool_call_count == 1");
    EXPECT_STR_EQ(resp_tc.tool_calls[0].name, "play_music", "tool_calls[0].name");
    EXPECT_STR_CONTAINS(resp_tc.tool_calls[0].arguments_json, "song", "arguments_json has song");
    wukong_llm_resp_free(&resp_tc);

    /* --- 非 200 → 错误 --- */
    __test_set_http_response("{\"error\":\"bad key\"}", 401);
    WUKONG_LLM_RESP_T resp2 = {0};
    CHAR_T *err2 = NULL;
    EXPECT_ERR(wukong_llm_runtime_chat(rt, &req, &resp2, &err2),
               OPRT_COM_ERROR, "http 401 → error");

    wukong_llm_resp_free(&resp);
    wukong_llm_runtime_destroy(rt);
    ty_cJSON_Delete(msgs);
    TEST_END();
}
