#include "ui_svc_tm.h"

#include <string.h>             /* memset/strncpy */

#include "wukong_tm.h"          /* business header — allowed only in services */
#include "wukong_audio_player.h"/* dingdong ring tone — business header, services-only */
#include "ui_state.h"
#include "ui_app.h"             /* ui_app_async_call — marshal to UI thread */
#include "ty_cJSON.h"
#include "tal_memory.h"         /* tal_malloc/tal_free */
#include "uni_log.h"            /* PR_* log macros */

static ui_svc_tm_fire_cb_t s_fire_cb = NULL;
static ui_svc_tm_clock_tab_t s_clock_tab_request = UI_SVC_TM_CLOCK_TAB_NONE;

typedef struct {
    ui_svc_tm_fire_event_t event;
    char id[WUKONG_TM_ALARM_ID_LEN + 1];
    char message[WUKONG_TM_ALARM_MESSAGE_LEN + 1];
} fire_async_ctx_t;

static void fire_on_ui_thread(void *p)
{
    fire_async_ctx_t *ctx = (fire_async_ctx_t *)p;
    if (s_fire_cb) {
        s_fire_cb(ctx->event, ctx->id, ctx->message);
    }
    ui_svc_tm_refresh_counts();   /* counts may have changed (once-alarm consumed) */
    tal_free(ctx);
}

