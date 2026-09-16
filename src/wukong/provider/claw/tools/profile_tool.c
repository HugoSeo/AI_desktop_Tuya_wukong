/**
 * @file profile_tool.c
 * @brief MCP tools: profile_update / profile_reset — the LLM's write access
 *        to its persona (soul) and the user profile, backed by
 *        wukong_profile (provider/claw/context).
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */
#include "profile_tool.h"
#include "wukong_tool.h"
#include "wukong_profile.h"
#include "tal_memory.h"
#include <string.h>

/* Full profile text is injected into every prompt (Persona / User sections),
 * so cap rewrites to keep the prompt budget bounded. */
#define WK_PROFILE_CONTENT_MAX 2048

/* Wrap one text string as an MCP tool content array. */
STATIC OPERATE_RET __make_text(CONST CHAR_T *text, ty_cJSON **out)
{
    if (out == NULL) return OPRT_INVALID_PARM;
    *out = ty_cJSON_CreateArray();
    if (*out == NULL) return OPRT_MALLOC_FAILED;
    ty_cJSON_AddItemToArray(*out, wukong_tool_make_text(text));
    return OPRT_OK;
}

/* profile_update: args {target:"soul"|"user", content} -> wukong_profile_update. */
STATIC OPERATE_RET __update(CONST CHAR_T *name, CONST ty_cJSON *args,
                            ty_cJSON **out_content,
                            VOID *user_data)
{
    ty_cJSON *tj = args ? ty_cJSON_GetObjectItem(args, "target") : NULL;
    ty_cJSON *cj = args ? ty_cJSON_GetObjectItem(args, "content") : NULL;
    CONST CHAR_T *target = (tj != NULL && ty_cJSON_IsString(tj)) ? tj->valuestring : NULL;
    CONST CHAR_T *content = (cj != NULL && ty_cJSON_IsString(cj)) ? cj->valuestring : NULL;

    (VOID)name;
    (VOID)user_data;

    if (target == NULL || content == NULL || content[0] == '\0') {
        (VOID_T)__make_text("{\"success\":false,\"reason\":\"bad args\"}", out_content);
        return OPRT_COM_ERROR;
    }
    if (strlen(content) > WK_PROFILE_CONTENT_MAX) {
        (VOID_T)__make_text("{\"success\":false,\"reason\":\"content too long\"}", out_content);
        return OPRT_COM_ERROR;
    }
    if (wukong_profile_update(target, content) != OPRT_OK) {
        /* Covers both a bad target and a failed flush (no medium). */
        (VOID_T)__make_text("{\"success\":false,\"reason\":\"not saved\"}", out_content);
        return OPRT_COM_ERROR;
    }
    return __make_text("{\"success\":true}", out_content);
}

/* profile_reset: args {target} -> wukong_profile_reset (built-in default). */
STATIC OPERATE_RET __reset(CONST CHAR_T *name, CONST ty_cJSON *args,
                           ty_cJSON **out_content,
                           VOID *user_data)
{
    ty_cJSON *tj = args ? ty_cJSON_GetObjectItem(args, "target") : NULL;
    CONST CHAR_T *target = (tj != NULL && ty_cJSON_IsString(tj)) ? tj->valuestring : NULL;

    (VOID)name;
    (VOID)user_data;

    if (target == NULL || wukong_profile_reset(target) != OPRT_OK) {
        (VOID_T)__make_text("{\"success\":false}", out_content);
        return OPRT_COM_ERROR;
    }
    return __make_text("{\"success\":true}", out_content);
}

OPERATE_RET profile_tool_init(VOID)
{
    OPERATE_RET rt;

    rt = WUKONG_TOOL_ADD(
        "profile_update",
        "Rewrite the agent persona (target \"soul\") or the user profile "
        "(target \"user\"). The current content is in the system prompt "
        "(Persona / User sections); produce the COMPLETE new file content, "
        "keeping everything that should not change. Only rewrite the persona "
        "when the user explicitly asks for it.",
        __update, NULL, WUKONG_TOOL_AGENT,
        TOOL_SCHEMA_STR("target", "\"soul\" (persona) or \"user\" (user profile)."),
        TOOL_SCHEMA_STR("content", "Complete new file content (markdown, <=2048 chars)."));
    if (rt != OPRT_OK) return rt;

    rt = WUKONG_TOOL_ADD(
        "profile_reset",
        "Restore the agent persona (\"soul\") or the user profile (\"user\") "
        "to the built-in default.",
        __reset, NULL, WUKONG_TOOL_AGENT,
        TOOL_SCHEMA_STR("target", "\"soul\" or \"user\"."));
    return rt;
}
