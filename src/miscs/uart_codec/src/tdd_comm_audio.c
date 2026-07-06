/**
 * @file ty_gfw_wifi.c
 * @brief
 * @version 0.1
 * @date 2023-06-25
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

#include "base_event.h"
#include "uni_log.h"
#include "gw_intf.h"
#include "ty_cJSON.h"
#include "ws_db_gw.h"
#include "tal_sleep.h"
#include "tal_workq_service.h"
#include "tal_semaphore.h"
#include "uni_queue.h"
#include "tuya_svc_netmgr_linkage.h"
#include "mqc_app.h"

#include "gfw_mcu_ota/gfw_mcu_ota.h"
#include "tdd_comm_audio.h"
#include "tal_queue.h"


#if defined(ENABLE_BT_SERVICE) && (ENABLE_BT_SERVICE==1)
#include "tuya_bt.h"
#include "tdl_comm_audio.h"
#endif

/***********************************************************
************************macro define************************
***********************************************************/
#define UART_SPK_CFG_SUCC  (0x00)
#define UART_SPK_CFG_FAIL  (0x01)
#define UART_SPK_MAX_LEN   (4096)

#define UART_VAD_START  (0)
#define UART_VAD_DATA   (1)
#define UART_VAD_STOP   (2)

#define MIC_SAMPLE_RATE    (16000)
#define MIC_BIT_WIDTH      (16)
#define MIC_CHANNEL        (1)

#define MIC_RECV_MAX_LEN   (1024)

#define SPK_SAMPLE_RATE    (16000)
#define SPK_BIT_WIDTH      (16)

#define SPK_SEND_DEF_LEN   (1152)

#define CMD_TIME_OUT_MS   (250)

#define INIT_TIME_OUT_MS  (5*1000)

#define SPK_MIN_VOLUME     (0)
#define SPK_DEF_VOLUME     (50)  // 0-100
#define SPK_MAX_VOLUME     (100)

#define MIC_MIN_DB         (0) 
#define MIC_DEF_DB         (26)  // 0-32(dB)
#define MIC_MAX_DB         (32) 

#define TDD_COMM_AUDIO_PARAM_CHECK(_min,_value,_max) ((_value)<(_min)?(_min):((_value)>(_max)?(_max):(_value)))

/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef struct {
    CHAR_T sw_ver[SW_VER_LEN + 1];
    BOOL_T inited;
    DELAYED_WORK_HANDLE qr_ver_hand;
    MUTEX_HANDLE uart_pack_mutex;
    SEM_HANDLE init_finsh;

    TDD_COMM_AUDIO_MIC_UPLOAD_CB mic_upload;
    QUEUE_HANDLE audio_test_result_queue;
}TDD_COMM_AUDIO_CTX_T;

/***********************************************************
***********************variable define**********************
***********************************************************/
STATIC TDD_COMM_AUDIO_CTX_T s_audio_ctx = {
    .sw_ver = {0},
    .inited = FALSE,
    .qr_ver_hand = NULL,
    .init_finsh = NULL,
    .mic_upload = NULL,
    .audio_test_result_queue = NULL
};

STATIC TDD_COMM_AUDIO_SPK_CFG_T g_spk_config = {
    .sps = SPK_SAMPLE_RATE,
    .bit = SPK_BIT_WIDTH,
    .volume = SPK_DEF_VOLUME,
#if (TDD_COMM_AUDIO_CODEC_TO_MCU_TYPE == TDD_COMM_AUDIO_CODEC_TO_MCU_PCM)
    .type = TDD_COMM_AUDIO_FRAME_FORMAT_PCM,
#elif (TDD_COMM_AUDIO_CODEC_TO_MCU_TYPE == TDD_COMM_AUDIO_CODEC_TO_MCU_MP3)
    .type = TDD_COMM_AUDIO_FRAME_FORMAT_MP3,
#else
    .type = TDD_COMM_AUDIO_FRAME_FORMAT_OPUS,
#endif
    .send_max_len = SPK_SEND_DEF_LEN,
};


