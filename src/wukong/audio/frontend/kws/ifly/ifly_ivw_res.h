#ifndef __IFLY_IVW_RES_H__
#define __IFLY_IVW_RES_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Resource selection
 * ---------------------------------------------------------------------------
 * Define IFLY_IVW_USE_TUYA_RES=1 to use the shallow-customized Tuya wakeword
 * resources (你好涂鸦 / 嘿涂鸦, tag1201).
 * Leave undefined (or set to 0) to use the original ivw80 resources.
 * --------------------------------------------------------------------------- */
#ifndef IFLY_IVW_USE_TUYA_RES
#define IFLY_IVW_USE_TUYA_RES 1
#endif

/* ---------------------------------------------------------------------------
 * Original ivw80 resources (keyword_main.bin + mlp_output.bin)
 * --------------------------------------------------------------------------- */
extern const unsigned char ifly_ivw_keyword_res[];
extern const unsigned int  ifly_ivw_keyword_res_len;

extern const unsigned char ifly_ivw_mlp_res[];
extern const unsigned int  ifly_ivw_mlp_res_len;

/* ---------------------------------------------------------------------------
 * Tuya shallow-customized resources (tag1201):
 *   filler_keywords_tuyacombine.bin
 *   mlp_deep_low_MTK_only_frame_20260210_tuya_combine.bin
 * --------------------------------------------------------------------------- */
extern const unsigned char ifly_ivw_tuya_keyword_res[];
extern const unsigned int  ifly_ivw_tuya_keyword_res_len;

extern const unsigned char ifly_ivw_tuya_mlp_res[];
extern const unsigned int  ifly_ivw_tuya_mlp_res_len;

/* ---------------------------------------------------------------------------
 * Unified aliases resolved by IFLY_IVW_USE_TUYA_RES
 * --------------------------------------------------------------------------- */
#if IFLY_IVW_USE_TUYA_RES
#define IFLY_IVW_ACTIVE_KEYWORD_RES      ifly_ivw_tuya_keyword_res
#define IFLY_IVW_ACTIVE_KEYWORD_RES_LEN  ifly_ivw_tuya_keyword_res_len
#define IFLY_IVW_ACTIVE_MLP_RES          ifly_ivw_tuya_mlp_res
#define IFLY_IVW_ACTIVE_MLP_RES_LEN      ifly_ivw_tuya_mlp_res_len
#else
#define IFLY_IVW_ACTIVE_KEYWORD_RES      ifly_ivw_keyword_res
#define IFLY_IVW_ACTIVE_KEYWORD_RES_LEN  ifly_ivw_keyword_res_len
#define IFLY_IVW_ACTIVE_MLP_RES          ifly_ivw_mlp_res
#define IFLY_IVW_ACTIVE_MLP_RES_LEN      ifly_ivw_mlp_res_len
#endif

#ifdef __cplusplus
}
#endif

#endif /* __IFLY_IVW_RES_H__ */
