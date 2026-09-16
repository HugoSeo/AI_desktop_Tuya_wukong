/**
 * @file xiaozhi_board.c
 * @brief XiaoZhi AI board abstraction layer implementation - Device information only
 * @date 2026-01-13
 * @author linch
 */

#include "xiaozhi_board.h"
#include "tuya_cloud_types.h"
#include "tuya_error_code.h"
#include "tal_log.h"
#include "tal_system.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

// External interface (TuyaOS system functions)
extern const char *get_gw_uuid(void);
extern char *tuya_get_serialno(void);
extern const char *get_gw_ssid(void);

/* Configuration */
#ifndef CHIP_MODEL
#define CHIP_MODEL "bk7258"
#endif

#ifndef USER_SW_VER
#define USER_SW_VER "1.0.0"
#endif

#ifndef APP_BIN_NAME
#define APP_BIN_NAME "tuyaos_demo_xiaozhi_ai"
#endif


/* Global variables */
static xiaozhi_board_t s_xiaozhi_board = {0};
static bool s_initialized = false;

/* Forward declarations */
static char *GetUuid(void);
static char *GetMacAddress(void);
static char *GetBoardType(void);
static char *GetBoardName(void);
static char *GetNetworkInfo(void);
static int LoadSerialNumber(void);

/******************************************************************************
 * Helper functions implementation
 ******************************************************************************/

/**
 * @brief Get and convert UUID
 * 
 * @return char* UUID string (format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx), NULL on failure
 */
static char *GetUuid(void)
{
    // Return directly if already converted
    if (strlen(s_xiaozhi_board.uuid) > 0) {
        return s_xiaozhi_board.uuid;
    }
    
    // Get TuyaOS system UUID (16 bytes raw data)
    const char *gw_uuid = get_gw_uuid();
    if (!gw_uuid) {
        TAL_PR_ERR("Failed to get gw uuid");
        return NULL;
    }
    
    // Copy to temp buffer (need to modify version and variant bits)
    uint8_t uuid_bytes[16];
    memcpy(uuid_bytes, gw_uuid, 16);
    
    // Set version bits (version 4)
    uuid_bytes[6] = (uuid_bytes[6] & 0x0F) | 0x40;
    
    // Set variant bits (variant 1)
    uuid_bytes[8] = (uuid_bytes[8] & 0x3F) | 0x80;
    
    // Convert to standard UUID string format (xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx)
    snprintf(s_xiaozhi_board.uuid, sizeof(s_xiaozhi_board.uuid),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             uuid_bytes[0], uuid_bytes[1], uuid_bytes[2], uuid_bytes[3],
             uuid_bytes[4], uuid_bytes[5], uuid_bytes[6], uuid_bytes[7],
             uuid_bytes[8], uuid_bytes[9], uuid_bytes[10], uuid_bytes[11],
             uuid_bytes[12], uuid_bytes[13], uuid_bytes[14], uuid_bytes[15]);
    
    TAL_PR_DEBUG("Generated UUID: %s", s_xiaozhi_board.uuid);
    return s_xiaozhi_board.uuid;
}

/**
 * @brief Get and format MAC address
 * 
 * @return char* MAC address string (format: XX:XX:XX:XX:XX:XX), NULL on failure
 */
static char *GetMacAddress(void)
{
    // Return directly if already formatted
    if (s_xiaozhi_board.mac_address[0] != '\0') {
        return s_xiaozhi_board.mac_address;
    }
    
    // Get MAC address without colons (format: "112233445566")
    char *serialno = tuya_get_serialno();
    if (!serialno) {
        TAL_PR_ERR("Failed to get serial number");
        return NULL;
    }
    
    // Check input length (should be 12 characters)
    if (strlen(serialno) != 12) {
        TAL_PR_ERR("Invalid MAC address length: %d", strlen(serialno));
        return NULL;
    }
    
    // Format to XX:XX:XX:XX:XX:XX
    snprintf(s_xiaozhi_board.mac_address, sizeof(s_xiaozhi_board.mac_address),
             "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
             serialno[0], serialno[1],
             serialno[2], serialno[3],
             serialno[4], serialno[5],
             serialno[6], serialno[7],
             serialno[8], serialno[9],
             serialno[10], serialno[11]);
    
    TAL_PR_DEBUG("Formatted MAC address: %s", s_xiaozhi_board.mac_address);
    return s_xiaozhi_board.mac_address;
}

