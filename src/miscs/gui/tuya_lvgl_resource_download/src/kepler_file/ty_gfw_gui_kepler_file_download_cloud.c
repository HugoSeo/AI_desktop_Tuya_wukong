/**
 * @file ty_gfw_gui_kepler_file_download_cloud.c
 * @brief tuya files download cloud handle
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

#include <stdio.h>
#include <stdlib.h>
#include "string.h"
#include "tuya_iot_internal_api.h"
#include "ty_cJSON.h"
#include "uni_log.h"
#include "tal_time_service.h"
#include "tal_thread.h"
#include "ty_gfw_gui_kepler_file.h"
#include "ty_gfw_gui_kepler_file_download_cloud.h"
#include "tal_hash.h"
#include "gw_intf.h"
#include "tuya_app_gui_display_text.h"
#include "app_ipc_command.h"
#include "gui_resource_update.h"

/***********************************************************
*************************micro define***********************
***********************************************************/
#define TY_FILES_DL_GET_FILELIST_URL                "thing.rtos.advance.ability.list"    //GUI资源文件信息查询url

#define TY_HTTP_GET_RETRY                   (3)
#define TY_HTTP_GET_TIMEOUT                 (3000)
#define TY_FILES_DL_MAX_NUM                 128

/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef enum {
    TY_FILES_DL_TASK_FLAG_IDLE = 0,
    TY_FILES_DL_TASK_FLAG_ING = 1,
} TY_FILES_DL_TASK_FLAG_E;

//文件执行结果
typedef enum {
    TY_FILES_DL_EXESTAT_INVALID = 1,                    //无新文件下载
    TY_FILES_DL_EXESTAT_VALID = 2,                      //有新文件下载
    TY_FILES_DL_EXESTAT_SUCC = 3,
    TY_FILES_DL_EXESTAT_FAILED = 4,
    TY_FILES_DL_EXESTAT_CANCEL_SUCC = 8,
    TY_FILES_DL_EXESTAT_CANCEL_FAILED = 9,
    TY_FILES_DL_EXESTAT_EXCEPT_END,                         //所有文件下载异常结束
    TY_FILES_DL_EXESTAT_SUCC_END,                           //所有文件下载成功结束
} TY_FILES_DL_EXESTAT_E;

//文件下载任务状态to cloud
typedef enum {
    TY_FILES_DL_TASK_STAT_CLOUD_ING = 2,
    TY_FILES_DL_TASK_STAT_CLOUD_SUCC = 3,
    TY_FILES_DL_TASK_STAT_CLOUD_FAILED = 4,
    TY_FILES_DL_TASK_STAT_CANCEL_SUCC = 8,
    TY_FILES_DL_TASK_STAT_CANCEL_FAILED = 9,
} TY_FILES_DL_TASK_STAT_CLOUD_E;

//workType任务类型
typedef enum
{
    TY_FILES_DL_FILE_DOWNLOAD = 0,
    TY_FILES_DL_FILE_UPLOAD = 1,
    TY_FILES_DL_FILE_DELETE  = 2,
}TY_FILES_DL_WORK_TYPE_E;

typedef struct {
    TY_FILES_DL_USER_REG_S user_cb;
    TY_FILES_DL_TASK_FLAG_E files_dl_flag;       //多文件下载时候进行flag
    TY_FILES_DL_TASK_STAT_CLOUD_E files_dl_stat; // need to cloud
    TY_FILES_DL_EXESTAT_E files_ext_stat;
    TY_FILES_DL_STAT_E files_dl_result; // to uesr
    TY_FILES_DL_UNIT_LEN_E unit_len;
    THREAD_HANDLE files_dl_trd;
    UCHAR_T files_dl_count;
    UCHAR_T files_num;
    DELAYED_WORK_HANDLE p_report_dl_prog_tm_msg;
    DELAYED_WORK_HANDLE p_get_filelist_tm_msg;
    DELAYED_WORK_HANDLE p_files_dl_tm_msg;
    UINT_T dl_prog;
    gui_resource_info_s *curr_res;                      //当前的GUI资源信息
    gui_resource_info_s *dl_res;                        //正在下载的GUI资源信息
    VOID *curr_dl_node;                                 //当前正在下载的文件节点
    BOOL_T curr_dl_is_config;                           //当前正在下载的文件是否为data_file(语言配置文件)
    BOOL_T res_dl_continue;                             //有新资源,是否需要下载?
    IOT_RAW_HTTP_S raw_http;
    SEM_HANDLE wait_sem;
    MUTEX_HANDLE mutex;
    TY_FILES_DL_WAIT_POST_RESULT_E wait_result;
    UCHAR_T files_dl_stop_ctrl;
    BOOL_T stop_type;
    TKL_HASH_HANDLE file_md5_ctx;
    TY_FILE_DL_TRANS_DATA *dl_read_buf;
} TY_FILES_DL_MANAGE_S;

/***********************************************************
***********************function declaration*****************
***********************************************************/
STATIC OPERATE_RET __gui_report_files_dl_stat(VOID);
STATIC OPERATE_RET __gui_report_files_dl_result(IN TY_FILES_DL_EXESTAT_E retCode);
STATIC VOID_T __report_dl_prog_tm_msg_cb(VOID_T *data);
STATIC OPERATE_RET __http_get_filelist(OUT TY_FILES_DL_WORK_TYPE_E *type);
STATIC OPERATE_RET __get_filelist(OUT TY_FILES_DL_WORK_TYPE_E *type);
STATIC VOID_T __get_filelist_inf_tm_msg_cb(VOID_T *data);
STATIC OPERATE_RET __get_files_dl_data_handle(IN UINT_T total_len, IN UINT_T offset, IN CHAR_T *data, IN UINT_T len,
                                              INOUT TY_FILES_DL_PACK_E *p_wait_flag);
STATIC OPERATE_RET __http_files_dl(IN CHAR_T *p_url, IN USHORT_T unit_len, IN UINT_T total_len);
STATIC UCHAR_T __tfm_kepler_files_download_abort_handle(VOID_T);
STATIC VOID_T __files_dl_thread(IN PVOID_T arg);
STATIC VOID_T __files_dl_tm_msg_cb(VOID_T *data);
STATIC VOID_T __files_dl_manage_free(VOID_T);
STATIC UCHAR_T __tfm_kepler_files_download_get_stop_crtl(VOID_T);

/***********************************************************
***********************variable define**********************
***********************************************************/
STATIC TY_FILES_DL_MANAGE_S *p_sg_flies_dl_manage = NULL;
STATIC BOOL_T g_init_is = FALSE;

/***********************************************************
***********************function define**********************
***********************************************************/
STATIC VOID __http_list_printf_completely(CHAR_T *info_str, size_t str_len)
{
#define PRINT_CR_BUFF_SIZE 512
    CHAR_T *sec_buf = NULL;
    UCHAR_T section = (str_len/(PRINT_CR_BUFF_SIZE-1));         //每一段最后一位保留作为结束符

    if ((str_len%(PRINT_CR_BUFF_SIZE-1)) > 0)
        section += 1;

    if (section <= 0)
        return;
    PR_NOTICE("%s,%d: json len '%d', print section '%d' unit mem '%d' \r\n", __func__, __LINE__, str_len, section, PRINT_CR_BUFF_SIZE);
    sec_buf = (CHAR_T *)tal_malloc(PRINT_CR_BUFF_SIZE);
    if (sec_buf) {
        for(UCHAR_T i=0; i<section; i++) {
            memset(sec_buf, 0, PRINT_CR_BUFF_SIZE);
            strncpy(sec_buf, info_str+i*(PRINT_CR_BUFF_SIZE-1), PRINT_CR_BUFF_SIZE-1);
            PR_NOTICE("cr section %d---*%s*", i, sec_buf);
        }
        tal_free(sec_buf);
        sec_buf = NULL;
    }
#undef PRINT_CR_BUFF_SIZE
}

#ifndef GUI_RESOURCE_MANAGMENT_VER_1_0
STATIC GUI_LANGUAGE_E __gui_get_language_type(CHAR_T *lang_str)
{
    GUI_LANGUAGE_E language = GUI_LG_UNKNOWN;

    if (strcasecmp(lang_str, UI_DISPLAY_NAME_CH) == 0) {
        language = GUI_LG_CHINESE;
    }
    else if (strcasecmp(lang_str, UI_DISPLAY_NAME_EN) == 0) {
        language = GUI_LG_ENGLISH;
    }
    else if (strcasecmp(lang_str, UI_DISPLAY_NAME_KR) == 0) {
        language = GUI_LG_KOREAN;
    }
    else if (strcasecmp(lang_str, UI_DISPLAY_NAME_JP) == 0) {
        language = GUI_LG_JAPANESE;
    }
    else if (strcasecmp(lang_str, UI_DISPLAY_NAME_FR) == 0) {
        language = GUI_LG_FRENCH;
    }
    else if (strcasecmp(lang_str, UI_DISPLAY_NAME_DE) == 0) {
        language = GUI_LG_GERMAN;
    }
    else if (strcasecmp(lang_str, UI_DISPLAY_NAME_RU) == 0) {
        language = GUI_LG_RUSSIAN;
    }
    else if (strcasecmp(lang_str, UI_DISPLAY_NAME_IN) == 0) {
        language = GUI_LG_INDIA;
    }
    else {
        PR_ERR("[%s][%d] un-supported language type '%s' ???\n", __FUNCTION__, __LINE__, lang_str);
    }

    return language;
}
#endif

