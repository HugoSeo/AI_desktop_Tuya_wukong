/**
 * @file ty_gfw_gui_cloud_recipe.c
 * @brief file handle
 * @version 0.1
 * @date 2023-08-01
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

#include "ty_gfw_gui_kepler_menu.h"
#include "ty_gfw_gui_cloud_recipe.h"
#include "ty_gfw_gui_kepler_menu_get_list_handle.h"
#include "lv_vendor_event.h"

OPERATE_RET ty_gfw_gui_cloud_recipe_init(VOID)
{
    OPERATE_RET op_ret = OPRT_OK;

    op_ret = ty_gfw_kepler_gui_menu_req_list_init();
    if (op_ret == OPRT_OK)
        op_ret = ty_cloud_recipe_menu_parser_init();
    return op_ret;
}

OPERATE_RET ty_gfw_gui_cloud_recipe_req(VOID *param, UINT_T dl_unit_len)
{
    gui_cloud_recipe_interactive_s *cr_req = (gui_cloud_recipe_interactive_s *)param;

    return ty_gfw_kepler_gui_menu_init((GUI_CLOUD_RECIPE_REQ_TYPE_E)cr_req->req_type, cr_req->req_param, dl_unit_len);
}
