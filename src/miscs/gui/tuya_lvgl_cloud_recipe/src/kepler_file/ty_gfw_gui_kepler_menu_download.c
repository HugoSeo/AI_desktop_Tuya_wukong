/**
 * @file ty_gfw_gui_kepler_menu_download.c
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

#include "ty_gfw_gui_kepler_menu_download.h"
#include "ty_gfw_gui_kepler_menu.h"

/***********************************************************
*************************micro define***********************
***********************************************************/

/***********************************************************
***********************variable define**********************
***********************************************************/
BOOL_T ty_gfw_gui_menu_download_get_run_is(VOID)
{
    return (tfm_kepler_gui_menu_download_is_initialized());
}

OPERATE_RET ty_gfw_gui_menu_download_init(CR_REQ_LIST_NODE_S *req_node)
{
    OPERATE_RET op_ret = OPRT_OK;

    op_ret = tfm_kepler_gui_menu_download_init(req_node);

    return op_ret;
}
