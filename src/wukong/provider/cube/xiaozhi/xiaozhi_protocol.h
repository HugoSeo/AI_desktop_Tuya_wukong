/**
 * @file xiaozhi_protocol.h
 * @brief XiaoZhi AI Protocol - WebSocket audio streaming protocol (V3 only)
 * @date 2026-01-13
 * @author linch
 */

#ifndef XIAOZHI_PROTOCOL_H
#define XIAOZHI_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== Protocol types and modes ========== */

typedef enum {
    XIAOZHI_PROTOCOL_MQTT,
    XIAOZHI_PROTOCOL_WEBSOCKET
} xiaozhi_protocol_type_t;

typedef enum {
    XIAOZHI_LISTENING_MODE_AUTO_STOP,      // Auto stop (VAD detection)
    XIAOZHI_LISTENING_MODE_MANUAL_STOP,    // Manual stop
    XIAOZHI_LISTENING_MODE_REALTIME        // Real-time mode (requires AEC support)
} xiaozhi_listening_mode_t;

typedef enum {
    XIAOZHI_ABORT_REASON_NONE,
    XIAOZHI_ABORT_REASON_USER_INTERRUPT,
    XIAOZHI_ABORT_REASON_WAKE_WORD_DETECTED
} xiaozhi_abort_reason_t;

/* ========== Audio format (downlink, parsed from server hello audio_params.format) ========== */

typedef enum {
    XIAOZHI_AUDIO_FORMAT_OPUS = 0,   /* default, backward compatible */
    XIAOZHI_AUDIO_FORMAT_MP3,
    XIAOZHI_AUDIO_FORMAT_SPEEX,
} xiaozhi_audio_format_t;

/* ========== Audio data packet (V3 simplified) ========== */

typedef struct  __attribute__((packed)) {
    uint16_t  payload_len;   // Data length
    uint8_t  *payload;       // Audio data (dynamically allocated)
} xiaozhi_audio_packet_t;

/* ========== Binary protocol format ========== */

// Protocol version 3 (simplified, no timestamp)
typedef struct __attribute__((packed)) {
    uint8_t type;           // Message type (0=OPUS audio, 1=JSON text)
    uint8_t reserved;       // Reserved field
    uint16_t payload_size;  // Payload size (network byte order)
    uint8_t payload[];      // Payload data
} xiaozhi_binary_protocol_v3_t;

/* ========== Main protocol structure ========== */

typedef struct xiaozhi_protocol xiaozhi_protocol_t;

struct xiaozhi_protocol {
    /* User callback functions (C++ style, capitalized) */
    void (*OnIncomingAudio)(xiaozhi_audio_packet_t *packet, void *user_data);
    void (*OnIncomingJson)(const cJSON *root, void *user_data);
    void (*OnAudioChannelOpened)(void *user_data);
    void (*OnAudioChannelClosed)(void *user_data);
    void (*OnNetworkError)(const char *message, void *user_data);
    void (*OnConnected)(void *user_data);
    void (*OnDisconnected)(void *user_data);
    void *user_data;
    
    /* Protocol operation functions (filled by registration mechanism, C++ style without ctx parameter) */
    bool (*Start)(void);
    bool (*OpenAudioChannel)(void);
    void (*CloseAudioChannel)(void);
    bool (*IsAudioChannelOpened)(void);
    bool (*SendAudio)(xiaozhi_audio_packet_t *packet);
    bool (*SendText)(const char *text);
    
    /* Protocol message sending functions (filled by core implementation, access instance via global pointer) */
    void (*SendWakeWordDetected)(const char *wake_word);
    void (*SendStartListening)(xiaozhi_listening_mode_t mode);
    void (*SendStopListening)(void);
    void (*SendAbortSpeaking)(xiaozhi_abort_reason_t reason);
    void (*SendMcpMessage)(const char *message);
    
    /* Protocol state */
    int server_sample_rate;      // Server sample rate (default 24000)
    int server_frame_duration;   // Server frame duration (default 60ms)
    xiaozhi_audio_format_t server_audio_format;  // Downlink audio format, from server hello audio_params.format
    char session_id[64];         // Session ID
    bool error_occurred;         // Error flag
    uint32_t last_incoming_time; // Last incoming time (for timeout detection)
    
    /* Internal use */
    xiaozhi_protocol_type_t type;
};

/* ========== Public interface ========== */

/**
 * @brief Initialize protocol instance
 * @param type Protocol type
 * @return xiaozhi_protocol_t* Protocol instance pointer (must free with xiaozhi_protocol_deinit)
 */
xiaozhi_protocol_t *xiaozhi_protocol_init(xiaozhi_protocol_type_t type);

/**
 * @brief Destroy protocol instance
 * @param protocol Protocol instance pointer
 */
void xiaozhi_protocol_deinit(xiaozhi_protocol_t *protocol);

/**
 * @brief Check if protocol is timeout
 * @param protocol Protocol instance pointer
 * @return bool true=timeout, false=normal
 */
bool xiaozhi_protocol_is_timeout(xiaozhi_protocol_t *protocol);

/**
 * @brief Update last incoming time
 * @param protocol Protocol instance pointer
 */
void xiaozhi_protocol_update_incoming_time(xiaozhi_protocol_t *protocol);

/**
 * @brief Protocol registration function (factory pattern)
 * @param type Protocol type
 * @return xiaozhi_protocol_t* Allocated protocol instance pointer, NULL on failure
 * 
 * @note This function allocates the full derived structure (e.g. xiaozhi_websocket_ctx_t)
 *       and returns the base protocol pointer (first member, same address)
 */
xiaozhi_protocol_t *xiaozhi_protocol_register(xiaozhi_protocol_type_t type);

#ifdef __cplusplus
}
#endif

#endif // XIAOZHI_PROTOCOL_H
