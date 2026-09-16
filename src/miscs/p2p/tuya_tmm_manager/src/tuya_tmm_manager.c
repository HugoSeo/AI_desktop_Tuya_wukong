/**
 * @file tuya_tmm_manager.c
 * @author baijue.huang@tuya.com
 * @brief tuya tmm manager
 * @version 0.1
 * @date 2022-8-31
 * 
 * @copyright Copyright (c) 2021
 * 
 */

/***********************************************************************
 ** INCLUDE                                                           **
 **********************************************************************/
#include <stdio.h>
#include <string.h>

#include "tal_memory.h"
#include "tal_mutex.h"
#include "tal_system.h"
#include "tal_thread.h"
#include "uni_log.h"
#include "tuya_iot_internal_api.h"
#include "gw_intf.h"

#include "tuya_tmm_control.h"
#include "tuya_tmm_stream.h"
#include "tuya_tmm_manager.h"
#include "tuya_tmm_manager_atop.h"
#include "tuya_ringbuf.h"

#include "tuya_tmm_manager_queue.h"

/* P2P SDK 单调毫秒时钟(mid_p2p 内部头未对外,extern 声明;
 * 上行音视频 pts 共用此钟,见 __tmm_audio_send_task 与 __voip_h264_cb) */
extern uint64_t tuya_p2p_misc_get_current_time_ms(void);

/* 上行音频 jitter 参数（8k/16bit/mono PCM）。
 * 单帧 40ms = 8000*16/8*0.04 = 640B；上游一次喂 ~80ms=1280B。
 * jitter 容量取 6 帧(240ms)=3840B，吸收上游 80ms 突发 + 发送侧抖动；发送线程 40ms 节拍。 */
#define TMM_AUDIO_FRAME_BYTES          640      // 40ms@8k/16bit：与 SDK correct_timestamp(frame_interval=40) 帧节拍对齐
#define TMM_AUDIO_JITTER_BUF_SIZE      (TMM_AUDIO_FRAME_BYTES * 6)   // ~240ms(6帧)，吸收上游 80ms 突发 + 发送侧抖动
#define TMM_AUDIO_SEND_PERIOD_MS       40       // 40ms 匀速：8k/16bit 实时率(16KB/s)，匹配 SDK 40ms 帧节拍，避免 2x 实时灌爆 KCP
#define TMM_AUDIO_UNDERRUN_FRAMES      10       // 连续取空达 10 帧(400ms) 判为"长时间没数据"异常(上游没喂/喂得太慢)

/* 前向声明：上行音频 40ms 匀速发送线程，定义在 data_feed 之后 */
STATIC VOID __tmm_audio_send_task(VOID *pv);



/***********************************************************************
 ** CONSTANT ( MACRO AND ENUM )                                       **
 **********************************************************************/

#define TUYA_TMM_MANAGER_DEFAULT_CALL_TIMEOUT       180
#define TUYA_TMM_DEVICE_ID_LEN                      32
#define TUYA_TMM_PEER_NAME_LEN                      64
#define TUYA_TMM_MANAGER_DEV_INFO_GET               "tuya.device.rtc.device.info.get"
#define TUYA_TMM_MANAGER_DEV_RTC_CALLABLE_GET       "tuya.device.rtc.callable.list"

/***********************************************************************
 ** STRUCT                                                            **
 **********************************************************************/

typedef enum {
    TUYA_TMM_MANAGER_STA_IDLE = 0,
    TUYA_TMM_MANAGER_STA_INCOMING,
    TUYA_TMM_MANAGER_STA_CALLING,
    TUYA_TMM_MANAGER_STA_P2P_DISCONNECTING,
    TUYA_TMM_MANAGER_STA_P2P_CONNECTING,
    TUYA_TMM_MANAGER_STA_P2P_CONNECTED,
} TUYA_TMM_MANAGER_STA_E;

typedef struct {
    BOOL_T                          init;
    BOOL_T                          task_run;
    INT_T                           status;
    MUTEX_HANDLE                    lock;
    THREAD_HANDLE                   thrd_handle;
    TUYA_TMM_QUEUE_HANDLE_T         queue_handle;

    TUYA_TMM_MANAGER_EVENT_CB       event_cb;
    TUYA_TMM_CONTROL_STREAM_TYPE_E  stream_type;
    INT_T                           incoming_type;  // 0-dev, 1-app

    TUYA_TMM_STREAM_CONF_S          stream_conf;
    CHAR_T                          peer_name[TUYA_TMM_PEER_NAME_LEN];
    BOOL_T                          send_audio_enable;

    /* 上行音频 jitter 缓冲：上游每 ~80ms 喂一次 80ms 大包，但 P2P 发送侧每 ~40ms 取一帧，
     * 节奏错配导致发送侧周期性取空（no audio data count 上涨，对端断续）。
     * data_feed 把数据缓存进 jitter buffer，由独立 40ms 线程匀速取送，吸收节奏不匹配。 */
    TUYA_RINGBUFF_T                 audio_jitter_buf;   // jitter 环形缓冲（8k PCM）
    MUTEX_HANDLE                    audio_jitter_lock;  // jitter 读写锁
    THREAD_HANDLE                   audio_send_thrd;    // 40ms 匀速发送线程
    BOOL_T                          audio_send_run;     // 发送线程运行标志
    SYS_TIME_T                      audio_last_feed_ms; // 上游最近一次喂入时刻(墙钟ms)，供发送线程判 feed 是否及时
} TUYA_TMM_MANAGER_HANDLE_S;

typedef enum {
    TUYA_TMM_MGR_MSG_CMD_CONNECT,
    TUYA_TMM_MGR_MSG_CMD_DISCONNECT,
    TUYA_TMM_MGR_MSG_CMD_TYPE_CHANGE,
    TUYA_TMM_MGR_MSG_CMD_MIC_CHANGE,
} TUYA_TMM_MGR_MSG_CMD_E;

typedef struct {
    TUYA_TMM_MGR_MSG_CMD_E         cmd;
    INT_T                          ext;
} TUYA_TMM_MANAGER_MSG_S;

/***********************************************************************
 ** VARIABLE                                                          **
 **********************************************************************/

STATIC TUYA_TMM_MANAGER_HANDLE_S        g_tmm_manager_handle = {0};

/***********************************************************************
 ** FUNCTON                                                           **
 **********************************************************************/