/**
 * @brief: __gui_report_files_dl_stat
 * @desc: Download progress report
 * @param[in] stage:File download stage
 * @param[in] workid:workid
 * @param[in] file_id:file_id
 * @param[in] progress:dl progress
 * @return OPERATE_RET
 * @note none
 */
STATIC OPERATE_RET __gui_report_files_dl_stat(VOID)
{
    OPERATE_RET op_ret = OPRT_OK;
    LV_DISP_EVENT_S Sevt = {0};
    STATIC LV_RESOURCE_DL_EVENT_SS g_res_evt_s = {0};
    //LV_TUYAOS_EVENT_S g_dp_evt_s = {0};

    memset(&g_res_evt_s, 0, SIZEOF(LV_RESOURCE_DL_EVENT_SS));
    g_res_evt_s.files_dl_count = p_sg_flies_dl_manage->files_dl_count + 1;
    g_res_evt_s.files_num = p_sg_flies_dl_manage->files_num;
    g_res_evt_s.curr_dl_is_config = p_sg_flies_dl_manage->curr_dl_is_config;
    g_res_evt_s.curr_dl_node = p_sg_flies_dl_manage->curr_dl_node;
    g_res_evt_s.dl_prog = p_sg_flies_dl_manage->dl_prog;
    //g_dp_evt_s.event_typ = LV_TUYAOS_EVENT_RESOURCE_UPDATE_PROGRESS;
    //g_dp_evt_s.param1 = (UINT_T)&g_res_evt_s;
    //Sevt.event = LLV_EVENT_VENDOR_TUYAOS;
    //Sevt.param = (UINT_T)&g_dp_evt_s;
#if defined(TUYA_IMG_DIRECT_FLUSH) && (TUYA_IMG_DIRECT_FLUSH == 1)
    LV_TUYAOS_EVENT_S g_dp_evt_s = {0};
    Sevt.event = LLV_EVENT_VENDOR_TUYAOS;
    g_dp_evt_s.event_typ = LV_TUYAOS_EVENT_RESOURCE_UPDATE_PROGRESS;
    g_dp_evt_s.param1 = (UINT_T)&g_res_evt_s;
    Sevt.param = (UINT_T)&g_dp_evt_s;
#else
    Sevt.event = LLV_EVENT_CLICKED;
    Sevt.tag = TyResUpdateTag;              //直接发送给对象,事件更新及时!
    Sevt.param = (UINT_T)&g_res_evt_s;
#endif
    tuya_disp_gui_event_send(&Sevt);
    return op_ret;
}

/**
 * @brief: __gui_report_files_dl_result
 * @desc: Report document execution result
 * @param[in] stage:File download stage
 * @param[in] workid:workid
 * @param[in] file_id:file_id
 * @param[in] retCode:result of execution
 * @return OPERATE_RET
 * @note none
 */
STATIC OPERATE_RET __gui_report_files_dl_result(IN TY_FILES_DL_EXESTAT_E retCode)
{
    OPERATE_RET op_ret = OPRT_OK;
    LV_DISP_EVENT_S Sevt = {0};
    LV_TUYAOS_EVENT_S g_dp_evt_s = {0};

    Sevt.event = LLV_EVENT_VENDOR_TUYAOS;
    if (retCode == TY_FILES_DL_EXESTAT_INVALID) {
        g_dp_evt_s.event_typ = LV_TUYAOS_EVENT_NO_NEW_RESOURCE;
        Sevt.param = (UINT_T)&g_dp_evt_s;
        tuya_disp_gui_event_send(&Sevt);
    }
    else if (retCode == TY_FILES_DL_EXESTAT_VALID) {
        //cp1 需要决定要不要开始下载新资源!
        g_dp_evt_s.event_typ = LV_TUYAOS_EVENT_HAS_NEW_RESOURCE;
        Sevt.param = (UINT_T)&g_dp_evt_s;
        tuya_disp_gui_event_send(&Sevt);
    }
    else if ((retCode == TY_FILES_DL_EXESTAT_EXCEPT_END) || (retCode == TY_FILES_DL_EXESTAT_SUCC_END)) {
        g_dp_evt_s.event_typ = LV_TUYAOS_EVENT_RESOURCE_UPDATE_END;
        g_dp_evt_s.param2 = (retCode == TY_FILES_DL_EXESTAT_SUCC_END)?OPRT_OK:1;
        Sevt.param = (UINT_T)&g_dp_evt_s;
        tuya_disp_gui_event_send(&Sevt);
    }

    return op_ret;
}

/**
 * @brief: __report_dl_prog_tm_msg_cb
 * @desc: File download progress is reported
 * @return VOID_T
 * @note none
 */
STATIC VOID_T __report_dl_prog_tm_msg_cb(VOID_T *data)
{
    OPERATE_RET op_ret = OPRT_OK;

    tal_mutex_lock(p_sg_flies_dl_manage->mutex);
    if (g_init_is) {
        op_ret = __gui_report_files_dl_stat();
        if (OPRT_OK != op_ret) {
            PR_ERR("report_files_dl_stat err:%d!", op_ret);
        }
        op_ret = tal_workq_start_delayed(p_sg_flies_dl_manage->p_report_dl_prog_tm_msg, 2000, LOOP_ONCE);
        if (OPRT_OK != op_ret) {
            PR_ERR("cmmod_start_tm_msg err:%d!", op_ret);
        }
    }
    tal_mutex_unlock(p_sg_flies_dl_manage->mutex);
    return;
}

