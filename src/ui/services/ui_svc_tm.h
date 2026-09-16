#ifndef __UI_SVC_TM_H__
#define __UI_SVC_TM_H__

#include <stdint.h>
#include <stdbool.h>

/* Mirror of WUKONG_TM_FIRE_EVENT_E, kept LVGL/business-header-free for pages. */
typedef enum {
    UI_SVC_TM_FIRE_ALARM = 0,
    UI_SVC_TM_FIRE_REMINDER,
    UI_SVC_TM_FIRE_COUNTDOWN_DONE,
    UI_SVC_TM_FIRE_POMODORO_PHASE,
} ui_svc_tm_fire_event_t;

/* One-shot Clock-page launch target, used by cross-page shortcuts without
 * introducing a page-to-page dependency. Values intentionally mirror the
 * Clock tab order, but the page maps them explicitly. */
typedef enum {
    UI_SVC_TM_CLOCK_TAB_NONE = -1,
    UI_SVC_TM_CLOCK_TAB_ALARM = 0,
    UI_SVC_TM_CLOCK_TAB_COUNTDOWN,
    UI_SVC_TM_CLOCK_TAB_STOPWATCH,
    UI_SVC_TM_CLOCK_TAB_POMODORO,
} ui_svc_tm_clock_tab_t;

typedef struct { bool active; bool paused; int state; long remaining_sec; long duration_sec; } ui_svc_tm_countdown_t;
typedef struct {
    bool active;
    bool paused;
    uint64_t elapsed_ms;
    long elapsed_sec;  /* compatibility view: elapsed_ms / 1000 */
} ui_svc_tm_stopwatch_t;
typedef struct {
    int work_duration; int short_break_duration; int long_break_duration;
    int work_sessions_before_long_break;
} ui_svc_tm_pomodoro_cfg_t;
typedef struct {
    bool active; bool paused; int phase; unsigned cycle; unsigned completed;
    long remaining_sec;
    ui_svc_tm_pomodoro_cfg_t cfg;   /* effective cfg of the running session (MCP/MQTT may differ from defaults) */
} ui_svc_tm_pomodoro_t;

/* event delivered already marshalled to the UI thread */
typedef void (*ui_svc_tm_fire_cb_t)(ui_svc_tm_fire_event_t event,
                                    const char *id, const char *message);

void ui_svc_tm_init(void);                 /* register observer + initial count sync */
void ui_svc_tm_set_fire_cb(ui_svc_tm_fire_cb_t cb);  /* single slot; NULL clears */

/* snapshots (caller-owned json freed via ui_svc_tm_json_free) */
int  ui_svc_tm_alarm_list(char **json);        /* OPRT_OK==0 */
int  ui_svc_tm_reminder_list(char **json);
void ui_svc_tm_json_free(char *json);
int  ui_svc_tm_countdown_query(ui_svc_tm_countdown_t *snap);
int  ui_svc_tm_stopwatch_query(ui_svc_tm_stopwatch_t *snap);
int  ui_svc_tm_pomodoro_query(ui_svc_tm_pomodoro_t *snap);

/* Request/consume the tab opened by the next Clock page creation. UI thread
 * only; consuming resets the target to NONE. */
void ui_svc_tm_clock_tab_request(ui_svc_tm_clock_tab_t tab);
ui_svc_tm_clock_tab_t ui_svc_tm_clock_tab_take(void);

/* controls (direct pass-through) */
void ui_svc_tm_alarm_enable_set(const char *id, bool on);
void ui_svc_tm_alarm_remove(const char *id);
void ui_svc_tm_alarm_ack(const char *id);
void ui_svc_tm_reminder_remove(const char *id);
void ui_svc_tm_countdown_create(int hours, int minutes, int seconds);
void ui_svc_tm_countdown_pause(void);
void ui_svc_tm_countdown_resume(void);
void ui_svc_tm_countdown_delete(void);
void ui_svc_tm_stopwatch_start(void);
void ui_svc_tm_stopwatch_pause(void);
void ui_svc_tm_stopwatch_resume(void);
void ui_svc_tm_stopwatch_reset(void);
void ui_svc_tm_pomodoro_start(const ui_svc_tm_pomodoro_cfg_t *cfg);
void ui_svc_tm_pomodoro_pause(void);
void ui_svc_tm_pomodoro_resume(void);
void ui_svc_tm_pomodoro_stop(void);

/* recompute enabled-alarm / future-reminder counts -> ui_state (call after edits) */
void ui_svc_tm_refresh_counts(void);

#endif /* __UI_SVC_TM_H__ */
