/**
 * @file xiaozhi_board.h
 * @brief XiaoZhi AI board abstraction layer - Device information only
 * @date 2026-01-13
 * @author linch
 */

#ifndef XIAOZHI_BOARD_H
#define XIAOZHI_BOARD_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Constants definition */
#define XIAOZHI_UUID_MAX_LEN 64
#define XIAOZHI_SERIAL_NUMBER_LEN 32

/* Device operation interface */
typedef struct {
    char *(*GetUuid)(void);                    // Get device UUID
    char *(*GetMacAddress)(void);              // Get MAC address
    char *(*GetBoardType)(void);               // Get board type
    char *(*GetBoardName)(void);               // Get board name
    char *(*GetNetworkInfo)(void);             // Get network info (SSID etc)
} xiaozhi_board_ops_t;

/* Device instance */
typedef struct {
    char mac_address[18];                                  // MAC address (format: XX:XX:XX:XX:XX:XX)
    char uuid[XIAOZHI_UUID_MAX_LEN];                      // UUID v4 (converted to standard format)
    char serial_number[XIAOZHI_SERIAL_NUMBER_LEN + 1];    // Serial number (32 bytes + null)
    bool has_serial_number;                                // Whether has serial number

    xiaozhi_board_ops_t ops;                              // Operation interface
} xiaozhi_board_t;

/**
 * @brief Get device instance (singleton pattern)
 * 
 * @return xiaozhi_board_t* Device instance pointer
 */
xiaozhi_board_t *xiaozhi_board_get_instance(void);

/**
 * @brief Build system info JSON
 * 
 * @return char* System info JSON string (caller must free with cJSON_free)
 */
char *xiaozhi_board_get_system_info_json(void);

#ifdef __cplusplus
}
#endif

#endif // XIAOZHI_BOARD_H