STATIC OPERATE_RET __http_filelist_parse(ty_cJSON *p_result_obj, CONST CHAR_T *item_tag)
{
    OPERATE_RET op_ret = OPRT_OK;
    UCHAR_T num = 0, i = 0;
    UINT_T len = 0;
    ty_cJSON *p_target_item = NULL;
    ty_cJSON *p_file_item = NULL, *p_file_sub_item = NULL;
    gui_res_file_s *file_node = NULL;
    gui_res_file_ss *ffile_node = NULL;

    p_target_item = ty_cJSON_GetObjectItem(p_result_obj, item_tag);
    if (NULL == p_target_item) {
        PR_ERR("%s: item is null", item_tag);
        op_ret = OPRT_NOT_FOUND;
        goto err_out;
    }

    num = ty_cJSON_GetArraySize(p_target_item);
    if (num <= 0) {
        PR_ERR("%s ty_cJSON_GetArraySize:[%d]", item_tag, num);
        if (strcmp(item_tag, "audio") == 0) {           //暂不支持音频!
            goto err_out; 
        }
        op_ret = OPRT_COM_ERROR;
        goto err_out;
    }

    if (num > TY_FILES_DL_MAX_NUM) {
        PR_ERR("%s files num more than %d num:%d!", item_tag, TY_FILES_DL_MAX_NUM, num);
        num = TY_FILES_DL_MAX_NUM;
    }

    if (strcmp(item_tag, "font") == 0)
        ffile_node = &p_sg_flies_dl_manage->dl_res->font;
    else if (strcmp(item_tag, "images") == 0)
        ffile_node = &p_sg_flies_dl_manage->dl_res->picture;
    else if (strcmp(item_tag, "audio") == 0)
        ffile_node = &p_sg_flies_dl_manage->dl_res->music;
    else {
        PR_ERR("err parse item tag '%s'!", item_tag);
        op_ret = OPRT_COM_ERROR;
        goto err_out;
    }

    ffile_node->node = (gui_res_file_s *)Malloc(SIZEOF(gui_res_file_s) * num);
    if (ffile_node->node == NULL) {
        PR_ERR("%s malloc failed!", item_tag);
        op_ret = OPRT_MALLOC_FAILED;
        goto err_out;
    }
    memset(ffile_node->node, 0, SIZEOF(gui_res_file_s) * num);
    ffile_node->node_num = num;
    p_sg_flies_dl_manage->files_num += num;
    PR_DEBUG("%s num: [%d]", item_tag, num);
    for (i = 0; i < num; i++) {
        file_node = &ffile_node->node[i];
        p_file_item = ty_cJSON_GetArrayItem(p_target_item, i);
        if (NULL == p_file_item) {
            PR_ERR("%s ty_cJSON_GetArrayItem[%d]:failed!", item_tag, i);
            op_ret = OPRT_CJSON_PARSE_ERR;
            goto err_out;
        }
        // file name
        p_file_sub_item = ty_cJSON_GetObjectItem(p_file_item, "file");
        if (NULL == p_file_sub_item) {
            PR_ERR("%s ty_cJSON_GetObjectItem file [%d]:failed!", item_tag, i);
            op_ret = OPRT_CJSON_GET_ERR;
            goto err_out;
        }
        len = strlen(p_file_sub_item->valuestring);
        file_node->file_name = (CHAR_T *)Malloc(len + 1);
        if (NULL == file_node->file_name) {
            PR_ERR("%s file_name_malloc failed!", item_tag);
            op_ret = OPRT_MALLOC_FAILED;
            goto err_out;
        }
        memset(file_node->file_name, 0, len + 1);
        strcpy(file_node->file_name, p_file_sub_item->valuestring);
        PR_DEBUG("%s[%d]-->file name:%s", item_tag, i, file_node->file_name);

        // MD5
        p_file_sub_item = ty_cJSON_GetObjectItem(p_file_item, "md5");
        if (NULL == p_file_sub_item) {
            PR_ERR("%s ty_cJSON_GetObjectItem md5 [%d]:failed!", item_tag, i);
            op_ret = OPRT_CJSON_GET_ERR;
            goto err_out;
        }
        len = strlen(p_file_sub_item->valuestring);
        file_node->md5 = (CHAR_T *)Malloc(len + 1);
        if (NULL == file_node->md5) {
            PR_ERR("%s file_name_malloc failed!",item_tag);
            op_ret = OPRT_MALLOC_FAILED;
            goto err_out;
        }
        memset(file_node->md5, 0, len + 1);
        strcpy(file_node->md5, p_file_sub_item->valuestring);
        PR_DEBUG("%s[%d]-->md5:%s", item_tag, i, file_node->md5);

        // url
        p_file_sub_item = ty_cJSON_GetObjectItem(p_file_item, "url");
        if (NULL == p_file_sub_item) {
            PR_ERR("%s ty_cJSON_GetObjectItem url [%d]:failed!", item_tag, i);
            op_ret = OPRT_CJSON_GET_ERR;
            goto err_out;
        }
        len = strlen(p_file_sub_item->valuestring);
        file_node->url = (CHAR_T *)Malloc(len + 1);
        if (NULL == file_node->url) {
            PR_ERR("%s file_name_malloc failed!",item_tag);
            op_ret = OPRT_MALLOC_FAILED;
            goto err_out;
        }
        memset(file_node->url, 0, len + 1);
        strcpy(file_node->url, p_file_sub_item->valuestring);
        PR_DEBUG("%s[%d]-->url:%s", item_tag, i, file_node->url);

        // id
        p_file_sub_item = ty_cJSON_GetObjectItem(p_file_item, "id");
        if (NULL == p_file_sub_item) {
            PR_ERR("%s ty_cJSON_GetObjectItem id [%d]:failed!", item_tag, i);
            op_ret = OPRT_CJSON_GET_ERR;
            goto err_out;
        }
        file_node->id = p_file_sub_item->valueint;
        PR_DEBUG("%s[%d]-->id:%d", item_tag, i, file_node->id);

        //size
        p_file_sub_item = ty_cJSON_GetObjectItem(p_file_item, "size");
        if (NULL == p_file_sub_item) {
            PR_ERR("%s ty_cJSON_GetObjectItem size [%d]:failed!", item_tag, i);
            op_ret = OPRT_CJSON_GET_ERR;
            goto err_out;
        }
        file_node->size = p_file_sub_item->valueint;
        PR_DEBUG("%s[%d]-->size:%d", item_tag, i, file_node->size);

        if (strcmp(item_tag, "font") == 0)
            file_node->type = T_GUI_RES_FONT;
        else if (strcmp(item_tag, "images") == 0)
            file_node->type = T_GUI_RES_PICTURE;
        else if (strcmp(item_tag, "audio") == 0)
            file_node->type = T_GUI_RES_MUSIC;
    }

    err_out:
    return op_ret;
}

/**
 * @brief: __http_get_filelist
 * @desc: Get the file list information and parse it
 * @param[in] workid:Gets the file list input parameters
 * @return OPERATE_RET
 * @note none
 */
