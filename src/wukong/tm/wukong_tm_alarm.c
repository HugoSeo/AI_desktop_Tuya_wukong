/**
 * @file wukong_tm_alarm.c
 * @brief Alarm service implementation for the unified time-management module.
 */

#include "wukong_tm.h"

#include <stdio.h>
#include <string.h>

#include "tal_log.h"
#include "tal_memory.h"
#include "tal_time_service.h"
#include "tuya_ws_db.h"
#include "ty_cJSON.h"
#include "wukong_ai_agent.h"
#include "wukong_cron.h"
#include "wukong_tm_internal.h"

/**
 * @brief Maximum number of in-memory alarms supported by v1.
 */
#define WUKONG_TM_ALARM_MAX_COUNT  8
/**
 * @brief Local JSON-RPC method name used by cron to fire alarms.
 */
#define WUKONG_TM_ALARM_FIRE_METHOD  "alarm.fire"
/**
 * @brief Local JSON-RPC method used by unanswered alarms to enter snooze state.
 */
#define WUKONG_TM_ALARM_SNOOZE_TIMEOUT_METHOD  "alarm.snooze.timeout"
/**
 * @brief Local JSON-RPC method used to replay one snoozed alarm.
 */
#define WUKONG_TM_ALARM_SNOOZE_FIRE_METHOD     "alarm.snooze.fire"
/**
 * @brief Default unanswered window before one alarm enters snooze.
 */
#define WUKONG_TM_ALARM_ACK_TIMEOUT_SEC  30
/**
 * @brief Default snooze delay once the unanswered window expires.
 */
#define WUKONG_TM_ALARM_SNOOZE_DELAY_SEC  300
/**
 * @brief Default maximum snooze rounds before the alarm auto-finishes (0 = unlimited).
 */
#define WUKONG_TM_ALARM_SNOOZE_MAX_COUNT  3
/**
 * @brief KV storage key for persisted alarm data.
 */
#define WUKONG_TM_ALARM_KV_KEY  "wk_tm_alarms"
/**
 * @brief Seconds in one day, used to roll once-alarms to tomorrow.
 */
#define WUKONG_TM_ALARM_ONCE_DAY_SEC  86400U

/**
 * @brief Global alarm runtime context.
 */
typedef struct {
    /** Whether the service has been initialized. */
    BOOL_T initialized;
    /** Monotonic sequence used to generate alarm ids. */
    UINT_T next_alarm_seq;
    /** Fixed-size in-memory alarm table. */
    WUKONG_TM_ALARM_ITEM_T alarms[WUKONG_TM_ALARM_MAX_COUNT];
} WUKONG_TM_ALARM_CTX_T;

/**
 * @brief Global time-manage alarm context.
 */
STATIC WUKONG_TM_ALARM_CTX_T s_alarm_ctx;

/**
 * @brief Whether unanswered timeout may schedule snooze replay (default enabled).
 */
STATIC BOOL_T s_alarm_snooze_enabled = TRUE;
/**
 * @brief Configurable ring duration in seconds (unanswered window before snooze).
 */
STATIC UINT_T s_alarm_ring_duration_sec = WUKONG_TM_ALARM_ACK_TIMEOUT_SEC;
/**
 * @brief Configurable snooze delay in seconds (wait time before replaying alarm).
 */
STATIC UINT_T s_alarm_snooze_delay_sec = WUKONG_TM_ALARM_SNOOZE_DELAY_SEC;
/**
 * @brief Maximum snooze rounds before the alarm auto-finishes (0 = unlimited).
 */
STATIC UINT_T s_alarm_max_snooze_count = WUKONG_TM_ALARM_SNOOZE_MAX_COUNT;

#define ALARM_CRON_BUF_SIZE 768
STATIC CHAR_T s_cron_buf[ALARM_CRON_BUF_SIZE];

STATIC OPERATE_RET __alarm_remove_job_if_needed(CHAR_T *job_id);
STATIC VOID __alarm_clear_runtime_state(WUKONG_TM_ALARM_ITEM_T *alarm, BOOL_T remove_jobs);
STATIC OPERATE_RET __alarm_build_once_cron_expr(TIME_T trigger_time, CHAR_T *cron_expr, UINT_T expr_len);
STATIC OPERATE_RET __alarm_escape_message(CONST CHAR_T *src, CHAR_T *dst, UINT_T dst_len);
STATIC OPERATE_RET __alarm_schedule_runtime_job(WUKONG_TM_ALARM_ITEM_T *alarm, CONST CHAR_T *method,
                                                UINT_T ring_seq, TIME_T delay_sec,
                                                CHAR_T *job_id, UINT_T job_id_len);
STATIC OPERATE_RET __alarm_emit_event(CONST CHAR_T *alarm_id, UINT_T ring_seq, WUKONG_TM_TIMER_OPR_E opr);
STATIC OPERATE_RET __alarm_start_ringing(WUKONG_TM_ALARM_ITEM_T *alarm);
STATIC OPERATE_RET __alarm_ack_index(INT_T index);
STATIC OPERATE_RET __alarm_parse_runtime_params(CONST ty_cJSON *params, CONST CHAR_T **alarm_id, UINT_T *ring_seq);
STATIC OPERATE_RET __alarm_once_rollover_to_next_fire(WUKONG_TM_ALARM_CFG_T *alarm_cfg);
STATIC INT_T __alarm_is_expired(CONST WUKONG_TM_ALARM_ITEM_T *alarm, TIME_T now,
                                CONST POSIX_TM_S *local_now);
STATIC BOOL_T __alarm_disable_expired_once_if_needed(WUKONG_TM_ALARM_ITEM_T *alarm, TIME_T now,
                                                     CONST POSIX_TM_S *local_now);

/**
 * @brief Return whether one weekly alarm covers all weekdays.
 *
 * @param[in] alarm_cfg Alarm configuration to check.
 * @return TRUE when the weekly mask contains Sunday-Saturday.
 */
STATIC BOOL_T __alarm_is_all_weekdays(CONST WUKONG_TM_ALARM_CFG_T *alarm_cfg)
{
    TUYA_CHECK_NULL_RETURN(alarm_cfg, FALSE);
    return (alarm_cfg->repeat_type == WUKONG_TM_ALARM_REPEAT_WEEKLY &&
            alarm_cfg->weekday_mask == 0x7FU) ? TRUE : FALSE;
}

/**
 * @brief Return whether two alarm configs are semantically equal for delete matching.
 *
 * Delete should tolerate duplicate records and the common "daily" versus
 * "weekly all weekdays" representation difference.
 *
 * @param[in] stored_cfg Persisted alarm configuration.
 * @param[in] match_cfg  Time-description provided by the caller.
 * @return TRUE when both configs should be treated as the same schedule.
 */
STATIC BOOL_T __alarm_semantic_match(CONST WUKONG_TM_ALARM_CFG_T *stored_cfg,
                                     CONST WUKONG_TM_ALARM_CFG_T *match_cfg)
{
    TUYA_CHECK_NULL_RETURN(stored_cfg, FALSE);
    TUYA_CHECK_NULL_RETURN(match_cfg, FALSE);

    if (stored_cfg->hour != match_cfg->hour || stored_cfg->minute != match_cfg->minute) {
        return FALSE;
    }

    if (stored_cfg->repeat_type == match_cfg->repeat_type) {
        if (stored_cfg->repeat_type == WUKONG_TM_ALARM_REPEAT_WEEKLY) {
            return (stored_cfg->weekday_mask == match_cfg->weekday_mask) ? TRUE : FALSE;
        }
        if (stored_cfg->repeat_type == WUKONG_TM_ALARM_REPEAT_MONTHLY) {
            return (stored_cfg->month_day == match_cfg->month_day) ? TRUE : FALSE;
        }
        return TRUE;
    }

    if ((stored_cfg->repeat_type == WUKONG_TM_ALARM_REPEAT_DAILY && __alarm_is_all_weekdays(match_cfg)) ||
        (match_cfg->repeat_type == WUKONG_TM_ALARM_REPEAT_DAILY && __alarm_is_all_weekdays(stored_cfg))) {
        return TRUE;
    }

    return FALSE;
}

/**
 * @brief Validate one alarm configuration object.
 *
 * @param[in] alarm_cfg Alarm configuration to validate.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __alarm_validate_cfg(CONST WUKONG_TM_ALARM_CFG_T *alarm_cfg)
{
    TUYA_CHECK_NULL_RETURN(alarm_cfg, OPRT_INVALID_PARM);

    if (alarm_cfg->hour > 23 || alarm_cfg->minute > 59) {
        return OPRT_INVALID_PARM;
    }

    if (alarm_cfg->repeat_type < WUKONG_TM_ALARM_REPEAT_ONCE ||
        alarm_cfg->repeat_type > WUKONG_TM_ALARM_REPEAT_MONTHLY) {
        return OPRT_INVALID_PARM;
    }

    if (alarm_cfg->repeat_type == WUKONG_TM_ALARM_REPEAT_WEEKLY &&
        alarm_cfg->weekday_mask == 0) {
        return OPRT_INVALID_PARM;
    }

    if (alarm_cfg->repeat_type == WUKONG_TM_ALARM_REPEAT_MONTHLY &&
        (alarm_cfg->month_day == 0 || alarm_cfg->month_day > 31)) {
        return OPRT_INVALID_PARM;
    }

    return OPRT_OK;
}

/**
 * @brief Find an alarm slot by alarm id.
 *
 * @param[in] alarm_id Target alarm id.
 * @return Slot index on success, `-1` when not found.
 */
