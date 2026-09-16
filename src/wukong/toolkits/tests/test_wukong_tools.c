/* Base-level tests for wukong_tool.c: exec shaping per caller, flags-gated
 * visibility, JSON-encoded-string argument promotion, and the business-error
 * (rc != OK + text) contract. Fake tools are registered into the REAL registry
 * (this suite links wukong_tool.c), then exercised through wukong_tool_exec. */
#include "wukong_test.h"
#include "wukong_tool.h"
#include "tal_memory.h"
#include <stdlib.h>
#include <string.h>

/* agent-only echo: content = [{"type":"text","text":<msg or "none">}] */
STATIC OPERATE_RET h_echo(CONST CHAR_T *name, CONST ty_cJSON *args,
                          ty_cJSON **out, VOID *ud)
{
    (VOID)name; (VOID)ud;
    ty_cJSON *m = args ? ty_cJSON_GetObjectItem(args, "msg") : NULL;
    CONST CHAR_T *txt = (m && ty_cJSON_IsString(m)) ? m->valuestring : "none";
    *out = ty_cJSON_CreateArray();
    ty_cJSON_AddItemToArray(*out, wukong_tool_make_text(txt));
    return OPRT_OK;
}

/* mcp-only: fixed content item, used to check MCP serialization path */
STATIC OPERATE_RET h_served(CONST CHAR_T *name, CONST ty_cJSON *args,
                            ty_cJSON **out, VOID *ud)
{
    (VOID)name; (VOID)args; (VOID)ud;
    *out = ty_cJSON_CreateArray();
    ty_cJSON_AddItemToArray(*out, wukong_tool_make_text("served"));
    return OPRT_OK;
}

/* promotion probe: "data" is declared non-string, so a JSON-encoded string
 * value must be promoted to a real array before the handler sees it. */
STATIC OPERATE_RET h_probe(CONST CHAR_T *name, CONST ty_cJSON *args,
                           ty_cJSON **out, VOID *ud)
{
    (VOID)name; (VOID)ud;
    ty_cJSON *d = args ? ty_cJSON_GetObjectItem(args, "data") : NULL;
    *out = ty_cJSON_CreateArray();
    ty_cJSON_AddItemToArray(*out, wukong_tool_make_text(ty_cJSON_IsArray(d) ? "array" : "other"));
    return OPRT_OK;
}

/* business error: populates content AND returns a non-OK code */
STATIC OPERATE_RET h_fail(CONST CHAR_T *name, CONST ty_cJSON *args,
                          ty_cJSON **out, VOID *ud)
{
    (VOID)name; (VOID)args; (VOID)ud;
    *out = ty_cJSON_CreateArray();
    ty_cJSON_AddItemToArray(*out, wukong_tool_make_text("boom"));
    return OPRT_COM_ERROR;
}

/* no-schema probe: registered with zero schema properties (like fc's
 * music/story/PlayControl/cloud_event). Neither "songId" nor "note" is
 * declared anywhere, so both must reach the handler as untouched strings —
 * "12345" must NOT become a number and "true" must NOT become a bool. */
STATIC OPERATE_RET h_noschema(CONST CHAR_T *name, CONST ty_cJSON *args,
                              ty_cJSON **out, VOID *ud)
{
    (VOID)name; (VOID)ud;
    ty_cJSON *song_id = args ? ty_cJSON_GetObjectItem(args, "songId") : NULL;
    ty_cJSON *note = args ? ty_cJSON_GetObjectItem(args, "note") : NULL;
    BOOL_T ok = song_id && ty_cJSON_IsString(song_id) && song_id->valuestring != NULL &&
                strcmp(song_id->valuestring, "12345") == 0 &&
                note && ty_cJSON_IsString(note) && note->valuestring != NULL &&
                strcmp(note->valuestring, "true") == 0;
    *out = ty_cJSON_CreateArray();
    ty_cJSON_AddItemToArray(*out, wukong_tool_make_text(ok ? "untouched" : "retyped"));
    return OPRT_OK;
}

