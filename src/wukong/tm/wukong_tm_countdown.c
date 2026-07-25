/**
 * @file wukong_tm_countdown.c
 * @brief Cron-backed single-countdown implementation for time-manage.
 */

#include "wukong_tm.h"

#include <stdio.h>
#include <string.h>

#include "tal_log.h"
#include "tal_memory.h"
#include "tal_time_service.h"
#include "wukong_ai_agent.h"
#include "wukong_cron.h"
#include "wukong_tm_internal.h"

/**
 * @brief Fixed method name used by cron tick callbacks.
 */
#define WUKONG_TM_COUNTDOWN_TICK_METHOD "tm.countdown.tick"
/**
 * @brief Fixed job id buffer length for the single countdown cron binding.
 */
#define WUKONG_TM_COUNTDOWN_JOB_ID_LEN  32

/**
 * @brief Singleton countdown runtime context.
 */
typedef struct {
    BOOL_T initialized;
    UINT_T next_handle;
    UINT_T handle;
    WUKONG_TM_COUNTDOWN_STATE_E state;
    TIME_T duration_sec;
    TIME_T target_ts;
    TIME_T paused_remaining_sec;
    CHAR_T cron_job_id[WUKONG_TM_COUNTDOWN_JOB_ID_LEN + 1];
} WUKONG_TM_COUNTDOWN_CTX_T;

STATIC WUKONG_TM_COUNTDOWN_CTX_T s_countdown_ctx;

/**
 * @brief Emit one countdown event payload.
 *
 * @param[in] handle         Countdown handle.
 * @param[in] opr            Countdown operation.
 * @param[in] remaining_sec  Remaining seconds represented by the event.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __countdown_emit_event(UINT_T handle, WUKONG_TM_TIMER_OPR_E opr, TIME_T remaining_sec)
{
    INT_T hours = 0;
    INT_T minutes = 0;
    INT_T seconds = 0;
    UINT_T offset = 0;
    UINT8_T *msg = NULL;
    UINT_T len = 0;
    UINT_T handle_value = handle;
    UINT_T remaining_value = 0;
    UINT_T elapsed_value = 0;
    TIME_T elapsed_sec = 0;
    BOOL_T pack_elapsed = FALSE;

    if (remaining_sec < 0) {
        remaining_sec = 0;
    }
    remaining_value = (UINT_T)remaining_sec;

    if (opr == WUKONG_TM_TIMER_OPR_PAUSE) {
        elapsed_sec = s_countdown_ctx.duration_sec - remaining_sec;
        if (elapsed_sec < 0) {
            elapsed_sec = 0;
        }
        elapsed_value = (UINT_T)elapsed_sec;
        pack_elapsed = TRUE;
    }

    hours = (INT_T)(remaining_sec / 3600);
    minutes = (INT_T)((remaining_sec % 3600) / 60);
    seconds = (INT_T)(remaining_sec % 60);

    len += (WUKONG_TM_TLV_TL_LEN + 1);
    len += (WUKONG_TM_TLV_TL_LEN + 1);
    len += (WUKONG_TM_TLV_TL_LEN + 1);
    len += (WUKONG_TM_TLV_TL_LEN + 1);
    len += (WUKONG_TM_TLV_TL_LEN + sizeof(handle_value));
    len += (WUKONG_TM_TLV_TL_LEN + sizeof(remaining_value));
    if (pack_elapsed) {
        len += (WUKONG_TM_TLV_TL_LEN + sizeof(elapsed_value));
    }

    msg = tal_malloc(len);
    if (msg == NULL) {
        TAL_PR_ERR("%s: malloc failed", __func__);
        return OPRT_MALLOC_FAILED;
    }

    __tm_tlv_pack(msg, WUKONG_TM_TAG_COUNTDOWN_OPR, 1, (CONST UINT8_T *)&opr, &offset);
    __tm_tlv_pack(msg, WUKONG_TM_TAG_COUNTDOWN_HOUR, 1, (CONST UINT8_T *)&hours, &offset);
    __tm_tlv_pack(msg, WUKONG_TM_TAG_COUNTDOWN_MINUTE, 1, (CONST UINT8_T *)&minutes, &offset);
    __tm_tlv_pack(msg, WUKONG_TM_TAG_COUNTDOWN_SECOND, 1, (CONST UINT8_T *)&seconds, &offset);
    __tm_tlv_pack(msg, WUKONG_TM_TAG_COUNTDOWN_HANDLE, sizeof(handle_value),
                  (CONST UINT8_T *)&handle_value, &offset);
    __tm_tlv_pack(msg, WUKONG_TM_TAG_COUNTDOWN_REMAINING_SEC, sizeof(remaining_value),
                  (CONST UINT8_T *)&remaining_value, &offset);
    if (pack_elapsed) {
        __tm_tlv_pack(msg, WUKONG_TM_TAG_COUNTDOWN_ELAPSED_SEC, sizeof(elapsed_value),
                      (CONST UINT8_T *)&elapsed_value, &offset);
    }

    if (opr == WUKONG_TM_TIMER_OPR_TICK) {
        TAL_PR_DEBUG("countdown -> emit event: opr=TICK remaining=%02d:%02d:%02d(%us)",
                     hours, minutes, seconds, remaining_value);
    } else {
        TAL_PR_NOTICE("countdown -> emit event: opr=%s(%d) handle=%u "
                      "remaining=%02d:%02d:%02d(%us) duration=%llds",
                      __tm_opr_name(opr), opr, handle,
                      hours, minutes, seconds, remaining_value,
                      (long long)s_countdown_ctx.duration_sec);
    }
    wukong_ai_event_notify(WUKONG_AI_EVENT_CLOCK_MCP_COUNTDOWN_TIMER, msg);
    tal_free(msg);
    return OPRT_OK;
}

/**
 * @brief Return whether the countdown currently exists.
 */
