/**
 * @file xiaozhi_protocol.c
 * @brief XiaoZhi AI Protocol core implementation
 * @date 2026-01-13
 * @author linch
 */

#include "xiaozhi_protocol.h"
#include "tuya_simple_http.h"
#include "tuya_error_code.h"
#include "tal_log.h"
#include "tal_system.h"
#include "tal_memory.h"
#include <string.h>
#include <stdio.h>

/* ========== Private definitions ========== */

#define XIAOZHI_TIMEOUT_SECONDS 120

/* Global protocol instance pointer (for internal use) */
static xiaozhi_protocol_t *g_protocol = NULL;

/* ========== Private function declarations ========== */

static void send_wake_word_detected_impl(const char *wake_word);
static void send_start_listening_impl(xiaozhi_listening_mode_t mode);
static void send_stop_listening_impl(void);
static void send_abort_speaking_impl(xiaozhi_abort_reason_t reason);
static void send_mcp_message_impl(const char *message);

/* ========== Protocol initialization and cleanup ========== */

xiaozhi_protocol_t *xiaozhi_protocol_init(xiaozhi_protocol_type_t type)
{
    // Check if already initialized
    if (g_protocol) {
        TAL_PR_WARN("Protocol already initialized, deinit first");
        return g_protocol;
    }
    
    // Register specific protocol implementation (allocates full derived structure)
    // Returns base protocol pointer (first member, same address as derived structure)
    xiaozhi_protocol_t *protocol = xiaozhi_protocol_register(type);
    if (!protocol) {
        TAL_PR_ERR("Failed to register protocol: %d", type);
        return NULL;
    }
    
    // Initialize common protocol fields
    protocol->type = type;
    protocol->server_sample_rate = 24000;
    protocol->server_frame_duration = 60;
    protocol->server_audio_format = XIAOZHI_AUDIO_FORMAT_OPUS;
    
    // Register message send functions (core implementation)
    protocol->SendWakeWordDetected = send_wake_word_detected_impl;
    protocol->SendStartListening = send_start_listening_impl;
    protocol->SendStopListening = send_stop_listening_impl;
    protocol->SendAbortSpeaking = send_abort_speaking_impl;
    protocol->SendMcpMessage = send_mcp_message_impl;
    
    g_protocol = protocol;
    TAL_PR_INFO("Protocol initialized successfully: type=%d", type);
    
    return protocol;
}

void xiaozhi_protocol_deinit(xiaozhi_protocol_t *protocol)
{
    if (!protocol) {
        return;
    }
    
    TAL_PR_INFO("Deinitializing protocol");
    
    // Close audio channel
    if (protocol->CloseAudioChannel) {
        protocol->CloseAudioChannel();
    }
    
    // Free the full derived structure (e.g. xiaozhi_websocket_ctx_t)
    // Since protocol is the first member, the addresses are the same
    tal_free(protocol);
    g_protocol = NULL;
}

/* ========== Protocol message sending implementation ========== */

static void send_wake_word_detected_impl(const char *wake_word)
{
    if (!g_protocol || !g_protocol->SendText) {
        TAL_PR_ERR("Protocol not initialized or SendText not available");
        return;
    }
    
    if (!wake_word) {
        TAL_PR_ERR("Invalid wake_word parameter");
        return;
    }
    
    char json[256];
    snprintf(json, sizeof(json),
             "{\"session_id\":\"%s\",\"type\":\"listen\",\"state\":\"detect\",\"text\":\"%s\"}",
             g_protocol->session_id, wake_word);
    
    TAL_PR_DEBUG("Sending wake word: %s", wake_word);
    g_protocol->SendText(json);
}

static void send_start_listening_impl(xiaozhi_listening_mode_t mode)
{
    if (!g_protocol || !g_protocol->SendText) {
        TAL_PR_ERR("Protocol not initialized or SendText not available");
        return;
    }
    
    const char *mode_str;
    switch (mode) {
        case XIAOZHI_LISTENING_MODE_REALTIME:
            mode_str = "realtime";
            break;
        case XIAOZHI_LISTENING_MODE_AUTO_STOP:
            mode_str = "auto";
            break;
        default:
            mode_str = "manual";
            break;
    }
    
    char json[256];
    snprintf(json, sizeof(json),
             "{\"session_id\":\"%s\",\"type\":\"listen\",\"state\":\"start\",\"mode\":\"%s\"}",
             g_protocol->session_id, mode_str);
    
    TAL_PR_DEBUG("Sending start listening: mode=%s", mode_str);
    g_protocol->SendText(json);
}

static void send_stop_listening_impl(void)
{
    if (!g_protocol || !g_protocol->SendText) {
        TAL_PR_ERR("Protocol not initialized or SendText not available");
        return;
    }
    
    char json[256];
    snprintf(json, sizeof(json),
             "{\"session_id\":\"%s\",\"type\":\"listen\",\"state\":\"stop\"}",
             g_protocol->session_id);
    
    TAL_PR_DEBUG("Sending stop listening");
    g_protocol->SendText(json);
}

static void send_abort_speaking_impl(xiaozhi_abort_reason_t reason)
{
    if (!g_protocol || !g_protocol->SendText) {
        TAL_PR_ERR("Protocol not initialized or SendText not available");
        return;
    }
    
    char json[256];
    snprintf(json, sizeof(json),
             "{\"session_id\":\"%s\",\"type\":\"abort\"%s}",
             g_protocol->session_id,
             (reason == XIAOZHI_ABORT_REASON_WAKE_WORD_DETECTED) ? 
                ",\"reason\":\"wake_word_detected\"" : "");
    
    TAL_PR_DEBUG("Sending abort speaking: reason=%d", reason);
    g_protocol->SendText(json);
}

static void send_mcp_message_impl(const char *message)
{
    if (g_protocol == NULL || g_protocol->SendText == NULL || message == NULL) {
        return;
    }

    /* Wrap the JSON-RPC message into xiaozhi's MCP envelope (see mcp.md):
     *   {"session_id":..,"type":"mcp","payload":<message>}
     * payload is embedded raw since `message` is already a serialized JSON value. */
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }
    cJSON_AddStringToObject(root, "session_id", g_protocol->session_id);
    cJSON_AddStringToObject(root, "type", "mcp");
    cJSON_AddRawToObject(root, "payload", message);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json) {
        TAL_PR_TRACE("Sending MCP: %s", json);
        g_protocol->SendText(json);
        cJSON_free(json);
    }
}

/* ========== Timeout detection ========== */

bool xiaozhi_protocol_is_timeout(xiaozhi_protocol_t *protocol)
{
    if (!protocol) {
        return true;
    }
    
    uint32_t now = tal_system_get_millisecond();
    uint32_t duration_ms = now - protocol->last_incoming_time;
    
    if (duration_ms > XIAOZHI_TIMEOUT_SECONDS * 1000) {
        TAL_PR_ERR("Protocol timeout: %u ms", duration_ms);
        return true;
    }
    
    return false;
}

void xiaozhi_protocol_update_incoming_time(xiaozhi_protocol_t *protocol)
{
    if (protocol) {
        protocol->last_incoming_time = tal_system_get_millisecond();
    }
}