STATIC OPERATE_RET __http_get_filelist(OUT TY_FILES_DL_WORK_TYPE_E *type)
{
    OPERATE_RET op_ret = OPRT_OK;
    UCHAR_T num = 0, i = 0;
    ty_cJSON *p_root = NULL;
    ty_cJSON *p_result_obj = NULL;
    ty_cJSON *p_id_item = NULL, *p_word_item = NULL;
    ty_cJSON *p_file_item = NULL, *p_file_sub_item = NULL;
    gui_res_lang_s *lang_node = NULL;
    CHAR_T *p_root_dbg = NULL;
    CHAR_T *p_out = NULL;
    UINT_T len = 0;
    GW_CNTL_S *gw_cntl = get_gw_cntl();

    p_root = ty_cJSON_CreateObject();
    if (NULL == p_root) {
        PR_ERR("p_root is null!");
        return OPRT_CR_CJSON_ERR;
    }

    ty_cJSON_AddStringToObject(p_root, "devId", gw_cntl->gw_if.id);
    if (TUYA_SECURITY_LEVEL == TUYA_SL_0) {
        // 加psk代理
        ty_cJSON_AddNumberToObject(p_root, "psk", 1);
    } else {
        // 不加psk代理
        ty_cJSON_AddNumberToObject(p_root, "psk", 0);
    }
    p_out = ty_cJSON_PrintUnformatted(p_root);
    ty_cJSON_Delete(p_root);
    if (NULL == p_out) {
        PR_ERR("ty_cJSON_PrintUnformatted failed");
        return OPRT_COM_ERROR;
    }

    PR_DEBUG("iot_httpc_common_post out: %s", p_out);
    op_ret = iot_httpc_common_post(TY_FILES_DL_GET_FILELIST_URL, "1.0", NULL, gw_cntl->gw_if.id, p_out, NULL, &p_result_obj);
    Free(p_out), p_out = NULL;
    if (OPRT_OK != op_ret) {
        return op_ret;
    }

    if (NULL == p_result_obj) {
        PR_ERR("result is null!");
        return OPRT_COM_ERROR;
    }

    p_root_dbg = ty_cJSON_PrintUnformatted(p_result_obj);
    if (NULL == p_root_dbg) {
        PR_ERR("ty_cJSON_Print failed");
        return OPRT_COM_ERROR;
    }
    __http_list_printf_completely(p_root_dbg, strlen(p_root_dbg));
    Free(p_root_dbg), p_root_dbg = NULL;

    *type = TY_FILES_DL_FILE_DOWNLOAD;
    //构建准备下载的资源数据结构
    p_sg_flies_dl_manage->dl_res = (gui_resource_info_s *)Malloc(SIZEOF(gui_resource_info_s));
    if (NULL == p_sg_flies_dl_manage->dl_res) {
        PR_ERR("%s:%d:dl_res malloc fail", __func__,__LINE__);
        op_ret = OPRT_MALLOC_FAILED;
        goto EXIT;
    }
    memset(p_sg_flies_dl_manage->dl_res, 0, SIZEOF(gui_resource_info_s));

    //1: id
    p_id_item = ty_cJSON_GetObjectItem(p_result_obj, "id");                     //必须!
    if (NULL == p_id_item) {
        PR_ERR("id: item is null");
        op_ret = OPRT_CJSON_GET_ERR;
        goto EXIT;
    }
    len = strlen(p_id_item->valuestring);
    p_sg_flies_dl_manage->dl_res->version.ver = (CHAR_T *)Malloc(len + 1);
    if (NULL == p_sg_flies_dl_manage->dl_res->version.ver) {
        PR_ERR("id malloc failed!");
        op_ret = OPRT_MALLOC_FAILED;
        goto EXIT;
    }
    memset(p_sg_flies_dl_manage->dl_res->version.ver, 0, len + 1);
    strcpy(p_sg_flies_dl_manage->dl_res->version.ver, p_id_item->valuestring);
    p_sg_flies_dl_manage->dl_res->version.file_name = NULL;         //暂时用默认名称:version.inf
    //PR_NOTICE("gui new resource id '%s'\r\n",p_sg_flies_dl_manage->dl_res->version.ver);

    // 2.word:语言配置文本
    p_word_item = ty_cJSON_GetObjectItem(p_result_obj, "word");                 //可选!
    if (NULL == p_word_item) {
        PR_ERR("word: item is null");               //允许没有word!
        p_sg_flies_dl_manage->dl_res->data_file.node_num = 0;
        //op_ret = OPRT_CJSON_GET_ERR;
        //goto EXIT;
    }
    else {
    #ifdef GUI_RESOURCE_MANAGMENT_VER_1_0
        num = 1;
    #else
        num = ty_cJSON_GetArraySize(p_word_item);
    #endif
        if (num <= 0) {
            PR_ERR("ty_cJSON_GetArraySize:[%d]", num);
            op_ret = OPRT_COM_ERROR;
            goto EXIT;
        }

        if (num > TY_FILES_DL_MAX_NUM) {
            PR_ERR("word files num more than %d num:%d!", TY_FILES_DL_MAX_NUM, num);
            num = TY_FILES_DL_MAX_NUM;
        }
        p_sg_flies_dl_manage->dl_res->data_file.node = (gui_res_lang_s *)Malloc(SIZEOF(gui_res_lang_s) * num);
        if (p_sg_flies_dl_manage->dl_res->data_file.node == NULL) {
            PR_ERR("data file malloc failed!");
            op_ret = OPRT_MALLOC_FAILED;
            goto EXIT;
        }
        memset(p_sg_flies_dl_manage->dl_res->data_file.node, 0, SIZEOF(gui_res_lang_s) * num);
        p_sg_flies_dl_manage->dl_res->data_file.node_num = num;
        p_sg_flies_dl_manage->files_num += num;
        PR_DEBUG("word(lang-config) num: [%d]", num);
        for (i = 0; i < num; i++) {
            lang_node = &p_sg_flies_dl_manage->dl_res->data_file.node[i];
        #ifdef GUI_RESOURCE_MANAGMENT_VER_1_0
            p_file_item = p_word_item;
        #else
            p_file_item = ty_cJSON_GetArrayItem(p_word_item, i);
        #endif
            if (NULL == p_file_item) {
                PR_ERR("ty_cJSON_GetArrayItem[%d]:failed!", i);
                op_ret = OPRT_CJSON_PARSE_ERR;
                goto EXIT;
            }
            // file name
            p_file_sub_item = ty_cJSON_GetObjectItem(p_file_item, "file");
            if (NULL == p_file_sub_item) {
                PR_ERR("ty_cJSON_GetObjectItem url[%d]:failed!", i);
                op_ret = OPRT_CJSON_GET_ERR;
                goto EXIT;
            }
            len = strlen(p_file_sub_item->valuestring);
            lang_node->file_name = (CHAR_T *)Malloc(len + 1);
            if (NULL == lang_node->file_name) {
                PR_ERR("file_name_malloc failed!");
                op_ret = OPRT_MALLOC_FAILED;
                goto EXIT;
            }
            memset(lang_node->file_name, 0, len + 1);
            strcpy(lang_node->file_name, p_file_sub_item->valuestring);
            PR_DEBUG("word[%d]-->file name:%s", i, lang_node->file_name);

        #ifdef GUI_RESOURCE_MANAGMENT_VER_1_0
            lang_node->language = GUI_LG_ALL;
        #else
            //lang
            p_file_sub_item = ty_cJSON_GetObjectItem(p_file_item,"lang");
            if (NULL == p_file_sub_item) {
                PR_ERR("ty_cJSON_GetObjectItem url[%d]:failed!", i);
                op_ret = OPRT_CJSON_GET_ERR;
                goto EXIT;
            }
            lang_node->language = __gui_get_language_type(p_file_sub_item->valuestring);
        #endif
            PR_DEBUG("word[%d]-->lang:%d", i, lang_node->language);

            // MD5
            p_file_sub_item = ty_cJSON_GetObjectItem(p_file_item, "md5");
            if (NULL == p_file_sub_item) {
                PR_ERR("ty_cJSON_GetObjectItem url[%d]:failed!", i);
                op_ret = OPRT_CJSON_GET_ERR;
                goto EXIT;
            }
            len = strlen(p_file_sub_item->valuestring);
            lang_node->md5 = (CHAR_T *)Malloc(len + 1);
            if (NULL == lang_node->md5) {
                PR_ERR("file_name_malloc failed!");
                op_ret = OPRT_MALLOC_FAILED;
                goto EXIT;
            }
            memset(lang_node->md5, 0, len + 1);
            strcpy(lang_node->md5, p_file_sub_item->valuestring);
            PR_DEBUG("word[%d]-->md5:%s", i, lang_node->md5);

            // url
            p_file_sub_item = ty_cJSON_GetObjectItem(p_file_item, "url");
            if (NULL == p_file_sub_item) {
                PR_ERR("ty_cJSON_GetObjectItem url[%d]:failed!", i);
                op_ret = OPRT_CJSON_GET_ERR;
                goto EXIT;
            }
            len = strlen(p_file_sub_item->valuestring);
            lang_node->url = (CHAR_T *)Malloc(len + 1);
            if (NULL == lang_node->url) {
                PR_ERR("file_name_malloc failed!");
                op_ret = OPRT_MALLOC_FAILED;
                goto EXIT;
            }
            memset(lang_node->url, 0, len + 1);
            strcpy(lang_node->url, p_file_sub_item->valuestring);
            PR_DEBUG("word[%d]-->url:%s", i, lang_node->url);

            //size
            p_file_sub_item = ty_cJSON_GetObjectItem(p_file_item, "size");
            if (NULL == p_file_sub_item) {
                PR_ERR("ty_cJSON_GetObjectItem size[%d]:failed!", i);
                op_ret = OPRT_CJSON_GET_ERR;
                goto EXIT;
            }
            lang_node->size = p_file_sub_item->valueint;
            PR_DEBUG("word[%d]-->size:%d", i, lang_node->size);

            //id:忽略
        }
    }

    // 3.font字体文件信息
    op_ret = __http_filelist_parse(p_result_obj, "font");                               //可选!
    if (op_ret != OPRT_OK) {
        if (op_ret == OPRT_NOT_FOUND) {
            if (p_sg_flies_dl_manage->dl_res->data_file.node_num > 0) {
                PR_ERR("word exist, but font is null !");
                op_ret = OPRT_INVALID_PARM;
                goto EXIT;
            }
            else {
                p_sg_flies_dl_manage->dl_res->font.node_num = 0;
                op_ret = OPRT_OK;                                   //允许没有font!
            }
        }
        else {
            goto EXIT;
        }
    }
    else {
        if (p_sg_flies_dl_manage->dl_res->data_file.node_num == 0) {
            PR_ERR("font exist, but word is null !");
            op_ret = OPRT_INVALID_PARM;
            goto EXIT;
        }
    }

    // 3.images图像文件信息
    op_ret = __http_filelist_parse(p_result_obj, "images");                             //可选!
    if (op_ret != OPRT_OK) {
        if (op_ret == OPRT_NOT_FOUND) {
            p_sg_flies_dl_manage->dl_res->picture.node_num = 0;
            op_ret = OPRT_OK;                                   //允许没有image!
        }
        else {
            goto EXIT;
        }
    }

    // 3.audio音频文件信息:可选
    op_ret = __http_filelist_parse(p_result_obj, "audio");                              //可选!
    if (op_ret != OPRT_OK) {
        if (op_ret == OPRT_NOT_FOUND) {
            p_sg_flies_dl_manage->dl_res->music.node_num = 0;
            op_ret = OPRT_OK;                                   //允许没有audio!
        }
        else {
            goto EXIT;
        }
    }

EXIT:
    if (p_result_obj) {
        ty_cJSON_Delete(p_result_obj);
    }
    return op_ret;
}

/**
 * @brief: __get_filelist
 * @desc: Gets the file list information
 * @return OPERATE_RET
 * @note none
 */
STATIC OPERATE_RET __get_filelist(OUT TY_FILES_DL_WORK_TYPE_E *type)
{
    OPERATE_RET op_ret = OPRT_OK;

    // get url
    op_ret = __http_get_filelist(type);
    if (OPRT_OK != op_ret) {
        PR_ERR("__http_get_filelist_inf err:%d!", op_ret);
        return op_ret;
    }
    PR_DEBUG("get_file_num:%d", p_sg_flies_dl_manage->files_num);
    return op_ret;
}

/**
 * @brief: __get_filelist_inf_tm_msg_cb
 * @desc: Get file list information processing
 * @return VOID_T
 * @note none
 */
