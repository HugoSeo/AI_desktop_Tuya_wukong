/**
 * @file ty_gfw_gui_kepler_file.c
 * @brief File related serial data forwarding 1K
 * @version 0.1
 * @date 2023-07-17
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

#include "ty_gfw_gui_kepler_menu.h"
#include "ty_gfw_gui_kepler_menu_download.h"
#include "uni_log.h"
#include "tuya_list.h"
#include "tal_memory.h"
#include "tal_mutex.h"
#include "base_event.h"

#include "lv_vendor_event.h"

/***********************************************************
*************************micro define***********************
***********************************************************/

/***********************************************************
***********************variable define**********************
***********************************************************/
STATIC LIST_HEAD cr_req_list_head;
STATIC MUTEX_HANDLE cr_req_mutex = NULL;
/***********************************************************
***********************function define**********************
***********************************************************/
STATIC OPERATE_RET ty_gfw_kepler_gui_menu_req_enqueue(GUI_CLOUD_RECIPE_REQ_TYPE_E cl_req_type, VOID *param, UINT_T dl_unit_len)
{
    CR_REQ_LIST_NODE_S *req_node = (CR_REQ_LIST_NODE_S *)Malloc(sizeof(CR_REQ_LIST_NODE_S));
    UINT32_T len = 0;

    if (req_node != NULL) {
        req_node->dl_unit_len = dl_unit_len;
        req_node->cr_req.req_type = cl_req_type;
        req_node->cr_req.req_param = NULL;
        if (param != NULL) {
            len = strlen((CHAR_T *)param) + 1;
            req_node->cr_req.req_param = (CHAR_T *)Malloc(len);
            if (req_node->cr_req.req_param != NULL) {
                memset(req_node->cr_req.req_param, 0, len);
                strcpy((CHAR_T *)req_node->cr_req.req_param, (CHAR_T *)param);
            }
            else {
                PR_ERR("cr req list malloc fail...");
                return OPRT_MALLOC_FAILED;
            }
        }
        tal_mutex_lock(cr_req_mutex);
        tuya_list_add_tail(&(req_node->node), &cr_req_list_head);
        tal_mutex_unlock(cr_req_mutex);
        PR_NOTICE("cr req list enqueue...");
        return OPRT_OK;
    }
    else {
        PR_ERR("cr req malloc fail...");
        return OPRT_MALLOC_FAILED;
    }
}

STATIC CR_REQ_LIST_NODE_S *ty_gfw_kepler_gui_menu_req_dequeue(VOID)
{
    struct tuya_list_head *p = NULL;
    struct tuya_list_head *n = NULL;

    tal_mutex_lock(cr_req_mutex);
    CR_REQ_LIST_NODE_S* entry = NULL;
    tuya_list_for_each_safe(p, n, &cr_req_list_head) {
        entry = tuya_list_entry(p, CR_REQ_LIST_NODE_S, node);
        DeleteNode(entry, node);
        break;
    }
    tal_mutex_unlock(cr_req_mutex);
    return entry;
}

STATIC OPERATE_RET  __cr_req_process_completed_event(VOID *data)
{
    CR_REQ_LIST_NODE_S *req_node = NULL;

    PR_INFO("%s:-----> !", __func__);

    if (ty_gfw_gui_menu_download_get_run_is() == TRUE) {
        PR_ERR("[%s:%d]:cr req is going, delay to process req type...", __func__, __LINE__);
        return OPRT_OK;
    }

    req_node = ty_gfw_kepler_gui_menu_req_dequeue();
    if (req_node == NULL) {
        PR_ERR("[%s:%d]:------get empty cr req node !\r\n", __func__, __LINE__);
        return OPRT_INVALID_PARM;
    }

    //重新唤醒文件下载服务
    if (OPRT_OK != ty_gfw_gui_menu_download_init(req_node)) {
        PR_ERR("file download failed!");
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

OPERATE_RET ty_gfw_kepler_gui_menu_req_list_init(VOID)
{
    OPERATE_RET op_ret = OPRT_OK;

    op_ret = tal_mutex_create_init(&cr_req_mutex);
    if (op_ret != OPRT_OK) {
        PR_ERR("[%s:%d]:------cr req mutex init fail ???\r\n", __func__, __LINE__);
        return op_ret;
    }

    INIT_LIST_HEAD(&cr_req_list_head);
    ty_subscribe_event(EVENT_MENU_REQ_PROCESS_COMPLETED, "cr_req", __cr_req_process_completed_event, SUBSCRIBE_TYPE_NORMAL);
    return op_ret;
}

/**
 * @brief: 开普勒文件传输
 * @desc: 文件传输服务初始化,支持文件上传和文件下载
 * @return 执行结果,返回 OPRT_OK表示成功，返回 其他 则表示失败
 * @note
 */
OPERATE_RET ty_gfw_kepler_gui_menu_init(GUI_CLOUD_RECIPE_REQ_TYPE_E cl_req_type, VOID *param, UINT_T dl_unit_len)
{
    OPERATE_RET op_ret = OPRT_OK;

    op_ret = ty_gfw_kepler_gui_menu_req_enqueue(cl_req_type, param, dl_unit_len);
    if (op_ret != OPRT_OK) {
        return op_ret;
    }

    op_ret = __cr_req_process_completed_event(NULL);
    return op_ret;
}
