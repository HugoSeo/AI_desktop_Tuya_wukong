/**
 * @file gfw_core.c
 * @brief
 * @version 0.1
 * @date 2023-06-14
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

#include "uni_log.h"
#include "ty_cJSON.h"
#include "tal_uart.h"
#include "tal_system.h"
#include "tal_thread.h"
#include "tuya_ws_db.h"
#include "gfw_core.h"
#include "gfw_core_cmd.h"
#include "gfw_core_cmd_queue.h"
#include "gfw_core_dispatcher.h"

#include "tkl_semaphore.h"


#define GF_KV_BAUDRATE "baud_cfg" // keep the same as before

typedef enum {
    CORE_STATE_IDLE,
    CORE_STATE_INIT,
    CORE_STATE_BAUDRATE_DETECT,
    CORE_STATE_RUNNING,
    CORE_STATE_MAX
} CORE_STATE_E;

typedef struct {
    CORE_STATE_E state;
    THREAD_HANDLE thread;
    TKL_SEM_HANDLE sem;
    UINT_T sem_wait_time;

    TUYA_UART_NUM_E port;
    UINT_T def_baudrate; // default baudrate
    BOOL_T auto_baudrate; // detect baudrate for communication
    UINT_T baudrate_list[MAX_DETECT_BAUD_NUM];
    UINT_T baudrate_cnt;
    BYTE_T mcu_cmd_ver;
    AUDIO_GFW_TRANS_T trans;

} GFW_CORE_CTX_T;

STATIC GFW_CORE_CTX_T s_gfw_core_ctx;

STATIC UINT_T __read_baudrate(VOID)
{
    OPERATE_RET rt = OPRT_OK;
    INT_T baud_rate = 0;
    BYTE_T *buf = NULL;
    UINT_T buf_len = 0;
    ty_cJSON *root = NULL, *item = NULL;

    TUYA_CALL_ERR_RETURN(wd_common_read(GF_KV_BAUDRATE, &buf, &buf_len));

    root = ty_cJSON_Parse((CHAR_T *)buf);
    wd_common_free_data(buf);

    if (root) {
        item = ty_cJSON_GetObjectItem(root, "baud");
        if (item) {
            baud_rate = item->valueint;
            PR_NOTICE("read baud rate %d", baud_rate);
        }
    }

    ty_cJSON_Delete(root);

    return baud_rate;
}

STATIC OPERATE_RET __write_baudrate(UINT_T baud_rate)
{
    ty_cJSON *root = NULL;
    CHAR_T *buf = NULL;

    PR_NOTICE("write baud rate %d", baud_rate);

    root = ty_cJSON_CreateObject();
    if (!root) {
        return OPRT_MALLOC_FAILED;
    }

    ty_cJSON_AddNumberToObject(root, "baud", baud_rate);
    buf = ty_cJSON_PrintUnformatted(root);
    if (buf) {
        wd_common_write(GF_KV_BAUDRATE, (BYTE_T *)buf, strlen(buf));
        Free(buf);
    }

    ty_cJSON_Delete(root);

    return OPRT_OK;
}

STATIC OPERATE_RET __cmd_dispatch(AUDIO_GFW_CMD_DATA_T *data)
{
    gfw_core_cmd_queue_ack(data->cmd);
    return gfw_core_cmd_dispatch(data);
}

STATIC OPERATE_RET __cmd_detect_send(VOID)
{
    CONST BYTE_T data[] = {0x55, 0xaa, 0x00, 0x92, 0x00, 0x01, 0x01, 0x93}; // version query

    return tal_uart_write(s_gfw_core_ctx.port, data, SIZEOF(data));
}

STATIC OPERATE_RET __cmd_detect_recv(UINT_T baudrate, AUDIO_GFW_CMD_DATA_T *data)
{
    if (data->cmd == GFW_CMD_VOICE_SERV) { // hearbeat
        if (s_gfw_core_ctx.def_baudrate != baudrate) {
            s_gfw_core_ctx.def_baudrate = baudrate;
            __write_baudrate(baudrate);
        }
        s_gfw_core_ctx.mcu_cmd_ver = data->ver; // default 3
        s_gfw_core_ctx.state = CORE_STATE_RUNNING;
        return OPRT_OK;
    } else {
        return OPRT_NOT_FOUND;
    }
}

STATIC VOID __core_idle(VOID)
{
    tal_system_sleep(50);
}

STATIC VOID __core_baudrate_detect(VOID)
{
    PR_DEBUG("baudrate detect proc!");

    DISPATCHER_CFG_T cfg = {0};
    cfg.cmd_handler = __cmd_dispatch;
    cfg.detect_send = __cmd_detect_send;
    cfg.detect_recv = __cmd_detect_recv;
    cfg.cmd_interval = 50;
    cfg.port = s_gfw_core_ctx.port;
    cfg.baud_rate_cnt = s_gfw_core_ctx.baudrate_cnt;
    cfg.trans = &s_gfw_core_ctx.trans;
    memcpy(cfg.baud_rate, s_gfw_core_ctx.baudrate_list, SIZEOF(cfg.baud_rate));

    INT_T baud_rate = 0;
    INT_T i = 0;
    baud_rate = __read_baudrate();
    if (baud_rate > 0) {
        s_gfw_core_ctx.def_baudrate = baud_rate;

        if (cfg.baud_rate[0] != baud_rate) {
            for (i = 0; i < cfg.baud_rate_cnt; i++) {
                if (cfg.baud_rate[i] == baud_rate) {
                    break;
                }
            }

            if (i < cfg.baud_rate_cnt) {
                cfg.baud_rate[i] = cfg.baud_rate[0];
                cfg.baud_rate[0] = baud_rate;
            }
        }
    }

    gfw_core_dispatcher_start(&cfg);
}

STATIC VOID __core_running(VOID)
{
    DISPATCHER_CFG_T cfg = {0};
    cfg.cmd_handler = __cmd_dispatch;
    cfg.detect_send = NULL;
    cfg.detect_recv = NULL;
    cfg.cmd_interval = 50;
    cfg.port = s_gfw_core_ctx.port;
    cfg.baud_rate_cnt = 1;
    cfg.baud_rate[0] = s_gfw_core_ctx.def_baudrate;
    cfg.trans = &s_gfw_core_ctx.trans;
    gfw_core_dispatcher_start(&cfg);
}

STATIC VOID __gfw_core_thread(PVOID_T args)
{
    s_gfw_core_ctx.sem_wait_time = 10;

    while (tal_thread_get_state(s_gfw_core_ctx.thread) == THREAD_STATE_RUNNING) {
        TAL_PR_DEBUG("s_gfw_core_ctx.state %d", s_gfw_core_ctx.state);
        tkl_semaphore_wait(s_gfw_core_ctx.sem,s_gfw_core_ctx.sem_wait_time);
        switch (s_gfw_core_ctx.state) {
        case CORE_STATE_IDLE:
        case CORE_STATE_INIT:
            __core_idle();
            break;
        case CORE_STATE_BAUDRATE_DETECT:
            __core_baudrate_detect();
            break;
        case CORE_STATE_RUNNING:
            __core_running();
            break;
        default:
            break;
        }
    }
}

OPERATE_RET gfw_core_init(AUDIO_GFW_CORE_CFG_T *cfg)
{
    OPERATE_RET rt = OPRT_OK;

    if (CORE_STATE_IDLE != s_gfw_core_ctx.state) {//低功耗重入
        THREAD_STATE_E state = tal_thread_get_state(s_gfw_core_ctx.thread);

        PR_DEBUG("gfw core lowpower exit:%d",state);
        // TUYA_CALL_ERR_RETURN(tal_thread_create_and_start(&s_gfw_core_ctx.thread, NULL, NULL, __gfw_core_thread, NULL, &cfg->thread_cfg));
        gfw_core_start();
        s_gfw_core_ctx.sem_wait_time = 10;
        TUYA_CALL_ERR_RETURN(tkl_semaphore_post(s_gfw_core_ctx.sem));
        return OPRT_OK;
    }

    if (cfg->auto_baudrate && cfg->ext_trans) {
        return OPRT_INVALID_PARM;
    }

    UINT_T cfg_baudrate_list[] = TY_AUDIO_BAUD_LIST;
    s_gfw_core_ctx.auto_baudrate = cfg->auto_baudrate;
    s_gfw_core_ctx.def_baudrate = cfg->def_baudrate;
    s_gfw_core_ctx.baudrate_cnt = CNTSOF(cfg_baudrate_list);
    if (s_gfw_core_ctx.baudrate_cnt > MAX_DETECT_BAUD_NUM) {
        return OPRT_INVALID_PARM;
    }
    if (cfg->ext_trans) {
        memcpy(&s_gfw_core_ctx.trans, &cfg->trans, SIZEOF(AUDIO_GFW_TRANS_T));
    } else {
        memset(&s_gfw_core_ctx.trans, 0, SIZEOF(AUDIO_GFW_TRANS_T));
    }
    memcpy(s_gfw_core_ctx.baudrate_list, cfg_baudrate_list, SIZEOF(cfg_baudrate_list));
    s_gfw_core_ctx.port = cfg->port;
    s_gfw_core_ctx.state = CORE_STATE_INIT;
    s_gfw_core_ctx.mcu_cmd_ver = GFW_DEF_PROTOCOL_VER;

    TUYA_CALL_ERR_RETURN(gfw_core_cmd_queue_init(DEFAULT_CMD_QUEUE_NUM, &s_gfw_core_ctx.trans, s_gfw_core_ctx.port));
    TUYA_CALL_ERR_RETURN(tal_thread_create_and_start(&s_gfw_core_ctx.thread, NULL, NULL, __gfw_core_thread, NULL, &cfg->thread_cfg));
    TUYA_CALL_ERR_RETURN(tkl_semaphore_create_init(&s_gfw_core_ctx.sem,0,1));

    PR_DEBUG("gfw_core_init success!");

    return rt;
}

OPERATE_RET gfw_core_deinit(VOID)
{
    s_gfw_core_ctx.state = CORE_STATE_INIT;

    gfw_core_dispatcher_pause();

    gfw_core_dispatcher_stop();

    // tal_thread_delete(s_gfw_core_ctx.thread);
    s_gfw_core_ctx.sem_wait_time = -1;

    return OPRT_OK;
}

OPERATE_RET gfw_core_start(VOID)
{
    PR_DEBUG("gfw_core_start enter!");
    if (CORE_STATE_INIT == s_gfw_core_ctx.state) {
        if (s_gfw_core_ctx.auto_baudrate) {
            s_gfw_core_ctx.state = CORE_STATE_BAUDRATE_DETECT;
        } else {
            s_gfw_core_ctx.state = CORE_STATE_RUNNING;
        }
    }

    PR_DEBUG("gfw_core_init start!");
    return OPRT_OK;
}

BYTE_T gfw_core_get_mcu_protocol_ver(VOID)
{
    return s_gfw_core_ctx.mcu_cmd_ver;
}

OPERATE_RET gfw_core_set_mcu_protocol_ver(BYTE_T ver)
{
    s_gfw_core_ctx.mcu_cmd_ver = ver;
    return OPRT_OK;
}
