/**
 * @file wukong_tm.h
 * @brief Unified time-management service public API.
 *
 * This module is the shared service entry for alarm, reminder, countdown,
 * stopwatch, and pomodoro features.
 */

#ifndef __WUKONG_TM_H__
#define __WUKONG_TM_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Supported time-management feature types.
 */
typedef enum {
    WUKONG_TM_TYPE_ALARM = 0,
    WUKONG_TM_TYPE_REMINDER = 1,
    WUKONG_TM_TYPE_COUNTDOWN = 2,
    WUKONG_TM_TYPE_STOPWATCH = 3,
    WUKONG_TM_TYPE_POMODORO = 4,
} WUKONG_TM_TYPE_E;

/**
 * @brief Maximum length for a time-manage alarm identifier.
 */
#define WUKONG_TM_ALARM_ID_LEN           32
/**
 * @brief Maximum length for a time-manage alarm cron job identifier.
 */
#define WUKONG_TM_ALARM_CRON_JOB_ID_LEN  32
/**
 * @brief Maximum length for a time-manage reminder identifier.
 */
#define WUKONG_TM_REMINDER_ID_LEN        32
/**
 * @brief Maximum length for a time-manage alarm message.
 */
#define WUKONG_TM_ALARM_MESSAGE_LEN     128
/**
 * @brief Maximum length for a time-manage reminder message.
 */
#define WUKONG_TM_REMINDER_MESSAGE_LEN   128
/**
 * @brief Maximum length for a time-manage reminder cron job identifier.
 */
#define WUKONG_TM_REMINDER_CRON_JOB_ID_LEN 32

/**
 * @brief Time-manage alarm repeat rule type.
 */
typedef enum {
    /** Fire only once. */
    WUKONG_TM_ALARM_REPEAT_ONCE = 0,
    /** Fire every day at the specified time. */
    WUKONG_TM_ALARM_REPEAT_DAILY = 1,
    /** Fire on specific weekdays at the specified time. */
    WUKONG_TM_ALARM_REPEAT_WEEKLY = 2,
    /** Fire on a specific day of month at the specified time. */
    WUKONG_TM_ALARM_REPEAT_MONTHLY = 3,
} WUKONG_TM_ALARM_REPEAT_TYPE_E;

/**
 * @brief Time-manage alarm configuration object.
 */
typedef struct {
    /** Whether the alarm is enabled. */
    BOOL_T enabled;
    /** Alarm repeat rule. */
    WUKONG_TM_ALARM_REPEAT_TYPE_E repeat_type;
    /** Trigger hour in local time, range [0, 23]. */
    UINT_T hour;
    /** Trigger minute in local time, range [0, 59]. */
    UINT_T minute;
    /** Weekday bitmask for weekly alarms. */
    UINT_T weekday_mask;
    /** Day of month for monthly alarms, range [1, 31]. */
    UINT_T month_day;
    /** Absolute trigger timestamp for once alarms (UTC POSIX seconds, 0 = unset). */
    TIME_T start_time;
    /** Optional alarm semantic message kept for MCP/cron payload sync. */
    CHAR_T message[WUKONG_TM_ALARM_MESSAGE_LEN + 1];
    /** Bound cron job id, empty before cron mapping is created. */
    CHAR_T cron_job_id[WUKONG_TM_ALARM_CRON_JOB_ID_LEN + 1];
} WUKONG_TM_ALARM_CFG_T;

/**
 * @brief One alarm slot in the in-memory table.
 */
