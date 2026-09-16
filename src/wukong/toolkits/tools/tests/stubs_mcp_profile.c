/* Stubs for profile MCP tool tests — tool-registry capture,
 * wukong_tool_make_text, and controllable wukong_profile_* module. */
#include "wukong_tool.h"
#include "wukong_profile.h"
#include <stdarg.h>
#include <string.h>
#include "ty_cJSON.h"

/* Tool registry capture (mirrors stubs_mcp_memory.c). */
typedef struct { const char *name; WUKONG_TOOL_HANDLER_CB handler; } REG_T;
static REG_T g_tools[8];
static int g_n = 0;

OPERATE_RET wukong_tool_register(CONST CHAR_T *name, CONST CHAR_T *description,
                                     WUKONG_TOOL_HANDLER_CB handler, VOID *ud, UINT_T flags, ...)
{
    (void)description; (void)ud; (void)flags;
    if (g_n < 8) { g_tools[g_n].name = name; g_tools[g_n].handler = handler; g_n++; }
    return OPRT_OK;
}
ty_cJSON *wukong_tool_make_text(CONST CHAR_T *text) { return ty_cJSON_CreateString(text); }
int test_has_tool(const char *n){int i;for(i=0;i<g_n;i++)if(!strcmp(g_tools[i].name,n))return 1;return 0;}
WUKONG_TOOL_HANDLER_CB test_get_tool_handler(const char *n){int i;for(i=0;i<g_n;i++)if(!strcmp(g_tools[i].name,n))return g_tools[i].handler;return NULL;}


static char s_upd_target[16];
static char s_upd_content[64];
static OPERATE_RET s_upd_ret = OPRT_OK;
static char s_rst_target[16];

void test_prof_set_update_ret(OPERATE_RET rt) { s_upd_ret = rt; }
const char *test_prof_update_target(void) { return s_upd_target; }
const char *test_prof_update_content(void) { return s_upd_content; }
const char *test_prof_reset_target(void) { return s_rst_target; }

OPERATE_RET wukong_profile_update(CONST CHAR_T *target, CONST CHAR_T *content)
{
    if (!target || !content) {
        return OPRT_INVALID_PARM;
    }
    if (strcmp(target, "soul") != 0 && strcmp(target, "user") != 0) {
        return OPRT_INVALID_PARM;
    }
    strncpy(s_upd_target, target, sizeof(s_upd_target) - 1);
    strncpy(s_upd_content, content, sizeof(s_upd_content) - 1);
    return s_upd_ret;
}

OPERATE_RET wukong_profile_reset(CONST CHAR_T *target)
{
    if (!target) {
        return OPRT_INVALID_PARM;
    }
    if (strcmp(target, "soul") != 0 && strcmp(target, "user") != 0) {
        return OPRT_INVALID_PARM;
    }
    strncpy(s_rst_target, target, sizeof(s_rst_target) - 1);
    return OPRT_OK;
}
