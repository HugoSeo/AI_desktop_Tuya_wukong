/**
 * @file tuya_tmm_manager.h
 * @author baijue.huang@tuya.com
 * @brief tuya tmm manager
 * @version 0.1
 * @date 2022-8-31
 * 
 * @copyright Copyright (c) 2021
 * 
 */

#ifndef __TUYA_TMM_MANAGER_H__
#define __TUYA_TMM_MANAGER_H__

/***********************************************************************
 ** INCLUDE                                                           **
 **********************************************************************/
#include "tuya_cloud_types.h"
#include "tuya_cloud_com_defs.h"

#include "tuya_tmm_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************
 ** CONSTANT ( MACRO AND ENUM )                                       **
 **********************************************************************/

/***********************************************************************
 ** STRUCT                                                            **
 **********************************************************************/

typedef enum {
    TUYA_TMM_MANAGER_EVT_INCOMING,           /*< tmm manager incoming */
    TUYA_TMM_MANAGER_EVT_ACCEPTED,           /*< tmm manager other accepted */
    TUYA_TMM_MANAGER_EVT_REJECT,             /*< tmm manager other decline */
    TUYA_TMM_MANAGER_EVT_UNANSWERED,         /*< tmm manager other unanswered */
    TUYA_TMM_MANAGER_EVT_CANCEL,             /*< tmm manager my stop call */
    TUYA_TMM_MANAGER_EVT_HANGUP,             /*< tmm manager my or other hang up */
    TUYA_TMM_MANAGER_EVT_BUSY,               /*< tmm manager other busy */
    TUYA_TMM_MANAGER_EVT_STOP,               /*< tmm manager other stop */
    TUYA_TMM_MANAGER_EVT_ERROR,              /*< tmm manager other error */

    TUYA_TMM_MANAGER_EVT_PLAY_START,         /*< tmm manager voip play start */
    TUYA_TMM_MANAGER_EVT_PLAY_STOP,          /*< tmm manager voip play stop */
    TUYA_TMM_MANAGER_EVT_VIDEO_START,        /*< tmm manager voip start push video stream */
    TUYA_TMM_MANAGER_EVT_VIDEO_STOP,         /*< tmm manager voip stop push video stream */
    TUYA_TMM_MANAGER_EVT_AUDIO_START,        /*< tmm manager voip start push audio stream */
    TUYA_TMM_MANAGER_EVT_AUDIO_STOP,         /*< tmm manager voip stop push audio stream */
} TUYA_TMM_MANAGER_EVENT_E;

typedef enum
{
    TUYA_TMM_MANAGER_CALL_TYPE_AUDIO,              //音频通话
    TUYA_TMM_MANAGER_CALL_TYPE_VIDEO,              //视频通话
} TUYA_TMM_MANAGER_CALL_TYPE_E;

typedef VOID (*TUYA_TMM_MANAGER_DEV_RTC_CB)(CHAR_T *id, CHAR_T *name, CHAR_T *type, VOID *usrdata);
typedef OPERATE_RET (*TUYA_TMM_MANAGER_EVENT_CB)(TUYA_TMM_MANAGER_EVENT_E event, VOID *data, INT_T len);

typedef struct {
    TUYA_TMM_MANAGER_EVENT_CB   event_cb;
    TUYA_TMM_STREAM_INIT_S      stream_init_param;
} TUYA_TMM_MANAGER_INIT_S;

/***********************************************************************
 ** VARIABLE                                                          **
 **********************************************************************/

/***********************************************************************
 ** FUNCTON                                                           **
 **********************************************************************/

/*****************************************************************************
 * @brief      tuya tmm manager 初始化.
 *
 * @param      init_param: 初始化参数
 *
 * @return
 *     - 0， success
 *     - 非0 error
*****************************************************************************/
OPERATE_RET tuya_tmm_manager_init(TUYA_TMM_MANAGER_INIT_S *init_param);

/*****************************************************************************
 * @brief      tuya tmm manager 注销.
 *
 * @param      init_param: 初始化参数
 *
 * @return
 *     - 0， success
 *     - 非0 error
*****************************************************************************/
OPERATE_RET tuya_tmm_manager_deinit(VOID);

/*****************************************************************************
 * @brief      tuya tmm manager 拨打电话.
 *
 * @param      target_id: 目标设备的dev id
 *
 * @return
 *     - 0， success
 *     - 非0 error
*****************************************************************************/
OPERATE_RET tuya_tmm_manager_call(CHAR_T *target_id, TUYA_TMM_MANAGER_CALL_TYPE_E call_type);

/*****************************************************************************
 * @brief      tuya tmm manager 通话类型切换
 *
 * @param      call_type: 需要切换的类型
 *
 * @return
 *     - 0， success
 *     - 非0 error
*****************************************************************************/
OPERATE_RET tuya_tmm_manager_call_type_change(TUYA_TMM_MANAGER_CALL_TYPE_E call_type);

/*****************************************************************************
 * @brief      tuya tmm manager 麦克风状态切换
 *
 * @param      stat: 麦克风状态， TRUE：开启， FALSE：关闭
 *
 * @return
 *     - 0， success
 *     - 非0 error
*****************************************************************************/
OPERATE_RET tuya_tmm_manager_call_mic_change(BOOL_T stat);

/*****************************************************************************
 * @brief      tuya tmm manager 接通电话.
 *
 * @return
 *     - 0， success
 *     - 非0 error
*****************************************************************************/
OPERATE_RET tuya_tmm_manager_answer(VOID);

/*****************************************************************************
 * @brief      tuya tmm manager 挂断电话.
 *
 * @return
 *     - 0， success
 *     - 非0 error
*****************************************************************************/
OPERATE_RET tuya_tmm_manager_hangup(VOID);

/*****************************************************************************
 * @brief      tuya tmm manager 发送音频数据，目前支持 16bit， 8KHz，单通道PCM数据
 *
 * @return
 *     - 0， success
 *     - 非0 error
*****************************************************************************/
OPERATE_RET tuya_tmm_manager_data_feed(CHAR_T *buffer, INT_T len);

/*****************************************************************************
 * @brief      tuya tmm manager 获取支持音视频的设备列表，通过callback传出
 *
 * @param      cb: callback函数指针
 * @param      usrdata: 用户数据，通过callback返回
 * @return
 *     - 0， success
 *     - 非0 error
*****************************************************************************/
OPERATE_RET tuya_tmm_manager_get_dev_rtc_list(TUYA_TMM_MANAGER_DEV_RTC_CB cb, VOID *usrdata);

/**
 * @brief Get incoming peer type cached by manager
 * @return 0 device, 1 app, -1 unknown
 */
INT_T tuya_tmm_manager_get_incoming_type(VOID);

/**
 * @brief Get current call peer display name and device id
 * @param[out] name peer display name buffer, may be empty
 * @param[in] name_size name buffer size in bytes
 * @param[out] dev_id peer device id buffer, may be empty
 * @param[in] dev_id_size dev_id buffer size in bytes
 * @return OPRT_OK on success
 */
OPERATE_RET tuya_tmm_manager_get_peer_display(CHAR_T *name, UINT32_T name_size,
                                              CHAR_T *dev_id, UINT32_T dev_id_size);

#ifdef __cplusplus
}
#endif

#endif /* __TUYA_TMM_MANAGER_H__ */
