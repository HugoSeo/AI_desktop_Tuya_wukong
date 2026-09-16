/**
 * @file tuya_cellular_cloud.h
 * @brief
 * @version 0.1
 * @date 2023-06-26
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

 #ifndef __TUYA_CELLULAR_CLOUD_H__
 #define __TUYA_CELLULAR_CLOUD_H__

 #ifdef __cplusplus
 extern "C" {
 #endif
 #include "tuya_cloud_types.h"
 #include "oc_app_entry.h"

 #define MQTT_KEEPALIVE 120

 OPERATE_RET tuya_cellular_cloud_init(VOID);

 OPERATE_RET tuya_cellular_cloud_start(TY_IOT_CBS_S* cbs,CONST CHAR_T* pid,CONST CHAR_T* appv,CONST CHAR_T* fw_key,UINT_T mqtt_time);


 #ifdef __cplusplus
 }
 #endif
 #endif