typedef struct {
    /** Whether the slot is currently occupied. */
    BOOL_T used;
    /** Stable generated alarm id. */
    CHAR_T alarm_id[WUKONG_TM_ALARM_ID_LEN + 1];
    /** Alarm business configuration. */
    WUKONG_TM_ALARM_CFG_T cfg;
    /** Whether the alarm is currently in the ringing window. */
    BOOL_T is_ringing;
    /** Monotonic round sequence to reject stale cron callbacks. */
    UINT_T ring_seq;
    /** One-shot cron job id for the unanswered timeout window. */
    CHAR_T ack_timeout_job_id[WUKONG_TM_ALARM_CRON_JOB_ID_LEN + 1];
    /** One-shot cron job id for the pending snooze replay. */
    CHAR_T snooze_job_id[WUKONG_TM_ALARM_CRON_JOB_ID_LEN + 1];
} WUKONG_TM_ALARM_ITEM_T;

/**
 * @brief Time-manage reminder configuration object.
 */
typedef struct {
    /** Whether the reminder is enabled. */
    BOOL_T enabled;
    /** Absolute local trigger time represented as POSIX timestamp. */
    TIME_T start_time;
    /** Reminder message forwarded into cron params and local action. */
    CHAR_T message[WUKONG_TM_REMINDER_MESSAGE_LEN + 1];
    /** Bound cron job id, empty before cron mapping is created. */
    CHAR_T cron_job_id[WUKONG_TM_REMINDER_CRON_JOB_ID_LEN + 1];
} WUKONG_TM_REMINDER_CFG_T;

/**
 * @brief One runtime reminder slot.
 */
typedef struct {
    /** Whether the slot is currently used. */
    BOOL_T in_use;
    /** Stable reminder id exposed to upper layers. */
    CHAR_T reminder_id[WUKONG_TM_REMINDER_ID_LEN + 1];
    /** Stored reminder configuration. */
    WUKONG_TM_REMINDER_CFG_T cfg;
} WUKONG_TM_REMINDER_ITEM_T;

/** Minimum `work_sessions_before_long_break` accepted by wukong_tm_pomodoro_start(). */
#define WUKONG_TM_POMODORO_WORK_BEFORE_LONG_MIN 1
/** Maximum `work_sessions_before_long_break` accepted by wukong_tm_pomodoro_start(). */
#define WUKONG_TM_POMODORO_WORK_BEFORE_LONG_MAX 12

/**
 * @brief Time-manage pomodoro configuration object.
 */
typedef struct {
    /** Work session duration in minutes. */
    INT_T work_duration;
    /** Short break duration in minutes. */
    INT_T short_break_duration;
    /** Long break duration in minutes. */
    INT_T long_break_duration;
    /** Completed work phases before a long break (classic Pomodoro uses 4). */
    INT_T work_sessions_before_long_break;
} WUKONG_TM_POMODORO_CFG_T;

/**
 * @brief Pomodoro phase type.
 */
typedef enum {
    WUKONG_TM_POMODORO_PHASE_WORK = 0,
    WUKONG_TM_POMODORO_PHASE_SHORT_BREAK,
    WUKONG_TM_POMODORO_PHASE_LONG_BREAK,
} WUKONG_TM_POMODORO_PHASE_E;

/**
 * @brief Runtime snapshot of the active pomodoro session.
 */
typedef struct {
    /** Whether one pomodoro session exists. */
    BOOL_T active;
    /** Whether the current session is paused. */
    BOOL_T paused;
    /** Monotonic session identifier used to distinguish lifecycle restarts. */
    UINT_T session_id;
    /** Current phase. */
    WUKONG_TM_POMODORO_PHASE_E phase;
    /** Current cycle index, starting from 1. */
    UINT_T current_cycle;
    /** Completed work phase count. */
    UINT_T completed_work_count;
    /** Current phase start timestamp in local POSIX seconds. */
    TIME_T phase_start_ts;
    /** Current phase end timestamp in local POSIX seconds, or 0 when paused. */
    TIME_T phase_end_ts;
    /** Remaining seconds in the current phase. */
    TIME_T remaining_sec;
    /** Effective pomodoro configuration of the current session. */
    WUKONG_TM_POMODORO_CFG_T cfg;
} WUKONG_TM_POMODORO_STATE_T;

