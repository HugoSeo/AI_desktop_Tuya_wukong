/**
 * @file joyinside_client.c
 * @brief joyinside client module
 * @version 0.1
 * @date 2025-12-15
 *
 * @copyright Copyright (c) 2023 Tuya Inc. All Rights Reserved.
 *
 * Permission is hereby granted, to any person obtaining a copy of this software and
 * associated documentation files (the "Software"), Under the premise of complying
 * with the license of the third-party open source software contained in the software,
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software.
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 */

#include <stdio.h>
#include "joyinside_client.h"
#include "joyinside_auth.h"
#include "tal_sw_timer.h"
#include "tal_workq_service.h"
#include "tal_thread.h"
#include "uni_log.h"
#include "tal_memory.h"
#include "tuya_transporter.h"
#include "tuya_svc_netmgr.h"
#include "tal_semaphore.h"
#include "tal_time_service.h"
#include "websocket_transporter.h"
#include "httpc.h"
#include "mqc_app.h"
#include "uni_random.h"
#include "tuya_devos_utils.h"
#include "joyinside_parse.h"

typedef enum {
    JD_STATE_IDLE,
    JD_STATE_SETUP,
    JD_STATE_CONNECT,
    JD_STATE_RUNNING,
    JD_STATE_END
} JD_CLIENT_STATE_E;

#define JD_WSS_AUDIO_URL "wss://joyinside.jd.com:443/soulmate/voiceChat/v1" // Different from docs, default port 80 causes tls 0x7200 error, need explicit port 443
#define JD_PING_TIMEOUT  6
#define JD_READ_TIMEOUT  5

#ifndef JD_PING_INTERVAL
#define JD_PING_INTERVAL (30)
#endif
#ifndef JD_RECV_BUF_SIZE
#define JD_RECV_BUF_SIZE (200 * 1024)
#endif
#ifndef JD_CLIENT_STACK_SIZE
#define JD_CLIENT_STACK_SIZE (8 * 1024)
#endif

typedef struct {
    tuya_transporter_t transporter;
    BOOL_T terminate;
    THREAD_HANDLE thread;
    BYTE_T recv_buf[JD_RECV_BUF_SIZE];
    UINT_T recv_len;
    JD_CLIENT_STATE_E state;
    BYTE_T heartbeat_lost_cnt;
    DELAYED_WORK_HANDLE alive_work;
    TIMER_ID alive_timeout_timer;
    BOOL_T is_paused;
    SEM_HANDLE sem_state;
} JD_CLIENT_S;
STATIC JD_CLIENT_S *g_joyinside_client = NULL;

VOID joyinside_client_deinit(VOID)
{
    ty_publish_event(EVENT_AI_CLIENT_CLOSE, NULL);
    g_joyinside_client->terminate = TRUE;
}

STATIC INT_T __jd_link_down_cb(VOID *data)
{
    g_joyinside_client->is_paused = TRUE;
    return OPRT_OK;
}

STATIC INT_T __jd_link_up_cb(VOID *data)
{
    g_joyinside_client->is_paused = FALSE;
    tal_semaphore_post(g_joyinside_client->sem_state);
    return OPRT_OK;
}

STATIC VOID __jd_transport_close(VOID)
{
    if (g_joyinside_client->transporter) {
        tuya_transporter_destroy(g_joyinside_client->transporter);
        g_joyinside_client->transporter = NULL;
    }
}

STATIC OPERATE_RET __jd_client_deinit(VOID)
{
    if (g_joyinside_client->thread) {
        tal_thread_delete(g_joyinside_client->thread);
        g_joyinside_client->thread = NULL;
    }
    if (g_joyinside_client->alive_timeout_timer) {
        tal_sw_timer_delete(g_joyinside_client->alive_timeout_timer);
        g_joyinside_client->alive_timeout_timer = NULL;
    }
    if (g_joyinside_client->alive_work) {
        tal_workq_cancel_delayed(g_joyinside_client->alive_work);
        g_joyinside_client->alive_work = NULL;
    }
    ty_unsubscribe_event(EVENT_LINK_UP, "jd_clt", __jd_link_up_cb);
    ty_unsubscribe_event(EVENT_LINK_DOWN, "jd_clt", __jd_link_down_cb);
    __jd_transport_close();
    joyinside_auth_deinit();
    joyinside_biz_deinit();
    Free(g_joyinside_client);
    g_joyinside_client = NULL;
    PR_NOTICE("jd client deinit success");
    return OPRT_OK;
}

