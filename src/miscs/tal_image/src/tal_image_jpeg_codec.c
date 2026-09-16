/**
 * @file tal_image_jpeg_codec.c
 * @brief JPEG decode implementation: libjpeg-turbo or tjpgd backend.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "tal_image_jpeg_codec.h"
#include "tuya_error_code.h"
#include "tal_memory.h"
#include <stdbool.h>
#include <string.h>

#if defined(ENABLE_LIBJPEGTURBO)
#include <setjmp.h>
#include <stdio.h>      /* jpeglib.h uses FILE/size_t but does not include this */
#include "jpeglib.h"
#include "turbojpeg.h"
#else
#include "tjpgd/tjpgd.h"
#endif

/***********************************************************
************************macro define************************
***********************************************************/
#if !defined(ENABLE_LIBJPEGTURBO)
#define TJPGD_WORKBUF_SIZE  (10*1024)
#endif

/** Denominator of libjpeg's reduced-size decode ratios (numerator 1..8). */
#define JPEG_SCALE_DENOM    8u

/***********************************************************
***********************typedef define***********************
***********************************************************/
#if !defined(ENABLE_LIBJPEGTURBO)
/** Memory stream for tjpgd input */
typedef struct {
    const uint8_t *data;
    uint32_t       size;
    uint32_t       pos;
} jpeg_mem_stream_t;
#endif

/***********************************************************
*********************static declarations*******************
***********************************************************/
#if !defined(ENABLE_LIBJPEGTURBO)
static size_t tjpgd_input_func(JDEC *jd, uint8_t *buf, size_t ndata);
static int    tjpgd_outfunc_rgb888(JDEC *jd, void *bitmap, JRECT *rect);
static int    tjpgd_outfunc_rgb565(JDEC *jd, void *bitmap, JRECT *rect);
static int    tjpgd_outfunc_gray(JDEC *jd, void *bitmap, JRECT *rect);
#endif

#if defined(ENABLE_LIBJPEGTURBO)
/** libjpeg error manager that unwinds instead of calling exit(). */
typedef struct {
    struct jpeg_error_mgr pub;
    jmp_buf               jump;
} __jpeg_err_mgr_t;
#endif

/** Output context for tjpgd outfunc */
typedef struct {
    uint8_t  *out_buf;
    uint16_t  out_width;
    uint16_t  out_height;
    uint8_t   format; /* 0: rgb888, 1: rgb565, 2: gray */
} tjpgd_out_ctx_t;

#if !defined(ENABLE_LIBJPEGTURBO)
static tjpgd_out_ctx_t s_out_ctx;
#endif

/***********************************************************
*********************static functions***********************
***********************************************************/

#if defined(ENABLE_LIBJPEGTURBO)
/** libjpeg calls this on a fatal error; its default aborts the process. */
static void __jpeg_error_exit(j_common_ptr cinfo)
{
    __jpeg_err_mgr_t *err = (__jpeg_err_mgr_t *)cinfo->err;
    longjmp(err->jump, 1);
}

/** Swallow libjpeg's diagnostics; the return code is what callers act on. */
static void __jpeg_output_message(j_common_ptr cinfo)
{
    (void)cinfo;
}
#endif

#if !defined(ENABLE_LIBJPEGTURBO)
static size_t tjpgd_input_func(JDEC *jd, uint8_t *buf, size_t ndata)
{
    jpeg_mem_stream_t *stream;
    uint32_t left;
    size_t   n;

    if (jd == NULL || jd->device == NULL) {
        return 0;
    }
    stream = (jpeg_mem_stream_t *)jd->device;

    if (stream->pos >= stream->size) {
        return 0;
    }
    left = stream->size - stream->pos;
    n    = (ndata <= (size_t)left) ? ndata : (size_t)left;

    if (buf != NULL && n > 0) {
        memcpy(buf, stream->data + stream->pos, n);
    }
    stream->pos += (uint32_t)n;
    return n;
}

