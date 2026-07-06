/*
 * dvp_common.c
 * Copyright (C) 2024 cc <cc@tuya>
 *
 * Distributed under terms of the MIT license.
 */

#include "string.h"
#include "tuya_cloud_types.h"
#include "tdd_dvp.h"
#include "tkl_system.h"
#include "dvp_common.h"
#include "tal_log.h"
extern TDD_DVP_SENSOR_DEVICE_S dvp_gc2145_device;
#ifdef TUYA_MULTI_TYPES_DVP
static const TDD_DVP_SENSOR_DEVICE_S *tuya_dvp_devices [] = {
#if defined(DVP_MODULE_GC2145)
    &dvp_gc2145_device,
#else
#error "undefine dvp device"
#endif
};

#endif

VOID *tdd_dvp_driver_query(CONST CHAR_T *name)
{
    bk_printf("---trace %s %d, name: %s\r\n", __func__, __LINE__, name);
    if (name == NULL) {
        return NULL;
    }

#ifdef TUYA_MULTI_TYPES_DVP
    for (int i = 0; i < sizeof(tuya_dvp_devices)/sizeof(TDD_DVP_SENSOR_DEVICE_S *); i++) {
        if (0 == strcmp(name, tuya_dvp_devices[i]->name)) {
            return tuya_dvp_devices[i];
        }
    }
#else
    for (int i = 0; i < sizeof(tuya_dvp_devices)/sizeof(TDD_DVP_SENSOR_DEVICE_S *); i++) {
    bk_printf("---trace %s %d, dev: %p\r\n", __func__, __LINE__, tuya_dvp_devices[i]);
        if (tuya_dvp_devices[i] == NULL)
            break;
        if (0 == strcmp(name, tuya_dvp_devices[i]->name)) {
            return tuya_dvp_devices[i];
        }
    }
#endif
    TAL_PR_WARN("not found dvp driver: %s", name);
    return NULL;
}

