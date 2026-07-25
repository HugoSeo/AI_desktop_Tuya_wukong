/**
 * @file ty_gfw_gui_cloud_recipe_param.h
 * @brief
 * @version 0.1
 * @date 2023-06-15
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

#ifndef __TUYA_GFW_GUI_CLOUD_RECIPE_PARAM_H__
#define __TUYA_GFW_GUI_CLOUD_RECIPE_PARAM_H__

#include "tuya_cloud_types.h"
#include "dl_list.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CL_MENU_CATEGORY_LIST_1_0 = 0,                              //获取食谱一级分类列表
    CL_MENU_CATEGORY_LIST_2_0,                                  //获取食谱分类列表

    CL_MENU_LANG_LIST_1_0,                                      //食谱列表(分页获取B端客户食谱信息)

    CL_MENU_GET_1_0,                                            //获取食谱详情

    CL_MENU_CUSTOM_LANG_LIST_1_0,                               //自定义食谱列表
    CL_MENU_DIY_ADD_2_0,                                        //自定义食谱添加
    CL_MENU_DIY_UPDATE_2_0,                                     //自定义食谱更新
    CL_MENU_DIY_DELETE_1_0,                                     //自定义食谱删除

    CL_MENU_STAR_LIST_2_0,                                      //获取食谱收藏列表
    CL_MENU_STAR_ADD_1_0,                                       //添加食谱收藏
    CL_MENU_STAR_DELETE_1_0,                                    //取消食谱收藏
    CL_MENU_STAR_CHECK_1_0,                                     //食谱是否收藏
    CL_MENU_STAR_COUNT_1_0,                                     //食谱收藏数量

    CL_MENU_SCORE_ADD_1_0,                                      //食谱添加评分

    CL_MENU_BANNER_LIST_1_0,                                    //获取推荐设备列表和banner列表

    CL_COOKBOOK_ALL_LANG_1_0,                                   //获取食谱所有语言

    CL_MENU_QUERY_MODEL_LIST_1_0,                               //设备获取搜索模型

    CL_MENU_SEARCH_CONDITION_1_0,                               //获取烹饪时间筛选条件

    CL_MENU_SYNC_LIST_1_0,                                      //获取待同步食谱列表
    CL_MENU_SYNC_DETAILS_1_0,                                   //批量同步食谱详情


    CL_MENU_DOWNLOAD_FAIL,                                      //下载云食谱失败

    //last line, don't change it !!!
    CL_MENU_TYPE_UNKNOWN = 0xff,
} GUI_CLOUD_RECIPE_REQ_TYPE_E;

/**********************************************************************************************/
/**************************************云菜谱响应数据集合**************************************/
/**********************************************************************************************/
//通用数据结构
typedef struct
{
    CHAR_T          *name;
    CHAR_T          *lang;
    struct dl_list  m_list;
} category_common_langinfo_node_s;

/**************************************1.食谱一级分类列表**************************************/
//--->请求参数
typedef struct {
    CHAR_T  *lang;                              //必须
} TY_MENU_PARAM_CATEGORY_LIST_1_0_S;

//<---响应参数
typedef struct
{
    ULONG_T                         id;
    CHAR_T                          *name;
    CHAR_T                          *imageUrl;
    UINT_T                          imageSize;
    VOID                            *imageBuff;             //大内存,从cp1的psram请求申请!
    UINT64_T                        gmtCreate;
    UINT64_T                        gmtModified;
    struct dl_list      mlistHead_langinfo;                 //2级挂载--->category_common_langinfo_node_s
    struct dl_list      m_list;
} category_list_1_0_node_s;

typedef struct {
    struct dl_list      mlistHead;                          //1级挂载--->category_list_1_0_node_s
} TY_MENU_PARAM_CATEGORY_LIST_1_0_RSP_S;
/**************************************1.食谱一级分类列表**************************************/


/**************************************2.食谱分类列表**************************************/
//--->请求参数
typedef struct {
    CHAR_T  *lang;                              //必须
} TY_MENU_PARAM_CATEGORY_LIST_2_0_S;

