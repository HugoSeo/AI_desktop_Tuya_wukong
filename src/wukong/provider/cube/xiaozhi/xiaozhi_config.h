/**
 * @file xiaozhi_config.h
 * @brief XiaoZhi Config
 * @date 2026-01-13
 * @author linch
 */

#ifndef XIAOZHI_CONFIG_H
#define XIAOZHI_CONFIG_H

/* 提供 USING_UART_AUDIO_INPUT + UART_CODEC_UPLOAD_FORMAT，供下方 CUBE_UPLINK_* 控制宏派生
 * (tuya_device_cfg.h 聚合 app config + board config，经 uart_codec 模块验证可达) */
#include "tuya_device_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef XIAOZHI_LISTENING_MODE
#define XIAOZHI_LISTENING_MODE XIAOZHI_LISTENING_MODE_REALTIME
#endif

// WebSocket Protocol Version (1 or 3)
// V1: Raw Opus data (no header)
// V3: Binary protocol with header (type + reserved + payload_size + payload)
#ifndef XIAOZHI_PROTOCOL_VERSION
#if defined(USING_UART_AUDIO_INPUT) && (USING_UART_AUDIO_INPUT == 1)
    /* uart 输入：V1 raw（无 V3 header）→ 上行 speex 无 type 字段、下行 mp3 无 [2B len] 前缀 */
    #define XIAOZHI_PROTOCOL_VERSION 1
#else
    /* board 输入：V3 带 header，匹配 opus_vbr 下行([2B len][opus]) */
    #define XIAOZHI_PROTOCOL_VERSION 3
#endif
#endif

#ifndef XIAOZHI_HOST
#define XIAOZHI_HOST "api.tenclass.net"
#endif

#define XIAOZHI_SERVER_URL "https://"XIAOZHI_HOST"/xiaozhi/ota/"

#define XIAOZHI_ROOT_CERTIFICATE                                                \
    "-----BEGIN CERTIFICATE-----\r\n"                                           \
    "MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh\r\n"      \
    "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\r\n"      \
    "d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH\r\n"      \
    "MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT\r\n"      \
    "MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\r\n"      \
    "b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG\r\n"      \
    "9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI\r\n"      \
    "2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx\r\n"      \
    "1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ\r\n"      \
    "q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz\r\n"      \
    "tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ\r\n"      \
    "vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP\r\n"      \
    "BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV\r\n"      \
    "5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY\r\n"      \
    "1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4\r\n"      \
    "NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG\r\n"      \
    "Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91\r\n"      \
    "8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe\r\n"      \
    "pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl\r\n"      \
    "MrY=\r\n"                                                                  \
    "-----END CERTIFICATE-----\r\n"


#ifdef __cplusplus
}
#endif

/* ─── Cube 上行音频格式控制宏 ───
 * 由输入源(board/uart) + uart codec 格式派生，集中控制 hello 的 format / frame_duration，
 * 及 provider 上行是否本地编码。切换上行格式只需改此处，不必散落各点 #if。
 *   board 输入   → 本地 PCM→opus 编码   format=opus    40ms  (PREENCODED=0)
 *   uart + opus  → 透传                  format=opus    40ms  (PREENCODED=1)
 *   uart + speex → 透传                  format=speex   20ms  (PREENCODED=1)
 */
#if defined(USING_UART_AUDIO_INPUT) && (USING_UART_AUDIO_INPUT == 1)
    #define CUBE_UPLINK_PREENCODED     1
  #if defined(UART_CODEC_UPLOAD_FORMAT) && (UART_CODEC_UPLOAD_FORMAT == 1)
    #define CUBE_UPLINK_AUDIO_FORMAT   "speex"
    #define CUBE_UPLINK_FRAME_DURATION 20
  #else
    #define CUBE_UPLINK_AUDIO_FORMAT   "opus"
    #define CUBE_UPLINK_FRAME_DURATION 40
  #endif
#else
    #define CUBE_UPLINK_PREENCODED     0
    #define CUBE_UPLINK_AUDIO_FORMAT   "opus"
    #define CUBE_UPLINK_FRAME_DURATION 40
#endif

#endif // XIAOZHI_CONFIG_H
