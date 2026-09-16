/**
 * @file xiaozhi_server.c
 * @brief XiaoZhi AI HTTP business layer implementation
 * @date 2026-01-13
 * @author linch
 */

#include "xiaozhi_server.h"
#include "xiaozhi_config.h"
#include "xiaozhi_board.h"
#include "tuya_simple_http.h"
#include "tuya_cloud_types.h"
#include "tuya_error_code.h"
#include "tal_log.h"
#include "tal_hash.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>


/* Copy a JSON string field into a fixed char-array dst (no-op if missing / not a string).
 * dst MUST be an array (sizeof(dst) is the capacity), not a pointer. */
#define COPY_JSON_STR(dst, obj, key) do {                       \
    cJSON *_it = cJSON_GetObjectItem((obj), (key));             \
    if (_it && cJSON_IsString(_it)) {                           \
        strncpy((dst), _it->valuestring, sizeof(dst) - 1);      \
    }                                                           \
} while (0)

/* Static global config pointer (for protocol layer) - dynamically allocated */
static xiaozhi_server_config_t *s_server_config = NULL;

/* Forward declarations */
static char *GetActivationPayload(const xiaozhi_server_config_t *config);
static int CalculateHMAC(const uint8_t *key, uint32_t key_len,
                         const uint8_t *data, uint32_t data_len,
                         char *output_hex);
static void xiaozhi_add_headers(http_session_t session, void *data);

/******************************************************************************
 * Helper functions implementation
 ******************************************************************************/

/**
 * @brief Get check version URL
 * 
 * @return const char* OTA URL string
 */
static const char *GetCheckVersionUrl(void)
{
    // TODO: Read custom URL from Flash/KV storage
    // const char *custom_url = read_ota_url_from_flash();
    // if (custom_url && strlen(custom_url) > 0) {
    //     return custom_url;
    // }
    
    // Return default configuration URL
    return XIAOZHI_SERVER_URL;
}

/**
 * @brief Calculate HMAC-SHA256
 */
static int CalculateHMAC(const uint8_t *key, uint32_t key_len,
                         const uint8_t *data, uint32_t data_len,
                         char *output_hex)
{
    uint8_t hmac_result[32];  // SHA-256 outputs 32 bytes
    
    // Use TuyaOS interface to calculate HMAC-SHA256
    int ret = tal_sha256_mac(key, key_len, data, data_len, hmac_result);
    if (ret != OPRT_OK) {
        TAL_PR_ERR("HMAC calculation failed: %d", ret);
        return ret;
    }
    
    // Convert to hex string
    for (uint32_t i = 0; i < 32; i++) {
        sprintf(output_hex + i * 2, "%02x", hmac_result[i]);
    }
    output_hex[64] = '\0';
    
    return OPRT_OK;
}

/**
 * @brief Generate activation payload
 * 
 * @param config Configuration info
 * @return char* JSON string (allocated with SIMPLE_HTTP_MALLOC, caller must free with SIMPLE_HTTP_FREE)
 * 
 * @note JSON format estimated size:
 *       - Fixed part: {"algorithm":"hmac-sha256","serial_number":"","challenge":"","hmac":""} ≈ 70 bytes
 *       - serial_number: 32 bytes
 *       - challenge: max 128 bytes
 *       - hmac: 64 bytes
 *       - Total: ~294 bytes, pre-allocate 512 bytes
 */
