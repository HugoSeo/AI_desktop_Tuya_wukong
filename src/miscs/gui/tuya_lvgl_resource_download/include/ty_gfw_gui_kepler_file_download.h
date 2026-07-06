/**
 * @file ty_gfw_gui_kepler_file_download.h
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

#ifndef __TY_GFW_GUI_KEPLER_FILE_DOWNLOAD_H_
#define __TY_GFW_GUI_KEPLER_FILE_DOWNLOAD_H_

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
***********************function declaration*****************
***********************************************************/
/**
 * @brief: 文件下载回调
 * @desc: 文件下载开始回调
 * @return VOID
 * @note none
 */
typedef VOID (*FILES_DL_TASK_START_CB)(VOID);

/**
 * @brief: 文件下载回调
 * @desc: 文件下载结束回调
 * @return VOID
 * @note none
 */
typedef VOID (*FILES_DL_TASK_END_CB)(VOID);

/**
 * @brief: ty_gfw_gui_file_download_get_run_is
 * @desc: 获取文件下载运行状态
 * @return 文件下载运行状态
 */
BOOL_T ty_gfw_gui_file_download_get_run_is(VOID);

/**
 * @brief: ty_gfw_gui_file_set_unit_len
 * @desc: 设置单包数据长度
 * @return none
 * @note 设置每个数据包传输的大小
 */
VOID ty_gfw_gui_file_set_unit_len(UINT_T len);

/**
 * @brief: 文件下载初始化
 * @desc: kepler文件下载初始化
 * @return 执行结果,返回 OPRT_OK表示成功，返回 其他 则表示失败
 * @note none
 */
OPERATE_RET ty_gfw_gui_file_system_path_type_restore(UINT_T fs_path_type);

/**
 * @brief: 文件下载初始化
 * @desc: kepler文件下载初始化
 * @return 执行结果,返回 OPRT_OK表示成功，返回 其他 则表示失败
 * @note none
 */
OPERATE_RET ty_gfw_gui_file_download_init(VOID);

#ifdef __cplusplus
} // extern "C"
#endif
#endif
