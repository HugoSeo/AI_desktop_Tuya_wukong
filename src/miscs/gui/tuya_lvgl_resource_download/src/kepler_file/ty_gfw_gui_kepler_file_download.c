/**
 * @file ty_gfw_gui_kepler_file_download.c
 * @brief tuya files download module
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

#include "uni_log.h"
#include "ty_cJSON.h"
#include "string.h"
#include "base_event.h"
#include "tal_mutex.h"
#include "tal_semaphore.h"
#include "tal_sw_timer.h"
#include "tuya_ws_db.h"
#include "ty_gfw_gui_kepler_file_download.h"
#include "ty_gfw_gui_kepler_file_download_cloud.h"
#include "ty_gfw_gui_kepler_file.h"
#include "lv_vendor_event.h"
#include "tuya_app_gui_display_text.h"
#include "ty_gui_fs.h"
#include <stdio.h>
#include <sys/stat.h>

/***********************************************************
*************************micro define***********************
***********************************************************/
#define MCU_RECV_TIME 5 // MCU recv timeout
#define TYPE_FILE_NEW 2

typedef UCHAR_T MCU_RET; // cmd 3704
#define MCU_ABORT_ALL     0x00
#define MCU_ABORT_CURRENT 0x01
#define MCU_GET_STATUS    0x02

typedef CHAR_T DOWNLOAD_FILE_STATE_GUI;
#define FILE_REASON_NO_FILE             0x00
#define FILE_REASON_START_LOADING       0x01
#define FILE_REASON_LOADING             0x02
#define FILE_REASON_DOWNLOAD_SUCCESS    0x03
#define FILE_REASON_UPLOAD_SUCCESS      0x04
#define FILE_REASON_MCU_TIMEOUT         0x05
#define FILE_REASON_UPLOAD_URL_FAILED   0x06
#define FILE_REASON_UPLOAD_FAILED       0x07
#define FILE_REASON_DOWNLOAD_FAILED     0x08
#define FILE_REASON_DOWNLOAD_ACK_FAILED 0x09

typedef CHAR_T FILE_RET;
#define FILE_RET_SUCESS 0x00
#define FILE_RET_FAILED 0x01
#define FILE_DP_TIMEOUT 30

/***********************************************************
***********************typedef define***********************
***********************************************************/

typedef struct {
    BOOL_T run_is;
    DOWNLOAD_FILE_STATE_GUI file_state;
    UINT_T unit_len;
    BOOL_T start_flag;
} FILE_DL_TASK_MANAGE_S;

/***********************************************************
***********************variable define**********************
***********************************************************/
STATIC CONST UINT_T unit_size[] = {256, 512, 1024, 2048, 3072, 4096, 5120, 10240};

STATIC FILE_DL_TASK_MANAGE_S *g_p_flies_dl_task_manage = NULL;

/***********************************************************
***********************function define**********************
***********************************************************/
/**
 * @brief: ty_gfw_gui_file_download_get_run_is
 * @desc: 获取文件下载运行状态
 * @return 文件下载运行状态
 */
BOOL_T ty_gfw_gui_file_download_get_run_is(VOID)
{
    if (tfm_kepler_gui_files_download_is_initialized())
        return TRUE;

    if (NULL == g_p_flies_dl_task_manage)
        return FALSE;
    return g_p_flies_dl_task_manage->run_is;
}

/**
 * @brief: ty_gfw_gui_file_set_unit_len
 * @desc: 设置单包数据长度
 * @return none
 * @note 设置每个数据包传输的大小
 */
VOID ty_gfw_gui_file_set_unit_len(UINT_T len)
{
    g_p_flies_dl_task_manage->unit_len = len;
}

/**
 * @brief: __get_unit_len
 * @desc: 获取单包传输长度
 * @return 单包数据长度
 * @note 读取每个数据包的大小
 */
STATIC UINT_T __get_unit_len(VOID)
{
    return g_p_flies_dl_task_manage->unit_len;
}

