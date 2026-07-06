/**
 * @file wukong_tm_pomodoro.c
 * @brief Cron-driven pomodoro timer with independent state management.
 *
 * Phase transitions are driven by once cron jobs.  When a phase ends the
 * RPC handler automatically advances to the next phase and schedules the
 * next cron job.  Pause removes the pending cron; resume re-creates it
 * with the saved remaining time.
 */

#include "wukong_tm.h"

#include <stdio.h>
#include <string.h>

#include "tal_log.h"
#include "tal_memory.h"
#include "tal_time_service.h"
#include "ty_cJSON.h"
#include "wukong_ai_agent.h"
#include "wukong_cron.h"
#include "wukong_tm_internal.h"

#define WUKONG_TM_POMODORO_PHASE_END_METHOD "tm.pomodoro.phase_end"
#define WUKONG_TM_POMODORO_JOB_ID_LEN      32

typedef struct {
    BOOL_T initialized;
    BOOL_T active;
    BOOL_T paused;
    UINT_T next_session_id;
    CHAR_T cron_job_id[WUKONG_TM_POMODORO_JOB_ID_LEN + 1];
    WUKONG_TM_POMODORO_STATE_T state;
} WUKONG_TM_POMODORO_CTX_T;

STATIC WUKONG_TM_POMODORO_CTX_T s_ctx = {
    .next_session_id = 1,
};

/* ---------------------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------------------- */

/**
 * @brief Ask the AI agent to speak pomodoro milestones (same path as reminder fire).
 *
 * Uses wukong_tm_reminder_action_notify() for the same cloud TTS path as
 * scheduled reminder fires (prefixed prompt plus user-facing text).
 *
 * @param[in] opr          Timer operation being emitted.
 * @param[in] ended_phase  Segment that just ended; used when opr is FINISH.
 * @return none
 */
STATIC VOID __pomodoro_reminder_style_notify(WUKONG_TM_TIMER_OPR_E opr,
                                             WUKONG_TM_POMODORO_PHASE_E ended_phase)
{
    OPERATE_RET rt = OPRT_OK;

    if (opr == WUKONG_TM_TIMER_OPR_FINISH) {
        switch (ended_phase) {
        case WUKONG_TM_POMODORO_PHASE_WORK:
            rt = wukong_tm_reminder_action_notify("番茄钟专注时间已结束，请休息。");
            break;
        case WUKONG_TM_POMODORO_PHASE_SHORT_BREAK:
            rt = wukong_tm_reminder_action_notify("番茄钟休息时间已结束，请继续专注。");
            break;
        case WUKONG_TM_POMODORO_PHASE_LONG_BREAK:
            rt = wukong_tm_reminder_action_notify("番茄钟长休息已结束，请继续专注。");
            break;
        default:
            return;
        }
    } else if (opr == WUKONG_TM_TIMER_OPR_STOP) {
        rt = wukong_tm_reminder_action_notify("番茄钟已停止。");
    } else {
        return;
    }

    if (rt != OPRT_OK) {
        TAL_PR_ERR("pomodoro: reminder-style notify failed %d", rt);
    }
}

/**
 * @brief Emit one pomodoro event payload to the application layer.
 *
 * TLV carries operation and current phase only; full cfg and timers use wukong_tm_pomodoro_query().
 * For WUKONG_TM_TIMER_OPR_FINISH, phase is the segment that just ended (before transition).
 *
 * @param[in] opr Pomodoro operation.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __pomodoro_emit_event(WUKONG_TM_TIMER_OPR_E opr)
{
    UINT_T offset = 0;
    UINT8_T *msg = NULL;
    UINT_T len = 0;
    WUKONG_TM_POMODORO_PHASE_E phase = s_ctx.state.phase;

    len += (WUKONG_TM_TLV_TL_LEN + 1);
    len += (WUKONG_TM_TLV_TL_LEN + 1);

    msg = tal_malloc(len);
    if (msg == NULL) {
        TAL_PR_ERR("%s: malloc failed", __func__);
        return OPRT_MALLOC_FAILED;
    }

    __tm_tlv_pack(msg, WUKONG_TM_TAG_POMODORO_OPR, 1, (CONST UINT8_T *)&opr, &offset);
    __tm_tlv_pack(msg, WUKONG_TM_TAG_POMODORO_PHASE, 1, (CONST UINT8_T *)&phase, &offset);

    TAL_PR_NOTICE("pomodoro -> emit event: opr=%s(%d) phase=%s(%d) "
                  "cycle=%u completed_work=%u paused=%d",
                  __tm_opr_name(opr), opr,
                  __tm_pomodoro_phase_name((INT_T)phase), (INT_T)phase,
                  s_ctx.state.current_cycle, s_ctx.state.completed_work_count,
                  s_ctx.paused);
    wukong_ai_event_notify(WUKONG_AI_EVENT_CLOCK_MCP_POMODORO_TIMER, msg);
    __pomodoro_reminder_style_notify(opr, phase);
    tal_free(msg);
    return OPRT_OK;
}

/**
 * @brief Return the total seconds for the given phase.
 *
 * @param[in] phase Current pomodoro phase.
 * @param[in] cfg   Active pomodoro configuration.
 * @return Phase duration in seconds.
 */
