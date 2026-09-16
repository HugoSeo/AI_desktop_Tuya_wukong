/**
 * @file tool_system.c
 * @brief MCP tools: device system info — current time, uptime, free memory.
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */

#include "tool_system.h"
#include "wukong_tool.h"

#include <stdio.h>
#include "tuya_iot_config.h"    /* ENABLE_EXT_RAM */
#include "tal_log.h"
#include "tal_memory.h"         /* tal_system_get_free_heap_size */
#include "tal_system.h"         /* tal_system_get_millisecond */
#include "tal_time_service.h"   /* tal_time_get_posix / tal_time_check_time_zone_sync */
#include "wukong_iso8601.h"     /* wukong_iso8601_format (timezone-aware) */
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
#include "tkl_memory.h"         /* tkl_system_psram_get_free_heap_size */
#endif

/* Wrap one text string as an MCP tool content array. */
STATIC OPERATE_RET __make_text_result(CONST CHAR_T *text, ty_cJSON **out_content)
{
    if (out_content == NULL) {
        return OPRT_INVALID_PARM;
    }
    *out_content = ty_cJSON_CreateArray();
    if (*out_content == NULL) {
        return OPRT_MALLOC_FAILED;
    }
    ty_cJSON_AddItemToArray(*out_content, wukong_tool_make_text(text));
    return OPRT_OK;
}

/* device_time_get: current local date-time as ISO 8601 with timezone offset. */
STATIC OPERATE_RET __get_time(CONST CHAR_T *name, CONST ty_cJSON *args,
                              ty_cJSON **out_content,
                              VOID *user_data)
{
    CHAR_T buf[40] = {0};

    (VOID)name;
    (VOID)args;
    (VOID)user_data;

    /* The clock is meaningless until it has been synced from the network;
     * report that explicitly instead of returning a bogus 1970 timestamp. */
    if (tal_time_check_time_zone_sync() != OPRT_OK) {
        return __make_text_result(
            "{\"synced\":false,\"reason\":\"device time not synced yet\"}", out_content);
    }
    if (wukong_iso8601_format(tal_time_get_posix(), buf, sizeof(buf)) != OPRT_OK) {
        return __make_text_result(
            "{\"synced\":false,\"reason\":\"time format error\"}", out_content);
    }
    return __make_text_result(buf, out_content);
}

/* device_uptime_get: time since last boot, seconds + human-readable form. */
STATIC OPERATE_RET __get_uptime(CONST CHAR_T *name, CONST ty_cJSON *args,
                                ty_cJSON **out_content,
                                VOID *user_data)
{
    CHAR_T buf[96] = {0};
    UINT_T sec, d, h, m, s;

    (VOID)name;
    (VOID)args;
    (VOID)user_data;

    sec = (UINT_T)(tal_system_get_millisecond() / 1000u);
    d = sec / 86400u;
    h = (sec % 86400u) / 3600u;
    m = (sec % 3600u) / 60u;
    s = sec % 60u;
    (VOID)snprintf(buf, sizeof(buf),
                   "{\"uptime_sec\":%u,\"human\":\"%ud %uh %um %us\"}", sec, d, h, m, s);
    return __make_text_result(buf, out_content);
}

/* device_memory_get: current free heap in bytes. SRAM always; PSRAM only when
 * external RAM is present on this board. */
STATIC OPERATE_RET __get_memory(CONST CHAR_T *name, CONST ty_cJSON *args,
                                ty_cJSON **out_content,
                                VOID *user_data)
{
    CHAR_T buf[96] = {0};
    INT_T sram_free = (INT_T)tal_system_get_free_heap_size();

    (VOID)name;
    (VOID)args;
    (VOID)user_data;

#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    /* tal_system_psram_get_free_heap_size() is declared but not implemented in
     * this SDK revision (link error) — call the TKL layer directly, matching
     * ui_svc_sysmon.c. */
    {
        INT_T psram_free = (INT_T)tkl_system_psram_get_free_heap_size();
        (VOID)snprintf(buf, sizeof(buf),
                       "{\"sram_free\":%d,\"psram_free\":%d}", sram_free, psram_free);
    }
#else
    (VOID)snprintf(buf, sizeof(buf), "{\"sram_free\":%d}", sram_free);
#endif
    return __make_text_result(buf, out_content);
}

OPERATE_RET tool_system_init(VOID)
{
    OPERATE_RET rt;

    rt = WUKONG_TOOL_ADD(
        "device_time_get",
        "Get the current device date and time as an ISO 8601 string with timezone "
        "offset, e.g. 2026-08-03T14:05:00+08:00. Use this whenever you need to know "
        "the current time or date.",
        __get_time, NULL, WUKONG_TOOL_MCP | WUKONG_TOOL_AGENT);
    if (rt != OPRT_OK)
        return rt;

    rt = WUKONG_TOOL_ADD(
        "device_uptime_get",
        "Get how long the device has been running since its last boot. Returns "
        "uptime_sec (seconds) and a human-readable form.",
        __get_uptime, NULL, WUKONG_TOOL_MCP | WUKONG_TOOL_AGENT);
    if (rt != OPRT_OK)
        return rt;

    rt = WUKONG_TOOL_ADD(
        "device_memory_get",
        "Get the device's current free memory in bytes: sram_free always, and "
        "psram_free when external RAM is present.",
        __get_memory, NULL, WUKONG_TOOL_MCP | WUKONG_TOOL_AGENT);
    return rt;
}