STATIC INT_T __alarm_find_index(CONST CHAR_T *alarm_id)
{
    INT_T index = 0;

    if (alarm_id == NULL) {
        return -1;
    }

    for (index = 0; index < WUKONG_TM_ALARM_MAX_COUNT; index++) {
        if (s_alarm_ctx.alarms[index].used &&
            strcmp(s_alarm_ctx.alarms[index].alarm_id, alarm_id) == 0) {
            return index;
        }
    }

    return -1;
}

/**
 * @brief Find the first free slot in the in-memory alarm table.
 *
 * @return Slot index on success, `-1` when the table is full.
 */
STATIC INT_T __alarm_alloc_index(VOID)
{
    INT_T index = 0;

    for (index = 0; index < WUKONG_TM_ALARM_MAX_COUNT; index++) {
        if (!s_alarm_ctx.alarms[index].used) {
            return index;
        }
    }

    return -1;
}

/**
 * @brief Build the cron expression for one alarm configuration.
 *
 * @param[in]  alarm_cfg   Source alarm configuration.
 * @param[out] cron_expr   Output cron expression buffer.
 * @param[in]  expr_len    Size of @p cron_expr in bytes.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __alarm_build_cron_expr(CONST WUKONG_TM_ALARM_CFG_T *alarm_cfg,
                                           CHAR_T *cron_expr, UINT_T expr_len)
{
    UINT_T weekday = 0;
    CHAR_T weekday_expr[32] = {0};
    UINT_T used = 0;

    TUYA_CHECK_NULL_RETURN(alarm_cfg, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(cron_expr, OPRT_INVALID_PARM);
    if (expr_len == 0) {
        return OPRT_INVALID_PARM;
    }

    switch (alarm_cfg->repeat_type) {
    case WUKONG_TM_ALARM_REPEAT_ONCE:
        if (alarm_cfg->start_time > 0) {
            return __alarm_build_once_cron_expr(alarm_cfg->start_time, cron_expr, expr_len);
        }
        (VOID)snprintf(cron_expr, expr_len, "0 %u %u * * *", alarm_cfg->minute, alarm_cfg->hour);
        return OPRT_OK;
    case WUKONG_TM_ALARM_REPEAT_DAILY:
        (VOID)snprintf(cron_expr, expr_len, "0 %u %u * * *", alarm_cfg->minute, alarm_cfg->hour);
        return OPRT_OK;
    case WUKONG_TM_ALARM_REPEAT_WEEKLY:
        for (weekday = 0; weekday <= 6; weekday++) {
            if ((alarm_cfg->weekday_mask & (1U << weekday)) == 0) {
                continue;
            }
            used += (UINT_T)snprintf(weekday_expr + used, sizeof(weekday_expr) - used,
                                     (used == 0) ? "%u" : ",%u", weekday);
        }
        if (used == 0) {
            return OPRT_INVALID_PARM;
        }
        (VOID)snprintf(cron_expr, expr_len, "0 %u %u * * %s",
                       alarm_cfg->minute, alarm_cfg->hour, weekday_expr);
        return OPRT_OK;
    case WUKONG_TM_ALARM_REPEAT_MONTHLY:
        (VOID)snprintf(cron_expr, expr_len, "0 %u %u %u * *",
                       alarm_cfg->minute, alarm_cfg->hour, alarm_cfg->month_day);
        return OPRT_OK;
    default:
        return OPRT_INVALID_PARM;
    }
}

/**
 * @brief Remove one cron job id when present and then clear the storage.
 *
 * @param[in,out] job_id Target job id buffer.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __alarm_remove_job_if_needed(CHAR_T *job_id)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(job_id, OPRT_INVALID_PARM);

    if (job_id[0] == '\0') {
        return OPRT_OK;
    }

    rt = wukong_cron_job_remove(job_id);
    if (rt != OPRT_OK && rt != OPRT_NOT_FOUND) {
        return rt;
    }

    job_id[0] = '\0';
    return OPRT_OK;
}

/**
 * @brief Clear one alarm runtime ringing/snooze state.
 *
 * @param[in,out] alarm        Alarm runtime slot.
 * @param[in]     remove_jobs  Whether pending runtime cron jobs should be removed.
 */
STATIC VOID __alarm_clear_runtime_state(WUKONG_TM_ALARM_ITEM_T *alarm, BOOL_T remove_jobs)
{
    if (alarm == NULL) {
        return;
    }

    if (remove_jobs) {
        (VOID)__alarm_remove_job_if_needed(alarm->ack_timeout_job_id);
        (VOID)__alarm_remove_job_if_needed(alarm->snooze_job_id);
    } else {
        alarm->ack_timeout_job_id[0] = '\0';
        alarm->snooze_job_id[0] = '\0';
    }

    alarm->is_ringing = FALSE;
}

/**
 * @brief Roll a once-alarm to the next valid fire time (today or tomorrow).
 *
 * When the wall-clock time (hour:minute) is earlier than now, add 24 hours so
 * the alarm fires tomorrow. When it is still later today, keep today's slot.
 * A future absolute start_time is left unchanged.
 *
 * @param[in,out] alarm_cfg Once-alarm configuration.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __alarm_once_rollover_to_next_fire(WUKONG_TM_ALARM_CFG_T *alarm_cfg)
{
    TIME_T now = 0;
    POSIX_TM_S local_now = {0};
    POSIX_TM_S trigger_tm = {0};
    TIME_T trigger_ts = 0;
    POSIX_TM_S local_now_copy = {0};
    TIME_T tz_offset = 0;

    TUYA_CHECK_NULL_RETURN(alarm_cfg, OPRT_INVALID_PARM);
    if (alarm_cfg->repeat_type != WUKONG_TM_ALARM_REPEAT_ONCE) {
        return OPRT_OK;
    }

    now = tal_time_get_posix();
    if (now == 0 || tal_time_get_local_time_custom(now, &local_now) != OPRT_OK) {
        return OPRT_COM_ERROR;
    }

    if (alarm_cfg->start_time > now) {
        return OPRT_OK;
    }

    /* tal_time_mktime treats input as UTC; compensate for timezone offset */
    local_now_copy = local_now;
    tz_offset = tal_time_mktime(&local_now_copy) - now;

    trigger_tm = local_now;
    trigger_tm.tm_hour = (INT_T)alarm_cfg->hour;
    trigger_tm.tm_min = (INT_T)alarm_cfg->minute;
    trigger_tm.tm_sec = 0;
    trigger_ts = tal_time_mktime(&trigger_tm) - tz_offset;
    if (trigger_ts == 0) {
        return OPRT_COM_ERROR;
    }

    if (trigger_ts <= now) {
        if (trigger_ts > (TIME_T)(-1 - (TIME_T)WUKONG_TM_ALARM_ONCE_DAY_SEC)) {
            return OPRT_COM_ERROR;
        }
        trigger_ts += (TIME_T)WUKONG_TM_ALARM_ONCE_DAY_SEC;
    }

    alarm_cfg->start_time = trigger_ts;
    if (tal_time_get_local_time_custom(trigger_ts, &trigger_tm) == OPRT_OK) {
        alarm_cfg->hour = (UINT_T)trigger_tm.tm_hour;
        alarm_cfg->minute = (UINT_T)trigger_tm.tm_min;
    }
    return OPRT_OK;
}

/**
 * @brief Disable one expired once-alarm and tear down its cron/runtime state.
 *
 * @param[in,out] alarm      Alarm slot.
 * @param[in]     now        Current POSIX timestamp.
 * @param[in]     local_now  Current local broken-down time.
 * @return TRUE when the alarm was auto-disabled.
 */
STATIC BOOL_T __alarm_disable_expired_once_if_needed(WUKONG_TM_ALARM_ITEM_T *alarm, TIME_T now,
                                                     CONST POSIX_TM_S *local_now)
{
    if (alarm == NULL || local_now == NULL || now == 0) {
        return FALSE;
    }
    if (!alarm->used || alarm->cfg.repeat_type != WUKONG_TM_ALARM_REPEAT_ONCE ||
        !alarm->cfg.enabled) {
        return FALSE;
    }
    if (!__alarm_is_expired(alarm, now, local_now)) {
        return FALSE;
    }

    alarm->cfg.enabled = FALSE;
    __alarm_clear_runtime_state(alarm, TRUE);
    alarm->ring_seq = 0;
    if (alarm->cfg.cron_job_id[0] != '\0') {
        (VOID)__alarm_remove_job_if_needed(alarm->cfg.cron_job_id);
    }
    TAL_PR_DEBUG("alarm -> auto-disabled expired once-alarm %s", alarm->alarm_id);
    return TRUE;
}

