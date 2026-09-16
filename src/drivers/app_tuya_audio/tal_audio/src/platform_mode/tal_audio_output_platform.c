/**
 * @file tal_audio_output_platform.c
 * @brief Platform-mode output backend: map tal_audio_output_* onto legacy
 *        vendor tkl_ao_*. The legacy ao is parasitic on tkl_ai_init (real
 *        init happens on the input path); output_init only caches PA/sample
 *        params for the input side and defers readiness checks to the
 *        underlying driver (RESOURCE_NOT_READY before input is up).
 */
#include "tuya_iot_config.h"

#ifdef USER_SW_VER
#include "tuya_app_config.h"   /* app-level Kconfig macros (ENABLE_WUKONG_AUDIO_PIPELINE) */
#endif

/* platform mode is the ENABLE_WUKONG_AUDIO_PIPELINE=n branch (all-chip legacy tkl path) */
#if !defined(ENABLE_WUKONG_AUDIO_PIPELINE) || (ENABLE_WUKONG_AUDIO_PIPELINE == 0)

#include <string.h>

#include "tal_audio_output.h"
#include "tal_audio_platform.h"
#include "tkl_audio.h"
#include "uni_log.h"

typedef struct {
    BOOL_T  used;
    VOID_T *tkl_handle;   /* legacy pseudo handle: (void*)1 once voice inited */
} TAL_AO_PLATFORM_CTRL_T;

STATIC TAL_AO_PLATFORM_CTRL_T s_ao_ctrl = {0};

TAL_AUDIO_OUTPUT_HANDLE tal_audio_output_init(TAL_AUDIO_OUTPUT_CFG_T *config)
{
    if (config == NULL) {
        PR_ERR("platform ao: config is NULL");
        return NULL;
    }
    if (config->type != TAL_AUDIO_OUTPUT_DAC) {
        PR_ERR("platform ao: type %d not supported (board DAC only)", config->type);
        return NULL;
    }
    if (s_ao_ctrl.used) {
        PR_ERR("platform ao: already inited");
        return NULL;
    }
    /* port/spk_num/frame_time_ms have no legacy equivalent: ignored */
    PR_DEBUG("platform ao: ignore port=%d spk_num=%u frame_ms=%u",
             config->dev.dac_config.port, config->dev.dac_config.spk_num,
             config->frame_time_ms);

    TAL_AUDIO_PLATFORM_CTX_T *ctx = tal_audio_platform_ctx();

    /* cache speaker params for the input-side tkl_ai_init (legacy inits
     * mic + speaker + PA together from the ai config) */
    ctx->spk_sample = config->sample_rate;
    ctx->spk_gpio   = (INT32_T)config->dev.dac_config.pa_gpio;
    /* legacy polarity field is inverted: 0 means active-HIGH */
    ctx->spk_gpio_polarity = (config->dev.dac_config.pa_active_level == TUYA_GPIO_LEVEL_HIGH)
                                 ? (INT32_T)TUYA_GPIO_LEVEL_LOW
                                 : (INT32_T)TUYA_GPIO_LEVEL_HIGH;
    ctx->out_cfg_cached = TRUE;

    if (ctx->voice_inited) {
        /* input already up: legacy ao_init just validates and hands out (void*)1 */
        TKL_AUDIO_CONFIG_T tkl_cfg;
        memset(&tkl_cfg, 0, sizeof(tkl_cfg));
        tkl_cfg.sample    = (TKL_AUDIO_SAMPLE_E)config->sample_rate;
        tkl_cfg.datebits  = (TKL_AUDIO_DATABITS_E)config->sample_bits;
        tkl_cfg.channel   = TKL_AUDIO_CHANNEL_MONO;
        tkl_cfg.codectype = TKL_CODEC_AUDIO_PCM;
        if (tkl_ao_init(&tkl_cfg, 1, &s_ao_ctrl.tkl_handle) != OPRT_OK) {
            PR_ERR("platform ao: tkl_ao_init failed");
            return NULL;
        }
    } else {
        /* input not up yet (normal app order: player init first). Legacy
         * semantics: playback becomes usable after input side tkl_ai_init/
         * start; until then write/set_vol return RESOURCE_NOT_READY. */
        s_ao_ctrl.tkl_handle = NULL;
        PR_NOTICE("platform ao: voice not inited yet, params cached for input side");
    }

    s_ao_ctrl.used = TRUE;
    return (TAL_AUDIO_OUTPUT_HANDLE)&s_ao_ctrl;
}