static char *GetActivationPayload(const xiaozhi_server_config_t *config)
{
    // Pre-allocate fixed size (512 bytes is enough for max possible JSON)
    #define ACTIVATION_PAYLOAD_SIZE 512
    char *json_str = (char *)SIMPLE_HTTP_MALLOC(ACTIVATION_PAYLOAD_SIZE);
    if (!json_str) {
        TAL_PR_ERR("Failed to allocate memory for activation payload");
        return NULL;
    }
    
    xiaozhi_board_t *board = xiaozhi_board_get_instance();
    
    // Version 1: No serial number, return empty payload
    if (!board->has_serial_number) {
        strcpy(json_str, "{}");
        return json_str;
    }
    
    // Version 2: Include HMAC signature
    // Check configuration validity
    if (!config || !config->activation.has_challenge) {
        TAL_PR_ERR("No activation challenge available");
        strcpy(json_str, "{}");
        return json_str;
    }
    
    // Calculate HMAC
    char hmac_hex[65] = {0};
    int ret = CalculateHMAC((uint8_t *)board->serial_number, 
                           XIAOZHI_SERIAL_NUMBER_LEN,
                           (uint8_t *)config->activation.challenge,
                           strlen(config->activation.challenge),
                           hmac_hex);
    if (ret != OPRT_OK) {
        TAL_PR_ERR("Failed to calculate HMAC");
        strcpy(json_str, "{}");
        return json_str;
    }
    
    // Build JSON string
    // format: {"algorithm":"hmac-sha256","serial_number":"...","challenge":"...","hmac":"..."}
    snprintf(json_str, ACTIVATION_PAYLOAD_SIZE,
             "{\"algorithm\":\"hmac-sha256\","
             "\"serial_number\":\"%s\","
             "\"challenge\":\"%s\","
             "\"hmac\":\"%s\"}",
             board->serial_number,
             config->activation.challenge,
             hmac_hex);
    
    return json_str;
}

/**
 * @brief Custom HTTP request header callback
 */
static void xiaozhi_add_headers(http_session_t session, void *data)
{
    xiaozhi_board_t *board = xiaozhi_board_get_instance();
    
    http_add_header(session, NULL, "Activation-Version", 
                    board->has_serial_number ? "2" : "1");
    http_add_header(session, NULL, "Device-Id", board->mac_address);     // MAC address
    http_add_header(session, NULL, "Client-Id", board->uuid);            // UUID v4
    
    if (board->has_serial_number) {
        http_add_header(session, NULL, "Serial-Number", board->serial_number);
    }
    
    http_add_header(session, NULL, "Accept-Language", "zh-CN");
}

/******************************************************************************
 * Public interface implementation
 ******************************************************************************/

/**
 * @brief Allocate an empty server config without any HTTP request (Tuya gateway flow).
 */
xiaozhi_server_config_t *xiaozhi_server_alloc_config(void)
{
    if (s_server_config == NULL) {
        s_server_config = (xiaozhi_server_config_t *)SIMPLE_HTTP_MALLOC(sizeof(xiaozhi_server_config_t));
        if (s_server_config) {
            memset(s_server_config, 0, sizeof(xiaozhi_server_config_t));
        } else {
            TAL_PR_ERR("Failed to allocate config memory");
        }
    }
    return s_server_config;
}

/**
 * @brief Check server configuration and get latest config
 */
