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
#include "string.h"
#include "tuya_iot_internal_api.h"
#include "ty_cJSON.h"
#include "uni_log.h"
#include "tal_time_service.h"
#include "tal_thread.h"
#include "ty_gfw_gui_kepler_menu.h"
#include "ty_gfw_gui_kepler_menu_download_cloud.h"
#include "tal_hash.h"
#include "gw_intf.h"
#include "base_event.h"
#include "tuya_app_gui_display_text.h"
#include "app_ipc_command.h"
#include "ty_gfw_gui_kepler_menu_param_generate.h"
#include "ty_gfw_gui_kepler_menu_get_list_handle.h"
#include "tuya_app_gui_gw_core0.h"
#include "http_manager.h"

/***********************************************************
*************************micro define***********************
***********************************************************/

#define TY_HTTP_GET_RETRY                   (3)
#define TY_HTTP_GET_TIMEOUT                 (3000)

/***********************************************************
***********************typedef define***********************
***********************************************************/
// Session池中的单个session条目
typedef struct http_session_entry {
    CHAR_T host[256];               // 主机名(含端口)
    SESSION_ID session;             // HTTP session ID
    struct http_session_entry *next; // 链表指针
} HTTP_SESSION_ENTRY_S;

typedef struct {
    UINT_T unit_len;
    DELAYED_WORK_HANDLE p_get_filelist_tm_msg;
    IOT_RAW_HTTP_S raw_http;
    TY_MENU_DL_TRANS_DATA *dl_read_buf;
    // HTTP Session Manager support
    BOOL_T session_reuse_enabled;   // 是否启用session复用
} TY_FILES_DL_MANAGE_S;

STATIC CR_REQ_LIST_NODE_S *curr_cr_req = NULL;
STATIC GUI_CLOUD_RECIPE_REQ_TYPE_E currCrReqType = CL_MENU_TYPE_UNKNOWN;

