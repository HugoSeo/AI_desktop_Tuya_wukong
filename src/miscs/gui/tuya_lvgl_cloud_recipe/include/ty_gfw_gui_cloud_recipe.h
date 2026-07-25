/**
 * @file ty_gfw_gui_cloud_recipe.h
 * @brief
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

#ifndef __TUYA_GFW_GUI_CLOUD_RECIPE_H__
#define __TUYA_GFW_GUI_CLOUD_RECIPE_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

OPERATE_RET ty_gfw_gui_cloud_recipe_init(VOID);

OPERATE_RET ty_gfw_gui_cloud_recipe_req(VOID *param, UINT_T dl_unit_len);

#ifdef __cplusplus
}
#endif

#endif // __TUYA_GFW_GUI_FILE_H__