OPERATE_RET tuya_tmm_manager_evt_cb(TUYA_TMM_MANAGER_EVENT_E event, VOID *data, INT_T len)
{
    if (g_tmm_manager_handle.event_cb) {
        return g_tmm_manager_handle.event_cb(event, data, len);
    }

    return OPRT_OK;
}

/**
 * @brief Check whether call target is mobile app (key_*)
 * @param[in] target_id call target id
 * @return TRUE if app target
 */
STATIC BOOL_T __tmm_is_app_target(CONST CHAR_T *target_id)
{
    if (target_id == NULL || target_id[0] == '\0') {
        return FALSE;
    }

    return (strncmp(target_id, "key_", 4) == 0) ? TRUE : FALSE;
}

/**
 * @brief Check whether device-to-device P2P connect is required
 * @param[in] peer_dev_id peer device id
 * @param[in] local_key peer local key
 * @return TRUE if should call tuya_ipc_p2p_client_connect
 * @note App calls use moto/cloud RTC path and must not enter p2p_client_connect.
 */
STATIC BOOL_T __tmm_need_p2p_connect(CONST CHAR_T *peer_dev_id, CONST CHAR_T *local_key)
{
    CONST CHAR_T *self_id = get_gw_cntl()->gw_if.id;

    if (peer_dev_id == NULL || peer_dev_id[0] == '\0') {
        return FALSE;
    }

    if (__tmm_is_app_target(peer_dev_id)) {
        return FALSE;
    }

    if (local_key == NULL || local_key[0] == '\0') {
        return FALSE;
    }

    if (self_id != NULL && strcmp(peer_dev_id, self_id) == 0) {
        PR_WARN("tmm skip p2p connect: peer is self (%s)", peer_dev_id);
        return FALSE;
    }

    return TRUE;
}

STATIC VOID __tuya_tmm_update_callable_device(const CHAR_T *target_id)
{
    ty_cJSON *result = NULL;

    if (target_id == NULL) {
        return;
    }

    g_tmm_manager_handle.incoming_type = 0;

    if (tuya_tmm_manager_atop_contact_list(&result) != OPRT_OK) {
        PR_ERR("get contact list failed");
        return;
    }

    if (result) {
        INT_T dev_num = ty_cJSON_GetArraySize(result);
        ty_cJSON *id = NULL;
        ty_cJSON *type = NULL;
        ty_cJSON *name = NULL;
        ty_cJSON *item = NULL;

        for (INT_T index = 0; index < dev_num; index++) {
            item = ty_cJSON_GetArrayItem(result, index);
            if (item == NULL) {
                continue;
            }

            id = ty_cJSON_GetObjectItem(item, "resourceId");
            name = ty_cJSON_GetObjectItem(item, "resourceName");
            type = ty_cJSON_GetObjectItem(item, "resourceType");

            if (id == NULL || name == NULL || type == NULL) {
                continue;
            }

            if (strcmp(id->valuestring, target_id) == 0) {
                g_tmm_manager_handle.incoming_type = type->valueint;
            }

            PR_DEBUG("rtc devices: id:%s, name:%s, type:%d", id->valuestring, name->valuestring, type->valueint);
        }

        ty_cJSON_Delete(result);
    }
}

STATIC OPERATE_RET __tuya_tmm_stream_event_cb(TUYA_TMM_STREAM_EVENT_E event)
{
    BOOL_T was_connecting = FALSE;

    if (event == TUYA_TMM_STREAM_EVT_SPEAKER_START ||
        event == TUYA_TMM_STREAM_EVT_VIDEO_START ||
        event == TUYA_TMM_STREAM_EVT_AUDIO_START) {
        if (g_tmm_manager_handle.status == TUYA_TMM_MANAGER_STA_P2P_CONNECTING) {
            was_connecting = TRUE;
            tal_mutex_lock(g_tmm_manager_handle.lock);
            g_tmm_manager_handle.status = TUYA_TMM_MANAGER_STA_P2P_CONNECTED;
            tal_mutex_unlock(g_tmm_manager_handle.lock);
        }
    }

    switch (event) {
    case TUYA_TMM_STREAM_EVT_SPEAKER_START:
        tuya_tmm_manager_evt_cb(TUYA_TMM_MANAGER_EVT_PLAY_START, NULL, 0);
        break;
    case TUYA_TMM_STREAM_EVT_SPEAKER_STOP:
        tuya_tmm_manager_evt_cb(TUYA_TMM_MANAGER_EVT_PLAY_STOP, NULL, 0);
        break;
    case TUYA_TMM_STREAM_EVT_VIDEO_START:
        tuya_tmm_manager_evt_cb(TUYA_TMM_MANAGER_EVT_VIDEO_START, NULL, 0);
        break;
    case TUYA_TMM_STREAM_EVT_VIDEO_STOP:
        tuya_tmm_manager_evt_cb(TUYA_TMM_MANAGER_EVT_VIDEO_STOP, NULL, 0);
        break;
    case TUYA_TMM_STREAM_EVT_AUDIO_START:
        tuya_tmm_stream_audio_uplink_reset();
        tal_mutex_lock(g_tmm_manager_handle.audio_jitter_lock);
        tuya_ring_buff_reset(g_tmm_manager_handle.audio_jitter_buf);
        g_tmm_manager_handle.audio_last_feed_ms = 0;   // 清零：新通话起始，发送线程在首次喂入前不判异常
        tal_mutex_unlock(g_tmm_manager_handle.audio_jitter_lock);
        g_tmm_manager_handle.send_audio_enable = TRUE;
        /* IPC 预览式视频对讲(App 直接拉流,不走 TMM 控制呼叫)不会在 stream_conf 里置
         * enable_send_audio;而 AUDIO_START(对端来拉音频)本身即表示"要发本机音频",
         * 补置使能,否则 data_feed 会因 enable_send_audio=FALSE 丢弃全部上行麦克风帧。
         * 控制呼叫路径下该值已为 TRUE,此处等幂;麦克风静音仍由 call_mic_change 复位。 */
        g_tmm_manager_handle.stream_conf.enable_send_audio = TRUE;
        tuya_tmm_manager_evt_cb(TUYA_TMM_MANAGER_EVT_AUDIO_START, NULL, 0);
        break;
    case TUYA_TMM_STREAM_EVT_AUDIO_STOP:
        g_tmm_manager_handle.send_audio_enable = FALSE;
        tuya_tmm_manager_evt_cb(TUYA_TMM_MANAGER_EVT_AUDIO_STOP, NULL, 0);
        break;
    default:
        break;
    }

    return OPRT_OK;
}

