/**
 * @file wukong_cron_rpc.h
 * @brief Wukong local JSON-RPC 2.0 executor public API.
 *
 * This header defines the local JSON-RPC callback type, standard error codes,
 * and the API used by the cron service to register methods and execute requests.
 */

#ifndef __WUKONG_CRON_RPC_H__
#define __WUKONG_CRON_RPC_H__

#include "tuya_cloud_types.h"
#include "ty_cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief JSON-RPC 2.0 parse error.
 */
#define WUKONG_CRON_RPC_ERR_PARSE            (-32700)
/**
 * @brief JSON-RPC 2.0 invalid request.
 */
#define WUKONG_CRON_RPC_ERR_INVALID_REQUEST  (-32600)
/**
 * @brief JSON-RPC 2.0 method not found.
 */
#define WUKONG_CRON_RPC_ERR_METHOD_NOT_FOUND (-32601)
/**
 * @brief JSON-RPC 2.0 invalid params.
 */
#define WUKONG_CRON_RPC_ERR_INVALID_PARAMS   (-32602)
/**
 * @brief JSON-RPC 2.0 internal error.
 */
#define WUKONG_CRON_RPC_ERR_INTERNAL         (-32603)

/**
 * @brief Local method callback used by the cron RPC executor.
 *
 * The callback receives a `params` object and returns a JSON object via
 * `result` when execution succeeds.
 */
typedef OPERATE_RET (*WUKONG_CRON_RPC_HANDLER)(CONST ty_cJSON *params, ty_cJSON **result);

/**
 * @brief Initialize the local JSON-RPC method registry.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_cron_rpc_init(VOID);
/**
 * @brief Release all registered methods.
 */
VOID wukong_cron_rpc_deinit(VOID);
/**
 * @brief Register a JSON-RPC method handler.
 *
 * @param[in] method  JSON-RPC method name.
 * @param[in] handler Method callback.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_cron_rpc_method_register(CONST CHAR_T *method, WUKONG_CRON_RPC_HANDLER handler);
/**
 * @brief Unregister a JSON-RPC method handler.
 *
 * @param[in] method JSON-RPC method name.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_cron_rpc_method_unregister(CONST CHAR_T *method);
/**
 * @brief Execute a JSON-RPC request object and build a response object.
 *
 * @param[in]  request   Input JSON-RPC request object.
 * @param[out] response  Output JSON-RPC response object.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_cron_rpc_execute_json(CONST ty_cJSON *request, ty_cJSON **response);
/**
 * @brief Execute a JSON-RPC request string and return an unformatted response string.
 *
 * @param[in]  request_json   Unformatted JSON-RPC request string.
 * @param[out] response_json  Unformatted JSON-RPC response string allocated by cJSON.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_cron_rpc_execute_string(CONST CHAR_T *request_json, CHAR_T **response_json);

#ifdef __cplusplus
}
#endif

#endif /* __WUKONG_CRON_RPC_H__ */