/**
 * @brief Countdown runtime state.
 */
typedef enum {
    WUKONG_TM_COUNTDOWN_STATE_IDLE = 0,
    WUKONG_TM_COUNTDOWN_STATE_RUNNING,
    WUKONG_TM_COUNTDOWN_STATE_PAUSED,
} WUKONG_TM_COUNTDOWN_STATE_E;

/**
 * @brief Runtime snapshot of the active countdown instance.
 */
typedef struct {
    /** Whether one countdown instance exists. */
    BOOL_T active;
    /** Current countdown state. */
    WUKONG_TM_COUNTDOWN_STATE_E state;
    /** Remaining seconds of the current countdown. */
    TIME_T remaining_sec;
    /** Total duration in seconds set at create (constant for the session). */
    TIME_T duration_sec;
    /** Seconds already counted down: duration_sec - remaining_sec (clamped). */
    TIME_T elapsed_sec;
} WUKONG_TM_COUNTDOWN_SNAPSHOT_T;

/**
 * @brief Runtime snapshot of the stopwatch singleton.
 */
typedef struct {
    /** Whether one stopwatch instance exists. */
    BOOL_T active;
    /** Whether the stopwatch is currently paused. */
    BOOL_T paused;
    /** Total elapsed seconds since start (includes paused intervals). */
    TIME_T elapsed_sec;
} WUKONG_TM_STOPWATCH_STATE_T;

/**
 * @brief Initialize the unified time-management service.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_time_manage_init(VOID);

/**
 * @brief Deinitialize the unified time-management service.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_time_manage_deinit(VOID);

/**
 * @brief Initialize the alarm feature under time-manage.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_alarm_init(VOID);

/**
 * @brief Deinitialize the alarm feature under time-manage.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_alarm_deinit(VOID);

/**
 * @brief Add one alarm through the time-manage facade.
 *
 * @param[in] alarm_cfg  Alarm configuration to store.
 * @param[in] alarm_id   Caller-provided alarm identifier (must be unique).
 * @return OPRT_OK on success, OPRT_INVALID_PARM when id is NULL/empty,
 *         OPRT_COM_ERROR when the id already exists or no slot is available.
 */
OPERATE_RET wukong_tm_alarm_add(CONST WUKONG_TM_ALARM_CFG_T *alarm_cfg, CONST CHAR_T *alarm_id);

/**
 * @brief Update one alarm through the time-manage facade.
 *
 * @param[in] alarm_id    Target alarm id.
 * @param[in] alarm_cfg   Replacement alarm configuration.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_alarm_update(CONST CHAR_T *alarm_id, CONST WUKONG_TM_ALARM_CFG_T *alarm_cfg);

/**
 * @brief Read one alarm configuration snapshot by id.
 *
 * @param[in]  alarm_id    Target alarm id.
 * @param[out] alarm_cfg   Buffer used to receive the stored configuration.
 * @return OPRT_OK on success, OPRT_NOT_FOUND when the id does not exist.
 */
OPERATE_RET wukong_tm_alarm_get(CONST CHAR_T *alarm_id, WUKONG_TM_ALARM_CFG_T *alarm_cfg);

/**
 * @brief Remove one alarm through the time-manage facade.
 *
 * @param[in] alarm_id Target alarm id.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_alarm_remove(CONST CHAR_T *alarm_id);

/**
 * @brief Remove all alarms matching the given time description.
 *
 * Delete semantics are intentionally broader than unique lookup so voice
 * deletion can clear duplicate or semantically equivalent alarms.
 *
 * @param[in]  alarm_cfg       Time-description fields used for matching.
 * @param[out] removed_count   Optional number of removed alarms.
 * @return OPRT_OK on success, OPRT_NOT_FOUND when nothing matched.
 */
OPERATE_RET wukong_tm_alarm_remove_by_time(CONST WUKONG_TM_ALARM_CFG_T *alarm_cfg, UINT_T *removed_count);