STATIC VOID __tuya_tmm_manager_task(VOID *pv)
{
    TUYA_TMM_MANAGER_MSG_S     msg = {0};
    UINT_T                     loop_cnt = 0;

    g_tmm_manager_handle.task_run = TRUE;
    PR_NOTICE("[tmm_diag] manager task started, queue=%p", (VOID *)g_tmm_manager_handle.queue_handle);

    while (g_tmm_manager_handle.task_run) {
        memset(&msg, 0, sizeof(TUYA_TMM_MANAGER_MSG_S));
        if (tuya_tmm_mgr_queue_fetch(g_tmm_manager_handle.queue_handle, (VOID *)&msg, 500) == OPRT_OK) {
            PR_NOTICE("[tmm_diag] dequeue cmd=%d status=%d", msg.cmd, g_tmm_manager_handle.status);
            switch (msg.cmd) {
            case TUYA_TMM_MGR_MSG_CMD_CONNECT:
                PR_NOTICE("[tmm_diag] CONNECT enter -> tuya_tmm_stream_connect");
                if (tuya_tmm_stream_connect(&g_tmm_manager_handle.stream_conf) != OPRT_OK) {
                    PR_ERR("[tmm_diag] CONNECT tuya_tmm_stream_connect FAILED -> hangup");
                    tal_mutex_lock(g_tmm_manager_handle.lock);
                    tuya_tmm_control_hangup();
                    g_tmm_manager_handle.status = TUYA_TMM_MANAGER_STA_IDLE;
                    tal_mutex_unlock(g_tmm_manager_handle.lock);
                    tuya_tmm_manager_evt_cb(TUYA_TMM_MANAGER_EVT_HANGUP, NULL, 0);
                } else {
                    PR_NOTICE("[tmm_diag] CONNECT tuya_tmm_stream_connect OK -> P2P_CONNECTED");
                    tal_mutex_lock(g_tmm_manager_handle.lock);
                    g_tmm_manager_handle.status = TUYA_TMM_MANAGER_STA_P2P_CONNECTED;
                    tal_mutex_unlock(g_tmm_manager_handle.lock);
                }
                break;
            case TUYA_TMM_MGR_MSG_CMD_DISCONNECT:
                g_tmm_manager_handle.stream_conf.enable_send_audio = FALSE;
                g_tmm_manager_handle.stream_conf.enable_recv_audio = FALSE;
                g_tmm_manager_handle.stream_conf.enable_send_video = FALSE;
                g_tmm_manager_handle.stream_conf.enable_recv_video = FALSE;
                if (tuya_tmm_stream_disconnect(&g_tmm_manager_handle.stream_conf) != OPRT_OK) {
                    PR_ERR("tuya_tmm_stream_disconnect failed");
                }
                break;
            case TUYA_TMM_MGR_MSG_CMD_TYPE_CHANGE:
                if (TUYA_TMM_MANAGER_CALL_TYPE_AUDIO == msg.ext) {
                    g_tmm_manager_handle.stream_conf.enable_send_video = FALSE;
                } else if (TUYA_TMM_MANAGER_CALL_TYPE_VIDEO == msg.ext) {
                    g_tmm_manager_handle.stream_conf.enable_send_video = TRUE;
                }
                if (tuya_tmm_stream_ctrl(&g_tmm_manager_handle.stream_conf) != OPRT_OK) {
                    PR_ERR("tuya_tmm_stream_disconnect failed");
                }
                break;
            case TUYA_TMM_MGR_MSG_CMD_MIC_CHANGE:
                g_tmm_manager_handle.stream_conf.enable_send_audio = msg.ext;
                if (tuya_tmm_stream_ctrl(&g_tmm_manager_handle.stream_conf) != OPRT_OK) {
                    PR_ERR("tuya_tmm_stream_disconnect failed");
                }
                break;
            default:
                break;
            }
        } else {
            /* [tmm_diag] 心跳:仅在通话期间(status!=IDLE)每 ~5s 打一次,证明 manager
             * 工作线程在跑;idle 时静默,避免刷屏。是否卡死以 stream_connect 各阶段
             * 日志为准(before/after ipc_p2p_client_connect);此心跳用于佐证"线程
             * 还活着但没取到消息"。线程真死时,下一次通话会只见 CONNECT queued OK
             * 却无 dequeue cmd=0。 */
            if (g_tmm_manager_handle.status != TUYA_TMM_MANAGER_STA_IDLE &&
                (++loop_cnt % 10) == 0) {
                PR_NOTICE("[tmm_diag] task alive loop=%u status=%d", loop_cnt, g_tmm_manager_handle.status);
            }
        }
    }

    g_tmm_manager_handle.task_run = FALSE;
    if (g_tmm_manager_handle.thrd_handle != NULL) {
        tal_thread_delete(g_tmm_manager_handle.thrd_handle);
        g_tmm_manager_handle.thrd_handle = NULL;
    }

    PR_DEBUG("tmm manager task deleted");
}

/**
 * @brief Apply peer display fields from RTC signaling payload
 * @param[in] pinfo control event info
 * @return none
 */
STATIC VOID __tmm_manager_apply_peer_info(TUYA_TMM_CONTROL_INFO_S *pinfo)
{
    if (pinfo == NULL) {
        return;
    }

    tal_mutex_lock(g_tmm_manager_handle.lock);
    if (pinfo->target_name[0] != '\0') {
        snprintf(g_tmm_manager_handle.peer_name, sizeof(g_tmm_manager_handle.peer_name),
                 "%s", pinfo->target_name);
    }
    if (pinfo->target_id[0] != '\0') {
        snprintf(g_tmm_manager_handle.stream_conf.dev_id,
                 sizeof(g_tmm_manager_handle.stream_conf.dev_id), "%s", pinfo->target_id);
    }
    if (pinfo->target_localkey[0] != '\0') {
        snprintf(g_tmm_manager_handle.stream_conf.local_key,
                 sizeof(g_tmm_manager_handle.stream_conf.local_key), "%s",
                 pinfo->target_localkey);
    }
    tal_mutex_unlock(g_tmm_manager_handle.lock);
}

/**
 * @brief Clear cached peer display name (caller must hold manager lock)
 * @return none
 */
STATIC VOID __tmm_manager_clear_peer_name_locked(VOID)
{
    memset(g_tmm_manager_handle.peer_name, 0, sizeof(g_tmm_manager_handle.peer_name));
}

/**
 * @brief Clear cached peer display name
 * @return none
 */
