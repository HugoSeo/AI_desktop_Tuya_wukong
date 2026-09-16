#include "ty_avi_audio.h"
#include "rb.h"
#include "avi_port.h"

#include "uni_log.h"
#include "tal_thread.h"

#define TY_AVI_AUDIO_CHUNK      640
#define TY_AVI_AUDIO_SAMPLE     16000
#define TY_AVI_AUDIO_RB_SIZE    (256 * 1024)
#define TY_AVI_AUDIO_CAPTURE_RB_SIZE (16 * 1024)
#define TY_AVI_AUDIO_FRAME_TIME_MS 20
#define TY_AVI_SPK_GPIO         28

static struct ringbuffer s_avi_audio_rb;
static struct ringbuffer s_capture_audio_rb;
static volatile uint8_t s_audio_running = 0;
static volatile uint8_t s_capture_running = 0;
static uint8_t s_audio_preview = 0;
static uint8_t s_rb_inited = 0;
static uint8_t s_capture_rb_inited = 0;
static THREAD_HANDLE s_capture_thread = NULL;

/**
 * @brief Map UI volume to ADC/DAC hardware gain
 * @param[in] volume UI volume
 * @return Hardware gain value
 */
static UINT8_T ty_avi_audio_map_gain(INT32_T volume)
{
    if (volume <= 0) {
        return 0x2d;
    }
    if (volume <= 0x3f) {
        return (UINT8_T)volume;
    }
    return (UINT8_T)(volume * 0x3f / 100);
}

/**
 * @brief Control speaker PA GPIO
 * @param[in] on 1 enable, 0 disable
 * @return none
 */
static void ty_avi_audio_spk_pa_ctrl(uint8_t on)
{
    TUYA_GPIO_BASE_CFG_T cfg = {
        .direct = TUYA_GPIO_OUTPUT,
        .mode = TUYA_GPIO_PULLUP,
        .level = TUYA_GPIO_LEVEL_LOW,
    };

    sys_port.io_init(TY_AVI_SPK_GPIO, &cfg);
    sys_port.io_write(TY_AVI_SPK_GPIO,
                   on ? TUYA_GPIO_LEVEL_HIGH : TUYA_GPIO_LEVEL_LOW);
}

/**
 * @brief Write PCM to DAC with busy retry
 * @param[in] pcm PCM buffer
 * @param[in] len buffer length
 * @return OPRT_OK on success, error code on failure
 */
static OPERATE_RET ty_avi_audio_dac_write(uint8_t *pcm, uint32_t len)
{
    OPERATE_RET ret;

    do {
        ret = audio_port.spk_write(TUYA_AUDIO_DAC_PORT_0, pcm, len);
        if (ret == OPRT_OS_ADAPTER_DAC_BUSY) {
            sys_port.sleep(5);
        }
    } while (s_capture_running && ret == OPRT_OS_ADAPTER_DAC_BUSY);

    return ret;
}

/**
 * @brief ADC DMA ISR callback; copy one PCM frame into capture RB
 * @param[in] event ADC frame event
 * @param[in] pcm PCM data
 * @param[in] len data length
 * @param[in] args unused
 * @return none
 * @note Runs in DMA ISR; keep bounded to one PCM copy.
 */
static VOID_T ty_avi_audio_adc_cb(TUYA_AUDIO_FRAME_EVT_E event, uint8_t *pcm,
                                  uint32_t len, VOID_T *args)
{
    (void)args;

    if (!s_capture_running || event != TUYA_AUDIO_FRAME_EVENT_ADC_RX ||
        !pcm || len != TY_AVI_AUDIO_CHUNK) {
        return;
    }

    if (avi_rb_unused(&s_capture_audio_rb) >= len) {
        (void)avi_rb_in(&s_capture_audio_rb, pcm, len);
    }
}

/**
 * @brief Capture thread: pull ADC frames, optional preview, push to AVI RB
 * @param[in] arg unused
 * @return none
 */
static void ty_avi_audio_capture_thread(void *arg)
{
    uint8_t pcm[TY_AVI_AUDIO_CHUNK];

    (void)arg;

    while (s_capture_running) {
        if (avi_rb_avail(&s_capture_audio_rb) < TY_AVI_AUDIO_CHUNK) {
            sys_port.sleep(5);
            continue;
        }

        uint32_t n = avi_rb_out(&s_capture_audio_rb, pcm, sizeof(pcm));
        if (n != TY_AVI_AUDIO_CHUNK) {
            continue;
        }

        if (s_audio_preview) {
            (void)ty_avi_audio_dac_write(pcm, n);
        }

        avi_rb_in(&s_avi_audio_rb, pcm, n);
    }
}

