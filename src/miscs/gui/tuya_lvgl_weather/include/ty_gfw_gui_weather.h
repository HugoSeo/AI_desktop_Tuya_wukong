/**
 * @file ty_gfw_gui_weather.h
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

#ifndef __TUYA_GFW_GUI_WEATHER_H__
#define __TUYA_GFW_GUI_WEATHER_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 获取天气数据初始化
 * @param[in]  pArr:天气code
 * @return 执行结果,返回 OPRT_OK表示成功，返回 其他 则表示失败
 * @note
 */
OPERATE_RET ty_gfw_gui_weather_init(IN CHAR_T *pArr);

OPERATE_RET ty_gfw_gui_weather_get(CHAR_T **out_weather, UINT_T *out_len);

#ifdef __cplusplus
}
#endif

#endif // __TUYA_GFW_GUI_WEATHER_H__