/**
 * @brief Build a one-shot cron expression from an absolute local trigger time.
 *
 * @param[in]  trigger_time Absolute POSIX trigger timestamp.
 * @param[out] cron_expr    Output cron expression buffer.
 * @param[in]  expr_len     Size of @p cron_expr in bytes.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __alarm_build_once_cron_expr(TIME_T trigger_time, CHAR_T *cron_expr, UINT_T expr_len)
{
    POSIX_TM_S local_tm = {0};
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(cron_expr, OPRT_INVALID_PARM);
    if (expr_len == 0) {
        return OPRT_INVALID_PARM;
    }

    rt = tal_time_get_local_time_custom(trigger_time, &local_tm);
    if (rt != OPRT_OK) {
        return rt;
    }

    (VOID)snprintf(cron_expr, expr_len, "%d %d %d %d %d *",
                   local_tm.tm_sec, local_tm.tm_min, local_tm.tm_hour,
                   local_tm.tm_mday, local_tm.tm_mon + 1);
    return OPRT_OK;
}

/**
 * @brief Escape one alarm message for safe JSON embedding.
 *
 * @param[in]  src      Source plain text.
 * @param[out] dst      Destination buffer.
 * @param[in]  dst_len  Size of @p dst in bytes.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __alarm_escape_message(CONST CHAR_T *src, CHAR_T *dst, UINT_T dst_len)
{
    UINT_T src_index = 0;
    UINT_T dst_index = 0;

    TUYA_CHECK_NULL_RETURN(src, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(dst, OPRT_INVALID_PARM);
    if (dst_len == 0) {
        return OPRT_INVALID_PARM;
    }

    while (src[src_index] != '\0') {
        CHAR_T ch = src[src_index++];

        if (ch == '\"' || ch == '\\') {
            if (dst_index + 2 >= dst_len) {
                return OPRT_COM_ERROR;
            }
            dst[dst_index++] = '\\';
            dst[dst_index++] = ch;
            continue;
        }

        if (dst_index + 1 >= dst_len) {
            return OPRT_COM_ERROR;
        }
        dst[dst_index++] = ch;
    }

    dst[dst_index] = '\0';
    return OPRT_OK;
}

/**
 * @brief Schedule one runtime cron job for unanswered timeout or snooze replay.
 *
 * @param[in]  alarm       Target alarm runtime slot.
 * @param[in]  method      Local JSON-RPC method.
 * @param[in]  ring_seq    Alarm round sequence bound to the job.
 * @param[in]  delay_sec   Relative delay in seconds.
 * @param[out] job_id      Output cron job id buffer.
 * @param[in]  job_id_len  Size of @p job_id in bytes.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __alarm_schedule_runtime_job(WUKONG_TM_ALARM_ITEM_T *alarm, CONST CHAR_T *method,
                                                UINT_T ring_seq, TIME_T delay_sec,
                                                CHAR_T *job_id, UINT_T job_id_len)
{
    CHAR_T cron_expr[32] = {0};
    TIME_T trigger_time = 0;
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(alarm, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(method, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(job_id, OPRT_INVALID_PARM);
    if (job_id_len == 0) {
        return OPRT_INVALID_PARM;
    }

    trigger_time = tal_time_get_posix() + delay_sec;
    rt = __alarm_build_once_cron_expr(trigger_time, cron_expr, sizeof(cron_expr));
    if (rt != OPRT_OK) {
        return rt;
    }

    (VOID)snprintf(s_cron_buf, ALARM_CRON_BUF_SIZE,
                   "{\"name\":\"alarm-runtime-%s\",\"enabled\":1,\"once\":1,\"cron\":\"%s\","
                   "\"request\":{\"jsonrpc\":\"2.0\",\"id\":\"req-%s-%u\","
                   "\"method\":\"%s\",\"params\":{\"alarm_id\":\"%s\",\"ring_seq\":%u}}}",
                   alarm->alarm_id, cron_expr, alarm->alarm_id, ring_seq,
                   method, alarm->alarm_id, ring_seq);

    return wukong_cron_job_add(s_cron_buf, job_id, job_id_len);
}

/**
 * @brief Create or replace the cron job mapped to one alarm slot.
 *
 * @param[in,out] alarm Target alarm slot.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __alarm_sync_cron_job(WUKONG_TM_ALARM_ITEM_T *alarm)
{
    CHAR_T cron_expr[32] = {0};
    CHAR_T escaped_message[(WUKONG_TM_ALARM_MESSAGE_LEN * 2) + 1] = {0};
    CHAR_T cron_job_id[WUKONG_TM_ALARM_CRON_JOB_ID_LEN + 1] = {0};
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(alarm, OPRT_INVALID_PARM);

    if (alarm->cfg.cron_job_id[0] != '\0') {
        rt = wukong_cron_job_remove(alarm->cfg.cron_job_id);
        if (rt != OPRT_OK && rt != OPRT_NOT_FOUND) {
            return rt;
        }
        alarm->cfg.cron_job_id[0] = '\0';
    }

    rt = __alarm_build_cron_expr(&alarm->cfg, cron_expr, sizeof(cron_expr));
    if (rt != OPRT_OK) {
        return rt;
    }

    rt = __alarm_escape_message(alarm->cfg.message, escaped_message, sizeof(escaped_message));
    if (rt != OPRT_OK) {
        return rt;
    }

    (VOID)snprintf(s_cron_buf, ALARM_CRON_BUF_SIZE,
                   "{\"name\":\"alarm-%s\",\"enabled\":%d,\"once\":%d,\"cron\":\"%s\","
                   "\"request\":{\"jsonrpc\":\"2.0\",\"id\":\"req-%s\","
                   "\"method\":\"%s\",\"params\":{\"alarm_id\":\"%s\",\"message\":\"%s\"}}}",
                   alarm->alarm_id, alarm->cfg.enabled ? 1 : 0,
                   (alarm->cfg.repeat_type == WUKONG_TM_ALARM_REPEAT_ONCE) ? 1 : 0, cron_expr,
                   alarm->alarm_id, WUKONG_TM_ALARM_FIRE_METHOD, alarm->alarm_id, escaped_message);

    rt = wukong_cron_job_add(s_cron_buf, cron_job_id, sizeof(cron_job_id));
    if (rt != OPRT_OK) {
        return rt;
    }

    strncpy(alarm->cfg.cron_job_id, cron_job_id, sizeof(alarm->cfg.cron_job_id) - 1);
    return OPRT_OK;
}

STATIC OPERATE_RET __alarm_store_notify(VOID)
{
    UINT_T offset = 0;
    UINT8_T *msg = NULL;
    UINT_T len = 0;
    WUKONG_TM_TIMER_OPR_E opr = WUKONG_TM_TIMER_OPR_SYNC_LIST;
    WUKONG_TM_ALARM_ITEM_T *alarm_ptr = &s_alarm_ctx.alarms[0];
    UINT8_T alarm_count = (UINT8_T)WUKONG_TM_ALARM_MAX_COUNT;

    len += (WUKONG_TM_TLV_TL_LEN + 1);                      //opr
    len += (WUKONG_TM_TLV_TL_LEN + sizeof(alarm_ptr));      //alarm item pointer
    len += (WUKONG_TM_TLV_TL_LEN + 1);                      //alarm count
    msg = tal_malloc(len);
    if (msg == NULL) {
        TAL_PR_ERR("%s: malloc failed", __func__);
        return OPRT_MALLOC_FAILED;
    }
    __tm_tlv_pack(msg, WUKONG_TM_TAG_ALARM_OPR, 1, (CONST UINT8_T *)&opr, &offset);
    __tm_tlv_pack(msg, WUKONG_TM_TAG_ALARM_ITEM, sizeof(alarm_ptr), (CONST UINT8_T *)&alarm_ptr, &offset);
    __tm_tlv_pack(msg, WUKONG_TM_TAG_ALARM_COUNT, 1, (CONST UINT8_T *)&alarm_count, &offset);
    wukong_ai_event_notify(WUKONG_AI_EVENT_CLOCK_MCP_ALARM, msg);
    tal_free(msg);
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * KV persistence
 * --------------------------------------------------------------------------- */
