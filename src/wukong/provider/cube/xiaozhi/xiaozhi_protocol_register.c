/**
 * @file xiaozhi_protocol_register.c
 * @brief XiaoZhi AI Protocol registration (Factory pattern)
 * @date 2026-01-13
 * @author linch
 */

#include "xiaozhi_protocol.h"
#include "tuya_error_code.h"
#include "tal_log.h"

/* ========== Forward declarations ========== */

// WebSocket Protocol registration function (returns allocated protocol pointer)
extern xiaozhi_protocol_t *xiaozhi_protocol_websocket_register(void);

/* ========== Protocol registration implementation ========== */

xiaozhi_protocol_t *xiaozhi_protocol_register(xiaozhi_protocol_type_t type)
{
    TAL_PR_INFO("Registering protocol: type=%d", type);
    
    switch (type) {
        case XIAOZHI_PROTOCOL_WEBSOCKET:
            return xiaozhi_protocol_websocket_register();
            
        case XIAOZHI_PROTOCOL_MQTT:
            TAL_PR_WARN("MQTT protocol not supported yet");
            return NULL;
            
        default:
            TAL_PR_ERR("Unknown protocol type: %d", type);
            return NULL;
    }
}
