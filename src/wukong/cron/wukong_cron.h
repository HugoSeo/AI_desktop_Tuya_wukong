/**
 * @file wukong_cron.h
 * @brief Wukong cron service public API.
 *
 * This header exposes the service layer for cron job lifecycle management:
 * initialization, method registration, job CRUD, immediate execution, and
 * scheduling resume after local time becomes available.
 */

#ifndef __WUKONG_CRON_H__
#define __WUKONG_CRON_H__

#include "tuya_cloud_types.h"

#include "wukong_cron_rpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum length for a persisted/runtime job identifier.
 */
#define WUKONG_CRON_JOB_ID_LEN    32
/**
 * @brief Maximum length for a human-readable job name.
 */
#define WUKONG_CRON_JOB_NAME_LEN  64

/**
 * @brief Initialize the cron service and its scheduler context.
 *
 * Creates the internal JSON-RPC executor, prepares the in-memory store and
 * allocates the single software timer used for nearest-deadline scheduling.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_cron_init(VOID);

/**
 * @brief Tear down the cron service and release all runtime resources.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_cron_deinit(VOID);

/**
 * @brief Register a local JSON-RPC method that cron jobs can invoke.
 *
 * @param[in] method  JSON-RPC method name.
 * @param[in] handler Callback used to execute the method.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_cron_method_register(CONST CHAR_T *method, WUKONG_CRON_RPC_HANDLER handler);

/**
 * @brief Remove a previously registered local JSON-RPC method.
 *
 * @param[in] method JSON-RPC method name.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_cron_method_unregister(CONST CHAR_T *method);

/**
 * @brief Add a cron job from a JSON description.
 *
 * @param[in]  job_json    Job definition containing id/name/enabled/cron/request.
 * @param[out] job_id      Buffer used to receive the final job id.
 * @param[in]  job_id_len  Size of @p job_id in bytes.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_cron_job_add(CONST CHAR_T *job_json, CHAR_T *job_id, UINT_T job_id_len);

/**
 * @brief Update an existing cron job by id.
 *
 * The input JSON must contain a valid existing `id`.
 *
 * @param[in] job_json Job definition containing an existing `id`.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_cron_job_update(CONST CHAR_T *job_json);

/**
 * @brief Remove an existing cron job by id.
 *
 * @param[in] job_id Target job id.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_cron_job_remove(CONST CHAR_T *job_id);

/**
 * @brief Export the current job list as an unformatted JSON string.
 *
 * @param[out] job_list_json Unformatted JSON string allocated by cJSON.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_cron_job_list(CHAR_T **job_list_json);

/**
 * @brief Execute a job payload immediately without waiting for the scheduler.
 *
 * @param[in]  job_json       Job definition in JSON form.
 * @param[out] response_json  Unformatted JSON-RPC response string allocated by cJSON.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_cron_job_execute(CONST CHAR_T *job_json, CHAR_T **response_json);

/**
 * @brief Notify cron that local time/timezone is ready for scheduling.
 *
 * Until this is called successfully, jobs may be registered but their
 * `next_fire_ts` will remain suspended.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_cron_time_ready_notify(VOID);

#ifdef __cplusplus
}
#endif

#endif /* __WUKONG_CRON_H__ */