/**
 * @brief Serialize all in-memory alarms to KV storage.
 *
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __alarm_store_save(VOID)
{
    ty_cJSON *root = NULL;
    ty_cJSON *arr = NULL;
    CHAR_T *json_str = NULL;
    INT_T index = 0;
    OPERATE_RET rt = OPRT_OK;

    root = ty_cJSON_CreateObject();
    arr = ty_cJSON_CreateArray();
    if (root == NULL || arr == NULL) {
        ty_cJSON_Delete(root);
        ty_cJSON_Delete(arr);
        return OPRT_MALLOC_FAILED;
    }

    for (index = 0; index < WUKONG_TM_ALARM_MAX_COUNT; index++) {
        ty_cJSON *item = NULL;
        WUKONG_TM_ALARM_ITEM_T *alarm = &s_alarm_ctx.alarms[index];

        if (!alarm->used) {
            continue;
        }

        item = ty_cJSON_CreateObject();
        if (item == NULL) {
            continue;
        }

        ty_cJSON_AddStringToObject(item, "id", alarm->alarm_id);
        ty_cJSON_AddNumberToObject(item, "enabled", alarm->cfg.enabled ? 1 : 0);
        ty_cJSON_AddNumberToObject(item, "repeat_type", (INT_T)alarm->cfg.repeat_type);
        ty_cJSON_AddNumberToObject(item, "hour", (INT_T)alarm->cfg.hour);
        ty_cJSON_AddNumberToObject(item, "minute", (INT_T)alarm->cfg.minute);
        ty_cJSON_AddNumberToObject(item, "weekday_mask", (INT_T)alarm->cfg.weekday_mask);
        ty_cJSON_AddNumberToObject(item, "month_day", (INT_T)alarm->cfg.month_day);
        ty_cJSON_AddNumberToObject(item, "start_time", (double)alarm->cfg.start_time);
        ty_cJSON_AddStringToObject(item, "message", alarm->cfg.message);
        ty_cJSON_AddItemToArray(arr, item);
    }

    ty_cJSON_AddItemToObject(root, "alarms", arr);
    json_str = ty_cJSON_PrintUnformatted(root);
    ty_cJSON_Delete(root);
    if (json_str == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    rt = wd_common_write(WUKONG_TM_ALARM_KV_KEY, (CONST BYTE_T *)json_str, strlen(json_str));
    ty_cJSON_FreeBuffer(json_str);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("alarm -> store save failed, rt=%d", rt);
    }
    else {          //notify UI to sync alarm info!
        __alarm_store_notify();
    }
    return rt;
}

/**
 * @brief Load persisted alarms from KV into the in-memory table.
 *
 * Expired once-alarms are loaded into memory for query purposes but their
 * cron job is not created so they will not fire.
 *
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __alarm_store_load(VOID)
{
    BYTE_T *data = NULL;
    UINT_T data_len = 0;
    ty_cJSON *root = NULL;
    ty_cJSON *arr = NULL;
    ty_cJSON *node = NULL;
    INT_T count = 0;
    INT_T i = 0;
    INT_T loaded = 0;
    BOOL_T need_save = FALSE;
    OPERATE_RET rt = OPRT_OK;
    TIME_T now = tal_time_get_posix();

    rt = wd_common_read(WUKONG_TM_ALARM_KV_KEY, &data, &data_len);
    if (rt != OPRT_OK) {
        TAL_PR_DEBUG("alarm -> no stored data, starting fresh");
        return OPRT_OK;
    }

    root = ty_cJSON_Parse((CONST CHAR_T *)data);
    wd_common_free_data(data);
    if (root == NULL) {
        TAL_PR_WARN("alarm -> stored data parse failed");
        return OPRT_OK;
    }

    arr = ty_cJSON_GetObjectItem(root, "alarms");
    if (!ty_cJSON_IsArray(arr)) {
        ty_cJSON_Delete(root);
        return OPRT_OK;
    }

    count = ty_cJSON_GetArraySize(arr);
    for (i = 0; i < count; i++) {
        ty_cJSON *item = ty_cJSON_GetArrayItem(arr, i);
        INT_T slot = 0;
        WUKONG_TM_ALARM_ITEM_T *alarm = NULL;
        BOOL_T expired = FALSE;

        if (!ty_cJSON_IsObject(item)) {
            continue;
        }

        node = ty_cJSON_GetObjectItem(item, "id");
        if (!ty_cJSON_IsString(node) || node->valuestring == NULL ||
            node->valuestring[0] == '\0') {
            continue;
        }

        slot = __alarm_alloc_index();
        if (slot < 0) {
            TAL_PR_WARN("alarm -> no free slot for stored alarm %d", i);
            break;
        }

        alarm = &s_alarm_ctx.alarms[slot];
        memset(alarm, 0, sizeof(*alarm));
        strncpy(alarm->alarm_id, node->valuestring, sizeof(alarm->alarm_id) - 1);

        node = ty_cJSON_GetObjectItem(item, "enabled");
        alarm->cfg.enabled = (node != NULL && ty_cJSON_IsNumber(node))
                             ? (node->valueint != 0) : TRUE;

        node = ty_cJSON_GetObjectItem(item, "repeat_type");
        alarm->cfg.repeat_type = (node != NULL && ty_cJSON_IsNumber(node))
                                 ? (WUKONG_TM_ALARM_REPEAT_TYPE_E)node->valueint
                                 : WUKONG_TM_ALARM_REPEAT_ONCE;

        node = ty_cJSON_GetObjectItem(item, "hour");
        alarm->cfg.hour = (node != NULL && ty_cJSON_IsNumber(node))
                          ? (UINT_T)node->valueint : 0;

        node = ty_cJSON_GetObjectItem(item, "minute");
        alarm->cfg.minute = (node != NULL && ty_cJSON_IsNumber(node))
                            ? (UINT_T)node->valueint : 0;

        node = ty_cJSON_GetObjectItem(item, "weekday_mask");
        alarm->cfg.weekday_mask = (node != NULL && ty_cJSON_IsNumber(node))
                                  ? (UINT_T)node->valueint : 0;

        node = ty_cJSON_GetObjectItem(item, "month_day");
        alarm->cfg.month_day = (node != NULL && ty_cJSON_IsNumber(node))
                               ? (UINT_T)node->valueint : 0;

        node = ty_cJSON_GetObjectItem(item, "start_time");
        alarm->cfg.start_time = (node != NULL && ty_cJSON_IsNumber(node))
                                ? (TIME_T)node->valuedouble : 0;

        node = ty_cJSON_GetObjectItem(item, "message");
        if (node != NULL && ty_cJSON_IsString(node) && node->valuestring != NULL) {
            strncpy(alarm->cfg.message, node->valuestring,
                    sizeof(alarm->cfg.message) - 1);
        }

        if (__alarm_validate_cfg(&alarm->cfg) != OPRT_OK) {
            TAL_PR_WARN("alarm -> invalid stored alarm %s, skipped", alarm->alarm_id);
            memset(alarm, 0, sizeof(*alarm));
            continue;
        }

        alarm->used = TRUE;
        loaded++;

        if (!alarm->cfg.enabled) {
            TAL_PR_DEBUG("alarm -> loaded disabled alarm %s (no cron)", alarm->alarm_id);
            continue;
        }

        expired = (now > 0 &&
                   alarm->cfg.repeat_type == WUKONG_TM_ALARM_REPEAT_ONCE &&
                   alarm->cfg.start_time > 0 &&
                   alarm->cfg.start_time < now);
        if (expired) {
            alarm->cfg.enabled = FALSE;
            need_save = TRUE;
            TAL_PR_DEBUG("alarm -> loaded expired once-alarm %s (disabled, no cron)", alarm->alarm_id);
            continue;
        }

        rt = __alarm_sync_cron_job(alarm);
        if (rt != OPRT_OK) {
            TAL_PR_WARN("alarm -> cron sync failed for %s, rt=%d", alarm->alarm_id, rt);
        }
    }

    ty_cJSON_Delete(root);
    TAL_PR_NOTICE("alarm -> loaded %d alarms from KV", loaded);
    if (need_save) {
        (VOID)__alarm_store_save();
    } else {
        __alarm_store_notify();
    }
    return OPRT_OK;
}

/**
 * @brief Emit one alarm TLV event via wukong_ai_event_notify.
 *
 * @param[in] alarm_id  Target alarm id string.
 * @param[in] ring_seq  Current ringing round (1 = first, >1 = snooze replay).
 * @param[in] opr       Operation code.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __alarm_emit_event(CONST CHAR_T *alarm_id, UINT_T ring_seq, WUKONG_TM_TIMER_OPR_E opr)
{
    UINT_T offset = 0;
    UINT8_T *msg = NULL;
    UINT_T len = 0;
    UINT_T id_len = 0;
    UINT_T seq_value = ring_seq;

    TUYA_CHECK_NULL_RETURN(alarm_id, OPRT_INVALID_PARM);

    id_len = strlen(alarm_id) + 1;

    len += (WUKONG_TM_TLV_TL_LEN + 1);
    len += (WUKONG_TM_TLV_TL_LEN + id_len);
    len += (WUKONG_TM_TLV_TL_LEN + sizeof(seq_value));

    msg = tal_malloc(len);
    if (msg == NULL) {
        TAL_PR_ERR("%s: malloc failed", __func__);
        return OPRT_MALLOC_FAILED;
    }

    __tm_tlv_pack(msg, WUKONG_TM_TAG_ALARM_OPR, 1, (CONST UINT8_T *)&opr, &offset);
    __tm_tlv_pack(msg, WUKONG_TM_TAG_ALARM_ID, id_len, (CONST UINT8_T *)alarm_id, &offset);
    __tm_tlv_pack(msg, WUKONG_TM_TAG_ALARM_RING_SEQ, sizeof(seq_value),
                  (CONST UINT8_T *)&seq_value, &offset);

    {
        INT_T idx = __alarm_find_index(alarm_id);
        if (idx >= 0) {
            CONST WUKONG_TM_ALARM_CFG_T *cfg = &s_alarm_ctx.alarms[idx].cfg;
            (void)cfg;
            TAL_PR_NOTICE("alarm -> emit event: opr=%s(%d) id=%s seq=%u "
                          "repeat=%s(%d) %02u:%02u enabled=%d msg=%s",
                          __tm_opr_name(opr), opr, alarm_id, ring_seq,
                          __tm_repeat_name(cfg->repeat_type), cfg->repeat_type,
                          cfg->hour, cfg->minute, cfg->enabled,
                          cfg->message[0] ? cfg->message : "(none)");
        } else {
            TAL_PR_NOTICE("alarm -> emit event: opr=%s(%d) id=%s seq=%u",
                          __tm_opr_name(opr), opr, alarm_id, ring_seq);
        }
    }
    wukong_ai_event_notify(WUKONG_AI_EVENT_CLOCK_MCP_ALARM, msg);
    tal_free(msg);
    return OPRT_OK;
}

/**
 * @brief Start one ringing round and arm the unanswered timeout job.
 *
 * @param[in,out] alarm Target alarm runtime slot.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __alarm_start_ringing(WUKONG_TM_ALARM_ITEM_T *alarm)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(alarm, OPRT_INVALID_PARM);

    __alarm_clear_runtime_state(alarm, TRUE);

    alarm->ring_seq++;
    alarm->is_ringing = TRUE;

    rt = __alarm_schedule_runtime_job(alarm, WUKONG_TM_ALARM_SNOOZE_TIMEOUT_METHOD,
                                      alarm->ring_seq, s_alarm_ring_duration_sec,
                                      alarm->ack_timeout_job_id, sizeof(alarm->ack_timeout_job_id));
    if (rt != OPRT_OK) {
        alarm->is_ringing = FALSE;
        alarm->ack_timeout_job_id[0] = '\0';
        return rt;
    }

    (VOID)__alarm_emit_event(alarm->alarm_id, alarm->ring_seq, WUKONG_TM_TIMER_OPR_START);
    return OPRT_OK;
}

/**
 * @brief Parse runtime snooze RPC params shared by timeout/fire handlers.
 *
 * @param[in]  params     RPC params object.
 * @param[out] alarm_id   Parsed alarm id string pointer.
 * @param[out] ring_seq   Parsed ring sequence.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __alarm_parse_runtime_params(CONST ty_cJSON *params, CONST CHAR_T **alarm_id, UINT_T *ring_seq)
{
    ty_cJSON *alarm_id_item = NULL;
    ty_cJSON *ring_seq_item = NULL;

    TUYA_CHECK_NULL_RETURN(params, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(alarm_id, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(ring_seq, OPRT_INVALID_PARM);

    alarm_id_item = ty_cJSON_GetObjectItem(params, "alarm_id");
    ring_seq_item = ty_cJSON_GetObjectItem(params, "ring_seq");
    if (!ty_cJSON_IsString(alarm_id_item) || alarm_id_item->valuestring == NULL ||
        !ty_cJSON_IsNumber(ring_seq_item)) {
        return OPRT_INVALID_PARM;
    }

    *alarm_id = alarm_id_item->valuestring;
    *ring_seq = (UINT_T)ring_seq_item->valueint;
    return OPRT_OK;
}

/**
 * @brief Local JSON-RPC handler that dispatches one alarm fire request.
 *
 * @param[in]  params  JSON-RPC params object.
 * @param[out] result  JSON-RPC result object.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __alarm_fire_rpc_handler(CONST ty_cJSON *params, ty_cJSON **result)
{
    ty_cJSON *alarm_id = NULL;
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(params, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(result, OPRT_INVALID_PARM);

    alarm_id = ty_cJSON_GetObjectItem(params, "alarm_id");
    if (!ty_cJSON_IsString(alarm_id) || alarm_id->valuestring == NULL) {
        return OPRT_INVALID_PARM;
    }

    rt = wukong_tm_alarm_fire(alarm_id->valuestring);
    if (rt != OPRT_OK) {
        return rt;
    }

    *result = ty_cJSON_CreateObject();
    if (*result == NULL) {
        return OPRT_MALLOC_FAILED;
    }
    ty_cJSON_AddStringToObject(*result, "status", "ok");
    return OPRT_OK;
}

/**
 * @brief Local JSON-RPC handler for the 30-second unanswered alarm window.
 *
 * @param[in]  params  JSON-RPC params object.
 * @param[out] result  JSON-RPC result object.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __alarm_snooze_timeout_rpc_handler(CONST ty_cJSON *params, ty_cJSON **result)
{
    CONST CHAR_T *alarm_id = NULL;
    UINT_T ring_seq = 0;
    INT_T index = -1;

    TUYA_CHECK_NULL_RETURN(result, OPRT_INVALID_PARM);

    if (__alarm_parse_runtime_params(params, &alarm_id, &ring_seq) != OPRT_OK) {
        return OPRT_INVALID_PARM;
    }

    index = __alarm_find_index(alarm_id);
    if (index >= 0 && s_alarm_ctx.alarms[index].used &&
        s_alarm_ctx.alarms[index].is_ringing &&
        s_alarm_ctx.alarms[index].ring_seq == ring_seq) {
        OPERATE_RET snooze_rt = OPRT_OK;

        s_alarm_ctx.alarms[index].ack_timeout_job_id[0] = '\0';
        s_alarm_ctx.alarms[index].is_ringing = FALSE;
        if (s_alarm_snooze_enabled &&
            (s_alarm_max_snooze_count == 0 || ring_seq <= s_alarm_max_snooze_count)) {
            snooze_rt = __alarm_schedule_runtime_job(&s_alarm_ctx.alarms[index],
                                                     WUKONG_TM_ALARM_SNOOZE_FIRE_METHOD,
                                                     ring_seq, s_alarm_snooze_delay_sec,
                                                     s_alarm_ctx.alarms[index].snooze_job_id,
                                                     sizeof(s_alarm_ctx.alarms[index].snooze_job_id));
            if (snooze_rt != OPRT_OK) {
                s_alarm_ctx.alarms[index].is_ringing = TRUE;
                TAL_PR_ERR("alarm -> snooze cron schedule failed, rt=%d", snooze_rt);
            } else {
                (VOID)__alarm_emit_event(alarm_id, ring_seq, WUKONG_TM_TIMER_OPR_PAUSE);
            }
        } else {
            (VOID)__alarm_emit_event(alarm_id, ring_seq, WUKONG_TM_TIMER_OPR_FINISH);
            s_alarm_ctx.alarms[index].ring_seq = 0;
            if (s_alarm_ctx.alarms[index].cfg.repeat_type == WUKONG_TM_ALARM_REPEAT_ONCE) {
                s_alarm_ctx.alarms[index].cfg.enabled = FALSE;
                (VOID)__alarm_store_save();
            }
        }
    }

    *result = ty_cJSON_CreateObject();
    if (*result == NULL) {
        return OPRT_MALLOC_FAILED;
    }
    ty_cJSON_AddStringToObject(*result, "status", "ok");
    return OPRT_OK;
}

/**
 * @brief Local JSON-RPC handler that replays one snoozed alarm round.
 *
 * @param[in]  params  JSON-RPC params object.
 * @param[out] result  JSON-RPC result object.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __alarm_snooze_fire_rpc_handler(CONST ty_cJSON *params, ty_cJSON **result)
{
    CONST CHAR_T *alarm_id = NULL;
    UINT_T ring_seq = 0;
    INT_T index = -1;
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(result, OPRT_INVALID_PARM);

    if (__alarm_parse_runtime_params(params, &alarm_id, &ring_seq) != OPRT_OK) {
        return OPRT_INVALID_PARM;
    }

    index = __alarm_find_index(alarm_id);
    if (index >= 0 && s_alarm_ctx.alarms[index].used &&
        s_alarm_ctx.alarms[index].ring_seq == ring_seq) {
        s_alarm_ctx.alarms[index].snooze_job_id[0] = '\0';
        if (!s_alarm_snooze_enabled) {
            *result = ty_cJSON_CreateObject();
            if (*result == NULL) {
                return OPRT_MALLOC_FAILED;
            }
            ty_cJSON_AddStringToObject(*result, "status", "ok");
            return OPRT_OK;
        }
        rt = __alarm_start_ringing(&s_alarm_ctx.alarms[index]);
        if (rt != OPRT_OK) {
            return rt;
        }
    }

    *result = ty_cJSON_CreateObject();
    if (*result == NULL) {
        return OPRT_MALLOC_FAILED;
    }
    ty_cJSON_AddStringToObject(*result, "status", "ok");
    return OPRT_OK;
}

/**
 * @brief Initialize the alarm feature under time-manage.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_alarm_init(VOID)
{
    OPERATE_RET rt = OPRT_OK;

    if (s_alarm_ctx.initialized) {
        return OPRT_OK;
    }

    memset(&s_alarm_ctx, 0, sizeof(s_alarm_ctx));
    s_alarm_ctx.initialized = TRUE;
    s_alarm_ctx.next_alarm_seq = 1;

    rt = wukong_cron_method_register(WUKONG_TM_ALARM_FIRE_METHOD, __alarm_fire_rpc_handler);
    if (rt != OPRT_OK) {
        memset(&s_alarm_ctx, 0, sizeof(s_alarm_ctx));
        return rt;
    }
    rt = wukong_cron_method_register(WUKONG_TM_ALARM_SNOOZE_TIMEOUT_METHOD,
                                     __alarm_snooze_timeout_rpc_handler);
    if (rt != OPRT_OK) {
        (VOID)wukong_cron_method_unregister(WUKONG_TM_ALARM_FIRE_METHOD);
        memset(&s_alarm_ctx, 0, sizeof(s_alarm_ctx));
        return rt;
    }
    rt = wukong_cron_method_register(WUKONG_TM_ALARM_SNOOZE_FIRE_METHOD,
                                     __alarm_snooze_fire_rpc_handler);
    if (rt != OPRT_OK) {
        (VOID)wukong_cron_method_unregister(WUKONG_TM_ALARM_SNOOZE_TIMEOUT_METHOD);
        (VOID)wukong_cron_method_unregister(WUKONG_TM_ALARM_FIRE_METHOD);
        memset(&s_alarm_ctx, 0, sizeof(s_alarm_ctx));
        return rt;
    }

    (VOID)__alarm_store_load();
    return OPRT_OK;
}

/**
 * @brief Deinitialize the alarm feature under time-manage.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_alarm_deinit(VOID)
{
    INT_T index = 0;

    for (index = 0; index < WUKONG_TM_ALARM_MAX_COUNT; index++) {
        if (s_alarm_ctx.alarms[index].used) {
            __alarm_clear_runtime_state(&s_alarm_ctx.alarms[index], TRUE);
            if (s_alarm_ctx.alarms[index].cfg.cron_job_id[0] != '\0') {
                (VOID)wukong_cron_job_remove(s_alarm_ctx.alarms[index].cfg.cron_job_id);
            }
        }
    }
    (VOID)wukong_cron_method_unregister(WUKONG_TM_ALARM_FIRE_METHOD);
    (VOID)wukong_cron_method_unregister(WUKONG_TM_ALARM_SNOOZE_TIMEOUT_METHOD);
    (VOID)wukong_cron_method_unregister(WUKONG_TM_ALARM_SNOOZE_FIRE_METHOD);
    memset(&s_alarm_ctx, 0, sizeof(s_alarm_ctx));
    return OPRT_OK;
}

/**
 * @brief Enable or disable alarm snooze for unanswered ringing windows.
 *
 * @param[in] enable TRUE to allow snooze, FALSE to disable.
 * @return OPRT_OK on success, OPRT_COM_ERROR when the alarm module is not initialized.
 */