static int tjpgd_outfunc_rgb888(JDEC *jd, void *bitmap, JRECT *rect)
{
    const uint8_t *src;
    uint8_t       *dst;
    uint16_t       y, w, stride;

    (void)jd;
    if (bitmap == NULL || rect == NULL || s_out_ctx.out_buf == NULL) {
        return 0;
    }
    w      = (uint16_t)(rect->right - rect->left + 1);
    stride = s_out_ctx.out_width * 3u;
    src    = (const uint8_t *)bitmap;

    for (y = rect->top; y <= rect->bottom; y++) {
        dst = s_out_ctx.out_buf + (uint32_t)y * stride + (uint32_t)rect->left * 3u;
        memcpy(dst, src, (size_t)w * 3u);
        src += (size_t)w * 3u;
    }
    return 1;
}

static int tjpgd_outfunc_rgb565(JDEC *jd, void *bitmap, JRECT *rect)
{
    const uint8_t *src;
    uint16_t      *dst;
    uint16_t       y, x, w, stride;
    uint16_t       r, g, b, rgb565;

    (void)jd;
    if (bitmap == NULL || rect == NULL || s_out_ctx.out_buf == NULL) {
        return 0;
    }
    w      = (uint16_t)(rect->right - rect->left + 1);
    stride = s_out_ctx.out_width;
    src    = (const uint8_t *)bitmap;

    /* tjpgd outputs pixels in B-G-R order */
    for (y = rect->top; y <= rect->bottom; y++) {
        dst = (uint16_t *)(s_out_ctx.out_buf + (uint32_t)y * stride * 2u) + rect->left;
        for (x = 0; x < w; x++) {
            b      = (uint16_t)src[0];
            g      = (uint16_t)src[1];
            r      = (uint16_t)src[2];
            src   += 3;
            rgb565 = (uint16_t)((r & 0xF8u) << 8 | (g & 0xFCu) << 3 | (b >> 3));
            *dst++ = rgb565;
        }
    }
    return 1;
}

static int tjpgd_outfunc_gray(JDEC *jd, void *bitmap, JRECT *rect)
{
    const uint8_t *src;
    uint8_t       *dst;
    uint16_t       y, x, w, stride;
    uint16_t       gray;

    (void)jd;
    if (bitmap == NULL || rect == NULL || s_out_ctx.out_buf == NULL) {
        return 0;
    }
    w      = (uint16_t)(rect->right - rect->left + 1);
    stride = s_out_ctx.out_width;
    src    = (const uint8_t *)bitmap;

    /* tjpgd outputs pixels in B-G-R order */
    for (y = rect->top; y <= rect->bottom; y++) {
        dst = s_out_ctx.out_buf + (uint32_t)y * stride + rect->left;
        for (x = 0; x < w; x++) {
            /* Y = 0.299*R + 0.587*G + 0.114*B; src order is B, G, R */
            gray = (uint16_t)((117u * (uint16_t)src[0] + 601u * (uint16_t)src[1] + 306u * (uint16_t)src[2]) >> 10);
            if (gray > 255u) {
                gray = 255u;
            }
            *dst++ = (uint8_t)gray;
            src   += 3;
        }
    }
    return 1;
}
#endif

/***********************************************************
*********************public functions***********************
***********************************************************/

OPERATE_RET tal_image_jpeg_get_info(const uint8_t *jpeg_data,
                                    uint32_t       jpeg_size,
                                    TAL_IMAGE_JPEG_INFO_T *info)
{
    if (jpeg_data == NULL || info == NULL) {
        return OPRT_INVALID_PARM;
    }

    // if (jpeg_size < TAL_IMAGE_JPEG_MIN_READ_SIZE) {
    //     // Just a warning, don't return error. We might have a very small JPEG or just enough header.
    //     // return OPRT_INVALID_PARM;
    // }
    
    if (jpeg_data[0] != 0xFFu || jpeg_data[1] != 0xD8u) {
        return OPRT_INVALID_PARM;
    }

#if defined(ENABLE_LIBJPEGTURBO)
    {
        tjhandle    handle;
        int         w, h, subsamp;
        int         ret;

        handle = tjInitDecompress();
        if (handle == NULL) {
            return OPRT_COM_ERROR;
        }
        ret = tjDecompressHeader2(handle, (unsigned char *)jpeg_data, (unsigned long)jpeg_size,
                                 &w, &h, &subsamp);
        tjDestroy(handle);
        if (ret != 0) {
            return OPRT_COM_ERROR;
        }
        if (w <= 0 || h <= 0 || w > 65535 || h > 65535) {
            return OPRT_INVALID_PARM;
        }
        info->width        = (uint16_t)w;
        info->height       = (uint16_t)h;
        info->n_components = (subsamp == TJSAMP_GRAY) ? (uint8_t)1 : (uint8_t)3;
    }
#else
    {
        JDEC            jd;
        uint8_t        *workbuf;
        jpeg_mem_stream_t stream;
        JRESULT         rc;

        workbuf = (uint8_t *)Malloc(TJPGD_WORKBUF_SIZE);
        if (workbuf == NULL) {
            return OPRT_MALLOC_FAILED;
        }

        memset(&stream, 0, sizeof(stream));
        stream.data = jpeg_data;
        stream.size = jpeg_size;
        stream.pos  = 0;

        rc = jd_prepare(&jd, tjpgd_input_func, workbuf, (size_t)TJPGD_WORKBUF_SIZE, &stream);
        if (rc != JDR_OK) {
            Free(workbuf);
            return OPRT_COM_ERROR;
        }
        info->width        = jd.width;
        info->height       = jd.height;
        info->n_components = jd.ncomp;
        Free(workbuf);
    }
#endif
    return OPRT_OK;
}

