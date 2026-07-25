/**
 * @file ifly_kws.c
 * @brief IFlytek IVW keyword spotting engine adapter for wukong KWS framework
 *
 * Follows the same pattern as tutuclear/tutuclear.c.
 *
 * Audio flow (from wukong_kws worker thread):
 *   wukong_kws_feed_with_vad  →  ring-buffer  →  detect(data, datalen)
 *   detect: split datalen bytes into 160-sample (320-byte) frames and feed
 *           each frame to ivw_module_process_audio.
 *   Wake-up events arrive asynchronously via the IVW wakeup_callback which
 *   parses the keyword string and calls wukong_kws_event().
 *   Once a wakeup is detected, s_wakeup_flag is set so detect() returns
 *   OPRT_OK immediately, letting the framework reset the ring buffer and
 *   stop feeding stale frames into the engine.
 */

#include "ifly_kws.h"
#include "ifly_ivw_res.h"
#include "wukong_kws.h"
#include "ivw_module.h"
#include "tal_log.h"
#include "tal_memory.h"
#include "tal_system.h"

#include <string.h>

/* IVW expects 160-sample (320-byte) frames at 16kHz/16bit/mono */
#define IFLY_IVW_FRAME_SAMPLES  160
#define IFLY_IVW_FRAME_BYTES    (IFLY_IVW_FRAME_SAMPLES * sizeof(int16_t))

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
/**
 * Set to 1 inside the wakeup callback; cleared at the start of each detect()
 * call. When set, detect() stops feeding frames and returns OPRT_OK so the
 * framework resets the ring buffer immediately.
 */
STATIC volatile INT_T s_wakeup_flag = 0;

/* ---------------------------------------------------------------------------
 * Internal wakeup callback
 * --------------------------------------------------------------------------- */
/**
 * @brief IVW wakeup callback; called from ivw_module_process_audio context
 * @param[in] param JSON result string from IVW engine
 * @param[in] userparam Unused
 * @return 0
 */
STATIC INT_T __ifly_ivw_wakeup_cb(const char *param, VOID *userparam)
{
    if (!param) {
        return 0;
    }

    TAL_PR_DEBUG("ifly ivw wakeup: %s", param);

    /* Signal detect() to stop feeding frames and return OPRT_OK */
    s_wakeup_flag = 1;

    /* Map keyword string to WUKONG_KWS_INDEX_E */
    /* Original ivw80 resources use English identifiers (nihaotuya / heytuya).
     * Tuya shallow-customized resources (tag1201) return pinyin strings:
     *   你好涂鸦 → "ni3 hao3 tu2 ya1"   (iresid 0)
     *   嘿涂鸦   → "hei4 tu2 ya1"        (iresid 1) */
    if (strstr(param, "nihaotuya") != NULL ||
        strstr(param, "ni3 hao3 tu2 ya1") != NULL) {
        wukong_kws_event(WUKONG_KWS_NIHAOTUYA);
    } else if (strstr(param, "heytuya") != NULL ||
               strstr(param, "hei4 tu2 ya1") != NULL) {
        wukong_kws_event(WUKONG_KWS_HEYTUYA);
    } else if (strstr(param, "xiaozhitongxue") != NULL) {
        wukong_kws_event(WUKONG_KWS_XIAOZHITONGXUE);
    } else if (strstr(param, "xiao3 fei1 xiao3 fei1") != NULL) {
        wukong_kws_event(WUKONG_KWS_NIHAOTUYA);
    } else {
        TAL_PR_DEBUG("ifly ivw: unknown keyword [%s]", param);
    }

    return 0;
}

/* --------------------------------------------------------------------------
 * WUKONG_KWS_CFG_T callbacks
 * -------------------------------------------------------------------------- */

