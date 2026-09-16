/**
 * @file tuya_tmm_stream_link.c
 * @author baijue.huang@tuya.com
 * @brief tuya tmm stream link
 * @version 0.1
 * @date 2022-9-01
 * 
 * @copyright Copyright (c) 2021
 * 
 */

/***********************************************************************
 ** INCLUDE                                                           **
 **********************************************************************/
#include <string.h>

#include "uni_log.h"
#include "tal_time_service.h"
#include "tuya_ipc_p2p.h"
#include "tuya_tmm_link.h"
#include "gw_intf.h"

#include "tuya_tmm_stream_link.h"

/***********************************************************************
 ** CONSTANT ( MACRO AND ENUM )                                       **
 **********************************************************************/

#define TMM_STREAM_MAX_SESSION_NUM              10

/***********************************************************************
 ** STRUCT                                                            **
 **********************************************************************/

typedef struct
{
    TMM_TRANS_TYPE_E  type;
    INT_T             conn;                      //handle
    UINT_T            media_ctl_flag;
    CHAR_T            dev_id[DEV_ID_LEN + 1];
}TMM_STREAM_HDL_T;

typedef OPERATE_RET (*OPS_FUNC)(UINT_T stream_type, TMM_STREAM_HDL_T* sess);

typedef struct {
    UINT_T           media_ctl_bit;
    OPS_FUNC         start;
    OPS_FUNC         stop;
} TMM_STREAM_MEDIA_OPS_T;

/***********************************************************************
 ** VARIABLE                                                          **
 **********************************************************************/

STATIC TMM_STREAM_HDL_T     sessions[TMM_STREAM_MAX_SESSION_NUM] = {{0}};

/***********************************************************************
 ** FUNCTON                                                           **
 **********************************************************************/


STATIC TMM_STREAM_HDL_T* __tmm_stream_get_session(CHAR_T* dev_id)
{
    for(INT_T i = 0; i < TMM_STREAM_MAX_SESSION_NUM; i++) {
        TMM_STREAM_HDL_T* sess = &sessions[i];
        if (0 == strcmp(dev_id, sess->dev_id)) {
            return sess;
        }
    }

    return NULL;
}

STATIC TMM_STREAM_HDL_T* __tmm_stream_find_free_session()
{
    for(INT_T i = 0; i < TMM_STREAM_MAX_SESSION_NUM; i++) {
        TMM_STREAM_HDL_T* sess = &sessions[i];
        if (sess->dev_id[0] == '\0') {
            return sess;
        }
    }

    return NULL;
}

STATIC VOID __tmm_stream_free_session(TMM_STREAM_HDL_T* sess)
{
    memset(sess, 0, sizeof(TMM_STREAM_HDL_T));
}

static OPERATE_RET __tmm_stream_start_video_send(UINT_T stream_type, TMM_STREAM_HDL_T* sess)
{
    return tuya_ipc_p2p_client_video_send_start(sess->conn);
}

static OPERATE_RET __tmm_stream_stop_video_send(UINT_T stream_type, TMM_STREAM_HDL_T* sess)
{
    return tuya_ipc_p2p_client_video_send_stop(sess->conn);
}

static OPERATE_RET __tmm_stream_start_video_recv(UINT_T stream_type, TMM_STREAM_HDL_T* sess)
{
    tuya_ipc_p2p_client_set_video_clarity_standard(sess->conn);
    return tuya_ipc_p2p_client_start_prev(sess->conn);
}

static OPERATE_RET __tmm_stream_stop_video_recv(UINT_T stream_type, TMM_STREAM_HDL_T* sess)
{
    return tuya_ipc_p2p_client_stop_prev(sess->conn);
}

static OPERATE_RET __tmm_stream_start_audio_send(UINT_T stream_type, TMM_STREAM_HDL_T* sess)
{
    return tuya_ipc_p2p_client_audio_send_start(sess->conn);
}

static OPERATE_RET __tmm_stream_stop_audio_send(UINT_T stream_type, TMM_STREAM_HDL_T* sess)
{
    return tuya_ipc_p2p_client_audio_send_stop(sess->conn);
}

static OPERATE_RET __tmm_stream_start_audio_recv(UINT_T stream_type, TMM_STREAM_HDL_T* sess)
{
    return tuya_ipc_p2p_client_start_audio(sess->conn);
}

static OPERATE_RET __tmm_stream_stop_audio_recv(UINT_T stream_type, TMM_STREAM_HDL_T* sess)
{
    return tuya_ipc_p2p_client_stop_audio(sess->conn);
}

