/**
 * @file wukong_cron.c
 * @brief Wukong cron service: in-memory job table, single-timer scheduling, and JSON-RPC dispatch.
 *
 * This module owns the runtime scheduler for cron jobs. Jobs are stored in a fixed-size
 * in-memory table, parsed from JSON, translated into cron bitmaps, and scheduled by one
 * `tal_sw_timer`. When a job expires, the timer callback only selects due jobs and hands
 * execution off to `WORKQ_SYSTEM`; the actual business method is executed through the
 * local JSON-RPC executor in `wukong_cron_rpc.c`.
 *
 * Main responsibilities:
 * - Manage add/update/remove/list/execute operations for cron jobs
 * - Keep `next_fire_ts` and `last_fire_ts` for each enabled job
 * - Rearm a single nearest-deadline timer instead of polling every second
 * - Keep persistence behind internal store hooks so v1 can stay memory-only
 */

#include "wukong_cron.h"

#include <stdio.h>
#include <string.h>

#include "tal_log.h"
#include "tal_memory.h"
#include "tal_sw_timer.h"
#include "tal_time_service.h"
#include "tal_workq_service.h"

#include "wukong_cron_expr.h"

/* ---------------------------------------------------------------------------
 * Runtime data structures
 * --------------------------------------------------------------------------- */
/**
 * @brief Fixed-size job table for the v1 in-memory scheduler.
 */
#define WUKONG_CRON_MAX_JOBS 16

/**
 * @brief Runtime representation of one cron job.
 */
typedef struct {
    /** Whether this slot is currently occupied. */
    BOOL_T used;
    /** Whether this job is enabled for scheduling. */
    BOOL_T enabled;
    /** Whether this job should be removed after its first scheduled dispatch. */
    BOOL_T once;
    /** Stable runtime/persisted job identifier. */
    CHAR_T id[WUKONG_CRON_JOB_ID_LEN + 1];
    /** Human-readable job name. */
    CHAR_T name[WUKONG_CRON_JOB_NAME_LEN + 1];
    /** Original cron expression string. */
    CHAR_T cron_expr[WUKONG_CRON_EXPR_MAX_LEN];
    /** Parsed bitmap representation of `cron_expr`. */
    WUKONG_CRON_EXPR_T expr;
    /** Duplicated JSON-RPC request object executed when the job fires. */
    ty_cJSON *request;
    /** Next scheduled firing timestamp in local POSIX time. */
    TIME_T next_fire_ts;
    /** Last actual firing timestamp in local POSIX time. */
    TIME_T last_fire_ts;
} WUKONG_CRON_JOB_T;

/**
 * @brief Work queue payload used to execute a job outside timer context.
 */
typedef struct {
    /** Serialized JSON-RPC request string for deferred execution. */
    CHAR_T *request_json;
} WUKONG_CRON_WORK_T;

/**
 * @brief Global cron runtime context.
 */
typedef struct {
    /** Whether the cron service has been initialized. */
    BOOL_T initialized;
    /** Whether local time/timezone is ready for scheduling. */
    BOOL_T time_ready;
    /** Whether the scheduler is currently running inside the timer callback. */
    BOOL_T in_timer_cb;
    /** Monotonic sequence used to auto-generate job ids. */
    UINT_T next_job_seq;
    /** Single software timer for nearest-deadline scheduling. */
    TIMER_ID timer;
    /** Fixed-size in-memory job table. */
    WUKONG_CRON_JOB_T jobs[WUKONG_CRON_MAX_JOBS];
} WUKONG_CRON_CTX_T;

STATIC WUKONG_CRON_CTX_T s_cron_ctx;

/* ---------------------------------------------------------------------------
 * Store hooks
 * --------------------------------------------------------------------------- */
/**
 * @brief Load persisted jobs into the runtime table.
 *
 * v1 is memory-only, so this hook currently does nothing and succeeds.
 *
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __store_load(VOID)
{
    return OPRT_OK;
}

/**
 * @brief Flush runtime changes to the backing store.
 *
 * v1 is memory-only, so this hook currently does nothing and succeeds.
 *
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __store_sync(VOID)
{
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------------------- */
/**
 * @brief Release a job slot and its duplicated JSON payload.
 *
 * @param[in,out] job Target slot to clear.
 */