OPERATE_RET wukong_tm_alarm_snooze_enable_set(BOOL_T enable)
{
    INT_T index = 0;

    if (!s_alarm_ctx.initialized) {
        return OPRT_COM_ERROR;
    }

    s_alarm_snooze_enabled = enable;

    if (!enable) {
        for (index = 0; index < WUKONG_TM_ALARM_MAX_COUNT; index++) {
            if (!s_alarm_ctx.alarms[index].used) {
                continue;
            }
            if (s_alarm_ctx.alarms[index].snooze_job_id[0] != '\0') {
                (VOID)__alarm_remove_job_if_needed(s_alarm_ctx.alarms[index].snooze_job_id);
            }
        }
    }

    return OPRT_OK;
}

/**
 * @brief Set the alarm ring duration (unanswered window before snooze).
 *
 * Takes effect on the next ring cycle; already-scheduled timeout jobs
 * are not retroactively modified. Can be called before init.
 *
 * @param[in] seconds Ring duration in seconds, must be > 0.
 * @return OPRT_OK on success, OPRT_INVALID_PARM when seconds is 0.
 */
OPERATE_RET wukong_tm_alarm_ring_duration_set(UINT_T seconds)
{
    if (seconds == 0) {
        return OPRT_INVALID_PARM;
    }

    s_alarm_ring_duration_sec = seconds;
    return OPRT_OK;
}