STATIC VOID __tmm_manager_clear_peer_name(VOID)
{
    tal_mutex_lock(g_tmm_manager_handle.lock);
    __tmm_manager_clear_peer_name_locked();
    tal_mutex_unlock(g_tmm_manager_handle.lock);
}

OPERATE_RET tuya_tmm_control_evt_cb(TUYA_TMM_CONTROL_INFO_S* pinfo, VOID* priv_data)
{
    TUYA_TMM_MANAGER_MSG_S msg = {0};

    switch (pinfo->event) {
    case TUYA_TMM_CONTROL_EVT_RINGING:
        break;
    case TUYA_TMM_CONTROL_EVT_INCOMING:
        tal_mutex_lock(g_tmm_manager_handle.lock);
        memset(&g_tmm_manager_handle.stream_conf, 0, sizeof(TUYA_TMM_STREAM_CONF_S));
        if (pinfo->stream_type == TUYA_TMM_CONTROL_STREAM_TYPE_AUDIO) {
            g_tmm_manager_handle.stream_conf.enable_send_audio = TRUE;
            g_tmm_manager_handle.stream_conf.enable_recv_audio = TRUE;
        } else if (pinfo->stream_type == TUYA_TMM_CONTROL_STREAM_TYPE_VIDEO) {
            g_tmm_manager_handle.stream_conf.enable_send_video = TRUE;
            g_tmm_manager_handle.stream_conf.enable_recv_video = TRUE;
        } else if (pinfo->stream_type == TUYA_TMM_CONTROL_STREAM_TYPE_AV) {
            g_tmm_manager_handle.stream_conf.enable_send_audio = TRUE;
            g_tmm_manager_handle.stream_conf.enable_recv_audio = TRUE;
            g_tmm_manager_handle.stream_conf.enable_send_video = TRUE;
            g_tmm_manager_handle.stream_conf.enable_recv_video = TRUE;
        }

        snprintf(g_tmm_manager_handle.stream_conf.dev_id,
                 sizeof(g_tmm_manager_handle.stream_conf.dev_id), "%s", pinfo->target_id);
        snprintf(g_tmm_manager_handle.stream_conf.local_key,
                 sizeof(g_tmm_manager_handle.stream_conf.local_key), "%s",
                 pinfo->target_localkey);
        snprintf(g_tmm_manager_handle.peer_name, sizeof(g_tmm_manager_handle.peer_name),
                 "%s", pinfo->target_name);

        __tuya_tmm_update_callable_device(g_tmm_manager_handle.stream_conf.dev_id);
        if (g_tmm_manager_handle.stream_conf.local_key[0] != '\0') {
            g_tmm_manager_handle.incoming_type = 0;
        }

        PR_NOTICE("tmm incoming: dev=%s incoming_type=%d has_lk=%d",
                  g_tmm_manager_handle.stream_conf.dev_id,
                  g_tmm_manager_handle.incoming_type,
                  (g_tmm_manager_handle.stream_conf.local_key[0] != '\0') ? 1 : 0);

        g_tmm_manager_handle.status = TUYA_TMM_MANAGER_STA_INCOMING;
        g_tmm_manager_handle.send_audio_enable = FALSE;
        tal_mutex_unlock(g_tmm_manager_handle.lock);
        tuya_tmm_manager_evt_cb(TUYA_TMM_MANAGER_EVT_INCOMING, NULL, 0);
        break;

    case TUYA_TMM_CONTROL_EVT_ACCEPTED:
        __tmm_manager_apply_peer_info(pinfo);
        tuya_tmm_manager_evt_cb(TUYA_TMM_MANAGER_EVT_ACCEPTED, NULL, 0);
        tal_mutex_lock(g_tmm_manager_handle.lock);
        /* Outgoing call already set stream_conf.dev_id in tuya_tmm_manager_call(). */
        if (g_tmm_manager_handle.status != TUYA_TMM_MANAGER_STA_CALLING &&
            pinfo->target_id[0] != '\0') {
            snprintf(g_tmm_manager_handle.stream_conf.dev_id,
                     sizeof(g_tmm_manager_handle.stream_conf.dev_id), "%s", pinfo->target_id);
        }
        if (pinfo->target_localkey[0] != '\0') {
            snprintf(g_tmm_manager_handle.stream_conf.local_key,
                     sizeof(g_tmm_manager_handle.stream_conf.local_key), "%s",
                     pinfo->target_localkey);
        }
        /*
         * Caller does not invoke p2p_client_connect; callee sends offer after answer.
         * Downlink speaker is started by P2P cmd_send on peer op[52].
         */
        g_tmm_manager_handle.status = TUYA_TMM_MANAGER_STA_P2P_CONNECTED;
        tal_mutex_unlock(g_tmm_manager_handle.lock);
        break;

    case TUYA_TMM_CONTROL_EVT_ACCEPTED_BY_OTHERS:
        tuya_tmm_manager_evt_cb(TUYA_TMM_MANAGER_EVT_HANGUP, NULL, 0);
        tal_mutex_lock(g_tmm_manager_handle.lock);
        g_tmm_manager_handle.status = TUYA_TMM_MANAGER_STA_IDLE;
        __tmm_manager_clear_peer_name_locked();
        tal_mutex_unlock(g_tmm_manager_handle.lock);
        break;

    case TUYA_TMM_CONTROL_EVT_UNANSWERED:
        tuya_tmm_manager_evt_cb(TUYA_TMM_MANAGER_EVT_UNANSWERED, NULL, 0);
        tal_mutex_lock(g_tmm_manager_handle.lock);
        g_tmm_manager_handle.status = TUYA_TMM_MANAGER_STA_IDLE;
        __tmm_manager_clear_peer_name_locked();
        tal_mutex_unlock(g_tmm_manager_handle.lock);
        break;

    case TUYA_TMM_CONTROL_EVT_REJECT:
        tuya_tmm_manager_evt_cb(TUYA_TMM_MANAGER_EVT_REJECT, NULL, 0);
        tal_mutex_lock(g_tmm_manager_handle.lock);
        g_tmm_manager_handle.status = TUYA_TMM_MANAGER_STA_IDLE;
        __tmm_manager_clear_peer_name_locked();
        tal_mutex_unlock(g_tmm_manager_handle.lock);
        break;

    case TUYA_TMM_CONTROL_EVT_HANGUP:
        tuya_tmm_manager_evt_cb(TUYA_TMM_MANAGER_EVT_HANGUP, NULL, 0);
        tal_mutex_lock(g_tmm_manager_handle.lock);
        if (g_tmm_manager_handle.status >= TUYA_TMM_MANAGER_STA_P2P_CONNECTING) {
            msg.cmd = TUYA_TMM_MGR_MSG_CMD_DISCONNECT;
            PR_DEBUG("cmd request, cmd = %d\n",  msg.cmd);
            tuya_tmm_mgr_queue_post(g_tmm_manager_handle.queue_handle, (VOID * )&msg, 20);
        }
        g_tmm_manager_handle.status = TUYA_TMM_MANAGER_STA_IDLE;
        __tmm_manager_clear_peer_name_locked();
        tal_mutex_unlock(g_tmm_manager_handle.lock);
        break;

    case TUYA_TMM_CONTROL_EVT_BUSY:
        tuya_tmm_manager_evt_cb(TUYA_TMM_MANAGER_EVT_BUSY, NULL, 0);
        tal_mutex_lock(g_tmm_manager_handle.lock);
        g_tmm_manager_handle.status = TUYA_TMM_MANAGER_STA_IDLE;
        tal_mutex_unlock(g_tmm_manager_handle.lock);
        break;
    case TUYA_TMM_CONTROL_EVT_CANCEL:
        tuya_tmm_manager_evt_cb(TUYA_TMM_MANAGER_EVT_CANCEL, NULL, 0);
        tal_mutex_lock(g_tmm_manager_handle.lock);
        if (g_tmm_manager_handle.status >= TUYA_TMM_MANAGER_STA_P2P_CONNECTING) {
            msg.cmd = TUYA_TMM_MGR_MSG_CMD_DISCONNECT;
            PR_DEBUG("cmd request, cmd = %d\n",  msg.cmd);
            tuya_tmm_mgr_queue_post(g_tmm_manager_handle.queue_handle, (VOID * )&msg, 20);
        }
        g_tmm_manager_handle.status = TUYA_TMM_MANAGER_STA_IDLE;
        tal_mutex_unlock(g_tmm_manager_handle.lock);
        break;
    case TUYA_TMM_CONTROL_EVT_STOP:
        tuya_tmm_manager_evt_cb(TUYA_TMM_MANAGER_EVT_STOP, NULL, 0);
        tal_mutex_lock(g_tmm_manager_handle.lock);
        if (g_tmm_manager_handle.status >= TUYA_TMM_MANAGER_STA_P2P_CONNECTING) {
            msg.cmd = TUYA_TMM_MGR_MSG_CMD_DISCONNECT;
            PR_DEBUG("cmd request, cmd = %d\n",  msg.cmd);
            tuya_tmm_mgr_queue_post(g_tmm_manager_handle.queue_handle, (VOID * )&msg, 20);
        }
        g_tmm_manager_handle.status = TUYA_TMM_MANAGER_STA_IDLE;
        tal_mutex_unlock(g_tmm_manager_handle.lock);
        break;
    case TUYA_TMM_CONTROL_EVT_ERROR:
        tuya_tmm_manager_evt_cb(TUYA_TMM_MANAGER_EVT_ERROR, NULL, 0);
        tal_mutex_lock(g_tmm_manager_handle.lock);
        if (g_tmm_manager_handle.status >= TUYA_TMM_MANAGER_STA_P2P_CONNECTING) {
            msg.cmd = TUYA_TMM_MGR_MSG_CMD_DISCONNECT;
            PR_DEBUG("cmd request, cmd = %d\n",  msg.cmd);
            tuya_tmm_mgr_queue_post(g_tmm_manager_handle.queue_handle, (VOID * )&msg, 20);
        }
        g_tmm_manager_handle.status = TUYA_TMM_MANAGER_STA_IDLE;
        tal_mutex_unlock(g_tmm_manager_handle.lock);
        break;
    default:
        break;
    }

    return OPRT_OK;
}