VOID joyinside_start_ping(VOID)
{
    tal_workq_start_delayed(g_joyinside_client->alive_work, (JD_PING_INTERVAL * 1000), LOOP_ONCE);
}

STATIC VOID __jd_stop_ping(VOID)
{
    tal_workq_stop_delayed(g_joyinside_client->alive_work);
    g_joyinside_client->heartbeat_lost_cnt = 0;
    tal_sw_timer_stop(g_joyinside_client->alive_timeout_timer);
}

STATIC VOID __jd_client_set_state(JD_CLIENT_STATE_E state)
{
    PR_NOTICE("***** jd client state %d -> %d *****", g_joyinside_client->state, state);
    g_joyinside_client->state = state;
}

STATIC OPERATE_RET __jd_idle(VOID)
{
    if ((tuya_svc_netmgr_get_status() != NETWORK_STATUS_MQTT) ||
        (tal_time_check_time_sync() != OPRT_OK)) {
        return OPRT_COM_ERROR;
    }
    __jd_client_set_state(JD_STATE_SETUP);
    return OPRT_OK;
}

STATIC OPERATE_RET __jd_setup(VOID)
{
    OPERATE_RET rt = OPRT_OK;

    rt = joyinside_auth_token();
    if (OPRT_OK != rt) {
        PR_ERR("get token err, rt:%d", rt);
        return rt;
    }

    rt = joyinside_dev_register();
    if (OPRT_OK != rt) {
        PR_ERR("device register err, rt:%d", rt);
        return rt;
    }

    __jd_client_set_state(JD_STATE_CONNECT);
    return rt;
}

STATIC VOID __jd_clear_recv_buf(VOID)
{
    memset(g_joyinside_client->recv_buf, 0, JD_RECV_BUF_SIZE);
    g_joyinside_client->recv_len = 0;
}

VOID joyinside_client_close(VOID)
{
    ty_publish_event(EVENT_AI_CLIENT_CLOSE, NULL);
    __jd_stop_ping();
    __jd_transport_close();
    __jd_clear_recv_buf();
    __jd_client_set_state(JD_STATE_IDLE);
}

STATIC OPERATE_RET __jd_client_handle_err(OPERATE_RET rt)
{
    if (g_joyinside_client->state == JD_STATE_SETUP) {
        __jd_client_set_state(JD_STATE_IDLE);
        tal_system_sleep(5000);
    } else if (g_joyinside_client->state == JD_STATE_CONNECT) {
        __jd_client_set_state(JD_STATE_IDLE);
        tal_system_sleep(1000);
    } else if (g_joyinside_client->state == JD_STATE_RUNNING) {
        PR_ERR("jd client running error, close client %d, %d", rt, tal_net_get_errno());
        joyinside_client_close();
    } else {
        tal_system_sleep(1000);
    }
    return OPRT_OK;
}

STATIC VOID_T __jd_wss_close_work(VOID_T *data)
{
    joyinside_client_close();
}

VOID __jd_wss_client_event_cb(websocket_client_msg_t *msg, void *priv_data)
{
    if (msg && msg->event == WEBSOCKET_CLOSE_EVENT) {
        PR_NOTICE("websocket disconnected event received");
        tal_workq_schedule(WORKQ_SYSTEM, __jd_wss_close_work, NULL);
    }
}

