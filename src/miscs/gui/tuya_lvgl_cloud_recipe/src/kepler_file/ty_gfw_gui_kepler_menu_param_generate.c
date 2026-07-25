/**
 * @file ty_gfw_gui_kepler_menu_param_generate.c
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
#include <string.h>
#include "string.h"
#include "tuya_iot_internal_api.h"
#include "ty_cJSON.h"
#include "tal_memory.h"
#include "uni_log.h"
#include "ty_gfw_gui_kepler_menu_download_cloud.h"

/***********************************************************
*************************micro define***********************
***********************************************************/
#define CR_REQ_PARAMM_JSON          //使用json字符串直接作为请求参数

#ifdef CR_REQ_PARAMM_JSON
STATIC OPERATE_RET _cp_cr_str(IN CHAR_T *src_str, OUT CHAR_T **des_str)
{
    UINT32_T len = strlen(src_str) + 1;

    *des_str = (CHAR_T *)Malloc(len);
    if (*des_str == NULL) {
        PR_ERR("%s-%d:malloc fail!", __func__, __LINE__);
        return OPRT_MALLOC_FAILED;
    }
    memset(*des_str, 0, len);
    strcpy(*des_str, src_str);
    return OPRT_OK;
}
#endif

OPERATE_RET ty_cloud_recipe_menu_category_list_1_0_param_gen(IN VOID *req_param, OUT CHAR_T **pp_out)
{
    OPERATE_RET op_ret = OPRT_INVALID_PARM;
#ifdef CR_REQ_PARAMM_JSON
    op_ret = _cp_cr_str((CHAR_T *)req_param, pp_out);
#else
    ty_cJSON *p_root = NULL;
    CHAR_T *p_out = NULL;
    TY_MENU_PARAM_CATEGORY_LIST_1_0_S *param = NULL;

    if (req_param != NULL) {
        param = (TY_MENU_PARAM_CATEGORY_LIST_1_0_S *)req_param;
        p_root = ty_cJSON_CreateObject();
        if (NULL == p_root) {
            PR_ERR("%s-%d: p_root is null!", __func__, __LINE__);
            return op_ret;
        }
        ty_cJSON_AddStringToObject(p_root, "lang", param->lang);
        p_out = ty_cJSON_PrintUnformatted(p_root);
        ty_cJSON_Delete(p_root);
        if (NULL == p_out) {
            PR_ERR("%s-%d: ty_cJSON_PrintUnformatted failed", __func__, __LINE__);
            return op_ret;
        }
        *pp_out = p_out;
        op_ret = OPRT_OK;
    }
#endif
    return op_ret;
}

OPERATE_RET ty_cloud_recipe_menu_category_list_2_0_param_gen(IN VOID *req_param, OUT CHAR_T **pp_out)
{
    OPERATE_RET op_ret = OPRT_INVALID_PARM;
#ifdef CR_REQ_PARAMM_JSON
    op_ret = _cp_cr_str((CHAR_T *)req_param, pp_out);
#else
    ty_cJSON *p_root = NULL;
    CHAR_T *p_out = NULL;
    TY_MENU_PARAM_CATEGORY_LIST_2_0_S *param = NULL;

    if (req_param != NULL) {
        param = (TY_MENU_PARAM_CATEGORY_LIST_2_0_S *)req_param;
        p_root = ty_cJSON_CreateObject();
        if (NULL == p_root) {
            PR_ERR("%s-%d: p_root is null!", __func__, __LINE__);
            return op_ret;
        }
        ty_cJSON_AddStringToObject(p_root, "lang", param->lang);
        p_out = ty_cJSON_PrintUnformatted(p_root);
        ty_cJSON_Delete(p_root);
        if (NULL == p_out) {
            PR_ERR("%s-%d: ty_cJSON_PrintUnformatted failed", __func__, __LINE__);
            return op_ret;
        }
        *pp_out = p_out;
        op_ret = OPRT_OK;
    }
#endif
    return op_ret;

}