OPERATE_RET tuya_tmm_manager_init(TUYA_TMM_MANAGER_INIT_S *init_param)
{
    if (g_tmm_manager_handle.init) {
        return OPRT_OK;
    }

    if (init_param == NULL) {
        PR_ERR("the param is null.");
        return OPRT_INVALID_PARM;
    }

    init_param->stream_init_param.event_cb = __tuya_tmm_stream_event_cb;

    if (tuya_tmm_control_init(tuya_tmm_control_evt_cb, NULL, TUYA_TMM_MANAGER_DEFAULT_CALL_TIMEOUT) != OPRT_OK) {
        PR_ERR("tuya_tmm_control_init FAILED.");
        return OPRT_COM_ERROR;
    }

    if (tuya_tmm_stream_init(&init_param->stream_init_param) != OPRT_OK) {
        PR_ERR("tuya_tmm_stream_init FAILED.");
        return OPRT_COM_ERROR;
    }

    if (tal_mutex_create_init(&g_tmm_manager_handle.lock) != OPRT_OK) {
        PR_ERR("tal_mutex_create_init failed\n");
        return OPRT_COM_ERROR;
    }

    if (tuya_tmm_mgr_queue_create(&g_tmm_manager_handle.queue_handle, sizeof(TUYA_TMM_MANAGER_MSG_S), 20) != OPRT_OK) {
        PR_ERR("tuya_tmm_mgr_queue_create FAILED.");
        return OPRT_COM_ERROR;
    }

    if (tal_mutex_create_init(&g_tmm_manager_handle.audio_jitter_lock) != OPRT_OK) {
        PR_ERR("audio_jitter_lock create failed");
        return OPRT_COM_ERROR;
    }
    if (tuya_ring_buff_create(TMM_AUDIO_JITTER_BUF_SIZE, OVERFLOW_PSRAM_STOP_TYPE,
                              &g_tmm_manager_handle.audio_jitter_buf) != OPRT_OK) {
        PR_ERR("audio jitter buf create failed");
        return OPRT_COM_ERROR;
    }

    THREAD_CFG_T thrd_param = {1024 * 10, THREAD_PRIO_1, "tuya_tmm_manager"};
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    thrd_param.psram_mode = 1;
#endif
    if (tal_thread_create_and_start(&g_tmm_manager_handle.thrd_handle, NULL, NULL,
                                    __tuya_tmm_manager_task, &g_tmm_manager_handle, &thrd_param) != OPRT_OK) {
        return OPRT_COM_ERROR;
    }

    THREAD_CFG_T audio_thrd_param = {1024 * 4, THREAD_PRIO_1, "tmm_audio_send"};
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    audio_thrd_param.psram_mode = 1;
#endif
    if (tal_thread_create_and_start(&g_tmm_manager_handle.audio_send_thrd, NULL, NULL,
                                    __tmm_audio_send_task, NULL, &audio_thrd_param) != OPRT_OK) {
        return OPRT_COM_ERROR;
    }

    g_tmm_manager_handle.event_cb = init_param->event_cb;

    g_tmm_manager_handle.init = TRUE;

    return OPRT_OK;
}