STATIC OPERATE_RET __jd_connect(VOID)
{
    OPERATE_RET rt = OPRT_OK;
    CHAR_T request_id[JD_UUID_V4_LEN + 1] = {0};
    joyinside_uuid_v4(request_id);
    CHAR_T session_id[128] = {0};
    JD_DEV_INFO_S *dev_info = joyinside_get_dev_info();
    JD_TOKEN_S *token_info = joyinside_get_token_info();
    snprintf(session_id, 128, "%s_%s", "tuya", dev_info->botId);

    NW_IP_S ip;
    memset(&ip, 0, sizeof(NW_IP_S));
    netmgr_linkage_t *linkage = NULL;
    mqc_get_connection_linkage(&linkage);
    if (!linkage || linkage->get(LINKAGE_CFG_IP, &ip) != OPRT_OK) {
        PR_ERR("ji get ip failed %d", linkage ? linkage->type : 255);
        return OPRT_COM_ERROR;
    }

    BOOL_T manual_call = joyinside_get_manual_call();
    CHAR_T *domain = Malloc(512);
    TUYA_CHECK_NULL_RETURN(domain, OPRT_MALLOC_FAILED);
    memset(domain, 0, 512);
    if (manual_call) {
        snprintf(domain, 512, "%s?botId=%s&sessionId=%s&requestId=%s&needManualCall=true&feature=AUDIO_BOOK_V2",
                 JD_WSS_AUDIO_URL, dev_info->botId, session_id, request_id);
    } else {
        snprintf(domain, 512, "%s?botId=%s&sessionId=%s&requestId=%s&feature=AUDIO_BOOK_V2",
                 JD_WSS_AUDIO_URL, dev_info->botId, session_id, request_id);
    }
    JD_PR_D("wss domain:%s", domain);
    parsed_url_t url;
    CHAR_T tmp_buf[256] = {0};
    rt = http_parse_URL(domain, tmp_buf, SIZEOF(tmp_buf), &url);
    if (OPRT_OK != rt) {
        PR_ERR("parse url err, rt:%d", rt);
        Free(domain);
        return rt;
    }

    g_joyinside_client->transporter = tuya_transporter_create(TRANSPORT_TYPE_WEBSOCKET, NULL);
    tuya_websocket_config_t websocket_config;
    websocket_config.path = url.resource;
    websocket_config.scheme = "wss";
    websocket_config.token = token_info->accessToken;
    websocket_config.event_cb = __jd_wss_client_event_cb;
    rt = tuya_transporter_ctrl(g_joyinside_client->transporter, TUYA_TRANSPORTER_SET_WEBSOCKET_CONFIG, &websocket_config);

    struct socket_config_t sock_config = {0};
    sock_config.isReuse = TRUE;
    sock_config.isDisableNagle = TRUE;
    sock_config.isKeepAlive = TRUE;
    sock_config.keepAliveIdleTime = 60;
    sock_config.keepAliveInterval = 5;
    sock_config.keepAliveCount = 1;
    sock_config.isBlock = TRUE;
    sock_config.bindPort = 20000 + uni_random();
    sock_config.sendTimeoutMs = 5000;
#if defined(ENABLE_IPv6) && (ENABLE_IPv6 == 1)
    sock_config.bindAddr = tal_net_str2addr(ip.nwipstr);
#else
    sock_config.bindAddr = tal_net_str2addr(ip.ip);
#endif
    rt = tuya_transporter_ctrl(g_joyinside_client->transporter, TUYA_TRANSPORTER_SET_TCP_CONFIG, &sock_config);
    Free(domain);

    rt = tuya_transporter_connect(g_joyinside_client->transporter, url.hostname, url.portno, 10 * 1000);
    if (OPRT_OK != rt) {
        tuya_transporter_destroy(g_joyinside_client->transporter);
        g_joyinside_client->transporter = NULL;
        PR_ERR("transporter connect err, rt:%d", rt);
        return rt;
    }
    joyinside_start_ping();
    __jd_client_set_state(JD_STATE_RUNNING);
    ty_publish_event(EVENT_AI_CLIENT_RUN, NULL);
    return rt;
}

OPERATE_RET joyinside_transporter_write(CHAR_T *data, UINT_T len)
{
    OPERATE_RET rt = OPRT_OK;
    rt = websocket_transporter_write_text(g_joyinside_client->transporter, (UCHAR_T *)data, len);
    if (rt <= 0) {
        PR_ERR("websocket send text failed, rt:%d", rt);
        return rt;
    }
    return OPRT_OK;
}

