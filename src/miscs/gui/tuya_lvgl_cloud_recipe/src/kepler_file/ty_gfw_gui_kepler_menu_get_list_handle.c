/**
 * @file ty_gfw_gui_kepler_menu_get_list_handle.c
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
#include "gw_intf.h"
#include "ty_cJSON.h"
#include "uni_log.h"
#include "tal_mutex.h"
#include "ty_gfw_gui_kepler_menu_download_cloud.h"
#include "ty_gfw_gui_kepler_menu_get_list_handle.h"
#include "app_ipc_command.h"
#include "tuya_app_gui_gw_core0.h"

/***********************************************************
*************************micro define***********************
***********************************************************/
/*
嵌入式设备因资源紧张，支持标准的 TLS 有困难，但因业务需要需访问标准 TLS 的服务（往往是第三方服务），
嵌入式设备以TLS-PSK 的形式接入去访问标准 TLS 服务
需要修改 HTTP Request URI,
在原本的 URI 前面加上 /dst=,比如原来的 URI 是 :
https://images.tuyacn.com/smart/product/ability/form/file/1730717133390440e772a.json，
那新的 URI 是:
https://storage-proxy.tuyacn.com:7779/dst=https://images.tuyacn.com/smart/product/ability/form/file/1730717133390440e772a.json
各区代理服务器地址参考：
https://wiki.tuya-inc.com:7799/page/1422839659087540278
*/
//云食谱各区域代理服务器信息
typedef struct {
    CONST CHAR_T *region;
    CONST CHAR_T *proxy;
} TY_CLOUD_RECIPE_PROXY_INF_S;

STATIC TY_CLOUD_RECIPE_PROXY_INF_S ty_cloud_recipe_proxy_array[] = {
    {"AY",         "storage-proxy.tuyacn.com"},                         //中国杭州
    {"SH",         "storage-proxy.tuyacn.com"},                         //中国上海
    {"QD",         "storage-proxy.tuyacn.com"},                         //中国青岛
    {"NX",         "storage-proxy.tuyacn.com"},                         //中国宁夏
    {"A1",         "storage-proxy.tuyacn.com"},                         //中国联通
    {"AZ",         "storage-proxy.tuyaus.com"},                         //美国西海岸
    {"UE",         "storage-proxy-ueaz.iotbing.com"},                   //美国东部
    {"EU",         "storage-proxy.tuyaeu.com"},                         //欧洲
    {"WE",         "storage-proxy-we.iotbing.com"},                     //欧洲西部
    {"IN",         "storage-proxy.tuyain.com"}                          //印度孟买
};

STATIC CLOUD_RECIPE_DL_CB CloudRecipeDl_cb = NULL;

STATIC CHAR_T *ty_cloud_recipe_menu_get_region_psk_url(ty_cJSON *json)
{
    CHAR_T *psk_url = NULL;
    CONST CHAR_T *region = get_gw_region();
    SIZE_T len = 0;
    CHAR_T *tls_url = NULL;

    if (json == NULL) {
        PR_ERR("null tls url !");
        return NULL;
    }

    tls_url = json->valuestring;
    if (tls_url == NULL || strlen(tls_url) == 0) {
        PR_ERR("null tls url !");
        return NULL;
    }

    if (TUYA_SECURITY_LEVEL == TUYA_SL_0) {
         // 加psk代理
        if (strstr(tls_url, ":7779/dst=")) {
            len = strlen(tls_url) + 1;
            psk_url = (CHAR_T *)cr_parser_malloc(len);
            if (psk_url != NULL) {
                memset(psk_url, 0, len);
                strcpy(psk_url, tls_url);
            }
            else {
                PR_ERR("malloc fail when tls url '%s' !", region, tls_url);
            }
            return psk_url;
        }

        for (UINT16_T i = 0; i < CNTSOF(ty_cloud_recipe_proxy_array); i++) {
            if (strcmp(ty_cloud_recipe_proxy_array[i].region, region) == 0) {
                len = strlen("https://") + strlen(ty_cloud_recipe_proxy_array[i].proxy) + strlen(":7779/dst=") + strlen(tls_url) + 1;
                psk_url = (CHAR_T *)cr_parser_malloc(len);
                if (psk_url != NULL) {
                    memset(psk_url, 0, len);
                    snprintf(psk_url, len, "https://%s:7779/dst=%s", ty_cloud_recipe_proxy_array[i].proxy, tls_url);
                }
                break;
            }
        }
    }
    else {
        // 不加psk代理
        len = strlen(tls_url) + 1;
        psk_url = (CHAR_T *)cr_parser_malloc(len);
        if (psk_url != NULL) {
            memset(psk_url, 0, len);
            strcpy(psk_url, tls_url);
        }
        else {
            PR_ERR("malloc fail when tls url '%s' !", region, tls_url);
        }
    }

    if (psk_url){
        //PR_NOTICE("tls url '%s' to psk url '%s'", tls_url, psk_url);
    }
    else {
        PR_ERR("can not found region '%s' proxy of tls url '%s' !", region, tls_url);
    }
    return psk_url;
}

STATIC OPERATE_RET ty_copy_json_str(CHAR_T **des, ty_cJSON *json, const char *param_name)
{
    ty_cJSON *obj_json = ty_cJSON_GetObjectItem(json, param_name);

    if (obj_json == NULL) {
        PR_WARN("json param '%s' not exist !", param_name);
        *des = NULL;
    }
    else if (strlen(obj_json->valuestring) == 0) {
        PR_WARN("json param '%s' str value is empty !", param_name);
        *des = NULL;
    }
    else {
        *des = (CHAR_T *)cr_parser_malloc(strlen(obj_json->valuestring) + 1);
        if (*des == NULL) {
            PR_ERR("ty copy json str malloc fail at str '%s' !", obj_json->valuestring);
            return OPRT_MALLOC_FAILED;
        }
        strcpy(*des, obj_json->valuestring);
    }
    return OPRT_OK;
}

STATIC VOID ty_copy_json_bool(BOOL_T *des, ty_cJSON *json, const char *param_name)
{
    ty_cJSON *obj_json = ty_cJSON_GetObjectItem(json, param_name);

    if (obj_json != NULL) {
        if (ty_cJSON_IsTrue(obj_json))
            *des = TRUE;
        else
            *des = FALSE;
    }
    else {
        PR_WARN("json param '%s' not exist !", param_name);
        *des = FALSE;
    }
}

STATIC VOID ty_copy_json_byte(UCHAR_T *des, ty_cJSON *json, const char *param_name)
{
    ty_cJSON *obj_json = ty_cJSON_GetObjectItem(json, param_name);

    if (obj_json == NULL) {
        PR_WARN("json param '%s' not exist !", param_name);
    }
    else if (ty_cJSON_IsNumber(obj_json))
        *des = obj_json->valueint;
}

STATIC VOID ty_copy_json_ulong(ULONG_T *des, ty_cJSON *json, const char *param_name)
{
    ty_cJSON *obj_json = ty_cJSON_GetObjectItem(json, param_name);

    if (obj_json == NULL) {
        PR_WARN("json param '%s' not exist !", param_name);
    }
    else if (ty_cJSON_IsNumber(obj_json))
        *des = obj_json->valueint;
}

STATIC VOID ty_copy_json_uint(UINT_T *des, ty_cJSON *json, const char *param_name)
{
    ty_cJSON *obj_json = ty_cJSON_GetObjectItem(json, param_name);

    if (obj_json == NULL) {
        PR_WARN("json param '%s' not exist !", param_name);
    }
    else if (ty_cJSON_IsNumber(obj_json))
        *des = obj_json->valueint;
}

STATIC VOID ty_copy_json_uint64(UINT64_T *des, ty_cJSON *json, const char *param_name)
{
    ty_cJSON *obj_json = ty_cJSON_GetObjectItem(json, param_name);

    if (obj_json == NULL) {
        PR_WARN("json param '%s' not exist !", param_name);
    }
    else if (ty_cJSON_IsNumber(obj_json))
        *des = (UINT64_T)obj_json->valuedouble;
}

STATIC VOID ty_copy_json_double(DOUBLE_T *des, ty_cJSON *json, const char *param_name)
{
    ty_cJSON *obj_json = ty_cJSON_GetObjectItem(json, param_name);

    if (obj_json == NULL) {
        PR_WARN("json param '%s' not exist !", param_name);
    }
    else if (ty_cJSON_IsNumber(obj_json))
        *des = obj_json->valuedouble;
}