STATIC TDD_COMM_AUDIO_MIC_CFG_T g_mic_config = {
        .sps = MIC_SAMPLE_RATE,
        .bit = MIC_BIT_WIDTH,
        .channel = MIC_CHANNEL,
        .gain = MIC_DEF_DB,
#if (TDD_COMM_AUDIO_UPLOAD_MODE == TDD_COMM_AUDIO_UPLOAD_MODE_SPEEX)
        .type = TDD_COMM_AUDIO_FRAME_FORMAT_SPEEX,
#elif (TDD_COMM_AUDIO_UPLOAD_MODE == TDD_COMM_AUDIO_UPLOAD_MODE_OPUS)
        .type = TDD_COMM_AUDIO_FRAME_FORMAT_OPUS,
#endif
        .recv_max_len = MIC_RECV_MAX_LEN,
};

STATIC DEV_TYPE_T s_comm_audio_ota_tp = DEV_ATTACH_MOD_10;

/***********************************************************
********************function declaration********************
***********************************************************/
STATIC OPERATE_RET ty_gfw_mcu_ver_query(VOID);
STATIC OPERATE_RET ty_gfw_voice_mic_config(VOID);
STATIC OPERATE_RET ty_gfw_voice_spk_config(VOID);

/***********************************************************
***********************function define**********************
***********************************************************/
// 波特率锁定事件处理函数
// 当波特率锁定事件发生时，会调用此函数
// 参数：data - 事件数据（未使用）
// 返回值：返回MCU版本查询结果
STATIC OPERATE_RET __evt_baudrate_locked(VOID_T *data)
{
    PR_DEBUG("baudrate locked");
    return ty_gfw_mcu_ver_query();
}

STATIC OPERATE_RET __evt_mcu_ota_end(VOID_T *data)
{
    PR_DEBUG("on mcu ota end");
    return tal_workq_start_delayed(s_audio_ctx.qr_ver_hand, 6000, LOOP_ONCE);
}

// MCU版本查询函数
// 该函数用于向设备发送MCU版本查询命令
// 参数：无
// 返回值：始终返回OPRT_OK表示操作成功
STATIC OPERATE_RET ty_gfw_mcu_ver_query(VOID)
{
    UCHAR_T send_data[1] = {GFW_0x92_VER_QUERY};
    gfw_core_cmd_send_with_params(AUDIO_GFW_CMD_TYPE_DEFAULT, GFW_CMD_VOICE_SERV, send_data, SIZEOF(send_data), GFW_CMD_RESPONSE_TIMEOUT, NULL, GFW_CMD_WAIT_FOREVER);
    
    return OPRT_OK;
}

STATIC OPERATE_RET _mic_uart_config(CONST TDD_COMM_AUDIO_MIC_CFG_T *mic_cfg)
{
    UCHAR_T *buf = NULL;
    UINT_T buf_len = 0;
    UCHAR_T mic_type = TDD_COMM_AUDIO_FRAME_FORMAT_SPEEX;


    TDD_COMM_AUDIO_MIC_CFG_T mic_config = {0};

    if (NULL == mic_cfg) {
        return OPRT_INVALID_PARM;
    }
    
    memcpy(&mic_config, mic_cfg, SIZEOF(TDD_COMM_AUDIO_MIC_CFG_T));

    TAL_PR_NOTICE("set mic gain : %d ",mic_config.gain);

    mic_config.sps = DWORD_SWAP(mic_config.sps);
    mic_config.recv_max_len = DWORD_SWAP(mic_config.recv_max_len);

    buf_len = SIZEOF(TDD_COMM_AUDIO_MIC_CFG_T) + 1;
    buf = (UCHAR_T *)Malloc(buf_len);
    if (NULL == buf) {
        PR_ERR("mic config malloc failed");
        return OPRT_MALLOC_FAILED;
    }

    memset(buf, 0, buf_len);
    buf[0] = GFW_0x92_MIC_CFG;
    memcpy(buf + 1, &mic_config, SIZEOF(TDD_COMM_AUDIO_MIC_CFG_T));
    gfw_core_cmd_send_timeout(GFW_CMD_VOICE_SERV, buf, buf_len, GFW_CMD_RESPONSE_TIMEOUT, NULL);

    Free(buf);
    return OPRT_OK;
}