OPERATE_RET ty_cloud_recipe_menu_lang_list_1_0_param_gen(IN VOID *req_param, OUT CHAR_T **pp_out)
{
    OPERATE_RET op_ret = OPRT_INVALID_PARM;
#ifdef CR_REQ_PARAMM_JSON
    op_ret = _cp_cr_str((CHAR_T *)req_param, pp_out);
#else
    ty_cJSON *p_root = NULL;
    ty_cJSON *p_sub_root = NULL;
    CHAR_T *p_out = NULL;
    TY_MENU_PARAM_LANG_LIST_1_0_S *param = NULL;

    p_root = ty_cJSON_CreateObject();
    if (NULL == p_root) {
        PR_ERR("%s-%d: p_root is null!", __func__, __LINE__);
        goto err_out;
    }
    p_sub_root = ty_cJSON_CreateObject();
    if (NULL == p_sub_root) {
        PR_ERR("%s-%d: p_sub_root is null!", __func__, __LINE__);
        goto err_out;
    }

    ty_cJSON_AddItemToObject(p_root, "queryJson", p_sub_root);
    if (req_param != NULL) {
        param = (TY_MENU_PARAM_LANG_LIST_1_0_S *)req_param;
        ty_cJSON_AddStringToObject(p_sub_root, "lang",      param->lang);
        ty_cJSON_AddNumberToObject(p_sub_root, "pageNo",    param->pageNo);
        ty_cJSON_AddNumberToObject(p_sub_root, "pageSize",  param->pageSize);
    }
    else {
        ty_cJSON_AddStringToObject(p_sub_root, "lang",      "en");
        ty_cJSON_AddNumberToObject(p_sub_root, "pageNo",    0);
        ty_cJSON_AddNumberToObject(p_sub_root, "pageSize",  500);
    }
    p_out = ty_cJSON_PrintUnformatted(p_root);
    if (NULL == p_out) {
        PR_ERR("%s-%d: ty_cJSON_PrintUnformatted failed", __func__, __LINE__);
    }
    else {
        *pp_out = p_out;
        op_ret = OPRT_OK;
    }

    err_out:
    if (p_root)
        ty_cJSON_Delete(p_root);
#endif
    return op_ret;

}

OPERATE_RET ty_cloud_recipe_menu_get_1_0_param_gen(IN VOID *req_param, OUT CHAR_T **pp_out)
{
    OPERATE_RET op_ret = OPRT_INVALID_PARM;
#ifdef CR_REQ_PARAMM_JSON
    op_ret = _cp_cr_str((CHAR_T *)req_param, pp_out);
#else
    ty_cJSON *p_root = NULL;
    CHAR_T *p_out = NULL;
    TY_MENU_PARAM_GET_1_0_S *param = NULL;
    CHAR_T menuid_str[64] = {0};

    if (req_param != NULL) {
        param = (TY_MENU_PARAM_GET_1_0_S *)req_param;
        p_root = ty_cJSON_CreateObject();
        if (NULL == p_root) {
            PR_ERR("%s-%d: p_root is null!", __func__, __LINE__);
            return op_ret;
        }
        memset(menuid_str, 0, SIZEOF(menuid_str));
        snprintf(menuid_str, SIZEOF(menuid_str), "%lu", param->menuId);
        ty_cJSON_AddStringToObject(p_root, "lang",      param->lang);
        ty_cJSON_AddStringToObject(p_root, "menuId",    menuid_str);
        p_out = ty_cJSON_PrintUnformatted(p_root);
        ty_cJSON_Delete(p_root);
        if (NULL == p_out) {
            PR_ERR("%s-%d: ty_cJSON_PrintUnformatted failed", __func__, __LINE__);
            return op_ret;
        }
        *pp_out = p_out;
        op_ret = OPRT_OK;
    }
#endif
    return op_ret;

}