/**
 * @brief Start DAC speaker for preview
 * @param[in] spk_vol speaker volume
 * @return OPRT_OK on success, error code on failure
 */
static OPERATE_RET ty_avi_audio_spk_start(INT32_T spk_vol)
{
    TKL_AUD_DAC_CFG_T spk_cfg = {
        .volume = ty_avi_audio_map_gain(spk_vol),
        .chan_num = 1,
        .sample_bits = TUYA_AUDIO_SAMPLE_BITS_16,
        .sample_rate = TY_AVI_AUDIO_SAMPLE,
        .frame_time_ms = TY_AVI_AUDIO_FRAME_TIME_MS,
        .frame_cb = NULL,
        .args = NULL,
    };

    ty_avi_audio_spk_pa_ctrl(1);
    if (audio_port.spk_init(TUYA_AUDIO_DAC_PORT_0, &spk_cfg) != OPRT_OK) {
        ty_avi_audio_spk_pa_ctrl(0);
        return OPRT_COM_ERROR;
    }
    if (audio_port.spk_start(TUYA_AUDIO_DAC_PORT_0) != OPRT_OK) {
        audio_port.spk_deinit(TUYA_AUDIO_DAC_PORT_0);
        ty_avi_audio_spk_pa_ctrl(0);
        return OPRT_COM_ERROR;
    }
    return OPRT_OK;
}

/**
 * @brief Start AVI audio recording
 * @param[in] mic_vol microphone volume
 * @param[in] enable_preview enable speaker preview
 * @param[in] spk_vol speaker volume when preview enabled
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET ty_avi_audio_rec_start(INT32_T mic_vol, uint8_t enable_preview, INT32_T spk_vol)
{
    TKL_AUD_ADC_CFG_T mic_cfg = {
        .chan = TUYA_AUDIO_ADC_CHANNEL_L,
        .vol = ty_avi_audio_map_gain(mic_vol),
        .sample_bits = TUYA_AUDIO_SAMPLE_BITS_16,
        .sample_rate = TY_AVI_AUDIO_SAMPLE,
        .frame_time_ms = TY_AVI_AUDIO_FRAME_TIME_MS,
        .upper_cb = ty_avi_audio_adc_cb,
        .args = NULL,
    };
    THREAD_CFG_T thrd = {
        .priority = THREAD_PRIO_2,
        .stackDepth = 4096,
        .thrdname = "avi_audio",
    };

    if (s_audio_running) {
        return OPRT_OK;
    }

    if (!s_rb_inited) {
        if (avi_rb_init(&s_avi_audio_rb, TY_AVI_AUDIO_RB_SIZE, 1) != 0) {
            return OPRT_MALLOC_FAILED;
        }
        s_rb_inited = 1;
    } else {
        avi_rb_clean(&s_avi_audio_rb);
    }
    if (!s_capture_rb_inited) {
        if (avi_rb_init(&s_capture_audio_rb, TY_AVI_AUDIO_CAPTURE_RB_SIZE, 1) != 0) {
            return OPRT_MALLOC_FAILED;
        }
        s_capture_rb_inited = 1;
    } else {
        avi_rb_clean(&s_capture_audio_rb);
    }

    PR_INFO("avi audio mic_vol=%d spk_vol=%d preview=%u mic_cfg.vol=%u",
            mic_vol, spk_vol, enable_preview, mic_cfg.vol);

    if (audio_port.mic_init(TUYA_AUDIO_ADC_PORT_0, &mic_cfg) != OPRT_OK) {
        PR_ERR("avi adc init failed");
        return OPRT_COM_ERROR;
    }
    if (audio_port.mic_start(TUYA_AUDIO_ADC_PORT_0) != OPRT_OK) {
        PR_ERR("avi adc start failed");
        audio_port.mic_deinit(TUYA_AUDIO_ADC_PORT_0);
        return OPRT_COM_ERROR;
    }

    s_audio_preview = enable_preview ? 1 : 0;
    if (s_audio_preview) {
        if (ty_avi_audio_spk_start(spk_vol) != OPRT_OK) {
            PR_ERR("avi speaker start failed");
            audio_port.mic_stop(TUYA_AUDIO_ADC_PORT_0);
            audio_port.mic_deinit(TUYA_AUDIO_ADC_PORT_0);
            s_audio_preview = 0;
            return OPRT_COM_ERROR;
        }
    }

    s_capture_running = 1;
    s_audio_running = 1;
    if (tal_thread_create_and_start(&s_capture_thread, NULL, NULL,
                                    ty_avi_audio_capture_thread, NULL,
                                    &thrd) != OPRT_OK) {
        PR_ERR("avi audio capture thread failed");
        s_capture_running = 0;
        s_audio_running = 0;
        if (s_audio_preview) {
            audio_port.spk_stop(TUYA_AUDIO_DAC_PORT_0);
            audio_port.spk_deinit(TUYA_AUDIO_DAC_PORT_0);
            ty_avi_audio_spk_pa_ctrl(0);
            s_audio_preview = 0;
        }
        audio_port.mic_stop(TUYA_AUDIO_ADC_PORT_0);
        audio_port.mic_deinit(TUYA_AUDIO_ADC_PORT_0);
        return OPRT_COM_ERROR;
    }

    PR_NOTICE("avi audio start preview=%u", s_audio_preview);
    return OPRT_OK;
}

/**
 * @brief Enable or disable recording preview
 * @param[in] on 1 enable, 0 disable
 * @param[in] spk_vol speaker volume when enabling
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET ty_avi_audio_rec_set_preview(uint8_t on, INT32_T spk_vol)
{
    if (!s_audio_running) {
        return OPRT_COM_ERROR;
    }

    on = on ? 1 : 0;
    if (on == s_audio_preview) {
        return OPRT_OK;
    }

    if (on) {
        if (ty_avi_audio_spk_start(spk_vol) != OPRT_OK) {
            PR_ERR("avi speaker start failed");
            return OPRT_COM_ERROR;
        }
        s_audio_preview = 1;
    } else {
        s_audio_preview = 0;
        audio_port.spk_stop(TUYA_AUDIO_DAC_PORT_0);
        audio_port.spk_deinit(TUYA_AUDIO_DAC_PORT_0);
        ty_avi_audio_spk_pa_ctrl(0);
    }

    PR_NOTICE("avi audio preview=%u", s_audio_preview);
    return OPRT_OK;
}

/**
 * @brief Stop AVI audio recording
 * @return none
 */