STATIC OPERATE_RET file_dl_mkdir_p(CONST CHAR_T *file_path)
{
    OPERATE_RET rt = OPRT_COM_ERROR;
    CHAR_T tmp[UI_FILE_PATH_MAX_LEN];
    CHAR_T *p = NULL;
    size_t len;

    memset(tmp, 0, sizeof(tmp));
    strncpy(tmp, file_path, sizeof(tmp));

    len = strlen(tmp);
    if(len == 0) {
        PR_ERR("Invalid path: empty string\n");
        return rt;
    }

    // 移除末尾的斜杠（如果有）
    if(tmp[len - 1] == '/')
        tmp[len - 1] = '\0';

    // 从路径的第二个字符开始，避免处理根目录 '/'
    for(p = tmp + 1; *p; p++) {
        if(*p == '/') {
            *p = '\0';
            if(ty_gui_fs_mkdir(tmp) != 0) {
                PR_ERR("mkdir '%s' error\n", tmp);
                return rt;
            }
            PR_NOTICE("%s-successful create folder '%s'\r\n", __func__, tmp);
            *p = '/';
        }
    }

    // 创建最终的目录
    if(ty_gui_fs_mkdir(tmp) != 0) {
        PR_ERR("mkdir '%s' error\n", tmp);
    }
    else {
        rt = OPRT_OK;
        PR_NOTICE("%s-successful create final folder '%s'\r\n", __func__, tmp);
    }

    return rt;
}

STATIC OPERATE_RET file_dl_remove_fs_folder(IN CONST CHAR_T *folder)
{
    OPERATE_RET op_ret = OPRT_OK;
    TUYA_DIR dir = NULL;
    TUYA_FILEINFO info = NULL;
    CONST CHAR_T *name = NULL;
    BOOL_T is_exist = FALSE;
    CHAR_T pathname[UI_FILE_PATH_MAX_LEN];
    UINT_T mode = 0;
    INT_T dir_read_ret = 0;

    ty_gui_fs_is_exist(folder, &is_exist);
    if (is_exist == TRUE) {       //目录存在!
        op_ret = ty_gui_fs_mode(folder, &mode);
        if (op_ret != OPRT_OK) {
            PR_ERR("%s:%d..... get folder '%s' mode fail ???", __FUNCTION__, __LINE__, folder);
            return op_ret;
        }
        if (S_ISDIR(mode)) {     //文件夹
            op_ret = ty_gui_dir_open(folder, &dir);
            if (op_ret == OPRT_OK) {
                for (;;) {
                    info = NULL;
                    dir_read_ret = ty_gui_dir_read(dir, &info);
                    if (info == NULL || dir_read_ret != 0)
                        break;
                    op_ret = ty_gui_dir_name(info, &name);
                    if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
                        memset(pathname, 0, UI_FILE_PATH_MAX_LEN);
                        snprintf(pathname, UI_FILE_PATH_MAX_LEN, "%s/%s", folder, name);
                        op_ret = file_dl_remove_fs_folder(pathname);
                    }
                }
                //删除目录
                ty_gui_dir_close(dir);
                op_ret = ty_gui_fs_remove(folder);
                PR_WARN("[%s][%d] remove folder '%s' ret '%d'.......\r\n", __FUNCTION__, __LINE__, folder, op_ret);
            }
            else {
                PR_ERR("[%s][%d] open dir '%s' fail ???\r\n", __FUNCTION__, __LINE__, folder);
            }
        }
        else {      //普通文件
            op_ret = ty_gui_fs_remove(folder);
            PR_WARN("[%s][%d] remove file '%s' ret '%d'.......\r\n", __FUNCTION__, __LINE__, folder, op_ret);
        }
    }
    else {
        PR_ERR("%s:%d..... fs '%s' not exist", __FUNCTION__, __LINE__, folder);
    }

    return op_ret;
}

