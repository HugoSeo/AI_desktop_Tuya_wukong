/**
 * @file wukong_tm_stopwatch.c
 * @brief Stopwatch facade for the unified time-management module.
 */

#include "wukong_tm.h"

#include <string.h>

#include "tal_log.h"
#include "tal_memory.h"
#include "tal_time_service.h"
#include "wukong_ai_agent.h"
#include "wukong_tm_internal.h"

typedef struct {
    BOOL_T initialized;
    BOOL_T active;
    BOOL_T paused;
    TIME_T start_ts;
    TIME_T paused_elapsed_sec;
} WUKONG_TM_STOPWATCH_CTX_T;

STATIC WUKONG_TM_STOPWATCH_CTX_T s_stopwatch_ctx;

/**
 * @brief Compute total elapsed seconds for the current session.
 *
 * @return Elapsed seconds, or 0 when inactive.
 */
STATIC TIME_T __stopwatch_elapsed_sec(VOID)
{
    if (!s_stopwatch_ctx.active) {
        return 0;
    }
    if (s_stopwatch_ctx.paused) {
        return s_stopwatch_ctx.paused_elapsed_sec;
    }
    return s_stopwatch_ctx.paused_elapsed_sec +
           (tal_time_get_posix() - s_stopwatch_ctx.start_ts);
}

/**
 * @brief Emit one stopwatch event payload.
 *
 * @param[in] opr                 Stopwatch operation.
 * @param[in] include_elapsed_sec When TRUE, append TLV with cumulative elapsed seconds (host LE).
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __stopwatch_emit_event(WUKONG_TM_TIMER_OPR_E opr, BOOL_T include_elapsed_sec)
{
    UINT_T offset = 0;
    UINT8_T *msg = NULL;
    UINT_T len = WUKONG_TM_TLV_TL_LEN + 1;
    TIME_T elapsed = 0;
    UINT_T elapsed_value = 0;

    if (include_elapsed_sec) {
        elapsed = __stopwatch_elapsed_sec();
        if (elapsed < 0) {
            elapsed = 0;
        }
        elapsed_value = (UINT_T)elapsed;
        len += (WUKONG_TM_TLV_TL_LEN + sizeof(elapsed_value));
    }

    msg = tal_malloc(len);
    if (msg == NULL) {
        TAL_PR_ERR("%s: malloc failed", __func__);
        return OPRT_MALLOC_FAILED;
    }

    __tm_tlv_pack(msg, WUKONG_TM_TAG_STOPWATCH_OPR, 1, (CONST UINT8_T *)&opr, &offset);
    if (include_elapsed_sec) {
        __tm_tlv_pack(msg, WUKONG_TM_TAG_STOPWATCH_ELAPSED_SEC, sizeof(elapsed_value),
                      (CONST UINT8_T *)&elapsed_value, &offset);
    }
    TAL_PR_NOTICE("stopwatch -> emit event: opr=%s(%d) elapsed=%llds paused=%d",
                  __tm_opr_name(opr), opr,
                  (long long)__stopwatch_elapsed_sec(),
                  s_stopwatch_ctx.paused);
    wukong_ai_event_notify(WUKONG_AI_EVENT_CLOCK_MCP_STOPWATCH_TIMER, msg);
    tal_free(msg);
    return OPRT_OK;
}

/**
 * @brief Initialize the stopwatch feature.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_stopwatch_init(VOID)
{
    if (s_stopwatch_ctx.initialized) {
        return OPRT_OK;
    }
    memset(&s_stopwatch_ctx, 0, sizeof(s_stopwatch_ctx));
    s_stopwatch_ctx.initialized = TRUE;
    return OPRT_OK;
}

/**
 * @brief Deinitialize the stopwatch feature.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_tm_stopwatch_deinit(VOID)
{
    memset(&s_stopwatch_ctx, 0, sizeof(s_stopwatch_ctx));
    return OPRT_OK;
}

/**
 * @brief Start the stopwatch.
 *
 * @return OPRT_OK on success, OPRT_COM_ERROR when already active.
 */