STATIC TIME_T __pomodoro_phase_total_sec(WUKONG_TM_POMODORO_PHASE_E phase,
                                         CONST WUKONG_TM_POMODORO_CFG_T *cfg)
{
    if (cfg == NULL) {
        return 0;
    }
    if (phase == WUKONG_TM_POMODORO_PHASE_SHORT_BREAK) {
        return (TIME_T)cfg->short_break_duration * 60;
    }
    if (phase == WUKONG_TM_POMODORO_PHASE_LONG_BREAK) {
        return (TIME_T)cfg->long_break_duration * 60;
    }
    return (TIME_T)cfg->work_duration * 60;
}

/**
 * @brief Build a once cron expression for the given absolute target time.
 *
 * @param[in]  target_ts      Absolute POSIX timestamp.
 * @param[out] cron_expr      Output buffer.
 * @param[in]  cron_expr_len  Buffer size.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __pomodoro_build_cron_expr(TIME_T target_ts, CHAR_T *cron_expr, UINT_T cron_expr_len)
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
 * @brief Remove the currently bound cron job if any.
 */
STATIC VOID __pomodoro_remove_cron(VOID)
{
    if (s_ctx.cron_job_id[0] == '\0') {
        return;
    }
    (VOID)wukong_cron_job_remove(s_ctx.cron_job_id);
    s_ctx.cron_job_id[0] = '\0';
}

/**
 * @brief Schedule a once cron job that fires after @p delay_sec seconds.
 *
 * @param[in] delay_sec Seconds from now to trigger.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __pomodoro_schedule_cron(TIME_T delay_sec)
{
    CHAR_T cron_expr[32] = {0};
    CHAR_T job_json[384] = {0};
    CHAR_T job_id[WUKONG_TM_POMODORO_JOB_ID_LEN + 1] = {0};
    OPERATE_RET rt = OPRT_OK;
    TIME_T fire_ts = 0;

    if (delay_sec <= 0) {
        return OPRT_INVALID_PARM;
    }

    fire_ts = tal_time_get_posix() + delay_sec;
    rt = __pomodoro_build_cron_expr(fire_ts, cron_expr, sizeof(cron_expr));
    if (rt != OPRT_OK) {
        return rt;
    }

    __pomodoro_remove_cron();
    (VOID)snprintf(job_json, sizeof(job_json),
                   "{\"name\":\"tm-pomodoro\",\"enabled\":1,\"once\":1,"
                   "\"cron\":\"%s\","
                   "\"request\":{\"jsonrpc\":\"2.0\",\"id\":\"tm-pomo-%u\","
                   "\"method\":\"%s\",\"params\":{\"session_id\":%u}}}",
                   cron_expr, s_ctx.state.session_id,
                   WUKONG_TM_POMODORO_PHASE_END_METHOD, s_ctx.state.session_id);

    rt = wukong_cron_job_add(job_json, job_id, sizeof(job_id));
    if (rt != OPRT_OK) {
        return rt;
    }

    strncpy(s_ctx.cron_job_id, job_id, sizeof(s_ctx.cron_job_id) - 1);
    s_ctx.cron_job_id[sizeof(s_ctx.cron_job_id) - 1] = '\0';
    return OPRT_OK;
}

/**
 * @brief Clear all pomodoro runtime state.
 */
STATIC VOID __pomodoro_reset(VOID)
{
    UINT_T next_session_id = s_ctx.next_session_id;
    BOOL_T initialized = s_ctx.initialized;

    __pomodoro_remove_cron();
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.initialized = initialized;
    s_ctx.next_session_id = (next_session_id == 0) ? 1 : next_session_id;
}

/**
 * @brief Refresh remaining_sec from the live clock for a running session.
 */
STATIC VOID __pomodoro_refresh_running(VOID)
{
    TIME_T now = 0;

    if (!s_ctx.active || s_ctx.paused) {
        return;
    }
    now = tal_time_get_posix();
    if (now >= s_ctx.state.phase_end_ts) {
        s_ctx.state.remaining_sec = 0;
        return;
    }
    s_ctx.state.remaining_sec = s_ctx.state.phase_end_ts - now;
}