/* start/stop/set_volume/write are thin passthroughs: the legacy driver gates
 * every call on its own audio_init/audio_start state (RESOURCE_NOT_READY
 * before the input side brings the voice path up), duplicating those checks
 * here adds nothing. */
OPERATE_RET tal_audio_output_start(TAL_AUDIO_OUTPUT_HANDLE handle)
{
    (void)handle;
    /* underlying write task is started by tkl_ai_start and this is an
     * idempotent re-arm (no-op when voice not up yet) */
    return tkl_ao_start(TKL_AUDIO_TYPE_BOARD, TKL_AO_0, s_ao_ctrl.tkl_handle);
}

OPERATE_RET tal_audio_output_stop(TAL_AUDIO_OUTPUT_HANDLE handle)
{
    (void)handle;
    /* legacy tkl_ao_stop is a deliberate no-op for the BOARD card (keeps the
     * shared write path alive for direct writers). MUST stay a passthrough.
     * vendor returns OPRT_NOT_SUPPORTED on that no-op path while playing;
     * normalize to OK to keep the same contract as the tuya-mechanism stop. */
    OPERATE_RET rt = tkl_ao_stop(TKL_AUDIO_TYPE_BOARD, TKL_AO_0, s_ao_ctrl.tkl_handle);
    return (rt == OPRT_NOT_SUPPORTED) ? OPRT_OK : rt;
}

OPERATE_RET tal_audio_output_set_volume(TAL_AUDIO_OUTPUT_HANDLE handle, UINT8_T volume)
{
    (void)handle;
    return tkl_ao_set_vol(TKL_AUDIO_TYPE_BOARD, TKL_AO_0, s_ao_ctrl.tkl_handle, (INT32_T)volume);
}

OPERATE_RET tal_audio_output_write(TAL_AUDIO_OUTPUT_HANDLE handle, UINT8_T *buf, UINT32_T len)
{
    (void)handle;
    /* buf/len check stays: vendor put_frame dereferences pbuf without validating */
    if ((buf == NULL) || (len == 0U)) {
        return OPRT_INVALID_PARM;
    }
    TKL_AUDIO_FRAME_INFO_T frame;
    memset(&frame, 0, sizeof(frame));
    frame.type      = TKL_AUDIO_FRAME;
    frame.codectype = TKL_CODEC_AUDIO_PCM;
    frame.pbuf      = (CHAR_T *)buf;
    frame.buf_size  = len;
    frame.used_size = len;
    /* legacy put_frame has a bounded internal retry/timeout (returns
     * OPRT_TIMEOUT when the DAC is not consuming) */
    return tkl_ao_put_frame(TKL_AUDIO_TYPE_BOARD, TKL_AO_0, s_ao_ctrl.tkl_handle, &frame);
}

OPERATE_RET tal_audio_output_deinit(TAL_AUDIO_OUTPUT_HANDLE handle)
{
    (void)handle;
    if (!s_ao_ctrl.used) {
        return OPRT_INVALID_PARM;
    }
    tkl_ao_uninit(s_ao_ctrl.tkl_handle);
    s_ao_ctrl.used = FALSE;
    s_ao_ctrl.tkl_handle = NULL;
    tal_audio_platform_ctx()->out_cfg_cached = FALSE;
    return OPRT_OK;
}

#endif /* !ENABLE_WUKONG_AUDIO_PIPELINE (platform mode) */
