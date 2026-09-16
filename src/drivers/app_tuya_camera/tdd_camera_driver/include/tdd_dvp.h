/*
 * tdd_dvp.h
 * Copyright (C) 2024 cc <cc@tuya>
 *
 * Distributed under terms of the MIT license.
 */

#ifndef __TDD_DVP_H__
#define __TDD_DVP_H__

#include "tuya_cloud_types.h"
#include "tkl_display.h"
// #include "tuya_app_gui_core_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TUYA_MULTI_TYPES_DVP        1
#define DVP_MODULE_GC2145        1
#define TUYA_DVP_DEVICE_T50P181CQ   1

VOID *tdd_dvp_driver_query(CONST CHAR_T *name);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* !__TDD_DVP_H__ */
