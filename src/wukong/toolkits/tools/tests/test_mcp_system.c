/**
 * @file test_mcp_system.c
 * @brief MCP system tool tests — time / uptime / memory handlers + registration.
 * Run via pytest test_suite.py -k mcp_system.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "wukong_test.h"
#include "ty_cJSON.h"
#include "wukong_tool.h"
#include "tool_system.h"

/* Accessors + setters provided by stubs_mcp_system.c */
int test_has_tool(const char *name);
WUKONG_TOOL_HANDLER_CB test_get_tool_handler(const char *name);
void test_sys_set_synced(int v);
void test_sys_set_posix(TIME_T v);
void test_sys_set_tz_sec(int v);
void test_sys_set_uptime_ms(uint64_t v);
void test_sys_set_sram_free(int32_t v);
void test_sys_reset(void);

/** Call a no-arg tool handler; returns out_content array. */
static ty_cJSON *call_no_args(WUKONG_TOOL_HANDLER_CB handler)
{
    ty_cJSON *out_content = NULL;
    handler("test", NULL, &out_content, NULL);
    return out_content;
}

/** Extract text from first content item of tool result. */
static const char *result_text(ty_cJSON *content)
{
    ty_cJSON *item;
    if (!content) return NULL;
    item = ty_cJSON_GetArrayItem(content, 0);
    if (!item) return NULL;
    return item->valuestring;
}

int main(void)
{
    WUKONG_TOOL_HANDLER_CB h = NULL;
    ty_cJSON *r = NULL;
    const char *text = NULL;

    test_sys_reset();
    tool_system_init();

    /* ==== 1. Tool registration ======================================== */
    EXPECT(test_has_tool("device_time_get"),   "device_time_get is registered");
    EXPECT(test_has_tool("device_uptime_get"), "device_uptime_get is registered");
    EXPECT(test_has_tool("device_memory_get"), "device_memory_get is registered");

    /* ==== 2. time: synced -> ISO 8601 with timezone offset ============ */
    h = test_get_tool_handler("device_time_get");
    EXPECT_NOT_NULL(h, "time handler is not NULL");
    if (h) {
        test_sys_set_synced(1);
        test_sys_set_tz_sec(8 * 3600);   /* +08:00 */
        test_sys_set_posix(1000);        /* +tz -> 1970-01-01T08:16:40 */
        r = call_no_args(h);
        text = result_text(r);
        EXPECT_NOT_NULL(text, "time text is not NULL");
        EXPECT_STR_CONTAINS(text, "+08:00", "time carries +08:00 offset");
        EXPECT_STR_CONTAINS(text, "1970-01-01", "time date is rendered");
        if (r) ty_cJSON_Delete(r);
    }

    /* ==== 3. time: not synced -> synced:false ========================= */
    if (h) {
        test_sys_set_synced(0);
        r = call_no_args(h);
        text = result_text(r);
        EXPECT_STR_CONTAINS(text, "\"synced\":false",
                            "unsynced clock reports synced:false");
        if (r) ty_cJSON_Delete(r);
        test_sys_set_synced(1);
    }

    /* ==== 4. uptime: ms -> uptime_sec + human form ==================== */
    h = test_get_tool_handler("device_uptime_get");
    EXPECT_NOT_NULL(h, "uptime handler is not NULL");
    if (h) {
        test_sys_set_uptime_ms((uint64_t)90061000ull);  /* 1d 1h 1m 1s */
        r = call_no_args(h);
        text = result_text(r);
        EXPECT_STR_CONTAINS(text, "\"uptime_sec\":90061", "uptime_sec computed");
        EXPECT_STR_CONTAINS(text, "1d 1h 1m 1s", "uptime human form");
        if (r) ty_cJSON_Delete(r);
    }

    /* ==== 5. memory: sram_free present, no psram on host ============== */
    h = test_get_tool_handler("device_memory_get");
    EXPECT_NOT_NULL(h, "memory handler is not NULL");
    if (h) {
        test_sys_set_sram_free(123456);
        r = call_no_args(h);
        text = result_text(r);
        EXPECT_STR_CONTAINS(text, "\"sram_free\":123456", "sram_free reported");
        EXPECT(text && strstr(text, "psram_free") == NULL,
               "no psram_free without ENABLE_EXT_RAM");
        if (r) ty_cJSON_Delete(r);
    }

    TEST_END();
}
