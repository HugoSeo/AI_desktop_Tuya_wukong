/**
 * @file tuya_tmm_stream_link.h
 * @author baijue.huang@tuya.com
 * @brief tuya tmm stream link
 * @version 0.1
 * @date 2022-9-01
 * 
 * @copyright Copyright (c) 2021
 * 
 */

#ifndef __TUYA_TMM_STREAM_LINK_H__
#define __TUYA_TMM_STREAM_LINK_H__

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

/***********************************************************************
 ** VARIABLE                                                          **
 **********************************************************************/

/***********************************************************************
 ** FUNCTON                                                           **
 **********************************************************************/

OPERATE_RET tuya_tmm_stream_link_connect(TUYA_TMM_STREAM_CONF_S *conf);

OPERATE_RET tuya_tmm_stream_link_disconnect(TUYA_TMM_STREAM_CONF_S *conf);

OPERATE_RET tuya_tmm_stream_link_control(TUYA_TMM_STREAM_CONF_S *conf, INT_T timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* __TUYA_TMM_STREAM_LINK_H__ */