void ty_avi_audio_rec_stop(void)
{
    if (!s_audio_running) {
        return;
    }

    s_audio_running = 0;
    s_capture_running = 0;
    audio_port.mic_stop(TUYA_AUDIO_ADC_PORT_0);
    audio_port.mic_deinit(TUYA_AUDIO_ADC_PORT_0);
    if (s_capture_thread) {
        tal_thread_delete(s_capture_thread);
        s_capture_thread = NULL;
    }

    if (s_audio_preview) {
        audio_port.spk_stop(TUYA_AUDIO_DAC_PORT_0);
        audio_port.spk_deinit(TUYA_AUDIO_DAC_PORT_0);
        ty_avi_audio_spk_pa_ctrl(0);
        s_audio_preview = 0;
    }
    avi_rb_clean(&s_avi_audio_rb);
    avi_rb_clean(&s_capture_audio_rb);
}

/**
 * @brief Non-blocking read one audio chunk
 * @param[out] buf output buffer
 * @param[in] len expected length (must be TY_AVI_AUDIO_CHUNK)
 * @return bytes read on success, -1 on failure
 */
int ty_avi_audio_rec_try_read(uint8_t *buf, uint32_t len)
{
    uint32_t n;

    if (!s_audio_running || !buf || len == 0 || len != TY_AVI_AUDIO_CHUNK) {
        return -1;
    }

    if (avi_rb_avail(&s_avi_audio_rb) < len) {
        return -1;
    }

    n = avi_rb_out(&s_avi_audio_rb, buf, len);
    if (n != len) {
        return -1;
    }

    return (int)n;
}

/**
 * @brief Blocking read one audio chunk
 * @param[out] buf output buffer
 * @param[in] len expected length (must be TY_AVI_AUDIO_CHUNK)
 * @return bytes read on success, -1 on failure
 */
int ty_avi_audio_rec_read(uint8_t *buf, uint32_t len)
{
    if (!s_audio_running || !buf || len == 0) {
        return -1;
    }

    if (len != TY_AVI_AUDIO_CHUNK) {
        return -1;
    }

    while (s_audio_running) {
        if (ty_avi_audio_rec_try_read(buf, len) >= 0) {
            return (int)len;
        }
        sys_port.sleep(5);
    }

    return -1;
}
