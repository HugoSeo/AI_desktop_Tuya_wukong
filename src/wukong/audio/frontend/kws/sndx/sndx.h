/*
 * Copyright (c) 2017 Spectimbre.
 */

#ifndef _SNDX_H_
#define _SNDX_H_

#include "tuya_cloud_types.h"
#include "wukong_kws.h"


/* ------------------------------------------------------------------------- */
#ifdef __cplusplus
extern "C" {
#endif

INT_T SNDX_kws_reset(WUKONG_KWS_CTX_T *ctx);
INT_T SNDX_kws_detect(WUKONG_KWS_CTX_T *ctx, UINT8_T *data, UINT32_T datalen);
INT_T SNDX_kws_create(WUKONG_KWS_CTX_T *ctx);
INT_T SNDX_kws_deinit(WUKONG_KWS_CTX_T *ctx);

#ifdef __cplusplus
}
#endif

#endif // _SNDX_H_
