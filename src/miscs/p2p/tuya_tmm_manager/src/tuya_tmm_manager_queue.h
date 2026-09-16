/**
 * @file tuya_tmm_manager_queue.h
 * @author baijue.huang@tuya.com
 * @brief tuya tmm manager queue
 * @version 0.1
 * @date 2022-9-02
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#ifndef __TUYA_TMM_MANAGER_QUEUE_H__
#define __TUYA_TMM_MANAGER_QUEUE_H__

/***********************************************************************
 ** INCLUDE                                                           **
 **********************************************************************/
#include "tuya_cloud_types.h"
#include "tuya_cloud_com_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************
 ** CONSTANT ( MACRO AND ENUM )                                       **
 **********************************************************************/

/***********************************************************************
 ** STRUCT                                                            **
 **********************************************************************/
typedef struct tuya_tmm_mgr_queue *TUYA_TMM_QUEUE_HANDLE_T;

/***********************************************************************
 ** VARIABLE                                                          **
 **********************************************************************/

/***********************************************************************
 ** FUNCTON                                                           **
 **********************************************************************/

/*****************************************************************************
 * @brief tuya_tmm_mgr_queue_create 消息队列
 *
 * @param[in] msgsize 消息体的大小，msgcount 消息体的个数
 * @param[out] handle 返回queue句柄
 * @return int 0=成功，非0=失败
*****************************************************************************/
OPERATE_RET tuya_tmm_mgr_queue_create(TUYA_TMM_QUEUE_HANDLE_T *handle, INT_T msgsize, INT_T msgcount);

/*****************************************************************************
 * @brief tuya_tmm_mgr_queue_destroy 消息队列销毁
 *
 * @param[in] handle 返回queue句柄
 * @return int 0=成功，非0=失败
*****************************************************************************/
OPERATE_RET tuya_tmm_mgr_queue_destroy(TUYA_TMM_QUEUE_HANDLE_T handle);

/*****************************************************************************
 * @brief tuya_tmm_mgr_queue_post用于发送一个消息到指定的队列中
 *
 * @param[in] handle queue句柄，data消息体指针，timeout 超时时间
 * @return int 0=成功，非0=失败
*****************************************************************************/
OPERATE_RET tuya_tmm_mgr_queue_post(TUYA_TMM_QUEUE_HANDLE_T handle, VOID *data, UINT_T timeout);

/*****************************************************************************
 * @brief tuya_tmm_mgr_queue_fetch
 *
 * @param[in] handle  tuya queue句柄，data消息体指针，timeout 超时时间
 * @return int 0=成功，非0=失败
*****************************************************************************/
OPERATE_RET tuya_tmm_mgr_queue_fetch(TUYA_TMM_QUEUE_HANDLE_T handle, VOID *msg, UINT_T timeout);

#ifdef __cplusplus
}
#endif

#endif /* __TUYA_TMM_MANAGER_QUEUE_H__ */