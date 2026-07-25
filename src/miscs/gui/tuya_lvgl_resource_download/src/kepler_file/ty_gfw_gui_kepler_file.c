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

#include "uni_log.h"
#include "string.h"
#include "tal_time_service.h"
#include "ty_gfw_gui_kepler_file.h"

#include "ty_gfw_gui_kepler_file_download_cloud.h"
#include "ty_gfw_gui_kepler_file_download.h"

/***********************************************************
*************************micro define***********************
***********************************************************/

/***********************************************************
***********************variable define**********************
***********************************************************/

/***********************************************************
***********************function define**********************
***********************************************************/

/**
 * @brief: 开普勒文件传输
 * @desc: 文件传输服务初始化,支持文件上传和文件下载
 * @return 执行结果,返回 OPRT_OK表示成功，返回 其他 则表示失败
 * @note
 */
OPERATE_RET ty_gfw_kepler_gui_file_init(VOID)
{
    OPERATE_RET op_ret = OPRT_OK;

    if (ty_gfw_gui_file_download_get_run_is() == TRUE) {
        PR_ERR("file download is going...");
        return op_ret;
    }

    //文件下载服务
    op_ret = ty_gfw_gui_file_download_init();
    if (OPRT_OK != op_ret) {
        PR_ERR("file download failed! ret:%d", op_ret);
        return op_ret;
    }
    return op_ret;
}
