/**
 * @file tal_lcd_service.h
 * @brief LCD conversion service: YUV422/MJPEG → RGB565, plus pbuf pool management.
 *
 * This module is independent of tal_camera.  It operates on raw data pointers,
 * so it can be used by any producer (camera, video file, etc.).
 *
 * Conversion paths:
 *  - YUV422 → RGB565 : hardware DMA2D (synchronous inside the call)
 *  - MJPEG  → RGB565 : hardware JPEG decoder (bk_jpeg_hw_decode_to_mem)
 *
 * The output buffer is acquired from an internal tuya_lcd_pbuf pool.
 * The caller must call tal_lcd_service_release_pbuf() when the buffer is
 * no longer needed (e.g. from the LCD flush-complete callback).
 *
 * DMA2D lifecycle note:
 *   DMA2D is a shared hardware resource.  It must NOT be initialized at
 *   module init time because other subsystems (e.g. LVGL/UI) may own it.
 *   The caller is responsible for calling tal_lcd_service_dma2d_init()
 *   immediately before starting the camera stream (after the UI is paused)
 *   and tal_lcd_service_dma2d_deinit() when stopping the stream.
 *
 * @author linch
 * @date 2025-02-09
 * @version 1.0.0
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All rights reserved.
 */

#ifndef __TAL_LCD_SERVICE_H__
#define __TAL_LCD_SERVICE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"
#include "tuya_lcd_pbuf.h"   /* tuya_lcd_pbuf_node_t */

/**
 * @brief  DMA2D conversion mode.
 *
 * DMA2D is a shared hardware resource used by two mutually exclusive paths:
 *  - YUV  : tkl_dma2d         for YUV422 → RGB565  (DVP camera)
 *  - JPEG : bk_jpeg_hw_decode for MJPEG  → RGB565  (UVC camera)
 *
 * Only one mode can be active at a time.  Pass the appropriate mode to
 * tal_lcd_service_dma2d_init() based on the active camera type.
 */
typedef enum {
    TAL_LCD_DMA2D_MODE_YUV  = 0, /**< DVP path : tkl_dma2d owns DMA2D */
    TAL_LCD_DMA2D_MODE_JPEG = 1, /**< UVC path : bk_jpeg_hw owns DMA2D */
} TAL_LCD_DMA2D_MODE_E;


/* ------------------------------------------------------------------ */
/*  Lifecycle                                                           */
/* ------------------------------------------------------------------ */

/**
 * @brief  Initialize the RGB565 buffer pool.
 *         Does NOT touch DMA2D — call tal_lcd_service_dma2d_init() separately
 *         after the UI layer has released the DMA2D hardware.
 *
 * @param  src_w      Camera output width  in pixels.
 *                    For MJPEG must be a multiple of 32.
 * @param  src_h      Camera output height in pixels.
 *                    For MJPEG must be a multiple of 8.
 * @param  lcd_w      LCD display width  in pixels
 * @param  lcd_h      LCD display height in pixels
 * @param  pool_size  Number of RGB565 buffers to pre-allocate (recommended 3).
 *                    Each buffer is lcd_w × lcd_h × 2 bytes.
 * @param  mode       TAL_LCD_DMA2D_MODE_YUV  : DVP/YUV422 path
 *                    TAL_LCD_DMA2D_MODE_JPEG : UVC/MJPEG path
 *                    Recorded here so tal_lcd_service_dma2d_init() needs no argument.
 * @param  byte_swap  TRUE : swap bytes in every RGB565 pixel (SPI screens).
 * @return OPRT_OK on success
 */
OPERATE_RET tal_lcd_service_init(UINT16_T src_w, UINT16_T src_h,
                                 UINT16_T lcd_w, UINT16_T lcd_h,
                                 UINT8_T  pool_size,
                                 TAL_LCD_DMA2D_MODE_E mode,
                                 BOOL_T   byte_swap);

/**
 * @brief  Release the JPEG decoder and buffer pool.
 *         Does NOT touch DMA2D — call tal_lcd_service_dma2d_deinit() separately.
 */
OPERATE_RET tal_lcd_service_deinit(VOID);


/**
 * @brief  Claim and initialize DMA2D in the mode recorded at init time.
 *
 * Must be called after the UI layer (e.g. LVGL) has stopped using DMA2D,
 * typically from tuya_ai_toy_camera_start() after tuya_ai_display_pause().
 * For JPEG mode also allocates the internal decode buffer (src_w × src_h).
 *
 * @return OPRT_OK on success
 */
OPERATE_RET tal_lcd_service_dma2d_init(VOID);

/**
 * @brief  Deinitialize and release DMA2D back to the system.
 *         Call from tuya_ai_toy_camera_stop() so the UI can reclaim DMA2D.
 */
OPERATE_RET tal_lcd_service_dma2d_deinit(VOID);

/* ------------------------------------------------------------------ */
/*  Conversion                                                          */
/* ------------------------------------------------------------------ */

/**
 * @brief  Convert a YUV422 frame to RGB565 via DMA2D.
 *
 * Acquires a buffer from the pool, runs DMA2D centre-crop (src → lcd size),
 * applies byte_swap if configured, and returns the filled node.
 * src and lcd dimensions are taken from tal_lcd_service_init().
 *
 * @param  src_data  Pointer to YUV422 data (must be in PSRAM/SRAM, not Flash)
 * @param  out       [out] Pointer to acquired pbuf node on success
 * @return OPRT_OK on success; OPRT_COM_ERROR if DMA2D fails or pool empty
 */
OPERATE_RET tal_lcd_service_yuv2rgb(const UINT8_T *src_data,
                                    tuya_lcd_pbuf_node_t **out);

/**
 * @brief  Decode a MJPEG frame to RGB565 via hardware JPEG decoder.
 *
 * Decodes the JPEG into an internal pre-allocated buffer (src_w × src_h),
 * centre-crops the lcd_w × lcd_h region into the pbuf node (contiguous copy),
 * and applies byte_swap if configured.  The pbuf node's frame buffer is
 * always lcd_w × lcd_h × 2 bytes; no pointer adjustment is made.
 * All dimensions and flags are taken from tal_lcd_service_init().
 *
 * @param  src_data  Pointer to JPEG data (must be in PSRAM/SRAM, not Flash)
 * @param  src_len   Byte length of JPEG data
 * @param  out       [out] Pointer to acquired pbuf node on success
 * @return OPRT_OK on success; OPRT_COM_ERROR if decode fails or pool empty
 */
OPERATE_RET tal_lcd_service_mjpeg2rgb(const UINT8_T *src_data,
                                      UINT32_T src_len,
                                      tuya_lcd_pbuf_node_t **out);

/* ------------------------------------------------------------------ */
/*  Buffer management                                                   */
/* ------------------------------------------------------------------ */

/**
 * @brief  Return a pbuf node to the pool.
 *         Typically called from the LCD flush-complete callback.
 */
OPERATE_RET tal_lcd_service_release_pbuf(tuya_lcd_pbuf_node_t *node);

#ifdef __cplusplus
}
#endif

#endif /* __TAL_LCD_SERVICE_H__ */