OPERATE_RET tuya_tmm_stream_link_connect(TUYA_TMM_STREAM_CONF_S *conf)
{
    INT_T conn = -1;

    if (conf == NULL) {
        return OPRT_INVALID_PARM;
    }

    PR_DEBUG("begin connect dev %s lk %s", conf->dev_id, conf->local_key);

    if (conf->dev_id[0] == '\0' || conf->local_key[0] == '\0') {
        PR_ERR("invalid p2p connect param");
        return OPRT_INVALID_PARM;
    }

    if (strncmp(conf->dev_id, "key_", 4) == 0) {
        PR_ERR("refuse p2p connect to app target %s", conf->dev_id);
        return OPRT_INVALID_PARM;
    }

    if (strcmp(conf->dev_id, get_gw_cntl()->gw_if.id) == 0) {
        PR_ERR("refuse p2p connect to self %s", conf->dev_id);
        return OPRT_INVALID_PARM;
    }

    UINT_T t0 = (UINT_T)tal_time_get_posix_ms();
    PR_NOTICE("[tmm_diag] before tuya_ipc_p2p_client_connect dev %s", conf->dev_id);
    OPERATE_RET ret = tuya_ipc_p2p_client_connect(&conn, conf->dev_id, conf->local_key);
    PR_NOTICE("[tmm_diag] after  tuya_ipc_p2p_client_connect rt=%d conn=%d %ums",
              ret, conn, (UINT_T)(tal_time_get_posix_ms() - t0));
    if(ret != OPRT_OK) {
        return ret;
    }

    TMM_STREAM_HDL_T *hdl = __tmm_stream_get_session(conf->dev_id);
    if (hdl != NULL) {
        tuya_ipc_p2p_client_disconnect(hdl->conn);
    }
    else {
        hdl = __tmm_stream_find_free_session();
        if (NULL == hdl)
        {
            PR_ERR("no free session!!!");
            tuya_ipc_p2p_client_disconnect(conn);
            return OPRT_RESOURCE_NOT_READY;
        }
    }

    memset(hdl, 0, sizeof(TMM_STREAM_HDL_T));
    hdl->type = TMM_TRANS_P2P;
    hdl->conn = conn;
    strcpy(hdl->dev_id, conf->dev_id);

    return OPRT_OK;
}

OPERATE_RET tuya_tmm_stream_link_disconnect(TUYA_TMM_STREAM_CONF_S *conf)
{
    if(conf == NULL)
    {
        return OPRT_INVALID_PARM;
    }
    
    PR_DEBUG("disconnect with: %s", conf->dev_id);

    TMM_STREAM_HDL_T *sess = __tmm_stream_get_session(conf->dev_id);
    if(sess != NULL)
    {
        tuya_ipc_p2p_client_disconnect(sess->conn);
        __tmm_stream_free_session(sess);
    }
    else{
        PR_ERR("session(%s) not found", conf->dev_id);
    }

    return OPRT_OK;
}

OPERATE_RET tuya_tmm_stream_link_control(TUYA_TMM_STREAM_CONF_S *conf, INT_T timeout_ms)
{
    OPERATE_RET op_ret = OPRT_OK;
    UINT_T      flag = 0;

    if(conf == NULL)
    {
        PR_ERR("devid is null");
        return OPRT_INVALID_PARM;
    }

    TMM_STREAM_HDL_T* sess = __tmm_stream_get_session(conf->dev_id);
    if(sess == NULL)
    {
        PR_ERR("no connect info.");
        return OPRT_INVALID_PARM;
    }

    TMM_STREAM_MEDIA_OPS_T ops[] =
    {
        {TMM_RECV_VIDEO, __tmm_stream_start_video_recv, __tmm_stream_stop_video_recv},
        {TMM_RECV_AUDIO, __tmm_stream_start_audio_recv, __tmm_stream_stop_audio_recv},
        {TMM_SEND_VIDEO, __tmm_stream_start_video_send, __tmm_stream_stop_video_send},
        {TMM_SEND_AUDIO, __tmm_stream_start_audio_send, __tmm_stream_stop_audio_send},
    };

    flag |= conf->enable_send_audio? TMM_SEND_AUDIO: 0;
    flag |= conf->enable_send_video? TMM_SEND_VIDEO: 0;
    flag |= conf->enable_recv_audio? TMM_RECV_AUDIO: 0;
    flag |= conf->enable_recv_video? TMM_RECV_VIDEO: 0;

    UINT_T diff = flag ^ sess->media_ctl_flag;
    PR_DEBUG("devid: %s, old_flag: %u, flag: %u, diff: %u", conf->dev_id, sess->media_ctl_flag, flag, diff);

    SYS_TICK_T start_ms = tal_time_get_posix_ms();

    for(INT_T i = 0; i < sizeof(ops) / sizeof(ops[0]); i++)
    {
        if (diff & ops[i].media_ctl_bit)
        {
            if (flag & ops[i].media_ctl_bit)
            {
                PR_DEBUG("start %d", i);
                if (OPRT_OK == ops[i].start(flag, sess))
                {
                    sess->media_ctl_flag |= ops[i].media_ctl_bit;
                }
                else
                {
                    op_ret = OPRT_COM_ERROR;
                }
            }
            else
            {
                PR_DEBUG("stop %d", i);
                if (OPRT_OK == ops[i].stop(flag, sess))
                {
                    sess->media_ctl_flag &= (~ops[i].media_ctl_bit);
                }
                else
                {
                    op_ret = OPRT_COM_ERROR;
                }
            }
        }

        SYS_TICK_T now_ms = tal_time_get_posix_ms();
        if (timeout_ms >= 0 && now_ms - start_ms > timeout_ms)
        {
            op_ret = OPRT_TIMEOUT;
            break;
        }
    }

    return op_ret;
}