/**
 * @brief Export the current alarm list through the time-manage facade.
 *
 * @param[out] alarm_list_json Unformatted JSON string allocated by cJSON.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_alarm_list(CHAR_T **alarm_list_json);

/**
 * @brief Find one unique alarm by its time description.
 *
 * @param[in]  alarm_cfg      Time-description fields used for matching.
 * @param[out] alarm_id       Buffer used to receive the matched alarm id.
 * @param[in]  alarm_id_len   Size of @p alarm_id in bytes.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_alarm_find_by_time(CONST WUKONG_TM_ALARM_CFG_T *alarm_cfg, CHAR_T *alarm_id, UINT_T alarm_id_len);

/**
 * @brief Fire one alarm immediately by alarm id.
 *
 * @param[in] alarm_id Target alarm id.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_alarm_fire(CONST CHAR_T *alarm_id);

/**
 * @brief Acknowledge one active/pending alarm instance by id.
 *
 * This stops the current ringing round and cancels any pending snooze created
 * for the same alarm round.
 *
 * @param[in] alarm_id Target alarm id.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_alarm_ack(CONST CHAR_T *alarm_id);

/**
 * @brief Acknowledge the current active or pending snooze alarm.
 *
 * @return OPRT_OK on success, OPRT_NOT_FOUND when nothing is active.
 */
OPERATE_RET wukong_tm_alarm_ack_active(VOID);

/**
 * @brief Enable or disable alarm snooze for unanswered ringing windows.
 *
 * When disabled, the unanswered timeout ends the round without scheduling a
 * delayed replay, and any pending snooze cron jobs are removed. Default after
 * boot is enabled until changed.
 *
 * @param[in] enable TRUE to allow snooze, FALSE to disable.
 * @return OPRT_OK on success, OPRT_COM_ERROR when the alarm module is not initialized.
 */
OPERATE_RET wukong_tm_alarm_snooze_enable_set(BOOL_T enable);

/**
 * @brief Set the alarm ring duration (unanswered window before snooze).
 *
 * Takes effect on the next ring cycle; already-scheduled timeout jobs
 * are not retroactively modified. Can be called before init.
 *
 * @param[in] seconds Ring duration in seconds, must be > 0.
 * @return OPRT_OK on success, OPRT_INVALID_PARM when seconds is 0.
 */
OPERATE_RET wukong_tm_alarm_ring_duration_set(UINT_T seconds);

/**
 * @brief Get the current alarm ring duration in seconds.
 *
 * @return Current ring duration in seconds.
 */
UINT_T wukong_tm_alarm_ring_duration_get(VOID);

/**
 * @brief Set the snooze delay (wait time before replaying an unanswered alarm).
 *
 * Takes effect on the next snooze cycle; already-scheduled snooze jobs
 * are not retroactively modified. Can be called before init.
 *
 * @param[in] seconds Snooze delay in seconds, must be > 0.
 * @return OPRT_OK on success, OPRT_INVALID_PARM when seconds is 0.
 */
OPERATE_RET wukong_tm_alarm_snooze_delay_set(UINT_T seconds);

/**
 * @brief Get the current snooze delay in seconds.
 *
 * @return Current snooze delay in seconds.
 */
UINT_T wukong_tm_alarm_snooze_delay_get(VOID);

/**
 * @brief Set the maximum snooze round count before auto-finish.
 *
 * When the snooze round count exceeds this limit, the alarm emits a
 * FINISH event instead of scheduling another snooze replay.
 * Can be called before init.
 *
 * @param[in] count Maximum snooze rounds. 0 means unlimited (no cap).
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_alarm_snooze_max_count_set(UINT_T count);

/**
 * @brief Get the current maximum snooze round count.
 *
 * @return Current max snooze count. 0 means unlimited.
 */
UINT_T wukong_tm_alarm_snooze_max_count_get(VOID);

