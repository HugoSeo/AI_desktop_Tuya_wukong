/**
 * @file tal_audio_input_platform.c
 * @brief Platform-mode input backend: map tal_audio_input_* onto legacy
 *        vendor tkl_ai_* (bk_voice capture + driver-internal AFE).
 *        Delivered frames are AFE-processed MONO (not raw stereo LR).
 */
#include "tuya_iot_config.h"

#ifdef USER_SW_VER
#include "tuya_app_config.h"   /* app-level Kconfig macros (ENABLE_WUKONG_AUDIO_PIPELINE) */
#endif

/* platform mode is the ENABLE_WUKONG_AUDIO_PIPELINE=n branch (all-chip legacy tkl path) */
#if !defined(ENABLE_WUKONG_AUDIO_PIPELINE) || (ENABLE_WUKONG_AUDIO_PIPELINE == 0)

#if defined(USING_SPRS_AUDIO_FRONTEND) && (USING_SPRS_AUDIO_FRONTEND == 1)
#error "SPRS dual-mic frontend requires ENABLE_WUKONG_AUDIO_PIPELINE=y (wukong pipeline)"
#endif

#include <string.h>

#include "tal_audio_input.h"
#include "tal_audio_platform.h"
#include "tkl_audio.h"
#include "tkl_system.h"
#include "uni_log.h"

typedef struct {
    BOOL_T             used;
    TAL_AUDIO_INPUT_CB cb;
    VOID_T            *args;
    UINT32_T           seq_no;
} TAL_AI_PLATFORM_CTRL_T;

STATIC TAL_AI_PLATFORM_CTRL_T s_ai_ctrl = {0};
STATIC TAL_AUDIO_PLATFORM_CTX_T s_platform_ctx = {0};

TAL_AUDIO_PLATFORM_CTX_T *tal_audio_platform_ctx(VOID_T)
{
    return &s_platform_ctx;
}

/* tkl put_cb shim: wrap legacy frame into TAL_AUDIO_FRAME_T.
 * Runs in the vendor voice-read task context: copy/deliver quickly. */
STATIC INT_T __platform_frame_put(TKL_AUDIO_FRAME_INFO_T *pframe)
{
    /* snapshot cb/args once: deinit may clear them concurrently from another
     * thread and the voice-read task must never call through a torn read */
    TAL_AUDIO_INPUT_CB cb = s_ai_ctrl.cb;
    VOID_T *args = s_ai_ctrl.args;

    if (!s_ai_ctrl.used || (cb == NULL) ||
        (pframe == NULL) || (pframe->pbuf == NULL)) {
        return 0;
    }

    TAL_AUDIO_FRAME_T frame;
    frame.event      = TUYA_AUDIO_FRAME_EVENT_ADC_RX;
    frame.buf        = (UINT8_T *)pframe->pbuf;
    frame.len        = pframe->buf_size;
    frame.seq_no     = s_ai_ctrl.seq_no++;
    frame.time_stamp = tkl_system_get_millisecond();
    cb(&frame, args);
    return (INT_T)frame.len;
}

TAL_AUDIO_INPUT_HANDLE tal_audio_input_init(TAL_AUDIO_INPUT_CFG_T *config)
{
    if ((config == NULL) || (config->audio_input_cb == NULL)) {
        PR_ERR("platform ai: invalid config");
        return NULL;
    }
    if (config->type != TAL_AUDIO_INPUT_ADC) {
        PR_ERR("platform ai: type %d not supported (board ADC only)", config->type);
        return NULL;
    }
    if (s_ai_ctrl.used) {
        PR_ERR("platform ai: already inited");
        return NULL;
    }
    /* port/chan/frame_time_ms have no legacy equivalent: accepted and ignored */
    PR_DEBUG("platform ai: ignore port=%d chan=%d frame_ms=%u",
             config->dev.ai_adc_conf.port, config->dev.ai_adc_conf.chan,
             config->frame_time_ms);

    TKL_AUDIO_CONFIG_T tkl_cfg;
    memset(&tkl_cfg, 0, sizeof(tkl_cfg));
    tkl_cfg.enable    = 1;                       /* vendor AFE(AEC,ND,VAD) on */
    tkl_cfg.card      = TKL_AUDIO_TYPE_BOARD;
    tkl_cfg.ai_chn    = TKL_AI_0;
    tkl_cfg.sample    = (TKL_AUDIO_SAMPLE_E)config->sample_rate;
    tkl_cfg.datebits  = (TKL_AUDIO_DATABITS_E)config->sample_bits;
    tkl_cfg.channel   = TKL_AUDIO_CHANNEL_MONO;
    tkl_cfg.codectype = TKL_CODEC_AUDIO_PCM;
    tkl_cfg.put_cb    = __platform_frame_put;

    /* speaker side params come from the cached output cfg (output_init runs
     * first in the app flow); fall back to sane defaults otherwise */
    if (s_platform_ctx.out_cfg_cached) {
        tkl_cfg.spk_sample        = (int)s_platform_ctx.spk_sample;
        tkl_cfg.spk_gpio          = s_platform_ctx.spk_gpio;
        tkl_cfg.spk_gpio_polarity = s_platform_ctx.spk_gpio_polarity;
    } else {
        tkl_cfg.spk_sample = (int)config->sample_rate;
        tkl_cfg.spk_gpio   = 0xFF;                /* >=56: vendor treats as no PA amplifier (tkl_audio.c checks spk_gpio < 56) */
    }

    if (tkl_ai_init(&tkl_cfg, 0) != OPRT_OK) {
        PR_ERR("platform ai: tkl_ai_init failed");
        return NULL;
    }

    s_ai_ctrl.used   = TRUE;
    s_ai_ctrl.cb     = config->audio_input_cb;
    s_ai_ctrl.args   = config->args;
    s_ai_ctrl.seq_no = 0;
    s_platform_ctx.voice_inited = TRUE;
    PR_NOTICE("platform ai: inited, sample=%u", config->sample_rate);
    return (TAL_AUDIO_INPUT_HANDLE)&s_ai_ctrl;
}

/* start/stop/set_volume are thin passthroughs: the legacy driver already
 * gates every call on its own audio_init/audio_start state, adding another
 * validation layer here would only duplicate it. */
OPERATE_RET tal_audio_input_start(TAL_AUDIO_INPUT_HANDLE handle)
{
    (void)handle;
    return tkl_ai_start(TKL_AUDIO_TYPE_BOARD, TKL_AI_0);
}

OPERATE_RET tal_audio_input_stop(TAL_AUDIO_INPUT_HANDLE handle)
{
    (void)handle;
    return tkl_ai_stop(TKL_AUDIO_TYPE_BOARD, TKL_AI_0);
}

OPERATE_RET tal_audio_input_set_volume(TAL_AUDIO_INPUT_HANDLE handle, UINT8_T volume)
{
    (void)handle;
    return tkl_ai_set_vol(TKL_AUDIO_TYPE_BOARD, TKL_AI_0, (INT32_T)volume);
}

OPERATE_RET tal_audio_input_deinit(TAL_AUDIO_INPUT_HANDLE handle)
{
    (void)handle;
    if (!s_ai_ctrl.used) {
        return OPRT_INVALID_PARM;
    }
    s_ai_ctrl.used = FALSE;
    s_ai_ctrl.cb   = NULL;
    tkl_ai_uninit();
    s_platform_ctx.voice_inited = FALSE;
    return OPRT_OK;
}

#endif /* !ENABLE_WUKONG_AUDIO_PIPELINE (platform mode) */