STATIC CONST CHAR_T *file_dl_get_lang_config_file_restore_tmp_path(GUI_LANGUAGE_E language)
{
    if(GUI_LG_CHINESE == language)
        return tuya_app_gui_get_unused_lang_ch_path();
    else if(GUI_LG_ENGLISH == language)
        return tuya_app_gui_get_unused_lang_en_path();
    else if(GUI_LG_KOREAN == language)
        return tuya_app_gui_get_unused_lang_kr_path();
    else if(GUI_LG_JAPANESE == language)
        return tuya_app_gui_get_unused_lang_jp_path();
    else if(GUI_LG_FRENCH == language)
        return tuya_app_gui_get_unused_lang_fr_path();
    else if(GUI_LG_GERMAN == language)
        return tuya_app_gui_get_unused_lang_de_path();
    else if(GUI_LG_RUSSIAN == language)
        return tuya_app_gui_get_unused_lang_ru_path();
    else if(GUI_LG_INDIA == language)
        return tuya_app_gui_get_unused_lang_in_path();
#ifdef GUI_RESOURCE_MANAGMENT_VER_1_0
    else if(GUI_LG_ALL == language)
        return tuya_app_gui_get_unused_lang_all_path();
#endif
    else {
        PR_ERR("[%s][%d] unknown language type '%d' ???\r\n", __FUNCTION__, __LINE__, language);
        return NULL;
    }
}

STATIC OPERATE_RET file_dl_rename_resource_file_by_id(gui_res_file_s *file_node)
{
    OPERATE_RET op_ret = OPRT_OK;
    CHAR_T *ptr = NULL;
    CHAR_T *file_name = NULL;

    //file_node->file_name应该按照id来命名!!!!!!
    if ((file_node->type == T_GUI_RES_MUSIC) || (file_node->type == T_GUI_RES_PICTURE)) {
        ptr = strrchr(file_node->file_name, '.');
        if (ptr != NULL) {
            file_name = (CHAR_T *)tal_malloc(UI_FILE_NAME_MAX_LEN);
            if (file_name == NULL) {
                op_ret = OPRT_MALLOC_FAILED;
                PR_ERR("[%s][%d] malloc fail ???\r\n", __FUNCTION__, __LINE__);
                return op_ret;
            }
            memset(file_name, 0, UI_FILE_NAME_MAX_LEN);
            if (file_node->type == T_GUI_RES_MUSIC) {
                if(strcmp(ptr, ".mp3") == 0) {
                    snprintf(file_name, UI_FILE_NAME_MAX_LEN, "%d.mp3", file_node->id);
                }
                else if (strcmp(ptr, ".MP3") == 0) {
                    snprintf(file_name, UI_FILE_NAME_MAX_LEN, "%d.MP3", file_node->id);
                }
                else {
                    op_ret = OPRT_COM_ERROR;
                    PR_ERR("[%s][%d] unrecognized file music name '%s' ???\r\n", __FUNCTION__, __LINE__, file_node->file_name);
                }
            }
            else {
                if (strcmp(ptr, ".jpg") == 0) {
                    snprintf(file_name, UI_FILE_NAME_MAX_LEN, "%d.jpg", file_node->id);
                }
                else if (strcmp(ptr, ".JPG") == 0) {
                    snprintf(file_name, UI_FILE_NAME_MAX_LEN, "%d.JPG", file_node->id);
                }
                else if (strcmp(ptr, ".png") == 0) {
                    snprintf(file_name, UI_FILE_NAME_MAX_LEN, "%d.png", file_node->id);
                }
                else if (strcmp(ptr, ".PNG") == 0) {
                    snprintf(file_name, UI_FILE_NAME_MAX_LEN, "%d.PNG", file_node->id);
                }
                else if (strcmp(ptr, ".gif") == 0) {
                    snprintf(file_name, UI_FILE_NAME_MAX_LEN, "%d.gif", file_node->id);
                }
                else if (strcmp(ptr, ".GIF") == 0) {
                    snprintf(file_name, UI_FILE_NAME_MAX_LEN, "%d.GIF", file_node->id);
                }
                else {
                    op_ret = OPRT_COM_ERROR;
                    PR_ERR("[%s][%d] unrecognized file picture name '%s' ???\r\n", __FUNCTION__, __LINE__, file_node->file_name);
                }
            }
            if (op_ret == OPRT_OK) {
                PR_NOTICE("[%s][%d] '%s' rename to '%s'",__FUNCTION__, __LINE__, file_node->file_name, file_name);
                tal_free(file_node->file_name);
                file_node->file_name = file_name;
            }
            else {
                tal_free(file_name);
            }
        }
        else {
            op_ret = OPRT_COM_ERROR;
            PR_ERR("[%s][%d] unrecognized file name '%s' ???\r\n", __FUNCTION__, __LINE__, file_node->file_name);
        }
    }
    else {
        //其他资源暂时不需要更名!
    }
    return op_ret;
}