STATIC VOID __job_reset(WUKONG_CRON_JOB_T *job)
{
    if (job == NULL) {
        return;
    }

    ty_cJSON_Delete(job->request);
    memset(job, 0, sizeof(*job));
}

/**
 * @brief Serialize one cached request object before scheduling work queue execution.
 *
 * @param[in]  job           Source job containing the duplicated request object.
 * @param[out] request_json  Serialized JSON string owned by the caller.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __job_clone_request_json(CONST WUKONG_CRON_JOB_T *job, CHAR_T **request_json)
{
    TUYA_CHECK_NULL_RETURN(job, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(request_json, OPRT_INVALID_PARM);

    *request_json = ty_cJSON_PrintUnformatted(job->request);
    return (*request_json != NULL) ? OPRT_OK : OPRT_MALLOC_FAILED;
}

/**
 * @brief Find an existing job slot by id.
 *
 * @param[in] job_id Target job id.
 * @return Slot index on success, `-1` if not found.
 */
STATIC INT_T __job_find_index(CONST CHAR_T *job_id)
{
    INT_T index = 0;

    if (job_id == NULL) {
        return -1;
    }

    for (index = 0; index < WUKONG_CRON_MAX_JOBS; index++) {
        if (s_cron_ctx.jobs[index].used &&
            strcmp(s_cron_ctx.jobs[index].id, job_id) == 0) {
            return index;
        }
    }

    return -1;
}

/**
 * @brief Find the first free slot in the fixed-size runtime table.
 *
 * @return Slot index on success, `-1` if the table is full.
 */
STATIC INT_T __job_alloc_index(VOID)
{
    INT_T index = 0;

    for (index = 0; index < WUKONG_CRON_MAX_JOBS; index++) {
        if (!s_cron_ctx.jobs[index].used) {
            return index;
        }
    }

    return -1;
}

