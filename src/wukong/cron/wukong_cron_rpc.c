/**
 * @file wukong_cron_rpc.c
 * @brief Wukong local JSON-RPC 2.0 executor implementation.
 *
 * This module provides a minimal local JSON-RPC 2.0 dispatcher for cron jobs.
 * It owns a fixed-size method registry, validates request structure, and builds
 * standard `result` / `error` response objects for local execution.
 */

#include "wukong_cron_rpc.h"

#include <string.h>

#include "tal_log.h"
#include "tal_memory.h"
#include "utilities/mix_method.h"

/* ---------------------------------------------------------------------------
 * Runtime method registry
 * --------------------------------------------------------------------------- */
/**
 * @brief Fixed-size local registry for cron-exposed JSON-RPC methods.
 */
#define WUKONG_CRON_RPC_MAX_METHODS 16

/**
 * @brief One method entry in the local JSON-RPC registry.
 */
typedef struct {
    /** Registered JSON-RPC method name. */
    CHAR_T *method;
    /** Method callback bound to `method`. */
    WUKONG_CRON_RPC_HANDLER handler;
} WUKONG_CRON_RPC_ENTRY_T;

STATIC WUKONG_CRON_RPC_ENTRY_T s_rpc_entries[WUKONG_CRON_RPC_MAX_METHODS];

/* ---------------------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------------------- */
/**
 * @brief Look up a registered method by name.
 *
 * @param[in] method Target method name.
 * @return Slot index on success, `-1` when not found.
 */
STATIC INT_T __find_method_index(CONST CHAR_T *method)
{
    INT_T index = 0;

    if (method == NULL) {
        return -1;
    }

    for (index = 0; index < WUKONG_CRON_RPC_MAX_METHODS; index++) {
        if (s_rpc_entries[index].method != NULL &&
            strcmp(s_rpc_entries[index].method, method) == 0) {
            return index;
        }
    }

    return -1;
}

/**
 * @brief Duplicate the request id when possible, otherwise return JSON null.
 *
 * @param[in] id Request id node.
 * @return Duplicated id node or JSON null.
 */
STATIC ty_cJSON *__duplicate_id_or_null(CONST ty_cJSON *id)
{
    if (id == NULL) {
        return ty_cJSON_CreateNull();
    }

    return ty_cJSON_Duplicate(id, 1);
}

/**
 * @brief Build a standard JSON-RPC error response object.
 *
 * @param[in]  id       Request id node.
 * @param[in]  code     JSON-RPC error code.
 * @param[in]  msg      JSON-RPC error message.
 * @param[out] response Output response object.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __build_error_response(CONST ty_cJSON *id, INT_T code,
                                          CONST CHAR_T *msg, ty_cJSON **response)
{
    ty_cJSON *root = NULL;
    ty_cJSON *error = NULL;

    TUYA_CHECK_NULL_RETURN(response, OPRT_INVALID_PARM);

    root = ty_cJSON_CreateObject();
    error = ty_cJSON_CreateObject();
    if (root == NULL || error == NULL) {
        ty_cJSON_Delete(root);
        ty_cJSON_Delete(error);
        return OPRT_MALLOC_FAILED;
    }

    ty_cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    ty_cJSON_AddItemToObject(root, "id", __duplicate_id_or_null(id));
    ty_cJSON_AddNumberToObject(error, "code", code);
    ty_cJSON_AddStringToObject(error, "message", msg ? msg : "Unknown error");
    ty_cJSON_AddItemToObject(root, "error", error);

    *response = root;
    return OPRT_OK;
}

/**
 * @brief Build a standard JSON-RPC success response object.
 *
 * @param[in]  id       Request id node.
 * @param[in]  result   JSON-RPC result object, ownership transferred on success.
 * @param[out] response Output response object.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __build_result_response(CONST ty_cJSON *id, ty_cJSON *result,
                                           ty_cJSON **response)
{
    ty_cJSON *root = NULL;

    TUYA_CHECK_NULL_RETURN(response, OPRT_INVALID_PARM);

    root = ty_cJSON_CreateObject();
    if (root == NULL) {
        ty_cJSON_Delete(result);
        return OPRT_MALLOC_FAILED;
    }

    ty_cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    ty_cJSON_AddItemToObject(root, "id", __duplicate_id_or_null(id));
    if (result != NULL) {
        ty_cJSON_AddItemToObject(root, "result", result);
    } else {
        ty_cJSON_AddItemToObject(root, "result", ty_cJSON_CreateObject());
    }

    *response = root;
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------- */
/**
 * @brief Initialize the local JSON-RPC method registry.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_cron_rpc_init(VOID)
{
    memset(s_rpc_entries, 0, sizeof(s_rpc_entries));
    return OPRT_OK;
}

/**
 * @brief Release all registered local methods.
 */
VOID wukong_cron_rpc_deinit(VOID)
{
    INT_T index = 0;

    for (index = 0; index < WUKONG_CRON_RPC_MAX_METHODS; index++) {
        tal_free(s_rpc_entries[index].method);
        s_rpc_entries[index].method = NULL;
        s_rpc_entries[index].handler = NULL;
    }
    return;
}

/**
 * @brief Register one local JSON-RPC method callback.
 *
 * @param[in] method  JSON-RPC method name.
 * @param[in] handler Method callback.
 */