OPERATE_RET tal_image_jpeg_calc_scaled_size(uint16_t src_w, uint16_t src_h,
                                            uint16_t want_w, uint16_t want_h,
                                            uint16_t *out_w, uint16_t *out_h)
{
    if (src_w == 0u || src_h == 0u || out_w == NULL || out_h == NULL) {
        return OPRT_INVALID_PARM;
    }

    if (want_w == 0u || want_w > src_w) {
        want_w = src_w;
    }
    if (want_h == 0u || want_h > src_h) {
        want_h = src_h;
    }

    *out_w = src_w;
    *out_h = src_h;

#if defined(ENABLE_LIBJPEGTURBO)
    {
        uint32_t m;

        /* libjpeg decodes at m/8 of the source, rounding each axis up; m == 8 is
         * native. Walk upwards so the first fit is the smallest one that still
         * covers the target on both axes. */
        for (m = 1u; m < JPEG_SCALE_DENOM; m++) {
            uint32_t sw = ((uint32_t)src_w * m + (JPEG_SCALE_DENOM - 1u)) / JPEG_SCALE_DENOM;
            uint32_t sh = ((uint32_t)src_h * m + (JPEG_SCALE_DENOM - 1u)) / JPEG_SCALE_DENOM;

            if (sw >= (uint32_t)want_w && sh >= (uint32_t)want_h) {
                *out_w = (uint16_t)sw;
                *out_h = (uint16_t)sh;
                break;
            }
        }
    }
#endif
    /* tjpgd is built with JD_USE_SCALE 0, so it only decodes at native size. */
    return OPRT_OK;
}

#if defined(ENABLE_LIBJPEGTURBO)
/**
 * @brief Decode straight to RGB565 using libjpeg's own RGB565 output.
 *
 * The TurboJPEG wrapper cannot emit RGB565, so going through it forces a
 * full-size RGB888 intermediate. The underlying library can emit RGB565
 * directly, which halves the working set and removes that second buffer, so
 * this drives the core API instead and writes each scanline into its final
 * place in @a out.
 *
 * Fails with OPRT_NOT_SUPPORTED-like status (any non-OK) when the stream is
 * one the RGB565 converter refuses, e.g. lossless JPEG; the caller then falls
 * back to __turbo_pack_rgb565().
 */