OPERATE_RET ty_cloud_recipe_menu_custom_lang_list_1_0_param_gen(IN VOID *req_param, OUT CHAR_T **pp_out)
{
    OPERATE_RET op_ret = OPRT_INVALID_PARM;
#ifdef CR_REQ_PARAMM_JSON
    op_ret = _cp_cr_str((CHAR_T *)req_param, pp_out);
#else
    ty_cJSON *p_root = NULL;
    ty_cJSON *p_sub_root = NULL;
    CHAR_T *p_out = NULL;
    TY_MENU_PARAM_CUSTOM_LANG_LIST_1_0_S *param = NULL;

    if (req_param != NULL) {
        param = (TY_MENU_PARAM_CUSTOM_LANG_LIST_1_0_S *)req_param;
        p_root = ty_cJSON_CreateObject();
        if (NULL == p_root) {
            PR_ERR("%s-%d: p_root is null!", __func__, __LINE__);
            goto err_out;
        }
        p_sub_root = ty_cJSON_CreateObject();
        if (NULL == p_sub_root) {
            PR_ERR("%s-%d: p_sub_root is null!", __func__, __LINE__);
            goto err_out;
        }
        
        ty_cJSON_AddItemToObject(p_root, "queryJson", p_sub_root);
        ty_cJSON_AddStringToObject(p_sub_root, "lang",      param->lang);
        ty_cJSON_AddNumberToObject(p_sub_root, "pageNo",    param->pageNo);
        ty_cJSON_AddNumberToObject(p_sub_root, "pageSize",  param->pageSize);
        p_out = ty_cJSON_PrintUnformatted(p_root);
        if (NULL == p_out) {
            PR_ERR("%s-%d: ty_cJSON_PrintUnformatted failed", __func__, __LINE__);
        }
        else {
            *pp_out = p_out;
            op_ret = OPRT_OK;
        }
    }

    err_out:
    if (p_root)
        ty_cJSON_Delete(p_root);
#endif
    return op_ret;

}

OPERATE_RET ty_cloud_recipe_menu_diy_add_2_0_param_gen(IN VOID *req_param, OUT CHAR_T **pp_out)
{
    OPERATE_RET op_ret = OPRT_INVALID_PARM;
#ifdef CR_REQ_PARAMM_JSON
    op_ret = _cp_cr_str((CHAR_T *)req_param, pp_out);
#else
    ty_cJSON *p_root = NULL;
    CHAR_T *p_out = NULL;
    TY_MENU_PARAM_DIY_ADD_2_0_S *param = NULL;

    if (req_param != NULL) {
        param = (TY_MENU_PARAM_DIY_ADD_2_0_S *)req_param;

    }
#endif
    return op_ret;

}

OPERATE_RET ty_cloud_recipe_menu_diy_update_2_0_param_gen(IN VOID *req_param, OUT CHAR_T **pp_out)
{
    OPERATE_RET op_ret = OPRT_INVALID_PARM;
#ifdef CR_REQ_PARAMM_JSON
    op_ret = _cp_cr_str((CHAR_T *)req_param, pp_out);
#else
    ty_cJSON *p_root = NULL;
    CHAR_T *p_out = NULL;
    TY_MENU_PARAM_DIY_UPDATE_2_0_S *param = NULL;

    if (req_param != NULL) {
        param = (TY_MENU_PARAM_DIY_UPDATE_2_0_S *)req_param;

    }
#endif
    return op_ret;

}

OPERATE_RET ty_cloud_recipe_menu_diy_delete_1_0_param_gen(IN VOID *req_param, OUT CHAR_T **pp_out)
{
    OPERATE_RET op_ret = OPRT_INVALID_PARM;
#ifdef CR_REQ_PARAMM_JSON
    op_ret = _cp_cr_str((CHAR_T *)req_param, pp_out);
#else
    ty_cJSON *p_root = NULL;
    CHAR_T *p_out = NULL;
    TY_MENU_PARAM_DIY_DELETE_1_0_S *param = NULL;

    if (req_param != NULL) {
        param = (TY_MENU_PARAM_DIY_DELETE_1_0_S *)req_param;

    }
#endif
    return op_ret;

}