/**
 * @brief Recalculate `next_fire_ts` for one job when scheduling is allowed.
 *
 * If time is not ready or the job is disabled, the timestamp is cleared and no
 * schedule is produced.
 *
 * @param[in,out] job Target runtime job.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __job_compute_next_fire(WUKONG_CRON_JOB_T *job)
{
    TUYA_CHECK_NULL_RETURN(job, OPRT_INVALID_PARM);

    job->next_fire_ts = 0;
    if (!s_cron_ctx.time_ready || !job->used || !job->enabled) {
        return OPRT_OK;
    }

    return wukong_cron_expr_next_fire(&job->expr, tal_time_get_posix(), &job->next_fire_ts);
}

/**
 * @brief Rearm the single software timer to the nearest enabled job.
 *
 * The timer is always stopped first, then restarted only when at least one
 * enabled job has a valid `next_fire_ts`.
 *
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __cron_reschedule_locked(VOID)
{
    TIME_T now = tal_time_get_posix();
    TIME_T next_fire_ts = 0;
    UINT_T delay_ms = 0;
    INT_T index = 0;

    if (s_cron_ctx.timer == NULL) {
        return OPRT_OK;
    }

    if (!s_cron_ctx.in_timer_cb) {
        tal_sw_timer_stop(s_cron_ctx.timer);
    }

    if (!s_cron_ctx.time_ready) {
        return OPRT_OK;
    }

    for (index = 0; index < WUKONG_CRON_MAX_JOBS; index++) {
        if (!s_cron_ctx.jobs[index].used || !s_cron_ctx.jobs[index].enabled ||
            s_cron_ctx.jobs[index].next_fire_ts <= 0) {
            continue;
        }

        TAL_PR_DEBUG("cron -> job id=%s name=%s enabled=%d once=%d",
                     s_cron_ctx.jobs[index].id, s_cron_ctx.jobs[index].name,
                     s_cron_ctx.jobs[index].enabled ? 1 : 0,
                     s_cron_ctx.jobs[index].once ? 1 : 0);
        TAL_PR_DEBUG("cron -> %s", s_cron_ctx.jobs[index].cron_expr);
        // TAL_PR_DEBUG("cron -> job %s", s_cron_ctx.jobs[index].request);
        TAL_PR_DEBUG("cron -> %lld %lld",
                     (long long)s_cron_ctx.jobs[index].next_fire_ts,
                     (long long)s_cron_ctx.jobs[index].last_fire_ts);

        if (next_fire_ts == 0 || s_cron_ctx.jobs[index].next_fire_ts < next_fire_ts) {
            next_fire_ts = s_cron_ctx.jobs[index].next_fire_ts;
        }
    }

    if (next_fire_ts == 0) {
        return OPRT_OK;
    }

    delay_ms = (next_fire_ts <= now) ? 1U : (UINT_T)((next_fire_ts - now) * 1000);
    if (delay_ms == 0) {
        delay_ms = 1;
    }

    TAL_PR_DEBUG("cron -> reschedule timer: %u ms", delay_ms);
    return tal_sw_timer_start(s_cron_ctx.timer, delay_ms, TAL_TIMER_ONCE);
}

/**
 * @brief Parse a job JSON payload into the runtime representation used by the scheduler.
 *
 * Expected fields are `id`, `name`, `enabled`, optional `once`, `cron`, and `request`. When
 * `auto_id` is true, a missing `id` is tolerated and later generated by the
 * caller.
 *
 * @param[in]  job_json Input job JSON string.
 * @param[in]  auto_id  Whether a missing `id` may be auto-generated later.
 * @param[out] job      Parsed runtime job.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __job_parse_json(CONST CHAR_T *job_json, BOOL_T auto_id, WUKONG_CRON_JOB_T *job)
{
    ty_cJSON *root = NULL;
    ty_cJSON *node = NULL;
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(job_json, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(job, OPRT_INVALID_PARM);

    memset(job, 0, sizeof(*job));

    root = ty_cJSON_Parse(job_json);
    if (!ty_cJSON_IsObject(root)) {
        ty_cJSON_Delete(root);
        return OPRT_INVALID_PARM;
    }

    node = ty_cJSON_GetObjectItem(root, "id");
    if (node != NULL && ty_cJSON_IsString(node) && node->valuestring != NULL) {
        strncpy(job->id, node->valuestring, sizeof(job->id) - 1);
    } else if (!auto_id) {
        ty_cJSON_Delete(root);
        return OPRT_INVALID_PARM;
    }

    node = ty_cJSON_GetObjectItem(root, "name");
    if (node != NULL && ty_cJSON_IsString(node) && node->valuestring != NULL) {
        strncpy(job->name, node->valuestring, sizeof(job->name) - 1);
    }

    job->enabled = TRUE;
    node = ty_cJSON_GetObjectItem(root, "enabled");
    if (node != NULL && ty_cJSON_IsNumber(node)) {
        job->enabled = (node->valueint != 0) ? TRUE : FALSE;
    }

    node = ty_cJSON_GetObjectItem(root, "once");
    if (node != NULL && ty_cJSON_IsNumber(node)) {
        job->once = (node->valueint != 0) ? TRUE : FALSE;
    }

    node = ty_cJSON_GetObjectItem(root, "cron");
    if (!ty_cJSON_IsString(node) || node->valuestring == NULL) {
        ty_cJSON_Delete(root);
        return OPRT_INVALID_PARM;
    }
    strncpy(job->cron_expr, node->valuestring, sizeof(job->cron_expr) - 1);

    rt = wukong_cron_expr_parse(job->cron_expr, &job->expr);
    if (rt != OPRT_OK) {
        ty_cJSON_Delete(root);
        return rt;
    }

    node = ty_cJSON_GetObjectItem(root, "request");
    if (!ty_cJSON_IsObject(node)) {
        ty_cJSON_Delete(root);
        return OPRT_INVALID_PARM;
    }
    job->request = ty_cJSON_Duplicate(node, 1);
    if (job->request == NULL) {
        ty_cJSON_Delete(root);
        return OPRT_MALLOC_FAILED;
    }

    job->used = TRUE;
    ty_cJSON_Delete(root);
    return OPRT_OK;
}

/**
 * @brief Work queue callback used to execute a due job outside timer context.
 *
 * @param[in] data Pointer to `WUKONG_CRON_WORK_T`.
 */
