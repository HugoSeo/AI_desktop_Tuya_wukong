/**
 * @file xiaozhi_server.h
 * @brief XiaoZhi AI HTTP business layer - Configuration and activation
 * @date 2026-01-13
 * @author linch
 */

#ifndef XIAOZHI_SERVER_H
#define XIAOZHI_SERVER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Constants definition */
#define XIAOZHI_ACTIVATION_CODE_LEN 8
#define XIAOZHI_CHALLENGE_MAX_LEN 128
#define XIAOZHI_MESSAGE_MAX_LEN 256

/* Configuration structure */
typedef struct {
    // Activation info
    struct {
        bool has_challenge;                             // Whether activation needed
        char code[XIAOZHI_ACTIVATION_CODE_LEN];        // Activation code (6 digits)
        char challenge[XIAOZHI_CHALLENGE_MAX_LEN];     // Activation challenge string
        char message[XIAOZHI_MESSAGE_MAX_LEN];         // Activation prompt message
    } activation;
    
    // MQTT Configuration
    struct {
        bool has_config;           // Whether has MQTT configuration
        char endpoint[128];        // MQTT server endpoint
        char client_id[128];       // Client ID
        char username[128];        // Username
        char password[128];        // Password
        char publish_topic[64];    // Publish topic
        char subscribe_topic[64];  // Subscribe topic
    } mqtt;
    
    // WebSocket Configuration
    struct {
        bool has_config;           // Whether has configuration
        char url[256];             // WebSocket URL
        char token[256];           // Authentication token
    } websocket;
    
    // Server time
    struct {
        bool has_time;             // Whether has time info
        int64_t timestamp;         // Unix timestamp (milliseconds)
        int32_t timezone_offset;   // Timezone offset (minutes)
    } server_time;
    
    // Firmware info (OTA, not yet implemented)
    struct {
        bool has_new_version;      // Whether has new version
        char version[32];          // Version number
        char url[256];             // Download URL
    } firmware;

    // Tuya Cloud Configuration (for device authentication)
    // Filled by tuya_xiaozhi_server_check_config() via thing.solution.gateway.entrance.get (2.0)
    struct {
        bool has_config;           // Whether has Tuya configuration
        char token[512];           // Device token (2.0: deviceToken / HMAC)
        char auth_type[16];        // Auth type (2.0: "HMAC")
        char url[512];             // Gateway WebSocket URL (full, with path: ws://IP:PORT/smart/speech)
    } tuya;

} xiaozhi_server_config_t;

/**
 * @brief Check server configuration and get latest config
 * 
 * @return int OPRT_OK on success, other values indicate error code
 * 
 * @note Configuration is dynamically allocated via malloc, get via xiaozhi_server_get_config()
 * @note This function is called on each startup to get latest config from server
 */
int xiaozhi_server_check_config(void);

/**
 * @brief Query device activation status
 * 
 * @return int OPRT_OK on activation success, other values indicate error code
 *   - OPRT_OK: Activation successful or not required
 *   - OPRT_RESOURCE_NOT_READY: Pending user input (HTTP 202)
 *   - OPRT_NOT_FOUND: Server config not available
 *   - Other: HTTP request or network error
 * 
 * @note Internally uses cached configuration info
 */
int xiaozhi_server_check_activate(void);

/**
 * @brief Get cached server configuration info
 * 
 * @return xiaozhi_server_config_t* Configuration pointer, NULL if no configuration
 * 
 * @note Must call xiaozhi_server_check_config() first to get configuration
 * @note This interface is for protocol layer and application layer use
 */
xiaozhi_server_config_t *xiaozhi_server_get_config(void);

/**
 * @brief Allocate an empty (zeroed) server config without any HTTP request.
 *
 * @return xiaozhi_server_config_t* Cached config pointer, NULL on malloc failure.
 *
 * @note For the Tuya gateway flow (cube), which does not contact the public
 *       xiaozhi/tenclass server: allocate a config here, then fill the Tuya
 *       fields via tuya_xiaozhi_server_check_config(). Idempotent — returns the
 *       existing config if one was already allocated.
 */
xiaozhi_server_config_t *xiaozhi_server_alloc_config(void);

/**
 * @brief Get Tuya cloud configuration for device authentication
 * 
 * @param config Pointer to server config (will update tuya field)
 * @return int OPRT_OK on success, error code otherwise
 * 
 * @note Queries Tuya cloud API to get device JWT token and gateway URL
 * @note This is a helper function for device authentication flow
 */
int tuya_xiaozhi_server_check_config(xiaozhi_server_config_t *config);

#ifdef __cplusplus
}
#endif

#endif // XIAOZHI_SERVER_H