static VOID_T fire_observer(CONST WUKONG_TM_FIRE_INFO_T *info)
{
    fire_async_ctx_t *ctx;

    /* Default ring tone: play built-in dingdong on every ringing kind, including
       each pomodoro phase end (sound only; the phase switch stays in-page with
       no global overlay -- see on_tm_fire in ui_app.c). NOT_ACTIVE falls through
       to media_src_dingdong_zh in wukong_audio_player_alert(). Runs on the
       business/cron thread; audio play is async-enqueued and must NOT go on the
       UI thread. */
    if (info->event == WUKONG_TM_FIRE_ALARM ||
        info->event == WUKONG_TM_FIRE_REMINDER ||
        info->event == WUKONG_TM_FIRE_COUNTDOWN_DONE ||
        info->event == WUKONG_TM_FIRE_POMODORO_PHASE) {
        wukong_audio_player_alert(AI_TOY_ALERT_TYPE_NOT_ACTIVE, FALSE);
    }

    ctx = (fire_async_ctx_t *)tal_malloc(sizeof(*ctx));
    if (ctx == NULL) {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->event = (ui_svc_tm_fire_event_t)info->event;
    strncpy(ctx->id, info->id, sizeof(ctx->id) - 1);
    strncpy(ctx->message, info->message, sizeof(ctx->message) - 1);
    ui_app_async_call(fire_on_ui_thread, ctx);   /* marshal to UI thread */
}

static void refresh_counts_on_ui_thread(void *p)
{
    (void)p;
    ui_svc_tm_refresh_counts();
}

static VOID_T list_changed_observer(WUKONG_TM_TYPE_E type)
{
    (void)type;
    /* Runs on the tm business thread inside the store-save path; marshal the
       count recompute to the UI thread to avoid re-entering wukong_tm. */
    ui_app_async_call(refresh_counts_on_ui_thread, NULL);
}

void ui_svc_tm_init(void)
{
    wukong_tm_fire_observer_set(fire_observer);
    wukong_tm_changed_observer_set(list_changed_observer);
    ui_svc_tm_refresh_counts();
}

void ui_svc_tm_set_fire_cb(ui_svc_tm_fire_cb_t cb) { s_fire_cb = cb; }

void ui_svc_tm_clock_tab_request(ui_svc_tm_clock_tab_t tab)
{
    if (tab >= UI_SVC_TM_CLOCK_TAB_ALARM &&
        tab <= UI_SVC_TM_CLOCK_TAB_POMODORO) {
        s_clock_tab_request = tab;
    }
}

ui_svc_tm_clock_tab_t ui_svc_tm_clock_tab_take(void)
{
    ui_svc_tm_clock_tab_t tab = s_clock_tab_request;
    s_clock_tab_request = UI_SVC_TM_CLOCK_TAB_NONE;
    return tab;
}

int ui_svc_tm_alarm_list(char **json)    { return (int)wukong_tm_alarm_list(json); }
int ui_svc_tm_reminder_list(char **json) { return (int)wukong_tm_reminder_list(json); }
void ui_svc_tm_json_free(char *json)     { if (json) ty_cJSON_FreeBuffer(json); }

int ui_svc_tm_countdown_query(ui_svc_tm_countdown_t *snap)
{
    WUKONG_TM_COUNTDOWN_SNAPSHOT_T s = {0};
    OPERATE_RET rt = wukong_tm_countdown_query(&s);
    if (rt != OPRT_OK) { snap->active = false; return (int)rt; }
    snap->active = s.active;
    snap->paused = (s.state == WUKONG_TM_COUNTDOWN_STATE_PAUSED);
    snap->state = (int)s.state;
    snap->remaining_sec = (long)s.remaining_sec;
    snap->duration_sec = (long)s.duration_sec;
    return 0;
}

int ui_svc_tm_stopwatch_query(ui_svc_tm_stopwatch_t *snap)
{
    WUKONG_TM_STOPWATCH_STATE_T s = {0};
    OPERATE_RET rt = wukong_tm_stopwatch_query(&s);
    if (rt != OPRT_OK) { snap->active = false; return (int)rt; }
    snap->active = s.active; snap->paused = s.paused;
    snap->elapsed_ms = (uint64_t)s.elapsed_ms;
    snap->elapsed_sec = (long)s.elapsed_sec;
    return 0;
}

int ui_svc_tm_pomodoro_query(ui_svc_tm_pomodoro_t *snap)
{
    WUKONG_TM_POMODORO_STATE_T s = {0};
    OPERATE_RET rt = wukong_tm_pomodoro_query(&s);
    if (rt != OPRT_OK) { snap->active = false; return (int)rt; }
    snap->active = s.active; snap->paused = s.paused;
    snap->phase = (int)s.phase; snap->cycle = s.current_cycle;
    snap->completed = s.completed_work_count;
    snap->remaining_sec = (long)s.remaining_sec;
    snap->cfg.work_duration = s.cfg.work_duration;
    snap->cfg.short_break_duration = s.cfg.short_break_duration;
    snap->cfg.long_break_duration = s.cfg.long_break_duration;
    snap->cfg.work_sessions_before_long_break = s.cfg.work_sessions_before_long_break;
    return 0;
}

void ui_svc_tm_alarm_enable_set(const char *id, bool on) { wukong_tm_alarm_enable_set(id, on); ui_svc_tm_refresh_counts(); }
void ui_svc_tm_alarm_remove(const char *id) { wukong_tm_alarm_remove(id); ui_svc_tm_refresh_counts(); }
void ui_svc_tm_alarm_ack(const char *id)    { wukong_tm_alarm_ack(id); }
void ui_svc_tm_reminder_remove(const char *id) { wukong_tm_reminder_remove(id); ui_svc_tm_refresh_counts(); }
void ui_svc_tm_countdown_create(int hours, int minutes, int seconds) { wukong_tm_countdown_create(hours, minutes, seconds); }
void ui_svc_tm_countdown_pause(void)  { wukong_tm_countdown_pause(); }
void ui_svc_tm_countdown_resume(void) { wukong_tm_countdown_resume(); }
void ui_svc_tm_countdown_delete(void) { wukong_tm_countdown_delete(); }
void ui_svc_tm_stopwatch_start(void)  { wukong_tm_stopwatch_start(); }
void ui_svc_tm_stopwatch_pause(void)  { wukong_tm_stopwatch_pause(); }
void ui_svc_tm_stopwatch_resume(void) { wukong_tm_stopwatch_resume(); }
void ui_svc_tm_stopwatch_reset(void)  { wukong_tm_stopwatch_reset(); }
void ui_svc_tm_pomodoro_pause(void)   { wukong_tm_pomodoro_pause(); }
void ui_svc_tm_pomodoro_resume(void)  { wukong_tm_pomodoro_resume(); }
void ui_svc_tm_pomodoro_stop(void)    { wukong_tm_pomodoro_stop(); }

void ui_svc_tm_pomodoro_start(const ui_svc_tm_pomodoro_cfg_t *cfg)
{
    WUKONG_TM_POMODORO_CFG_T c;
    c.work_duration = cfg->work_duration;
    c.short_break_duration = cfg->short_break_duration;
    c.long_break_duration = cfg->long_break_duration;
    c.work_sessions_before_long_break = cfg->work_sessions_before_long_break;
    wukong_tm_pomodoro_start(&c);
}

void ui_svc_tm_refresh_counts(void)
{
    char *json = NULL;
    uint8_t alarms = 0, reminders = 0;

    if (wukong_tm_alarm_list(&json) == OPRT_OK && json) {
        ty_cJSON *root = ty_cJSON_Parse(json);
        ty_cJSON *arr = root ? ty_cJSON_GetObjectItem(root, "alarms") : NULL;
        if (arr) {
            int n = ty_cJSON_GetArraySize(arr), i;
            for (i = 0; i < n; i++) {
                ty_cJSON *it = ty_cJSON_GetArrayItem(arr, i);
                ty_cJSON *en = it ? ty_cJSON_GetObjectItem(it, "enabled") : NULL;
                if (en && en->valueint) alarms++;
            }
        }
        ty_cJSON_Delete(root);
        ty_cJSON_FreeBuffer(json);
        json = NULL;
    }
    if (wukong_tm_reminder_list(&json) == OPRT_OK && json) {
        ty_cJSON *root = ty_cJSON_Parse(json);
        ty_cJSON *arr = root ? ty_cJSON_GetObjectItem(root, "reminders") : NULL;
        if (arr) reminders = (uint8_t)ty_cJSON_GetArraySize(arr);
        ty_cJSON_Delete(root);
        ty_cJSON_FreeBuffer(json);
    }
    ui_state_set_alarm_count(alarms);
    ui_state_set_calendar_count(reminders);
}