STATIC VOID __cron_job_worker(VOID_T *data)
{
    WUKONG_CRON_WORK_T *work = (WUKONG_CRON_WORK_T *)data;
    CHAR_T *response_json = NULL;

    if (work == NULL) {
        return;
    }

    TAL_PR_DEBUG("cron -> job worker: %s", work->request_json);
    if (work->request_json != NULL) {
        if (wukong_cron_rpc_execute_string(work->request_json, &response_json) == OPRT_OK) {
            ty_cJSON_FreeBuffer(response_json);
        }
        ty_cJSON_FreeBuffer(work->request_json);
    }

    tal_free(work);
}

/**
 * @brief Software timer callback for due-job dispatch.
 *
 * The callback intentionally avoids business logic. It only finds jobs whose
 * `next_fire_ts` has arrived, schedules them to `WORKQ_SYSTEM`, recalculates
 * each job's next timestamp, and rearms the global timer.
 *
 * @param[in] timer_id Triggered timer id.
 * @param[in] arg      User argument, unused.
 */
STATIC VOID __cron_timer_cb(TIMER_ID timer_id, VOID_T *arg)
{
    TIME_T now = tal_time_get_posix();
    INT_T index = 0;
    OPERATE_RET rt = OPRT_OK;

    (VOID)timer_id;
    (VOID)arg;

    TAL_PR_DEBUG("cron -> timer callback");
    s_cron_ctx.in_timer_cb = TRUE;
    for (index = 0; index < WUKONG_CRON_MAX_JOBS; index++) {
        WUKONG_CRON_JOB_T *job = &s_cron_ctx.jobs[index];
        WUKONG_CRON_WORK_T *work = NULL;

        if (!job->used || !job->enabled || job->next_fire_ts <= 0 || job->next_fire_ts > now) {
            continue;
        }

        work = tal_calloc(1, sizeof(*work));
        if (work != NULL && __job_clone_request_json(job, &work->request_json) == OPRT_OK) {
            if (tal_workq_schedule(WORKQ_SYSTEM, __cron_job_worker, work) != OPRT_OK) {
                ty_cJSON_FreeBuffer(work->request_json);
                tal_free(work);
            }
        } else {
            tal_free(work);
        }

        if (job->once) {
            __job_reset(job);
            continue;
        }

        job->last_fire_ts = now;
        __job_compute_next_fire(job);
    }

    rt = __cron_reschedule_locked();
    s_cron_ctx.in_timer_cb = FALSE;
    if (rt != OPRT_OK) {
        TAL_PR_ERR("cron -> reschedule failed after callback, rt: %d", rt);
    }
}

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------- */
/**
 * @brief Initialize the cron runtime context.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_cron_init(VOID)
{
    OPERATE_RET rt = OPRT_OK;

    if (s_cron_ctx.initialized) {
        return OPRT_OK;
    }

    memset(&s_cron_ctx, 0, sizeof(s_cron_ctx));
    s_cron_ctx.next_job_seq = 1;

    rt = wukong_cron_rpc_init();
    if (rt != OPRT_OK) {
        return rt;
    }
    rt = tal_sw_timer_create(__cron_timer_cb, NULL, &s_cron_ctx.timer);
    if (rt != OPRT_OK) {
        wukong_cron_rpc_deinit();
        return rt;
    }
    rt = __store_load();
    if (rt != OPRT_OK) {
        tal_sw_timer_delete(s_cron_ctx.timer);
        s_cron_ctx.timer = NULL;
        wukong_cron_rpc_deinit();
        return rt;
    }

    s_cron_ctx.initialized = TRUE;
    return OPRT_OK;
}

/**
 * @brief Deinitialize the cron runtime context and release all slots.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_cron_deinit(VOID)
{
    INT_T index = 0;

    if (!s_cron_ctx.initialized) {
        return OPRT_OK;
    }

    tal_sw_timer_stop(s_cron_ctx.timer);
    tal_sw_timer_delete(s_cron_ctx.timer);
    s_cron_ctx.timer = NULL;

    for (index = 0; index < WUKONG_CRON_MAX_JOBS; index++) {
        __job_reset(&s_cron_ctx.jobs[index]);
    }

    wukong_cron_rpc_deinit();
    memset(&s_cron_ctx, 0, sizeof(s_cron_ctx));
    return OPRT_OK;
}

/**
 * @brief Register one local JSON-RPC method for cron execution.
 *
 * This is a thin forwarding wrapper over `wukong_cron_rpc_method_register()`.
 *
 * @param[in] method  JSON-RPC method name.
 * @param[in] handler Method callback.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_cron_method_register(CONST CHAR_T *method, WUKONG_CRON_RPC_HANDLER handler)
{
    return wukong_cron_rpc_method_register(method, handler);
}

/**
 * @brief Unregister one local JSON-RPC method.
 *
 * @param[in] method JSON-RPC method name.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_cron_method_unregister(CONST CHAR_T *method)
{
    return wukong_cron_rpc_method_unregister(method);
}

/**
 * @brief Add one cron job to the runtime table.
 *
 * @param[in]  job_json    Job definition in JSON form.
 * @param[out] job_id      Final job id written by the module.
 * @param[in]  job_id_len  Output buffer size.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_cron_job_add(CONST CHAR_T *job_json, CHAR_T *job_id, UINT_T job_id_len)
{
    WUKONG_CRON_JOB_T job = {0};
    INT_T index = 0;
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(job_json, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(job_id, OPRT_INVALID_PARM);
    if (job_id_len == 0) {
        return OPRT_INVALID_PARM;
    }

    TAL_PR_DEBUG("cron -> job add: %s", job_json);
    job_id[0] = '\0';
    rt = __job_parse_json(job_json, TRUE, &job);
    if (rt != OPRT_OK) {
        return rt;
    }

    if (job.id[0] == '\0') {
        (VOID)snprintf(job.id, sizeof(job.id), "cron-%u", s_cron_ctx.next_job_seq++);
    } else if (__job_find_index(job.id) >= 0) {
        __job_reset(&job);
        return OPRT_COM_ERROR;
    }

    index = __job_alloc_index();
    if (index < 0) {
        __job_reset(&job);
        return OPRT_COM_ERROR;
    }

    s_cron_ctx.jobs[index] = job;
    rt = __job_compute_next_fire(&s_cron_ctx.jobs[index]);
    if (rt != OPRT_OK) {
        __job_reset(&s_cron_ctx.jobs[index]);
        return rt;
    }
    rt = __store_sync();
    if (rt != OPRT_OK) {
        __job_reset(&s_cron_ctx.jobs[index]);
        return rt;
    }

    strncpy(job_id, s_cron_ctx.jobs[index].id, job_id_len - 1);
    job_id[job_id_len - 1] = '\0';
    return __cron_reschedule_locked();
}

/**
 * @brief Update one existing cron job.
 *
 * The job must already exist and must carry an explicit `id`.
 *
 * @param[in] job_json Job definition in JSON form.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_cron_job_update(CONST CHAR_T *job_json)
{
    WUKONG_CRON_JOB_T job = {0};
    INT_T index = 0;
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(job_json, OPRT_INVALID_PARM);

    rt = __job_parse_json(job_json, FALSE, &job);
    if (rt != OPRT_OK) {
        return rt;
    }

    index = __job_find_index(job.id);
    if (index < 0) {
        __job_reset(&job);
        return OPRT_NOT_FOUND;
    }

    __job_reset(&s_cron_ctx.jobs[index]);
    s_cron_ctx.jobs[index] = job;
    rt = __job_compute_next_fire(&s_cron_ctx.jobs[index]);
    if (rt != OPRT_OK) {
        __job_reset(&s_cron_ctx.jobs[index]);
        return rt;
    }
    rt = __store_sync();
    if (rt != OPRT_OK) {
        __job_reset(&s_cron_ctx.jobs[index]);
        return rt;
    }

    return __cron_reschedule_locked();
}

/**
 * @brief Remove one existing cron job by id.
 *
 * @param[in] job_id Target job id.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_cron_job_remove(CONST CHAR_T *job_id)
{
    INT_T index = 0;
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(job_id, OPRT_INVALID_PARM);

    index = __job_find_index(job_id);
    if (index < 0) {
        return OPRT_NOT_FOUND;
    }

    __job_reset(&s_cron_ctx.jobs[index]);
    rt = __store_sync();
    if (rt != OPRT_OK) {
        return rt;
    }
    return __cron_reschedule_locked();
}

/**
 * @brief Export a lightweight JSON snapshot of the in-memory job table.
 *
 * @param[out] job_list_json Unformatted JSON string allocated by cJSON.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_cron_job_list(CHAR_T **job_list_json)
{
    ty_cJSON *root = NULL;
    ty_cJSON *jobs = NULL;
    INT_T index = 0;

    TUYA_CHECK_NULL_RETURN(job_list_json, OPRT_INVALID_PARM);

    *job_list_json = NULL;
    root = ty_cJSON_CreateObject();
    jobs = ty_cJSON_CreateArray();
    if (root == NULL || jobs == NULL) {
        ty_cJSON_Delete(root);
        ty_cJSON_Delete(jobs);
        return OPRT_MALLOC_FAILED;
    }

    for (index = 0; index < WUKONG_CRON_MAX_JOBS; index++) {
        ty_cJSON *item = NULL;
        WUKONG_CRON_JOB_T *job = &s_cron_ctx.jobs[index];

        if (!job->used) {
            continue;
        }

        item = ty_cJSON_CreateObject();
        if (item == NULL) {
            continue;
        }
        ty_cJSON_AddStringToObject(item, "id", job->id);
        ty_cJSON_AddStringToObject(item, "name", job->name);
        ty_cJSON_AddNumberToObject(item, "enabled", job->enabled ? 1 : 0);
        ty_cJSON_AddNumberToObject(item, "once", job->once ? 1 : 0);
        ty_cJSON_AddStringToObject(item, "cron", job->cron_expr);
        ty_cJSON_AddNumberToObject(item, "next_fire_ts", (INT_T)job->next_fire_ts);
        ty_cJSON_AddNumberToObject(item, "last_fire_ts", (INT_T)job->last_fire_ts);
        ty_cJSON_AddItemToArray(jobs, item);
    }

    ty_cJSON_AddItemToObject(root, "jobs", jobs);
    *job_list_json = ty_cJSON_PrintUnformatted(root);
    ty_cJSON_Delete(root);
    if (*job_list_json == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    return OPRT_OK;
}

/**
 * @brief Execute one job definition immediately without adding it to the table.
 *
 * @param[in]  job_json        Job definition in JSON form.
 * @param[out] response_json   JSON-RPC response string allocated by cJSON.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_cron_job_execute(CONST CHAR_T *job_json, CHAR_T **response_json)
{
    WUKONG_CRON_JOB_T job = {0};
    ty_cJSON *response = NULL;
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(job_json, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(response_json, OPRT_INVALID_PARM);

    *response_json = NULL;
    rt = __job_parse_json(job_json, TRUE, &job);
    if (rt != OPRT_OK) {
        return rt;
    }

    rt = wukong_cron_rpc_execute_json(job.request, &response);
    __job_reset(&job);
    if (rt != OPRT_OK) {
        return rt;
    }

    *response_json = ty_cJSON_PrintUnformatted(response);
    ty_cJSON_Delete(response);
    if (*response_json == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    return OPRT_OK;
}

/**
 * @brief Notify the scheduler that local time and timezone are ready.
 *
 * Once this call succeeds, all registered jobs are recalculated and the nearest
 * one is armed on the single software timer.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_cron_time_ready_notify(VOID)
{
    INT_T index = 0;
    OPERATE_RET rt = OPRT_OK;

    s_cron_ctx.time_ready = TRUE;
    for (index = 0; index < WUKONG_CRON_MAX_JOBS; index++) {
        if (!s_cron_ctx.jobs[index].used) {
            continue;
        }
        rt = __job_compute_next_fire(&s_cron_ctx.jobs[index]);
        if (rt != OPRT_OK) {
            return rt;
        }
    }

    return __cron_reschedule_locked();
}