/**
 * @brief Get the current alarm ring duration in seconds.
 *
 * @return Current ring duration in seconds.
 */
UINT_T wukong_tm_alarm_ring_duration_get(VOID)
{
    return s_alarm_ring_duration_sec;
}

/**
 * @brief Set the snooze delay (wait time before replaying an unanswered alarm).
 *
 * Takes effect on the next snooze cycle; already-scheduled snooze jobs
 * are not retroactively modified. Can be called before init.
 *
 * @param[in] seconds Snooze delay in seconds, must be > 0.
 * @return OPRT_OK on success, OPRT_INVALID_PARM when seconds is 0.
 */
OPERATE_RET wukong_tm_alarm_snooze_delay_set(UINT_T seconds)
{
    if (seconds == 0) {
        return OPRT_INVALID_PARM;
    }

    s_alarm_snooze_delay_sec = seconds;
    return OPRT_OK;
}

/**
 * @brief Get the current snooze delay in seconds.
 *
 * @return Current snooze delay in seconds.
 */
UINT_T wukong_tm_alarm_snooze_delay_get(VOID)
{
    return s_alarm_snooze_delay_sec;
}

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
OPERATE_RET wukong_tm_alarm_snooze_max_count_set(UINT_T count)
{
    s_alarm_max_snooze_count = count;
    return OPRT_OK;
}

/**
 * @brief Get the current maximum snooze round count.
 *
 * @return Current max snooze count. 0 means unlimited.
 */
UINT_T wukong_tm_alarm_snooze_max_count_get(VOID)
{
    return s_alarm_max_snooze_count;
}

/**
 * @brief Enable or disable one alarm by id, syncing its cron job accordingly.
 *
 * @param[in] alarm_id Target alarm id.
 * @param[in] enable   TRUE to enable, FALSE to disable.
 * @return OPRT_OK on success, OPRT_NOT_FOUND when the id does not exist.
 * @note Enabling an expired once-alarm rolls its trigger to the next valid
 *       slot (today if still upcoming, otherwise +24 hours).
 */
OPERATE_RET wukong_tm_alarm_enable_set(CONST CHAR_T *alarm_id, BOOL_T enable)
{
    INT_T index = 0;
    WUKONG_TM_ALARM_ITEM_T *alarm = NULL;

    TUYA_CHECK_NULL_RETURN(alarm_id, OPRT_INVALID_PARM);
    if (!s_alarm_ctx.initialized) {
        return OPRT_COM_ERROR;
    }

    index = __alarm_find_index(alarm_id);
    if (index < 0) {
        return OPRT_NOT_FOUND;
    }

    alarm = &s_alarm_ctx.alarms[index];
    if (alarm->cfg.enabled == enable) {
        return OPRT_OK;
    }

    alarm->cfg.enabled = enable;

    if (!enable) {
        __alarm_clear_runtime_state(alarm, TRUE);
        alarm->ring_seq = 0;
        if (alarm->cfg.cron_job_id[0] != '\0') {
            (VOID)wukong_cron_job_remove(alarm->cfg.cron_job_id);
            alarm->cfg.cron_job_id[0] = '\0';
        }
    } else {
        if (alarm->cfg.repeat_type == WUKONG_TM_ALARM_REPEAT_ONCE) {
            if (__alarm_once_rollover_to_next_fire(&alarm->cfg) != OPRT_OK) {
                alarm->cfg.enabled = FALSE;
                return OPRT_COM_ERROR;
            }
            TAL_PR_NOTICE("alarm -> enable once-alarm %s, start_time=%d",
                          alarm->alarm_id, (INT_T)alarm->cfg.start_time);
        }
        if (__alarm_sync_cron_job(alarm) != OPRT_OK) {
            alarm->cfg.enabled = FALSE;
            return OPRT_COM_ERROR;
        }
    }

    (VOID)__alarm_store_save();
    return OPRT_OK;
}

/**
 * @brief Add one alarm through the time-manage service.
 *
 * @param[in] alarm_cfg  Alarm configuration to store.
 * @param[in] alarm_id   Caller-provided alarm identifier (must be unique).
 * @return OPRT_OK on success, OPRT_INVALID_PARM when id is NULL/empty,
 *         OPRT_COM_ERROR when the id already exists or no slot is available.
 */