STATIC VOID_T __get_filelist_inf_tm_msg_cb(VOID_T *data)
{
    OPERATE_RET op_ret = OPRT_OK;
    TY_FILES_DL_PACK_E wait_flag = TY_FILES_DL_PACK_WAIT;
    TY_FILES_DL_UNIT_LEN_E unit_len = 0;
    TY_FILES_DL_EXESTAT_E retCode = TY_FILES_DL_EXESTAT_EXCEPT_END;

    p_sg_flies_dl_manage->curr_res = tuya_gui_resource_statistics();
    if (p_sg_flies_dl_manage->curr_res == NULL) {
        PR_ERR("current resource not exist ?");
        //p_sg_flies_dl_manage->files_dl_result = TY_FILES_DL_STAT_GET_FILELIST_FAILED;
        //goto ERR_EXIT;
    }
    else {
        tuya_gui_resource_show(p_sg_flies_dl_manage->curr_res);
    }

    p_sg_flies_dl_manage->files_dl_stat = TY_FILES_DL_TASK_STAT_CLOUD_FAILED;
    TY_FILES_DL_WORK_TYPE_E type = TY_FILES_DL_FILE_DOWNLOAD;
    op_ret = __get_filelist(&type); // get file list
    if(OPRT_OK != op_ret){
        PR_ERR("_get_file_list failed :%d!", op_ret);
        p_sg_flies_dl_manage->files_dl_result = TY_FILES_DL_STAT_GET_FILELIST_FAILED;
        goto ERR_EXIT;
    }

    if ((p_sg_flies_dl_manage->curr_res == NULL) || (p_sg_flies_dl_manage->curr_res != NULL && p_sg_flies_dl_manage->curr_res->version.ver == NULL)) {
        PR_NOTICE("old resource is empty, continue going absoutely !!!\r\n");
    }
    else {
        PR_NOTICE("old resource id '%s', new resource id '%s' \r\n", \
            p_sg_flies_dl_manage->curr_res->version.ver, p_sg_flies_dl_manage->dl_res->version.ver);
        if (strcmp(p_sg_flies_dl_manage->curr_res->version.ver, p_sg_flies_dl_manage->dl_res->version.ver) == 0) {  //同样的资源ID
            PR_NOTICE("No newer resource needs to update!");
            p_sg_flies_dl_manage->files_dl_result = TY_FILES_DL_STAT_USER_CANCEL;
            retCode = TY_FILES_DL_EXESTAT_INVALID;              //通知GUI,无新文件下载!
            goto ERR_EXIT;
        }
    }

    //阻塞等待确认是否下载
    op_ret = p_sg_flies_dl_manage->user_cb.files_dl_start_cb(p_sg_flies_dl_manage->dl_res, &wait_flag, &unit_len);
    if (OPRT_OK != op_ret) {
        PR_ERR("files_dl_start_cb return :%d!", op_ret);
        p_sg_flies_dl_manage->files_dl_result = TY_FILES_DL_STAT_USER_CANCEL;
        goto ERR_EXIT;
    }

    p_sg_flies_dl_manage->unit_len = unit_len;
    __gui_report_files_dl_result(TY_FILES_DL_EXESTAT_VALID);                    //通知GUI,有新文件下载!
    if (TY_FILES_DL_PACK_WAIT == wait_flag) {
        tal_semaphore_wait(p_sg_flies_dl_manage->wait_sem, SEM_WAIT_FOREVER);
    }

    if (!p_sg_flies_dl_manage->res_dl_continue) {
        PR_WARN("user refuse to update new resource!");
        p_sg_flies_dl_manage->files_dl_result = TY_FILES_DL_STAT_USER_CANCEL;
        goto ERR_EXIT;
    }

    if (TY_FILES_DL_WAIT_POST_FAILED == p_sg_flies_dl_manage->wait_result) {
        PR_ERR("wait_result is failed!");
        p_sg_flies_dl_manage->files_dl_result = TY_FILES_DL_STAT_USER_CANCEL;
        goto ERR_EXIT;
    }

    //开启线程准备文件下载
    op_ret = tal_workq_start_delayed(p_sg_flies_dl_manage->p_files_dl_tm_msg, 200, LOOP_ONCE); // start tm msg
    if (OPRT_OK != op_ret) {
        PR_ERR("tal_workq_start_delayed failed! op_ret:%d", op_ret);
        p_sg_flies_dl_manage->files_dl_result = TY_FILES_DL_STAT_INTERNAL_ERR;
        goto ERR_EXIT;
    }

    p_sg_flies_dl_manage->files_dl_stat = TY_FILES_DL_TASK_STAT_CLOUD_ING;
    return;

ERR_EXIT:
    p_sg_flies_dl_manage->files_dl_flag = TY_FILES_DL_TASK_FLAG_IDLE;
    //错误返回给应用层
    p_sg_flies_dl_manage->user_cb.files_dl_end_cb(TY_FILES_DL_RESULT_FAILED, p_sg_flies_dl_manage->files_dl_result);
    __gui_report_files_dl_result(retCode);
    __files_dl_manage_free();
    return;
}

/**
 * @brief: check file sign same or not
 * @desc:
 * @return TRUE/FALSE
 * @note none
 */
STATIC BOOL_T __files_dl_sign_check(IN UINT_T total_len, IN UINT_T offset, IN CHAR_T *data, IN UINT_T len)
{
    UCHAR_T md5[16] = {0};
    CHAR_T local_sign[32 + 1] = {0};
    INT_T s_offset = 0, idx = 0;
    gui_res_lang_s *config_node = NULL;
    gui_res_file_s *file_node = NULL;
    CHAR_T *cloud_sign = NULL;

    if (p_sg_flies_dl_manage->curr_dl_is_config) {
        config_node = (gui_res_lang_s *)p_sg_flies_dl_manage->curr_dl_node;
        cloud_sign = config_node->md5;
    }
    else {
        file_node = (gui_res_file_s *)p_sg_flies_dl_manage->curr_dl_node;
        cloud_sign = file_node->md5;
    }
    if (cloud_sign) {
        if (NULL == p_sg_flies_dl_manage->file_md5_ctx) {
            tal_md5_create_init(&p_sg_flies_dl_manage->file_md5_ctx);
            tal_md5_starts_ret(p_sg_flies_dl_manage->file_md5_ctx);
        }
        tal_md5_update_ret(p_sg_flies_dl_manage->file_md5_ctx, (UCHAR_T *)data, len);
        if (offset + len >= total_len) {
            tal_md5_finish_ret(p_sg_flies_dl_manage->file_md5_ctx, md5);
            for (idx = 0; idx < 16; idx++) {
                sprintf(&local_sign[s_offset], "%02x", md5[idx]);
                s_offset += 2;
            }
            if (memcmp(local_sign, cloud_sign, 32)) {
                PR_ERR("file sign not match,cloud:%s,local:%s", cloud_sign, local_sign);
                return FALSE;
            }
        }
    }

    return TRUE;
}

/**
 * @brief: _get_files_dl_data_handle
 * @desc: File pull data processing
 * @param[in] total_len:Total file length
 * @param[in] offset:File offset
 * @param[in] data:File data
 * @param[in] len:File data length
 * @param[in] wait_flag:Waiting for the sign
 * @return OPERATE_RET
 * @note none
 */
STATIC OPERATE_RET __get_files_dl_data_handle(IN UINT_T total_len, IN UINT_T offset, IN CHAR_T *data, IN UINT_T len,
                                              INOUT TY_FILES_DL_PACK_E *p_wait_flag)
{
    OPERATE_RET op_ret = OPRT_OK;
    STATIC UINT_T prePosix = 0;
    STATIC UINT_T prePercent = 0;

    if (!__files_dl_sign_check(total_len, offset, data, len)) {
        return OPRT_COM_ERROR;
    }

    if (total_len < (0xffffffff / 100)) {
        p_sg_flies_dl_manage->dl_prog = ((offset * 100) / (total_len + 1));
    } else {
        p_sg_flies_dl_manage->dl_prog = ((offset / 100) / (total_len / 10000 + 1));
    }

    UINT_T curPosix = tal_time_get_posix();
    if ((curPosix - prePosix >= 5) || (p_sg_flies_dl_manage->dl_prog - prePercent >= 30) ||
        ((p_sg_flies_dl_manage->dl_prog >= 98) && (curPosix - prePosix >= 1))) {
        prePosix = curPosix;
        prePercent = p_sg_flies_dl_manage->dl_prog;
        PR_NOTICE("percent %d offset:%u total:%u ", p_sg_flies_dl_manage->dl_prog, offset, total_len);
        __report_dl_prog_tm_msg_cb(p_sg_flies_dl_manage->p_report_dl_prog_tm_msg);
    }

    PR_DEBUG(" total_len:%d,offset:%d,datalen:%d", total_len, offset, len);
    op_ret = p_sg_flies_dl_manage->user_cb.files_dl_trans_cb(p_sg_flies_dl_manage->curr_dl_is_config, p_sg_flies_dl_manage->curr_dl_node,    \
                                                            p_sg_flies_dl_manage->files_dl_count + 1, data, len,    \
                                                             offset, p_wait_flag);
    PR_DEBUG("__get_download_file_data_cb  op_ret:%d", op_ret);
    return op_ret;
}

