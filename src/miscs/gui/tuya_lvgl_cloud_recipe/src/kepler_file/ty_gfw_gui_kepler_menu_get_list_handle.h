/**
 * @file ty_gfw_gui_kepler_menu_get_list_handle.h
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

#ifndef __TY_GFW_GUI_KEPLER_MENU_GET_LIST_HDL_H_
#define __TY_GFW_GUI_KEPLER_MENU_GET_LIST_HDL_H_

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief: 文件下载回调
 * @desc: 文件下载开始回调
 * @return VOID
 * @note none
 */
typedef OPERATE_RET (*CLOUD_RECIPE_DL_CB)(OUT UINT_T *total_len, OUT VOID **out_buff, IN CHAR_T *p_url);

//云菜谱列表解析器申请内存
VOID *cr_parser_malloc(IN UINT_T buf_len);

//云菜谱列表解析器释放内存
OPERATE_RET cr_parser_free(IN VOID *ptr);

OPERATE_RET ty_cloud_recipe_menu_parser_init(VOID);

OPERATE_RET ty_cloud_recipe_menu_dl_register_cb(CLOUD_RECIPE_DL_CB cb);

OPERATE_RET ty_cloud_recipe_menu_category_list_1_0_parse(IN VOID *json);

OPERATE_RET ty_cloud_recipe_menu_category_list_2_0_parse(IN VOID *json);

OPERATE_RET ty_cloud_recipe_menu_lang_list_1_0_parse(IN VOID *json);

OPERATE_RET ty_cloud_recipe_menu_get_1_0_parse(IN VOID *json);

OPERATE_RET ty_cloud_recipe_menu_custom_lang_list_1_0_parse(IN VOID *json);

OPERATE_RET ty_cloud_recipe_menu_diy_add_2_0_parse(IN VOID *json);

OPERATE_RET ty_cloud_recipe_menu_diy_update_2_0_parse(IN VOID *json);

OPERATE_RET ty_cloud_recipe_menu_diy_delete_1_0_parse(IN VOID *json);

OPERATE_RET ty_cloud_recipe_menu_star_list_2_0_parse(IN VOID *json);

OPERATE_RET ty_cloud_recipe_menu_star_add_1_0_parse(IN VOID *json);

OPERATE_RET ty_cloud_recipe_menu_star_delete_1_0_parse(IN VOID *json);

OPERATE_RET ty_cloud_recipe_menu_star_check_1_0_parse(IN VOID *json);

OPERATE_RET ty_cloud_recipe_menu_star_count_1_0_parse(IN VOID *json);

OPERATE_RET ty_cloud_recipe_menu_score_add_1_0_parse(IN VOID *json);

OPERATE_RET ty_cloud_recipe_menu_banner_lsit_1_0_parse(IN VOID *json);

OPERATE_RET ty_cloud_recipe_cookbook_all_lang_1_0_parse(IN VOID *json);

OPERATE_RET ty_cloud_recipe_menu_query_model_list_1_0_parse(IN VOID *json);

OPERATE_RET ty_cloud_recipe_menu_search_condition_1_0_parse(IN VOID *json);

OPERATE_RET ty_cloud_recipe_menu_sync_list_1_0_parse(IN VOID *json);

OPERATE_RET ty_cloud_recipe_menu_sync_details_1_0_parse(IN VOID *json);

OPERATE_RET ty_cloud_recipe_menu_resp_send(UINT32_T rsp_type, VOID *param);

#ifdef __cplusplus
} // extern "C"
#endif
#endif