STATIC BOOL_T __countdown_exists(VOID)
{
    return (s_countdown_ctx.state != WUKONG_TM_COUNTDOWN_STATE_IDLE) ? TRUE : FALSE;
}

/**
 * @brief Calculate remaining seconds for the current countdown state.
 */
STATIC TIME_T __countdown_remaining(VOID)
{
    TIME_T now = tal_time_get_posix();

    if (s_countdown_ctx.state == WUKONG_TM_COUNTDOWN_STATE_PAUSED) {
        return (s_countdown_ctx.paused_remaining_sec > 0) ? s_countdown_ctx.paused_remaining_sec : 0;
    }
    if (s_countdown_ctx.state != WUKONG_TM_COUNTDOWN_STATE_RUNNING) {
        return 0;
    }
    if (s_countdown_ctx.target_ts <= now) {
        return 0;
    }
    return s_countdown_ctx.target_ts - now;
}

/**
 * @brief Pick the next cadence bucket from remaining seconds.
 */
STATIC TIME_T __countdown_step_from_remaining(TIME_T remaining_sec)
{
    if (remaining_sec > 3600) {
        return 3600;
    }
    if (remaining_sec > 600) {
        return 60;
    }
    if (remaining_sec > 10) {
        return 10;
    }
    return 1;
}

/**
 * @brief Convert one absolute local timestamp to a one-shot cron expression.
 */
STATIC OPERATE_RET __countdown_build_cron_expr(TIME_T target_ts, CHAR_T *cron_expr, UINT_T cron_expr_len)
{
    POSIX_TM_S tm_info;
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(cron_expr, OPRT_INVALID_PARM);
    if (cron_expr_len == 0) {
        return OPRT_INVALID_PARM;
    }
    memset(&tm_info, 0, sizeof(tm_info));
    rt = tal_time_get_local_time_custom(target_ts, &tm_info);
    if (rt != OPRT_OK) {
        return rt;
    }

    (VOID)snprintf(cron_expr, cron_expr_len, "%d %d %d %d %d *",
                   tm_info.tm_sec, tm_info.tm_min, tm_info.tm_hour,
                   tm_info.tm_mday, tm_info.tm_mon + 1);
    return OPRT_OK;
}

/**
 * @brief Remove the currently bound cron job when it exists.
 */
STATIC VOID __countdown_remove_bound_cron(VOID)
{
    if (s_countdown_ctx.cron_job_id[0] == '\0') {
        return;
    }

    (VOID)wukong_cron_job_remove(s_countdown_ctx.cron_job_id);
    s_countdown_ctx.cron_job_id[0] = '\0';
}

/**
 * @brief Schedule the next countdown progress tick with a one-shot cron job.
 */
