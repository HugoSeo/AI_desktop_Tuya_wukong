/**
 * @file avi_port.c
 * @brief Hardware/OS abstraction port bindings for tuya_avi_service
 * @version 1.0
 * @date 2026-07-24
 * @copyright Copyright (c) Tuya Inc.
 */
#include "avi_port.h"

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
#if defined(TY_AVI_ENABLE_AUDIO) && TY_AVI_ENABLE_AUDIO
AVI_AUDIO_PORT_T audio_port = {
    .mic_init   = tkl_aud_adc_init,
    .mic_deinit = tkl_aud_adc_deinit,
    .mic_start  = tkl_aud_adc_start,
    .mic_stop   = tkl_aud_adc_stop,
    .spk_deinit = tkl_aud_dac_deinit,
    .spk_init   = tkl_aud_dac_init,
    .spk_start  = tkl_aud_dac_start,
    .spk_stop   = tkl_aud_dac_stop,
    .spk_write  = tkl_aud_dac_write,
    .spk_get_frame_size = tkl_aud_dac_get_frame_size,
};
#else
AVI_AUDIO_PORT_T audio_port = {0};
#endif

/* libavi still needs stream primitives for an already-resolved file path.
 * Mounting, directory enumeration and path ownership belong to Demo ui_svc_fs. */
AVI_FILE_OPT_PORT_T file_opt_port = {
    .fclose     = tkl_fclose,
    .fopen      = tkl_fopen,
    .fread      = tkl_fread,
    .fseek      = tkl_fseek,
    .ftell      = tkl_ftell,
    .fwrite     = tkl_fwrite,
};

#if defined(TY_AVI_ENABLE_PLAYBACK) && TY_AVI_ENABLE_PLAYBACK
AVI_MEDIA_OPT_PORT_T media_opt_port = {
    .dma2d_init     = tkl_dma2d_init,
    .dma2d_deinit   = tkl_dma2d_deinit,
    .dma2d_convert  = tkl_dma2d_convert,
    .jpeg_init      = tkl_jpeg_codec_init,
    .jpeg_deinit    = tkl_jpeg_codec_deinit,
    .jpeg_convert   = tkl_jpeg_codec_convert,
    .jpeg_img_info_get = tkl_jpeg_codec_img_info_get,
};
#else
AVI_MEDIA_OPT_PORT_T media_opt_port = {0};
#endif

AVI_SYS_PORT_T sys_port = {
    .io_init    = tkl_gpio_init,
    .io_write   = tkl_gpio_write,
    .get_tick   = tkl_system_get_tick_count,
    .sleep      = tkl_system_sleep,
    .psram_free = tkl_system_psram_free,
    .psram_malloc = tkl_system_psram_malloc,
};