//CNTSOF(ty_cloud_recipe_hdl_array)
STATIC TY_CLOUD_RECIPE_HDL_INF_S ty_cloud_recipe_hdl_array[] = {
    //获取食谱分类列表
    {CL_MENU_CATEGORY_LIST_1_0,         "tuya.device.menu.category.list",       "1.0",  NULL,   ty_cloud_recipe_menu_category_list_1_0_param_gen,       ty_cloud_recipe_menu_category_list_1_0_parse},
    {CL_MENU_CATEGORY_LIST_2_0,         "tuya.device.menu.category.list",       "2.0",  NULL,   ty_cloud_recipe_menu_category_list_2_0_param_gen,       ty_cloud_recipe_menu_category_list_2_0_parse},
    //获取食谱列表
    {CL_MENU_LANG_LIST_1_0,             "tuya.device.menu.lang.list",           "1.0",  NULL,   ty_cloud_recipe_menu_lang_list_1_0_param_gen,           ty_cloud_recipe_menu_lang_list_1_0_parse},
    //获取食谱详情
    {CL_MENU_GET_1_0,                   "tuya.device.menu.get",                 "1.0",  NULL,   ty_cloud_recipe_menu_get_1_0_param_gen,                 ty_cloud_recipe_menu_get_1_0_parse},
    //自定义食谱
    {CL_MENU_CUSTOM_LANG_LIST_1_0,      "tuya.device.menu.custom.lang.list",    "1.0",  NULL,   ty_cloud_recipe_menu_custom_lang_list_1_0_param_gen,    ty_cloud_recipe_menu_custom_lang_list_1_0_parse},
    {CL_MENU_DIY_ADD_2_0,               "tuya.device.menu.diy.add",             "2.0",  NULL,   ty_cloud_recipe_menu_diy_add_2_0_param_gen,             ty_cloud_recipe_menu_diy_add_2_0_parse},
    {CL_MENU_DIY_UPDATE_2_0,            "tuya.device.menu.diy.update",          "2.0",  NULL,   ty_cloud_recipe_menu_diy_update_2_0_param_gen,          ty_cloud_recipe_menu_diy_update_2_0_parse},
    {CL_MENU_DIY_DELETE_1_0,            "tuya.device.menu.diy.delete",          "1.0",  NULL,   ty_cloud_recipe_menu_diy_delete_1_0_param_gen,          ty_cloud_recipe_menu_diy_delete_1_0_parse},
    //食谱收藏
    {CL_MENU_STAR_LIST_2_0,             "tuya.device.menu.star.list",           "2.0",  NULL,   ty_cloud_recipe_menu_star_list_2_0_param_gen,           ty_cloud_recipe_menu_star_list_2_0_parse},
    {CL_MENU_STAR_ADD_1_0,              "tuya.device.menu.star.add",            "1.0",  NULL,   ty_cloud_recipe_menu_star_add_1_0_param_gen,            ty_cloud_recipe_menu_star_add_1_0_parse},
    {CL_MENU_STAR_DELETE_1_0,           "tuya.device.menu.star.delete",         "1.0",  NULL,   ty_cloud_recipe_menu_star_delete_1_0_param_gen,         ty_cloud_recipe_menu_star_delete_1_0_parse},
    {CL_MENU_STAR_CHECK_1_0,            "tuya.device.menu.star.check",          "1.0",  NULL,   ty_cloud_recipe_menu_star_check_1_0_param_gen,          ty_cloud_recipe_menu_star_check_1_0_parse},
    {CL_MENU_STAR_COUNT_1_0,            "tuya.device.menu.star.count",          "1.0",  NULL,   ty_cloud_recipe_menu_star_count_1_0_param_gen,          ty_cloud_recipe_menu_star_count_1_0_parse},
    //食谱添加评分
    {CL_MENU_SCORE_ADD_1_0,             "tuya.device.menu.score.add",           "1.0",  NULL,   ty_cloud_recipe_menu_score_add_1_0_param_gen,           ty_cloud_recipe_menu_score_add_1_0_parse},
    //获取推荐设备列表和banner列表
    {CL_MENU_BANNER_LIST_1_0,           "tuya.device.menu.banner.list",         "1.0",  NULL,   ty_cloud_recipe_menu_banner_lsit_1_0_param_gen,         ty_cloud_recipe_menu_banner_lsit_1_0_parse},
    //获取食谱所有语言
    {CL_COOKBOOK_ALL_LANG_1_0,          "tuya.device.cookbook.all.lang",        "1.0",  NULL,   ty_cloud_recipe_cookbook_all_lang_1_0_param_gen,        ty_cloud_recipe_cookbook_all_lang_1_0_parse},
    //设备获取搜索模型
    {CL_MENU_QUERY_MODEL_LIST_1_0,      "tuya.device.menu.query.model.list",    "1.0",  NULL,   ty_cloud_recipe_menu_query_model_list_1_0_param_gen,    ty_cloud_recipe_menu_query_model_list_1_0_parse},
    //获取烹饪时间筛选条件
    {CL_MENU_SEARCH_CONDITION_1_0,      "tuya.device.menu.search.condition",    "1.0",  NULL,   ty_cloud_recipe_menu_search_condition_1_0_param_gen,    ty_cloud_recipe_menu_search_condition_1_0_parse},
    //获取待同步食谱列表
    {CL_MENU_SYNC_LIST_1_0,             "tuya.device.menu.sync.list",           "1.0",  NULL,   ty_cloud_recipe_menu_sync_list_1_0_param_gen,           ty_cloud_recipe_menu_sync_list_1_0_parse},
    //批量同步食谱详情
    {CL_MENU_SYNC_DETAILS_1_0,          "tuya.device.menu.sync.details",        "1.0",  NULL,   ty_cloud_recipe_menu_sync_details_1_0_param_gen,        ty_cloud_recipe_menu_sync_details_1_0_parse}
};

/***********************************************************
***********************function declaration*****************
***********************************************************/
STATIC OPERATE_RET __http_get_filelist(TY_CLOUD_RECIPE_HDL_INF_S *cl_hdl);
STATIC OPERATE_RET __get_filelist(VOID_T);
STATIC VOID_T __get_filelist_inf_tm_msg_cb(VOID_T *data);
STATIC OPERATE_RET __get_files_dl_data_handle(IN UINT_T total_len, IN VOID_T *des_buff, IN UINT_T offset, IN CHAR_T *data, IN UINT_T len);
STATIC OPERATE_RET __http_files_dl(OUT UINT_T *total_len, OUT VOID **out_buff, IN CHAR_T *p_url);
STATIC VOID_T __files_dl_manage_free(VOID_T);
STATIC VOID __remove_session_from_pool(IN CHAR_T *host);