/**
 * @brief: file_dl_start_callback
 * @desc: 文件下载启动回调
 * @param[in] p_file_inf:文件信息结构
 * @param[in] num:文件数量
 * @param[inout] p_iswait:是否阻塞
 * @param[inout] p_unit_len:单包数据长度
 * @return 执行结果,返回 OPRT_OK表示成功，返回 其他 则表示失败
 * @note 如果是阻塞状态, 调用tuya_svc_files_download_post_wait_result()来决定是否继续执行
 */
STATIC OPERATE_RET file_dl_start_callback(IN gui_resource_info_s *dl_res,INOUT TY_FILES_DL_PACK_E *p_iswait, INOUT TY_FILES_DL_UNIT_LEN_E *p_unit_len)
{
    OPERATE_RET op_ret = OPRT_OK;

    PR_NOTICE("file_dl_start_callback ...");
    if ((NULL == dl_res) || (NULL == p_iswait) || (NULL == p_unit_len)) {
        PR_ERR("file_dl_start_callback is NULL");
        return OPRT_INVALID_PARM;
    }

    if (TRUE == g_p_flies_dl_task_manage->run_is) {
        return OPRT_COM_ERROR;
    }

    //获取当前单元下载长度!
    *p_unit_len = __get_unit_len();

    g_p_flies_dl_task_manage->file_state = FILE_REASON_START_LOADING;
    //到这里为止,需要用户从GUI返回是否决定下一步下载资源,,,,,,,,,,!
    *p_iswait = TY_FILES_DL_PACK_WAIT;      //TY_FILES_DL_PACK_GO_ON;

    //删除备份的文件系统目录:
    PR_NOTICE("file_dl_start_callback ... delete fs path '%s'!", tuya_app_gui_get_unused_top_path());
    op_ret = file_dl_remove_fs_folder(tuya_app_gui_get_unused_top_path());

    g_p_flies_dl_task_manage->run_is = TRUE;
    g_p_flies_dl_task_manage->start_flag = TRUE;
    return op_ret;
}

/**
 * @brief: file_dl_trans_callback
 * @desc: 文件下载数据处理
 * @param[in] file_idx:文件索引，从1开始
 * @param[in] data:文件数据
 * @param[in] datalen:数据长度
 * @param[in] offset:偏移量，从0开始
 * @param[inout] p_iswait:是否阻塞
 * @return 执行结果,返回 OPRT_OK表示成功，返回 其他 则表示失败
 * @note 如果是阻塞状态, 调用tuya_svc_files_download_post_wait_result()来决定是否继续执行
 */