//<---响应参数
typedef struct
{
    ULONG_T                         id;
    CHAR_T                          *name;
    CHAR_T                          *imageUrl;
    UINT_T                          imageSize;
    VOID                            *imageBuff;             //大内存,从cp1的psram请求申请!
    UINT64_T                        gmtCreate;
    UINT64_T                        gmtModified;
    UINT_T                          level;
    ULONG_T                         parentId;
    struct dl_list      mlistHead_langinfo;                 //3级挂载--->category_common_langinfo_node_s
    struct dl_list      m_list;
} category_list_2_0_subMenuCategories_node_s;

typedef struct
{
    ULONG_T                         id;
    CHAR_T                          *name;
    CHAR_T                          *imageUrl;
    UINT_T                          imageSize;
    VOID                            *imageBuff;             //大内存,从cp1的psram请求申请!
    UINT64_T                        gmtCreate;
    UINT64_T                        gmtModified;
    UINT_T                          level;
    ULONG_T                         parentId;
    struct dl_list      mlistHead_langinfo;                 //2级挂载--->category_common_langinfo_node_s
    struct dl_list      mlistHead_subMenuCategories;        //2级挂载--->category_list_2_0_subMenuCategories_node_s
    struct dl_list      m_list;
} category_list_2_0_node_s;

typedef struct {
    struct dl_list      mlistHead;                          //1级挂载--->category_list_2_0_node_s
} TY_MENU_PARAM_CATEGORY_LIST_2_0_RSP_S;
/**************************************2.食谱分类列表**************************************/


/**************************************3.食谱列表**************************************/
//--->请求参数
typedef struct {
    CHAR_T  *lang;                              //必须
    CHAR_T  *name;
    VOID    *categoryIds;
    UINT_T  cookTimeMin;
    UINT_T  cookTimeMax;
    VOID    *easyLevelIds;
    VOID    *foodTypeIds;
    VOID    *allergenIds;
    UCHAR_T isFoodChannel;
    UINT_T  orderType;
    UINT_T  pvWeight;
    UINT_T  starCountWeight;
    UINT_T  avgScoreWeight;
    UINT_T  pageNo;                             //必须
    UINT_T  pageSize;                           //必须
} TY_MENU_PARAM_LANG_LIST_1_0_S;

//<---响应参数
typedef struct
{
    CHAR_T                          *imageUrl;
    UINT_T                          imageSize;
    VOID                            *imageBuff;             //大内存,从cp1的psram请求申请!
    struct dl_list  m_list;
} lang_list_1_0_mainImgs_node_s;

typedef struct {
    ULONG_T                         id;
    UINT_T                          lang;
    CHAR_T                          *langDesc;
    UINT_T                          cookType;
    UINT_T                          sourceType;
    UINT_T                          useFoodLib;
    UINT_T                          isMainShow;
    UINT_T                          isShowControl;
    UCHAR_T                         isFoodChannel;
    UINT_T                          isControl;
    CHAR_T                          *name;
    CHAR_T                          *easyLevel;
    CHAR_T                          *easyLevelDesc;
    CHAR_T                          *taste;
    CHAR_T                          *tasteDesc;
    CHAR_T                          *foodType;
    CHAR_T                          *foodTypeDesc;
    UINT_T                          cookTime;
    UINT_T                          eatCount;
    DOUBLE_T                        avgScore;
    BOOL_T                          isStar;
    UINT64_T                        gmtCreate;
    UINT64_T                        gmtModified;
    struct dl_list      mlistHead_mainImgs;                 //2级挂载--->lang_list_1_0_mainImgs_node_s
    struct dl_list      m_list;
} lang_list_1_0_data_node_s;

typedef struct {
    UINT_T              totalCount;
    struct dl_list      mlistHead_data;                     //1级挂载--->lang_list_1_0_data_node_s
} TY_MENU_PARAM_LANG_LIST_1_0_RSP_S;
/**************************************3.食谱列表**************************************/


/**************************************4.食谱详情**************************************/
//--->请求参数
typedef struct {
    CHAR_T  *lang;                              //必须
    ULONG_T menuId;                             //必须
} TY_MENU_PARAM_GET_1_0_S;

