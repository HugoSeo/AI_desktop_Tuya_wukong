/**
 * @file ty_gfw_gui_kepler_file_download_cloud.h
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

#ifndef __TY_GFW_GUI_KEPLER_FILE_DOWNLOAD_CLOUD_H_
#define __TY_GFW_GUI_KEPLER_FILE_DOWNLOAD_CLOUD_H_

#include "tuya_cloud_types.h"
#include "tal_semaphore.h"
#include "tal_workq_service.h"
#include "lv_vendor_event.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ID_LEN_LMT 20

/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef enum {
    TY_FILES_DL_CENCEL_NORMAL = 0,
    TY_FILES_DL_CENCEL_FILE = 1,
    TY_FILES_DL_CENCEL_WORK = 2,
} TY_FILES_DL_CENCEL_TYPE_E;

//文件信息
typedef struct {
    UINT_T id_val;
    CHAR_T id[ID_LEN_LMT];
    CHAR_T *p_name;
    UCHAR_T type;
    CHAR_T *p_url;
    UINT_T file_sz;
    CHAR_T *p_sign;
    CHAR_T *p_extInfo;
    UCHAR_T act;
} TY_FILES_DOWNLOAD_INF_S;

//文件ID结构
typedef struct{
    CHAR_T id[ID_LEN_LMT];
}TY_FILE_ID_S;

typedef enum {
    TY_FILES_DL_PACK_GO_ON = 0, /*继续拖动包*/
    TY_FILES_DL_PACK_WAIT = 1,  /*等待确认通知*/
} TY_FILES_DL_PACK_E;

//多个文件下载传输单元
typedef enum {
    TY_FILES_DL_UNIT_LEN_256 = 256,
    TY_FILES_DL_UNIT_LEN_512 = 512,
    TY_FILES_DL_UNIT_LEN_1024 = 1024,
    TY_FILES_DL_UNIT_LEN_2048 = 2048,
    TY_FILES_DL_UNIT_LEN_4096 = 4096,
    TY_FILES_DL_UNIT_LEN_5120 = 5120,
    TY_FILES_DL_UNIT_LEN_10240 = 10240,
} TY_FILES_DL_UNIT_LEN_E;

//文件下载结果
typedef enum {
    TY_FILES_DL_RESULT_SUCC = 0,
    TY_FILES_DL_RESULT_FAILED = 1,
} TY_FILES_DL_RESULT_E;

//文件下载无法枚举
typedef enum {
    TY_FILES_DL_STAT_GET_FILELIST_FAILED = 0,
    TY_FILES_DL_STAT_GET_DATA_FAILED = 1,
    TY_FILES_DL_STAT_USER_CANCEL = 2,
    TY_FILES_DL_STAT_INTERNAL_ERR = 3,
    TY_FILES_DL_STAT_SUCC = 4,
} TY_FILES_DL_STAT_E;

typedef struct {
    UCHAR_T encrypt;
    UCHAR_T signAlgo;
} TY_FILES_DL_FILELIST_INF_SECURITY_S;

typedef struct TY_FILE_DL_TRANS_DATA {
    CHAR_T head[6];//used for mcu uart cmd
    CHAR_T data[0];
} TY_FILE_DL_TRANS_DATA;

/**
 * @brief: Multi-file file download starts callback
 * @desc: 通知用户文件信息并获取文件传输单元
 * @param[in] p_file_inf:文件信息
 * @param[in] num:档案编号
 * @param[INOUT] p_iswait:是否阻塞
 * @param[INOUT] p_unit_len:多文件下载传输单元
 * @return OPERATE_RET
 * @note If it is blocked, call tfm_kepler_files_download_post_wait_result() to
 * decide whether to continue execution
 */
typedef OPERATE_RET(*FILES_DL_START_CB)(IN gui_resource_info_s *dl_res,
                                        INOUT TY_FILES_DL_PACK_E *p_iswait, INOUT TY_FILES_DL_UNIT_LEN_E *p_unit_len);
/**
 * @brief: Multi-file file download transfer callback
 * @desc: 返回多个文件以将内容下载给用户
 * @param[in] file_idx:文件编号从 1 开始
 * @param[in] data:文件数据
 * @param[in] datalen:文件数据长度
 * @param[in] offset:文件偏移量从 0 开始
 * @param[INOUT] p_iswait:Whether blocking
 * @return OPERATE_RET
 * @note If it is blocked, call tfm_kepler_files_download_post_wait_result() to
 * decide whether to continue execution
 */
typedef OPERATE_RET(*FILES_DL_TRANS_CB)(IN BOOL_T is_config, IN VOID_T *node,
                                        IN UCHAR_T file_idx, IN CHAR_T *p_data, IN USHORT_T datalen, IN UINT_T offset,
                                        INOUT TY_FILES_DL_PACK_E *p_iswait);