STATIC OPERATE_RET file_dl_trans_callback(IN BOOL_T is_config, IN VOID_T *file_node,
                                        IN UCHAR_T file_idx, IN CHAR_T *p_data, IN USHORT_T datalen, IN UINT_T offset,
                                          INOUT TY_FILES_DL_PACK_E *p_iswait)
{
    OPERATE_RET op_ret = OPRT_OK;
    gui_res_lang_s *_config_node = NULL;
    gui_res_file_s *_file_node = NULL;
    INT_T w_len = 0;
    INT_T w_ret = 0;
    CONST CHAR_T *file_path = NULL;
    CHAR_T *fname = NULL;

    if ((NULL == p_data) || (NULL == p_iswait)) {
        PR_ERR("file dl_trans_callback is NULL");
        return OPRT_INVALID_PARM;
    }
    *p_iswait = TY_FILES_DL_PACK_GO_ON;
    if (FALSE == g_p_flies_dl_task_manage->run_is) {
        return OPRT_COM_ERROR;
    }

    if (is_config) {
        _config_node = (gui_res_lang_s *)file_node;
        if (offset == 0) {
            PR_NOTICE("config file dl_trans_callback ... file idx '%d' name '%s' dl start----------------->", 
                                        file_idx, _config_node->file_name);
            file_path = file_dl_get_lang_config_file_restore_tmp_path(_config_node->language);
            if (file_dl_mkdir_p(file_path) == OPRT_OK) {
                if (file_path != NULL) {
                    fname = (CHAR_T *)tal_malloc(UI_FILE_PATH_MAX_LEN);
                    memset(fname, 0, UI_FILE_PATH_MAX_LEN);
                    snprintf(fname, UI_FILE_PATH_MAX_LEN, "%s%s", file_path, _config_node->file_name);
                    PR_NOTICE("config file dl_trans_callback ... open file tmp path '%s'", fname);
                    _config_node->file_hdl = (VOID *)ty_gui_fopen(fname, "wb");
                    tal_free(fname);
                }
                else {
                    PR_ERR("config file dl_trans_callback file path unknown!!!");
                    return OPRT_COM_ERROR;
                }
            }
            else {
                PR_ERR("config file dl_trans_callback file path create fail!!!");
                return OPRT_COM_ERROR;
            }
        }
        if (_config_node->file_hdl != NULL) {
            w_len = ty_gui_fwrite(p_data, datalen, (TUYA_FILE)_config_node->file_hdl);
            //PR_NOTICE("config file dl_trans_callback ...write offset '%d' len '%d'", offset, w_len);
            if (w_len != datalen) {
                PR_ERR("config file dl_trans_callback write len mismatch [%d --- %d]!!!", w_len, datalen);
                return OPRT_COM_ERROR;
            }
            if (offset + datalen >= _config_node->size) {       //write end
                PR_NOTICE("config file dl_trans_callback ... file idx '%d' name '%s' dl end==================>", 
                                            file_idx, _config_node->file_name);
                w_ret = ty_gui_fclose((TUYA_FILE)_config_node->file_hdl);
                if (w_ret != OPRT_OK) {
                    PR_ERR("config file dl_trans_callback file close fail, err = %d !!!", w_ret);
                    return OPRT_COM_ERROR;
                }
            }
        }
        else {
            PR_ERR("config file dl_trans_callback file hdl is NULL");
            return OPRT_COM_ERROR;
        }
    }
    else {
        _file_node = (gui_res_file_s *)file_node;
        if (offset == 0) {
            PR_NOTICE("file dl_trans_callback ... file idx '%d' type '%d' name '%s' dl start----------------->", 
                                        file_idx, _file_node->type, _file_node->file_name);
            if (_file_node->type == T_GUI_RES_FONT)
                file_path = tuya_app_gui_get_unused_font_path();
            else if (_file_node->type == T_GUI_RES_MUSIC)
                file_path = tuya_app_gui_get_unused_music_path();
            else if (_file_node->type == T_GUI_RES_PICTURE)
                file_path = tuya_app_gui_get_unused_picture_path();
            else {
                PR_ERR("file dl_trans_callback file type unknown of '%d'!!!", _file_node->type);
                return OPRT_COM_ERROR;
            }
            if (file_dl_mkdir_p(file_path) == OPRT_OK) {
                if (file_dl_rename_resource_file_by_id(_file_node) == OPRT_OK) {
                    fname = (CHAR_T *)tal_malloc(UI_FILE_PATH_MAX_LEN);
                    memset(fname, 0, UI_FILE_PATH_MAX_LEN);
                    snprintf(fname, UI_FILE_PATH_MAX_LEN, "%s%s", file_path, _file_node->file_name);
                    PR_NOTICE("file dl_trans_callback ... open file tmp path '%s'", fname);
                    _file_node->file_hdl = (VOID *)ty_gui_fopen(fname, "wb");
                    tal_free(fname);
                }
                else {
                    PR_ERR("file dl_trans_callback file rename fail!!!", _file_node->type);
                    return OPRT_COM_ERROR;
                }
            }
            else {
                PR_ERR("file dl_trans_callback file path create fail!!!");
                return OPRT_COM_ERROR;
            }
        }
        if (_file_node->file_hdl != NULL) {
            w_len = ty_gui_fwrite(p_data, datalen, (TUYA_FILE)_file_node->file_hdl);
            PR_NOTICE("file dl_trans_callback ...write offset '%d' len '%d'", offset, w_len);
            if (w_len != datalen) {
                PR_ERR("file dl_trans_callback write len mismatch [%d --- %d]!!!", w_len, datalen);
                return OPRT_COM_ERROR;
            }
            if (offset + datalen >= _file_node->size) {       //write end
                PR_NOTICE("file dl_trans_callback ... file idx '%d' name '%s' dl end==================>", 
                                            file_idx, _file_node->file_name);
                w_ret = ty_gui_fclose((TUYA_FILE)_file_node->file_hdl);
                if (w_ret != OPRT_OK) {
                    PR_ERR("file dl_trans_callback file close fail, err = %d !!!", w_ret);
                    return OPRT_COM_ERROR;
                }
            }
        }
        else {
            PR_ERR("file dl_trans_callback file hdl is NULL");
            return OPRT_COM_ERROR;
        }
    }

    /*
    op_ret = ty_download_file_cb(file_idx, p_data, datalen, offset);
    if (op_ret != OPRT_OK) {
        PR_ERR("ty_download_file_cb! ret:[%d]", op_ret);
        return OPRT_COM_ERROR;
    }
    */
    g_p_flies_dl_task_manage->file_state = FILE_REASON_LOADING;
    return op_ret;
}

