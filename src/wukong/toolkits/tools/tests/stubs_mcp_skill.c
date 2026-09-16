#include "wukong_tool.h"
#include "wukong_skill.h"
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "ty_cJSON.h"

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

/* controllable wukong_skill_read */
static char g_id[64] = {0};
static char g_body[256] = {0};
void test_skill_set_body(const char *id, const char *body)
{ strncpy(g_id, id, sizeof(g_id)-1); strncpy(g_body, body, sizeof(g_body)-1); }

OPERATE_RET wukong_skill_read(CONST CHAR_T *id, CHAR_T **out_text)
{
    if (out_text == NULL) return OPRT_INVALID_PARM;
    *out_text = NULL;
    if (id == NULL || strcmp(id, g_id) != 0) return OPRT_NOT_FOUND;
    *out_text = strdup(g_body);
    return OPRT_OK;
}