OPERATE_RET wukong_tm_stopwatch_start(VOID)
{
    if (s_stopwatch_ctx.active) {
        return OPRT_COM_ERROR;
    }

    s_stopwatch_ctx.active = TRUE;
    s_stopwatch_ctx.paused = FALSE;
    s_stopwatch_ctx.start_ts = tal_time_get_posix();
    s_stopwatch_ctx.paused_elapsed_sec = 0;
    return __stopwatch_emit_event(WUKONG_TM_TIMER_OPR_START, FALSE);
}

/**
 * @brief Pause the stopwatch.
 *
 * @return OPRT_OK on success, OPRT_NOT_SUPPORTED when not in a pausable state.
 */
OPERATE_RET wukong_tm_stopwatch_pause(VOID)
{
    if (!s_stopwatch_ctx.active || s_stopwatch_ctx.paused) {
        return OPRT_NOT_SUPPORTED;
    }

    s_stopwatch_ctx.paused_elapsed_sec += (tal_time_get_posix() - s_stopwatch_ctx.start_ts);
    s_stopwatch_ctx.paused = TRUE;
    return __stopwatch_emit_event(WUKONG_TM_TIMER_OPR_PAUSE, TRUE);
}

/**
 * @brief Resume the stopwatch.
 *
 * @return OPRT_OK on success, OPRT_NOT_SUPPORTED when not paused.
 */
OPERATE_RET wukong_tm_stopwatch_resume(VOID)
{
    if (!s_stopwatch_ctx.active || !s_stopwatch_ctx.paused) {
        return OPRT_NOT_SUPPORTED;
    }

    s_stopwatch_ctx.start_ts = tal_time_get_posix();
    s_stopwatch_ctx.paused = FALSE;
    return __stopwatch_emit_event(WUKONG_TM_TIMER_OPR_RESUME, FALSE);
}

/**
 * @brief Stop the stopwatch.
 *
 * @return OPRT_OK on success, OPRT_NOT_FOUND when no active stopwatch exists.
 */
OPERATE_RET wukong_tm_stopwatch_stop(VOID)
{
    OPERATE_RET rt = OPRT_OK;

    if (!s_stopwatch_ctx.active) {
        return OPRT_NOT_FOUND;
    }

    rt = __stopwatch_emit_event(WUKONG_TM_TIMER_OPR_STOP, TRUE);
    s_stopwatch_ctx.active = FALSE;
    s_stopwatch_ctx.paused = FALSE;
    s_stopwatch_ctx.start_ts = 0;
    s_stopwatch_ctx.paused_elapsed_sec = 0;
    return rt;
}

/**
 * @brief Reset the stopwatch.
 *
 * @return OPRT_OK on success, OPRT_NOT_FOUND when no active stopwatch exists.
 */
OPERATE_RET wukong_tm_stopwatch_reset(VOID)
{
    OPERATE_RET rt = OPRT_OK;

    if (!s_stopwatch_ctx.active) {
        return OPRT_NOT_FOUND;
    }

    rt = __stopwatch_emit_event(WUKONG_TM_TIMER_OPR_RESET, TRUE);
    s_stopwatch_ctx.active = FALSE;
    s_stopwatch_ctx.paused = FALSE;
    s_stopwatch_ctx.start_ts = 0;
    s_stopwatch_ctx.paused_elapsed_sec = 0;
    return rt;
}

/**
 * @brief Query the runtime snapshot of the stopwatch singleton.
 *
 * @param[out] state Stopwatch snapshot buffer.
 * @return OPRT_OK on success, OPRT_NOT_FOUND when no active stopwatch exists.
 */
OPERATE_RET wukong_tm_stopwatch_query(WUKONG_TM_STOPWATCH_STATE_T *state)
{
    if (state == NULL) {
        return OPRT_INVALID_PARM;
    }

    if (!s_stopwatch_ctx.active) {
        return OPRT_NOT_FOUND;
    }

    state->active = TRUE;
    state->paused = s_stopwatch_ctx.paused;
    state->elapsed_sec = __stopwatch_elapsed_sec();
    return OPRT_OK;
}