/**
 * @brief 配置语音麦克风的参数并发送配置命令
 * 
 * 该函数用于初始化语音麦克风的配置参数，包括采样率、位宽、声道数等，
 * 然后将这些参数打包成特定格式的数据，并通过核心命令接口发送配置命令。
 * 
 * @return OPERATE_RET 操作结果，OPRT_OK 表示操作成功，其他值表示失败
 */
STATIC OPERATE_RET ty_gfw_voice_mic_config(VOID)
{
    OPERATE_RET ret = OPRT_OK;
    ret = _mic_uart_config(&g_mic_config);
    return ret;
}

STATIC OPERATE_RET _speaker_uart_config(CONST TDD_COMM_AUDIO_SPK_CFG_T *spk_cfg)
{
    UCHAR_T *buf = NULL;
    UINT_T buf_len = 0;
    OPERATE_RET ret = OPRT_OK;
    TDD_COMM_AUDIO_SPK_CFG_T spk_config = {0};

    if (NULL == spk_cfg) {
        return OPRT_INVALID_PARM;
    }
    
    memcpy(&spk_config, spk_cfg, SIZEOF(TDD_COMM_AUDIO_SPK_CFG_T));

    TAL_PR_NOTICE("set speaker volume :%d ",spk_config.volume);

    spk_config.sps = DWORD_SWAP(spk_config.sps);
    spk_config.send_max_len = DWORD_SWAP(spk_config.send_max_len);

    buf_len = SIZEOF(TDD_COMM_AUDIO_SPK_CFG_T) + 1;
    buf = (UCHAR_T *)Malloc(buf_len);
    if (NULL == buf) {
        PR_ERR("mic config malloc failed");
        return OPRT_MALLOC_FAILED;
    }

    memset(buf, 0, buf_len);
    buf[0] = GFW_0x92_SPK_CFG;
    memcpy(buf + 1, &spk_config, SIZEOF(TDD_COMM_AUDIO_SPK_CFG_T));
    ret = gfw_core_cmd_send_timeout(GFW_CMD_VOICE_SERV, buf, buf_len, GFW_CMD_RESPONSE_TIMEOUT, NULL);

    Free(buf);
    return ret;
}

STATIC OPERATE_RET ty_gfw_voice_spk_config(VOID)
{
    OPERATE_RET ret = OPRT_OK;
    ret = _speaker_uart_config(&g_spk_config);
    return ret;
}

STATIC OPERATE_RET __gfw_cmd_wifi_version_query(AUDIO_GFW_CMD_DATA_T *data)
{
    PR_DEBUG("__gfw_cmd_wifi_version_query");

    // MCU版本号更新
    if (data->len > (3 + SW_VER_LEN)) {
        memcpy(s_audio_ctx.sw_ver, data->data + 3, SW_VER_LEN);
    } else {
        memcpy(s_audio_ctx.sw_ver, data->data + 3, data->len - 3);
    }
    PR_DEBUG("mcu sw ver:%s", s_audio_ctx.sw_ver);
    

    PR_DEBUG("MCU POWER FLAG:%02X", data->data[0]);
    if (data->data[0] == 0x00) {
        tal_workq_stop_delayed(s_audio_ctx.qr_ver_hand);
    }

    // MIC & SPK 配置
    ty_gfw_voice_mic_config();
    ty_gfw_voice_spk_config();

    PR_DEBUG("publish audio version event");
    ty_publish_event(GFW_EVENT_AUDIO_VERSION, NULL);
    PR_DEBUG("publish audio init event");

    if (!s_audio_ctx.inited) {        
        s_audio_ctx.inited = TRUE;

        ty_publish_event(GFW_EVENT_AUDIO_INIT, NULL);

        tal_semaphore_post(s_audio_ctx.init_finsh);

        return OPRT_OK;
    }
    else//Audio主动重启：异常复位||OTA结束
    {
        ty_publish_event(GFW_EVENT_AUDIO_RESET, NULL);
    }

    return OPRT_OK;
}