OPERATE_RET wukong_tm_alarm_add(CONST WUKONG_TM_ALARM_CFG_T *alarm_cfg, CONST CHAR_T *alarm_id)
{
    INT_T index = 0;

    TUYA_CHECK_NULL_RETURN(alarm_cfg, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(alarm_id, OPRT_INVALID_PARM);
    if (alarm_id[0] == '\0') {
        return OPRT_INVALID_PARM;
    }
    if (!s_alarm_ctx.initialized) {
        return OPRT_COM_ERROR;
    }
    if (__alarm_find_index(alarm_id) >= 0) {
        return OPRT_COM_ERROR;
    }
    if (__alarm_validate_cfg(alarm_cfg) != OPRT_OK) {
        return OPRT_INVALID_PARM;
    }

    index = __alarm_alloc_index();
    if (index < 0) {
        return OPRT_COM_ERROR;
    }

    memset(&s_alarm_ctx.alarms[index], 0, sizeof(s_alarm_ctx.alarms[index]));
    s_alarm_ctx.alarms[index].used = TRUE;
    s_alarm_ctx.alarms[index].cfg = *alarm_cfg;
    s_alarm_ctx.alarms[index].cfg.cron_job_id[0] = '\0';
    strncpy(s_alarm_ctx.alarms[index].alarm_id, alarm_id,
            sizeof(s_alarm_ctx.alarms[index].alarm_id) - 1);
    s_alarm_ctx.alarms[index].alarm_id[sizeof(s_alarm_ctx.alarms[index].alarm_id) - 1] = '\0';
    if (alarm_cfg->enabled && alarm_cfg->repeat_type == WUKONG_TM_ALARM_REPEAT_ONCE) {
        if (__alarm_once_rollover_to_next_fire(&s_alarm_ctx.alarms[index].cfg) != OPRT_OK) {
            memset(&s_alarm_ctx.alarms[index], 0, sizeof(s_alarm_ctx.alarms[index]));
            return OPRT_COM_ERROR;
        }
    }
    if (__alarm_sync_cron_job(&s_alarm_ctx.alarms[index]) != OPRT_OK) {
        memset(&s_alarm_ctx.alarms[index], 0, sizeof(s_alarm_ctx.alarms[index]));
        return OPRT_COM_ERROR;
    }

    (VOID)__alarm_store_save();
    return OPRT_OK;
}

/**
 * @brief Update one existing alarm business object by id.
 *
 * @param[in] alarm_id    Target alarm id.
 * @param[in] alarm_cfg   Replacement alarm configuration.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_alarm_update(CONST CHAR_T *alarm_id, CONST WUKONG_TM_ALARM_CFG_T *alarm_cfg)
{
    INT_T index = 0;
    WUKONG_TM_ALARM_CFG_T backup_cfg = {0};
    CHAR_T prev_cron_job_id[WUKONG_TM_ALARM_CRON_JOB_ID_LEN + 1] = {0};

    TUYA_CHECK_NULL_RETURN(alarm_id, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(alarm_cfg, OPRT_INVALID_PARM);
    if (__alarm_validate_cfg(alarm_cfg) != OPRT_OK) {
        return OPRT_INVALID_PARM;
    }

    index = __alarm_find_index(alarm_id);
    if (index < 0) {
        return OPRT_NOT_FOUND;
    }

    backup_cfg = s_alarm_ctx.alarms[index].cfg;

    __alarm_clear_runtime_state(&s_alarm_ctx.alarms[index], TRUE);
    strncpy(prev_cron_job_id, s_alarm_ctx.alarms[index].cfg.cron_job_id, sizeof(prev_cron_job_id) - 1);
    s_alarm_ctx.alarms[index].cfg = *alarm_cfg;
    strncpy(s_alarm_ctx.alarms[index].cfg.cron_job_id, prev_cron_job_id,
            sizeof(s_alarm_ctx.alarms[index].cfg.cron_job_id) - 1);
    if (s_alarm_ctx.alarms[index].cfg.enabled &&
        s_alarm_ctx.alarms[index].cfg.repeat_type == WUKONG_TM_ALARM_REPEAT_ONCE) {
        if (__alarm_once_rollover_to_next_fire(&s_alarm_ctx.alarms[index].cfg) != OPRT_OK) {
            s_alarm_ctx.alarms[index].cfg = backup_cfg;
            (VOID)__alarm_sync_cron_job(&s_alarm_ctx.alarms[index]);
            return OPRT_COM_ERROR;
        }
    }
    if (__alarm_sync_cron_job(&s_alarm_ctx.alarms[index]) != OPRT_OK) {
        s_alarm_ctx.alarms[index].cfg = backup_cfg;
        (VOID)__alarm_sync_cron_job(&s_alarm_ctx.alarms[index]);
        return OPRT_COM_ERROR;
    }

    (VOID)__alarm_store_save();
    return OPRT_OK;
}

/**
 * @brief Read one alarm configuration snapshot by id.
 *
 * @param[in]  alarm_id    Target alarm id.
 * @param[out] alarm_cfg   Buffer used to receive the stored configuration.
 * @return OPRT_OK on success, OPRT_NOT_FOUND when the id does not exist.
 */
OPERATE_RET wukong_tm_alarm_get(CONST CHAR_T *alarm_id, WUKONG_TM_ALARM_CFG_T *alarm_cfg)
{
    INT_T index = 0;

    TUYA_CHECK_NULL_RETURN(alarm_id, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(alarm_cfg, OPRT_INVALID_PARM);
    if (!s_alarm_ctx.initialized) {
        return OPRT_COM_ERROR;
    }

    index = __alarm_find_index(alarm_id);
    if (index < 0) {
        return OPRT_NOT_FOUND;
    }

    *alarm_cfg = s_alarm_ctx.alarms[index].cfg;
    return OPRT_OK;
}

/**
 * @brief Remove one existing alarm business object by id.
 *
 * @param[in] alarm_id Target alarm id.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_alarm_remove(CONST CHAR_T *alarm_id)
{
    INT_T index = 0;

    TUYA_CHECK_NULL_RETURN(alarm_id, OPRT_INVALID_PARM);

    index = __alarm_find_index(alarm_id);
    if (index < 0) {
        return OPRT_NOT_FOUND;
    }

    __alarm_clear_runtime_state(&s_alarm_ctx.alarms[index], TRUE);
    if (s_alarm_ctx.alarms[index].cfg.cron_job_id[0] != '\0') {
        (VOID)wukong_cron_job_remove(s_alarm_ctx.alarms[index].cfg.cron_job_id);
    }
    memset(&s_alarm_ctx.alarms[index], 0, sizeof(s_alarm_ctx.alarms[index]));

    (VOID)__alarm_store_save();
    return OPRT_OK;
}

/**
 * @brief Remove all alarms matching the given time description.
 *
 * @param[in]  alarm_cfg       Time-description used for delete matching.
 * @param[out] removed_count   Optional number of removed alarms.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_alarm_remove_by_time(CONST WUKONG_TM_ALARM_CFG_T *alarm_cfg, UINT_T *removed_count)
{
    UINT_T removed = 0;
    INT_T index = 0;
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(alarm_cfg, OPRT_INVALID_PARM);
    if (!s_alarm_ctx.initialized) {
        return OPRT_COM_ERROR;
    }

    for (index = 0; index < WUKONG_TM_ALARM_MAX_COUNT; index++) {
        CHAR_T alarm_id[WUKONG_TM_ALARM_ID_LEN + 1] = {0};
        WUKONG_TM_ALARM_ITEM_T *alarm = &s_alarm_ctx.alarms[index];

        if (!alarm->used || !__alarm_semantic_match(&alarm->cfg, alarm_cfg)) {
            continue;
        }

        strncpy(alarm_id, alarm->alarm_id, sizeof(alarm_id) - 1);
        rt = wukong_tm_alarm_remove(alarm_id);
        if (rt != OPRT_OK) {
            return rt;
        }
        removed++;
    }

    if (removed_count != NULL) {
        *removed_count = removed;
    }

    return (removed > 0) ? OPRT_OK : OPRT_NOT_FOUND;
}

/**
 * @brief Compute whether an alarm's most recent scheduled occurrence has already passed.
 *
 * For once alarms: expired when start_time <= now.
 * For daily alarms: expired when today's trigger timestamp <= now.
 * For weekly alarms: expired when today is a scheduled weekday and today's trigger <= now.
 * For monthly alarms: expired when this month's trigger day has passed, or it is today and
 *                     the trigger time has already passed.
 *
 * @param[in] alarm      Alarm slot to evaluate.
 * @param[in] now        Current POSIX timestamp.
 * @param[in] local_now  Current local broken-down time.
 * @return 1 if expired, 0 otherwise.
 */
STATIC INT_T __alarm_is_expired(CONST WUKONG_TM_ALARM_ITEM_T *alarm, TIME_T now,
                                CONST POSIX_TM_S *local_now)
{
    POSIX_TM_S trigger_tm = {0};
    TIME_T trigger_ts = 0;

    if (alarm == NULL || local_now == NULL || now == 0) {
        return 0;
    }

    switch (alarm->cfg.repeat_type) {
    case WUKONG_TM_ALARM_REPEAT_ONCE:
        return (alarm->cfg.start_time > 0 && alarm->cfg.start_time <= now) ? 1 : 0;

    case WUKONG_TM_ALARM_REPEAT_DAILY:
        trigger_tm = *local_now;
        trigger_tm.tm_hour = (INT_T)alarm->cfg.hour;
        trigger_tm.tm_min  = (INT_T)alarm->cfg.minute;
        trigger_tm.tm_sec  = 0;
        trigger_ts = tal_time_mktime(&trigger_tm);
        return (trigger_ts > 0 && trigger_ts <= now) ? 1 : 0;

    case WUKONG_TM_ALARM_REPEAT_WEEKLY:
        if ((alarm->cfg.weekday_mask & (1U << (UINT_T)local_now->tm_wday)) == 0) {
            return 0;
        }
        trigger_tm = *local_now;
        trigger_tm.tm_hour = (INT_T)alarm->cfg.hour;
        trigger_tm.tm_min  = (INT_T)alarm->cfg.minute;
        trigger_tm.tm_sec  = 0;
        trigger_ts = tal_time_mktime(&trigger_tm);
        return (trigger_ts > 0 && trigger_ts <= now) ? 1 : 0;

    case WUKONG_TM_ALARM_REPEAT_MONTHLY:
        if ((INT_T)alarm->cfg.month_day < local_now->tm_mday) {
            return 1;
        }
        if ((INT_T)alarm->cfg.month_day == local_now->tm_mday) {
            trigger_tm = *local_now;
            trigger_tm.tm_hour = (INT_T)alarm->cfg.hour;
            trigger_tm.tm_min  = (INT_T)alarm->cfg.minute;
            trigger_tm.tm_sec  = 0;
            trigger_ts = tal_time_mktime(&trigger_tm);
            return (trigger_ts > 0 && trigger_ts <= now) ? 1 : 0;
        }
        return 0;

    default:
        return 0;
    }
}

/**
 * @brief Export the current alarm list as an unformatted JSON string.
 *
 * Each alarm entry contains: id, enabled, repeat_type, time (merged local datetime or HH:MM
 * string), expired (1 = most recent occurrence has passed), weekday_mask (weekly only, non-zero),
 * month_day (monthly only, non-zero), message, cron_job_id.
 * The root object also carries device_time (current local datetime string).
 *
 * @param[out] alarm_list_json Unformatted JSON string allocated by cJSON.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_alarm_list(CHAR_T **alarm_list_json)
{
    ty_cJSON *root = NULL;
    ty_cJSON *alarms = NULL;
    INT_T index = 0;
    TIME_T now = 0;
    POSIX_TM_S local_now = {0};
    BOOL_T has_local_time = FALSE;
    BOOL_T need_save = FALSE;

    TUYA_CHECK_NULL_RETURN(alarm_list_json, OPRT_INVALID_PARM);

    *alarm_list_json = NULL;

    now = tal_time_get_posix();
    if (now > 0 && tal_time_get_local_time_custom(now, &local_now) == OPRT_OK) {
        has_local_time = TRUE;
    }

    if (has_local_time) {
        for (index = 0; index < WUKONG_TM_ALARM_MAX_COUNT; index++) {
            if (__alarm_disable_expired_once_if_needed(&s_alarm_ctx.alarms[index], now, &local_now)) {
                need_save = TRUE;
            }
        }
        if (need_save) {
            (VOID)__alarm_store_save();
        }
    }

    root = ty_cJSON_CreateObject();
    alarms = ty_cJSON_CreateArray();
    if (root == NULL || alarms == NULL) {
        ty_cJSON_Delete(root);
        ty_cJSON_Delete(alarms);
        return OPRT_MALLOC_FAILED;
    }

    if (has_local_time) {
        CHAR_T device_time_str[32] = {0};
        /* tm_year is always a valid calendar year at runtime; suppress GCC's
         * conservative format-truncation analysis which assumes INT_MAX. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        (VOID)snprintf(device_time_str, sizeof(device_time_str),
                       "%04d-%02d-%02d %02d:%02d:%02d",
                       local_now.tm_year + 1900, local_now.tm_mon + 1, local_now.tm_mday,
                       local_now.tm_hour, local_now.tm_min, local_now.tm_sec);
#pragma GCC diagnostic pop
        ty_cJSON_AddStringToObject(root, "device_time", device_time_str);
    }

    for (index = 0; index < WUKONG_TM_ALARM_MAX_COUNT; index++) {
        ty_cJSON *item = NULL;
        WUKONG_TM_ALARM_ITEM_T *alarm = &s_alarm_ctx.alarms[index];
        CHAR_T time_str[32] = {0};
        INT_T expired = 0;

        if (!alarm->used) {
            continue;
        }

        item = ty_cJSON_CreateObject();
        if (item == NULL) {
            continue;
        }

        if (alarm->cfg.repeat_type == WUKONG_TM_ALARM_REPEAT_ONCE &&
            alarm->cfg.start_time > 0) {
            POSIX_TM_S alarm_local = {0};
            if (tal_time_get_local_time_custom(alarm->cfg.start_time, &alarm_local) == OPRT_OK) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
                (VOID)snprintf(time_str, sizeof(time_str), "%04d-%02d-%02d %02d:%02d",
                               alarm_local.tm_year + 1900, alarm_local.tm_mon + 1,
                               alarm_local.tm_mday,
                               (INT_T)alarm->cfg.hour, (INT_T)alarm->cfg.minute);
#pragma GCC diagnostic pop
            } else {
                (VOID)snprintf(time_str, sizeof(time_str), "%02d:%02d",
                               (INT_T)alarm->cfg.hour, (INT_T)alarm->cfg.minute);
            }
        } else {
            (VOID)snprintf(time_str, sizeof(time_str), "%02d:%02d",
                           (INT_T)alarm->cfg.hour, (INT_T)alarm->cfg.minute);
        }

        if (has_local_time) {
            expired = __alarm_is_expired(alarm, now, &local_now);
        }

        ty_cJSON_AddStringToObject(item, "id", alarm->alarm_id);
        ty_cJSON_AddNumberToObject(item, "enabled", alarm->cfg.enabled ? 1 : 0);
        ty_cJSON_AddNumberToObject(item, "repeat_type", (INT_T)alarm->cfg.repeat_type);
        ty_cJSON_AddStringToObject(item, "time", time_str);
        ty_cJSON_AddNumberToObject(item, "expired", expired);
        if (alarm->cfg.weekday_mask != 0) {
            ty_cJSON_AddNumberToObject(item, "weekday_mask", (INT_T)alarm->cfg.weekday_mask);
        }
        if (alarm->cfg.month_day != 0) {
            ty_cJSON_AddNumberToObject(item, "month_day", (INT_T)alarm->cfg.month_day);
        }
        ty_cJSON_AddStringToObject(item, "message", alarm->cfg.message);
        ty_cJSON_AddItemToArray(alarms, item);
    }

    ty_cJSON_AddItemToObject(root, "alarms", alarms);
    *alarm_list_json = ty_cJSON_PrintUnformatted(root);
    ty_cJSON_Delete(root);
    if (*alarm_list_json == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    return OPRT_OK;
}

/**
 * @brief Find one unique alarm by its time description.
 *
 * For ONCE alarms the absolute trigger timestamp (`start_time`) is part of the
 * match key, so two one-time alarms at the same wall-clock hour:minute on
 * different dates are correctly treated as distinct. Recurring alarms keep
 * the previous semantics (hour/minute + repeat shape).
 *
 * @param[in]  alarm_cfg      Time-description fields used for matching.
 * @param[out] alarm_id       Buffer used to receive the matched alarm id.
 * @param[in]  alarm_id_len   Size of @p alarm_id in bytes.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_alarm_find_by_time(CONST WUKONG_TM_ALARM_CFG_T *alarm_cfg,
                                         CHAR_T *alarm_id, UINT_T alarm_id_len)
{
    INT_T index = 0;
    INT_T matched_index = -1;

    TUYA_CHECK_NULL_RETURN(alarm_cfg, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(alarm_id, OPRT_INVALID_PARM);
    if (alarm_id_len == 0) {
        return OPRT_INVALID_PARM;
    }

    for (index = 0; index < WUKONG_TM_ALARM_MAX_COUNT; index++) {
        WUKONG_TM_ALARM_ITEM_T *alarm = &s_alarm_ctx.alarms[index];

        if (!alarm->used) {
            continue;
        }
        if (alarm->cfg.repeat_type != alarm_cfg->repeat_type ||
            alarm->cfg.hour != alarm_cfg->hour ||
            alarm->cfg.minute != alarm_cfg->minute ||
            alarm->cfg.weekday_mask != alarm_cfg->weekday_mask ||
            alarm->cfg.month_day != alarm_cfg->month_day) {
            continue;
        }
        /* For ONCE alarms hour:minute alone is not unique: two one-time
         * alarms at the same wall-clock minute on different dates must NOT
         * be reported as duplicates. Compare absolute timestamps when both
         * sides have one set; if either side is unset (legacy data),
         * fall back to the hour:minute match so callers without timestamp
         * context (e.g. cleanup helpers) keep working. */
        if (alarm_cfg->repeat_type == WUKONG_TM_ALARM_REPEAT_ONCE &&
            alarm->cfg.start_time > 0 && alarm_cfg->start_time > 0 &&
            alarm->cfg.start_time != alarm_cfg->start_time) {
            continue;
        }

        if (matched_index >= 0) {
            return OPRT_COM_ERROR;
        }
        matched_index = index;
    }

    if (matched_index < 0) {
        return OPRT_NOT_FOUND;
    }

    strncpy(alarm_id, s_alarm_ctx.alarms[matched_index].alarm_id, alarm_id_len - 1);
    alarm_id[alarm_id_len - 1] = '\0';
    return OPRT_OK;
}