int xiaozhi_server_check_config(void)
{
    TAL_PR_INFO("Checking server config...");
    
    int ret = OPRT_OK;
    char *json_body = NULL;
    simple_http_response_t response = {0};
    cJSON *root = NULL;
    xiaozhi_server_config_t *new_config = NULL;
    
    // 1. Allocate new configuration
    new_config = (xiaozhi_server_config_t *)SIMPLE_HTTP_MALLOC(sizeof(xiaozhi_server_config_t));
    if (!new_config) {
        TAL_PR_ERR("Failed to allocate config memory");
        ret = OPRT_MALLOC_FAILED;
        goto exit;
    }
    memset(new_config, 0, sizeof(xiaozhi_server_config_t));
    
    // 2. Build system info JSON
    json_body = xiaozhi_board_get_system_info_json();
    if (!json_body) {
        TAL_PR_ERR("Failed to build system info JSON");
        ret = OPRT_MALLOC_FAILED;
        goto exit;
    }
    
    // 3. Send HTTP POST request
    const char *ota_url = GetCheckVersionUrl();
    /* simple_http refactor: per-request options bundled into simple_http_opts_t */
    simple_http_opts_t opts = {
        .add_head_cb = xiaozhi_add_headers,
        .field_flags = HDR_ADD_CONTENT_TYPE_JSON,
    };
    ret = tuya_simple_http_post(
        ota_url,
        (BYTE_T *)json_body,
        strlen(json_body),
        &opts,
        &response
    );
    
    if (ret != OPRT_OK) {
        TAL_PR_ERR("HTTP POST failed: %d", ret);
        goto exit;
    }
    
    // 4. Check HTTP status code
    TAL_PR_DEBUG("HTTP status code: %d", response.http_code);
    
    if (response.http_code != 200) {
        TAL_PR_ERR("HTTP request failed with status: %d", response.http_code);
        TAL_PR_ERR("Response: %s", response.data ? (char *)response.data : "NULL");
        ret = OPRT_COM_ERROR;
        goto exit;
    }
    
    // 5. Parse JSON response
    TAL_PR_DEBUG("Response: %s", (char *)response.data);
    
    root = cJSON_Parse((char *)response.data);
    if (!root) {
        TAL_PR_ERR("Failed to parse JSON response");
        ret = OPRT_CJSON_PARSE_ERR;
        goto exit;
    }
    
    // 5. Extract configuration info to cache
    // Parse activation info
    cJSON *activation = cJSON_GetObjectItem(root, "activation");
    if (activation && cJSON_IsObject(activation)) {
        new_config->activation.has_challenge = true;

        COPY_JSON_STR(new_config->activation.message,   activation, "message");
        COPY_JSON_STR(new_config->activation.code,      activation, "code");
        COPY_JSON_STR(new_config->activation.challenge, activation, "challenge");

        TAL_PR_NOTICE("Activation required!");
        TAL_PR_NOTICE("  Code: %s", new_config->activation.code);
        TAL_PR_NOTICE("  Message: %s", new_config->activation.message);
    } else {
        new_config->activation.has_challenge = false;
        TAL_PR_INFO("No activation required");
    }
    
    // Parse MQTT configuration
    cJSON *mqtt = cJSON_GetObjectItem(root, "mqtt");
    if (mqtt && cJSON_IsObject(mqtt)) {
        new_config->mqtt.has_config = true;

        COPY_JSON_STR(new_config->mqtt.endpoint,        mqtt, "endpoint");
        COPY_JSON_STR(new_config->mqtt.client_id,       mqtt, "client_id");
        COPY_JSON_STR(new_config->mqtt.username,        mqtt, "username");
        COPY_JSON_STR(new_config->mqtt.password,        mqtt, "password");
        COPY_JSON_STR(new_config->mqtt.publish_topic,   mqtt, "publish_topic");
        COPY_JSON_STR(new_config->mqtt.subscribe_topic, mqtt, "subscribe_topic");

        TAL_PR_INFO("MQTT config: %s", new_config->mqtt.endpoint);
    }
    
    // Parse WebSocket configuration
    cJSON *websocket = cJSON_GetObjectItem(root, "websocket");
    if (websocket && cJSON_IsObject(websocket)) {
        new_config->websocket.has_config = true;

        COPY_JSON_STR(new_config->websocket.url,   websocket, "url");
        COPY_JSON_STR(new_config->websocket.token, websocket, "token");

        TAL_PR_INFO("WebSocket config: %s", new_config->websocket.url);
    }
    
    // Parse server time
    cJSON *server_time = cJSON_GetObjectItem(root, "server_time");
    if (server_time && cJSON_IsObject(server_time)) {
        new_config->server_time.has_time = true;
        
        cJSON *item = cJSON_GetObjectItem(server_time, "timestamp");
        if (item && cJSON_IsNumber(item)) {
            new_config->server_time.timestamp = (int64_t)item->valuedouble;
        }
        
        item = cJSON_GetObjectItem(server_time, "timezone_offset");
        if (item && cJSON_IsNumber(item)) {
            new_config->server_time.timezone_offset = item->valueint;
        }
        
        TAL_PR_DEBUG("Server time: %lld", new_config->server_time.timestamp);
    }
    
    // Parse firmware info (OTA)
    cJSON *firmware = cJSON_GetObjectItem(root, "firmware");
    if (firmware && cJSON_IsObject(firmware)) {
        cJSON *fw_ver = cJSON_GetObjectItem(firmware, "version");
        if (fw_ver && cJSON_IsString(fw_ver)) {
            new_config->firmware.has_new_version = true;
            strncpy(new_config->firmware.version, fw_ver->valuestring, sizeof(new_config->firmware.version) - 1);
            COPY_JSON_STR(new_config->firmware.url, firmware, "url");
            TAL_PR_INFO("New firmware available: %s", new_config->firmware.version);
        }
    }
    
    // 6. Success, replace old configuration
    if (s_server_config) {
        SIMPLE_HTTP_FREE(s_server_config);
    }
    s_server_config = new_config;
    new_config = NULL;  // Avoid being freed in exit
    
    TAL_PR_INFO("Server config check completed successfully");
    ret = OPRT_OK;

exit:
    // Cleanup resources
    if (json_body) {
        cJSON_free(json_body);
    }
    if (response.data) {
        SIMPLE_HTTP_FREE(response.data);
    }
    if (root) {
        cJSON_Delete(root);
    }
    if (new_config) {
        SIMPLE_HTTP_FREE(new_config);
    }
    
    return ret;
}