/**
 * @brief: file_dl_end_callback
 * @desc: 文件下载结束回调
 * @param[in] ret:文件下载结果
 * @param[in] stat:Multi-file file download end reason
 * @return 执行结果,返回 OPRT_OK表示成功，返回 其他 则表示失败
 * @note none
 */
STATIC OPERATE_RET file_dl_end_callback(IN TY_FILES_DL_RESULT_E ret, IN TY_FILES_DL_STAT_E stat)
{
    OPERATE_RET op_ret = OPRT_OK;

    if (g_p_flies_dl_task_manage->start_flag == FALSE) {
        return op_ret;
    }
    g_p_flies_dl_task_manage->start_flag = FALSE;

    if (ret == TY_FILES_DL_RESULT_SUCC && stat == TY_FILES_DL_STAT_SUCC) {
        TUYA_FILE file_hdl = NULL;
        //TODO...   1.vesion.inf的重写
        //          2.这里要完成新文件目录类型指定!
        PR_NOTICE("file_dl_end_callback dl end successful==================>");
        file_hdl = ty_gui_fopen(tuya_app_gui_get_unused_version_path(), "wb");
        if (file_hdl != NULL) {
            INT_T w_num = 0, ret_num = 0;
            gui_version_s *Pversion = tfm_kepler_gui_files_get_version_node();
            w_num = strlen(Pversion->ver);
            ret_num = ty_gui_fwrite(Pversion->ver, w_num, file_hdl);
            ty_gui_fclose(file_hdl);
            if (w_num == ret_num) {
                TUYA_GUI_FS_PINPONG_E curr_fs_path_type = tuya_app_gui_get_path_type();
                //保持双文件系统
                op_ret = ty_gfw_gui_file_system_path_type_restore((curr_fs_path_type==TUYA_GUI_FS_PP_MAIN)?TUYA_GUI_FS_PP_EXT:TUYA_GUI_FS_PP_MAIN);
            }
            else {
                op_ret = OPRT_COM_ERROR;
                PR_ERR("[%s][%d] write file '%s' len mismatch '%d' - '%d' ???\r\n", __FUNCTION__, __LINE__, tuya_app_gui_get_unused_version_path(), w_num, ret_num);
            }
        }
        else {
            op_ret = OPRT_COM_ERROR;
            PR_ERR("[%s][%d] open file name '%s' fail ???\r\n", __FUNCTION__, __LINE__, tuya_app_gui_get_unused_version_path());
        }
    }
    else {
        PR_NOTICE("file_dl_end_callback dl end fail==================>");
    }

    g_p_flies_dl_task_manage->file_state = FILE_REASON_NO_FILE;
    g_p_flies_dl_task_manage->run_is = FALSE;
    Free(g_p_flies_dl_task_manage);
    g_p_flies_dl_task_manage = NULL;

    return op_ret;
}

