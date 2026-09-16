/**
 * @file feishu_channel.h
 * @brief Feishu (Lark) bot channel for the wukong AI framework.
 *
 * The device holds a WebSocket long connection to the Feishu open platform to
 * receive message events (no public IP needed) and sends replies over the
 * Feishu REST API. Inbound text is forwarded via wukong_ai_channel_input();
 * outbound replies arrive through the channel send() vtable and are posted with
 * a synchronous HTTP POST. Credentials come from Kconfig (ENABLE_CHAN_FEISHU +
 * FEISHU_APP_ID / FEISHU_APP_SECRET).
 *
 * The whole channel lives in one translation unit: the pure protobuf frame
 * codec stays in feishu_proto.c; everything else (REST client, inbound event
 * routing, reconnect backoff, WebSocket state machine, and the channel vtable)
 * is implemented in feishu_channel.c.
 *
 * @version 1.0
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __FEISHU_CHANNEL_H__
#define __FEISHU_CHANNEL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"

/** Channel name registered into the channel framework. */
#define WUKONG_CHAN_FEISHU  "feishu"

/**
 * @brief Register the Feishu channel with the wukong channel framework.
 *
 * Must be called before wukong_ai_channel_init_all() / start_all().
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET feishu_channel_register(VOID_T);

OPERATE_RET feishu_channel_set_creds(CONST CHAR_T *app_id, CONST CHAR_T *app_secret);
OPERATE_RET feishu_channel_get_creds_masked(CHAR_T *out, UINT_T out_cap);

#ifdef __cplusplus
}
#endif
#endif /* __FEISHU_CHANNEL_H__ */