/***********************************************************
***********************variable define**********************
***********************************************************/
STATIC TY_FILES_DL_MANAGE_S *p_sg_flies_dl_manage = NULL;
STATIC BOOL_T g_init_is = FALSE;

// 全局session池 - 不随下载任务释放
STATIC HTTP_SESSION_ENTRY_S *g_session_pool = NULL;

/***********************************************************
***********************function define**********************
***********************************************************/

STATIC VOID __http_menu_list_printf_completely(CHAR_T *info_str, size_t str_len)
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

/**
 * @brief: __http_get_filelist
 * @desc: Get the file list information and parse it
 * @param[in] workid:Gets the file list input parameters
 * @return OPERATE_RET
 * @note none
 */
STATIC OPERATE_RET __http_get_filelist(TY_CLOUD_RECIPE_HDL_INF_S *cl_hdl)
{
    OPERATE_RET op_ret = OPRT_OK;
    ty_cJSON *p_result_obj = NULL;
    CHAR_T *p_root_dbg = NULL;
    CHAR_T *p_out = NULL;
    GW_CNTL_S *gw_cntl = get_gw_cntl();

    if (cl_hdl->menu_gen_parm_hdl != NULL)
        op_ret = cl_hdl->menu_gen_parm_hdl(cl_hdl->req_param, &p_out);
    if (op_ret != OPRT_OK) {
        PR_ERR("menu gen param fail!");
        return op_ret;
    }
    
    PR_DEBUG("iot_httpc_common_post out: %s, api_ver: '%s'", (p_out==NULL)?"null":p_out, cl_hdl->api_ver);
    op_ret = iot_httpc_common_post(cl_hdl->api_name, cl_hdl->api_ver, NULL, gw_cntl->gw_if.id, p_out, NULL, &p_result_obj);
    if (p_out) {
        Free(p_out);
        p_out = NULL;
    }
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
    __http_menu_list_printf_completely(p_root_dbg, strlen(p_root_dbg));
    Free(p_root_dbg), p_root_dbg = NULL;
    op_ret = cl_hdl->menu_get_list_hdl(p_result_obj);
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
STATIC OPERATE_RET __get_filelist(VOID_T)
{
    OPERATE_RET op_ret = OPRT_OK;
    TY_CLOUD_RECIPE_HDL_INF_S *cl_hdl = NULL;

    for (UINT16_T i = 0; i < CNTSOF(ty_cloud_recipe_hdl_array); i++) {
        if (ty_cloud_recipe_hdl_array[i].req_type == curr_cr_req->cr_req.req_type && curr_cr_req->cr_req.req_type != CL_MENU_TYPE_UNKNOWN) {
            cl_hdl = &ty_cloud_recipe_hdl_array[i];
            if (curr_cr_req->cr_req.req_param != NULL)
                ty_cloud_recipe_hdl_array[i].req_param = curr_cr_req->cr_req.req_param;
            currCrReqType = curr_cr_req->cr_req.req_type;
            break;
        }
    }

    if (cl_hdl == NULL) {
        op_ret = OPRT_INVALID_PARM;
        currCrReqType = CL_MENU_TYPE_UNKNOWN;
        PR_ERR("__get_filelist param err:%d!", op_ret);
        return op_ret;
    }
    // get url
    ty_cloud_recipe_menu_dl_register_cb(__http_files_dl);          //注册下载回调函数!
    op_ret = __http_get_filelist(cl_hdl);
    if (OPRT_OK != op_ret) {
        PR_ERR("__http_get_filelist_inf err:%d!", op_ret);
        return op_ret;
    }

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
    if(OPRT_OK != __get_filelist()){
        PR_ERR("_get_file_list failed !");
        ty_cloud_recipe_menu_resp_send(CL_MENU_DOWNLOAD_FAIL, (VOID *)&currCrReqType);
    }

    __files_dl_manage_free();
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
STATIC OPERATE_RET __get_files_dl_data_handle(IN UINT_T total_len, IN VOID_T *des_buff, IN UINT_T offset, IN CHAR_T *data, IN UINT_T len)
{
    OPERATE_RET op_ret = OPRT_OK;

    if (offset > total_len) {
        op_ret = OPRT_COM_ERROR;
        PR_ERR("file dl offset '%lu' exceed file len '%lu' !", offset, total_len);
    }
    else
        memcpy(des_buff+offset, data, len);
    if (offset + len >= total_len) {       //write end
        PR_NOTICE("file dl end==================>");
    }
    return op_ret;
}

/**
 * @brief: __extract_host_from_url
 * @desc: 从URL中提取主机名(含端口)
 */
STATIC VOID __extract_host_from_url(IN CHAR_T *url, OUT CHAR_T *host, IN UINT_T host_len)
{
    if (!url || !host) return;
    
    // 跳过协议部分 http:// 或 https://
    CHAR_T *start = strstr(url, "://");
    if (start) start += 3;
    else start = url;
    
    // 查找路径开始位置 '/'
    CHAR_T *end = strchr(start, '/');
    UINT_T len = end ? (end - start) : strlen(start);
    
    if (len >= host_len) len = host_len - 1;
    strncpy(host, start, len);
    host[len] = '\0';
}

/**
 * @brief: __session_pool_find
 * @desc: 从全局session池中查找指定host的session
 */
STATIC SESSION_ID __session_pool_find(IN CHAR_T *host)
{
    if (!host) return NULL;
    
    HTTP_SESSION_ENTRY_S *entry = g_session_pool;
    while (entry) {
        if (strcmp(entry->host, host) == 0) {
            PR_DEBUG("Found cached session for host: %s", host);
            return entry->session;
        }
        entry = entry->next;
    }
    return NULL;
}

/**
 * @brief: __add_session_to_pool
 * @desc: 将session添加到session池
 */
STATIC OPERATE_RET __add_session_to_pool(IN CHAR_T *host, IN SESSION_ID session)
{
    if (!host || !session) return OPRT_INVALID_PARM;
    
    // 检查是否已存在(使用全局池)
    if (__session_pool_find(host) != NULL) {
        PR_DEBUG("Session already exists in global pool for host: %s", host);
        return OPRT_OK;
    }
    
    // 创建新条目
    HTTP_SESSION_ENTRY_S *entry = (HTTP_SESSION_ENTRY_S *)Malloc(sizeof(HTTP_SESSION_ENTRY_S));
    if (!entry) {
        PR_ERR("Failed to allocate session pool entry");
        return OPRT_MALLOC_FAILED;
    }
    
    memset(entry, 0, sizeof(HTTP_SESSION_ENTRY_S));
    strncpy(entry->host, host, sizeof(entry->host) - 1);
    entry->session = session;
    
    // 插入到全局链表头
    entry->next = g_session_pool;
    g_session_pool = entry;
    
    PR_NOTICE("Added session to global pool for host: %s", host);
    return OPRT_OK;
}

/**
 * @brief: __remove_session_from_pool
 * @desc: 从全局session池中移除指定host的session
 */
STATIC VOID __remove_session_from_pool(IN CHAR_T *host)
{
    if (!host) return;
    
    HTTP_SESSION_ENTRY_S *entry = g_session_pool;
    HTTP_SESSION_ENTRY_S *prev = NULL;
    
    while (entry) {
        if (strcmp(entry->host, host) == 0) {
            // 找到了,从链表中移除
            if (prev) {
                prev->next = entry->next;
            } else {
                g_session_pool = entry->next;
            }
            
            // 销毁session
            S_HTTP_MANAGER *mgr = get_http_manager_instance();
            if (mgr && entry->session) {
                mgr->destory_http_session(entry->session);
            }
            
            Free(entry);
            PR_DEBUG("Removed session from pool for host: %s", host);
            return;
        }
        prev = entry;
        entry = entry->next;
    }
}

#if 0
/**
 * @brief: __clear_session_pool
 * @desc: 清空全局session池 (不在每次下载任务结束时调用,保持session持久化)
 */
STATIC VOID __clear_session_pool(VOID)
{
    S_HTTP_MANAGER *mgr = get_http_manager_instance();
    HTTP_SESSION_ENTRY_S *entry = g_session_pool;
    
    while (entry) {
        HTTP_SESSION_ENTRY_S *next = entry->next;
        
        // 销毁session
        if (mgr && entry->session) {
            mgr->destory_http_session(entry->session);
        }
        
        Free(entry);
        entry = next;
    }
    
    g_session_pool = NULL;
    PR_NOTICE("Cleared all sessions from global pool");
}
#endif

/**
 * @brief: __http_files_dl_with_session
 * @desc: 使用http_manager.h实现session复用下载(支持多host的session池)
 * @return OPERATE_RET
 */
STATIC OPERATE_RET __http_files_dl_with_session(OUT UINT_T *total_len, OUT VOID **out_buff, IN CHAR_T *p_url)
{
    S_HTTP_MANAGER *mgr = get_http_manager_instance();
    if (!mgr) return OPRT_COM_ERROR;
    
    // ========== 计时开始 ==========
    SYS_TICK_T time_start = tal_system_get_millisecond();
    SYS_TICK_T time_conn_start = 0, time_conn_end = 0;
    SYS_TICK_T time_download_start = 0, time_download_end = 0;
    
    // 提取当前URL的host
    CHAR_T curr_host[256] = {0};
    __extract_host_from_url(p_url, curr_host, sizeof(curr_host));
    
    // 从session池中查找是否有可复用的session
    SESSION_ID session = __session_pool_find(curr_host);
    BOOL_T is_new_session = FALSE;
    
    // 如果没有找到,创建新session并加入池
    if (session == NULL) {
        time_conn_start = tal_system_get_millisecond();  // 连接开始计时
        
        session = mgr->create_http_session(p_url, TRUE);
        if (!session) {
            PR_ERR("Failed to create session for host: %s", curr_host);
            return OPRT_COM_ERROR;
        }
        
        time_conn_end = tal_system_get_millisecond();  // 连接结束计时
        
        if (__add_session_to_pool(curr_host, session) != OPRT_OK) {
            PR_WARN("Failed to add session to pool, but continue...");
        }
        is_new_session = TRUE;
        PR_NOTICE("Created NEW session for host: %s (conn_time: %ums)", 
                  curr_host, (UINT_T)(time_conn_end - time_conn_start));
    } else {
        // 复用时记录0连接时间
        time_conn_start = time_conn_end = tal_system_get_millisecond();
        PR_DEBUG("Reusing cached session for host: %s", curr_host);

    }

    // 发送GET请求
    time_download_start = tal_system_get_millisecond();  // 下载开始计时
    
    http_req_t req = {.type = HTTP_GET, .version = HTTP_VER_1_1};
    req.resource = strchr(p_url + 8, '/') ?: "/";
    
    OPERATE_RET ret = mgr->send_http_request(session, &req, STANDARD_HDR_FLAGS);
    if (ret != OPRT_OK) {
        PR_ERR("send_http_request failed: %d, remove session from pool", ret);
        // 连接可能断开,从池中移除,下次会重建
        __remove_session_from_pool(curr_host);
        return ret;
    }
    
    // 接收响应
    http_resp_t *resp = NULL;
    ret = mgr->receive_http_response(session, &resp);
    if (ret != OPRT_OK || !resp || (resp->status_code != 200 && resp->status_code != 206)) {
        PR_ERR("receive_http_response failed: ret=%d, status=%d", ret, resp?resp->status_code:0);
        __remove_session_from_pool(curr_host);
        return OPRT_COM_ERROR;
    }
    
    // 接收数据
    BYTE_T *data = NULL;
    ret = mgr->receive_http_data(session, resp, &data);
    if (ret != OPRT_OK || !data) {
        PR_ERR("receive_http_data failed: %d", ret);
        __remove_session_from_pool(curr_host);
        return ret;
    }
    
    // 复制到输出
    *total_len = resp->content_length;
    *out_buff = cr_parser_malloc(*total_len);
    if (!*out_buff) {
        PR_ERR("malloc failed for %u bytes", *total_len);
        // 内存不足,但session仍然有效,不从池中移除
        return OPRT_MALLOC_FAILED;
    }
    memcpy(*out_buff, data, *total_len);
    
    time_download_end = tal_system_get_millisecond();  // 下载结束计时
    
    // ========== 统计时间并打印 ==========
    SYS_TICK_T time_total = time_download_end - time_start;
    SYS_TICK_T time_conn = time_conn_end - time_conn_start;
    SYS_TICK_T time_download = time_download_end - time_download_start;
    
    if (is_new_session) {
        PR_NOTICE("NEW conn timing - Total: %ums, Conn: %ums, Download: %ums, Size: %u bytes", 
                  (UINT_T)time_total, (UINT_T)time_conn, (UINT_T)time_download, *total_len);
    } else {
        PR_NOTICE("REUSE timing - Total: %ums, Download: %ums, Size: %u bytes", 
                  (UINT_T)time_total, (UINT_T)time_download, *total_len);
    }
    
    // 只在创建新session时打印（与SDK内部http_inf.c行为一致）
    if (is_new_session) {
        PR_DEBUG("Session download OK: %u bytes (NEW session)", *total_len);
    }
    // 复用时静默执行，不输出日志（减少日志噪音）
    
    return OPRT_OK;
}

/**
 * @brief: __http_files_dl
 * @desc: Multiple files are downloaded and transferred
 * @param[in] url:File pull request parameters
 * @param[in] unit_len:Length of data in a single pull
 * @param[out] total_len:Total file length
 * @return VOID_T
 * @note none
 */
STATIC OPERATE_RET __http_files_dl(OUT UINT_T *total_len, OUT VOID **out_buff, IN CHAR_T *p_url)
{
    OPERATE_RET op_ret = OPRT_OK;
    OPERATE_RET op_ret2 = OPRT_OK;
    UINT_T offset = 0;
    INT_T status_code = 0;
    BOOL_T chunked = FALSE;
    UINT_T content_len = 0;
    UCHAR_T retry_count = 0, i = 0;
    USHORT_T unit_len = p_sg_flies_dl_manage->unit_len;

    // 尝试使用HTTP session复用机制
    // 测试: 对所有tuyacn.com域名(包括images.tuyacn.com)都尝试使用session复用
    if (p_sg_flies_dl_manage->session_reuse_enabled) {
        // 检查是否是tuyacn.com域名
        if (strstr(p_url, "tuyacn.com") != NULL) {
            // 尝试使用session复用
            op_ret = __http_files_dl_with_session(total_len, out_buff, p_url);
            if (OPRT_OK == op_ret) {
                PR_NOTICE("Downloaded via HTTP session manager (domain: %s)", p_url);
                return op_ret;
            } else {
                PR_WARN("Session manager failed, fallback to raw http. ret:%d", op_ret);
            }
        } else {
            // 非涂鸦域名,使用raw http
            PR_DEBUG("Using raw http for non-Tuya domain: %s", p_url);
        }
    }

    //tuya_tls_set_ssl_verify(0);
    for (i = 0; i < TY_HTTP_GET_RETRY; i++) {
        op_ret = iot_httpc_raw_get(p_url, &(p_sg_flies_dl_manage->raw_http), offset, 0);
        if (OPRT_OK != op_ret) {
            PR_ERR("iot_httpc_raw_get err:%d retry:%d", op_ret, i);
        } else {
            break;
        }
        tal_system_sleep(TY_HTTP_GET_TIMEOUT);
    }
    if (i >= TY_HTTP_GET_RETRY) {
        op_ret = OPRT_COM_ERROR;
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
        op_ret = OPRT_COM_ERROR;
        goto ERR_EXIT;
    }
    if (((status_code != 200) && (status_code != 206)) || (0 == content_len && !chunked)) {
        PR_ERR("content_len:%d chunked:%d status_code:%d Invalid!!!\n", content_len, chunked, status_code);
        op_ret = OPRT_COM_ERROR;
        goto ERR_EXIT;
    }
    *total_len = content_len;
    *out_buff = cr_parser_malloc(content_len);
    if (*out_buff == NULL) {
        PR_ERR("req cp1 malloc fail !!!\n");
        op_ret = OPRT_MALLOC_FAILED;
        goto ERR_EXIT;
    }

    UINT_T sum_read_len = 0;
    INT_T read_len = 0;
    UINT_T have_read_len = 0; //本次累计已读取的长度

    p_sg_flies_dl_manage->dl_read_buf = Malloc(sizeof(TY_MENU_DL_TRANS_DATA) + unit_len);
    if (NULL == p_sg_flies_dl_manage->dl_read_buf) {
        op_ret = OPRT_MALLOC_FAILED;
        PR_ERR("malloc failed!");
        goto ERR_EXIT;
    }
    memset(p_sg_flies_dl_manage->dl_read_buf, 0, sizeof(TY_MENU_DL_TRANS_DATA) + unit_len);
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
        if (sum_read_len + have_read_len >= *total_len) { // the last
            op_ret = __get_files_dl_data_handle(*total_len, *out_buff, sum_read_len, read_buff, have_read_len);
            if (OPRT_OK != op_ret) {
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
        op_ret = __get_files_dl_data_handle(*total_len, *out_buff, sum_read_len, read_buff, unit_len);
        if (OPRT_OK != op_ret) {
            goto ERR_EXIT;
        }
        sum_read_len += have_read_len;
        have_read_len = 0;
    }
    PR_DEBUG("http '%s' open success! content_len:%d chunked:%d status_code:%d", p_url, content_len, chunked, status_code);
    Free(p_sg_flies_dl_manage->dl_read_buf);
    p_sg_flies_dl_manage->dl_read_buf = NULL;

    op_ret2 = iot_httpc_raw_close(p_sg_flies_dl_manage->raw_http);
    if (op_ret2 != OPRT_OK) {
        PR_ERR("iot_httpc_raw_close err:%d!", op_ret2);
    }
    p_sg_flies_dl_manage->raw_http = NULL;

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

    return op_ret;
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
        if (p_sg_flies_dl_manage->p_get_filelist_tm_msg) {
            tal_workq_stop_delayed(p_sg_flies_dl_manage->p_get_filelist_tm_msg);
            tal_workq_cancel_delayed(p_sg_flies_dl_manage->p_get_filelist_tm_msg);
        }

        Free(p_sg_flies_dl_manage);
        p_sg_flies_dl_manage = NULL;
    }
    if (curr_cr_req != NULL) {
        if (curr_cr_req->cr_req.req_param)
            Free(curr_cr_req->cr_req.req_param);
        Free(curr_cr_req);
        curr_cr_req = NULL;
    }
    g_init_is = FALSE;
    ty_publish_event(EVENT_MENU_REQ_PROCESS_COMPLETED, NULL);
}

/**
 * @brief: Tuya Multiple file download service initialized
 * @desc: Tuya Multiple file download service initialized
 * @return OPERATE_RET
 * @param[in] TY_MENU_DL_USER_REG_S
 * @note none
 */
OPERATE_RET tfm_kepler_gui_menu_download_init(CR_REQ_LIST_NODE_S *req_node)
{
    OPERATE_RET op_ret = OPRT_OK;

    p_sg_flies_dl_manage = (TY_FILES_DL_MANAGE_S *)Malloc(SIZEOF(TY_FILES_DL_MANAGE_S));
    if (NULL == p_sg_flies_dl_manage) {
        PR_ERR("Malloc failed!");
        if (req_node != NULL) {
            if (req_node->cr_req.req_param)
                Free(req_node->cr_req.req_param);
            Free(req_node);
            req_node = NULL;
        }
        return OPRT_MALLOC_FAILED;
    }
    curr_cr_req = req_node;
    memset(p_sg_flies_dl_manage, 0, SIZEOF(TY_FILES_DL_MANAGE_S));
    p_sg_flies_dl_manage->p_get_filelist_tm_msg = NULL;
    p_sg_flies_dl_manage->unit_len = curr_cr_req->dl_unit_len;
    
    p_sg_flies_dl_manage->session_reuse_enabled = TRUE;
    PR_NOTICE("HTTP global session pool enabled (using WORKQ_SYSTEM)");

    //获取文件列表 - 使用WORKQ_SYSTEM以获得更大的栈空间
    op_ret = tal_workq_init_delayed(WORKQ_SYSTEM, __get_filelist_inf_tm_msg_cb, NULL, &p_sg_flies_dl_manage->p_get_filelist_tm_msg);
    if (OPRT_OK != op_ret) {
        PR_ERR("tal_workq_init_delayed __get_filelist_inf_tm_msg_cb failed:%d!", op_ret);
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
BOOL_T tfm_kepler_gui_menu_download_is_initialized(VOID)
{
    return g_init_is;
}
