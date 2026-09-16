/**
 * @file tal_audio_platform.h
 * @brief Platform-mode backend shared context (vendor tkl_audio path).
 *        Real voice init is anchored on the input path (tkl_ai_init inits
 *        mic + speaker + AFE together); output side only caches PA params.
 */
#ifndef __TAL_AUDIO_PLATFORM_H__
#define __TAL_AUDIO_PLATFORM_H__

#include "tuya_iot_config.h"
#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    BOOL_T   voice_inited;        /* tkl_ai_init done (input side owns the voice path) */

    /* output params cached by output_init (which runs before input_init in app flow) */
    BOOL_T   out_cfg_cached;
    UINT32_T spk_sample;
    INT32_T  spk_gpio;            /* >=56: vendor treats as no PA amplifier */
    INT32_T  spk_gpio_polarity;   /* legacy inverted semantics: 0 = active-HIGH */
} TAL_AUDIO_PLATFORM_CTX_T;

TAL_AUDIO_PLATFORM_CTX_T *tal_audio_platform_ctx(VOID_T);

#ifdef __cplusplus
}
#endif
#endif
