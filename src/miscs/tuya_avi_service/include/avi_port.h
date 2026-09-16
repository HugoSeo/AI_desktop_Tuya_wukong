/**
 * @file avi_port.h
 * @brief Hardware/OS abstraction port for tuya_avi_service
 * @version 1.0
 * @date 2026-07-24
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __AVI_PORT_H__
#define __AVI_PORT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"
#include "tkl_aud_adc.h"
#include "tkl_aud_dac.h"
#include "tkl_gpio.h"
#include "tkl_fs.h"
#include "tkl_dma2d.h"
#include "tkl_jpeg_codec.h"
#include "tkl_system.h"
#include "tkl_memory.h"

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef struct {
    OPERATE_RET (*mic_init)(TUYA_AUDIO_ADC_PORT_E port, TKL_AUD_ADC_CFG_T *config);
    VOID_T (*mic_deinit)(TUYA_AUDIO_ADC_PORT_E port);
    OPERATE_RET (*mic_start)(TUYA_AUDIO_ADC_PORT_E port);
    OPERATE_RET (*mic_stop)(TUYA_AUDIO_ADC_PORT_E port);
    OPERATE_RET (*spk_init)(TUYA_AUDIO_DAC_PORT_E port, TKL_AUD_DAC_CFG_T *config);
    OPERATE_RET (*spk_deinit)(TUYA_AUDIO_DAC_PORT_E port);
    OPERATE_RET (*spk_start)(TUYA_AUDIO_DAC_PORT_E port);
    OPERATE_RET (*spk_stop)(TUYA_AUDIO_DAC_PORT_E port);
    OPERATE_RET (*spk_write)(TUYA_AUDIO_DAC_PORT_E port, UINT8_T *buffer, UINT32_T len);
    UINT32_T (*spk_get_frame_size)(TUYA_AUDIO_DAC_PORT_E port);
} AVI_AUDIO_PORT_T;

typedef struct {
    INT_T (*fclose)(TUYA_FILE file);
    TUYA_FILE (*fopen)(CONST CHAR_T *path, CONST CHAR_T *mode);
    INT_T (*fread)(VOID_T *buf, INT_T bytes, TUYA_FILE file);
    INT_T (*fseek)(TUYA_FILE file, INT64_T offs, INT_T whence);
    INT64_T (*ftell)(TUYA_FILE file);
    INT_T (*fwrite)(VOID_T *buf, INT_T bytes, TUYA_FILE file);
} AVI_FILE_OPT_PORT_T;

typedef struct {
    OPERATE_RET (*dma2d_init)(CONST TUYA_DMA2D_BASE_CFG_T *cfg);
    OPERATE_RET (*dma2d_deinit)(VOID_T);
    OPERATE_RET (*dma2d_convert)(TKL_DMA2D_FRAME_INFO_T *src, TKL_DMA2D_FRAME_INFO_T *dst);
    OPERATE_RET (*jpeg_init)(VOID_T);
    OPERATE_RET (*jpeg_deinit)(VOID_T);
    OPERATE_RET (*jpeg_img_info_get)(UINT8_T *jpeg_buf, UINT32_T jpeg_size, TKL_JPEG_CODEC_INFO_T *jpeg_info);
    OPERATE_RET (*jpeg_convert)(UINT8_T *src_buf, UINT8_T *dst_buf, TKL_JPEG_CODEC_INFO_T *jpeg_codec_info, JPEG_DEC_OUT_FMT out_fmt);
} AVI_MEDIA_OPT_PORT_T;

typedef struct {
    OPERATE_RET (*io_init)(TUYA_GPIO_NUM_E pin_id, CONST TUYA_GPIO_BASE_CFG_T *cfg);
    OPERATE_RET (*io_write)(TUYA_GPIO_NUM_E pin_id, TUYA_GPIO_LEVEL_E level);
    SYS_TICK_T (*get_tick)(VOID_T);
    VOID_T *(*psram_malloc)(SIZE_T size);
    VOID_T (*psram_free)(VOID_T *ptr);
    VOID_T (*sleep)(UINT_T num_ms);
} AVI_SYS_PORT_T;

/* ---------------------------------------------------------------------------
 * Function declarations / globals
 * --------------------------------------------------------------------------- */
extern AVI_AUDIO_PORT_T audio_port;
extern AVI_FILE_OPT_PORT_T file_opt_port;
extern AVI_MEDIA_OPT_PORT_T media_opt_port;
extern AVI_SYS_PORT_T sys_port;

#ifdef __cplusplus
}
#endif
#endif /* __AVI_PORT_H__ */
