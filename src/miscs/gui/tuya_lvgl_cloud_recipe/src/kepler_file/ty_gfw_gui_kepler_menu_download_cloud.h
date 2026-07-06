/**
 * @file ty_gfw_gui_kepler_menu_download_cloud.h
 * @brief
 * @version 0.1
 * @date 2023-07-18
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

#ifndef __TY_GFW_GUI_KEPLER_MENU_DOWNLOAD_CLOUD_H_
#define __TY_GFW_GUI_KEPLER_MENU_DOWNLOAD_CLOUD_H_

#include "tuya_cloud_types.h"
#include "tal_semaphore.h"
#include "tal_workq_service.h"
#include "tuya_list.h"
#include "lv_vendor_event.h"
#include "ty_gfw_gui_cloud_recipe_param.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MENU_ID_LEN_LMT 20

/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef struct TY_MENU_DL_TRANS_DATA {
    CHAR_T head[6];//used for mcu uart cmd
    CHAR_T data[0];
} TY_MENU_DL_TRANS_DATA;

typedef OPERATE_RET(*MENU_GET_LIST_HDL_CB)(IN VOID *json);
typedef OPERATE_RET(*MENU_GEN_PARAM_HDL_CB)(IN VOID *req_param, OUT CHAR_T **p_out);

//云食谱请求信息
#define EVENT_MENU_REQ_PROCESS_COMPLETED   "cr.req.finish" // failed to connect wifi
typedef struct
{
    LIST_HEAD node;
    gui_cloud_recipe_interactive_s cr_req;
    UINT_T dl_unit_len;
} CR_REQ_LIST_NODE_S;

//云食谱获取信息
typedef struct {
    GUI_CLOUD_RECIPE_REQ_TYPE_E req_type;
    CONST CHAR_T *api_name;
    CONST CHAR_T *api_ver;
    VOID *req_param;
    MENU_GEN_PARAM_HDL_CB menu_gen_parm_hdl;
    MENU_GET_LIST_HDL_CB menu_get_list_hdl;
} TY_CLOUD_RECIPE_HDL_INF_S;

/***********************************************************
***********************function declaration*****************
***********************************************************/
/**
 * @brief: Tuya Multiple file download service initialized
 * @desc: 涂鸦多文件下载服务初始化
 * @return OPERATE_RET
 * @param[in] TY_MENU_DL_USER_REG_S
 * @note none
 */
OPERATE_RET tfm_kepler_gui_menu_download_init(CR_REQ_LIST_NODE_S *req_node);

/**
 * @brief: Tuya Multiple file download service is initialized
 * @desc: 涂鸦多文件下载服务是否在初始化运行
 * @return BOOL
 * @param[in] NULL
 * @note none
 */
BOOL_T tfm_kepler_gui_menu_download_is_initialized(VOID);

#ifdef __cplusplus
} // extern "C"
#endif
#endif