static OPERATE_RET __libjpeg_scan_rgb565(const uint8_t *jpeg_data,
                                         uint32_t       jpeg_size,
                                         TAL_IMAGE_JPEG_OUTPUT_T *out)
{
    struct jpeg_decompress_struct cinfo;
    __jpeg_err_mgr_t jerr;
    uint32_t         m;
    uint32_t         row_bytes;
    bool             started = false;

    memset(&cinfo, 0, sizeof(cinfo));
    cinfo.err                = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit      = __jpeg_error_exit;
    jerr.pub.output_message  = __jpeg_output_message;

    if (setjmp(jerr.jump) != 0) {
        if (started) {
            jpeg_abort_decompress(&cinfo);
        }
        jpeg_destroy_decompress(&cinfo);
        return OPRT_COM_ERROR;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, jpeg_data, (unsigned long)jpeg_size);
    (void)jpeg_read_header(&cinfo, TRUE);

    if (cinfo.image_width == 0 || cinfo.image_height == 0) {
        jpeg_destroy_decompress(&cinfo);
        return OPRT_INVALID_PARM;
    }

    /* Resolve the ratio that yields exactly the requested size, then let
     * libjpeg confirm it rather than trusting the arithmetic to agree. */
    for (m = 1u; m <= JPEG_SCALE_DENOM; m++) {
        uint32_t sw = ((uint32_t)cinfo.image_width  * m + (JPEG_SCALE_DENOM - 1u)) / JPEG_SCALE_DENOM;
        uint32_t sh = ((uint32_t)cinfo.image_height * m + (JPEG_SCALE_DENOM - 1u)) / JPEG_SCALE_DENOM;
        if (sw == (uint32_t)out->out_width && sh == (uint32_t)out->out_height) {
            break;
        }
    }
    if (m > JPEG_SCALE_DENOM) {
        jpeg_destroy_decompress(&cinfo);
        return OPRT_INVALID_PARM;
    }

    cinfo.scale_num      = m;
    cinfo.scale_denom    = JPEG_SCALE_DENOM;
    cinfo.out_color_space = JCS_RGB565;
    /* Default is JDITHER_FS; plain truncation is faster and matches what the
     * RGB888 fallback produces, so the two routes stay visually identical. */
    cinfo.dither_mode    = JDITHER_NONE;
    jpeg_calc_output_dimensions(&cinfo);

    if (cinfo.output_width != out->out_width || cinfo.output_height != out->out_height) {
        jpeg_destroy_decompress(&cinfo);
        return OPRT_INVALID_PARM;
    }

    /* RGB565 writes 2 bytes per pixel even though libjpeg reports
     * output_components as 3, so the stride must be computed, not queried. */
    row_bytes = (uint32_t)cinfo.output_width * 2u;
    if (out->out_buf_size < row_bytes * cinfo.output_height) {
        jpeg_destroy_decompress(&cinfo);
        return OPRT_BUFFER_NOT_ENOUGH;
    }

    (void)jpeg_start_decompress(&cinfo);
    started = true;

    while (cinfo.output_scanline < cinfo.output_height) {
        JSAMPROW row = (JSAMPROW)(out->out_buf + (uint32_t)cinfo.output_scanline * row_bytes);
        if (jpeg_read_scanlines(&cinfo, &row, 1) != 1) {
            jpeg_abort_decompress(&cinfo);
            jpeg_destroy_decompress(&cinfo);
            return OPRT_COM_ERROR;
        }
    }

    (void)jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return OPRT_OK;
}

/**
 * @brief RGB565 by way of a full-size RGB888 decode, then packing down.
 *
 * Only used when __libjpeg_scan_rgb565() cannot handle the stream. Costs a
 * whole extra width*height*3 buffer, so it is the fallback rather than the
 * primary route.
 */