/**
 * @brief: __http_files_dl
 * @desc: Multiple files are downloaded and transferred
 * @param[in] url:File pull request parameters
 * @param[in] unit_len:Length of data in a single pull
 * @param[in] total_len:Total file length
 * @return VOID_T
 * @note none
 */
STATIC OPERATE_RET __http_files_dl(IN CHAR_T *p_url, IN USHORT_T unit_len, IN UINT_T total_len)
{
    OPERATE_RET op_ret = OPRT_OK;
    OPERATE_RET op_ret2 = OPRT_OK;
    UINT_T offset = 0;
    INT_T status_code = 0;
    BOOL_T chunked = FALSE;
    UINT_T content_len = 0;
    UCHAR_T retry_count = 0, i = 0, stop_ctrl = 0;

    //tuya_tls_set_ssl_verify(0);
    for (i = 0; i < TY_HTTP_GET_RETRY; i++) {
        op_ret = iot_httpc_raw_get(p_url, &(p_sg_flies_dl_manage->raw_http), offset, total_len);
        if (OPRT_OK != op_ret) {
            PR_ERR("iot_httpc_raw_get err:%d retry:%d", op_ret, i);
        } else {
            break;
        }
        tal_system_sleep(TY_HTTP_GET_TIMEOUT);
    }
    if (i >= TY_HTTP_GET_RETRY) {
        goto ERR_EXIT;
    }
    for (i = 0; i < TY_HTTP_GET_RETRY; i++) {
        op_ret = iot_httpc_raw_read_resp_header(p_sg_flies_dl_manage->raw_http, &status_code, &chunked, &content_len);
        if (OPRT_OK != op_ret) {
            PR_ERR("iot_httpc_raw_read_resp_header err:%d retry:%d", op_ret, i);
        } else {
            break;
        }
        tal_system_sleep(TY_HTTP_GET_TIMEOUT);
    }
    if (i >= TY_HTTP_GET_RETRY) {
        goto ERR_EXIT;
    }
    if (((status_code != 200) && (status_code != 206)) || (0 == content_len && !chunked)) {
        PR_ERR("content_len:%d chunked:%d status_code:%d Invalid!!!\n", content_len, chunked, status_code);
        op_ret = OPRT_COM_ERROR;
        goto ERR_EXIT;
    }

    UINT_T sum_read_len = 0;
    INT_T read_len = 0;
    UINT_T have_read_len = 0; //本次累计已读取的长度
    TY_FILES_DL_PACK_E wait_flag = TY_FILES_DL_PACK_WAIT;
    p_sg_flies_dl_manage->dl_read_buf = Malloc(sizeof(TY_FILE_DL_TRANS_DATA) + unit_len);
    if (NULL == p_sg_flies_dl_manage->dl_read_buf) {
        op_ret = OPRT_MALLOC_FAILED;
        PR_ERR("malloc failed!");
        goto ERR_EXIT;
    }
    memset(p_sg_flies_dl_manage->dl_read_buf, 0, sizeof(TY_FILE_DL_TRANS_DATA) + unit_len);
    CHAR_T *read_buff = p_sg_flies_dl_manage->dl_read_buf->data;
    while (1) {
        read_len = iot_httpc_raw_read_content(p_sg_flies_dl_manage->raw_http, (BYTE_T *)&read_buff[have_read_len],
                                              unit_len - have_read_len);
        if (read_len <= 0) {
            retry_count++;
            PR_ERR("iot_httpc_raw_read_content err retry:%d!", retry_count);
            tal_system_sleep(TY_HTTP_GET_TIMEOUT);
            if (retry_count < TY_HTTP_GET_RETRY) {
                continue;
            }
            op_ret = OPRT_COM_ERROR;
            goto ERR_EXIT;
        }
        retry_count = 0;
        have_read_len += read_len;
        if (sum_read_len + have_read_len >= total_len) { // the last
            op_ret = __get_files_dl_data_handle(total_len, sum_read_len, read_buff, have_read_len, &wait_flag);
            if (OPRT_OK != op_ret) {
                goto ERR_EXIT;
            }
            if (TY_FILES_DL_PACK_WAIT == wait_flag) {
                tal_semaphore_wait(p_sg_flies_dl_manage->wait_sem, SEM_WAIT_FOREVER);
            }
            if (TY_FILES_DL_WAIT_POST_FAILED == p_sg_flies_dl_manage->wait_result) {
                goto ERR_EXIT;
            }
            PR_DEBUG("http get success! sum_read_len %d", sum_read_len + have_read_len);
            break; //单包传输成功
        } else {
            if (have_read_len < unit_len) {
                continue;
            }
        }
        //已读满unit_len
        op_ret = __get_files_dl_data_handle(total_len, sum_read_len, read_buff, unit_len, &wait_flag);
        if (OPRT_OK != op_ret) {
            goto ERR_EXIT;
        }
        if (TY_FILES_DL_PACK_WAIT == wait_flag) {
            tal_semaphore_wait(p_sg_flies_dl_manage->wait_sem, SEM_WAIT_FOREVER);
        }
        if (TY_FILES_DL_WAIT_POST_FAILED == p_sg_flies_dl_manage->wait_result) {
            goto ERR_EXIT;
        }
        sum_read_len += have_read_len;
        have_read_len = 0;
        //中止判断
        stop_ctrl = __tfm_kepler_files_download_get_stop_crtl();
        if (stop_ctrl != 0) {
            PR_NOTICE("goto cencel file");
            op_ret = OPRT_OK;
            goto ERR_EXIT;
        }
    }
    PR_DEBUG("http open success! content_len:%d chunked:%d status_code:%d", content_len, chunked, status_code);
    Free(p_sg_flies_dl_manage->dl_read_buf);
    p_sg_flies_dl_manage->dl_read_buf = NULL;

    op_ret2 = iot_httpc_raw_close(p_sg_flies_dl_manage->raw_http);
    if (op_ret2 != OPRT_OK) {
        PR_ERR("iot_httpc_raw_close err:%d!", op_ret2);
    }
    p_sg_flies_dl_manage->raw_http = NULL;
    if (p_sg_flies_dl_manage->file_md5_ctx) {
        tal_md5_free(p_sg_flies_dl_manage->file_md5_ctx);
        p_sg_flies_dl_manage->file_md5_ctx = NULL;
    }
    return op_ret;

ERR_EXIT:
    op_ret2 = iot_httpc_raw_close(p_sg_flies_dl_manage->raw_http);
    if (op_ret2 != OPRT_OK) {
        PR_ERR("iot_httpc_raw_close err:%d!", op_ret2);
    }
    if (p_sg_flies_dl_manage->dl_read_buf) {
        Free(p_sg_flies_dl_manage->dl_read_buf);
        p_sg_flies_dl_manage->dl_read_buf = NULL;
    }
    p_sg_flies_dl_manage->raw_http = NULL;
    if (p_sg_flies_dl_manage->file_md5_ctx) {
        tal_md5_free(p_sg_flies_dl_manage->file_md5_ctx);
        p_sg_flies_dl_manage->file_md5_ctx = NULL;
    }
    PR_NOTICE("goto cencel file stop_ctrl:%d", stop_ctrl);
    if (stop_ctrl != 0) {
        p_sg_flies_dl_manage->files_ext_stat = TY_FILES_DL_TASK_STAT_CANCEL_SUCC;
    } else {
        p_sg_flies_dl_manage->files_ext_stat = TY_FILES_DL_EXESTAT_FAILED;
    }
    p_sg_flies_dl_manage->files_dl_flag = TY_FILES_DL_TASK_FLAG_IDLE;
    return op_ret;
}

STATIC UCHAR_T __tfm_kepler_files_download_abort_handle(VOID_T)
{
    UCHAR_T stop_ctrl = 0;
    //中止处理
    stop_ctrl = __tfm_kepler_files_download_get_stop_crtl();
    if (TY_FILES_DL_STOP_CTRL_ABORT_ALL == stop_ctrl) {
        p_sg_flies_dl_manage->files_dl_count = p_sg_flies_dl_manage->files_num;
        p_sg_flies_dl_manage->files_dl_stop_ctrl = 0;
        return TRUE;
    } else if (TY_FILES_DL_STOP_CTRL_ABORT_CURRENT == stop_ctrl) {
        ((p_sg_flies_dl_manage->files_dl_count + 1) >= (p_sg_flies_dl_manage->files_num))
        ? (p_sg_flies_dl_manage->files_dl_count = p_sg_flies_dl_manage->files_num)
        : (p_sg_flies_dl_manage->files_dl_count++);
        p_sg_flies_dl_manage->files_dl_stop_ctrl = 0;
        return TRUE;
    }
    return FALSE;
}