STATIC OPERATE_RET __gfw_cmd_wifi_mic_config(AUDIO_GFW_CMD_DATA_T *data)
{
    PR_DEBUG("recv mic config ack");

    return OPRT_OK;
}

STATIC OPERATE_RET __gfw_cmd_wifi_spk_config(AUDIO_GFW_CMD_DATA_T *data)
{
    UINT_T length = 0;

    if ((data->data[0] == UART_SPK_CFG_FAIL) && (data->len >= 5)) {
        length = ((data->data[1]<<24)|(data->data[2]<<16)|(data->data[3]<<8)|(data->data[4]));
        if (length < UART_SPK_MAX_LEN) {
            g_spk_config.send_max_len = length;
        }
        PR_DEBUG("recv spk config new len:%d", g_spk_config.send_max_len);
    }else {
        PR_DEBUG("recv spk config default len:%d", g_spk_config.send_max_len);
    }

    return OPRT_OK;
}

STATIC OPERATE_RET __gfw_cmd_wifi_wake_config(AUDIO_GFW_CMD_DATA_T *data)
{
    PR_DEBUG("recv wake config ack");

    return OPRT_OK;
}

STATIC OPERATE_RET __gfw_cmd_wifi_mic_data(AUDIO_GFW_CMD_DATA_T *data)
{
    if(data->len < 1) {
        PR_ERR("recv mic data len is 0");
        return OPRT_OK;
    }

    PR_TRACE("recv mic data type:%d len:%d", data->data[0], data->len);

    // tuya_debug_hex_dump("mic_data:", 64, data->data, data->len);
    switch (data->data[0])
    {
    case UART_VAD_START:
        s_audio_ctx.mic_upload(TDD_COMM_AUDIO_STATUS_VAD_START, NULL, 0);
        break;
    case UART_VAD_DATA:
        s_audio_ctx.mic_upload(TDD_COMM_AUDIO_STATUS_RECEIVING, data->data + 1, data->len - 1);
        break;
    case UART_VAD_STOP:
        s_audio_ctx.mic_upload(TDD_COMM_AUDIO_STATUS_VAD_END, NULL, 0);
        break;
    
    default:
        break;
    }

    return OPRT_OK;
}

STATIC OPERATE_RET __gfw_cmd_wifi_spk_play(AUDIO_GFW_CMD_DATA_T *data)
{
    UINT_T recv_ack_no = 0;
    if (data->len < 5) {
        PR_ERR("recv spk play len is less than 5");
        return OPRT_OK;
    }

    recv_ack_no = ((data->data[1]<<24)|(data->data[2]<<16)|(data->data[3]<<8)|(data->data[4]));
    PR_DEBUG("recv spk play ack %d", recv_ack_no);
    return OPRT_OK;
}

STATIC OPERATE_RET __gfw_cmd_wifi_voice_config(AUDIO_GFW_CMD_DATA_T *data)
{
    PR_DEBUG("recv voice config ack");
    return OPRT_OK;
}

STATIC OPERATE_RET __gfw_cmd_wifi_voice_wake(AUDIO_GFW_CMD_DATA_T *data)
{
    PR_DEBUG("recv voice wake data len:%d", data->len);
    UINT_T wake_id = 0;

    if (data->len == 4) {
        memcpy(&wake_id, data->data, 4);
        wake_id = DWORD_SWAP(wake_id);
        PR_NOTICE("recv voice wake %08x", wake_id);
        OPERATE_RET rt = ty_publish_event(GFW_EVENT_AUDIO_WAKE_UP, &wake_id);
        PR_NOTICE("Publish event with code %d", rt);
    }   

    return OPRT_OK;
}