/**
 * @brief: Multi-file file download end callback
 * @desc: 多文件下载结束回调
 * @param[in] ret:多文件下载结果
 * @param[in] stat:多文件文件下载结束原因
 * @return OPERATE_RET
 * @note none
 */
typedef OPERATE_RET(*FILES_DL_END_CB)(IN TY_FILES_DL_RESULT_E ret, IN TY_FILES_DL_STAT_E stat);
/**
 * @brief: Multi-file delete callback
 * @desc: 多文件删除回调
 * @param[in] file_array:文件数组
 * @param[in] file_num:文件数量
 * @return OPERATE_RET
 * @note none
 */
typedef OPERATE_RET (*FILES_DELETE_CB)(IN TY_FILE_ID_S *file_array,IN UCHAR_T file_num);

//多文件下载用户回调
typedef struct {
    FILES_DL_START_CB files_dl_start_cb; //多文件文件下载开始回调
    FILES_DL_TRANS_CB files_dl_trans_cb; //多文件文件下载传输回调
    FILES_DL_END_CB files_dl_end_cb;     //多文件文件下载结束回调
    FILES_DELETE_CB file_delete_cb;      //多文件删除回调
} TY_FILES_DL_USER_REG_S;

//多个文件下载阻止回复枚举
typedef enum {
    TY_FILES_DL_WAIT_POST_SUCC = 0,   //继续多文件下载，确认成功
    TY_FILES_DL_WAIT_POST_FAILED = 1, //中止多文件下载并确认失败
} TY_FILES_DL_WAIT_POST_RESULT_E;

//多个文件下载文件执行状态枚举
typedef enum {
    TY_FILES_DL_FILEEXE_SUCC = 0,
    TY_FILES_DL_FILEEXE_ING = 1,
    TY_FILES_DL_FILEEXE_FAILED = 2,
} TY_FILES_DL_FILEEXESTAT_E;

//多个文件下载文件执行状态枚举
typedef enum {
    TY_FILES_DL_STOP_CTRL_ABORT_ALL = 1,
    TY_FILES_DL_STOP_CTRL_ABORT_CURRENT = 2,
} TY_FILES_DL_STOP_CTRL_E;

//多文件下载文件无法执行枚举
typedef enum {
    TY_FILES_DL_FILEEXEFAILED_POWEROFF = 0,
    TY_FILES_DL_FILEEXEFAILED_TRANS_TIMEOUT = 1,
    TY_FILES_DL_FILEEXEFAILED_BATTERYT_LOW = 2,
    TY_FILES_DL_FILEEXEFAILED_OVERHEATING = 3,
    TY_FILES_DL_FILEEXEFAILED_FILE_TOO_LARGE = 4,
    TY_FILES_DL_FILEEXEFAILED_DEV_SPACE_NOT_ENOUGH = 5,
    TY_FILES_DL_FILEEXEFAILED_DEV_ABNORMAL_OPERA = 6,
} TY_FILES_DL_FILEEXEFAILED_E;

/***********************************************************
***********************function declaration*****************
***********************************************************/
VOID_T tfm_kepler_gui_files_download_action(IN BOOL_T is_continue);

/**
 * @brief: Tuya Multiple file download service initialized
 * @desc: 涂鸦多文件下载服务初始化
 * @return OPERATE_RET
 * @param[in] TY_FILES_DL_USER_REG_S
 * @note none
 */
OPERATE_RET tfm_kepler_gui_files_download_init(IN TY_FILES_DL_USER_REG_S *p_user_reg);

/**
 * @brief: Tuya Multiple file download service is initialized
 * @desc: 涂鸦多文件下载服务是否在初始化运行
 * @return BOOL
 * @param[in] NULL
 * @note none
 */
BOOL_T tfm_kepler_gui_files_download_is_initialized(VOID);

/**
 * @brief: tfm_kepler_gui_files_download_stop_control
 * @desc:中止文件传输控制
 * @return OPERATE_RET
 * @param[in] stop_type : 真：失败  假：主动停止
 * @param[in] stop_ctrl :TY_FILES_DL_STOP_CTRL_ABORT_ALL：中止所有文件传输
                         TY_FILES_DL_STOP_CTRL_ABORT_CURRENT：中止当前文件传输开始下一次文件传输
 * @note none
 */
OPERATE_RET tfm_kepler_gui_files_download_stop_control(IN BOOL_T stop_type, IN TY_FILES_DL_STOP_CTRL_E stop_ctrl);

gui_version_s *tfm_kepler_gui_files_get_version_node(VOID);

#ifdef __cplusplus
} // extern "C"
#endif
#endif