/**
 * @brief Query device activation status
 */
int xiaozhi_server_check_activate(void)
{
    // Step 1: Check config handle
    if (!s_server_config) {
        TAL_PR_ERR("Server config not available");
        return OPRT_NOT_FOUND;
    }
    
    // Step 2: Check if activation is required
    if (!s_server_config->activation.has_challenge) {
        TAL_PR_DEBUG("No activation required");
        return OPRT_OK;  // Not needed, return success
    }
    
    TAL_PR_INFO("Checking activation status...");
    
    // 1. Generate activation payload
    char *json_payload = GetActivationPayload(s_server_config);
    if (!json_payload) {
        TAL_PR_ERR("Failed to generate activation payload");
        return OPRT_MALLOC_FAILED;
    }
    
    // 2. Build activation URL
    const char *ota_url = GetCheckVersionUrl();
    char activate_url[256];
    snprintf(activate_url, sizeof(activate_url), "%sactivate", ota_url);
    
    // 3. Send HTTP POST request
    simple_http_response_t response = {0};
    /* simple_http refactor: per-request options bundled into simple_http_opts_t */
    simple_http_opts_t opts = {
        .add_head_cb = xiaozhi_add_headers,
        .field_flags = HDR_ADD_CONTENT_TYPE_JSON,
    };
    int ret = tuya_simple_http_post(
        activate_url,
        (BYTE_T *)json_payload,
        strlen(json_payload),
        &opts,
        &response
    );
    
    SIMPLE_HTTP_FREE(json_payload);
    
    if (ret != OPRT_OK) {
        TAL_PR_ERR("HTTP POST failed: %d", ret);
        return ret;
    }
    
    // 4. Return result based on HTTP status code
    TAL_PR_DEBUG("HTTP status code: %d", response.http_code);
    TAL_PR_DEBUG("HTTP Response: %s", response.data ? (char *)response.data : "NULL");

    int result;
    if (response.http_code == 200) {
        TAL_PR_NOTICE("Activation successful!");
        result = OPRT_OK;
    } else if (response.http_code == 202) {
        TAL_PR_INFO("Activation pending, waiting for user input...");
        result = OPRT_RESOURCE_NOT_READY;
    } else {
        TAL_PR_ERR("Activation failed with status: %d", response.http_code);
        result = OPRT_COM_ERROR;
    }
    
    SIMPLE_HTTP_FREE(response.data);
    return result;
}

/* ========== Configuration access (for protocol layer use) ========== */

xiaozhi_server_config_t *xiaozhi_server_get_config(void)
{
    if (!s_server_config) {
        TAL_PR_WARN("No cached configuration available");
        return NULL;
    }
    
    return s_server_config;
}

#include "tuya_iot_internal_api.h"