OPERATE_RET tuya_tmm_manager_deinit(VOID)
{
    /* 1. 通知两个线程停止 */
    g_tmm_manager_handle.audio_send_run = FALSE;
    g_tmm_manager_handle.task_run = FALSE;

    /* 2. 等待音频发送线程退出。
     *    tal_thread_delete 阻塞至线程函数返回;在此之前释放 jitter buffer/lock
     *    会与发送线程 800-804 行的 mutex_lock/ring_buff_read 竞态 → use-after-free */
    if (g_tmm_manager_handle.audio_send_thrd != NULL) {
        tal_thread_delete(g_tmm_manager_handle.audio_send_thrd);
        g_tmm_manager_handle.audio_send_thrd = NULL;
    }

    /* 3. manager task 线程在见到 task_run=FALSE 后自删除(self-delete),
     *    会在退出前将 thrd_handle 置 NULL;短暂等待其完成 */
    if (g_tmm_manager_handle.thrd_handle != NULL) {
        /* 队列 fetch 超时最长 500ms,给足够时间让 task 退出循环并自删除 */
        tal_system_sleep(600);
    }

    /* 4. 确认两线程均已退出后,安全释放共享资源 */
    if (g_tmm_manager_handle.audio_jitter_buf != NULL) {
        tuya_ring_buff_free(g_tmm_manager_handle.audio_jitter_buf);
        g_tmm_manager_handle.audio_jitter_buf = NULL;
    }
    if (g_tmm_manager_handle.audio_jitter_lock != NULL) {
        tal_mutex_release(g_tmm_manager_handle.audio_jitter_lock);
        g_tmm_manager_handle.audio_jitter_lock = NULL;
    }

    g_tmm_manager_handle.init = FALSE;
    return OPRT_OK;
}

OPERATE_RET tuya_tmm_manager_call(CHAR_T *target_id, TUYA_TMM_MANAGER_CALL_TYPE_E call_type)
{
    tal_mutex_lock(g_tmm_manager_handle.lock);
    memset(&g_tmm_manager_handle.stream_conf, 0, sizeof(TUYA_TMM_STREAM_CONF_S));
    __tmm_manager_clear_peer_name_locked();
    g_tmm_manager_handle.stream_conf.enable_send_audio = TRUE;
    g_tmm_manager_handle.stream_conf.enable_recv_audio = TRUE;
    if (call_type == TUYA_TMM_MANAGER_CALL_TYPE_VIDEO) {
        g_tmm_manager_handle.stream_conf.enable_send_video = TRUE;
        g_tmm_manager_handle.stream_conf.enable_recv_video = TRUE;
    }
    snprintf(g_tmm_manager_handle.stream_conf.dev_id, sizeof(g_tmm_manager_handle.stream_conf.dev_id), "%s", target_id);

    if (tuya_tmm_control_call(target_id, "dgnzk", "dgnzk", (TUYA_TMM_CONTROL_STREAM_TYPE_E)call_type) != OPRT_OK) {
        PR_ERR("tuya_tmm_control_answer FAILED.");
        g_tmm_manager_handle.status = TUYA_TMM_MANAGER_STA_IDLE;
        tal_mutex_unlock(g_tmm_manager_handle.lock);
        return OPRT_COM_ERROR;
    }

    g_tmm_manager_handle.status = TUYA_TMM_MANAGER_STA_CALLING;
    g_tmm_manager_handle.send_audio_enable = FALSE;

    tal_mutex_unlock(g_tmm_manager_handle.lock);

    return OPRT_OK;
}

OPERATE_RET tuya_tmm_manager_call_type_change(TUYA_TMM_MANAGER_CALL_TYPE_E call_type)
{
    tal_mutex_lock(g_tmm_manager_handle.lock);

    TUYA_TMM_MANAGER_MSG_S msg = {0};
    msg.ext = call_type;
    msg.cmd = TUYA_TMM_MGR_MSG_CMD_TYPE_CHANGE;
    PR_DEBUG("cmd request, cmd = %d\n",  msg.cmd);
    if (tuya_tmm_mgr_queue_post(g_tmm_manager_handle.queue_handle, (VOID * )&msg, 20) != OPRT_OK) {
        PR_ERR("tuya_tmm_control_answer FAILED.");
        tal_mutex_unlock(g_tmm_manager_handle.lock);
        return OPRT_COM_ERROR;
    }

    tal_mutex_unlock(g_tmm_manager_handle.lock);

    return OPRT_OK;
}

OPERATE_RET tuya_tmm_manager_call_mic_change(BOOL_T stat)
{
    tal_mutex_lock(g_tmm_manager_handle.lock);

    TUYA_TMM_MANAGER_MSG_S msg = {0};
    msg.ext = stat;
    msg.cmd = TUYA_TMM_MGR_MSG_CMD_MIC_CHANGE;
    PR_DEBUG("cmd request, cmd = %d\n",  msg.cmd);
    if (tuya_tmm_mgr_queue_post(g_tmm_manager_handle.queue_handle, (VOID * )&msg, 20) != OPRT_OK) {
        PR_ERR("tuya_tmm_control_answer FAILED.");
        tal_mutex_unlock(g_tmm_manager_handle.lock);
        return OPRT_COM_ERROR;
    }

    tal_mutex_unlock(g_tmm_manager_handle.lock);

    return OPRT_OK;
}