/**
 * @brief Determine the next phase after the current one ends.
 *
 * @param[in] current_phase        Phase that just completed.
 * @param[in] completed_work_count Work phases completed so far.
 * @return Next phase.
 */
STATIC WUKONG_TM_POMODORO_PHASE_E __pomodoro_next_phase(WUKONG_TM_POMODORO_PHASE_E current_phase,
                                                         UINT_T completed_work_count)
{
    UINT_T interval;

    if (current_phase == WUKONG_TM_POMODORO_PHASE_SHORT_BREAK ||
        current_phase == WUKONG_TM_POMODORO_PHASE_LONG_BREAK) {
        return WUKONG_TM_POMODORO_PHASE_WORK;
    }

    interval = (UINT_T)s_ctx.state.cfg.work_sessions_before_long_break;
    if (interval == 0) {
        interval = 1;
    }
    if (completed_work_count > 0 && (completed_work_count % interval) == 0) {
        return WUKONG_TM_POMODORO_PHASE_LONG_BREAK;
    }
    return WUKONG_TM_POMODORO_PHASE_SHORT_BREAK;
}

/**
 * @brief Begin a new phase: set timestamps, schedule cron, emit event.
 *
 * @param[in] phase     Target phase.
 * @param[in] opr_event Event type to emit.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __pomodoro_begin_phase(WUKONG_TM_POMODORO_PHASE_E phase,
                                           WUKONG_TM_TIMER_OPR_E opr_event)
{
    TIME_T now = tal_time_get_posix();
    TIME_T phase_sec = __pomodoro_phase_total_sec(phase, &s_ctx.state.cfg);
    OPERATE_RET rt = OPRT_OK;

    s_ctx.state.phase = phase;
    s_ctx.state.phase_start_ts = now;
    s_ctx.state.phase_end_ts = now + phase_sec;
    s_ctx.state.remaining_sec = phase_sec;
    s_ctx.paused = FALSE;
    s_ctx.state.paused = FALSE;

    rt = __pomodoro_schedule_cron(phase_sec);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("pomodoro: schedule cron for phase %d failed %d", (INT_T)phase, rt);
        __pomodoro_reset();
        return rt;
    }

    return __pomodoro_emit_event(opr_event);
}

/**
 * @brief JSON-RPC handler invoked when the phase-end cron fires.
 *
 * @param[in]  params JSON-RPC params.
 * @param[out] result JSON-RPC result.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __pomodoro_phase_end_handler(CONST ty_cJSON *params, ty_cJSON **result)
{
    ty_cJSON *sid_node = NULL;
    UINT_T session_id = 0;
    WUKONG_TM_POMODORO_PHASE_E next_phase;
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(params, OPRT_INVALID_PARM);

    sid_node = ty_cJSON_GetObjectItem(params, "session_id");
    if (!ty_cJSON_IsNumber(sid_node)) {
        return OPRT_INVALID_PARM;
    }
    session_id = (UINT_T)sid_node->valueint;

    if (!s_ctx.active || s_ctx.paused || session_id != s_ctx.state.session_id) {
        return OPRT_NOT_FOUND;
    }

    s_ctx.cron_job_id[0] = '\0';

    if (s_ctx.state.phase == WUKONG_TM_POMODORO_PHASE_WORK) {
        s_ctx.state.completed_work_count++;
    }

    (VOID)__pomodoro_emit_event(WUKONG_TM_TIMER_OPR_FINISH);

    next_phase = __pomodoro_next_phase(s_ctx.state.phase, s_ctx.state.completed_work_count);
    if (next_phase == WUKONG_TM_POMODORO_PHASE_WORK) {
        s_ctx.state.current_cycle++;
    }

    rt = __pomodoro_begin_phase(next_phase, WUKONG_TM_TIMER_OPR_START);

    if (result != NULL) {
        *result = ty_cJSON_CreateObject();
        if (*result != NULL) {
            ty_cJSON_AddStringToObject(*result, "status", (rt == OPRT_OK) ? "ok" : "error");
        }
    }
    return rt;
}

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------- */