OPERATE_RET ty_cloud_recipe_menu_star_list_2_0_param_gen(IN VOID *req_param, OUT CHAR_T **pp_out)
{
    OPERATE_RET op_ret = OPRT_INVALID_PARM;
#ifdef CR_REQ_PARAMM_JSON
    op_ret = _cp_cr_str((CHAR_T *)req_param, pp_out);
#else
    ty_cJSON *p_root = NULL;
    CHAR_T *p_out = NULL;
    TY_MENU_PARAM_STAR_LIST_2_0_S *param = NULL;

    if (req_param != NULL) {
        param = (TY_MENU_PARAM_STAR_LIST_2_0_S *)req_param;

    }
#endif
    return op_ret;

}

OPERATE_RET ty_cloud_recipe_menu_star_add_1_0_param_gen(IN VOID *req_param, OUT CHAR_T **pp_out)
{
    OPERATE_RET op_ret = OPRT_INVALID_PARM;
#ifdef CR_REQ_PARAMM_JSON
    op_ret = _cp_cr_str((CHAR_T *)req_param, pp_out);
#else
    ty_cJSON *p_root = NULL;
    CHAR_T *p_out = NULL;
    TY_MENU_PARAM_STAR_ADD_1_0_S *param = NULL;

    if (req_param != NULL) {
        param = (TY_MENU_PARAM_STAR_ADD_1_0_S *)req_param;

    }
#endif
    return op_ret;

}

OPERATE_RET ty_cloud_recipe_menu_star_delete_1_0_param_gen(IN VOID *req_param, OUT CHAR_T **pp_out)
{
    OPERATE_RET op_ret = OPRT_INVALID_PARM;
#ifdef CR_REQ_PARAMM_JSON
    op_ret = _cp_cr_str((CHAR_T *)req_param, pp_out);
#else
    ty_cJSON *p_root = NULL;
    CHAR_T *p_out = NULL;
    TY_MENU_PARAM_STAR_DELETE_1_0_S *param = NULL;

    if (req_param != NULL) {
        param = (TY_MENU_PARAM_STAR_DELETE_1_0_S *)req_param;

    }
#endif
    return op_ret;

}

OPERATE_RET ty_cloud_recipe_menu_star_check_1_0_param_gen(IN VOID *req_param, OUT CHAR_T **pp_out)
{
    OPERATE_RET op_ret = OPRT_INVALID_PARM;
#ifdef CR_REQ_PARAMM_JSON
    op_ret = _cp_cr_str((CHAR_T *)req_param, pp_out);
#else
    ty_cJSON *p_root = NULL;
    CHAR_T *p_out = NULL;
    TY_MENU_PARAM_STAR_CHECK_1_0_S *param = NULL;

    if (req_param != NULL) {
        param = (TY_MENU_PARAM_STAR_CHECK_1_0_S *)req_param;

    }
#endif
    return op_ret;

}

OPERATE_RET ty_cloud_recipe_menu_star_count_1_0_param_gen(IN VOID *req_param, OUT CHAR_T **pp_out)
{
    OPERATE_RET op_ret = OPRT_INVALID_PARM;
#ifdef CR_REQ_PARAMM_JSON
    op_ret = _cp_cr_str((CHAR_T *)req_param, pp_out);
#else
    ty_cJSON *p_root = NULL;
    CHAR_T *p_out = NULL;
    TY_MENU_PARAM_STAR_COUNT_1_0_S *param = NULL;

    if (req_param != NULL) {
        param = (TY_MENU_PARAM_STAR_COUNT_1_0_S *)req_param;

    }
#endif
    return op_ret;

}

OPERATE_RET ty_cloud_recipe_menu_score_add_1_0_param_gen(IN VOID *req_param, OUT CHAR_T **pp_out)
{
    OPERATE_RET op_ret = OPRT_INVALID_PARM;
#ifdef CR_REQ_PARAMM_JSON
    op_ret = _cp_cr_str((CHAR_T *)req_param, pp_out);
#else
    ty_cJSON *p_root = NULL;
    CHAR_T *p_out = NULL;
    TY_MENU_PARAM_SCORE_ADD_1_0_S *param = NULL;

    if (req_param != NULL) {
        param = (TY_MENU_PARAM_SCORE_ADD_1_0_S *)req_param;

    }
#endif
    return op_ret;

}