BOOL_T joyinside_client_is_ready(VOID)
{
    if (g_joyinside_client) {
        if (!(g_joyinside_client->terminate) && (g_joyinside_client->state == JD_STATE_RUNNING)) {
            return TRUE;
        }
    }
    return FALSE;
}

STATIC OPERATE_RET __jd_extract_complete_json(ty_cJSON **root)
{
    if (g_joyinside_client->recv_len < 2) {
        return OPRT_INVALID_PARM;
    }

    // 1. Skip leading whitespace and locate JSON start {
    INT_T start = 0;
    while (start < g_joyinside_client->recv_len &&
           (g_joyinside_client->recv_buf[start] == ' ' || g_joyinside_client->recv_buf[start] == '\n' ||
            g_joyinside_client->recv_buf[start] == '\r' || g_joyinside_client->recv_buf[start] == '\t')) {
        start++;
    }

    // No valid JSON start found
    if (start >= g_joyinside_client->recv_len || g_joyinside_client->recv_buf[start] != '{') {
        if (start > 0) {
            g_joyinside_client->recv_len -= start;
            memmove(g_joyinside_client->recv_buf, g_joyinside_client->recv_buf + start, g_joyinside_client->recv_len);
        }
        return OPRT_INVALID_PARM;
    }

    // 2. Match {} pairs and locate JSON end } - optimized escape handling
    INT_T end = -1, brace_count = 0;
    BOOL_T in_string = FALSE;
    BOOL_T escaped = FALSE;

    for (INT_T idx = start; idx < g_joyinside_client->recv_len; idx++) {
        CHAR_T ch = g_joyinside_client->recv_buf[idx];

        if (in_string) {
            if (escaped) {
                escaped = FALSE; // Any character after \ is escaped
            } else if (ch == '\\') {
                escaped = TRUE;
            } else if (ch == '"') {
                in_string = FALSE; // End of string
            }
        } else {
            if (ch == '"') {
                in_string = TRUE; // Start of string
            } else if (ch == '{') {
                brace_count++;
            } else if (ch == '}') {
                brace_count--;
                if (brace_count == 0) {
                    end = idx; // Found complete closing }
                    break;
                }
            }
        }
    }

    if (end == -1) {
        return OPRT_INVALID_PARM; // No complete end found (partial packet)
    }

    // 3. Parse JSON safely - optimized memory handling
    INT_T json_len = end - start + 1;
    ty_cJSON *json_root = NULL;

    // Check if safe to add null terminator in original buffer
    if (end + 1 >= g_joyinside_client->recv_len) {
        // Safe: no data after JSON end, parse directly in original buffer
        CHAR_T backup_char = g_joyinside_client->recv_buf[end + 1];
        g_joyinside_client->recv_buf[end + 1] = '\0';
        json_root = ty_cJSON_Parse((CHAR_T *)(g_joyinside_client->recv_buf + start));
        g_joyinside_client->recv_buf[end + 1] = backup_char;
    } else {
        // Unsafe: sticky packet data after JSON, create temporary copy
        CHAR_T *json_str = (CHAR_T *)Malloc(json_len + 1);
        if (!json_str) {
            return OPRT_MALLOC_FAILED;
        }
        memcpy(json_str, g_joyinside_client->recv_buf + start, json_len);
        json_str[json_len] = '\0';
        json_root = ty_cJSON_Parse(json_str);
        Free(json_str);
    }

    *root = json_root;
    if (*root == NULL) {
        // JSON parse failed, skip one character and retry
        g_joyinside_client->recv_len -= (start + 1);
        memmove(g_joyinside_client->recv_buf, g_joyinside_client->recv_buf + start + 1, g_joyinside_client->recv_len);
        return OPRT_INVALID_PARM;
    }

    // 4. Successfully parsed JSON, preserve remaining data efficiently
    INT_T total_consumed = end + 1;
    INT_T remain_len = g_joyinside_client->recv_len - total_consumed;

    if (remain_len > 0) {
        // Move remaining data to buffer head
        memmove(g_joyinside_client->recv_buf, g_joyinside_client->recv_buf + total_consumed, remain_len);
    }

    g_joyinside_client->recv_len = remain_len;
    JD_PR_D("extracted json (len: %d), remaining: %d bytes", json_len, remain_len);

    return OPRT_OK;
}