//<---响应参数
typedef struct
{
    CHAR_T                          *lang;
    CHAR_T                          *stepImgUrl;
    UINT_T                          stepImgSize;
    VOID                            *stepImgBuff;                   //大内存,从cp1的psram请求申请!
    CHAR_T                          *origStepImgUrl;
    UINT_T                          origStepImgSize;
    VOID                            *origStepImgBuff;               //大内存,从cp1的psram请求申请!
    CHAR_T                          *desc;
    struct dl_list  m_list;
} get_1_0_langInfos_node_s;

typedef struct
{
    CHAR_T                          *lang;
    CHAR_T                          *name;
    CHAR_T                          *desc;
    CHAR_T                          *fileUrl;
    UINT_T                          fileSize;
    VOID                            *fileBuff;                      //大内存,从cp1的psram请求申请!
    struct dl_list  m_list;
} get_1_0_customlangInfos_node_s;

typedef struct
{
    CHAR_T                          *dpCode;
    CHAR_T                          *dpValue;
    BOOL_T                          isTypeValue;
    struct dl_list  m_list;
} get_1_0_cookArgs_node_s;

typedef struct
{
    CHAR_T                          *imageUrl;
    UINT_T                          imageSize;
    VOID                            *imageBuff;             //大内存,从cp1的psram请求申请!
    struct dl_list  m_list;
} get_1_0_mainImgs_node_s;

typedef struct
{
    ULONG_T                         id;
    CHAR_T                          *name;
    struct dl_list  m_list;
} get_1_0_allergens_node_s;

typedef struct
{
    ULONG_T                         id;
    ULONG_T                         foodId;
    CHAR_T                          *nutritionCode;
    ULONG_T                         nutritionId;
    CHAR_T                          *value;
    UCHAR_T                         status;
    struct dl_list  m_list;
} get_1_0_foodNutritionVOList_node_s;

typedef struct
{
    ULONG_T                         id;
    ULONG_T                         menuId;
    UINT_T                          step;
    struct dl_list                  mlistHead_langInfos;            //2级挂载:多语言信息--->get_1_0_langInfos_node_s
    UINT64_T                        gmtCreate;
    UINT64_T                        gmtModified;
    struct dl_list  m_list;
} get_1_0_menuStepInfoVOList_node_s;

typedef struct
{
    ULONG_T                         id;
    ULONG_T                         menuId;
    UINT_T                          isCookArgs;
    UINT_T                          step;
    CHAR_T                          *stepImgUrl;
    UINT_T                          stepImgSize;
    VOID                            *stepImgBuff;             //大内存,从cp1的psram请求申请!
    CHAR_T                          *finishCtrl;
    struct dl_list                  mlistHead_cookArgs;             //2级挂载:烹饪参数--->get_1_0_cookArgs_node_s
    struct dl_list                  mlistHead_langInfos;            //2级挂载:多语言信息--->get_1_0_langInfos_node_s
    UINT64_T                        gmtCreate;
    UINT64_T                        gmtModified;
    struct dl_list  m_list;
} get_1_0_cookStepInfoVOList_node_s;

typedef struct
{
    ULONG_T                         id;
    ULONG_T                         menuId;
    ULONG_T                         foodId;
    CHAR_T                          *amount;
    CHAR_T                          *secAmount;
    CHAR_T                          *secUnit;
    CHAR_T                          *secUnitCode;
    CHAR_T                          *secUnitDesc;
    ULONG_T                         tagId;
    CHAR_T                          *tagName;
    UINT_T                          tagOrder;
    UINT_T                          groupId;
    UINT_T                          foodOrder;
    UCHAR_T                         back;
    UCHAR_T                         status;
    struct dl_list                  mlistHead_langInfos;            //3级挂载:多语言信息--->get_1_0_langInfos_node_s
    UINT64_T                        gmtCreate;
    UINT64_T                        gmtModified;
} get_1_0_menuFoodRelationVO_s;

typedef struct
{
    ULONG_T                         id;
    CHAR_T                          *name;
    CHAR_T                          *desc;
    CHAR_T                          *lang;
    CHAR_T                          *imageUrl;
    UINT_T                          imageSize;
    VOID                            *imageBuff;             //大内存,从cp1的psram请求申请!
    struct dl_list                  mlistHead_foodNutritionVOList;  //2级挂载:食材营养成分--->get_1_0_foodNutritionVOList_node_s
    get_1_0_menuFoodRelationVO_s    menuFoodRelationVO;     //关联食材信息
    UINT64_T                        gmtCreate;
    UINT64_T                        gmtModified;
    struct dl_list  m_list;
} get_1_0_foodInfoVOList_node_s;