OPERATE_RET wukong_cron_rpc_method_register(CONST CHAR_T *method, WUKONG_CRON_RPC_HANDLER handler)
{
    INT_T index = 0;

    TUYA_CHECK_NULL_RETURN(method, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(handler, OPRT_INVALID_PARM);

    index = __find_method_index(method);
    if (index >= 0) {
        s_rpc_entries[index].handler = handler;
        return OPRT_OK;
    }

    for (index = 0; index < WUKONG_CRON_RPC_MAX_METHODS; index++) {
        if (s_rpc_entries[index].method == NULL) {
            s_rpc_entries[index].method = mm_strdup(method);
            if (s_rpc_entries[index].method == NULL) {
                return OPRT_MALLOC_FAILED;
            }
            s_rpc_entries[index].handler = handler;
            return OPRT_OK;
        }
    }

    return OPRT_COM_ERROR;
}

/**
 * @brief Unregister one local JSON-RPC method callback.
 *
 * @param[in] method JSON-RPC method name.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_cron_rpc_method_unregister(CONST CHAR_T *method)
{
    INT_T index = 0;

    TUYA_CHECK_NULL_RETURN(method, OPRT_INVALID_PARM);

    index = __find_method_index(method);
    if (index < 0) {
        return OPRT_NOT_FOUND;
    }

    tal_free(s_rpc_entries[index].method);
    s_rpc_entries[index].method = NULL;
    s_rpc_entries[index].handler = NULL;

    return OPRT_OK;
}

/**
 * @brief Execute one JSON-RPC request object and construct a response object.
 *
 * @param[in]  request   Input JSON-RPC request object.
 * @param[out] response  Output JSON-RPC response object.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_cron_rpc_execute_json(CONST ty_cJSON *request, ty_cJSON **response)
{
    ty_cJSON *jsonrpc = NULL;
    ty_cJSON *id = NULL;
    ty_cJSON *method = NULL;
    ty_cJSON *params = NULL;
    WUKONG_CRON_RPC_HANDLER handler = NULL;
    ty_cJSON *result = NULL;
    INT_T index = 0;
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(request, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(response, OPRT_INVALID_PARM);

    *response = NULL;

    if (!ty_cJSON_IsObject(request)) {
        return __build_error_response(NULL, WUKONG_CRON_RPC_ERR_INVALID_REQUEST,
                                      "Invalid JSON-RPC request", response);
    }

    jsonrpc = ty_cJSON_GetObjectItem(request, "jsonrpc");
    if (!ty_cJSON_IsString(jsonrpc) || strcmp(jsonrpc->valuestring, "2.0") != 0) {
        return __build_error_response(NULL, WUKONG_CRON_RPC_ERR_INVALID_REQUEST,
                                      "Invalid JSON-RPC version", response);
    }

    id = ty_cJSON_GetObjectItem(request, "id");
    if (id != NULL && !ty_cJSON_IsString(id) && !ty_cJSON_IsNumber(id) && !ty_cJSON_IsNull(id)) {
        return __build_error_response(NULL, WUKONG_CRON_RPC_ERR_INVALID_REQUEST,
                                      "Invalid id", response);
    }

    method = ty_cJSON_GetObjectItem(request, "method");
    if (!ty_cJSON_IsString(method) || method->valuestring == NULL) {
        return __build_error_response(id, WUKONG_CRON_RPC_ERR_INVALID_REQUEST,
                                      "Missing method", response);
    }

    params = ty_cJSON_GetObjectItem(request, "params");
    if (params != NULL && !ty_cJSON_IsObject(params)) {
        return __build_error_response(id, WUKONG_CRON_RPC_ERR_INVALID_PARAMS,
                                      "Params must be an object", response);
    }

    index = __find_method_index(method->valuestring);
    if (index < 0) {
        return __build_error_response(id, WUKONG_CRON_RPC_ERR_METHOD_NOT_FOUND,
                                      "Method not found", response);
    }

    handler = s_rpc_entries[index].handler;
    rt = handler(params, &result);
    if (rt != OPRT_OK) {
        ty_cJSON_Delete(result);
        return __build_error_response(id, WUKONG_CRON_RPC_ERR_INTERNAL,
                                      "Method execution failed", response);
    }

    return __build_result_response(id, result, response);
}

/**
 * @brief Execute one JSON-RPC request string and serialize the response.
 *
 * @param[in]  request_json   Unformatted JSON-RPC request string.
 * @param[out] response_json  Unformatted JSON-RPC response string allocated by cJSON.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_cron_rpc_execute_string(CONST CHAR_T *request_json, CHAR_T **response_json)
{
    ty_cJSON *request = NULL;
    ty_cJSON *response = NULL;
    CHAR_T *response_str = NULL;
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(request_json, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(response_json, OPRT_INVALID_PARM);

    *response_json = NULL;

    request = ty_cJSON_Parse(request_json);
    if (request == NULL) {
        rt = __build_error_response(NULL, WUKONG_CRON_RPC_ERR_PARSE,
                                    "Parse error", &response);
        if (rt != OPRT_OK) {
            return rt;
        }
    } else {
        rt = wukong_cron_rpc_execute_json(request, &response);
        ty_cJSON_Delete(request);
        if (rt != OPRT_OK) {
            return rt;
        }
    }

    response_str = ty_cJSON_PrintUnformatted(response);
    ty_cJSON_Delete(response);
    if (response_str == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    *response_json = response_str;
    return OPRT_OK;
}