static OPERATE_RET __turbo_pack_rgb565(const uint8_t *jpeg_data,
                                       uint32_t       jpeg_size,
                                       TAL_IMAGE_JPEG_OUTPUT_T *out)
{
    tjhandle  handle;
    int       w, h, subsamp;
    int       ret;
    uint16_t  dw, dh;
    uint32_t  i, pixels;
    uint8_t  *rgb_buf;
    const uint8_t *s;
    uint16_t *d;

    handle = tjInitDecompress();
    if (handle == NULL) {
        return OPRT_COM_ERROR;
    }
    ret = tjDecompressHeader2(handle, (unsigned char *)jpeg_data, (unsigned long)jpeg_size,
                             &w, &h, &subsamp);
    if (ret != 0) {
        tjDestroy(handle);
        return OPRT_COM_ERROR;
    }
    if (w <= 0 || h <= 0 ||
        tal_image_jpeg_calc_scaled_size((uint16_t)w, (uint16_t)h,
                                        out->out_width, out->out_height,
                                        &dw, &dh) != OPRT_OK ||
        dw != out->out_width || dh != out->out_height) {
        tjDestroy(handle);
        return OPRT_INVALID_PARM;
    }
    pixels = (uint32_t)dw * (uint32_t)dh;
    if (out->out_buf_size < pixels * 2u) {
        tjDestroy(handle);
        return OPRT_BUFFER_NOT_ENOUGH;
    }

    if (subsamp == TJSAMP_GRAY) {
        ret = tjDecompress2(handle, jpeg_data, (unsigned long)jpeg_size,
                           out->out_buf, dw, 0, dh, TJPF_GRAY, 0);
        tjDestroy(handle);
        if (ret != 0) {
            return OPRT_COM_ERROR;
        }
        /* In-place convert gray (1 byte/pixel) to RGB565 (2 bytes/pixel) from end to avoid overwrite */
        s = out->out_buf + pixels - 1;
        d = (uint16_t *)out->out_buf + (pixels - 1);
        for (i = pixels; i > 0; i--) {
            uint16_t g = (uint16_t)*s--;
            *d-- = (uint16_t)((g & 0xF8u) << 8 | (g & 0xFCu) << 3 | (g >> 3));
        }
        return OPRT_OK;
    }

    rgb_buf = (uint8_t *)Malloc(pixels * 3u);
    if (rgb_buf == NULL) {
        tjDestroy(handle);
        return OPRT_MALLOC_FAILED;
    }
    ret = tjDecompress2(handle, jpeg_data, (unsigned long)jpeg_size,
                       rgb_buf, dw, 0, dh, TJPF_RGB, 0);
    tjDestroy(handle);
    if (ret != 0) {
        Free(rgb_buf);
        return OPRT_COM_ERROR;
    }
    s = rgb_buf;
    d = (uint16_t *)out->out_buf;
    for (i = pixels; i > 0; i--) {
        uint16_t r = (uint16_t)s[0], g = (uint16_t)s[1], b = (uint16_t)s[2];
        *d++ = (uint16_t)((r & 0xF8u) << 8 | (g & 0xFCu) << 3 | (b >> 3));
        s += 3;
    }
    Free(rgb_buf);
    return OPRT_OK;
}
#endif /* ENABLE_LIBJPEGTURBO */