typedef struct
{
    CHAR_T                          *code;
    struct dl_list                  mlistHead_customlangInfos;            //2级挂载:多语言信息--->get_1_0_customlangInfos_node_s
    struct dl_list  m_list;
} get_1_0_customInfoList_node_s;

typedef struct {
    ULONG_T                         id;
    CHAR_T                          *name;
    CHAR_T                          *desc;
    CHAR_T                          *information;
    CHAR_T                          *xyxk;                  //相宜相克
    CHAR_T                          *foods;
    struct dl_list                  mlistHead_mainImgs;                 //1级挂载--->get_1_0_mainImgs_node_s
//    struct dl_list                  mlistHead_origMainImgs;             //1级挂载--->get_1_0_mainImgs_node_s
    ULONG_T                         lang;
    CHAR_T                          *langDesc;
    CHAR_T                          *preVideo;
    CHAR_T                          *stepVideo;
    UINT_T                          isMainShow;
    UINT_T                          isControl;
    UINT_T                          isShowControl;
    UINT_T                          sourceType;
    UINT_T                          cookType;
    CHAR_T                          *extInfo;
    UINT_T                          cookTime;
    UINT_T                          useFoodLib;
    UINT_T                          eatCount;
    UINT_T                          isFoodChannel;
    CHAR_T                          *easyLevel;
    CHAR_T                          *easyLevelDesc;
    CHAR_T                          *taste;
    CHAR_T                          *tasteDesc;
    CHAR_T                          *foodType;
    CHAR_T                          *foodTypeDesc;
    struct dl_list                  mlistHead_allergens;                //1级挂载:过敏原--->get_1_0_allergens_node_s
    CHAR_T                          *author;
    ULONG_T                         pv;
    DOUBLE_T                        avgScore;
    struct dl_list                  mlistHead_menuStepInfoVOList;       //1级挂载:图文步骤--->get_1_0_menuStepInfoVOList_node_s
    struct dl_list                  mlistHead_cookStepInfoVOList;       //1级挂载:烹饪步骤--->get_1_0_cookStepInfoVOList_node_s
    struct dl_list                  mlistHead_foodInfoVOList;           //1级挂载:食谱食材信息--->get_1_0_foodInfoVOList_node_s
    struct dl_list                  mlistHead_customInfoList;           //1级挂载:自定义属性--->get_1_0_customInfoList_node_s
    UINT64_T                        gmtCreate;
    UINT64_T                        gmtModified;
} TY_MENU_PARAM_GET_1_0_RSP_S;
/**************************************4.食谱详情**************************************/


/**************************************5.自定义食谱列表**************************************/
//--->请求参数
typedef struct {
    CHAR_T  *lang;                              //必须
    UINT_T  pageNo;
    UINT_T  pageSize;
} TY_MENU_PARAM_CUSTOM_LANG_LIST_1_0_S;

//<---响应参数
typedef struct {
    CHAR_T  *lang;                              //必须
    UINT_T  pageNo;
    UINT_T  pageSize;
} TY_MENU_PARAM_CUSTOM_LANG_LIST_1_0_RSP_S;
/**************************************5.自定义食谱列表**************************************/


/**************************************6.自定义食谱添加**************************************/
//--->请求参数
typedef struct {
    //食谱信息
    CHAR_T  *lang;                              //必须
    VOID    *mainImages;                        //必须
    CHAR_T  *name;                              //必须
    CHAR_T  *desc;
    CHAR_T  *productGroupId;                    //必须
    UCHAR_T isPublish;
    UINT_T  cookType;                           //必须
    CHAR_T  *foods;
    CHAR_T  *information;
    CHAR_T  *xyxk;
    UINT_T  cookTime;
    VOID    *stepInfoParams;                    //必须
    VOID    *cookStepParams;                    //必须
    //图文步骤
    UINT_T  img_step;
    CHAR_T  *img_stepImg;
    CHAR_T  *img_desc;
    //烹饪步骤信息
    UINT_T  cook_step;
    CHAR_T  *cook_stepImg;
    CHAR_T  *cook_desc;
    CHAR_T  *cook_finishCtrl;
    VOID    *cook_cookArgs;
} TY_MENU_PARAM_DIY_ADD_2_0_S;