OPERATE_RET ty_cloud_recipe_menu_banner_lsit_1_0_param_gen(IN VOID *req_param, OUT CHAR_T **pp_out)
{
    OPERATE_RET op_ret = OPRT_INVALID_PARM;
#ifdef CR_REQ_PARAMM_JSON
    op_ret = _cp_cr_str((CHAR_T *)req_param, pp_out);
#else
    ty_cJSON *p_root = NULL;
    CHAR_T *p_out = NULL;
    TY_MENU_PARAM_BANNER_LIST_1_0_S *param = NULL;

    if (req_param != NULL) {
        param = (TY_MENU_PARAM_BANNER_LIST_1_0_S *)req_param;

    }
#endif
    return op_ret;

}

OPERATE_RET ty_cloud_recipe_cookbook_all_lang_1_0_param_gen(IN VOID *req_param, OUT CHAR_T **pp_out)
{
    CHAR_T *p_out = NULL;

    *pp_out = p_out;
    return OPRT_OK;

}

OPERATE_RET ty_cloud_recipe_menu_query_model_list_1_0_param_gen(IN VOID *req_param, OUT CHAR_T **pp_out)
{
    OPERATE_RET op_ret = OPRT_INVALID_PARM;
#ifdef CR_REQ_PARAMM_JSON
    op_ret = _cp_cr_str((CHAR_T *)req_param, pp_out);
#else
    ty_cJSON *p_root = NULL;
    CHAR_T *p_out = NULL;
    TY_MENU_PARAM_QUERY_MODEL_LIST_1_0_S *param = NULL;

    if (req_param != NULL) {
        param = (TY_MENU_PARAM_QUERY_MODEL_LIST_1_0_S *)req_param;

    }
#endif
    return op_ret;

}

OPERATE_RET ty_cloud_recipe_menu_search_condition_1_0_param_gen(IN VOID *req_param, OUT CHAR_T **pp_out)
{
    OPERATE_RET op_ret = OPRT_INVALID_PARM;
#ifdef CR_REQ_PARAMM_JSON
    op_ret = _cp_cr_str((CHAR_T *)req_param, pp_out);
#else
    ty_cJSON *p_root = NULL;
    CHAR_T *p_out = NULL;
    TY_MENU_PARAM_SEARCH_CONDITION_1_0_S *param = NULL;

    if (req_param != NULL) {
        param = (TY_MENU_PARAM_SEARCH_CONDITION_1_0_S *)req_param;

    }
#endif
    return op_ret;

}

OPERATE_RET ty_cloud_recipe_menu_sync_list_1_0_param_gen(IN VOID *req_param, OUT CHAR_T **pp_out)
{
    OPERATE_RET op_ret = OPRT_INVALID_PARM;
#ifdef CR_REQ_PARAMM_JSON
    op_ret = _cp_cr_str((CHAR_T *)req_param, pp_out);
#else
    ty_cJSON *p_root = NULL;
    CHAR_T *p_out = NULL;
    TY_MENU_PARAM_SYNC_LIST_1_0_S *param = NULL;

    if (req_param != NULL) {
        param = (TY_MENU_PARAM_SYNC_LIST_1_0_S *)req_param;

    }
#endif
    return op_ret;

}

OPERATE_RET ty_cloud_recipe_menu_sync_details_1_0_param_gen(IN VOID *req_param, OUT CHAR_T **pp_out)
{
    OPERATE_RET op_ret = OPRT_INVALID_PARM;
#ifdef CR_REQ_PARAMM_JSON
    op_ret = _cp_cr_str((CHAR_T *)req_param, pp_out);
#else
    ty_cJSON *p_root = NULL;
    CHAR_T *p_out = NULL;
    TY_MENU_PARAM_SYNC_DETAILS_1_0_S *param = NULL;

    if (req_param != NULL) {
        param = (TY_MENU_PARAM_SYNC_DETAILS_1_0_S *)req_param;

    }
#endif
    return op_ret;

}
