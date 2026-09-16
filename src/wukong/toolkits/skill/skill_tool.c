/**
 * @file skill_tool.c
 * @brief AGENT tool: read_skill — load a skill's full instructions by id.
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */
#include "skill_tool.h"
#include "wukong_tool.h"
#include "wukong_skill.h"
#include "tal_memory.h"

STATIC OPERATE_RET __make_text(CONST CHAR_T *text, ty_cJSON **out)
{
    if (out == NULL) return OPRT_INVALID_PARM;
    *out = ty_cJSON_CreateArray();
    if (*out == NULL) return OPRT_MALLOC_FAILED;
    ty_cJSON_AddItemToArray(*out, wukong_tool_make_text(text));
    return OPRT_OK;
}

STATIC OPERATE_RET __read_skill(CONST CHAR_T *name, CONST ty_cJSON *args,
                                ty_cJSON **out_content,
                                VOID *user_data)
{
    CONST CHAR_T *id = NULL;
    CHAR_T *body = NULL;
    ty_cJSON *idj;

    (VOID)name;
    (VOID)user_data;

    idj = args ? ty_cJSON_GetObjectItem(args, "skill_id") : NULL;
    if (idj != NULL && ty_cJSON_IsString(idj)) {
        id = idj->valuestring;
    }
    if (id == NULL || wukong_skill_read(id, &body) != OPRT_OK || body == NULL) {
        (VOID_T)__make_text("[skill not found]", out_content);
        return OPRT_COM_ERROR;
    }
    {
        OPERATE_RET rt = __make_text(body, out_content);   /* body only, no wrapper */
        tal_free(body);
        return rt;
    }
}

OPERATE_RET skill_tool_init(VOID)
{
    return WUKONG_TOOL_ADD(
        "read_skill",
        "Load the full instructions of a skill listed under 'Available skills'. "
        "Pass the skill's id (the text before the colon). Returns the skill's "
        "markdown instructions to follow.",
        __read_skill, NULL, WUKONG_TOOL_AGENT,
        TOOL_SCHEMA_STR("skill_id", "The skill id from the Available skills list."));
}
