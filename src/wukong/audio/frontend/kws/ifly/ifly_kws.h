#ifndef __IFLY_KWS_H__
#define __IFLY_KWS_H__

#include "wukong_kws.h"

#ifdef __cplusplus
extern "C" {
#endif

INT_T ifly_kws_create(WUKONG_KWS_CTX_T *ctx);
INT_T ifly_kws_detect(WUKONG_KWS_CTX_T *ctx, UINT8_T *data, UINT32_T datalen);
INT_T ifly_kws_reset(WUKONG_KWS_CTX_T *ctx);
INT_T ifly_kws_deinit(WUKONG_KWS_CTX_T *ctx);

#ifdef __cplusplus
}
#endif

#endif /* __IFLY_KWS_H__ */
