/**
 * @file tool_tm.h
 * @brief Unified MCP tools for time-management features.
 *
 * This header exposes one registration entry for the full time-management MCP
 * surface: alarm, reminder, countdown, stopwatch, and pomodoro. It also keeps
 * alarm direct-exec helpers available for host-side tests.
 */

#ifndef __MCP_TOOL_TM_H__
#define __MCP_TOOL_TM_H__

#include "tuya_cloud_types.h"
#include "ty_cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register all time-management MCP tools with the local MCP server.
 *
 * Registered tools include:
 * - alarms
 * - reminders/schedules
 * - countdown timers
 * - stopwatches
 * - pomodoro timers
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET tool_tm_init(VOID);

/**
 * @brief Execute the alarm set tool logic directly.
 *
 * @param[in]  args         Tool argument object.
 * @param[out] out_content  Tool result content array.
 * @return OPRT_OK on success.
 */
OPERATE_RET tool_alarm_set_exec(CONST ty_cJSON *args, ty_cJSON **out_content);

/**
 * @brief Execute the alarm query tool logic directly.
 *
 * @param[in]  args         Tool argument object.
 * @param[out] out_content  Tool result content array.
 * @return OPRT_OK on success.
 */
OPERATE_RET tool_alarm_query_exec(CONST ty_cJSON *args, ty_cJSON **out_content);

#ifdef __cplusplus
}
#endif

#endif /* __MCP_TOOL_TM_H__ */