INT_T ifly_kws_create(WUKONG_KWS_CTX_T *ctx)
{
#if 0 /* test: copy resource to writable PSRAM memory */
    char *mlp_buf = (char *)tal_psram_malloc(IFLY_IVW_ACTIVE_MLP_RES_LEN);
    char *kw_buf  = (char *)tal_psram_malloc(IFLY_IVW_ACTIVE_KEYWORD_RES_LEN);
    if (!mlp_buf || !kw_buf) {
        TAL_PR_ERR("ifly_kws_create: malloc res buf failed");
        if (mlp_buf) tal_free(mlp_buf);
        if (kw_buf)  tal_free(kw_buf);
        return OPRT_MALLOC_FAILED;
    }
    memcpy(mlp_buf, IFLY_IVW_ACTIVE_MLP_RES, IFLY_IVW_ACTIVE_MLP_RES_LEN);
    memcpy(kw_buf,  IFLY_IVW_ACTIVE_KEYWORD_RES, IFLY_IVW_ACTIVE_KEYWORD_RES_LEN);

    ivw_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.mlp_res_data      = mlp_buf;
    cfg.mlp_res_size      = IFLY_IVW_ACTIVE_MLP_RES_LEN;
    cfg.keyword_res_data  = kw_buf;
    cfg.keyword_res_size  = IFLY_IVW_ACTIVE_KEYWORD_RES_LEN;
    cfg.wakeup_callback   = (ivw_wakeup_callback_t)__ifly_ivw_wakeup_cb;
    cfg.user_param        = NULL;

    INT_T ret = ivw_module_init(&cfg);
    if (ret != IVW_OK) {
        TAL_PR_ERR("ifly_kws_create: ivw_module_init failed %d", ret);
        tal_free(mlp_buf);
        tal_free(kw_buf);
        return OPRT_COM_ERROR;
    }
#else /* pass const resource pointers directly; select via IFLY_IVW_USE_TUYA_RES */
    ivw_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.mlp_res_data      = (const char *)IFLY_IVW_ACTIVE_MLP_RES;
    cfg.mlp_res_size      = IFLY_IVW_ACTIVE_MLP_RES_LEN;
    cfg.keyword_res_data  = (const char *)IFLY_IVW_ACTIVE_KEYWORD_RES;
    cfg.keyword_res_size  = IFLY_IVW_ACTIVE_KEYWORD_RES_LEN;
    cfg.wakeup_callback   = (ivw_wakeup_callback_t)__ifly_ivw_wakeup_cb;
    cfg.user_param        = NULL;

    INT_T ret = ivw_module_init(&cfg);
    if (ret != IVW_OK) {
        TAL_PR_ERR("ifly_kws_create: ivw_module_init failed %d", ret);
        return OPRT_COM_ERROR;
    }
#endif

    ctx->handle    = NULL;  /* IVW is a singleton, no per-instance handle */
    ctx->priv_data = NULL;

    TAL_PR_DEBUG("ifly_kws_create ok, ivw ver=%s", ivw_module_get_version());
    return OPRT_OK;
}

/**
 * @brief Detect keyword in one batch of PCM data
 * @param[in] ctx    KWS context (unused for IVW singleton)
 * @param[in] data   PCM data pointer (16-bit mono, gain already applied)
 * @param[in] datalen Data byte length
 * @return OPRT_OK if wakeup detected (causes framework to reset ring buffer),
 *         OPRT_COM_ERROR if no wakeup in this batch,
 *         OPRT_RESOURCE_NOT_READY if IVW is not initialized
 * @note IVW fires wakeup_callback synchronously inside ivw_module_process_audio.
 *       Once s_wakeup_flag is set, remaining frames in the batch are skipped to
 *       avoid blocking the detect thread with stale data.
 */
INT_T ifly_kws_detect(WUKONG_KWS_CTX_T *ctx, UINT8_T *data, UINT32_T datalen)
{
    if (!ivw_module_is_initialized()) {
        return OPRT_RESOURCE_NOT_READY;
    }

    /* Clear wakeup flag for this batch; the callback sets it if a keyword fires */
    s_wakeup_flag = 0;

    /* Split input into 160-sample (10 ms) frames and feed IVW one by one.
     * Gain has already been applied by ifly_audio_aec_vad.c (WAKEUP_AUDIO_FIXGAIN).
     * Each ivw_module_process_audio call processes exactly one 10 ms frame.
     * Log processing time every 8 frames (80 ms) to track per-batch latency. */
    UINT32_T offset = 0;
    UINT32_T frame_idx = 0;
    UINT32_T t_start = tal_system_get_millisecond();

    while (offset + IFLY_IVW_FRAME_BYTES <= datalen) {
        /* Stop immediately if wakeup was triggered inside a previous frame */
        if (s_wakeup_flag) {
            break;
        }

        const int16_t *src = (const int16_t *)(data + offset);
        INT_T ret = ivw_module_process_audio(src, (INT_T)IFLY_IVW_FRAME_BYTES);
        if (ret != IVW_OK) {
            TAL_PR_ERR("ivw_module_process_audio failed ret=%d frame=%u", ret, frame_idx);
        }

        offset += IFLY_IVW_FRAME_BYTES;
        frame_idx++;

        /* Print elapsed time every 8 frames (80 ms of audio) */
        if ((frame_idx & 7u) == 0) {
            UINT32_T elapsed = tal_system_get_millisecond() - t_start;
            // TAL_PR_DEBUG("ifly ivw detect: frame=%u elapsed=%ums", frame_idx, elapsed);
        }
    }

    if (s_wakeup_flag) {
        UINT32_T elapsed = tal_system_get_millisecond() - t_start;
        // TAL_PR_DEBUG("ifly ivw detect: wakeup at frame=%u elapsed=%ums", frame_idx, elapsed);
        return OPRT_OK; /* signal framework to reset ring buffer */
    }

    /* No wakeup in this batch */
    return OPRT_COM_ERROR;
}

/**
 * @brief Reset IVW engine state after a wakeup event
 * @param[in] ctx KWS context (unused for IVW singleton)
 * @return OPRT_OK always
 * @note Called by the KWS framework after detect() returns OPRT_OK.
 *       IVW has no dedicated reset API; clearing s_wakeup_flag is sufficient
 *       so the next detect() batch starts fresh.
 */
INT_T ifly_kws_reset(WUKONG_KWS_CTX_T *ctx)
{
    s_wakeup_flag = 0;
    return OPRT_OK;
}

INT_T ifly_kws_deinit(WUKONG_KWS_CTX_T *ctx)
{
    if (ivw_module_is_initialized()) {
        ivw_module_deinit();
    }
    return OPRT_OK;
}
