/*
 * Test-specific stubs for the iso8601 unit test.
 * Provides tal_time_service implementations and sibling-module stubs.
 */
#include "wukong_iso8601.h"
#include "wukong_tm.h"
#include "tal_time_service.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* ---- configurable timezone offset (default +08:00 = 28800) ---- */

INT_T g_stub_tz_offset_sec = 28800;

VOID stub_set_tz_offset(INT_T tz_sec)
{
    g_stub_tz_offset_sec = tz_sec;
}

/* ---- tal_time_service stubs ---- */

OPERATE_RET tal_time_get_time_zone_seconds(OUT INT_T *time_zone)
{
    if (time_zone == NULL) {
        return OPRT_INVALID_PARM;
    }
    *time_zone = g_stub_tz_offset_sec;
    return OPRT_OK;
}

TIME_T tal_time_get_posix(VOID)
{
    return 1749738600; /* 2025-06-12T14:30:00 UTC */
}

TIME_T tal_time_mktime(POSIX_TM_S *tm_info)
{
    if (tm_info == NULL) {
        return 0;
    }
    return (TIME_T)timegm(tm_info);
}

OPERATE_RET tal_time_get_local_time_custom(TIME_T posix_time,
                                           POSIX_TM_S *local_tm)
{
    time_t raw_time = (time_t)posix_time;
    if (local_tm == NULL) {
        return OPRT_INVALID_PARM;
    }
    return (gmtime_r(&raw_time, local_tm) == NULL)
               ? OPRT_INVALID_PARM : OPRT_OK;
}

OPERATE_RET tal_time_check_time_sync(VOID) { return OPRT_OK; }
OPERATE_RET tal_time_check_time_zone_sync(VOID) { return OPRT_OK; }

/* ---- sibling-module stubs (not under test) ---- */

OPERATE_RET wukong_tm_alarm_init(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_alarm_deinit(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_alarm_add(CONST WUKONG_TM_ALARM_CFG_T *a, CONST CHAR_T *id)
{ (VOID)a; (VOID)id; return OPRT_OK; }
OPERATE_RET wukong_tm_alarm_update(CONST CHAR_T *id, CONST WUKONG_TM_ALARM_CFG_T *a)
{ (VOID)id; (VOID)a; return OPRT_OK; }
OPERATE_RET wukong_tm_alarm_remove(CONST CHAR_T *id) { (VOID)id; return OPRT_OK; }
OPERATE_RET wukong_tm_alarm_list(CHAR_T **j) { (VOID)j; return OPRT_OK; }
OPERATE_RET wukong_tm_alarm_fire(CONST CHAR_T *id) { (VOID)id; return OPRT_OK; }
OPERATE_RET wukong_tm_reminder_init(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_reminder_deinit(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_countdown_init(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_countdown_deinit(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_stopwatch_init(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_stopwatch_deinit(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_pomodoro_init(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_pomodoro_deinit(VOID) { return OPRT_OK; }