STATIC OPERATE_RET __countdown_schedule_next_tick(TIME_T remaining_sec)
{
    CHAR_T cron_expr[32] = {0};
    CHAR_T job_json[384] = {0};
    CHAR_T job_id[WUKONG_TM_COUNTDOWN_JOB_ID_LEN + 1] = {0};
    TIME_T step = 0;
    TIME_T next_fire_ts = 0;
    OPERATE_RET rt = OPRT_OK;

    if (remaining_sec <= 0) {
        return OPRT_OK;
    }

    step = __countdown_step_from_remaining(remaining_sec);
    if (step > remaining_sec) {
        step = remaining_sec;
    }
    next_fire_ts = tal_time_get_posix() + step;
    rt = __countdown_build_cron_expr(next_fire_ts, cron_expr, sizeof(cron_expr));
    if (rt != OPRT_OK) {
        return rt;
    }

    __countdown_remove_bound_cron();
    (VOID)snprintf(job_json, sizeof(job_json),
                   "{\"name\":\"tm-countdown\",\"enabled\":1,\"once\":1,"
                   "\"cron\":\"%s\","
                   "\"request\":{\"jsonrpc\":\"2.0\",\"id\":\"tm-countdown-%u\","
                   "\"method\":\"%s\",\"params\":{\"handle\":%u}}}",
                   cron_expr, s_countdown_ctx.handle,
                   WUKONG_TM_COUNTDOWN_TICK_METHOD, s_countdown_ctx.handle);
    rt = wukong_cron_job_add(job_json, job_id, sizeof(job_id));
    if (rt != OPRT_OK) {
        return rt;
    }

    strncpy(s_countdown_ctx.cron_job_id, job_id, sizeof(s_countdown_ctx.cron_job_id) - 1);
    s_countdown_ctx.cron_job_id[sizeof(s_countdown_ctx.cron_job_id) - 1] = '\0';
    return OPRT_OK;
}

/**
 * @brief Reset the single countdown context to its idle state.
 */
STATIC VOID __countdown_reset(VOID)
{
    UINT_T next_handle = s_countdown_ctx.next_handle;
    BOOL_T initialized = s_countdown_ctx.initialized;

    memset(&s_countdown_ctx, 0, sizeof(s_countdown_ctx));
    s_countdown_ctx.initialized = initialized;
    s_countdown_ctx.next_handle = (next_handle == 0) ? 1 : next_handle;
}

/**
 * @brief Cron RPC handler used to emit progress and re-arm the next tick.
 */
STATIC OPERATE_RET __countdown_tick_handler(CONST ty_cJSON *params, ty_cJSON **result)
{
    ty_cJSON *handle_node = NULL;
    TIME_T remaining_sec = 0;
    UINT_T handle = 0;
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(params, OPRT_INVALID_PARM);
    handle_node = ty_cJSON_GetObjectItem(params, "handle");
    if (!ty_cJSON_IsNumber(handle_node)) {
        return OPRT_INVALID_PARM;
    }

    handle = (UINT_T)handle_node->valueint;
    if (!__countdown_exists() ||
        handle != s_countdown_ctx.handle ||
        s_countdown_ctx.state != WUKONG_TM_COUNTDOWN_STATE_RUNNING) {
        return OPRT_NOT_FOUND;
    }

    /* The once cron job has already been consumed at this point. */
    s_countdown_ctx.cron_job_id[0] = '\0';
    remaining_sec = __countdown_remaining();
    if (result != NULL) {
        *result = ty_cJSON_CreateObject();
        if (*result != NULL) {
            ty_cJSON_AddNumberToObject(*result, "handle", (INT_T)handle);
            ty_cJSON_AddNumberToObject(*result, "remaining_sec", (INT_T)remaining_sec);
        }
    }
    if (remaining_sec <= 0) {
        (VOID)__countdown_emit_event(s_countdown_ctx.handle,
                                             WUKONG_TM_TIMER_OPR_FINISH, 0);
        __countdown_reset();
        return OPRT_OK;
    }

    (VOID)__countdown_emit_event(s_countdown_ctx.handle,
                                         WUKONG_TM_TIMER_OPR_TICK, remaining_sec);
    rt = __countdown_schedule_next_tick(remaining_sec);
    if (rt != OPRT_OK) {
        __countdown_reset();
    }
    return rt;
}

OPERATE_RET wukong_tm_countdown_init(VOID)
{
    if (s_countdown_ctx.initialized) {
        return OPRT_OK;
    }

    memset(&s_countdown_ctx, 0, sizeof(s_countdown_ctx));
    s_countdown_ctx.initialized = TRUE;
    s_countdown_ctx.next_handle = 1;
    return wukong_cron_method_register(WUKONG_TM_COUNTDOWN_TICK_METHOD, __countdown_tick_handler);
}

OPERATE_RET wukong_tm_countdown_deinit(VOID)
{
    if (!s_countdown_ctx.initialized) {
        return OPRT_OK;
    }

    __countdown_remove_bound_cron();
    (VOID)wukong_cron_method_unregister(WUKONG_TM_COUNTDOWN_TICK_METHOD);
    memset(&s_countdown_ctx, 0, sizeof(s_countdown_ctx));
    return OPRT_OK;
}