STATIC VOID __http_files_dl_get(VOID)
{
    gui_resource_info_s *dl_res = p_sg_flies_dl_manage->dl_res;
    gui_res_file_ss *file_s = NULL;
    UINT_T i= 0;

    //1.遍历data_file(语言配置文件)
    if (dl_res->data_file.node_num > 0) {
        for (i=0; i<dl_res->data_file.node_num; i++) {
            if (dl_res->data_file.node[i].dl_finish != TRUE) {
                p_sg_flies_dl_manage->curr_dl_node = (VOID *)&dl_res->data_file.node[i];
                p_sg_flies_dl_manage->curr_dl_is_config = TRUE;
                return;
            }
        }
    }

    //2.遍历font
    file_s = &dl_res->font;
    if (file_s->node_num > 0) {
        for (i=0; i<file_s->node_num; i++) {
            if (file_s->node[i].dl_finish != TRUE) {
                p_sg_flies_dl_manage->curr_dl_node = (VOID *)&file_s->node[i];
                return;
            }
        }
    }

    //3.遍历music
    file_s = &dl_res->music;
    if (file_s->node_num > 0) {
        for (i=0; i<file_s->node_num; i++) {
            if (file_s->node[i].dl_finish != TRUE) {
                p_sg_flies_dl_manage->curr_dl_node = (VOID *)&file_s->node[i];
                return;
            }
        }
    }

    //4.遍历picture
    file_s = &dl_res->picture;
    if (file_s->node_num > 0) {
        for (i=0; i<file_s->node_num; i++) {
            if (file_s->node[i].dl_finish != TRUE) {
                p_sg_flies_dl_manage->curr_dl_node = (VOID *)&file_s->node[i];
                return;
            }
        }
    }
}

/**
 * @brief: __files_dl_thread
 * @desc: Perform multiple file download tasks
 * @return VOID_T
 * @note none
 */
STATIC VOID_T __files_dl_thread(IN PVOID_T arg)
{
    OPERATE_RET op_ret = OPRT_OK;
    gui_res_lang_s *config_node = NULL;
    gui_res_file_s *file_node = NULL;
    CHAR_T *p_url = NULL;
    UINT_T file_sz = 0;

    p_sg_flies_dl_manage->dl_prog = 0;
    tal_workq_start_delayed(p_sg_flies_dl_manage->p_report_dl_prog_tm_msg, 2000, LOOP_ONCE);
    PR_NOTICE("tal_workq_start_delayed...");

    p_sg_flies_dl_manage->curr_dl_is_config = FALSE;
    p_sg_flies_dl_manage->curr_dl_node = NULL;
    __http_files_dl_get();
    if (p_sg_flies_dl_manage->curr_dl_node == NULL) {
        op_ret = OPRT_INVALID_PARM;
        PR_ERR("%s:%d========================no found valid dl file node ???", __func__, __LINE__);
        p_sg_flies_dl_manage->user_cb.files_dl_end_cb(TY_FILES_DL_RESULT_SUCC, TY_FILES_DL_STAT_SUCC);
        __gui_report_files_dl_result(TY_FILES_DL_EXESTAT_EXCEPT_END);
        goto _dl_exit;
    }
    if (p_sg_flies_dl_manage->curr_dl_is_config) {
        config_node = (gui_res_lang_s *)p_sg_flies_dl_manage->curr_dl_node;
        p_url = config_node->url;
        file_sz = config_node->size;
    }
    else {
        file_node = (gui_res_file_s *)p_sg_flies_dl_manage->curr_dl_node;
        p_url = file_node->url;
        file_sz = file_node->size;
    }

    if (TUYA_SECURITY_LEVEL == TUYA_SL_0) {
        // 加psk代理
    }
    else {
        // 不加psk代理
        CHAR_T *psk_ptr = strstr(p_url, ":7779/dst=");
        if (psk_ptr != NULL) {      //有psk代理则删除
            p_url = psk_ptr+strlen(":7779/dst=");
        }
    }

    op_ret = __http_files_dl(p_url, p_sg_flies_dl_manage->unit_len, file_sz);
    if (OPRT_OK != op_ret) {
        PR_ERR("http_get_download_file faile op_ret:%d", op_ret);
        p_sg_flies_dl_manage->user_cb.files_dl_end_cb(TY_FILES_DL_RESULT_FAILED, TY_FILES_DL_STAT_GET_DATA_FAILED);
        __gui_report_files_dl_result(TY_FILES_DL_EXESTAT_EXCEPT_END);
    } else {
        p_sg_flies_dl_manage->files_ext_stat = TY_FILES_DL_EXESTAT_SUCC;
        p_sg_flies_dl_manage->dl_prog = 100;
        __gui_report_files_dl_result(TY_FILES_DL_EXESTAT_SUCC);
        __gui_report_files_dl_stat();
        //中止判断
        if (__tfm_kepler_files_download_abort_handle() == FALSE) {
            p_sg_flies_dl_manage->files_dl_count++;
            if (p_sg_flies_dl_manage->curr_dl_is_config) {
                config_node->dl_finish = TRUE;
            }
            else {
                file_node->dl_finish = TRUE;
            }
        }
        tal_workq_start_delayed(p_sg_flies_dl_manage->p_files_dl_tm_msg, 200, LOOP_ONCE);
    }

    _dl_exit:
    tal_workq_stop_delayed(p_sg_flies_dl_manage->p_report_dl_prog_tm_msg);
    tal_thread_delete(p_sg_flies_dl_manage->files_dl_trd);
    if (op_ret != OPRT_OK) {
        __files_dl_manage_free();
    }
    return;
}

/**
 * @brief: __files_dl_tm_msg_cb
 * @desc: Create a multi-file download task
 * @return VOID_T
 * @note none
 */
STATIC VOID_T __files_dl_tm_msg_cb(VOID_T *data)
{
    OPERATE_RET op_ret = OPRT_OK;
    UCHAR_T stop_ctrl = 0;
    //文件下载完成
    if (p_sg_flies_dl_manage->files_dl_count >= p_sg_flies_dl_manage->files_num) {
        stop_ctrl = __tfm_kepler_files_download_get_stop_crtl();
        if (TY_FILES_DL_STOP_CTRL_ABORT_ALL == stop_ctrl) {
            p_sg_flies_dl_manage->files_dl_stat = TY_FILES_DL_TASK_STAT_CANCEL_SUCC;
        } else {
            p_sg_flies_dl_manage->files_dl_stat = TY_FILES_DL_TASK_STAT_CLOUD_SUCC;
        }
        p_sg_flies_dl_manage->files_dl_flag = TY_FILES_DL_TASK_FLAG_IDLE;
        p_sg_flies_dl_manage->files_dl_count = 0;
        p_sg_flies_dl_manage->user_cb.files_dl_end_cb(TY_FILES_DL_RESULT_SUCC, TY_FILES_DL_STAT_SUCC);
        __gui_report_files_dl_result(TY_FILES_DL_EXESTAT_SUCC_END);
        __files_dl_manage_free();
        return;
    }

    THREAD_CFG_T thrd_param = {0};
    thrd_param.priority = THREAD_PRIO_3;
    thrd_param.stackDepth = (1024 * 6);             //通过TLS下载文件耗任务栈资源!
    thrd_param.thrdname = "files_dl_thrd";
    op_ret = tal_thread_create_and_start(&(p_sg_flies_dl_manage->files_dl_trd), NULL, NULL, __files_dl_thread, NULL, &(thrd_param));
    if (OPRT_OK != op_ret) {
        PR_ERR("tal_thread_create_and_start err:%d!", op_ret);
        p_sg_flies_dl_manage->files_dl_flag = TY_FILES_DL_TASK_FLAG_IDLE;
        p_sg_flies_dl_manage->files_dl_stat = TY_FILES_DL_TASK_STAT_CLOUD_FAILED;
        p_sg_flies_dl_manage->user_cb.files_dl_end_cb(TY_FILES_DL_RESULT_FAILED, TY_FILES_DL_STAT_INTERNAL_ERR);
        __gui_report_files_dl_result(TY_FILES_DL_EXESTAT_EXCEPT_END);
        __files_dl_manage_free();
    }
    return;
}

/** 
 * @brief: _files_dl_manage_free
 * @desc: Multiple file download cache released
 * @return VOID_T
 * @note none
 */
