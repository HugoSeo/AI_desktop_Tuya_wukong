/**
 * @file im_wechat_channel.h
 * @brief WeChat iLink Bot channel for the wukong AI framework.
 *
 * Wraps the WeChat iLink HTTP API as a WUKONG_AI_CHAN_T transport channel.
 * Inbound messages are delivered via wukong_ai_channel_input(); outbound
 * replies are dispatched through the channel send() vtable.
 *
 * @version 1.0
 * @date 2026-06-09
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __IM_WECHAT_CHANNEL_H__
#define __IM_WECHAT_CHANNEL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"
#include "wukong_ai_channel.h"

/* ---------------------------------------------------------------------------
 * WeChat iLink root CA
 * --------------------------------------------------------------------------- */

/** TLS host for which the root CA below is registered. */
#define WECHAT_ILINK_HOST  "ilinkai.weixin.qq.com"

/**
 * @brief Root CA certificate for ilinkai.weixin.qq.com (DigiCert Global Root G2).
 *
 * Chain: weixin.qq.com leaf
 *     → DigiCert Secure Site OV G2 TLS CN RSA4096 SHA256 2022 CA1 (intermediate)
 *     → DigiCert Global Root G2 (root, self-signed)
 *
 * Verified with:
 *   openssl s_client -connect ilinkai.weixin.qq.com:443 -showcerts
 *
 * @note mbedTLS PEM parser requires the null terminator to be included in the
 *       length passed to tuya_iot_store_third_cloud_ca, so use sizeof() not strlen().
 */
#define WECHAT_ILINK_ROOT_CA \
    "-----BEGIN CERTIFICATE-----\r\n" \
    "MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh\r\n" \
    "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\r\n" \
    "d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH\r\n" \
    "MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT\r\n" \
    "MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\r\n" \
    "b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG\r\n" \
    "9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI\r\n" \
    "2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx\r\n" \
    "1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ\r\n" \
    "q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz\r\n" \
    "tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ\r\n" \
    "vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP\r\n" \
    "BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV\r\n" \
    "5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY\r\n" \
    "1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4\r\n" \
    "NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG\r\n" \
    "Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91\r\n" \
    "8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe\r\n" \
    "pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl\r\n" \
    "MrY=\r\n" \
    "-----END CERTIFICATE-----\r\n"

/* ---------------------------------------------------------------------------
 * Configuration
 * --------------------------------------------------------------------------- */

/**
 * @brief Called by the channel when a QR code is ready for the user to scan.
 *
 * @param[in] qr_url  WeChat QR login URL (valid only during the callback).
 */
typedef VOID_T (*WECHAT_QR_NOTIFY_CB)(CONST CHAR_T *qr_url);

/**
 * @brief Caller-supplied configuration for the WeChat iLink channel.
 *
 * The channel manages token and API URLs internally via KV storage.
 * On first start (or after KV is cleared), the QR login flow is triggered
 * automatically and qr_notify_cb is called with the QR URL.
 */
typedef struct {
    WECHAT_QR_NOTIFY_CB qr_notify_cb; /**< QR code ready callback. NULL → silent. */
} WUKONG_CHAN_WECHAT_CFG_T;

/* ---------------------------------------------------------------------------
 * Channel descriptor (ROM-safe vtable)
 * --------------------------------------------------------------------------- */

/** WeChat iLink channel vtable — pass to im_wechat_channel_register(). */
extern CONST WUKONG_AI_CHAN_T g_wechat_channel;

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------- */

/**
 * @brief Register and initialise the WeChat channel in one call.
 *
 * Equivalent to wukong_ai_channel_register(&g_wechat_channel, cfg) followed
 * by the channel's own init().  Must be called before
 * wukong_ai_channel_start_all().
 *
 * @param[in] cfg Channel configuration. Must not be NULL.
 * @return OPRT_OK on success.
 */
OPERATE_RET im_wechat_channel_register(CONST WUKONG_CHAN_WECHAT_CFG_T *cfg);


#ifdef __cplusplus
}
#endif
#endif /* __IM_WECHAT_CHANNEL_H__ */