int main(void)
{
    EXPECT_OK(WUKONG_TOOL_ADD("agent_echo", "d", h_echo, NULL, WUKONG_TOOL_AGENT,
                              TOOL_SCHEMA_STR_OPT("msg", "text")), "register agent_echo");
    EXPECT_OK(WUKONG_TOOL_ADD("mcp_served", "d", h_served, NULL, WUKONG_TOOL_MCP),
              "register mcp_served");
    EXPECT_OK(WUKONG_TOOL_ADD("promote", "d", h_probe, NULL, WUKONG_TOOL_AGENT,
                              TOOL_SCHEMA_ARRAY_OPT("data", "n")), "register promote");
    EXPECT_OK(WUKONG_TOOL_ADD("boom", "d", h_fail, NULL, WUKONG_TOOL_AGENT), "register boom");
    EXPECT_OK(WUKONG_TOOL_ADD("noschema", "d", h_noschema, NULL, WUKONG_TOOL_AGENT),
              "register noschema");

    /* --- exec(AGENT): content flattened to plain text --- */
    ty_cJSON *args = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(args, "msg", "hi there");
    CHAR_T *out = NULL;
    EXPECT_OK(wukong_tool_exec(WUKONG_TOOL_AGENT, "agent_echo", args, &out), "agent exec ok");
    EXPECT_STR_EQ(out, "hi there", "agent caller gets flattened text");
    tal_free(out); ty_cJSON_Delete(args);

    /* --- exec(MCP): content serialized as a JSON array --- */
    out = NULL;
    EXPECT_OK(wukong_tool_exec(WUKONG_TOOL_MCP, "mcp_served", NULL, &out), "mcp exec ok");
    EXPECT_NOT_NULL(out, "mcp caller gets serialized content");
    EXPECT_STR_CONTAINS(out, "\"type\":\"text\"", "serialized as content array");
    EXPECT_STR_CONTAINS(out, "served", "serialized text present");
    tal_free(out);

    /* --- flags visibility: agent tool is invisible to the MCP caller --- */
    out = (CHAR_T *)0x1;
    EXPECT_ERR(wukong_tool_exec(WUKONG_TOOL_MCP, "agent_echo", NULL, &out),
               OPRT_NOT_FOUND, "cross-caller tool is NOT_FOUND");
    EXPECT_NULL(out, "out_text cleared on not-found");

    /* --- unknown tool --- */
    EXPECT_ERR(wukong_tool_exec(WUKONG_TOOL_AGENT, "ghost", NULL, &out),
               OPRT_NOT_FOUND, "unknown tool is NOT_FOUND");

    /* --- type promotion: JSON-encoded string -> real array --- */
    ty_cJSON *pa = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(pa, "data", "[1,2,3]");
    out = NULL;
    EXPECT_OK(wukong_tool_exec(WUKONG_TOOL_AGENT, "promote", pa, &out), "promote exec ok");
    EXPECT_STR_EQ(out, "array", "string arg promoted to array for non-string prop");
    tal_free(out); ty_cJSON_Delete(pa);

    /* --- no schema declared at all: JSON-literal-looking strings must stay
     * strings (whitelist, not opt-out) --- */
    ty_cJSON *na = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(na, "songId", "12345");
    ty_cJSON_AddStringToObject(na, "note", "true");
    out = NULL;
    EXPECT_OK(wukong_tool_exec(WUKONG_TOOL_AGENT, "noschema", na, &out), "noschema exec ok");
    EXPECT_STR_EQ(out, "untouched", "undeclared props are never promoted, even if JSON-literal-looking");
    tal_free(out); ty_cJSON_Delete(na);

    /* --- declared-type mismatch is a business error, not a silent pass-through:
     * "data" declares array; a comma-string survives promotion (not valid
     * JSON) and must be rejected with a self-correction message (the glm
     * "tags":"a,b,c" failure mode) --- */
    ty_cJSON *tm = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(tm, "data", "a,b,c");
    out = NULL;
    EXPECT_ERR(wukong_tool_exec(WUKONG_TOOL_AGENT, "promote", tm, &out),
               OPRT_INVALID_PARM, "type mismatch rejected");
    EXPECT_NOT_NULL(out, "type mismatch carries a message");
    EXPECT_STR_CONTAINS(out, "Invalid argument 'data'", "message names the argument");
    EXPECT_STR_CONTAINS(out, "must be a JSON array", "message names the expected type");
    tal_free(out); ty_cJSON_Delete(tm);

    /* parsed-but-wrong-type is also rejected: "7" promotes to a number,
     * which still mismatches the declared array */
    tm = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(tm, "data", "7");
    out = NULL;
    EXPECT_ERR(wukong_tool_exec(WUKONG_TOOL_AGENT, "promote", tm, &out),
               OPRT_INVALID_PARM, "promoted-to-wrong-type rejected");
    tal_free(out); ty_cJSON_Delete(tm);

    /* --- business error: rc != OK with a populated out_text --- */
    out = NULL;
    OPERATE_RET rt = wukong_tool_exec(WUKONG_TOOL_AGENT, "boom", NULL, &out);
    EXPECT(rt != OPRT_OK, "business error returns non-OK");
    EXPECT_STR_EQ(out, "boom", "business error still yields readable text");
    tal_free(out);

    /* --- list is filtered by caller --- */
    ty_cJSON *ml = wukong_tool_list(WUKONG_TOOL_MCP);
    EXPECT_EQ(ty_cJSON_GetArraySize(ml), 1, "only the MCP tool is listed for MCP caller");
    ty_cJSON_Delete(ml);

    /* --- OpenAI schema envelope built from the agent snapshot --- */
    CHAR_T *sc = wukong_tool_build_schema(WUKONG_LLM_BACKEND_OPENAI);
    EXPECT_NOT_NULL(sc, "schema built");
    EXPECT_STR_CONTAINS(sc, "\"type\":\"function\"", "openai function envelope");
    EXPECT_STR_CONTAINS(sc, "agent_echo", "agent tool present in schema");
    tal_free(sc);
    EXPECT_NULL(wukong_tool_build_schema(WUKONG_LLM_BACKEND_ANTHROPIC), "anthropic not built");

    mcp_tools_cap_destroy();
    TEST_END();
}
