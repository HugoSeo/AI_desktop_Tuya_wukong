/**
 * @file stubs_mcp_control.c
 * @brief Stubs for MCP control tool tests (schema capture + mode state).
 */

#include "wukong_tool.h"
#include "wukong_ai_mode.h"
#include "tuya_ai_toy.h"
#include "tuya_iot_com_api.h"
#include "wukong_audio_player.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "ty_cJSON.h"

/* --------------------------------------------------------------------------
 * Schema capture (same pattern as stubs_mcp_tools.c)
 * -------------------------------------------------------------------------- */

typedef struct {
    const char *name;
    const char *description;
    WUKONG_TOOL_HANDLER_CB handler;
    char prop_names[32][64];
    char prop_types[32][32];
    char prop_descriptions[32][256];
    char prop_enum_values[32][128];
    BOOL_T prop_required[32];
    int prop_count;
} REGISTERED_TOOL_T;

static REGISTERED_TOOL_T g_registered_tools[16];
static int g_registered_tool_count = 0;

OPERATE_RET wukong_tool_register(CONST CHAR_T *name,
                                     CONST CHAR_T *description,
                                     WUKONG_TOOL_HANDLER_CB handler,
                                     VOID *user_data, UINT_T flags, ...)
{
    (void)user_data;
    va_list ap;
    const WUKONG_TOOL_SCHEMA_PROP_T *prop = NULL;

    if (g_registered_tool_count < (int)(sizeof(g_registered_tools) / sizeof(g_registered_tools[0]))) {
        g_registered_tools[g_registered_tool_count].name = name;
        g_registered_tools[g_registered_tool_count].description = description;
        g_registered_tools[g_registered_tool_count].handler = handler;
        g_registered_tools[g_registered_tool_count].prop_count = 0;

        va_start(ap, flags);
        while ((prop = va_arg(ap, const WUKONG_TOOL_SCHEMA_PROP_T *)) != NULL) {
            if (g_registered_tools[g_registered_tool_count].prop_count <
                (int)(sizeof(g_registered_tools[g_registered_tool_count].prop_names) /
                      sizeof(g_registered_tools[g_registered_tool_count].prop_names[0]))) {
                int idx = g_registered_tools[g_registered_tool_count].prop_count++;
                if (prop->name != NULL) {
                    strncpy(g_registered_tools[g_registered_tool_count].prop_names[idx],
                            prop->name,
                            sizeof(g_registered_tools[g_registered_tool_count].prop_names[idx]) - 1);
                }
                if (prop->type != NULL) {
                    strncpy(g_registered_tools[g_registered_tool_count].prop_types[idx],
                            prop->type,
                            sizeof(g_registered_tools[g_registered_tool_count].prop_types[idx]) - 1);
                }
                if (prop->description != NULL) {
                    strncpy(g_registered_tools[g_registered_tool_count].prop_descriptions[idx],
                            prop->description,
                            sizeof(g_registered_tools[g_registered_tool_count].prop_descriptions[idx]) - 1);
                }
                if (prop->enum_values != NULL) {
                    strncpy(g_registered_tools[g_registered_tool_count].prop_enum_values[idx],
                            prop->enum_values,
                            sizeof(g_registered_tools[g_registered_tool_count].prop_enum_values[idx]) - 1);
                }
                g_registered_tools[g_registered_tool_count].prop_required[idx] = prop->required;
            }
        }
        va_end(ap);

        g_registered_tool_count++;
    }
    return OPRT_OK;
}

ty_cJSON *wukong_tool_make_text(CONST CHAR_T *text)
{
    return ty_cJSON_CreateString(text);
}

/* --------------------------------------------------------------------------
 * Mode stubs
 * -------------------------------------------------------------------------- */

static AI_CHAT_SUB_MODE_E g_stub_trigger_mode = AI_CHAT_SUB_FREE;
static AI_CHAT_SUB_MODE_E g_last_switch_mode = AI_CHAT_SUB_HOLD;
static BOOL_T g_switch_called = FALSE;

AI_CHAT_SUB_MODE_E tuya_ai_toy_trigger_mode_get(VOID)
{
    return g_stub_trigger_mode;
}

VOID tuya_ai_toy_trigger_mode_set(AI_CHAT_SUB_MODE_E mode)
{
    if (mode >= AI_CHAT_SUB_HOLD && mode < AI_CHAT_SUB_MAX) {
        g_stub_trigger_mode = mode;
    }
}

OPERATE_RET wukong_ai_chat_sub_mode_switch(AI_CHAT_SUB_MODE_E sub)
{
    if (sub >= AI_CHAT_SUB_MAX) {
        return OPRT_INVALID_PARM;
    }
    g_last_switch_mode = sub;
    g_switch_called = TRUE;
    g_stub_trigger_mode = sub;
    return OPRT_OK;
}