OPERATE_RET tuya_tmm_manager_answer(VOID)
{
    tal_mutex_lock(g_tmm_manager_handle.lock);
    if (tuya_tmm_control_answer() != OPRT_OK) {
        PR_ERR("tuya_tmm_control_answer FAILED.");
        g_tmm_manager_handle.status = TUYA_TMM_MANAGER_STA_IDLE;
        tal_mutex_unlock(g_tmm_manager_handle.lock);
        return OPRT_COM_ERROR;
    }

    if (g_tmm_manager_handle.incoming_type == 0 &&
        __tmm_need_p2p_connect(g_tmm_manager_handle.stream_conf.dev_id,
                               g_tmm_manager_handle.stream_conf.local_key)) {
        TUYA_TMM_MANAGER_MSG_S msg = {0};
        msg.cmd = TUYA_TMM_MGR_MSG_CMD_CONNECT;
        PR_NOTICE("tmm answer: dev call post connect dev=%s lk_len=%u",
                  g_tmm_manager_handle.stream_conf.dev_id,
                  (UINT_T)strlen(g_tmm_manager_handle.stream_conf.local_key));
        if (tuya_tmm_mgr_queue_post(g_tmm_manager_handle.queue_handle, (VOID * )&msg, 20) != OPRT_OK) {
            PR_ERR("[tmm_diag] CONNECT queue_post FAILED");
            g_tmm_manager_handle.status = TUYA_TMM_MANAGER_STA_IDLE;
            tal_mutex_unlock(g_tmm_manager_handle.lock);
            return OPRT_COM_ERROR;
        }
        PR_NOTICE("[tmm_diag] CONNECT queued OK (cmd=0), expect manager task to run stream_connect");
        g_tmm_manager_handle.status = TUYA_TMM_MANAGER_STA_P2P_CONNECTING;
    } else {
        PR_NOTICE("tmm answer: app/cloud call skip p2p connect dev=%s incoming_type=%d",
                  g_tmm_manager_handle.stream_conf.dev_id,
                  g_tmm_manager_handle.incoming_type);
        g_tmm_manager_handle.status = TUYA_TMM_MANAGER_STA_P2P_CONNECTED;
    }
    tal_mutex_unlock(g_tmm_manager_handle.lock);

    return OPRT_OK;
}

OPERATE_RET tuya_tmm_manager_hangup(VOID)
{
    OPERATE_RET            ret = OPRT_OK;
    TUYA_TMM_MANAGER_MSG_S msg = {0};

    tal_mutex_lock(g_tmm_manager_handle.lock);
    switch (g_tmm_manager_handle.status) {
    case TUYA_TMM_MANAGER_STA_INCOMING:
        ret = tuya_tmm_control_reject();
        break;

    case TUYA_TMM_MANAGER_STA_CALLING:
        ret = tuya_tmm_control_cancel();
        break;

    case TUYA_TMM_MANAGER_STA_P2P_CONNECTING:
    case TUYA_TMM_MANAGER_STA_P2P_CONNECTED:
        ret = tuya_tmm_control_hangup();
        msg.cmd = TUYA_TMM_MGR_MSG_CMD_DISCONNECT;
        PR_DEBUG("cmd request, cmd = %d\n",  msg.cmd);
        ret = tuya_tmm_mgr_queue_post(g_tmm_manager_handle.queue_handle, (VOID * )&msg, 20);
        break;

    default:
        break;
    }

    g_tmm_manager_handle.status = TUYA_TMM_MANAGER_STA_IDLE;

    tal_mutex_unlock(g_tmm_manager_handle.lock);

    return ret;
}

STATIC VOID __tmm_audio_send_task(VOID *pv)
{
    (VOID)pv;
    UINT8_T frame[TMM_AUDIO_FRAME_BYTES];
    TUYA_TMM_STREAM_AUDIO_DATA_S data = {0};

    data.p_data = frame;
    data.data_len = TMM_AUDIO_FRAME_BYTES;
    data.user_data.audio_channel = 1;
    data.user_data.audio_sample = 8000;
    data.user_data.audio_databits = 16;
    snprintf(data.user_data.audio_codec, sizeof(data.user_data.audio_codec), "pcm");

    g_tmm_manager_handle.audio_send_run = TRUE;
    PR_DEBUG("tmm audio send task start");

    /* 绝对节拍：以"下次应发送时刻"为锚点累加，而非每轮 sleep 固定值。
     * 相对 sleep 会把每轮的调度误差/处理耗时累加进来，长通话会漂移
     * (漂慢→jitter 涨至溢出丢帧/对端欠载断续；漂快→KCP 堆积 Check_Buffer)。
     * 绝对锚点下误差不累加，且对端按 PTS(=发送墙钟)回放，均匀锚点即均匀节奏。 */
    SYS_TIME_T next_deadline = tal_system_get_millisecond();

    /* 异常检测：仅告警不刷屏。每段欠载只告警一次 + 恢复时再告警一次。
     * dry_streak    = 通话中连续取空帧数(每帧 40ms)，达阈值判"长时间没数据"
     * stall_logged  = 本次欠载是否已告警(去重)
     * stall_start   = 本次取空起点(恢复时报告持续时长)
     * feed_gap      = 距上游最近一次喂入的间隔(判 feed 是否及时)；==0 表示上游尚未喂过
     * 说明：上游首次喂入前(audio_last_feed_ms==0)为通话起始正常等待，不判异常。 */
    UINT32_T dry_streak = 0;
    BOOL_T stall_logged = FALSE;
    SYS_TIME_T stall_start = 0;
    SYS_TIME_T last_heartbeat = 0;

    while (g_tmm_manager_handle.audio_send_run) {
        next_deadline += TMM_AUDIO_SEND_PERIOD_MS;

        /* 与 data_feed 一致:上行发送门控用 send_audio_enable(通道已起)+ enable_send_audio(未静音),
         * 不依赖 TMM 呼叫 status(预览式对讲恒为 IDLE,若沿用 status 会永不发送且不报欠载)。 */
        if (g_tmm_manager_handle.send_audio_enable &&
            g_tmm_manager_handle.stream_conf.enable_send_audio) {

            SYS_TIME_T cur = tal_system_get_millisecond();
            tal_mutex_lock(g_tmm_manager_handle.audio_jitter_lock);
            UINT32_T n = tuya_ring_buff_read(g_tmm_manager_handle.audio_jitter_buf,
                                             frame, TMM_AUDIO_FRAME_BYTES);
            SYS_TIME_T last_feed = g_tmm_manager_handle.audio_last_feed_ms;
            tal_mutex_unlock(g_tmm_manager_handle.audio_jitter_lock);

            SYS_TIME_T feed_gap = (last_feed != 0) ? (cur - last_feed) : 0;

            if (n > 0) {
                data.data_len = n;
                /* pts 用 P2P SDK 单调时钟:必须与视频侧(__voip_h264_cb)同源,
                 * 否则 APP 端音视频时间轴错位无法同步(对齐原生 tuya_p2p_app.c
                 * 音视频同用此钟的做法);posix 墙钟还受 NTP 校时跳变影响。 */
                data.user_data.timestamp = tuya_p2p_misc_get_current_time_ms();
                /* 非阻塞：下游 ring 满则返回 error，丢这一帧继续，不卡发送节拍 */
                tuya_tmm_stream_audio_send(&data);

                if (stall_logged) {
                    PR_WARN("tmm audio recover: underrun lasted %dms, feed_gap=%dms",
                            (UINT32_T)(cur - stall_start), (UINT32_T)feed_gap);
                    stall_logged = FALSE;
                }
                dry_streak = 0;
            } else {
                if (dry_streak == 0) {
                    stall_start = cur;
                }
                dry_streak++;
                /* 仅在上游已喂过数据后才判异常(排除通话起始正常等待) */
                if (last_feed != 0 && !stall_logged &&
                    dry_streak >= TMM_AUDIO_UNDERRUN_FRAMES) {
                    stall_logged = TRUE;
                    last_heartbeat = cur;
                    PR_WARN("tmm audio underrun: jitter empty %dms, feed_gap=%dms (upstream feed too slow/stopped?)",
                            dry_streak * TMM_AUDIO_SEND_PERIOD_MS, (UINT32_T)feed_gap);
                } else if (stall_logged && (cur - last_heartbeat) >= 2000) {
                    /* 长时间欠载：每 2s 心跳一次，表明仍在异常中(避免误判已恢复) */
                    last_heartbeat = cur;
                    PR_WARN("tmm audio still underrun: %dms, feed_gap=%dms",
                            dry_streak * TMM_AUDIO_SEND_PERIOD_MS, (UINT32_T)feed_gap);
                }
            }
        } else {
            dry_streak = 0;
            stall_logged = FALSE;
        }

        SYS_TIME_T now = tal_system_get_millisecond();
        if (next_deadline > now) {
            tal_system_sleep((UINT32_T)(next_deadline - now));
        } else if (now - next_deadline > TMM_AUDIO_SEND_PERIOD_MS) {
            /* 落后超过一帧(被抢占/卡顿)：丢弃累积欠债重新对齐到当前时刻，
             * 避免连续无 sleep 猛灌导致 KCP 堆积；不足一帧则本轮不睡自然追上 */
            next_deadline = now;
        }
    }

    PR_DEBUG("tmm audio send task exit");
}

