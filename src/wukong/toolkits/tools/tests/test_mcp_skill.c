/**
 * @file test_mcp_skill.c
 * @brief read_skill tool: registration + body passthrough + not-found error rc.
 */
#include <stdio.h>
#include <string.h>
#include "wukong_test.h"
#include "ty_cJSON.h"
#include "wukong_tool.h"
#include "skill_tool.h"

int test_has_tool(const char *name);
WUKONG_TOOL_HANDLER_CB test_get_tool_handler(const char *name);
void test_skill_set_body(const char *id, const char *body);  /* stub control */

static const char *result_text(ty_cJSON *c)
{
    ty_cJSON *it = c ? ty_cJSON_GetArrayItem(c, 0) : NULL;
    return it ? it->valuestring : NULL;
}

int main(void)
{
    skill_tool_init();
    EXPECT(test_has_tool("read_skill"), "read_skill registered");

    WUKONG_TOOL_HANDLER_CB h = test_get_tool_handler("read_skill");
    EXPECT_NOT_NULL(h, "read_skill handler not NULL");

    /* known skill -> body text */
    if (h) {
        test_skill_set_body("morning_routine", "# Morning Routine\n1. step\n");
        ty_cJSON *args = ty_cJSON_CreateObject();
        ty_cJSON_AddStringToObject(args, "skill_id", "morning_routine");
        ty_cJSON *out = NULL;
        OPERATE_RET rc = h("read_skill", args, &out, NULL);
        EXPECT(rc == OPRT_OK, "known skill not error");
        EXPECT_STR_CONTAINS(result_text(out), "# Morning Routine", "returns body");
        EXPECT(strstr(result_text(out), "skill_content") == NULL, "no wrapper");
        if (out) ty_cJSON_Delete(out);
        ty_cJSON_Delete(args);
    }
    /* unknown skill -> error rc */
    if (h) {
        ty_cJSON *args = ty_cJSON_CreateObject();
        ty_cJSON_AddStringToObject(args, "skill_id", "ghost");
        ty_cJSON *out = NULL;
        OPERATE_RET rc = h("read_skill", args, &out, NULL);
        EXPECT(rc != OPRT_OK, "unknown skill returns error");
        if (out) ty_cJSON_Delete(out);
        ty_cJSON_Delete(args);
    }
    TEST_END();
}