/**
 * @brief Get board type
 */
static char *GetBoardType(void)
{
    return "wifi";
}

/**
 * @brief Get board name
 */
static char *GetBoardName(void)
{
    return "xiaozhi-board";
}

/**
 * @brief Get network info
 */
static char *GetNetworkInfo(void)
{
    const char *ssid = get_gw_ssid();
    return (char *)ssid;
}

/**
 * @brief Load serial number
 */
static int LoadSerialNumber(void)
{
    // TODO: Return false for now, debug version 1 (no serial number) first
    // Can read serial number from:
    // - Method 1: Read from auth certificate
    // - Method 2: Read from specific Flash partition
    // - Method 3: Read from KV storage
    
    s_xiaozhi_board.has_serial_number = false;
    memset(s_xiaozhi_board.serial_number, 0, sizeof(s_xiaozhi_board.serial_number));
    
    TAL_PR_DEBUG("Serial number not available, using activation version 1");
    return OPRT_NOT_FOUND;
}

/******************************************************************************
 * Public interface implementation
 ******************************************************************************/

/**
 * @brief Get device instance
 */
xiaozhi_board_t *xiaozhi_board_get_instance(void)
{
    if (!s_initialized) {
        memset(&s_xiaozhi_board, 0, sizeof(s_xiaozhi_board));
        
        // Initialize operation interface
        s_xiaozhi_board.ops.GetUuid = GetUuid;
        s_xiaozhi_board.ops.GetMacAddress = GetMacAddress;
        s_xiaozhi_board.ops.GetBoardType = GetBoardType;
        s_xiaozhi_board.ops.GetBoardName = GetBoardName;
        s_xiaozhi_board.ops.GetNetworkInfo = GetNetworkInfo;
        
        // Initialize device ID
        GetUuid();         // Initialize UUID
        GetMacAddress();   // Initialize MAC address
        
        // Load serial number
        LoadSerialNumber();
        
        s_initialized = true;
        
        TAL_PR_INFO("XiaoZhi board initialized");
        TAL_PR_INFO("  UUID: %s", s_xiaozhi_board.uuid);
        TAL_PR_INFO("  MAC: %s", s_xiaozhi_board.mac_address);
        TAL_PR_INFO("  Activation version: %d", s_xiaozhi_board.has_serial_number ? 2 : 1);
    }
    
    return &s_xiaozhi_board;
}

/**
 * @brief Build system info JSON
 */
char *xiaozhi_board_get_system_info_json(void)
{
    // Ensure initialization
    xiaozhi_board_get_instance();
    
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        TAL_PR_ERR("Failed to create JSON object");
        return NULL;
    }
    
    // Basic info
    cJSON_AddNumberToObject(root, "version", 2);
    cJSON_AddStringToObject(root, "language", "zh-CN");
    cJSON_AddStringToObject(root, "mac_address", GetMacAddress());
    cJSON_AddStringToObject(root, "chip_model_name", CHIP_MODEL);
    cJSON_AddStringToObject(root, "uuid", GetUuid());
    
    // Optional: Flash size and remaining memory (add as needed)
    // cJSON_AddNumberToObject(root, "flash_size", get_flash_size());
    // cJSON_AddNumberToObject(root, "minimum_free_heap_size", tal_system_get_free_heap_size());
    
    // Application info
    cJSON *app = cJSON_CreateObject();
    if (app) {
        cJSON_AddStringToObject(app, "name", APP_BIN_NAME);
        cJSON_AddStringToObject(app, "version", USER_SW_VER);
        cJSON_AddStringToObject(app, "compile_time", __DATE__ "T" __TIME__ "Z");
        cJSON_AddItemToObject(root, "application", app);
    }
    
    // Board info
    cJSON *board = cJSON_CreateObject();
    if (board) {
        cJSON_AddStringToObject(board, "type", GetBoardType());
        cJSON_AddStringToObject(board, "name", GetBoardName());
        
        const char *ssid = GetNetworkInfo();
        if (ssid) {
            cJSON_AddStringToObject(board, "ssid", ssid);
        }
        cJSON_AddStringToObject(board, "mac", GetMacAddress());
        // Optional: Add RSSI, IP and other network info
        
        cJSON_AddItemToObject(root, "board", board);
    }
    
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (json_str) {
        TAL_PR_DEBUG("System info JSON: %s", json_str);
    }
    
    return json_str;  // Caller must free with cJSON_free
}