OPERATE_RET wukong_tm_countdown_create(INT_T hours, INT_T minutes, INT_T seconds)
{
    TIME_T total_sec = 0;
    OPERATE_RET rt = OPRT_OK;

    if (!s_countdown_ctx.initialized) {
        return OPRT_COM_ERROR;
    }
    if (__countdown_exists()) {
        return OPRT_COM_ERROR;
    }
    if (hours < 0 || minutes < 0 || seconds < 0) {
        return OPRT_INVALID_PARM;
    }

    total_sec = (TIME_T)hours * 3600 + (TIME_T)minutes * 60 + (TIME_T)seconds;
    if (total_sec <= 0) {
        return OPRT_INVALID_PARM;
    }

    s_countdown_ctx.handle = s_countdown_ctx.next_handle++;
    s_countdown_ctx.state = WUKONG_TM_COUNTDOWN_STATE_RUNNING;
    s_countdown_ctx.duration_sec = total_sec;
    s_countdown_ctx.target_ts = tal_time_get_posix() + total_sec;
    s_countdown_ctx.paused_remaining_sec = total_sec;

    rt = __countdown_emit_event(s_countdown_ctx.handle,
                                        WUKONG_TM_TIMER_OPR_START, total_sec);
    if (rt != OPRT_OK) {
        __countdown_reset();
        return rt;
    }

    rt = __countdown_schedule_next_tick(total_sec);
    if (rt != OPRT_OK) {
        __countdown_reset();
        return rt;
    }

    return OPRT_OK;
}

/**
 * @brief Query the runtime snapshot of the active countdown timer.
 *
 * @param[out] snapshot Countdown snapshot buffer.
 * @return OPRT_OK on success, OPRT_NOT_FOUND when no active countdown exists.
 */
OPERATE_RET wukong_tm_countdown_query(WUKONG_TM_COUNTDOWN_SNAPSHOT_T *snapshot)
{
    TUYA_CHECK_NULL_RETURN(snapshot, OPRT_INVALID_PARM);

    if (!__countdown_exists()) {
        return OPRT_NOT_FOUND;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->active = TRUE;
    snapshot->state = s_countdown_ctx.state;
    snapshot->duration_sec = s_countdown_ctx.duration_sec;
    snapshot->remaining_sec = __countdown_remaining();
    snapshot->elapsed_sec = snapshot->duration_sec - snapshot->remaining_sec;
    if (snapshot->elapsed_sec < 0) {
        snapshot->elapsed_sec = 0;
    }
    return OPRT_OK;
}

OPERATE_RET wukong_tm_countdown_pause(VOID)
{
    if (!__countdown_exists()) {
        return OPRT_NOT_FOUND;
    }
    if (s_countdown_ctx.state != WUKONG_TM_COUNTDOWN_STATE_RUNNING) {
        return OPRT_COM_ERROR;
    }

    s_countdown_ctx.paused_remaining_sec = __countdown_remaining();
    __countdown_remove_bound_cron();
    s_countdown_ctx.state = WUKONG_TM_COUNTDOWN_STATE_PAUSED;
    return __countdown_emit_event(s_countdown_ctx.handle,
                                          WUKONG_TM_TIMER_OPR_PAUSE,
                                          s_countdown_ctx.paused_remaining_sec);
}

OPERATE_RET wukong_tm_countdown_resume(VOID)
{
    OPERATE_RET rt = OPRT_OK;

    if (!__countdown_exists()) {
        return OPRT_NOT_FOUND;
    }
    if (s_countdown_ctx.state != WUKONG_TM_COUNTDOWN_STATE_PAUSED) {
        return OPRT_COM_ERROR;
    }
    if (s_countdown_ctx.paused_remaining_sec <= 0) {
        rt = __countdown_emit_event(s_countdown_ctx.handle,
                                            WUKONG_TM_TIMER_OPR_FINISH, 0);
        __countdown_reset();
        return rt;
    }

    s_countdown_ctx.target_ts = tal_time_get_posix() + s_countdown_ctx.paused_remaining_sec;
    s_countdown_ctx.state = WUKONG_TM_COUNTDOWN_STATE_RUNNING;
    rt = __countdown_emit_event(s_countdown_ctx.handle,
                                        WUKONG_TM_TIMER_OPR_RESUME,
                                        s_countdown_ctx.paused_remaining_sec);
    if (rt != OPRT_OK) {
        __countdown_reset();
        return rt;
    }
    rt = __countdown_schedule_next_tick(s_countdown_ctx.paused_remaining_sec);
    if (rt != OPRT_OK) {
        __countdown_reset();
    }
    return rt;
}

OPERATE_RET wukong_tm_countdown_delete(VOID)
{
    TIME_T remaining_sec = 0;
    OPERATE_RET rt = OPRT_OK;

    if (!__countdown_exists()) {
        return OPRT_NOT_FOUND;
    }

    remaining_sec = __countdown_remaining();
    __countdown_remove_bound_cron();
    rt = __countdown_emit_event(s_countdown_ctx.handle,
                                        WUKONG_TM_TIMER_OPR_STOP, remaining_sec);
    __countdown_reset();
    return rt;
}
