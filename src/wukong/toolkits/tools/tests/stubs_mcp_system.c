/**
 * @file stubs_mcp_system.c
 * @brief Stubs for MCP system tool tests — tool-registry capture + settable
 *        clock / uptime / free-heap sources.
 */

#include "wukong_tool.h"
#include "tal_time_service.h"
#include "tal_system.h"
#include "tal_memory.h"

#include <stdarg.h>
#include <string.h>
#include <time.h>

#include "ty_cJSON.h"

/* --------------------------------------------------------------------------
 * Tool registry capture (name / description / handler)
 * -------------------------------------------------------------------------- */

typedef struct {
    const char *name;
    const char *description;
    WUKONG_TOOL_HANDLER_CB handler;
} REG_TOOL_T;

static REG_TOOL_T g_tools[16];
static int g_tool_count = 0;

OPERATE_RET wukong_tool_register(CONST CHAR_T *name, CONST CHAR_T *description,
                                     WUKONG_TOOL_HANDLER_CB handler, VOID *user_data, UINT_T flags, ...)
{
    (void)user_data;
    (void)flags;
    if (g_tool_count < (int)(sizeof(g_tools) / sizeof(g_tools[0]))) {
        g_tools[g_tool_count].name = name;
        g_tools[g_tool_count].description = description;
        g_tools[g_tool_count].handler = handler;
        g_tool_count++;
    }
    return OPRT_OK;
}

ty_cJSON *wukong_tool_make_text(CONST CHAR_T *text)
{
    return ty_cJSON_CreateString(text);
}

int test_has_tool(const char *name)
{
    int i;
    for (i = 0; i < g_tool_count; i++)
        if (strcmp(g_tools[i].name, name) == 0) return 1;
    return 0;
}

const char *test_tool_description(const char *name)
{
    int i;
    for (i = 0; i < g_tool_count; i++)
        if (strcmp(g_tools[i].name, name) == 0) return g_tools[i].description;
    return NULL;
}

WUKONG_TOOL_HANDLER_CB test_get_tool_handler(const char *name)
{
    int i;
    for (i = 0; i < g_tool_count; i++)
        if (strcmp(g_tools[i].name, name) == 0) return g_tools[i].handler;
    return NULL;
}

/* --------------------------------------------------------------------------
 * Settable system sources
 * -------------------------------------------------------------------------- */

static int      g_synced    = 1;
static TIME_T   g_posix     = 0;
static int      g_tz_sec    = 0;
static uint64_t g_uptime_ms = 0;
static int32_t  g_sram_free = 0;

void test_sys_set_synced(int v)         { g_synced = v; }
void test_sys_set_posix(TIME_T v)       { g_posix = v; }
void test_sys_set_tz_sec(int v)         { g_tz_sec = v; }
void test_sys_set_uptime_ms(uint64_t v) { g_uptime_ms = v; }
void test_sys_set_sram_free(int32_t v)  { g_sram_free = v; }

void test_sys_reset(void)
{
    g_tool_count = 0;
    g_synced = 1;
    g_posix = 0;
    g_tz_sec = 0;
    g_uptime_ms = 0;
    g_sram_free = 0;
}

/* ---- time stubs (also consumed by the real wukong_iso8601.c) ---- */
TIME_T tal_time_mktime(POSIX_TM_S *tm_info)
{
    if (tm_info == NULL) return 0;
    return (TIME_T)timegm(tm_info);
}

TIME_T tal_time_get_posix(VOID)
{
    return g_posix;
}

OPERATE_RET tal_time_check_time_zone_sync(VOID)
{
    return g_synced ? OPRT_OK : OPRT_COM_ERROR;
}

OPERATE_RET tal_time_get_local_time_custom(TIME_T ts, POSIX_TM_S *tm_info)
{
    time_t raw = (time_t)(ts + g_tz_sec);
    return (gmtime_r(&raw, tm_info) == NULL) ? OPRT_INVALID_PARM : OPRT_OK;
}

OPERATE_RET tal_time_get_time_zone_seconds(INT_T *tz_sec)
{
    if (tz_sec == NULL) return OPRT_INVALID_PARM;
    *tz_sec = g_tz_sec;
    return OPRT_OK;
}

/* ---- system stubs ---- */
SYS_TIME_T tal_system_get_millisecond(VOID_T)
{
    return (SYS_TIME_T)g_uptime_ms;
}

int32_t tal_system_get_free_heap_size(void)
{
    return g_sram_free;
}
