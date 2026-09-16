/**
 * @file stubs_mcp_memory.c
 * @brief Stubs for memory MCP tool tests — tool-registry capture,
 *        wukong_tool_make_text, and controllable wukong_memory_* module.
 */
#include "wukong_tool.h"
#include "wukong_memory.h"
#include "tal_memory.h"
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "ty_cJSON.h"

/* --------------------------------------------------------------------------
 * Tool registry capture (name / handler), mirrors stubs_mcp_skill.c.
 * -------------------------------------------------------------------------- */
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

/* --------------------------------------------------------------------------
 * Controllable wukong_memory_get
 * -------------------------------------------------------------------------- */
static char       g_get_json[512] = {0};
static OPERATE_RET g_get_ret = OPRT_OK;
static char       g_get_ids[16][32];
static int        g_get_n = 0;

void test_mem_set_get_json(const char *json) { strncpy(g_get_json, json, sizeof(g_get_json)-1); }
void test_mem_set_get_ret(OPERATE_RET rt)    { g_get_ret = rt; }
int  test_mem_get_ids_count(void)            { return g_get_n; }
const char *test_mem_get_id(int i)           { return g_get_ids[i]; }

STATIC CONST CHAR_T *s_list_result =
    "{\"success\":true,\"count\":1,\"memories\":[{\"id\":\"mem_0001\","
    "\"tags\":[\"coffee\"]}]}";
OPERATE_RET wukong_memory_list(CHAR_T **out_json)
{
    if (!out_json) {
        return OPRT_INVALID_PARM;
    }
    *out_json = mm_strdup(s_list_result);
    return *out_json ? OPRT_OK : OPRT_MALLOC_FAILED;
}

OPERATE_RET wukong_memory_get(CONST CHAR_T *CONST *ids, UINT_T n, CHAR_T **out_json)
{
    UINT_T i;
    g_get_n = (int)n;
    for (i = 0; i < n && i < 16; i++)
        if (ids[i]) strncpy(g_get_ids[i], ids[i], sizeof(g_get_ids[i])-1);
    if (out_json == NULL) return OPRT_INVALID_PARM;
    *out_json = NULL;
    if (g_get_ret != OPRT_OK) return g_get_ret;
    *out_json = strdup(g_get_json[0] ? g_get_json : "{\"success\":true,\"memories\":[]}");
    return OPRT_OK;
}

/* --------------------------------------------------------------------------
 * Controllable wukong_memory_save
 * -------------------------------------------------------------------------- */
static char       g_save_content[512] = {0};
static char       g_save_tags[3][32];
static int        g_save_ntag = 0;
static int        g_save_importance = 0;
static char       g_save_out_id[32] = "mem_0001";
static OPERATE_RET g_save_ret = OPRT_OK;

void test_mem_set_save_id(const char *id)  { strncpy(g_save_out_id, id, sizeof(g_save_out_id)-1); }
void test_mem_set_save_ret(OPERATE_RET rt) { g_save_ret = rt; }
const char *test_mem_save_content(void)    { return g_save_content; }
int  test_mem_save_ntag(void)               { return g_save_ntag; }
const char *test_mem_save_tag(int i)        { return g_save_tags[i]; }
int  test_mem_save_importance(void)         { return g_save_importance; }

OPERATE_RET wukong_memory_save(CONST CHAR_T *content, CONST CHAR_T *CONST *tags,
                               UINT_T ntag, INT_T importance, CHAR_T **out_id)
{
    UINT_T i;
    if (content) strncpy(g_save_content, content, sizeof(g_save_content)-1);
    g_save_ntag = (int)ntag;
    for (i = 0; i < ntag && i < 3; i++)
        if (tags[i]) strncpy(g_save_tags[i], tags[i], sizeof(g_save_tags[i])-1);
    g_save_importance = importance;
    if (out_id == NULL) return OPRT_INVALID_PARM;
    *out_id = NULL;
    if (g_save_ret != OPRT_OK) return g_save_ret;
    *out_id = strdup(g_save_out_id);
    return OPRT_OK;
}

/* --------------------------------------------------------------------------
 * Controllable wukong_memory_update
 * -------------------------------------------------------------------------- */
static char       g_update_id[32] = {0};
static char       g_update_json[512] = {0};
static OPERATE_RET g_update_ret = OPRT_OK;

void test_mem_set_update_ret(OPERATE_RET rt) { g_update_ret = rt; }
const char *test_mem_update_id(void)         { return g_update_id; }
const char *test_mem_update_json(void)       { return g_update_json; }

OPERATE_RET wukong_memory_update(CONST CHAR_T *id, CONST ty_cJSON *updates)
{
    CHAR_T *s;
    if (id) strncpy(g_update_id, id, sizeof(g_update_id)-1);
    s = ty_cJSON_PrintUnformatted(updates);
    if (s) { strncpy(g_update_json, s, sizeof(g_update_json)-1); ty_cJSON_FreeBuffer(s); }
    return g_update_ret;
}

/* --------------------------------------------------------------------------
 * Controllable wukong_memory_delete
 * -------------------------------------------------------------------------- */
static char       g_delete_id[32] = {0};
static int        g_delete_called = 0;
static OPERATE_RET g_delete_ret = OPRT_OK;

void test_mem_set_delete_ret(OPERATE_RET rt) { g_delete_ret = rt; }
const char *test_mem_delete_id(void)         { return g_delete_id; }
int  test_mem_delete_called(void)            { return g_delete_called; }

OPERATE_RET wukong_memory_delete(CONST CHAR_T *id)
{
    g_delete_called = 1;
    if (id) strncpy(g_delete_id, id, sizeof(g_delete_id)-1);
    return g_delete_ret;
}