/* --------------------------------------------------------------------------
 * Volume stubs
 * -------------------------------------------------------------------------- */

static UINT8_T g_toy_volume_set_value = 0;
static BOOL_T  g_toy_volume_set_called = FALSE;

OPERATE_RET wukong_audio_player_get_vol(UINT8_T *volume)
{
    if (volume) *volume = 50;
    return OPRT_OK;
}

OPERATE_RET wukong_audio_player_set_vol(INT_T volume)
{
    (VOID)volume;
    return OPRT_OK;
}

OPERATE_RET tuya_ai_toy_volume_set(UINT8_T value)
{
    if (value > 100) {
        return OPRT_INVALID_PARM;
    }
    g_toy_volume_set_value = value;
    g_toy_volume_set_called = TRUE;
    return OPRT_OK;
}

/* --------------------------------------------------------------------------
 * Test helper accessors
 * -------------------------------------------------------------------------- */

int test_has_tool(const char *name)
{
    int i;
    for (i = 0; i < g_registered_tool_count; i++) {
        if (strcmp(g_registered_tools[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

const char *test_tool_description(const char *name)
{
    int i;
    for (i = 0; i < g_registered_tool_count; i++) {
        if (strcmp(g_registered_tools[i].name, name) == 0) {
            return g_registered_tools[i].description;
        }
    }
    return NULL;
}

const char *test_prop_description(const char *tool_name, const char *prop_name)
{
    int i, j;
    for (i = 0; i < g_registered_tool_count; i++) {
        if (strcmp(g_registered_tools[i].name, tool_name) != 0) continue;
        for (j = 0; j < g_registered_tools[i].prop_count; j++) {
            if (strcmp(g_registered_tools[i].prop_names[j], prop_name) == 0) {
                return g_registered_tools[i].prop_descriptions[j];
            }
        }
    }
    return NULL;
}

WUKONG_TOOL_HANDLER_CB test_get_tool_handler(const char *name)
{
    int i;
    for (i = 0; i < g_registered_tool_count; i++) {
        if (strcmp(g_registered_tools[i].name, name) == 0) {
            return g_registered_tools[i].handler;
        }
    }
    return NULL;
}

const char *test_prop_type(const char *tool_name, const char *prop_name)
{
    int i, j;
    for (i = 0; i < g_registered_tool_count; i++) {
        if (strcmp(g_registered_tools[i].name, tool_name) != 0) continue;
        for (j = 0; j < g_registered_tools[i].prop_count; j++) {
            if (strcmp(g_registered_tools[i].prop_names[j], prop_name) == 0) {
                return g_registered_tools[i].prop_types[j];
            }
        }
    }
    return NULL;
}

const char *test_prop_enum_values(const char *tool_name, const char *prop_name)
{
    int i, j;
    for (i = 0; i < g_registered_tool_count; i++) {
        if (strcmp(g_registered_tools[i].name, tool_name) != 0) continue;
        for (j = 0; j < g_registered_tools[i].prop_count; j++) {
            if (strcmp(g_registered_tools[i].prop_names[j], prop_name) == 0) {
                return g_registered_tools[i].prop_enum_values[j];
            }
        }
    }
    return NULL;
}

BOOL_T test_prop_required(const char *tool_name, const char *prop_name)
{
    int i, j;
    for (i = 0; i < g_registered_tool_count; i++) {
        if (strcmp(g_registered_tools[i].name, tool_name) != 0) continue;
        for (j = 0; j < g_registered_tools[i].prop_count; j++) {
            if (strcmp(g_registered_tools[i].prop_names[j], prop_name) == 0) {
                return g_registered_tools[i].prop_required[j];
            }
        }
    }
    return FALSE;
}

void test_control_set_trigger_mode(AI_CHAT_SUB_MODE_E mode)
{
    g_stub_trigger_mode = mode;
}

BOOL_T test_switch_called(void)
{
    return g_switch_called;
}

AI_CHAT_SUB_MODE_E test_last_switch_mode(void)
{
    return g_last_switch_mode;
}

BOOL_T test_toy_volume_set_called(void)
{
    return g_toy_volume_set_called;
}

UINT8_T test_toy_volume_set_value(void)
{
    return g_toy_volume_set_value;
}

void test_control_reset(void)
{
    g_stub_trigger_mode = AI_CHAT_SUB_FREE;
    g_last_switch_mode = AI_CHAT_SUB_HOLD;
    g_switch_called = FALSE;
    g_toy_volume_set_value = 0;
    g_toy_volume_set_called = FALSE;
}
