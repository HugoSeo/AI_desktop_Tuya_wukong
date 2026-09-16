/*
 * dvp_common.h
 * Copyright (C) 2024 cc <cc@tuya>
 *
 * Distributed under terms of the MIT license.
 */

#include "tdd_dvp.h"
#include "tal_dvp.h"

#ifndef DVP_COMMON_H
#define DVP_COMMON_H
typedef struct 
{
    char *name;
    TUYA_DVP_SENSOR_CFG_T *device;
}TDD_DVP_SENSOR_DEVICE_S;

#ifdef TUYA_MULTI_TYPES_DVP

#endif

#endif /* !DVP_COMMON_H */