STATIC OPERATE_RET __gfw_cmd_wifi_voice_sleep(AUDIO_GFW_CMD_DATA_T *data)
{
    return OPRT_OK;
}

STATIC OPERATE_RET __gfw_cmd_wifi_audio_test(AUDIO_GFW_CMD_DATA_T *data)
{
    OPERATE_RET rt;
    PR_DEBUG("recv audio test ack, len=%d", data->len);

    if (data->len != 1 || data->cmd != GFW_0x92_AUDIO_TEST){
        PR_ERR("audio test ack param error");
        return OPRT_INVALID_PARM;
    }

    BOOL_T test_result = (data->data[0] == 0x00);
    PR_DEBUG("===== Got audio test result 0x%02X, %s =====", data->data[0], test_result ? "SUCCESS" : "FAILED");

    if (s_audio_ctx.audio_test_result_queue == NULL){
        PR_ERR("fail to record audio test result because queue is null");
        return OPRT_NOT_EXIST;
    }

    rt = tal_queue_post(s_audio_ctx.audio_test_result_queue, &test_result, 0);
    if (rt != OPRT_OK) {
        PR_ERR("fail to record audio test result with error code %d", rt);
        return rt;
    }

    PR_DEBUG ("audio test result has put into queue.");
    
    return OPRT_OK;
}

STATIC VOID_T query_mcu_ver_cb(VOID_T *data)
{
    PR_DEBUG("mcu ota succ start query mcu ver");
    ty_gfw_mcu_ver_query();
}

/***********************************************************
*******************eternal function define******************
***********************************************************/
UINT_T tdd_comm_audio_spk_send_max_len_get(VOID)
{
    return g_spk_config.send_max_len;
}

BOOL_T tdl_comm_audio_is_inited(VOID) {
    return tdd_comm_audio_is_inited();
}

OPERATE_RET tdd_comm_audio_spk_audio_data_send(UCHAR_T type, UCHAR_T* data, UINT_T len) {
    UCHAR_T *buf = NULL;
    UINT_T buf_len = 0;

    buf_len = len + 2 + 4;
    buf = (UCHAR_T *)Malloc(buf_len);
    if (NULL == buf) {
        PR_ERR("spk data malloc failed");
        return OPRT_MALLOC_FAILED;
    }

    tal_mutex_lock(s_audio_ctx.uart_pack_mutex);
    STATIC UINT_T spk_no = 0;
    if (type == 0){
        spk_no = 0;
    }
    memset(buf, 0, buf_len);
    buf[0] = GFW_0x92_SPK_PLAY;
    buf[1] = type;
    buf[2] = (spk_no >> 24) & 0xFF;
    buf[3] = (spk_no >> 16) & 0xFF;
    buf[4] = (spk_no >> 8) & 0xFF;
    buf[5] = spk_no & 0xFF;
    if (len > 0) {
        memcpy(buf + 6, data, len);
    }
    // PR_DEBUG("speaker_send num:%d type:%d len:%d", spk_no, type, len);
    spk_no++;
    tal_mutex_unlock(s_audio_ctx.uart_pack_mutex);

    gfw_core_cmd_send_direct(GFW_CMD_VOICE_SERV, buf, buf_len);
    Free(buf);
    return OPRT_OK;
}

VOID tdd_comm_audio_spk_volume_init(UCHAR_T volume)
{
    g_spk_config.volume = volume;
    return;
}

OPERATE_RET tdd_comm_audio_spk_volume_set(UCHAR_T volume)
{
    OPERATE_RET ret = OPRT_OK;

    g_spk_config.volume = TDD_COMM_AUDIO_PARAM_CHECK(SPK_MIN_VOLUME,volume,SPK_MAX_VOLUME);
    ret = _speaker_uart_config(&g_spk_config);
    return ret;
}

UCHAR_T tdd_comm_audio_spk_volume_get(VOID)
{
    return g_spk_config.volume;
}