//<---响应参数
typedef struct {
    //食谱信息
    CHAR_T  *lang;                              //必须
    VOID    *mainImages;                        //必须
    CHAR_T  *name;                              //必须
    CHAR_T  *desc;
    CHAR_T  *productGroupId;                    //必须
    UCHAR_T isPublish;
    UINT_T  cookType;                           //必须
    CHAR_T  *foods;
    CHAR_T  *information;
    CHAR_T  *xyxk;
    UINT_T  cookTime;
    VOID    *stepInfoParams;                    //必须
    VOID    *cookStepParams;                    //必须
    //图文步骤
    UINT_T  img_step;
    CHAR_T  *img_stepImg;
    CHAR_T  *img_desc;
    //烹饪步骤信息
    UINT_T  cook_step;
    CHAR_T  *cook_stepImg;
    CHAR_T  *cook_desc;
    CHAR_T  *cook_finishCtrl;
    VOID    *cook_cookArgs;
} TY_MENU_PARAM_DIY_ADD_2_0_RSP_S;
/**************************************6.自定义食谱添加**************************************/


/**************************************7.自定义食谱更新**************************************/
//--->请求参数
typedef struct {
    //食谱信息
    ULONG_T id;                                 //必须
    TY_MENU_PARAM_DIY_ADD_2_0_S update_info;
} TY_MENU_PARAM_DIY_UPDATE_2_0_S;

//<---响应参数
typedef struct {
    //食谱信息
    ULONG_T id;                                 //必须
    TY_MENU_PARAM_DIY_ADD_2_0_S update_info;
} TY_MENU_PARAM_DIY_UPDATE_2_0_RSP_S;
/**************************************7.自定义食谱更新**************************************/


/**************************************8.自定义食谱删除**************************************/
//--->请求参数
typedef struct {
    ULONG_T menuId;                             //必须
} TY_MENU_PARAM_DIY_DELETE_1_0_S;

//<---响应参数
typedef struct {
    ULONG_T menuId;                             //必须
} TY_MENU_PARAM_DIY_DELETE_1_0_RSP_S;
/**************************************8.自定义食谱删除**************************************/


/**************************************9.食谱收藏列表**************************************/
//--->请求参数
typedef struct {
    CHAR_T  *lang;                              //必须
    UINT_T  pageNo;
    UINT_T  pageSize;
} TY_MENU_PARAM_STAR_LIST_2_0_S;

//<---响应参数
typedef struct {
    CHAR_T  *lang;                              //必须
    UINT_T  pageNo;
    UINT_T  pageSize;
} TY_MENU_PARAM_STAR_LIST_2_0_RSP_S;
/**************************************9.食谱收藏列表**************************************/


/**************************************10.添加食谱收藏**************************************/
//--->请求参数
typedef struct {
    ULONG_T menuId;                             //必须
} TY_MENU_PARAM_STAR_ADD_1_0_S;

//<---响应参数
typedef struct {
    ULONG_T menuId;                             //必须
} TY_MENU_PARAM_STAR_ADD_1_0_RSP_S;
/**************************************10.添加食谱收藏**************************************/


/**************************************11.取消食谱收藏**************************************/
//--->请求参数
typedef struct {
    ULONG_T menuId;                             //必须
} TY_MENU_PARAM_STAR_DELETE_1_0_S;

//<---响应参数
typedef struct {
    ULONG_T menuId;                             //必须
} TY_MENU_PARAM_STAR_DELETE_1_0_RSP_S;
/**************************************11.取消食谱收藏**************************************/


/**************************************12.食谱是否收藏**************************************/
//--->请求参数
typedef struct {
    ULONG_T menuId;                             //必须
} TY_MENU_PARAM_STAR_CHECK_1_0_S;

//<---响应参数
typedef struct {
    ULONG_T menuId;                             //必须
} TY_MENU_PARAM_STAR_CHECK_1_0_RSP_S;
/**************************************12.食谱是否收藏**************************************/