/**
 * @brief Fire one alarm immediately by alarm id.
 *
 * @param[in] alarm_id Target alarm id.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_alarm_fire(CONST CHAR_T *alarm_id)
{
    INT_T index = 0;

    TUYA_CHECK_NULL_RETURN(alarm_id, OPRT_INVALID_PARM);

    index = __alarm_find_index(alarm_id);
    if (index < 0) {
        return OPRT_NOT_FOUND;
    }

    return __alarm_start_ringing(&s_alarm_ctx.alarms[index]);
}

/**
 * @brief Acknowledge one alarm and cancel its current runtime state.
 *
 * @param[in] alarm_id Target alarm id.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_alarm_ack(CONST CHAR_T *alarm_id)
{
    INT_T index = 0;

    TUYA_CHECK_NULL_RETURN(alarm_id, OPRT_INVALID_PARM);

    index = __alarm_find_index(alarm_id);
    if (index < 0) {
        return OPRT_NOT_FOUND;
    }

    return __alarm_ack_index(index);
}

/**
 * @brief Acknowledge the currently ringing or pending-snooze alarm.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_alarm_ack_active(VOID)
{
    INT_T index = 0;
    INT_T matched_index = -1;

    for (index = 0; index < WUKONG_TM_ALARM_MAX_COUNT; index++) {
        if (!s_alarm_ctx.alarms[index].used) {
            continue;
        }
        if (!s_alarm_ctx.alarms[index].is_ringing &&
            s_alarm_ctx.alarms[index].ack_timeout_job_id[0] == '\0' &&
            s_alarm_ctx.alarms[index].snooze_job_id[0] == '\0') {
            continue;
        }

        if (matched_index >= 0) {
            return OPRT_COM_ERROR;
        }
        matched_index = index;
    }

    if (matched_index < 0) {
        return OPRT_NOT_FOUND;
    }

    return __alarm_ack_index(matched_index);
}

/**
 * @brief Acknowledge one alarm slot by index.
 *
 * @param[in] index Alarm slot index.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __alarm_ack_index(INT_T index)
{
    WUKONG_TM_ALARM_ITEM_T *alarm = NULL;

    if (index < 0 || index >= WUKONG_TM_ALARM_MAX_COUNT) {
        return OPRT_INVALID_PARM;
    }

    alarm = &s_alarm_ctx.alarms[index];
    if (!alarm->used) {
        return OPRT_NOT_FOUND;
    }

    (VOID)__alarm_emit_event(alarm->alarm_id, alarm->ring_seq, WUKONG_TM_TIMER_OPR_STOP);
    __alarm_clear_runtime_state(alarm, TRUE);
    alarm->ring_seq = 0;

    if (alarm->cfg.repeat_type == WUKONG_TM_ALARM_REPEAT_ONCE) {
        alarm->cfg.enabled = FALSE;
        if (alarm->cfg.cron_job_id[0] != '\0') {
            (VOID)wukong_cron_job_remove(alarm->cfg.cron_job_id);
            alarm->cfg.cron_job_id[0] = '\0';
        }
        (VOID)__alarm_store_save();
    }

    return OPRT_OK;
}