VOID joyinside_handle_pong(VOID)
{
    PR_DEBUG("jd pong");
    g_joyinside_client->heartbeat_lost_cnt = 0;
    tal_sw_timer_stop(g_joyinside_client->alive_timeout_timer);
    tal_workq_start_delayed(g_joyinside_client->alive_work, (JD_PING_INTERVAL * 1000), LOOP_ONCE);
}

STATIC OPERATE_RET __jd_running(VOID)
{
    OPERATE_RET rt = OPRT_OK;
    INT_T read_len = 0;
    ty_cJSON *root = NULL;

    while (TRUE) {
        root = NULL;
        rt = __jd_extract_complete_json(&root);
        if (rt == OPRT_OK && root != NULL) {
            JD_PR_D("extracted complete json, processing...");
            joyinside_parse_content(root);
            ty_cJSON_Delete(root);
            root = NULL;
            if (g_joyinside_client->recv_len > 0) {
                continue;
            } else {
                break;
            }
        } else {
            break;
        }
    }

    // Try to read new data from socket
    if (g_joyinside_client->recv_len < JD_RECV_BUF_SIZE) {
        read_len = tuya_transporter_read(g_joyinside_client->transporter,
                                         (UCHAR_T *)(g_joyinside_client->recv_buf + g_joyinside_client->recv_len),
                                         JD_RECV_BUF_SIZE - g_joyinside_client->recv_len,
                                         JD_READ_TIMEOUT * 1000);
        if (read_len > 0) {
            g_joyinside_client->recv_len += read_len;
            JD_PR_D("read %d bytes from socket, buffer total len: %d/%d",
                     read_len, g_joyinside_client->recv_len, JD_RECV_BUF_SIZE);
        } else if (read_len == -1) {
            return OPRT_COM_ERROR;
        }
    } else {
        PR_ERR("recv buffer full but no complete json extracted, this should not happen");
        memset(g_joyinside_client->recv_buf, 0, JD_RECV_BUF_SIZE);
        g_joyinside_client->recv_len = 0;
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

STATIC VOID __jd_client_thread(PVOID_T args)
{
    OPERATE_RET rt = OPRT_OK;
    while (!g_joyinside_client->terminate && tal_thread_get_state(g_joyinside_client->thread) == THREAD_STATE_RUNNING) {
        if (g_joyinside_client->is_paused) {
            PR_NOTICE("jd client paused, waiting for network recovery");
            if (g_joyinside_client->state == JD_STATE_RUNNING) {
                joyinside_client_close();
            } else {
                __jd_client_set_state(JD_STATE_IDLE);
            }
            tal_semaphore_wait(g_joyinside_client->sem_state, SEM_WAIT_FOREVER);
            tal_system_sleep(100);
            continue;
        }
        switch (g_joyinside_client->state) {
        case JD_STATE_IDLE:
            rt = __jd_idle();
            break;
        case JD_STATE_SETUP:
            rt = __jd_setup();
            break;
        case JD_STATE_CONNECT:
            rt = __jd_connect();
            break;
        case JD_STATE_RUNNING:
            rt = __jd_running();
            break;
        default:
            break;
        }
        if (OPRT_OK != rt) {
            __jd_client_handle_err(rt);
        }
    }

    __jd_client_deinit();
    PR_NOTICE("jd client thread exit");
}

STATIC OPERATE_RET __jd_client_create_task(VOID)
{
    OPERATE_RET rt = OPRT_OK;
    THREAD_CFG_T thrd_param = {0};
    thrd_param.priority = THREAD_PRIO_1;
    thrd_param.thrdname = "jd_client";
    thrd_param.stackDepth = JD_CLIENT_STACK_SIZE;
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    thrd_param.psram_mode = 1;
#endif

    rt = tal_thread_create_and_start(&g_joyinside_client->thread, NULL, NULL, __jd_client_thread, NULL, &thrd_param);
    if (OPRT_OK != rt) {
        PR_ERR("jd client thread create err, rt:%d", rt);
    }
    return rt;
}

STATIC VOID __jd_alive_timeout(TIMER_ID timer_id, VOID_T *data)
{
    PR_ERR("alive timeout");
    g_joyinside_client->heartbeat_lost_cnt++;
    if (g_joyinside_client->heartbeat_lost_cnt >= 3) {
        PR_ERR("ping lost >= 3, close wss connection");
        joyinside_client_close();
    } else {
        tal_workq_start_delayed(g_joyinside_client->alive_work, 10, LOOP_ONCE);
    }
}

STATIC OPERATE_RET __jd_make_ping_data(CHAR_T **data, UINT_T *len)
{
    OPERATE_RET rt = OPRT_OK;

    ty_cJSON *root = ty_cJSON_CreateObject();
    TUYA_CHECK_NULL_RETURN(root, OPRT_MALLOC_FAILED);

    CHAR_T mid[JD_UUID_V4_LEN + 1] = {0};
    joyinside_uuid_v4(mid);

    ty_cJSON_AddStringToObject(root, "mid", mid);
    ty_cJSON_AddStringToObject(root, "contentType", "PING");
    ty_cJSON_AddStringToObject(root, "uid", get_gw_dev_id());

    CHAR_T *root_str = ty_cJSON_PrintUnformatted(root);
    ty_cJSON_Delete(root);
    TUYA_CHECK_NULL_RETURN(root_str, OPRT_MALLOC_FAILED);

    *data = root_str;
    *len = strlen(root_str);
    return rt;
}

STATIC VOID __jd_ping(VOID *data)
{
    OPERATE_RET rt = OPRT_OK;
    CHAR_T *ping_data = NULL;
    UINT_T len = 0;
    rt = __jd_make_ping_data(&ping_data, &len);
    if (OPRT_OK != rt) {
        PR_ERR("make ping data failed, rt:%d", rt);
        return;
    }

    tal_sw_timer_start(g_joyinside_client->alive_timeout_timer, JD_PING_TIMEOUT * 1000, TAL_TIMER_ONCE);
    joyinside_transporter_write(ping_data, len);
    Free(ping_data);
    PR_DEBUG("jd ping");
}

OPERATE_RET joyinside_client_init(JD_CHAT_CFG_S *chat_cfg)
{
    OPERATE_RET rt = OPRT_OK;
    if (g_joyinside_client) {
        return rt;
    }
    g_joyinside_client = (JD_CLIENT_S *)Malloc(SIZEOF(JD_CLIENT_S));
    TUYA_CHECK_NULL_RETURN(g_joyinside_client, OPRT_MALLOC_FAILED);

    memset(g_joyinside_client, 0, SIZEOF(JD_CLIENT_S));
    joyinside_auth_init();
    joyinside_biz_init(chat_cfg);
    ty_subscribe_event(EVENT_LINK_UP, "jd_clt", __jd_link_up_cb, SUBSCRIBE_TYPE_NORMAL);
    ty_subscribe_event(EVENT_LINK_DOWN, "jd_clt", __jd_link_down_cb, SUBSCRIBE_TYPE_NORMAL);
    TUYA_CALL_ERR_GOTO(tal_semaphore_create_init(&g_joyinside_client->sem_state, 0, 1), EXIT);
    TUYA_CALL_ERR_GOTO(__jd_client_create_task(), EXIT);
    TUYA_CALL_ERR_GOTO(tal_sw_timer_create(__jd_alive_timeout, NULL, &g_joyinside_client->alive_timeout_timer), EXIT);
    TUYA_CALL_ERR_GOTO(tal_workq_init_delayed(WORKQ_HIGHTPRI, __jd_ping, NULL, &g_joyinside_client->alive_work), EXIT);
    PR_NOTICE("jd client init success");
    return rt;

EXIT:
    __jd_client_deinit();
    return rt;
}