/**
 * @brief: 文件路径类型保存
 * @desc: 文件路径类型保存
 * @return 执行结果,返回 OPRT_OK表示成功，返回 其他 则表示失败
 * @note none
 */
OPERATE_RET ty_gfw_gui_file_system_path_type_restore(UINT_T fs_path_type)
{
    OPERATE_RET op_ret = OPRT_OK;
    CHAR_T str[10] = {0};

    snprintf(str, sizeof(str), "%d", fs_path_type);
    op_ret = wd_common_write(GUI_FS_PATH_INFO, (BYTE_T *)&str, strlen(str) + 1);
    if (OPRT_OK != op_ret) {
        PR_ERR("write fs path type '%d' err:%d", fs_path_type, op_ret);
        return op_ret;
    }
    PR_NOTICE("%s:%d==================>save fs pah type as '%d' sucessful!", __func__, __LINE__, fs_path_type);
    return op_ret;
}

/**
 * @brief: 文件下载初始化
 * @desc: kepler文件下载初始化
 * @param[in] trans_handle 传输句柄
 * @return 执行结果,返回 OPRT_OK表示成功，返回 其他 则表示失败
 * @note none
 */
OPERATE_RET ty_gfw_gui_file_download_init(VOID)
{
    OPERATE_RET op_ret = OPRT_OK;
    TY_FILES_DL_USER_REG_S p_user_reg;

    g_p_flies_dl_task_manage = (FILE_DL_TASK_MANAGE_S *)Malloc(SIZEOF(FILE_DL_TASK_MANAGE_S));
    if (NULL == g_p_flies_dl_task_manage) {
        PR_ERR("Malloc failed!");
        return OPRT_MALLOC_FAILED;
    }
    memset(g_p_flies_dl_task_manage, 0, SIZEOF(FILE_DL_TASK_MANAGE_S));

    ty_gfw_gui_file_set_unit_len(unit_size[2]);     //256, 512, 1024, 2048, 3072, 4096, 5120, 10240

    p_user_reg.files_dl_start_cb = file_dl_start_callback;
    p_user_reg.files_dl_trans_cb = file_dl_trans_callback;
    p_user_reg.files_dl_end_cb = file_dl_end_callback;
    op_ret = tfm_kepler_gui_files_download_init(&p_user_reg);

    if (op_ret != OPRT_OK) {
        Free(g_p_flies_dl_task_manage);
        g_p_flies_dl_task_manage = NULL;
    }
    return op_ret;
}