OPERATE_RET tdd_comm_audio_mic_upload_regiter(TDD_COMM_AUDIO_MIC_UPLOAD_CB cb)
{
    s_audio_ctx.mic_upload = cb;
    return OPRT_OK;
}

OPERATE_RET tdd_comm_audio_mic_gain_set(UCHAR_T gain)
{
    OPERATE_RET ret = OPRT_OK;
    
    g_mic_config.gain = TDD_COMM_AUDIO_PARAM_CHECK(MIC_MIN_DB,gain,MIC_MAX_DB);
    ret = _mic_uart_config(&g_mic_config);
    return ret;
}

UCHAR_T tdd_comm_audio_mic_gain_get(VOID)
{
    return g_mic_config.gain;
}

OPERATE_RET tdd_comm_audio_voice_cfg_set(TDD_COMM_AUDIO_CFG_TYPE_E type, UCHAR_T data)
{
    OPERATE_RET ret = OPRT_OK;
    UCHAR_T send_data[3] = {GFW_0x92_VOICE_CFG, type, data};

    ret = gfw_core_cmd_send_timeout(GFW_CMD_VOICE_SERV, send_data, SIZEOF(send_data), CMD_TIME_OUT_MS, NULL);
    PR_DEBUG("tdd_comm_audio_voice_cfg_set type:%02X data:%02X ret:%d", type, data, ret);
    return ret;
}

OPERATE_RET tdd_comm_audio_recording_and_play_test(UCHAR_T time)
{
    UCHAR_T send_data[2] = {GFW_0x92_VOICE_TEST, time};
    return gfw_core_cmd_send(GFW_CMD_VOICE_SERV, send_data, SIZEOF(send_data));
}

CHAR_T* tdd_comm_audio_sw_ver_get(VOID)
{
    return s_audio_ctx.sw_ver;
}

VOID_T tdd_comm_audio_ota_tp_set(DEV_TYPE_T tp)
{
    s_comm_audio_ota_tp = tp;
}

DEV_TYPE_T tdd_comm_audio_ota_tp_get(VOID)
{
    return s_comm_audio_ota_tp;
}

BOOL_T tdd_comm_audio_is_inited(VOID) {
    return s_audio_ctx.inited != FALSE;
}

OPERATE_RET tdd_comm_audio_init_audio_test(VOID) {
    if(s_audio_ctx.audio_test_result_queue != NULL){
        return OPRT_OK;
    }
    return tal_queue_create_init(&s_audio_ctx.audio_test_result_queue, sizeof(BOOL_T), 1);
}

BOOL_T tdd_comm_audio_is_last_audio_test_success(VOID){
    BOOL_T test_result;
    OPERATE_RET rt;
    if (s_audio_ctx.audio_test_result_queue == NULL) {
        PR_ERR("Failed to get test result because queue is NULL");
        return FALSE;
    }

    rt = tal_queue_fetch(s_audio_ctx.audio_test_result_queue, &test_result, 1000);
    if (rt != OPRT_OK) {
        PR_ERR("Failed to get test result with error code %d", rt);
        return FALSE;
    }

    PR_NOTICE("Got audio test result from queue. result is %s", test_result ? "PASS" : "FAILED");
    return test_result;

}

STATIC VOID __gfw_cmd_audio_event_notify(AUDIO_GFW_CMD_DATA_T* data) {
    DWORD_T evt_id = 0;
    memcpy(&evt_id, data->data+0, sizeof(evt_id));
    evt_id = DWORD_SWAP(evt_id);
    // PR_INFO("Got event %08xH", evt_id);
    ty_publish_event(GFW_EVENT_AUDIO_EVENT_NOTIFY, &evt_id);
}