/**
 * @brief Initialize the pomodoro feature.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_pomodoro_init(VOID)
{
    if (s_ctx.initialized) {
        return OPRT_OK;
    }

    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.initialized = TRUE;
    s_ctx.next_session_id = 1;
    return wukong_cron_method_register(WUKONG_TM_POMODORO_PHASE_END_METHOD,
                                       __pomodoro_phase_end_handler);
}

/**
 * @brief Deinitialize the pomodoro feature.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_pomodoro_deinit(VOID)
{
    if (!s_ctx.initialized) {
        return OPRT_OK;
    }

    __pomodoro_remove_cron();
    (VOID)wukong_cron_method_unregister(WUKONG_TM_POMODORO_PHASE_END_METHOD);
    memset(&s_ctx, 0, sizeof(s_ctx));
    return OPRT_OK;
}

/**
 * @brief Start one pomodoro timer.
 *
 * @param[in] pomodoro_cfg Pomodoro configuration.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_pomodoro_start(CONST WUKONG_TM_POMODORO_CFG_T *pomodoro_cfg)
{
    if (pomodoro_cfg == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (!s_ctx.initialized) {
        return OPRT_COM_ERROR;
    }
    if (pomodoro_cfg->work_duration <= 0 ||
        pomodoro_cfg->short_break_duration <= 0 ||
        pomodoro_cfg->long_break_duration <= 0) {
        return OPRT_INVALID_PARM;
    }
    if (pomodoro_cfg->work_sessions_before_long_break < WUKONG_TM_POMODORO_WORK_BEFORE_LONG_MIN ||
        pomodoro_cfg->work_sessions_before_long_break > WUKONG_TM_POMODORO_WORK_BEFORE_LONG_MAX) {
        return OPRT_INVALID_PARM;
    }
    if (s_ctx.active) {
        return OPRT_COM_ERROR;
    }

    __pomodoro_reset();
    s_ctx.active = TRUE;
    s_ctx.state.active = TRUE;
    s_ctx.state.session_id = s_ctx.next_session_id++;
    s_ctx.state.current_cycle = 1;
    s_ctx.state.completed_work_count = 0;
    memcpy(&s_ctx.state.cfg, pomodoro_cfg, sizeof(s_ctx.state.cfg));

    return __pomodoro_begin_phase(WUKONG_TM_POMODORO_PHASE_WORK, WUKONG_TM_TIMER_OPR_START);
}

/**
 * @brief Pause the active pomodoro timer.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_pomodoro_pause(VOID)
{
    if (!s_ctx.active || s_ctx.paused) {
        return OPRT_NOT_FOUND;
    }

    __pomodoro_refresh_running();
    __pomodoro_remove_cron();

    s_ctx.paused = TRUE;
    s_ctx.state.paused = TRUE;
    s_ctx.state.phase_end_ts = 0;

    return __pomodoro_emit_event(WUKONG_TM_TIMER_OPR_PAUSE);
}

/**
 * @brief Resume the active pomodoro timer.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_pomodoro_resume(VOID)
{
    TIME_T now = 0;
    OPERATE_RET rt = OPRT_OK;

    if (!s_ctx.active || !s_ctx.paused) {
        return OPRT_NOT_FOUND;
    }

    if (s_ctx.state.remaining_sec <= 0) {
        return OPRT_COM_ERROR;
    }

    now = tal_time_get_posix();
    s_ctx.paused = FALSE;
    s_ctx.state.paused = FALSE;
    s_ctx.state.phase_start_ts = now;
    s_ctx.state.phase_end_ts = now + s_ctx.state.remaining_sec;

    rt = __pomodoro_schedule_cron(s_ctx.state.remaining_sec);
    if (rt != OPRT_OK) {
        __pomodoro_reset();
        return rt;
    }

    return __pomodoro_emit_event(WUKONG_TM_TIMER_OPR_RESUME);
}

/**
 * @brief Stop the active pomodoro timer.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_pomodoro_stop(VOID)
{
    OPERATE_RET rt = OPRT_OK;

    if (!s_ctx.active) {
        return OPRT_NOT_FOUND;
    }

    rt = __pomodoro_emit_event(WUKONG_TM_TIMER_OPR_STOP);
    __pomodoro_reset();
    return rt;
}

/**
 * @brief Query the runtime snapshot of the active pomodoro session.
 *
 * @param[out] state Runtime snapshot buffer.
 * @return OPRT_OK on success, OPRT_NOT_FOUND when no active session exists.
 */
OPERATE_RET wukong_tm_pomodoro_query(WUKONG_TM_POMODORO_STATE_T *state)
{
    if (state == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (!s_ctx.active) {
        return OPRT_NOT_FOUND;
    }

    __pomodoro_refresh_running();
    memcpy(state, &s_ctx.state, sizeof(*state));
    state->active = TRUE;
    state->paused = s_ctx.paused;
    return OPRT_OK;
}
