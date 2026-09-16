/*
 * Copyright (c) 2017 Spectimbre.
 */

#ifndef _TUTUCLEAR_H_
#define _TUTUCLEAR_H_

#include "tutu_typedef.h"
#include "tuya_cloud_types.h"
#include "wukong_kws.h"

// Set below to TUTUClearConfig_t.Version for version control
#define TUTUCLEAR_VERSION   MAKETUTUVER(2, 8, 0)


/* ------------------------------------------------------------------------- */
#ifdef __cplusplus
extern "C" {
#endif

INT_T TUTUClear_kws_reset(WUKONG_KWS_CTX_T *ctx);
INT_T TUTUClear_kws_detect(WUKONG_KWS_CTX_T *ctx, UINT8_T *data, UINT32_T datalen);
INT_T TUTUClear_kws_create(WUKONG_KWS_CTX_T *ctx);
INT_T TUTUClear_kws_deinit(WUKONG_KWS_CTX_T *ctx);
#ifdef __cplusplus
}
#endif

#endif // _TUTUCLEAR_H_