OPERATE_RET tal_image_jpeg_decode_rgb565(const uint8_t *jpeg_data,
                                         uint32_t       jpeg_size,
                                         TAL_IMAGE_JPEG_OUTPUT_T *out)
{
    if (jpeg_data == NULL || out == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (out->out_buf == NULL || out->out_buf_size == 0) {
        return OPRT_INVALID_PARM;
    }

#if defined(ENABLE_LIBJPEGTURBO)
    {
        OPERATE_RET rt = __libjpeg_scan_rgb565(jpeg_data, jpeg_size, out);
        if (rt != OPRT_OK) {
            /* JCS_RGB565 is refused for lossless JPEG. Retry through the
             * RGB888-and-pack route, which costs a full extra intermediate but
             * handles everything the decoder can produce at all. */
            rt = __turbo_pack_rgb565(jpeg_data, jpeg_size, out);
        }
        return rt;
    }
#else
    {
        JDEC              jd;
        uint8_t          *workbuf;
        jpeg_mem_stream_t stream;
        JRESULT           rc;

        workbuf = (uint8_t *)Malloc(TJPGD_WORKBUF_SIZE);
        if (workbuf == NULL) {
            return OPRT_MALLOC_FAILED;
        }
        memset(&stream, 0, sizeof(stream));
        stream.data = jpeg_data;
        stream.size = jpeg_size;
        stream.pos  = 0;

        rc = jd_prepare(&jd, tjpgd_input_func, workbuf, (size_t)TJPGD_WORKBUF_SIZE, &stream);
        if (rc != JDR_OK) {
            Free(workbuf);
            return OPRT_COM_ERROR;
        }
        if (jd.width != out->out_width || jd.height != out->out_height) {
            Free(workbuf);
            return OPRT_INVALID_PARM;
        }
        s_out_ctx.out_buf    = out->out_buf;
        s_out_ctx.out_width  = out->out_width;
        s_out_ctx.out_height = out->out_height;
        rc                   = jd_decomp(&jd, tjpgd_outfunc_rgb565, 0);
        Free(workbuf);
        if (rc != JDR_OK) {
            return OPRT_COM_ERROR;
        }
    }
#endif
    return OPRT_OK;
}

OPERATE_RET tal_image_jpeg_decode_rgb888(const uint8_t *jpeg_data,
                                         uint32_t       jpeg_size,
                                         TAL_IMAGE_JPEG_OUTPUT_T *out)
{
    if (jpeg_data == NULL || out == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (out->out_buf == NULL || out->out_buf_size == 0) {
        return OPRT_INVALID_PARM;
    }

#if defined(ENABLE_LIBJPEGTURBO)
    {
        tjhandle handle;
        int      w, h, subsamp;
        int      ret;
        uint16_t dw, dh;

        handle = tjInitDecompress();
        if (handle == NULL) {
            return OPRT_COM_ERROR;
        }
        ret = tjDecompressHeader2(handle, (unsigned char *)jpeg_data, (unsigned long)jpeg_size,
                                 &w, &h, &subsamp);
        if (ret != 0) {
            tjDestroy(handle);
            return OPRT_COM_ERROR;
        }
        if (w <= 0 || h <= 0 ||
            tal_image_jpeg_calc_scaled_size((uint16_t)w, (uint16_t)h,
                                            out->out_width, out->out_height,
                                            &dw, &dh) != OPRT_OK ||
            dw != out->out_width || dh != out->out_height) {
            tjDestroy(handle);
            return OPRT_INVALID_PARM;
        }
        ret = tjDecompress2(handle, jpeg_data, (unsigned long)jpeg_size,
                           out->out_buf, dw, 0, dh, TJPF_RGB, 0);
        tjDestroy(handle);
        if (ret != 0) {
            return OPRT_COM_ERROR;
        }
    }
#else
    {
        JDEC              jd;
        uint8_t          *workbuf;
        jpeg_mem_stream_t stream;
        JRESULT           rc;

        workbuf = (uint8_t *)Malloc(TJPGD_WORKBUF_SIZE);
        if (workbuf == NULL) {
            return OPRT_MALLOC_FAILED;
        }
        memset(&stream, 0, sizeof(stream));
        stream.data = jpeg_data;
        stream.size = jpeg_size;
        stream.pos  = 0;

        rc = jd_prepare(&jd, tjpgd_input_func, workbuf, (size_t)TJPGD_WORKBUF_SIZE, &stream);
        if (rc != JDR_OK) {
            Free(workbuf);
            return OPRT_COM_ERROR;
        }
        if (jd.width != out->out_width || jd.height != out->out_height) {
            Free(workbuf);
            return OPRT_INVALID_PARM;
        }
        s_out_ctx.out_buf    = out->out_buf;
        s_out_ctx.out_width  = out->out_width;
        s_out_ctx.out_height = out->out_height;
        rc                   = jd_decomp(&jd, tjpgd_outfunc_rgb888, 0);
        Free(workbuf);
        if (rc != JDR_OK) {
            return OPRT_COM_ERROR;
        }
    }
#endif
    return OPRT_OK;
}

OPERATE_RET tal_image_jpeg_decode_gray(const uint8_t *jpeg_data,
                                       uint32_t       jpeg_size,
                                       TAL_IMAGE_JPEG_OUTPUT_T *out)
{
    if (jpeg_data == NULL || out == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (out->out_buf == NULL || out->out_buf_size == 0) {
        return OPRT_INVALID_PARM;
    }

#if defined(ENABLE_LIBJPEGTURBO)
    {
        tjhandle handle;
        int      w, h, subsamp;
        int      ret;
        uint16_t dw, dh;

        handle = tjInitDecompress();
        if (handle == NULL) {
            return OPRT_COM_ERROR;
        }
        ret = tjDecompressHeader2(handle, (unsigned char *)jpeg_data, (unsigned long)jpeg_size,
                                 &w, &h, &subsamp);
        if (ret != 0) {
            tjDestroy(handle);
            return OPRT_COM_ERROR;
        }
        if (w <= 0 || h <= 0 ||
            tal_image_jpeg_calc_scaled_size((uint16_t)w, (uint16_t)h,
                                            out->out_width, out->out_height,
                                            &dw, &dh) != OPRT_OK ||
            dw != out->out_width || dh != out->out_height) {
            tjDestroy(handle);
            return OPRT_INVALID_PARM;
        }
        ret = tjDecompress2(handle, jpeg_data, (unsigned long)jpeg_size,
                           out->out_buf, dw, 0, dh, TJPF_GRAY, 0);
        tjDestroy(handle);
        if (ret != 0) {
            return OPRT_COM_ERROR;
        }
    }
#else
    {
        JDEC              jd;
        uint8_t          *workbuf;
        jpeg_mem_stream_t stream;
        JRESULT           rc;

        workbuf = (uint8_t *)Malloc(TJPGD_WORKBUF_SIZE);
        if (workbuf == NULL) {
            return OPRT_MALLOC_FAILED;
        }
        memset(&stream, 0, sizeof(stream));
        stream.data = jpeg_data;
        stream.size = jpeg_size;
        stream.pos  = 0;

        rc = jd_prepare(&jd, tjpgd_input_func, workbuf, (size_t)TJPGD_WORKBUF_SIZE, &stream);
        if (rc != JDR_OK) {
            Free(workbuf);
            return OPRT_COM_ERROR;
        }
        if (jd.width != out->out_width || jd.height != out->out_height) {
            Free(workbuf);
            return OPRT_INVALID_PARM;
        }
        s_out_ctx.out_buf    = out->out_buf;
        s_out_ctx.out_width  = out->out_width;
        s_out_ctx.out_height = out->out_height;
        rc                   = jd_decomp(&jd, tjpgd_outfunc_gray, 0);
        Free(workbuf);
        if (rc != JDR_OK) {
            return OPRT_COM_ERROR;
        }
    }
#endif
    return OPRT_OK;
}

OPERATE_RET tal_image_jpeg_decode_bitmap(const uint8_t *jpeg_data,
                                          uint32_t       jpeg_size,
                                          TAL_IMAGE_JPEG_OUTPUT_T *out,
                                          uint8_t        threshold)
{
    if (jpeg_data == NULL || out == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (out->out_buf == NULL || out->out_buf_size == 0) {
        return OPRT_INVALID_PARM;
    }

    uint16_t w = out->out_width;
    uint16_t h = out->out_height;
    uint32_t bytes_per_line = ((uint32_t)w + 7u) / 8u;
    uint32_t bmp_need = bytes_per_line * h;

    if (out->out_buf_size < bmp_need) {
        return OPRT_BUFFER_NOT_ENOUGH;
    }

    uint32_t gray_size = (uint32_t)w * h;
    uint8_t *gray_buf = (uint8_t *)Malloc(gray_size);
    if (gray_buf == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    TAL_IMAGE_JPEG_OUTPUT_T gray_out = {
        .out_buf      = gray_buf,
        .out_buf_size = gray_size,
        .out_width    = w,
        .out_height   = h,
    };
    OPERATE_RET rt = tal_image_jpeg_decode_gray(jpeg_data, jpeg_size, &gray_out);
    if (rt != OPRT_OK) {
        Free(gray_buf);
        return rt;
    }

    int16_t thr = (int16_t)threshold;
    uint8_t *bmp = out->out_buf;
    memset(bmp, 0, bmp_need);

    for (uint16_t y = 0; y < h; y++) {
        uint8_t *row  = gray_buf + (uint32_t)y * w;
        uint8_t *orow = bmp + (uint32_t)y * bytes_per_line;

        for (uint16_t x = 0; x < w; x++) {
            int16_t old_px = (int16_t)row[x];
            uint8_t new_px = (old_px < thr) ? 0 : 255;
            int16_t err    = old_px - (int16_t)new_px;

            if (new_px == 0) {
                orow[x >> 3] |= (0x80u >> (x & 7));
            }

            if (x + 1 < w) {
                int16_t v = (int16_t)row[x + 1] + (err * 7 / 16);
                row[x + 1] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
            }
            if (y + 1 < h) {
                uint8_t *next = gray_buf + (uint32_t)(y + 1) * w;
                if (x > 0) {
                    int16_t v = (int16_t)next[x - 1] + (err * 3 / 16);
                    next[x - 1] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
                }
                {
                    int16_t v = (int16_t)next[x] + (err * 5 / 16);
                    next[x] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
                }
                if (x + 1 < w) {
                    int16_t v = (int16_t)next[x + 1] + (err * 1 / 16);
                    next[x + 1] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
                }
            }
        }
    }

    Free(gray_buf);
    return OPRT_OK;
}