OPERATE_RET tuya_tmm_manager_data_feed(CHAR_T *buffer, INT_T len)
{
    /* 上行门控不依赖 TMM 控制呼叫的 status:IPC 预览式视频对讲(App 直接拉流)不走呼叫流程,
     * status 恒为 IDLE。send_audio_enable(AUDIO_START 置位、AUDIO_STOP 清零)才是"P2P 音频通道
     * 已起、对端在拉"的权威信号;enable_send_audio 用于麦克风静音控制。二者与会话建立方式无关。 */
    if (g_tmm_manager_handle.send_audio_enable != TRUE) {
        return OPRT_OK;
    }

    if (g_tmm_manager_handle.stream_conf.enable_send_audio != TRUE) {
        return OPRT_OK;
    }

    if (buffer == NULL || len <= 0) {
        return OPRT_INVALID_PARM;
    }

    /* 缓存进 jitter buffer，由 40ms 发送线程匀速取送，吸收上游 80ms 大包与发送侧 40ms 节拍错配 */
    tal_mutex_lock(g_tmm_manager_handle.audio_jitter_lock);
    tuya_ring_buff_write(g_tmm_manager_handle.audio_jitter_buf, buffer, len);
    g_tmm_manager_handle.audio_last_feed_ms = tal_system_get_millisecond();
    tal_mutex_unlock(g_tmm_manager_handle.audio_jitter_lock);

    return OPRT_OK;
}

OPERATE_RET tuya_tmm_manager_get_dev_rtc_list(TUYA_TMM_MANAGER_DEV_RTC_CB cb, VOID *usrdata)
{
    OPERATE_RET  rt = OPRT_OK;
    ty_cJSON    *result = NULL;
    CHAR_T       info[128] = {0};

    /* Legacy RTC callable API, kept for compatibility. Contacts UI now uses
     * tuya_tmm_manager_atop_contact_sync_list() per contact service spec. */
    snprintf(info, sizeof(info), "{\"devId\":\"%s\"}", get_gw_cntl()->gw_if.id);
    rt = iot_httpc_common_post_simple(TUYA_TMM_MANAGER_DEV_RTC_CALLABLE_GET, "1.0", info, NULL, &result);
    if (rt != OPRT_OK) {
        PR_ERR("get device info failed");
        return OPRT_COM_ERROR;
    }

    if (result) {
        INT_T dev_num = ty_cJSON_GetArraySize(result);
        ty_cJSON *id = NULL, *type = NULL, *name = NULL, *item = NULL;
        for (INT_T index = 0; index < dev_num; index++) {
            if ((item = ty_cJSON_GetArrayItem(result, index)) == NULL) {
                continue;
            }

            id = ty_cJSON_GetObjectItem(item, "resourceId");
            name = ty_cJSON_GetObjectItem(item, "resourceName");
            type = ty_cJSON_GetObjectItem(item, "resourceType");

            if (id == NULL || name == NULL || type == NULL) {
                PR_ERR("the param is error");
                continue;
            }

            PR_DEBUG("rtc devices: id:%s, name:%s, type:%d", id->valuestring, name->valuestring, type->valueint);
            if (cb != NULL) {
                CHAR_T type_str[8] = {0};
                snprintf(type_str, sizeof(type_str), "%d", type->valueint);
                cb(id->valuestring, name->valuestring, type_str, usrdata);
            }
        }

        ty_cJSON_Delete(result);
    }

    return OPRT_OK;
}

INT_T tuya_tmm_manager_get_incoming_type(VOID)
{
    return g_tmm_manager_handle.incoming_type;
}

OPERATE_RET tuya_tmm_manager_get_peer_display(CHAR_T *name, UINT32_T name_size,
                                              CHAR_T *dev_id, UINT32_T dev_id_size)
{
    if (name == NULL || name_size == 0 || dev_id == NULL || dev_id_size == 0) {
        return OPRT_INVALID_PARM;
    }

    name[0] = '\0';
    dev_id[0] = '\0';

    tal_mutex_lock(g_tmm_manager_handle.lock);
    if (g_tmm_manager_handle.peer_name[0] != '\0') {
        snprintf(name, name_size, "%s", g_tmm_manager_handle.peer_name);
    }
    if (g_tmm_manager_handle.stream_conf.dev_id[0] != '\0') {
        snprintf(dev_id, dev_id_size, "%s", g_tmm_manager_handle.stream_conf.dev_id);
    }
    tal_mutex_unlock(g_tmm_manager_handle.lock);

    return OPRT_OK;
}
