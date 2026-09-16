/**
 * @file tal_image_jpeg_codec.h
 * @brief Abstract JPEG decode interface for tjpgd and libjpeg-turbo.
 *
 * This header defines a unified JPEG decode API that can be implemented by
 * either the Tiny JPEG Decompressor (tjpgd) or libjpeg-turbo. The backend
 * is selected at compile time via Kconfig or macros.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef __TAL_IMAGE_JPEG_CODEC_H__
#define __TAL_IMAGE_JPEG_CODEC_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
************************macro define************************
***********************************************************/
/** Minimum size of JPEG input buffer for header parsing (SOI + segment) */
#define TAL_IMAGE_JPEG_MIN_READ_SIZE  512

/***********************************************************
***********************typedef define***********************
***********************************************************/
/** JPEG image info (from header) */
typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t  n_components;       /**< 1 = grayscale, 3 = YCbCr color */
} TAL_IMAGE_JPEG_INFO_T;

/** Decode output buffer (format implied by which decode API is used) */
typedef struct {
    uint8_t  *out_buf;       /**< Output buffer (caller-allocated) */
    uint32_t  out_buf_size;  /**< Real byte size of out_buf. Decoders bound their
                                  writes against it and fail with
                                  OPRT_BUFFER_NOT_ENOUGH rather than overrun, so
                                  it must be the true allocation size. */
    uint16_t  out_width;     /**< Width to decode: the native width, or a reduced
                                  size obtained from tal_image_jpeg_calc_scaled_size() */
    uint16_t  out_height;    /**< Height to decode, see out_width */
} TAL_IMAGE_JPEG_OUTPUT_T;

/***********************************************************
********************function declaration********************
***********************************************************/

/**
 * @brief Get JPEG image dimensions and component count from header.
 *
 * Does not decode the full image. Safe to call with only the beginning of
 * the stream if at least TAL_IMAGE_JPEG_MIN_READ_SIZE bytes (or full buffer)
 * are available. Both tjpgd and libjpeg-turbo backends support this.
 *
 * @param jpeg_data  Pointer to JPEG data (SOI and headers must be present).
 * @param jpeg_size  Length of jpeg_data in bytes.
 * @param info       Output: width, height, n_components (1 or 3). Must not be NULL.
 * @return OPERATE_RET OPRT_OK on success; otherwise see tuya_error_code.h.
 */
OPERATE_RET tal_image_jpeg_get_info(const uint8_t *jpeg_data,
                                    uint32_t jpeg_size,
                                    TAL_IMAGE_JPEG_INFO_T *info);

/**
 * @brief Resolve the smallest decode size the backend can produce that still
 *        covers a target size.
 *
 * A JPEG can be decoded at a reduced size straight from the DCT coefficients,
 * which costs a fraction of the memory of decoding at native resolution and
 * downscaling afterwards. Only a fixed set of ratios is available, so this
 * returns the smallest one that is still >= the target on both axes, leaving
 * the caller's remaining resample step a pure downscale.
 *
 * The result is never larger than the native size and never smaller than the
 * target. Backends without reduced-size decoding always report the native size.
 *
 * @param[in]  src_w   Native JPEG width, must be non-zero.
 * @param[in]  src_h   Native JPEG height, must be non-zero.
 * @param[in]  want_w  Target width; 0 or > src_w is treated as src_w.
 * @param[in]  want_h  Target height; 0 or > src_h is treated as src_h.
 * @param[out] out_w   Width to pass to a decode API. Must not be NULL.
 * @param[out] out_h   Height to pass to a decode API. Must not be NULL.
 * @return OPERATE_RET OPRT_OK on success; otherwise see tuya_error_code.h.
 */
OPERATE_RET tal_image_jpeg_calc_scaled_size(uint16_t src_w, uint16_t src_h,
                                            uint16_t want_w, uint16_t want_h,
                                            uint16_t *out_w, uint16_t *out_h);

/**
 * @brief Decode JPEG to RGB565 buffer (16-bit, 2 bytes per pixel).
 *
 * @param jpeg_data  Pointer to full JPEG data.
 * @param jpeg_size  Length of jpeg_data in bytes.
 * @param out        Output: out_buf, out_buf_size, out_width, out_height. Must not be NULL.
 * @return OPERATE_RET OPRT_OK on success; otherwise see tuya_error_code.h.
 */
OPERATE_RET tal_image_jpeg_decode_rgb565(const uint8_t *jpeg_data,
                                         uint32_t jpeg_size,
                                         TAL_IMAGE_JPEG_OUTPUT_T *out);

/**
 * @brief Decode JPEG to RGB888 buffer (24-bit, 3 bytes per pixel, R-G-B order).
 *
 * @param jpeg_data  Pointer to full JPEG data.
 * @param jpeg_size  Length of jpeg_data in bytes.
 * @param out        Output: out_buf, out_buf_size, out_width, out_height. Must not be NULL.
 * @return OPERATE_RET OPRT_OK on success; otherwise see tuya_error_code.h.
 */
OPERATE_RET tal_image_jpeg_decode_rgb888(const uint8_t *jpeg_data,
                                         uint32_t jpeg_size,
                                         TAL_IMAGE_JPEG_OUTPUT_T *out);

/**
 * @brief Decode JPEG to grayscale buffer (8-bit, 1 byte per pixel).
 *
 * @param jpeg_data  Pointer to full JPEG data.
 * @param jpeg_size  Length of jpeg_data in bytes.
 * @param out        Output: out_buf, out_buf_size, out_width, out_height. Must not be NULL.
 * @return OPERATE_RET OPRT_OK on success; otherwise see tuya_error_code.h.
 */
OPERATE_RET tal_image_jpeg_decode_gray(const uint8_t *jpeg_data,
                                       uint32_t jpeg_size,
                                       TAL_IMAGE_JPEG_OUTPUT_T *out);

/**
 * @brief Decode JPEG to 1-bit monochrome bitmap with Floyd-Steinberg dithering.
 *
 * Output is packed 1-bit-per-pixel, MSB first (bit 7 = leftmost pixel).
 * Black pixels (dark) are set to 1, white pixels (light) are set to 0.
 * Each row is ceil(width / 8) bytes. The caller must allocate
 * out_buf with at least ceil(out_width / 8) * out_height bytes.
 *
 * @param[in]  jpeg_data  Pointer to full JPEG data.
 * @param[in]  jpeg_size  Length of jpeg_data in bytes.
 * @param[out] out        Output: out_buf, out_buf_size, out_width, out_height. Must not be NULL.
 * @param[in]  threshold  Binarization threshold (0-255). Typical value is 128.
 * @return OPERATE_RET OPRT_OK on success; otherwise see tuya_error_code.h.
 */
OPERATE_RET tal_image_jpeg_decode_bitmap(const uint8_t *jpeg_data,
                                          uint32_t jpeg_size,
                                          TAL_IMAGE_JPEG_OUTPUT_T *out,
                                          uint8_t threshold);

#ifdef __cplusplus
}
#endif

#endif /* __TAL_IMAGE_JPEG_CODEC_H__ */