/**
 * @brief Enable or disable one alarm by id, syncing its cron job accordingly.
 *
 * When disabling, the cron job is removed and runtime state is cleared.
 * When enabling, the cron job is recreated. Enabling a once-alarm whose
 * trigger time has already passed returns OPRT_INVALID_PARM.
 *
 * @param[in] alarm_id Target alarm id.
 * @param[in] enable   TRUE to enable, FALSE to disable.
 * @return OPRT_OK on success, OPRT_NOT_FOUND when the id does not exist,
 *         OPRT_INVALID_PARM when enabling a once-alarm whose trigger time
 *         has already passed.
 */
OPERATE_RET wukong_tm_alarm_enable_set(CONST CHAR_T *alarm_id, BOOL_T enable);

/**
 * @brief Initialize the reminder feature under time-manage.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_reminder_init(VOID);

/**
 * @brief Deinitialize the reminder feature under time-manage.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_reminder_deinit(VOID);

/**
 * @brief Add one reminder through the time-manage facade.
 *
 * @param[in] reminder_cfg  Reminder configuration to store.
 * @param[in] reminder_id   Caller-provided reminder identifier (must be unique).
 * @return OPRT_OK on success, OPRT_INVALID_PARM when id is NULL/empty,
 *         OPRT_COM_ERROR when the id already exists or no slot is available.
 */
OPERATE_RET wukong_tm_reminder_add(CONST WUKONG_TM_REMINDER_CFG_T *reminder_cfg,
                                   CONST CHAR_T *reminder_id);

/**
 * @brief Update one reminder through the time-manage facade.
 *
 * @param[in] reminder_id     Target reminder id.
 * @param[in] reminder_cfg    Replacement reminder configuration.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_reminder_update(CONST CHAR_T *reminder_id,
                                      CONST WUKONG_TM_REMINDER_CFG_T *reminder_cfg);

/**
 * @brief Read one reminder configuration snapshot by id.
 *
 * @param[in]  reminder_id   Target reminder id.
 * @param[out] reminder_cfg  Buffer used to receive the stored configuration.
 * @return OPRT_OK on success, OPRT_NOT_FOUND when the id does not exist.
 */
OPERATE_RET wukong_tm_reminder_get(CONST CHAR_T *reminder_id, WUKONG_TM_REMINDER_CFG_T *reminder_cfg);

/**
 * @brief Remove one reminder through the time-manage facade.
 *
 * @param[in] reminder_id Target reminder id.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_reminder_remove(CONST CHAR_T *reminder_id);

/**
 * @brief Remove all reminders matching the given trigger time.
 *
 * @param[in]  start_time       Exact reminder trigger time.
 * @param[out] removed_count    Optional number of removed reminders.
 * @return OPRT_OK on success, OPRT_NOT_FOUND when nothing matched.
 */
OPERATE_RET wukong_tm_reminder_remove_by_time(TIME_T start_time, UINT_T *removed_count);

/**
 * @brief Fire one reminder immediately by reminder id.
 *
 * @param[in] reminder_id Target reminder id.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_reminder_fire(CONST CHAR_T *reminder_id);

/**
 * @brief Find one reminder by exact trigger time.
 *
 * @param[in]  start_time        Exact reminder trigger time.
 * @param[out] reminder_id       Buffer used to receive the matched reminder id.
 * @param[in]  reminder_id_len   Size of @p reminder_id in bytes.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_reminder_find_by_time(TIME_T start_time,
                                            CHAR_T *reminder_id, UINT_T reminder_id_len);

/**
 * @brief Query reminders and export them as text.
 *
 * @param[in] start_time Inclusive query start time, or 0 to ignore.
 * @param[in] end_time   Inclusive query end time, or 0 to ignore.
 * @param[in] keyword    Optional keyword matched against reminder message.
 * @return Newly allocated text on success, or NULL when no result/error.
 */
