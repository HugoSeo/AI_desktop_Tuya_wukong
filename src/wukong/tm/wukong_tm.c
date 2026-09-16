/**
 * @file wukong_tm.c
 * @brief Unified time-management service lifecycle skeleton.
 */

#include <string.h>

#include "base_event.h"
#include "tal_time_service.h"
#include "wukong_tm.h"
#include "wukong_tm_internal.h"
#include "wukong_cron.h"

STATIC BOOL_T s_tm_time_sync_subscribed = FALSE;

STATIC WUKONG_TM_FIRE_CB s_fire_observer = NULL;

STATIC WUKONG_TM_CHANGE_CB s_changed_observer = NULL;

OPERATE_RET wukong_tm_fire_observer_set(WUKONG_TM_FIRE_CB cb)
{
    s_fire_observer = cb;
    return OPRT_OK;
}

OPERATE_RET wukong_tm_changed_observer_set(WUKONG_TM_CHANGE_CB cb)
{
    s_changed_observer = cb;
    return OPRT_OK;
}

VOID_T wukong_tm_changed_notify(WUKONG_TM_TYPE_E type)
{
    if (s_changed_observer == NULL) {
        return;
    }
    s_changed_observer(type);
}

VOID_T wukong_tm_fire_notify(WUKONG_TM_FIRE_EVENT_E event,
                             CONST CHAR_T *id, CONST CHAR_T *message)
{
    WUKONG_TM_FIRE_INFO_T info = {0};

    if (s_fire_observer == NULL) {
        return;
    }
    info.event = event;
    if (id != NULL) {
        strncpy(info.id, id, sizeof(info.id) - 1);
    }
    if (message != NULL) {
        strncpy(info.message, message, sizeof(info.message) - 1);
    }
    s_fire_observer(&info);
}

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

/**
 * @brief Delete all persisted alarm and reminder data from persistent storage.
 *
 * Typically called after wukong_time_manage_deinit() during a device
 * reset so that expired once-alarms and old reminders do not survive
 * across resets and occupy all slots on the next boot.
 *
 * @return OPRT_OK on success, or the last non-OK error encountered.
 */
OPERATE_RET wukong_time_manage_kv_clear(VOID)
{
    OPERATE_RET rt = OPRT_OK;
    OPERATE_RET last_err = OPRT_OK;

    rt = wukong_tm_alarm_kv_clear();
    if (rt != OPRT_OK) {
        last_err = rt;
    }

    rt = wukong_tm_reminder_kv_clear();
    if (rt != OPRT_OK) {
        last_err = rt;
    }

    return last_err;
}