STATIC OPERATE_RET __category_common_add_langinfo_list(ty_cJSON *array, struct dl_list *mlistHead_langinfo)
{
    OPERATE_RET rt = OPRT_OK;
    ty_cJSON *p_langinfo_item = NULL;
    UCHAR_T num = 0, i = 0;
    category_common_langinfo_node_s *langinfo_node = NULL;

    dl_list_init(mlistHead_langinfo);
    if (array == NULL) {
        PR_ERR("[%s][%d] null array !\r\n", __FUNCTION__, __LINE__);
        //rt = OPRT_INVALID_PARM;
        return rt;
    }

    num = ty_cJSON_GetArraySize(array);
    if (num <= 0) {
        PR_WARN("%s-%d:empty langinfo !!", __func__, __LINE__);
    }
    for(i = 0; i < num; i++) {
        p_langinfo_item = ty_cJSON_GetArrayItem(array, i);
        if (p_langinfo_item) {
            langinfo_node = (category_common_langinfo_node_s *)cr_parser_malloc(SIZEOF(category_common_langinfo_node_s));
            if (langinfo_node == NULL) {
                PR_ERR("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
                rt = OPRT_MALLOC_FAILED;
                break;
            }
            memset(langinfo_node, 0, SIZEOF(category_common_langinfo_node_s));
            //name
            TUYA_CALL_ERR_RETURN(ty_copy_json_str(&langinfo_node->name, p_langinfo_item, "name"));
            //lang
            TUYA_CALL_ERR_RETURN(ty_copy_json_str(&langinfo_node->lang, p_langinfo_item, "lang"));
            dl_list_add(mlistHead_langinfo, &langinfo_node->m_list);
        }
    }
    return rt;
}

STATIC OPERATE_RET __category_list_2_0_add_subMenuCategorie_list(ty_cJSON *array, struct dl_list *mlistHead_subMenuCategorie)
{
    OPERATE_RET rt = OPRT_OK;
    ty_cJSON *p_subMenuCategorie_item = NULL;
    ty_cJSON *p_langinfo_array = NULL;
    UCHAR_T num = 0, i = 0;
    CHAR_T *psk_url = NULL;
    category_list_2_0_subMenuCategories_node_s *subMenuCategorie_node = NULL;

    dl_list_init(mlistHead_subMenuCategorie);

    if (array == NULL) {
        PR_ERR("[%s][%d] null array !\r\n", __FUNCTION__, __LINE__);
        //rt = OPRT_INVALID_PARM;
        return rt;
    }

    num = ty_cJSON_GetArraySize(array);
    if (num <= 0) {
        PR_WARN("%s-%d:empty subMenuCategorie !!", __func__, __LINE__);
    }
    for(i = 0; i < num; i++) {
        p_subMenuCategorie_item = ty_cJSON_GetArrayItem(array, i);
        if (p_subMenuCategorie_item) {
            subMenuCategorie_node = (category_list_2_0_subMenuCategories_node_s *)cr_parser_malloc(SIZEOF(category_list_2_0_subMenuCategories_node_s));
            if (subMenuCategorie_node == NULL) {
                PR_ERR("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
                rt = OPRT_MALLOC_FAILED;
                break;
            }
            memset(subMenuCategorie_node, 0, SIZEOF(category_list_2_0_subMenuCategories_node_s));
            //id
            ty_copy_json_ulong(&subMenuCategorie_node->id, p_subMenuCategorie_item, "id");
            //name
            TUYA_CALL_ERR_RETURN(ty_copy_json_str(&subMenuCategorie_node->name, p_subMenuCategorie_item, "name"));
            //imageUrl
            psk_url = ty_cloud_recipe_menu_get_region_psk_url(ty_cJSON_GetObjectItem(p_subMenuCategorie_item, "imageUrl"));
            if (psk_url != NULL) {
                subMenuCategorie_node->imageUrl = psk_url;
                rt = CloudRecipeDl_cb(&subMenuCategorie_node->imageSize, &subMenuCategorie_node->imageBuff, psk_url);
                if (rt != OPRT_OK) {
                    PR_ERR("[%s][%d] dl '%s' fail...\r\n", __FUNCTION__, __LINE__, psk_url);
                    break;
                }
            }
            ty_copy_json_uint64(&subMenuCategorie_node->gmtCreate, p_subMenuCategorie_item, "gmtCreate");
            ty_copy_json_uint64(&subMenuCategorie_node->gmtModified, p_subMenuCategorie_item, "gmtModified");
            //level
            ty_copy_json_uint(&subMenuCategorie_node->level, p_subMenuCategorie_item, "level");
            ty_copy_json_ulong(&subMenuCategorie_node->parentId, p_subMenuCategorie_item, "parentId");
            p_langinfo_array = ty_cJSON_GetObjectItem(p_subMenuCategorie_item, "langInfo");
            rt = __category_common_add_langinfo_list(p_langinfo_array, &subMenuCategorie_node->mlistHead_langinfo);
            if (rt != OPRT_OK)
                break;
            dl_list_add(mlistHead_subMenuCategorie, &subMenuCategorie_node->m_list);
        }
    }
    return rt;
}

STATIC OPERATE_RET __get_1_0_add_mainImgs_list(ty_cJSON *array, struct dl_list *mlistHead_mainImgs)
{
    OPERATE_RET rt = OPRT_OK;
    UCHAR_T num = 0, i = 0;
    ty_cJSON *p_mainImgs_item = NULL;
    get_1_0_mainImgs_node_s *mainImgs_node = NULL;
    CHAR_T *psk_url = NULL;

    dl_list_init(mlistHead_mainImgs);

    if (array == NULL) {
        PR_ERR("[%s][%d] null array !\r\n", __FUNCTION__, __LINE__);
        //rt = OPRT_INVALID_PARM;
        return rt;
    }

    num = ty_cJSON_GetArraySize(array);
    for (i = 0; i < num; i++) {
        p_mainImgs_item = ty_cJSON_GetArrayItem(array, i);
        mainImgs_node = (get_1_0_mainImgs_node_s *)cr_parser_malloc(SIZEOF(get_1_0_mainImgs_node_s));
        if (mainImgs_node == NULL) {
            PR_ERR("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
            rt = OPRT_COM_ERROR;
            break;
        }
        psk_url = ty_cloud_recipe_menu_get_region_psk_url(p_mainImgs_item);
        if (psk_url != NULL) {
            mainImgs_node->imageUrl = psk_url;
            rt = CloudRecipeDl_cb(&mainImgs_node->imageSize, &mainImgs_node->imageBuff, psk_url);
            if (rt != OPRT_OK) {
                PR_ERR("[%s][%d] dl '%s' fail...\r\n", __FUNCTION__, __LINE__, psk_url);
                break;
            }
        }
        dl_list_add(mlistHead_mainImgs, &mainImgs_node->m_list);
    }

    return rt;
}

STATIC OPERATE_RET __get_1_0_add_langInfos_list(ty_cJSON *array, struct dl_list *mlistHead_langInfos)
{
    OPERATE_RET rt = OPRT_OK;
    UCHAR_T num = 0, i = 0;
    ty_cJSON *p_langInfo_item = NULL;
    get_1_0_langInfos_node_s *langInfos_node = NULL;
    CHAR_T *psk_url = NULL;

    dl_list_init(mlistHead_langInfos);
    if (array == NULL) {
        PR_ERR("[%s][%d] null array !\r\n", __FUNCTION__, __LINE__);
        //rt = OPRT_INVALID_PARM;
        return rt;
    }

    num = ty_cJSON_GetArraySize(array);
    for (i = 0; i < num; i++) {
        p_langInfo_item = ty_cJSON_GetArrayItem(array, i);
        langInfos_node = (get_1_0_langInfos_node_s *)cr_parser_malloc(SIZEOF(get_1_0_langInfos_node_s));
        if (langInfos_node == NULL) {
            PR_ERR("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
            rt = OPRT_COM_ERROR;
            break;
        }
        //lang
        TUYA_CALL_ERR_RETURN(ty_copy_json_str(&langInfos_node->lang, p_langInfo_item, "lang"));
        //desc
        TUYA_CALL_ERR_RETURN(ty_copy_json_str(&langInfos_node->desc, p_langInfo_item, "desc"));
        //stepImg
        psk_url = ty_cloud_recipe_menu_get_region_psk_url(ty_cJSON_GetObjectItem(p_langInfo_item, "stepImg"));
        if (psk_url != NULL) {
            langInfos_node->stepImgUrl = psk_url;
            rt = CloudRecipeDl_cb(&langInfos_node->stepImgSize, &langInfos_node->stepImgBuff, psk_url);
            if (rt != OPRT_OK) {
                PR_ERR("[%s][%d] dl '%s' fail...\r\n", __FUNCTION__, __LINE__, psk_url);
                break;
            }
        }
        //origStepImg
        psk_url = ty_cloud_recipe_menu_get_region_psk_url(ty_cJSON_GetObjectItem(p_langInfo_item, "origStepImg"));
        if (psk_url != NULL) {
            langInfos_node->origStepImgUrl = psk_url;
            rt = CloudRecipeDl_cb(&langInfos_node->origStepImgSize, &langInfos_node->origStepImgBuff, psk_url);
            if (rt != OPRT_OK) {
                PR_ERR("[%s][%d] dl '%s' fail...\r\n", __FUNCTION__, __LINE__, psk_url);
                break;
            }
        }
        dl_list_add(mlistHead_langInfos, &langInfos_node->m_list);
    }

    return rt;
}

STATIC OPERATE_RET __get_1_0_add_cookArgs_list(ty_cJSON *array, struct dl_list *mlistHead_cookArgs)
{
    OPERATE_RET rt = OPRT_OK;
    UCHAR_T num = 0, i = 0;
    ty_cJSON *p_array_item = NULL;
    get_1_0_cookArgs_node_s *cookArgs_node = NULL;

    dl_list_init(mlistHead_cookArgs);
    if (array == NULL) {
        PR_ERR("[%s][%d] null array !\r\n", __FUNCTION__, __LINE__);
        //rt = OPRT_INVALID_PARM;
        return rt;
    }

    num = ty_cJSON_GetArraySize(array);
    for (i = 0; i < num; i++) {
        p_array_item = ty_cJSON_GetArrayItem(array, i);
        cookArgs_node = (get_1_0_cookArgs_node_s *)cr_parser_malloc(SIZEOF(get_1_0_cookArgs_node_s));
        if (cookArgs_node == NULL) {
            PR_ERR("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
            rt = OPRT_COM_ERROR;
            break;
        }
        //dpCode
        TUYA_CALL_ERR_RETURN(ty_copy_json_str(&cookArgs_node->dpCode, p_array_item, "dpCode"));
        //dpValue
        TUYA_CALL_ERR_RETURN(ty_copy_json_str(&cookArgs_node->dpValue, p_array_item, "dpValue"));
        //isTypeValue
        ty_copy_json_bool(&cookArgs_node->isTypeValue, p_array_item, "isTypeValue");
        dl_list_add(mlistHead_cookArgs, &cookArgs_node->m_list);
    }

    return rt;
}

STATIC OPERATE_RET __get_1_0_add_allergens_list(ty_cJSON *array, struct dl_list *mlistHead_allergens)
{
    OPERATE_RET rt = OPRT_OK;
    UCHAR_T num = 0, i = 0;
    ty_cJSON *p_array_item = NULL;
    get_1_0_allergens_node_s *allergens_node = NULL;

    dl_list_init(mlistHead_allergens);
    if (array == NULL) {
        PR_ERR("[%s][%d] null array !\r\n", __FUNCTION__, __LINE__);
        //rt = OPRT_INVALID_PARM;
        return rt;
    }

    num = ty_cJSON_GetArraySize(array);
    for (i = 0; i < num; i++) {
        p_array_item = ty_cJSON_GetArrayItem(array, i);
        allergens_node = (get_1_0_allergens_node_s *)cr_parser_malloc(SIZEOF(get_1_0_allergens_node_s));
        if (allergens_node == NULL) {
            PR_ERR("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
            rt = OPRT_COM_ERROR;
            break;
        }
        ty_copy_json_ulong(&allergens_node->id, p_array_item, "id");
        TUYA_CALL_ERR_RETURN(ty_copy_json_str(&allergens_node->name, p_array_item, "name"));
        dl_list_add(mlistHead_allergens, &allergens_node->m_list);
    }

    return rt;
}

STATIC OPERATE_RET __get_1_0_add_menuStepInfoVOList_list(ty_cJSON *array, struct dl_list *mlistHead_menuStepInfoVOList)
{
    OPERATE_RET rt = OPRT_OK;
    UCHAR_T num = 0, i = 0;
    ty_cJSON *p_array_item = NULL;
    get_1_0_menuStepInfoVOList_node_s *menuStepInfoVOList_node = NULL;

    dl_list_init(mlistHead_menuStepInfoVOList);
    if (array == NULL) {
        PR_ERR("[%s][%d] null array !\r\n", __FUNCTION__, __LINE__);
        //rt = OPRT_INVALID_PARM;
        return rt;
    }

    num = ty_cJSON_GetArraySize(array);
    for (i = 0; i < num; i++) {
        p_array_item = ty_cJSON_GetArrayItem(array, i);
        menuStepInfoVOList_node = (get_1_0_menuStepInfoVOList_node_s *)cr_parser_malloc(SIZEOF(get_1_0_menuStepInfoVOList_node_s));
        if (menuStepInfoVOList_node == NULL) {
            PR_ERR("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
            rt = OPRT_COM_ERROR;
            break;
        }
        ty_copy_json_ulong(&menuStepInfoVOList_node->id, p_array_item, "id");
        ty_copy_json_ulong(&menuStepInfoVOList_node->menuId, p_array_item, "menuId");
        ty_copy_json_uint(&menuStepInfoVOList_node->step, p_array_item, "step");
        ty_copy_json_uint64(&menuStepInfoVOList_node->gmtCreate, p_array_item, "gmtCreate");
        ty_copy_json_uint64(&menuStepInfoVOList_node->gmtModified, p_array_item, "gmtModified");
        rt = __get_1_0_add_langInfos_list(ty_cJSON_GetObjectItem(p_array_item, "langInfos"), &menuStepInfoVOList_node->mlistHead_langInfos);
        if (rt != OPRT_OK)
            break;
        dl_list_add(mlistHead_menuStepInfoVOList, &menuStepInfoVOList_node->m_list);
    }

    return rt;
}

STATIC OPERATE_RET __get_1_0_add_foodInfoVOList_foodNutritionVOList_list(ty_cJSON *array, struct dl_list *mlistHead_foodNutritionVOList)
{
    OPERATE_RET rt = OPRT_OK;
    UCHAR_T num = 0, i = 0;
    ty_cJSON *p_array_item = NULL;
    get_1_0_foodNutritionVOList_node_s *foodNutritionVOList_node = NULL;

    dl_list_init(mlistHead_foodNutritionVOList);
    if (array == NULL) {
        PR_ERR("[%s][%d] null array !\r\n", __FUNCTION__, __LINE__);
        //rt = OPRT_INVALID_PARM;
        return rt;
    }

    num = ty_cJSON_GetArraySize(array);
    for (i = 0; i < num; i++) {
        p_array_item = ty_cJSON_GetArrayItem(array, i);
        foodNutritionVOList_node = (get_1_0_foodNutritionVOList_node_s *)cr_parser_malloc(SIZEOF(get_1_0_foodNutritionVOList_node_s));
        if (foodNutritionVOList_node == NULL) {
            PR_ERR("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
            rt = OPRT_COM_ERROR;
            break;
        }
        ty_copy_json_ulong(&foodNutritionVOList_node->id, p_array_item, "id");
        ty_copy_json_ulong(&foodNutritionVOList_node->foodId, p_array_item, "foodId");
        ty_copy_json_ulong(&foodNutritionVOList_node->nutritionId, p_array_item, "nutritionId");
        ty_copy_json_byte(&foodNutritionVOList_node->status, p_array_item, "status");
        TUYA_CALL_ERR_RETURN(ty_copy_json_str(&foodNutritionVOList_node->nutritionCode, p_array_item, "nutritionCode"));
        TUYA_CALL_ERR_RETURN(ty_copy_json_str(&foodNutritionVOList_node->value, p_array_item, "value"));
        dl_list_add(mlistHead_foodNutritionVOList, &foodNutritionVOList_node->m_list);
    }

    return rt;
}

STATIC OPERATE_RET __get_1_0_add_foodInfoVOList_menuFoodRelationVO(ty_cJSON *json, get_1_0_menuFoodRelationVO_s *menuFoodRelationVO)
{
    OPERATE_RET rt = OPRT_OK;

    ty_copy_json_ulong(&menuFoodRelationVO->id, json, "id");
    ty_copy_json_ulong(&menuFoodRelationVO->menuId, json, "menuId");
    ty_copy_json_ulong(&menuFoodRelationVO->foodId, json, "foodId");
    TUYA_CALL_ERR_RETURN(ty_copy_json_str(&menuFoodRelationVO->amount, json, "amount"));
    TUYA_CALL_ERR_RETURN(ty_copy_json_str(&menuFoodRelationVO->secAmount, json, "secAmount"));
    TUYA_CALL_ERR_RETURN(ty_copy_json_str(&menuFoodRelationVO->secUnit, json, "secUnit"));
    TUYA_CALL_ERR_RETURN(ty_copy_json_str(&menuFoodRelationVO->secUnitCode, json, "secUnitCode"));
    TUYA_CALL_ERR_RETURN(ty_copy_json_str(&menuFoodRelationVO->secUnitDesc, json, "secUnitDesc"));
    ty_copy_json_ulong(&menuFoodRelationVO->tagId, json, "tagId");
    TUYA_CALL_ERR_RETURN(ty_copy_json_str(&menuFoodRelationVO->tagName, json, "tagName"));
    ty_copy_json_uint(&menuFoodRelationVO->tagOrder, json, "tagOrder");
    ty_copy_json_uint(&menuFoodRelationVO->groupId, json, "groupId");
    ty_copy_json_uint(&menuFoodRelationVO->foodOrder, json, "foodOrder");
    ty_copy_json_byte(&menuFoodRelationVO->back, json, "back");
    ty_copy_json_byte(&menuFoodRelationVO->status, json, "status");
    rt = __get_1_0_add_langInfos_list(ty_cJSON_GetObjectItem(json, "langInfos"), &menuFoodRelationVO->mlistHead_langInfos);
    if (rt != OPRT_OK)
        return rt;
    ty_copy_json_uint64(&menuFoodRelationVO->gmtCreate, json, "gmtCreate");
    ty_copy_json_uint64(&menuFoodRelationVO->gmtModified, json, "gmtModified");
    return rt;
}

STATIC OPERATE_RET __get_1_0_add_cookStepInfoVOList_list(ty_cJSON *array, struct dl_list *mlistHead_cookStepInfoVOList)
{
    OPERATE_RET rt = OPRT_OK;
    UCHAR_T num = 0, i = 0;
    ty_cJSON *p_array_item = NULL;
    CHAR_T *psk_url = NULL;
    get_1_0_cookStepInfoVOList_node_s *cookStepInfoVOList_node = NULL;

    dl_list_init(mlistHead_cookStepInfoVOList);
    if (array == NULL) {
        PR_ERR("[%s][%d] null array !\r\n", __FUNCTION__, __LINE__);
        //rt = OPRT_INVALID_PARM;
        return rt;
    }

    num = ty_cJSON_GetArraySize(array);
    for (i = 0; i < num; i++) {
        p_array_item = ty_cJSON_GetArrayItem(array, i);
        cookStepInfoVOList_node = (get_1_0_cookStepInfoVOList_node_s *)cr_parser_malloc(SIZEOF(get_1_0_cookStepInfoVOList_node_s));
        if (cookStepInfoVOList_node == NULL) {
            PR_ERR("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
            rt = OPRT_COM_ERROR;
            break;
        }
        ty_copy_json_ulong(&cookStepInfoVOList_node->id, p_array_item, "id");
        ty_copy_json_ulong(&cookStepInfoVOList_node->menuId, p_array_item, "menuId");
        ty_copy_json_uint(&cookStepInfoVOList_node->isCookArgs, p_array_item, "isCookArgs");
        ty_copy_json_uint(&cookStepInfoVOList_node->step, p_array_item, "step");
        psk_url = ty_cloud_recipe_menu_get_region_psk_url(ty_cJSON_GetObjectItem(p_array_item, "stepImg"));
        if (psk_url != NULL) {
            cookStepInfoVOList_node->stepImgUrl = psk_url;
            rt = CloudRecipeDl_cb(&cookStepInfoVOList_node->stepImgSize, &cookStepInfoVOList_node->stepImgBuff, psk_url);
            if (rt != OPRT_OK) {
                PR_ERR("[%s][%d] dl '%s' fail...\r\n", __FUNCTION__, __LINE__, psk_url);
                break;
            }
        }
        TUYA_CALL_ERR_RETURN(ty_copy_json_str(&cookStepInfoVOList_node->finishCtrl, p_array_item, "finishCtrl"));
        //cookArgs
        rt = __get_1_0_add_cookArgs_list(ty_cJSON_GetObjectItem(p_array_item, "cookArgs"), &cookStepInfoVOList_node->mlistHead_cookArgs);
        if (rt != OPRT_OK)
            break;
        //langInfos
        rt = __get_1_0_add_langInfos_list(ty_cJSON_GetObjectItem(p_array_item, "langInfos"), &cookStepInfoVOList_node->mlistHead_langInfos);
        if (rt != OPRT_OK)
            break;
        ty_copy_json_uint64(&cookStepInfoVOList_node->gmtCreate, p_array_item, "gmtCreate");
        ty_copy_json_uint64(&cookStepInfoVOList_node->gmtModified, p_array_item, "gmtModified");
        dl_list_add(mlistHead_cookStepInfoVOList, &cookStepInfoVOList_node->m_list);
    }

    return rt;
}

STATIC OPERATE_RET __get_1_0_add_foodInfoVOList_list(ty_cJSON *array, struct dl_list *mlistHead_foodInfoVOList)
{
    OPERATE_RET rt = OPRT_OK;
    UCHAR_T num = 0, i = 0;
    ty_cJSON *p_array_item = NULL;
    CHAR_T *psk_url = NULL;
    get_1_0_foodInfoVOList_node_s *foodInfoVOList_node = NULL;

    dl_list_init(mlistHead_foodInfoVOList);

    if (array == NULL) {            //启食材库时才有
        PR_ERR("[%s][%d] null array !\r\n", __FUNCTION__, __LINE__);
        //rt = OPRT_INVALID_PARM;
        return rt;
    }

    num = ty_cJSON_GetArraySize(array);
    for (i = 0; i < num; i++) {
        p_array_item = ty_cJSON_GetArrayItem(array, i);
        foodInfoVOList_node = (get_1_0_foodInfoVOList_node_s *)cr_parser_malloc(SIZEOF(get_1_0_foodInfoVOList_node_s));
        if (foodInfoVOList_node == NULL) {
            PR_ERR("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
            rt = OPRT_COM_ERROR;
            break;
        }
        ty_copy_json_ulong(&foodInfoVOList_node->id, p_array_item, "id");
        TUYA_CALL_ERR_RETURN(ty_copy_json_str(&foodInfoVOList_node->name, p_array_item, "name"));
        TUYA_CALL_ERR_RETURN(ty_copy_json_str(&foodInfoVOList_node->desc, p_array_item, "desc"));
        TUYA_CALL_ERR_RETURN(ty_copy_json_str(&foodInfoVOList_node->lang, p_array_item, "lang"));
        ty_copy_json_uint64(&foodInfoVOList_node->gmtCreate, p_array_item, "gmtCreate");
        ty_copy_json_uint64(&foodInfoVOList_node->gmtModified, p_array_item, "gmtModified");
        psk_url = ty_cloud_recipe_menu_get_region_psk_url(ty_cJSON_GetObjectItem(p_array_item, "image"));
        if (psk_url != NULL) {
            foodInfoVOList_node->imageUrl = psk_url;
            rt = CloudRecipeDl_cb(&foodInfoVOList_node->imageSize, &foodInfoVOList_node->imageBuff, psk_url);
            if (rt != OPRT_OK) {
                PR_ERR("[%s][%d] dl '%s' fail...\r\n", __FUNCTION__, __LINE__, psk_url);
                break;
            }
        }
        //foodNutritionVOList:食材营养成
        rt = __get_1_0_add_foodInfoVOList_foodNutritionVOList_list(ty_cJSON_GetObjectItem(p_array_item, "foodNutritionVOList"), &foodInfoVOList_node->mlistHead_foodNutritionVOList);
        if (rt != OPRT_OK)
            break;
        //menuFoodRelationVO:关联食材信息
        rt = __get_1_0_add_foodInfoVOList_menuFoodRelationVO(ty_cJSON_GetObjectItem(p_array_item, "menuFoodRelationVO"), &foodInfoVOList_node->menuFoodRelationVO);
        if (rt != OPRT_OK)
            break;
        dl_list_add(mlistHead_foodInfoVOList, &foodInfoVOList_node->m_list);
    }

    return rt;
}

STATIC OPERATE_RET __get_1_0_add_customInfoList_langInfo_list(ty_cJSON *array, struct dl_list *mlistHead_customlangInfos)
{
    OPERATE_RET rt = OPRT_OK;
    UCHAR_T num = 0, i = 0;
    ty_cJSON *p_array_item = NULL;
    CHAR_T *psk_url = NULL;
    get_1_0_customlangInfos_node_s *customlangInfos_node = NULL;

    dl_list_init(mlistHead_customlangInfos);
    if (array == NULL) {
        PR_ERR("[%s][%d] null array !\r\n", __FUNCTION__, __LINE__);
        //rt = OPRT_INVALID_PARM;
        return rt;
    }

    num = ty_cJSON_GetArraySize(array);
    for (i = 0; i < num; i++) {
        p_array_item = ty_cJSON_GetArrayItem(array, i);
        customlangInfos_node = (get_1_0_customlangInfos_node_s *)cr_parser_malloc(SIZEOF(get_1_0_customlangInfos_node_s));
        if (customlangInfos_node == NULL) {
            PR_ERR("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
            rt = OPRT_COM_ERROR;
            break;
        }
        TUYA_CALL_ERR_RETURN(ty_copy_json_str(&customlangInfos_node->lang, p_array_item, "lang"));
        TUYA_CALL_ERR_RETURN(ty_copy_json_str(&customlangInfos_node->name, p_array_item, "name"));
        TUYA_CALL_ERR_RETURN(ty_copy_json_str(&customlangInfos_node->desc, p_array_item, "desc"));
        psk_url = ty_cloud_recipe_menu_get_region_psk_url(ty_cJSON_GetObjectItem(p_array_item, "stepImg"));
        if (psk_url != NULL) {
            customlangInfos_node->fileUrl = psk_url;
            rt = CloudRecipeDl_cb(&customlangInfos_node->fileSize, &customlangInfos_node->fileBuff, psk_url);
            if (rt != OPRT_OK) {
                PR_ERR("[%s][%d] dl '%s' fail...\r\n", __FUNCTION__, __LINE__, psk_url);
                break;
            }
        }
        dl_list_add(mlistHead_customlangInfos, &customlangInfos_node->m_list);
    }

    return rt;
}

STATIC OPERATE_RET __get_1_0_add_customInfoList_list(ty_cJSON *array, struct dl_list *mlistHead_customInfoList)
{
    OPERATE_RET rt = OPRT_OK;
    UCHAR_T num = 0, i = 0;
    ty_cJSON *p_array_item = NULL;
    get_1_0_customInfoList_node_s *customInfoList_node = NULL;

    dl_list_init(mlistHead_customInfoList);
    if (array == NULL) {
        PR_ERR("[%s][%d] null array !\r\n", __FUNCTION__, __LINE__);
        //rt = OPRT_INVALID_PARM;
        return rt;
    }

    num = ty_cJSON_GetArraySize(array);
    for (i = 0; i < num; i++) {
        p_array_item = ty_cJSON_GetArrayItem(array, i);
        customInfoList_node = (get_1_0_customInfoList_node_s *)cr_parser_malloc(SIZEOF(get_1_0_customInfoList_node_s));
        if (customInfoList_node == NULL) {
            PR_ERR("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
            rt = OPRT_COM_ERROR;
            break;
        }
        TUYA_CALL_ERR_RETURN(ty_copy_json_str(&customInfoList_node->code, p_array_item, "code"));
        //customlangInfos
        rt = __get_1_0_add_customInfoList_langInfo_list(ty_cJSON_GetObjectItem(p_array_item, "langInfos"), &customInfoList_node->mlistHead_customlangInfos);
        if (rt != OPRT_OK)
            break;
        dl_list_add(mlistHead_customInfoList, &customInfoList_node->m_list);
    }

    return rt;
}

//云菜谱列表解析器申请内存
VOID *cr_parser_malloc(IN UINT_T buf_len)
{
    return cp0_req_cp1_malloc_psram(buf_len);
}

//云菜谱列表解析器释放内存
OPERATE_RET cr_parser_free(IN VOID *ptr)
{
    return cp0_req_cp1_free_psram(ptr);
}

OPERATE_RET ty_cloud_recipe_menu_resp_send(UINT32_T rsp_type, VOID *param)
{
    OPERATE_RET op_ret = OPRT_OK;
    LV_DISP_EVENT_S Sevt = {0};
    LV_TUYAOS_EVENT_S g_dp_evt_s = {0};
    gui_cloud_recipe_interactive_s cl_rsp = { 0 };

    cl_rsp.req_type = rsp_type;
    cl_rsp.req_param = param;
    Sevt.event = LLV_EVENT_VENDOR_TUYAOS;
    g_dp_evt_s.event_typ = LV_TUYAOS_EVENT_CLOUD_RECIPE;
    g_dp_evt_s.param1 = (UINT32_T)&cl_rsp;
    Sevt.param = (UINT_T)&g_dp_evt_s;
    tuya_disp_gui_event_send(&Sevt);

    return op_ret;
}

OPERATE_RET ty_cloud_recipe_menu_parser_init(VOID)
{
    OPERATE_RET rt = OPRT_OK;

    return rt;
}

OPERATE_RET ty_cloud_recipe_menu_dl_register_cb(CLOUD_RECIPE_DL_CB cb)
{
    CloudRecipeDl_cb = cb;
    return OPRT_OK;
}

OPERATE_RET ty_cloud_recipe_menu_category_list_1_0_parse(IN VOID *json)
{
    TY_MENU_PARAM_CATEGORY_LIST_1_0_RSP_S *ty_menu_category_list_1_0_rsp = NULL;
    ty_cJSON *root = (ty_cJSON *)json;
    ty_cJSON *p_array_item1 = NULL;
    ty_cJSON *p_langinfo_array = NULL;
    UCHAR_T num1 = 0, i = 0;
    CHAR_T *psk_url = NULL;
    OPERATE_RET rt = OPRT_OK;
    category_list_1_0_node_s *category_list_1_0_node = NULL;

    if (root == NULL) {
        PR_ERR("[%s][%d] null array !\r\n", __FUNCTION__, __LINE__);
        rt = OPRT_INVALID_PARM;
        return rt;
    }

    ty_menu_category_list_1_0_rsp = (TY_MENU_PARAM_CATEGORY_LIST_1_0_RSP_S *)cr_parser_malloc(SIZEOF(TY_MENU_PARAM_CATEGORY_LIST_1_0_RSP_S));
    if (ty_menu_category_list_1_0_rsp == NULL) {
        PR_ERR("category_list_1_0_parse malloc fail ?");
        return OPRT_MALLOC_FAILED;
    }

    dl_list_init(&ty_menu_category_list_1_0_rsp->mlistHead);

    num1 = ty_cJSON_GetArraySize(root);
    if(num1 <= 0){
        PR_ERR("ty_cJSON_GetArraySize:[%d]",num1);
        return OPRT_INVALID_PARM;
    }

    for(i = 0; i < num1; i++) {
        p_array_item1 = ty_cJSON_GetArrayItem(root,i);
        category_list_1_0_node = (category_list_1_0_node_s *)cr_parser_malloc(SIZEOF(category_list_1_0_node_s));
        if (category_list_1_0_node == NULL) {
            PR_ERR("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
            rt = OPRT_COM_ERROR;
            break;
        }

        //id
        ty_copy_json_ulong(&category_list_1_0_node->id, p_array_item1, "id");
        //name
        TUYA_CALL_ERR_RETURN(ty_copy_json_str(&category_list_1_0_node->name, p_array_item1, "name"));
        //imageUrl
        psk_url = ty_cloud_recipe_menu_get_region_psk_url(ty_cJSON_GetObjectItem(p_array_item1, "imageUrl"));
        if (psk_url != NULL) {
            category_list_1_0_node->imageUrl = psk_url;
            rt = CloudRecipeDl_cb(&category_list_1_0_node->imageSize, &category_list_1_0_node->imageBuff, psk_url);
            if (rt != OPRT_OK) {
                PR_ERR("[%s][%d] dl '%s' fail...\r\n", __FUNCTION__, __LINE__, psk_url);
                break;
            }
        }
        //gmtCreate(json中超过四个字节的整型从valuedouble中获取)
        ty_copy_json_uint64(&category_list_1_0_node->gmtCreate, p_array_item1, "gmtCreate");
        //gmtModified
        ty_copy_json_uint64(&category_list_1_0_node->gmtModified, p_array_item1, "gmtModified");
        p_langinfo_array = ty_cJSON_GetObjectItem(p_array_item1, "langInfo");
        rt = __category_common_add_langinfo_list(p_langinfo_array, &category_list_1_0_node->mlistHead_langinfo);
        if (rt != OPRT_OK)
            break;
        dl_list_add(&ty_menu_category_list_1_0_rsp->mlistHead, &category_list_1_0_node->m_list);
    }

    if (rt == OPRT_OK)
        rt = ty_cloud_recipe_menu_resp_send(CL_MENU_CATEGORY_LIST_1_0, (VOID *)ty_menu_category_list_1_0_rsp);

    return rt;
}

OPERATE_RET ty_cloud_recipe_menu_category_list_2_0_parse(IN VOID *json)
{
    TY_MENU_PARAM_CATEGORY_LIST_2_0_RSP_S *ty_menu_category_list_2_0_rsp = NULL;
    ty_cJSON *root = (ty_cJSON *)json;
    ty_cJSON *p_array = NULL;
    ty_cJSON *p_langinfo_array = NULL, *p_subMenuCategorie_array = NULL;
    UCHAR_T num = 0, i = 0;
    CHAR_T *psk_url = NULL;
    OPERATE_RET rt = OPRT_OK;
    category_list_2_0_node_s *category_list_2_0_node = NULL;

    if (root == NULL) {
        PR_ERR("[%s][%d] null array !\r\n", __FUNCTION__, __LINE__);
        rt = OPRT_INVALID_PARM;
        return rt;
    }

    ty_menu_category_list_2_0_rsp = (TY_MENU_PARAM_CATEGORY_LIST_2_0_RSP_S *)cr_parser_malloc(SIZEOF(TY_MENU_PARAM_CATEGORY_LIST_2_0_RSP_S));
    if (ty_menu_category_list_2_0_rsp == NULL) {
        PR_ERR("category_list_2_0_parse malloc fail ?");
        return OPRT_MALLOC_FAILED;
    }

    dl_list_init(&ty_menu_category_list_2_0_rsp->mlistHead);

    num = ty_cJSON_GetArraySize(root);
    if(num <= 0){
        PR_ERR("ty_cJSON_GetArraySize:[%d]",num);
        return OPRT_INVALID_PARM;
    }

    for(i = 0; i < num; i++) {
        p_array = ty_cJSON_GetArrayItem(root,i);
        category_list_2_0_node = (category_list_2_0_node_s *)cr_parser_malloc(SIZEOF(category_list_2_0_node_s));
        if (category_list_2_0_node == NULL) {
            PR_ERR("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
            rt = OPRT_COM_ERROR;
            break;
        }

        //id
        ty_copy_json_ulong(&category_list_2_0_node->id, p_array, "id");
        //name
        TUYA_CALL_ERR_RETURN(ty_copy_json_str(&category_list_2_0_node->name, p_array, "name"));
        //imageUrl
        psk_url = ty_cloud_recipe_menu_get_region_psk_url(ty_cJSON_GetObjectItem(p_array, "imageUrl"));
        if (psk_url != NULL) {
            category_list_2_0_node->imageUrl = psk_url;
            rt = CloudRecipeDl_cb(&category_list_2_0_node->imageSize, &category_list_2_0_node->imageBuff, psk_url);
            if (rt != OPRT_OK) {
                PR_ERR("[%s][%d] dl '%s' fail...\r\n", __FUNCTION__, __LINE__, psk_url);
                break;
            }
        }
        ty_copy_json_uint64(&category_list_2_0_node->gmtCreate, p_array, "gmtCreate");
        ty_copy_json_uint64(&category_list_2_0_node->gmtModified, p_array, "gmtModified");
        //level
        ty_copy_json_uint(&category_list_2_0_node->level, p_array, "level");
        //parentId
        ty_copy_json_ulong(&category_list_2_0_node->parentId, p_array, "parentId");
        //add langinfo list
        p_langinfo_array = ty_cJSON_GetObjectItem(p_array, "langInfo");
        rt = __category_common_add_langinfo_list(p_langinfo_array, &category_list_2_0_node->mlistHead_langinfo);
        if (rt != OPRT_OK) {
            break;
        }
        //add subMenuCategories list
        p_subMenuCategorie_array = ty_cJSON_GetObjectItem(p_array, "subMenuCategories");
        //add subMenuCategorie list
        rt = __category_list_2_0_add_subMenuCategorie_list(p_subMenuCategorie_array, &category_list_2_0_node->mlistHead_subMenuCategories);
        if (rt != OPRT_OK) {
            break;
        }
        dl_list_add(&ty_menu_category_list_2_0_rsp->mlistHead, &category_list_2_0_node->m_list);
    }

    if (rt == OPRT_OK)
        rt = ty_cloud_recipe_menu_resp_send(CL_MENU_CATEGORY_LIST_2_0, (VOID *)ty_menu_category_list_2_0_rsp);
    return rt;
}

OPERATE_RET ty_cloud_recipe_menu_lang_list_1_0_parse(IN VOID *json)
{
    TY_MENU_PARAM_LANG_LIST_1_0_RSP_S *ty_menu_lang_list_1_0_rsp = NULL;
    ty_cJSON *root = (ty_cJSON *)json;
    ty_cJSON *data = NULL;
    ty_cJSON *p_array = NULL;
    ty_cJSON *p_mainImgs_array = NULL, *p_mainImgs_item = NULL;
    UCHAR_T num = 0, i = 0;
    UCHAR_T num1 = 0, j = 0;
    CHAR_T *psk_url = NULL;
    OPERATE_RET rt = OPRT_OK;
    lang_list_1_0_data_node_s *lang_list_1_0_data_node = NULL;
    lang_list_1_0_mainImgs_node_s *lang_list_1_0_mainImgs_node = NULL;

    if (root == NULL) {
        PR_ERR("[%s][%d] null array !\r\n", __FUNCTION__, __LINE__);
        rt = OPRT_INVALID_PARM;
        return rt;
    }

    data = ty_cJSON_GetObjectItem(root, "data");
    if (data == NULL) {
        PR_ERR("menu_lang_list_1_0_parse data empty ?");
        return OPRT_MALLOC_FAILED;
    }

    ty_menu_lang_list_1_0_rsp = (TY_MENU_PARAM_LANG_LIST_1_0_RSP_S *)cr_parser_malloc(SIZEOF(TY_MENU_PARAM_LANG_LIST_1_0_RSP_S));
    if (ty_menu_lang_list_1_0_rsp == NULL) {
        PR_ERR("category_list_2_0_parse malloc fail ?");
        return OPRT_MALLOC_FAILED;
    }

    dl_list_init(&ty_menu_lang_list_1_0_rsp->mlistHead_data);
    ty_copy_json_uint(&ty_menu_lang_list_1_0_rsp->totalCount, root, "totalCount");
    
    num = ty_cJSON_GetArraySize(data);
    if(num <= 0){
        PR_ERR("ty_cJSON_GetArraySize:[%d]",num);
        return OPRT_INVALID_PARM;
    }

    for(i = 0; i < num; i++) {
        p_array = ty_cJSON_GetArrayItem(data,i);
        lang_list_1_0_data_node = (lang_list_1_0_data_node_s *)cr_parser_malloc(SIZEOF(lang_list_1_0_data_node_s));
        if (lang_list_1_0_data_node == NULL) {
            PR_ERR("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
            rt = OPRT_COM_ERROR;
            break;
        }

        //id
        ty_copy_json_ulong(&lang_list_1_0_data_node->id, p_array, "id");
        //lang
        ty_copy_json_uint(&lang_list_1_0_data_node->lang, p_array, "lang");
        //langDesc
        TUYA_CALL_ERR_RETURN(ty_copy_json_str(&lang_list_1_0_data_node->langDesc, p_array, "langDesc"));
        //cookType
        ty_copy_json_uint(&lang_list_1_0_data_node->cookType, p_array, "cookType");
        //sourceType
        ty_copy_json_uint(&lang_list_1_0_data_node->sourceType, p_array, "sourceType");
        //useFoodLib
        ty_copy_json_uint(&lang_list_1_0_data_node->useFoodLib, p_array, "useFoodLib");
        //isMainShow
        ty_copy_json_uint(&lang_list_1_0_data_node->isMainShow, p_array, "isMainShow");
        //isShowControl
        ty_copy_json_uint(&lang_list_1_0_data_node->isShowControl, p_array, "isShowControl");
        //isFoodChannel
        ty_copy_json_byte(&lang_list_1_0_data_node->isFoodChannel, p_array, "isFoodChannel");
        //isControl
        ty_copy_json_uint(&lang_list_1_0_data_node->isControl, p_array, "isControl");
        //CHAR_T                          *name;
        TUYA_CALL_ERR_RETURN(ty_copy_json_str(&lang_list_1_0_data_node->name, p_array, "name"));
        //CHAR_T                          *easyLevel;
        TUYA_CALL_ERR_RETURN(ty_copy_json_str(&lang_list_1_0_data_node->easyLevel, p_array, "easyLevel"));
        //CHAR_T                          *easyLevelDesc;
        TUYA_CALL_ERR_RETURN(ty_copy_json_str(&lang_list_1_0_data_node->easyLevelDesc, p_array, "easyLevelDesc"));
        //CHAR_T                          *taste;
        TUYA_CALL_ERR_RETURN(ty_copy_json_str(&lang_list_1_0_data_node->taste, p_array, "taste"));
        //CHAR_T                          *tasteDesc;
        TUYA_CALL_ERR_RETURN(ty_copy_json_str(&lang_list_1_0_data_node->tasteDesc, p_array, "tasteDesc"));
        //CHAR_T                          *foodType;
        TUYA_CALL_ERR_RETURN(ty_copy_json_str(&lang_list_1_0_data_node->foodType, p_array, "foodType"));
        //CHAR_T                          *foodTypeDesc;
        TUYA_CALL_ERR_RETURN(ty_copy_json_str(&lang_list_1_0_data_node->foodTypeDesc, p_array, "foodTypeDesc"));
        //UINT_T                          cookTime;
        ty_copy_json_uint(&lang_list_1_0_data_node->cookTime, p_array, "cookTime");
        //UINT_T                          eatCount;
        ty_copy_json_uint(&lang_list_1_0_data_node->eatCount, p_array, "eatCount");
        //DOUBLE_T                        avgScore;
        ty_copy_json_double(&lang_list_1_0_data_node->avgScore, p_array, "avgScore");
        //BOOL_T                          isStar;
        ty_copy_json_bool(&lang_list_1_0_data_node->isStar, p_array, "isStar");
        //UINT64_T                        gmtCreate;
        ty_copy_json_uint64(&lang_list_1_0_data_node->gmtCreate, p_array, "gmtCreate");
        //UINT64_T                        gmtModified;
        ty_copy_json_uint64(&lang_list_1_0_data_node->gmtModified, p_array, "gmtModified");

        //add mainImgs list
        num1 = 0;
        p_mainImgs_array = ty_cJSON_GetObjectItem(p_array, "mainImgs");
        if (p_mainImgs_array)
            num1 = ty_cJSON_GetArraySize(p_mainImgs_array);
        dl_list_init(&lang_list_1_0_data_node->mlistHead_mainImgs);
        for (j = 0; j < num1; j++) {
            p_mainImgs_item = ty_cJSON_GetArrayItem(p_mainImgs_array, j);
            lang_list_1_0_mainImgs_node = (lang_list_1_0_mainImgs_node_s *)cr_parser_malloc(SIZEOF(lang_list_1_0_mainImgs_node_s));
            if (lang_list_1_0_mainImgs_node == NULL) {
                PR_ERR("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
                rt = OPRT_COM_ERROR;
                break;
            }
            psk_url = ty_cloud_recipe_menu_get_region_psk_url(p_mainImgs_item);
            if (psk_url != NULL) {
                lang_list_1_0_mainImgs_node->imageUrl = psk_url;
                rt = CloudRecipeDl_cb(&lang_list_1_0_mainImgs_node->imageSize, &lang_list_1_0_mainImgs_node->imageBuff, psk_url);
                if (rt != OPRT_OK) {
                    PR_ERR("[%s][%d] dl '%s' fail...\r\n", __FUNCTION__, __LINE__, psk_url);
                    break;
                }
            }
            dl_list_add(&lang_list_1_0_data_node->mlistHead_mainImgs, &lang_list_1_0_mainImgs_node->m_list);
        }
        if (rt != OPRT_OK)
            break;
        dl_list_add(&ty_menu_lang_list_1_0_rsp->mlistHead_data, &lang_list_1_0_data_node->m_list);
    }

    if (rt == OPRT_OK)
        rt = ty_cloud_recipe_menu_resp_send(CL_MENU_LANG_LIST_1_0, (VOID *)ty_menu_lang_list_1_0_rsp);
    return rt;
}

OPERATE_RET ty_cloud_recipe_menu_get_1_0_parse(IN VOID *json)
{
    TY_MENU_PARAM_GET_1_0_RSP_S *ty_menu_get_1_0_rsp = NULL;
    ty_cJSON *root = (ty_cJSON *)json;
    OPERATE_RET rt = OPRT_OK;

    ty_menu_get_1_0_rsp = (TY_MENU_PARAM_GET_1_0_RSP_S *)cr_parser_malloc(SIZEOF(TY_MENU_PARAM_GET_1_0_RSP_S));
    if (ty_menu_get_1_0_rsp == NULL) {
        PR_ERR("menu_get_1_0_parse malloc fail ?");
        return OPRT_MALLOC_FAILED;
    }

    //id
    ty_copy_json_ulong(&ty_menu_get_1_0_rsp->id, root, "id");
    //name
    TUYA_CALL_ERR_RETURN(ty_copy_json_str(&ty_menu_get_1_0_rsp->name, root, "name"));
    //desc
    TUYA_CALL_ERR_RETURN(ty_copy_json_str(&ty_menu_get_1_0_rsp->desc, root, "desc"));
    //information
    TUYA_CALL_ERR_RETURN(ty_copy_json_str(&ty_menu_get_1_0_rsp->information, root, "information"));
    //xyxk
    TUYA_CALL_ERR_RETURN(ty_copy_json_str(&ty_menu_get_1_0_rsp->xyxk, root, "xyxk"));
    //foods
    TUYA_CALL_ERR_RETURN(ty_copy_json_str(&ty_menu_get_1_0_rsp->foods, root, "foods"));
    //dl_list                  mlistHead_mainImgs
    rt = __get_1_0_add_mainImgs_list(ty_cJSON_GetObjectItem(root, "mainImgs"), &ty_menu_get_1_0_rsp->mlistHead_mainImgs);
    if (rt != OPRT_OK)
        return rt;
    //lang
    ty_copy_json_ulong(&ty_menu_get_1_0_rsp->lang, root, "lang");
    //langDesc
    TUYA_CALL_ERR_RETURN(ty_copy_json_str(&ty_menu_get_1_0_rsp->langDesc, root, "langDesc"));
    //isMainShow
    ty_copy_json_uint(&ty_menu_get_1_0_rsp->isMainShow, root, "isMainShow");
    //isControl
    ty_copy_json_uint(&ty_menu_get_1_0_rsp->isControl, root, "isControl");
    //isShowControl
    ty_copy_json_uint(&ty_menu_get_1_0_rsp->isShowControl, root, "isShowControl");
    //sourceType
    ty_copy_json_uint(&ty_menu_get_1_0_rsp->sourceType, root, "sourceType");
    //cookType
    ty_copy_json_uint(&ty_menu_get_1_0_rsp->cookType, root, "cookType");
    //extInfo
    TUYA_CALL_ERR_RETURN(ty_copy_json_str(&ty_menu_get_1_0_rsp->extInfo, root, "extInfo"));
    //cookTime
    ty_copy_json_uint(&ty_menu_get_1_0_rsp->cookTime, root, "cookTime");
    //useFoodLib
    ty_copy_json_uint(&ty_menu_get_1_0_rsp->useFoodLib, root, "useFoodLib");
    //eatCount
    ty_copy_json_uint(&ty_menu_get_1_0_rsp->eatCount, root, "eatCount");
    //isFoodChannel
    ty_copy_json_uint(&ty_menu_get_1_0_rsp->isFoodChannel, root, "isFoodChannel");
    //easyLevel
    TUYA_CALL_ERR_RETURN(ty_copy_json_str(&ty_menu_get_1_0_rsp->easyLevel, root, "easyLevel"));
    //easyLevelDesc
    TUYA_CALL_ERR_RETURN(ty_copy_json_str(&ty_menu_get_1_0_rsp->easyLevelDesc, root, "easyLevelDesc"));
    //taste
    TUYA_CALL_ERR_RETURN(ty_copy_json_str(&ty_menu_get_1_0_rsp->taste, root, "taste"));
    //tasteDesc
    TUYA_CALL_ERR_RETURN(ty_copy_json_str(&ty_menu_get_1_0_rsp->tasteDesc, root, "tasteDesc"));
    //foodType
    TUYA_CALL_ERR_RETURN(ty_copy_json_str(&ty_menu_get_1_0_rsp->foodType, root, "foodType"));
    //foodTypeDesc
    TUYA_CALL_ERR_RETURN(ty_copy_json_str(&ty_menu_get_1_0_rsp->foodTypeDesc, root, "foodTypeDesc"));
    //dl_list                  mlistHead_allergens
    rt = __get_1_0_add_allergens_list(ty_cJSON_GetObjectItem(root, "allergens"), &ty_menu_get_1_0_rsp->mlistHead_allergens);
    if (rt != OPRT_OK)
        return rt;
    //author
    TUYA_CALL_ERR_RETURN(ty_copy_json_str(&ty_menu_get_1_0_rsp->author, root, "author"));
    //pv
    ty_copy_json_ulong(&ty_menu_get_1_0_rsp->pv, root, "pv");
    //avgScore
    ty_copy_json_double(&ty_menu_get_1_0_rsp->avgScore, root, "avgScore");
    //dl_list                  mlistHead_menuStepInfoVOList
    rt = __get_1_0_add_menuStepInfoVOList_list(ty_cJSON_GetObjectItem(root, "menuStepInfoVOList"), &ty_menu_get_1_0_rsp->mlistHead_menuStepInfoVOList);
    if (rt != OPRT_OK)
        return rt;
    //dl_list                  mlistHead_cookStepInfoVOList
    rt = __get_1_0_add_cookStepInfoVOList_list(ty_cJSON_GetObjectItem(root, "cookStepInfoVOList"), &ty_menu_get_1_0_rsp->mlistHead_cookStepInfoVOList);
    if (rt != OPRT_OK)
        return rt;
    //dl_list                  mlistHead_foodInfoVOList
    rt = __get_1_0_add_foodInfoVOList_list(ty_cJSON_GetObjectItem(root, "foodInfoVOList"), &ty_menu_get_1_0_rsp->mlistHead_foodInfoVOList);
    if (rt != OPRT_OK)
        return rt;
    //dl_list                  mlistHead_customInfoList
    rt = __get_1_0_add_customInfoList_list(ty_cJSON_GetObjectItem(root, "customInfoList"), &ty_menu_get_1_0_rsp->mlistHead_customInfoList);
    if (rt != OPRT_OK)
        return rt;
    //gmtCreate
    ty_copy_json_uint64(&ty_menu_get_1_0_rsp->gmtCreate, root, "gmtCreate");
    //gmtModified
    ty_copy_json_uint64(&ty_menu_get_1_0_rsp->gmtModified, root, "gmtModified");

    if (rt == OPRT_OK)
        rt = ty_cloud_recipe_menu_resp_send(CL_MENU_GET_1_0, (VOID *)ty_menu_get_1_0_rsp);
    return rt;
}

OPERATE_RET ty_cloud_recipe_menu_custom_lang_list_1_0_parse(IN VOID *json)
{
    TY_MENU_PARAM_CUSTOM_LANG_LIST_1_0_RSP_S *ty_menu_custom_lang_list_1_0_rsp = NULL;
    //ty_cJSON *p_result_obj = (ty_cJSON *)json;
    OPERATE_RET rt = OPRT_COM_ERROR;

    if (rt == OPRT_OK)
        rt = ty_cloud_recipe_menu_resp_send(CL_MENU_CUSTOM_LANG_LIST_1_0, (VOID *)ty_menu_custom_lang_list_1_0_rsp);
    return rt;
}

OPERATE_RET ty_cloud_recipe_menu_diy_add_2_0_parse(IN VOID *json)
{
    TY_MENU_PARAM_DIY_ADD_2_0_RSP_S *ty_menu_diy_add_2_0_rsp = NULL;
    //ty_cJSON *p_result_obj = (ty_cJSON *)json;
    OPERATE_RET rt = OPRT_COM_ERROR;

    if (rt == OPRT_OK)
        rt = ty_cloud_recipe_menu_resp_send(CL_MENU_DIY_ADD_2_0, (VOID *)ty_menu_diy_add_2_0_rsp);
    return rt;
}

OPERATE_RET ty_cloud_recipe_menu_diy_update_2_0_parse(IN VOID *json)
{
    TY_MENU_PARAM_DIY_UPDATE_2_0_RSP_S *ty_menu_diy_update_2_0_rsp = NULL;
    //ty_cJSON *p_result_obj = (ty_cJSON *)json;
    OPERATE_RET rt = OPRT_COM_ERROR;

    if (rt == OPRT_OK)
        rt = ty_cloud_recipe_menu_resp_send(CL_MENU_DIY_UPDATE_2_0, (VOID *)ty_menu_diy_update_2_0_rsp);
    return rt;
}

OPERATE_RET ty_cloud_recipe_menu_diy_delete_1_0_parse(IN VOID *json)
{
    TY_MENU_PARAM_DIY_DELETE_1_0_RSP_S *ty_menu_diy_delete_1_0_rsp = NULL;
    //ty_cJSON *p_result_obj = (ty_cJSON *)json;
    OPERATE_RET rt = OPRT_COM_ERROR;

    if (rt == OPRT_OK)
        rt = ty_cloud_recipe_menu_resp_send(CL_MENU_DIY_DELETE_1_0, (VOID *)ty_menu_diy_delete_1_0_rsp);
    return rt;
}

OPERATE_RET ty_cloud_recipe_menu_star_list_2_0_parse(IN VOID *json)
{
    TY_MENU_PARAM_STAR_LIST_2_0_RSP_S *ty_menu_star_list_2_0_rsp = NULL;
    //ty_cJSON *p_result_obj = (ty_cJSON *)json;
    OPERATE_RET rt = OPRT_COM_ERROR;

    if (rt == OPRT_OK)
        rt = ty_cloud_recipe_menu_resp_send(CL_MENU_STAR_LIST_2_0, (VOID *)ty_menu_star_list_2_0_rsp);
    return rt;
}

OPERATE_RET ty_cloud_recipe_menu_star_add_1_0_parse(IN VOID *json)
{
    TY_MENU_PARAM_STAR_ADD_1_0_RSP_S *ty_menu_star_add_1_0_rsp = NULL;
    //ty_cJSON *p_result_obj = (ty_cJSON *)json;
    OPERATE_RET rt = OPRT_COM_ERROR;

    if (rt == OPRT_OK)
        rt = ty_cloud_recipe_menu_resp_send(CL_MENU_STAR_ADD_1_0, (VOID *)ty_menu_star_add_1_0_rsp);
    return rt;
}

OPERATE_RET ty_cloud_recipe_menu_star_delete_1_0_parse(IN VOID *json)
{
    TY_MENU_PARAM_STAR_DELETE_1_0_RSP_S *ty_menu_star_delete_1_0_rsp = NULL;
    //ty_cJSON *p_result_obj = (ty_cJSON *)json;
    OPERATE_RET rt = OPRT_COM_ERROR;

    if (rt == OPRT_OK)
        rt = ty_cloud_recipe_menu_resp_send(CL_MENU_STAR_DELETE_1_0, (VOID *)ty_menu_star_delete_1_0_rsp);
    return rt;
}

OPERATE_RET ty_cloud_recipe_menu_star_check_1_0_parse(IN VOID *json)
{
    TY_MENU_PARAM_STAR_CHECK_1_0_RSP_S *ty_menu_star_check_1_0_rsp = NULL;
    //ty_cJSON *p_result_obj = (ty_cJSON *)json;
    OPERATE_RET rt = OPRT_COM_ERROR;

    if (rt == OPRT_OK)
        rt = ty_cloud_recipe_menu_resp_send(CL_MENU_STAR_CHECK_1_0, (VOID *)ty_menu_star_check_1_0_rsp);
    return rt;
}

OPERATE_RET ty_cloud_recipe_menu_star_count_1_0_parse(IN VOID *json)
{
    TY_MENU_PARAM_STAR_COUNT_1_0_RSP_S *ty_menu_star_count_1_0_rsp = NULL;
    //ty_cJSON *p_result_obj = (ty_cJSON *)json;
    OPERATE_RET rt = OPRT_COM_ERROR;

    if (rt == OPRT_OK)
        rt = ty_cloud_recipe_menu_resp_send(CL_MENU_STAR_COUNT_1_0, (VOID *)ty_menu_star_count_1_0_rsp);
    return rt;
}

OPERATE_RET ty_cloud_recipe_menu_score_add_1_0_parse(IN VOID *json)
{
    TY_MENU_PARAM_SCORE_ADD_1_0_RSP_S *ty_menu_score_add_1_0_rsp = NULL;
    //ty_cJSON *p_result_obj = (ty_cJSON *)json;
    OPERATE_RET rt = OPRT_COM_ERROR;

    if (rt == OPRT_OK)
        rt = ty_cloud_recipe_menu_resp_send(CL_MENU_SCORE_ADD_1_0, (VOID *)ty_menu_score_add_1_0_rsp);
    return rt;
}

OPERATE_RET ty_cloud_recipe_menu_banner_lsit_1_0_parse(IN VOID *json)
{
    TY_MENU_PARAM_BANNER_LIST_1_0_RSP_S *ty_menu_banner_list_1_0_rsp = NULL;
    //ty_cJSON *p_result_obj = (ty_cJSON *)json;
    OPERATE_RET rt = OPRT_COM_ERROR;

    if (rt == OPRT_OK)
        rt = ty_cloud_recipe_menu_resp_send(CL_MENU_BANNER_LIST_1_0, (VOID *)ty_menu_banner_list_1_0_rsp);
    return rt;
}

OPERATE_RET ty_cloud_recipe_cookbook_all_lang_1_0_parse(IN VOID *json)
{
    TY_COOKBOOK_PARAM_ALL_LANG_1_0_RSP_S *ty_menu_cookbook_all_lang_1_0_rsp = NULL;
    //ty_cJSON *p_result_obj = (ty_cJSON *)json;
    OPERATE_RET rt = OPRT_COM_ERROR;

    if (rt == OPRT_OK)
        rt = ty_cloud_recipe_menu_resp_send(CL_COOKBOOK_ALL_LANG_1_0, (VOID *)ty_menu_cookbook_all_lang_1_0_rsp);
    return rt;
}

OPERATE_RET ty_cloud_recipe_menu_query_model_list_1_0_parse(IN VOID *json)
{
    TY_MENU_PARAM_QUERY_MODEL_LIST_1_0_RSP_S *ty_menu_query_model_list_1_0_rsp = NULL;
    //ty_cJSON *p_result_obj = (ty_cJSON *)json;
    OPERATE_RET rt = OPRT_COM_ERROR;

    if (rt == OPRT_OK)
        rt = ty_cloud_recipe_menu_resp_send(CL_MENU_QUERY_MODEL_LIST_1_0, (VOID *)ty_menu_query_model_list_1_0_rsp);
    return rt;
}

OPERATE_RET ty_cloud_recipe_menu_search_condition_1_0_parse(IN VOID *json)
{
    TY_MENU_PARAM_SEARCH_CONDITION_1_0_RSP_S *ty_menu_search_conditon_1_0_rsp = NULL;
    //ty_cJSON *p_result_obj = (ty_cJSON *)json;
    OPERATE_RET rt = OPRT_COM_ERROR;

    if (rt == OPRT_OK)
        rt = ty_cloud_recipe_menu_resp_send(CL_MENU_SEARCH_CONDITION_1_0, (VOID *)ty_menu_search_conditon_1_0_rsp);
    return rt;
}

OPERATE_RET ty_cloud_recipe_menu_sync_list_1_0_parse(IN VOID *json)
{
    TY_MENU_PARAM_SYNC_LIST_1_0_RSP_S *ty_menu_sync_list_1_0_rsp = NULL;
    //ty_cJSON *p_result_obj = (ty_cJSON *)json;
    OPERATE_RET rt = OPRT_COM_ERROR;

    if (rt == OPRT_OK)
        rt = ty_cloud_recipe_menu_resp_send(CL_MENU_SYNC_LIST_1_0, (VOID *)ty_menu_sync_list_1_0_rsp);
    return rt;
}

OPERATE_RET ty_cloud_recipe_menu_sync_details_1_0_parse(IN VOID *json)
{
    TY_MENU_PARAM_SYNC_DETAILS_1_0_RSP_S *ty_menu_sync_details_1_0_rsp = NULL;
    ////ty_cJSON *p_result_obj = (ty_cJSON *)json;
    OPERATE_RET rt = OPRT_COM_ERROR;

    if (rt == OPRT_OK)
        rt = ty_cloud_recipe_menu_resp_send(CL_MENU_SYNC_DETAILS_1_0, (VOID *)ty_menu_sync_details_1_0_rsp);
    return rt;
}