CHAR_T *wukong_tm_reminder_query_text(TIME_T start_time, TIME_T end_time, CONST CHAR_T *keyword);

/**
 * @brief Initialize the countdown feature under time-manage.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_countdown_init(VOID);

/**
 * @brief Deinitialize the countdown feature under time-manage.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_countdown_deinit(VOID);

/**
 * @brief Create the singleton countdown timer.
 *
 * Only one countdown may exist at a time. When creation succeeds the module
 * binds one `once` cron job for the next progress tick.
 *
 * @param[in] hours    Countdown hours.
 * @param[in] minutes  Countdown minutes.
 * @param[in] seconds  Countdown seconds.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_countdown_create(INT_T hours, INT_T minutes, INT_T seconds);

/**
 * @brief Query the runtime snapshot of the active countdown timer.
 *
 * @param[out] snapshot Countdown snapshot buffer.
 * @return OPRT_OK on success, OPRT_NOT_FOUND when no active countdown exists.
 */
OPERATE_RET wukong_tm_countdown_query(WUKONG_TM_COUNTDOWN_SNAPSHOT_T *snapshot);

/**
 * @brief Pause the active countdown timer and remove its cron job.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_countdown_pause(VOID);

/**
 * @brief Resume the paused countdown timer and recreate its cron job.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_countdown_resume(VOID);

/**
 * @brief Delete the countdown timer and remove its cron job.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_countdown_delete(VOID);

/**
 * @brief Initialize the stopwatch feature under time-manage.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_stopwatch_init(VOID);

/**
 * @brief Deinitialize the stopwatch feature under time-manage.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_stopwatch_deinit(VOID);

/**
 * @brief Start the stopwatch.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_stopwatch_start(VOID);

/**
 * @brief Pause the stopwatch.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_stopwatch_pause(VOID);

/**
 * @brief Resume the stopwatch.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_stopwatch_resume(VOID);

/**
 * @brief Stop the stopwatch.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_stopwatch_stop(VOID);

/**
 * @brief Reset the stopwatch.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_stopwatch_reset(VOID);

/**
 * @brief Query the runtime snapshot of the stopwatch singleton.
 *
 * @param[out] state Stopwatch snapshot buffer.
 * @return OPRT_OK on success, OPRT_NOT_FOUND when no active stopwatch exists.
 */
OPERATE_RET wukong_tm_stopwatch_query(WUKONG_TM_STOPWATCH_STATE_T *state);

/**
 * @brief Initialize the pomodoro feature under time-manage.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_pomodoro_init(VOID);

/**
 * @brief Deinitialize the pomodoro feature under time-manage.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_pomodoro_deinit(VOID);

/**
 * @brief Start one pomodoro timer.
 *
 * @param[in] pomodoro_cfg Pomodoro configuration.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_pomodoro_start(CONST WUKONG_TM_POMODORO_CFG_T *pomodoro_cfg);

/**
 * @brief Pause the active pomodoro timer.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_pomodoro_pause(VOID);

/**
 * @brief Resume the active pomodoro timer.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_pomodoro_resume(VOID);

/**
 * @brief Stop the active pomodoro timer.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_pomodoro_stop(VOID);

/**
 * @brief Query the runtime snapshot of the active pomodoro session.
 *
 * @param[out] state Runtime snapshot buffer.
 * @return OPRT_OK on success, OPRT_NOT_FOUND when no active session exists.
 */
OPERATE_RET wukong_tm_pomodoro_query(WUKONG_TM_POMODORO_STATE_T *state);

/**
 * @brief Platform hook used to execute one local reminder action.
 *
 * The default implementation only logs the reminder text. Platforms can
 * override this symbol to connect the reminder fire path to local TTS or any
 * other fixed reminder behavior.
 *
 * @param[in] message Reminder message to consume.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_reminder_action_notify(CONST CHAR_T *message);

#ifdef __cplusplus
}
#endif

#endif /* __WUKONG_TM_H__ */
