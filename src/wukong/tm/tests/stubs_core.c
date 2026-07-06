#include <stddef.h>

#include "wukong_tm.h"
#include "wukong_cron.h"
#include "tal_time_service.h"
#include "base_event.h"

static int g_subscribe_calls = 0;
static int g_unsubscribe_calls = 0;
static int g_time_ready_notify_calls = 0;
static int g_time_sync_ready = 0;
static SUBSCRIBE_CALLBACK g_time_sync_cb = NULL;

OPERATE_RET ty_subscribe_event(INT_T event_id, CONST CHAR_T *name,
                               SUBSCRIBE_CALLBACK cb, INT_T type)
{
    (VOID)name; (VOID)type;
    if (event_id == EVENT_TIME_SYNC) {
        g_subscribe_calls++;
        g_time_sync_cb = cb;
    }
    return OPRT_OK;
}

OPERATE_RET ty_unsubscribe_event(INT_T event_id, CONST CHAR_T *name,
                                 SUBSCRIBE_CALLBACK cb)
{
    (VOID)name;
    if (event_id == EVENT_TIME_SYNC && cb == g_time_sync_cb) {
        g_unsubscribe_calls++;
        g_time_sync_cb = NULL;
    }
    return OPRT_OK;
}

OPERATE_RET tal_time_check_time_sync(VOID)
{
    return g_time_sync_ready ? OPRT_OK : -1;
}

OPERATE_RET tal_time_check_time_zone_sync(VOID)
{
    return g_time_sync_ready ? OPRT_OK : -1;
}

OPERATE_RET wukong_cron_time_ready_notify(VOID)
{
    g_time_ready_notify_calls++;
    return OPRT_OK;
}

OPERATE_RET wukong_tm_alarm_init(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_alarm_deinit(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_alarm_add(CONST WUKONG_TM_ALARM_CFG_T *a, CONST CHAR_T *id)
{ (VOID)a; (VOID)id; return OPRT_OK; }
OPERATE_RET wukong_tm_alarm_update(CONST CHAR_T *id, CONST WUKONG_TM_ALARM_CFG_T *a)
{ (VOID)id; (VOID)a; return OPRT_OK; }
OPERATE_RET wukong_tm_alarm_remove(CONST CHAR_T *id) { (VOID)id; return OPRT_OK; }
OPERATE_RET wukong_tm_alarm_list(CHAR_T **j) { (VOID)j; return OPRT_OK; }
OPERATE_RET wukong_tm_alarm_find_by_time(CONST WUKONG_TM_ALARM_CFG_T *a,
    CHAR_T *id, UINT_T len) { (VOID)a; (VOID)id; (VOID)len; return OPRT_OK; }
OPERATE_RET wukong_tm_alarm_fire(CONST CHAR_T *id) { (VOID)id; return OPRT_OK; }
OPERATE_RET wukong_tm_reminder_init(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_reminder_deinit(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_countdown_init(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_countdown_deinit(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_stopwatch_init(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_stopwatch_deinit(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_pomodoro_init(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_pomodoro_deinit(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_reminder_add(CONST WUKONG_TM_REMINDER_CFG_T *c,
    CONST CHAR_T *id) { (VOID)c; (VOID)id; return OPRT_OK; }
OPERATE_RET wukong_tm_reminder_remove(CONST CHAR_T *id) { (VOID)id; return OPRT_OK; }
OPERATE_RET wukong_tm_reminder_fire(CONST CHAR_T *id) { (VOID)id; return OPRT_OK; }
OPERATE_RET wukong_tm_reminder_action_notify(CONST CHAR_T *m) { (VOID)m; return OPRT_OK; }

int stub_get_subscribe_calls(void) { return g_subscribe_calls; }
int stub_get_unsubscribe_calls(void) { return g_unsubscribe_calls; }
int stub_get_time_ready_notify_calls(void) { return g_time_ready_notify_calls; }
void stub_set_time_sync_ready(int ready) { g_time_sync_ready = ready; }
int stub_fire_time_sync_event(void)
{
    if (g_time_sync_cb == NULL) return -1;
    return g_time_sync_cb(NULL);
}