STATIC VOID_T __files_dl_manage_free(VOID_T)
{
    if (p_sg_flies_dl_manage != NULL) {
        if (p_sg_flies_dl_manage->wait_sem != NULL)
            tal_semaphore_release(p_sg_flies_dl_manage->wait_sem);

        if (p_sg_flies_dl_manage->dl_res != NULL) {
            tuya_gui_resource_release(p_sg_flies_dl_manage->dl_res);
        }

        if (p_sg_flies_dl_manage->curr_res != NULL) {
            tuya_gui_resource_release(p_sg_flies_dl_manage->curr_res);
        }

        if (p_sg_flies_dl_manage->p_report_dl_prog_tm_msg) {
            tal_mutex_lock(p_sg_flies_dl_manage->mutex);
            tal_workq_stop_delayed(p_sg_flies_dl_manage->p_report_dl_prog_tm_msg);
            tal_workq_cancel_delayed(p_sg_flies_dl_manage->p_report_dl_prog_tm_msg);
            tal_mutex_unlock(p_sg_flies_dl_manage->mutex);
        }
        if (p_sg_flies_dl_manage->p_get_filelist_tm_msg) {
            tal_workq_stop_delayed(p_sg_flies_dl_manage->p_get_filelist_tm_msg);
            tal_workq_cancel_delayed(p_sg_flies_dl_manage->p_get_filelist_tm_msg);
        }
        if (p_sg_flies_dl_manage->p_files_dl_tm_msg) {
            tal_workq_stop_delayed(p_sg_flies_dl_manage->p_files_dl_tm_msg);
            tal_workq_cancel_delayed(p_sg_flies_dl_manage->p_files_dl_tm_msg);
        }

        if (p_sg_flies_dl_manage->mutex != NULL)
            tal_mutex_release(p_sg_flies_dl_manage->mutex);

        Free(p_sg_flies_dl_manage);
        p_sg_flies_dl_manage = NULL;
    }
    g_init_is = FALSE;
}

VOID_T tfm_kepler_gui_files_download_action(IN BOOL_T is_continue)
{
    p_sg_flies_dl_manage->res_dl_continue = is_continue;
    tal_semaphore_post(p_sg_flies_dl_manage->wait_sem);
}

/**
 * @brief: tfm_kepler_gui_files_download_stop_control
 * @desc: Abort file transfer control
 * @return OPERATE_RET
 * @param[in] stop_type : true:active shutdown false:failed
 * @param[in] stop_ctrl :TY_FILES_DL_STOP_CTRL_ABORT_ALL:Abort all file
 transfers TY_FILES_DL_STOP_CTRL_ABORT_CURRENT:Abort the current file transfer
 to start the next file transfer
 * @note none
 */
OPERATE_RET tfm_kepler_gui_files_download_stop_control(IN BOOL_T stop_type, IN TY_FILES_DL_STOP_CTRL_E stop_ctrl)
{
    if (stop_ctrl > TY_FILES_DL_STOP_CTRL_ABORT_CURRENT || stop_ctrl < TY_FILES_DL_STOP_CTRL_ABORT_ALL) {
        PR_ERR("stop_ctrl invalid :%d", stop_ctrl);
        return OPRT_INVALID_PARM;
    }

    if (p_sg_flies_dl_manage->files_dl_flag != TY_FILES_DL_TASK_FLAG_ING) {
        PR_ERR("files_dl service is idle");
        return OPRT_INVALID_PARM;
    }
    p_sg_flies_dl_manage->stop_type = stop_type;
    p_sg_flies_dl_manage->files_dl_stop_ctrl = stop_ctrl;
    return OPRT_OK;
}

/**
 * @brief: __tfm_kepler_files_download_get_stop_crtl
 * @desc: __tfm_kepler_files_download_get_stop_crtl
 * @return files_dl_stop_ctrl
 * @note none
 */
STATIC UCHAR_T __tfm_kepler_files_download_get_stop_crtl(VOID_T)
{
    return p_sg_flies_dl_manage->files_dl_stop_ctrl;
}

gui_version_s *tfm_kepler_gui_files_get_version_node(VOID)
{
    if (p_sg_flies_dl_manage->dl_res != NULL)
        return &p_sg_flies_dl_manage->dl_res->version;
    else
        return NULL;
}

/**
 * @brief: Tuya Multiple file download service initialized
 * @desc: Tuya Multiple file download service initialized
 * @return OPERATE_RET
 * @param[in] TY_FILES_DL_USER_REG_S
 * @note none
 */
OPERATE_RET tfm_kepler_gui_files_download_init(IN TY_FILES_DL_USER_REG_S *p_user_reg)
{
    OPERATE_RET op_ret = OPRT_OK;
    if(NULL == p_user_reg \
        || NULL == p_user_reg->files_dl_end_cb \
        || NULL == p_user_reg->files_dl_start_cb \
        || NULL == p_user_reg->files_dl_trans_cb){
        PR_ERR("tfm_kepler_gui_files_download_init param err!");
        return OPRT_INVALID_PARM;
    }

    p_sg_flies_dl_manage = (TY_FILES_DL_MANAGE_S *)Malloc(SIZEOF(TY_FILES_DL_MANAGE_S));
    if (NULL == p_sg_flies_dl_manage) {
        PR_ERR("Malloc failed!");
        return OPRT_MALLOC_FAILED;
    }
    memset(p_sg_flies_dl_manage, 0, SIZEOF(TY_FILES_DL_MANAGE_S));
    memcpy(&p_sg_flies_dl_manage->user_cb, p_user_reg, SIZEOF(TY_FILES_DL_USER_REG_S));
    p_sg_flies_dl_manage->curr_res = NULL;
    p_sg_flies_dl_manage->dl_res = NULL;
    p_sg_flies_dl_manage->curr_dl_node = NULL;
    p_sg_flies_dl_manage->wait_sem = NULL;
    p_sg_flies_dl_manage->mutex = NULL;
    p_sg_flies_dl_manage->p_report_dl_prog_tm_msg = NULL;
    p_sg_flies_dl_manage->p_get_filelist_tm_msg = NULL;
    p_sg_flies_dl_manage->p_files_dl_tm_msg = NULL;

    op_ret = tal_semaphore_create_init(&p_sg_flies_dl_manage->wait_sem, 0, 1);
    if (op_ret != OPRT_OK) {
        PR_ERR("tal_semaphore_create_init err:%d", op_ret);
        __files_dl_manage_free();
        return op_ret;
    }

    op_ret = tal_mutex_create_init(&p_sg_flies_dl_manage->mutex);
    if (OPRT_OK != op_ret) {
        PR_ERR("tal_mutex_create_init:%d", op_ret);
        __files_dl_manage_free();
        return op_ret;
    }

    //进度上报
    op_ret = tal_workq_init_delayed(WORKQ_HIGHTPRI, __report_dl_prog_tm_msg_cb, NULL, &p_sg_flies_dl_manage->p_report_dl_prog_tm_msg);
    if (OPRT_OK != op_ret) {
        PR_ERR("tal_workq_init_delayed __report_dl_prog_tm_msg_cb failed:%d!", op_ret);
        __files_dl_manage_free();
        return op_ret;
    }

    //获取文件列表
    op_ret = tal_workq_init_delayed(WORKQ_HIGHTPRI, __get_filelist_inf_tm_msg_cb, NULL, &p_sg_flies_dl_manage->p_get_filelist_tm_msg);
    if (OPRT_OK != op_ret) {
        PR_ERR("tal_workq_init_delayed __get_filelist_inf_tm_msg_cb failed:%d!", op_ret);
        __files_dl_manage_free();
        return op_ret;
    }

    //执行文件下载
    op_ret = tal_workq_init_delayed(WORKQ_HIGHTPRI, __files_dl_tm_msg_cb, NULL, &p_sg_flies_dl_manage->p_files_dl_tm_msg);
    if (OPRT_OK != op_ret) {
        PR_ERR("cmmod_cr_tm_msg_hand __files_dl_tm_msg_cb failed:%d!", op_ret);
        __files_dl_manage_free();
        return op_ret;
    }

    op_ret = tal_workq_start_delayed(p_sg_flies_dl_manage->p_get_filelist_tm_msg, 200, LOOP_ONCE);
    if (OPRT_OK != op_ret) {
        PR_ERR("tal_workq_start_delayed rr :%d!", op_ret);
        __files_dl_manage_free();
        return op_ret;
    }

    g_init_is = TRUE;
    return OPRT_OK;
}

/**
 * @brief: Tuya Multiple file download service is initialized
 * @desc: 涂鸦多文件下载服务是否在初始化运行
 * @return BOOL
 * @param[in] NULL
 * @note none
 */
BOOL_T tfm_kepler_gui_files_download_is_initialized(VOID)
{
    return g_init_is;
}