/**
 * @brief Get Tuya cloud configuration for XiaoZhi device authentication
 * 
 * This function queries Tuya cloud API to get device JWT token and gateway URL.
 * The configuration is used for XiaoZhi AI device authentication and connection.
 * 
 * @param config Pointer to server config structure (will update tuya field)
 * @return OPERATE_RET OPRT_OK on success, error code otherwise
 * 
 * @note API endpoint: thing.solution.gateway.entrance.get
 * @note Response is pre-parsed result object with deviceJwtToken and gatewayEntranceUrl
 * @note gatewayEntranceUrl format: ws://IP:PORT/smart/speech (complete WebSocket URL)
 */
int tuya_xiaozhi_server_check_config(xiaozhi_server_config_t *config)
{
    if (!config) {
        TAL_PR_ERR("Invalid parameter: config is NULL");
        return OPRT_INVALID_PARM;
    }
    
    OPERATE_RET ret = OPRT_OK;
    ty_cJSON *result = NULL;

    TAL_PR_INFO("Querying Tuya cloud config for XiaoZhi device...");
    
    /* thing.solution.gateway.entrance.get 2.0:
     *   result = { "authType":"HMAC", "deviceToken":"...", "gatewayEntranceUrl":"ws://IP:PORT/smart/speech" }
     * 2.0 returns a complete gateway URL *with path* (the 1.0 URL had no path -> 403). */
    ret = iot_httpc_common_post_simple("thing.solution.gateway.entrance.get", "2.0", NULL, NULL, &result);
    if (ret != OPRT_OK) {
        TAL_PR_ERR("Failed to query Tuya cloud API: %d", ret);
        return ret;
    }

    if (!result) {
        TAL_PR_ERR("Empty response from Tuya cloud");
        return OPRT_CJSON_PARSE_ERR;
    }

    // Extract auth type (2.0: "HMAC")
    ty_cJSON *auth_type = ty_cJSON_GetObjectItem(result, "authType");
    if (auth_type && auth_type->valuestring) {
        strncpy(config->tuya.auth_type, auth_type->valuestring, sizeof(config->tuya.auth_type) - 1);
        config->tuya.auth_type[sizeof(config->tuya.auth_type) - 1] = '\0';
        TAL_PR_INFO("Auth type: %s", config->tuya.auth_type);
    } else {
        config->tuya.auth_type[0] = '\0';
    }

    // Extract device token (2.0: deviceToken; fall back to 1.0 deviceJwtToken)
    ty_cJSON *token = ty_cJSON_GetObjectItem(result, "deviceToken");
    if (!token || !token->valuestring) {
        token = ty_cJSON_GetObjectItem(result, "deviceJwtToken");
    }
    if (token && token->valuestring) {
        strncpy(config->tuya.token, token->valuestring, sizeof(config->tuya.token) - 1);
        config->tuya.token[sizeof(config->tuya.token) - 1] = '\0';
        TAL_PR_DEBUG("Device token: %.40s...", config->tuya.token);
    } else {
        TAL_PR_WARN("Missing deviceToken in response");
        config->tuya.token[0] = '\0';
    }

    // Extract gatewayEntranceUrl directly from result (complete WebSocket URL with path)
    ty_cJSON *url = ty_cJSON_GetObjectItem(result, "gatewayEntranceUrl");
    if (url && url->valuestring) {
        strncpy(config->tuya.url, url->valuestring, sizeof(config->tuya.url) - 1);
        config->tuya.url[sizeof(config->tuya.url) - 1] = '\0';
        TAL_PR_INFO("Gateway WebSocket URL: %s", config->tuya.url);
    } else {
        TAL_PR_WARN("Missing gatewayEntranceUrl in response");
        config->tuya.url[0] = '\0';
    }

    // Mark as configured only when we have a usable gateway URL
    config->tuya.has_config = (config->tuya.url[0] != '\0');
    
    if (config->tuya.has_config) {
        TAL_PR_NOTICE("Tuya cloud config retrieved successfully");
    } else {
        TAL_PR_WARN("No Tuya config in response");
    }
    
    ty_cJSON_Delete(result);
    return OPRT_OK;
}
