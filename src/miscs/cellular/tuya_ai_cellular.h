/*
 * tuya_ai_battery.h
 * Copyright (C) 2025 cc <cc@tuya>
 *
 * Distributed under terms of the MIT license.
 */

#ifndef __TUYA_AI_CELLULAT_H__
#define __TUYA_AI_CELLULAT_H__

#include "tuya_cloud_types.h"
#include "tuya_app_config.h"
#include "tuya_device_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

VOID_T tuya_ai_cellular_module_boot(VOID_T);
OPERATE_RET tuya_ai_cellular_init(VOID_T);
OPERATE_RET tuya_ai_cellular_upload_iccid(VOID_T);
#ifdef __cplusplus
}
#endif

#endif /* !__TUYA_AI_CELLULAT_H__ */