/**************************************13.食谱收藏数量**************************************/
//--->请求参数
typedef struct {
    CHAR_T  *lang;                              //必须
} TY_MENU_PARAM_STAR_COUNT_1_0_S;

//<---响应参数
typedef struct {
    CHAR_T  *lang;                              //必须
} TY_MENU_PARAM_STAR_COUNT_1_0_RSP_S;
/**************************************13.食谱收藏数量**************************************/


/**************************************14.食谱添加评分**************************************/
//--->请求参数
typedef struct {
    ULONG_T menuId;                             //必须
    UCHAR_T score;
} TY_MENU_PARAM_SCORE_ADD_1_0_S;

//<---响应参数
typedef struct {
    ULONG_T menuId;                             //必须
    UCHAR_T score;
} TY_MENU_PARAM_SCORE_ADD_1_0_RSP_S;
/**************************************14.食谱添加评分**************************************/


/**************************************15.推荐设备列表和banner列表**************************************/
//--->请求参数
typedef struct {
    UCHAR_T bizType;
    CHAR_T  *lang;                              //必须
} TY_MENU_PARAM_BANNER_LIST_1_0_S;

//<---响应参数
typedef struct {
    UCHAR_T bizType;
    CHAR_T  *lang;                              //必须
} TY_MENU_PARAM_BANNER_LIST_1_0_RSP_S;
/**************************************15.推荐设备列表和banner列表**************************************/


/**************************************16.获取食谱所有语言**************************************/
//--->请求参数        (无)
//typedef struct {
//    
//} TY_COOKBOOK_PARAM_ALL_LANG_1_0_S;

//<---响应参数
typedef struct {
    UCHAR_T bizType;
} TY_COOKBOOK_PARAM_ALL_LANG_1_0_RSP_S;
/**************************************16.获取食谱所有语言**************************************/


/**************************************17.设备获取搜索模型**************************************/
//--->请求参数
typedef struct {
    CHAR_T  *lang;                              //必须
} TY_MENU_PARAM_QUERY_MODEL_LIST_1_0_S;

//<---响应参数
typedef struct {
    CHAR_T  *lang;                              //必须
} TY_MENU_PARAM_QUERY_MODEL_LIST_1_0_RSP_S;
/**************************************17.设备获取搜索模型**************************************/


/**************************************18.烹饪时间筛选条件**************************************/
//--->请求参数
typedef struct {
    UINT_T  sourceType;
    CHAR_T  *lang;
    CHAR_T  *name;
    VOID    *categoryIds;
} TY_MENU_PARAM_SEARCH_CONDITION_1_0_S;

//<---响应参数
typedef struct {
    UINT_T  sourceType;
    CHAR_T  *lang;
    CHAR_T  *name;
    VOID    *categoryIds;
} TY_MENU_PARAM_SEARCH_CONDITION_1_0_RSP_S;
/**************************************18.烹饪时间筛选条件**************************************/


/**************************************19.待同步食谱列表**************************************/
//--->请求参数
typedef struct {
    CHAR_T  *lang;
    CHAR_T  *name;
    VOID    *categoryIds;
    ULONG_T syncTime;
    UINT_T  pageNo;
    UINT_T  pageSize;
} TY_MENU_PARAM_SYNC_LIST_1_0_S;

//<---响应参数
typedef struct {
    CHAR_T  *lang;
    CHAR_T  *name;
    VOID    *categoryIds;
    ULONG_T syncTime;
    UINT_T  pageNo;
    UINT_T  pageSize;
} TY_MENU_PARAM_SYNC_LIST_1_0_RSP_S;
/**************************************19.待同步食谱列表**************************************/


/**************************************20.批量同步食谱详情**************************************/
//--->请求参数
typedef struct {
    CHAR_T  *lang;                              //必须
    VOID    *menuIds;
} TY_MENU_PARAM_SYNC_DETAILS_1_0_S;

//<---响应参数
typedef struct {
    CHAR_T  *lang;                              //必须
    VOID    *menuIds;
} TY_MENU_PARAM_SYNC_DETAILS_1_0_RSP_S;
/**************************************20.批量同步食谱详情**************************************/

#ifdef __cplusplus
}
#endif

#endif // __TUYA_GFW_GUI_WEATHER_H__
