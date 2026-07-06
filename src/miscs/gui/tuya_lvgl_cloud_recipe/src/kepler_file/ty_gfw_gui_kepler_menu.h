/**
 * @file ty_gfw_gui_kepler_menu.h
 * @brief
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

#ifndef __TY_GFW_GUI_KEPLER_MENU_H_
#define __TY_GFW_GUI_KEPLER_MENU_H_

#include "tuya_cloud_types.h"
#include "ty_gfw_gui_kepler_menu_download_cloud.h"

#ifdef __cplusplus
extern "C" {
#endif

OPERATE_RET ty_gfw_kepler_gui_menu_req_list_init(VOID);

/**
 * @brief: 开普勒文件传输
 * @desc: 文件传输服务初始化,支持文件上传和文件下载
 * @return 执行结果,返回 OPRT_OK表示成功，返回 其他 则表示失败
 * @note
 */
OPERATE_RET ty_gfw_kepler_gui_menu_init(GUI_CLOUD_RECIPE_REQ_TYPE_E cl_req_type, VOID *param, UINT_T dl_unit_len);

#ifdef __cplusplus
} // extern "C"
#endif

#endif

