/**
 * @file test_mcp_profile.c
 * @brief profile_update / profile_reset tools: registration + passthrough +
 *        arg validation + length cap.
 */
#include <stdio.h>
#include <string.h>
#include "wukong_test.h"
#include "ty_cJSON.h"
#include "wukong_tool.h"
#include "profile_tool.h"

int test_has_tool(const char *name);
WUKONG_TOOL_HANDLER_CB test_get_tool_handler(const char *name);

void test_prof_set_update_ret(OPERATE_RET rt);
const char *test_prof_update_target(void);
const char *test_prof_update_content(void);
const char *test_prof_reset_target(void);

static char *content_text(ty_cJSON *content)
{
    /* the stub wukong_tool_make_text returns a bare string node */
    ty_cJSON *item = ty_cJSON_GetArrayItem(content, 0);
    return (item && item->valuestring) ? item->valuestring : NULL;
}

int main(void)
{
    EXPECT_OK(profile_tool_init(), "init ok");
    EXPECT(test_has_tool("profile_update"), "profile_update registered");
    EXPECT(test_has_tool("profile_reset"), "profile_reset registered");

    WUKONG_TOOL_HANDLER_CB h_upd = test_get_tool_handler("profile_update");
    WUKONG_TOOL_HANDLER_CB h_rst = test_get_tool_handler("profile_reset");
    EXPECT_NOT_NULL(h_upd, "update handler");
    EXPECT_NOT_NULL(h_rst, "reset handler");

    /* update passthrough */
    {
        ty_cJSON *args = ty_cJSON_Parse("{\"target\":\"soul\",\"content\":\"NEW SOUL\"}");
        ty_cJSON *out = NULL;
        EXPECT_OK(h_upd("profile_update", args, &out, NULL), "update call ok");
        EXPECT(content_text(out) && strstr(content_text(out), "true"), "update success json");
        EXPECT_STR_EQ(test_prof_update_target(), "soul", "target passed");
        EXPECT_STR_EQ(test_prof_update_content(), "NEW SOUL", "content passed");
        ty_cJSON_Delete(args);
        ty_cJSON_Delete(out);
    }
    /* bad target -> error */
    {
        ty_cJSON *args = ty_cJSON_Parse("{\"target\":\"identity\",\"content\":\"x\"}");
        ty_cJSON *out = NULL;
        OPERATE_RET rc = h_upd("profile_update", args, &out, NULL);
        EXPECT(rc != OPRT_OK, "bad target flagged");
        ty_cJSON_Delete(args);
        ty_cJSON_Delete(out);
    }
    /* over-long content -> rejected before the module */
    {
        char big[3000];
        memset(big, 'a', sizeof(big) - 1);
        big[sizeof(big) - 1] = '\0';
        ty_cJSON *args = ty_cJSON_CreateObject();
        ty_cJSON_AddStringToObject(args, "target", "soul");
        ty_cJSON_AddStringToObject(args, "content", big);
        ty_cJSON *out = NULL;
        OPERATE_RET rc = h_upd("profile_update", args, &out, NULL);
        EXPECT(rc != OPRT_OK, "too-long content flagged");
        EXPECT(content_text(out) && strstr(content_text(out), "too long"), "length reason");
        ty_cJSON_Delete(args);
        ty_cJSON_Delete(out);
    }
    /* reset passthrough */
    {
        ty_cJSON *args = ty_cJSON_Parse("{\"target\":\"user\"}");
        ty_cJSON *out = NULL;
        EXPECT_OK(h_rst("profile_reset", args, &out, NULL), "reset call ok");
        EXPECT_STR_EQ(test_prof_reset_target(), "user", "reset target passed");
        ty_cJSON_Delete(args);
        ty_cJSON_Delete(out);
    }

    TEST_END();
}