CONST AUDIO_GFW_CMD_CFG_T s_gfw_wifi_voice_cmd[] GFW_CMD_SECTION = {
    {GFW_CMD_VOICE_SERV, GFW_0x92_VER_QUERY, __gfw_cmd_wifi_version_query}, // skip ack
    {GFW_CMD_VOICE_SERV, GFW_0x92_MIC_CFG, __gfw_cmd_wifi_mic_config},
    {GFW_CMD_VOICE_SERV, GFW_0x92_SPK_CFG, __gfw_cmd_wifi_spk_config},
    {GFW_CMD_VOICE_SERV, GFW_0x92_WAKE_CFG, __gfw_cmd_wifi_wake_config},
    {GFW_CMD_VOICE_SERV, GFW_0x92_MIC_DATA, __gfw_cmd_wifi_mic_data},
    {GFW_CMD_VOICE_SERV, GFW_0x92_SPK_PLAY, __gfw_cmd_wifi_spk_play},
    {GFW_CMD_VOICE_SERV, GFW_0x92_VOICE_CFG, __gfw_cmd_wifi_voice_config},
    {GFW_CMD_VOICE_SERV, GFW_0x92_VOICE_WAKE, __gfw_cmd_wifi_voice_wake},
    {GFW_CMD_VOICE_SERV, GFW_0x92_VOICE_SLEEP, __gfw_cmd_wifi_voice_sleep},
    {GFW_CMD_VOICE_SERV, GFW_0x92_AUDIO_TEST, __gfw_cmd_wifi_audio_test},
    {GFW_CMD_VOICE_SERV, GFW_0x92_EVENT_NOTIFY, __gfw_cmd_audio_event_notify},
};

OPERATE_RET tdd_comm_audio_init(TDD_COMM_AUDIO_CFG_T *cfg, UINT_T timeout)
{
    OPERATE_RET rt = OPRT_OK;
    UINT_T time  = (0 == timeout)?INIT_TIME_OUT_MS:timeout;
    
    AUDIO_GFW_CORE_CFG_T core_cfg = {0};
    core_cfg.port = cfg->port;
    core_cfg.auto_baudrate = TRUE;
    core_cfg.thread_cfg.priority = THREAD_PRIO_1;
    core_cfg.thread_cfg.stackDepth = 4*1024;
    core_cfg.thread_cfg.thrdname = "gfw_core";
    TUYA_CALL_ERR_RETURN(gfw_core_init(&core_cfg));

    if(FALSE == s_audio_ctx.inited)
    {
        if(NULL != cfg->mic_cfg)
        {
            memcpy(&g_mic_config,cfg->mic_cfg,SIZEOF(TDD_COMM_AUDIO_MIC_CFG_T));
        }

        if(NULL != cfg->spk_cfg)
        {
            memcpy(&g_spk_config,cfg->spk_cfg,SIZEOF(TDD_COMM_AUDIO_SPK_CFG_T));   
        }

        gfw_core_cmd_register(s_gfw_wifi_voice_cmd, CNTSOF(s_gfw_wifi_voice_cmd));

        TUYA_CALL_ERR_RETURN(tal_semaphore_create_init(&s_audio_ctx.init_finsh,0,1));
        ty_subscribe_event(EVENT_BAUDRATE_LOCKED, "baudrate_locked", __evt_baudrate_locked, SUBSCRIBE_TYPE_NORMAL); 
        ty_subscribe_event(GFW_EVENT_AUDIO_OTA_END, "ota_end", __evt_mcu_ota_end, SUBSCRIBE_TYPE_NORMAL); 
        
        tal_workq_init_delayed(WORKQ_SYSTEM, query_mcu_ver_cb, NULL, &s_audio_ctx.qr_ver_hand);

        TUYA_CALL_ERR_RETURN(gfw_mcu_ota_init());
        TUYA_CALL_ERR_RETURN(gfw_core_start());
    }
    else//低功耗重入
    {
        s_audio_ctx.inited = FALSE; //初始化标志重置
    }

    if(tal_semaphore_wait(s_audio_ctx.init_finsh,time))
    {
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

OPERATE_RET tdd_comm_audio_deinit(VOID)
{
    return gfw_core_deinit();
}
