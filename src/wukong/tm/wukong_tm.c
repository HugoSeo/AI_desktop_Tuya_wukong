/**
 * @file wukong_tm.c
 * @brief Unified time-management service lifecycle skeleton.
 */

#include "base_event.h"
#include "tal_time_service.h"
#include "wukong_tm.h"
#include "wukong_cron.h"

STATIC BOOL_T s_tm_time_sync_subscribed = FALSE;

/**
 * @brief Notify cron when both cloud time and timezone are ready.
 *
 * @param[in] data Unused event payload.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __tm_notify_time_ready(VOID_T *data)
{
    (VOID)data;
    if (tal_time_check_time_sync() != OPRT_OK ||
        tal_time_check_time_zone_sync() != OPRT_OK) {
        return OPRT_OK;
    }

    return wukong_cron_time_ready_notify();
}

/**
 * @brief Initialize the unified time-management service.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_time_manage_init(VOID)
{
    OPERATE_RET rt = OPRT_OK;

    rt = wukong_tm_alarm_init();
    if (rt != OPRT_OK) {
        return rt;
    }

    rt = wukong_tm_reminder_init();
    if (rt != OPRT_OK) {
        (VOID)wukong_tm_alarm_deinit();
        return rt;
    }

    rt = wukong_tm_countdown_init();
    if (rt != OPRT_OK) {
        (VOID)wukong_tm_reminder_deinit();
        (VOID)wukong_tm_alarm_deinit();
        return rt;
    }

    rt = wukong_tm_stopwatch_init();
    if (rt != OPRT_OK) {
        (VOID)wukong_tm_countdown_deinit();
        (VOID)wukong_tm_reminder_deinit();
        (VOID)wukong_tm_alarm_deinit();
        return rt;
    }

    rt = wukong_tm_pomodoro_init();
    if (rt != OPRT_OK) {
        (VOID)wukong_tm_stopwatch_deinit();
        (VOID)wukong_tm_countdown_deinit();
        (VOID)wukong_tm_reminder_deinit();
        (VOID)wukong_tm_alarm_deinit();
        return rt;
    }

    rt = ty_subscribe_event(EVENT_TIME_SYNC, "wukong_tm", __tm_notify_time_ready, SUBSCRIBE_TYPE_NORMAL);
    if (rt != OPRT_OK) {
        (VOID)wukong_tm_pomodoro_deinit();
        (VOID)wukong_tm_stopwatch_deinit();
        (VOID)wukong_tm_countdown_deinit();
        (VOID)wukong_tm_reminder_deinit();
        (VOID)wukong_tm_alarm_deinit();
        return rt;
    }
    s_tm_time_sync_subscribed = TRUE;

    rt = __tm_notify_time_ready(0);
    if (rt != OPRT_OK) {
        (VOID)ty_unsubscribe_event(EVENT_TIME_SYNC, "wukong_tm", __tm_notify_time_ready);
        s_tm_time_sync_subscribed = FALSE;
        (VOID)wukong_tm_pomodoro_deinit();
        (VOID)wukong_tm_stopwatch_deinit();
        (VOID)wukong_tm_countdown_deinit();
        (VOID)wukong_tm_reminder_deinit();
        (VOID)wukong_tm_alarm_deinit();
        return rt;
    }

    return OPRT_OK;
}

/**
 * @brief Deinitialize the unified time-management service.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_time_manage_deinit(VOID)
{
    OPERATE_RET rt = OPRT_OK;
    OPERATE_RET last_err = OPRT_OK;

    if (s_tm_time_sync_subscribed) {
        rt = ty_unsubscribe_event(EVENT_TIME_SYNC, "wukong_tm", __tm_notify_time_ready);
        if (rt != OPRT_OK) {
            last_err = rt;
        }
        s_tm_time_sync_subscribed = FALSE;
    }

    rt = wukong_tm_pomodoro_deinit();
    if (rt != OPRT_OK) {
        last_err = rt;
    }

    rt = wukong_tm_stopwatch_deinit();
    if (rt != OPRT_OK) {
        last_err = rt;
    }

    rt = wukong_tm_countdown_deinit();
    if (rt != OPRT_OK) {
        last_err = rt;
    }

    rt = wukong_tm_reminder_deinit();
    if (rt != OPRT_OK) {
        last_err = rt;
    }

    rt = wukong_tm_alarm_deinit();
    if (rt != OPRT_OK) {
        last_err = rt;
    }

    return last_err;
}
