#ifndef __TUYA_DMA2D_H__
#define __TUYA_DMA2D_H__

#include "tuya_cloud_types.h"
#include "tkl_dma2d.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file tuya_dma2d.h
 * @brief Single owner of the DMA2D hardware engine, independent of the UI.
 *
 * The chip has ONE DMA2D engine. Both the LVGL flush path (buffer copies) and
 * the camera-preview YUV422->RGB conversion need it, and they run on different
 * threads — so every op is issued and awaited under one shared mutex here, and
 * the engine is brought up exactly once via reference counting. Either consumer
 * (UI display port, camera service) may call init/deinit in any order.
 *
 * init()/deinit() are reference-counted and serialised by an internal mutex, so
 * the two consumers may bring the engine up/down from different threads at
 * runtime. The conversion / memcpy / drain entry points are thread-safe and may
 * be called concurrently from the camera and UI threads once the engine is up;
 * a caller must stop its producer (camera stream / LVGL flush) before dropping
 * its final reference.
 */

/**
 * @brief Bring up the DMA2D engine (reference counted, idempotent).
 *        First caller performs tkl_dma2d_init() + creates the op mutex/semaphore;
 *        subsequent callers just bump the refcount.
 * @return OPRT_OK on success, error code otherwise.
 */
OPERATE_RET tuya_dma2d_init(VOID);

/**
 * @brief Release one reference; the last release tears the engine down.
 * @return OPRT_OK on success, error code otherwise.
 */
OPERATE_RET tuya_dma2d_deinit(VOID);

/**
 * @brief Whether the engine is initialised (decides HW vs software fallback).
 */
BOOL_T tuya_dma2d_is_ready(VOID);

/**
 * @brief Block until any in-flight DMA2D op has finished.
 *        (Implemented as a take/release of the op mutex.)
 */
VOID tuya_dma2d_drain(VOID);

/**
 * @brief Read cumulative transfer-health counters (diagnostics).
 * @param[out] timeout_cnt  number of ops that hit the wait timeout (wedged engine)
 * @param[out] error_cnt    number of DMA2D error IRQs (CFG_ERROR / TRANS_ERROR)
 * @note Either pointer may be NULL. A non-zero count preceding a UI heap crash
 *       implicates a wedged/failing transfer desyncing buffer reuse.
 */
VOID tuya_dma2d_get_diag(uint32_t *timeout_cnt, uint32_t *error_cnt);

/**
 * @brief Hardware-accelerated YUV422 -> RGB565 conversion.
 *        Falls back to software (tal_image) when the engine is not ready or the
 *        transfer fails, so the call always produces a valid frame.
 */
OPERATE_RET tuya_dma2d_yuv422_to_rgb565(uint8_t *in_buf, uint16_t in_w, uint16_t in_h,
                                        uint8_t *out_buf, uint16_t out_w, uint16_t out_h);

/**
 * @brief Hardware-accelerated YUV422 -> RGB888 conversion (software fallback).
 */
OPERATE_RET tuya_dma2d_yuv422_to_rgb888(uint8_t *in_buf, uint16_t in_w, uint16_t in_h,
                                        uint8_t *out_buf, uint16_t out_w, uint16_t out_h);

/**
 * @brief Run one DMA2D memcpy (e.g. RGB565<->RGB565 buffer copy) and wait its
 *        completion, serialised against every other DMA2D user.
 *        Used by the LVGL flush path. Returns non-OK when the engine is not ready
 *        so the caller can fall back to a CPU copy.
 * @param[in] in_frame  source frame descriptor
 * @param[in] out_frame destination frame descriptor
 * @return OPRT_OK on success, error code otherwise.
 */
OPERATE_RET tuya_dma2d_memcpy(TKL_DMA2D_FRAME_INFO_T *in_frame, TKL_DMA2D_FRAME_INFO_T *out_frame);

#ifdef __cplusplus
}
#endif

#endif /* __TUYA_DMA2D_H__ */
