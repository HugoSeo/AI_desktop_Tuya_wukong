/**
 * @file tal_lcd_service.c
 * @brief LCD conversion service implementation.
 *
 * Wraps:
 *  - tkl_dma2d  : YUV422 → RGB565  (hardware accelerated, synchronous via semaphore)
 *  - bk_jpeg_hw : MJPEG  → RGB565  (hardware JPEG decoder)
 *  - tuya_lcd_pbuf : RGB565 buffer pool
 *
 * @author linch
 * @date 2025-02-09
 * @version 1.0.0
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All rights reserved.
 */

#include "tal_lcd_service.h"
#include "tal_log.h"
#include "tal_semaphore.h"
#include "tal_memory.h"
#include "tkl_dma2d.h"
#include "tuya_lcd_pbuf.h"
#include "tal_system.h"

/* ------------------------------------------------------------------ */
/*  Hardware JPEG decoder — forward decls (implemented in bk platform) */
/* ------------------------------------------------------------------ */
extern void bk_jpeg_hw_decode_to_mem_init(void);
extern void bk_jpeg_hw_decode_to_mem_deinit(void);
extern int  bk_jpeg_hw_decode_to_mem(uint8_t *src, uint8_t *dst,
                                     uint32_t src_size,
                                     uint16_t dst_w, uint16_t dst_h);

/* ------------------------------------------------------------------ */
/*  Performance timing — define TAL_LCD_SERVICE_PERF to enable         */
/* ------------------------------------------------------------------ */
/* #define TAL_LCD_SERVICE_PERF */

#ifdef TAL_LCD_SERVICE_PERF
#define LCD_PERF_START(t)   SYS_TICK_T t = tal_system_get_tick_count()
#define LCD_PERF_END(t, fmt, ...) \
    TAL_PR_DEBUG(fmt " (%u ticks)", ##__VA_ARGS__, \
                 (UINT32_T)(tal_system_get_tick_count() - (t)))
#else
#define LCD_PERF_START(t)        ((void)0)
#define LCD_PERF_END(t, fmt, ...) ((void)0)
#endif

/* ------------------------------------------------------------------ */
/*  Module state                                                        */
/* ------------------------------------------------------------------ */
typedef struct {
    BOOL_T               init;
    BOOL_T               dma2d_init;
    TAL_LCD_DMA2D_MODE_E dma2d_mode;  /* set at init time, used by dma2d_init/deinit */
    UINT16_T             src_w;       /* camera output width  (MJPEG decode size) */
    UINT16_T             src_h;       /* camera output height (MJPEG decode size) */
    UINT16_T             lcd_w;       /* display width  */
    UINT16_T             lcd_h;       /* display height */
    UINT8_T             *decode_buf;  /* MJPEG: pre-alloc src_w×src_h RGB565 decode buffer */
    BOOL_T               byte_swap;   /* TRUE: swap RGB565 bytes (SPI screens) */
    SEM_HANDLE           dma2d_sem;   /* signals DMA2D completion (YUV path only) */
} lcd_svc_t;

static lcd_svc_t s_svc = {0};

/* ------------------------------------------------------------------ */
/*  DMA2D IRQ callback                                                  */
/* ------------------------------------------------------------------ */
static void __dma2d_irq_cb(TUYA_DMA2D_IRQ_E type, void *args)
{
    lcd_svc_t *svc = (lcd_svc_t *)args;

    if (svc && svc->dma2d_sem) {
        tal_semaphore_post(svc->dma2d_sem);
    }
}

/* ------------------------------------------------------------------ */
/*  Lifecycle                                                           */
/* ------------------------------------------------------------------ */

OPERATE_RET tal_lcd_service_init(UINT16_T src_w, UINT16_T src_h,
                                 UINT16_T lcd_w, UINT16_T lcd_h,
                                 UINT8_T  pool_size,
                                 TAL_LCD_DMA2D_MODE_E mode,
                                 BOOL_T   byte_swap)
{
    if (s_svc.init) {
        TAL_PR_WARN("lcd_service already initialized");
        return OPRT_OK;
    }
    if (!src_w || !src_h || !lcd_w || !lcd_h || !pool_size) {
        return OPRT_INVALID_PARM;
    }

    s_svc.src_w      = src_w;
    s_svc.src_h      = src_h;
    s_svc.lcd_w      = lcd_w;
    s_svc.lcd_h      = lcd_h;
    s_svc.dma2d_mode = mode;
    s_svc.byte_swap  = byte_swap;
    s_svc.decode_buf = NULL;

    /* DMA2D semaphore (binary) — created once here, used during dma2d_init */
    OPERATE_RET rt = tal_semaphore_create_init(&s_svc.dma2d_sem, 0, 1);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("lcd_service: dma2d sem create failed: %d", rt);
        return rt;
    }

    /* pbuf always at lcd size — the decode_buf (src size) is separate for MJPEG */
    rt = tuya_lcd_pbuf_init(lcd_w, lcd_h, pool_size);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("lcd_service: pbuf init failed: %d", rt);
        tal_semaphore_release(s_svc.dma2d_sem);
        s_svc.dma2d_sem = NULL;
        return rt;
    }

    /* NOTE: DMA2D and JPEG decoder are NOT initialized here.
     *       Both share the same DMA2D hardware.  Call tal_lcd_service_dma2d_init()
     *       after the UI layer has released the DMA2D hardware. */

    s_svc.init = TRUE;
    TAL_PR_DEBUG("lcd_service init ok src=%dx%d lcd=%dx%d mode=%d byte_swap=%d pool=%d",
                 src_w, src_h, lcd_w, lcd_h, mode, byte_swap, pool_size);
    return OPRT_OK;
}

OPERATE_RET tal_lcd_service_deinit(VOID)
{
    if (!s_svc.init) {
        return OPRT_OK;
    }

    tuya_lcd_pbuf_deinit();

    if (s_svc.decode_buf) {
        tal_psram_free(s_svc.decode_buf);
        s_svc.decode_buf = NULL;
    }

    if (s_svc.dma2d_sem) {
        tal_semaphore_release(s_svc.dma2d_sem);
        s_svc.dma2d_sem = NULL;
    }

    s_svc.init       = FALSE;
    s_svc.src_w      = 0;
    s_svc.src_h      = 0;
    s_svc.lcd_w      = 0;
    s_svc.lcd_h      = 0;
    s_svc.byte_swap  = FALSE;
    TAL_PR_DEBUG("lcd_service deinit ok");
    return OPRT_OK;
}

OPERATE_RET tal_lcd_service_dma2d_init(VOID)
{
    if (!s_svc.init) {
        TAL_PR_ERR("lcd_service not initialized");
        return OPRT_COM_ERROR;
    }
    if (s_svc.dma2d_init) {
        return OPRT_OK;
    }

    OPERATE_RET rt = OPRT_OK;

    if (s_svc.dma2d_mode == TAL_LCD_DMA2D_MODE_YUV) {
        /* tkl_dma2d owns DMA2D; used by yuv2rgb */
        TUYA_DMA2D_BASE_CFG_T dma2d_cfg = {
            .cb  = __dma2d_irq_cb,
            .arg = &s_svc,
        };
        rt = tkl_dma2d_init(&dma2d_cfg);
        if (rt != OPRT_OK) {
            TAL_PR_ERR("lcd_service: tkl_dma2d_init failed: %d", rt);
            return rt;
        }
    } else {
        /* bk_jpeg_hw owns DMA2D; used by mjpeg2rgb.
         * Pre-allocate the decode buffer (src_w × src_h RGB565) once here
         * so mjpeg2rgb does not allocate/free on every frame. */
        if (!s_svc.decode_buf) {
            UINT32_T buf_size = (UINT32_T)s_svc.src_w * s_svc.src_h * 2;
            s_svc.decode_buf = (UINT8_T *)tal_psram_malloc(buf_size);
            if (!s_svc.decode_buf) {
                TAL_PR_ERR("lcd_service: decode_buf alloc failed (%u bytes)", buf_size);
                return OPRT_MALLOC_FAILED;
            }
        }
        bk_jpeg_hw_decode_to_mem_init();
    }

    s_svc.dma2d_init = TRUE;
    TAL_PR_DEBUG("lcd_service dma2d init ok (mode=%d)", s_svc.dma2d_mode);
    return OPRT_OK;
}

OPERATE_RET tal_lcd_service_dma2d_deinit(VOID)
{
    if (!s_svc.dma2d_init) {
        return OPRT_OK;
    }

    if (s_svc.dma2d_mode == TAL_LCD_DMA2D_MODE_YUV) {
        tkl_dma2d_deinit();
    } else {
        bk_jpeg_hw_decode_to_mem_deinit();
        if (s_svc.decode_buf) {
            tal_psram_free(s_svc.decode_buf);
            s_svc.decode_buf = NULL;
        }
    }

    s_svc.dma2d_init = FALSE;
    TAL_PR_DEBUG("lcd_service dma2d deinit ok (mode=%d)", s_svc.dma2d_mode);
    return OPRT_OK;
}

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                    */
/* ------------------------------------------------------------------ */

/**
 * Swap RGB565 bytes of a w×h region.
 * src and dst may alias (in-place) or be separate buffers of the same size.
 */
static inline void __rgb565_swap(UINT16_T *dst, const UINT16_T *src, UINT16_T w, UINT16_T h)
{
    UINT32_T pixels = (UINT32_T)w * h;
    for (UINT32_T k = 0; k < pixels; k++) {
        dst[k] = (UINT16_T)((src[k] >> 8) | (src[k] << 8));
    }
}

/* ------------------------------------------------------------------ */
/*  YUV422 → RGB565 via DMA2D                                          */
/* ------------------------------------------------------------------ */

OPERATE_RET tal_lcd_service_yuv2rgb(const UINT8_T *src_data,
                                    tuya_lcd_pbuf_node_t **out)
{
    if (!s_svc.init || !src_data || !out) {
        return OPRT_INVALID_PARM;
    }
    if (!s_svc.dma2d_init || s_svc.dma2d_mode != TAL_LCD_DMA2D_MODE_YUV) {
        TAL_PR_ERR("yuv2rgb: DMA2D not in YUV mode, call tal_lcd_service_dma2d_init(YUV) first");
        return OPRT_COM_ERROR;
    }

    tuya_lcd_pbuf_node_t *node = tuya_lcd_pbuf_acquire(300);
    if (!node) {
        TAL_PR_WARN("yuv2rgb: no pbuf available");
        return OPRT_COM_ERROR;
    }

    /* Centre-crop offset for DMA2D axis */
    UINT16_T off_x = (s_svc.src_w > s_svc.lcd_w) ? (s_svc.src_w - s_svc.lcd_w) / 2 : 0;
    UINT16_T off_y = (s_svc.src_h > s_svc.lcd_h) ? (s_svc.src_h - s_svc.lcd_h) / 2 : 0;

    TKL_DMA2D_FRAME_INFO_T src = {
        .type      = TUYA_FRAME_FMT_YUV422,
        .pbuf      = (CHAR_T *)src_data,
        .width     = s_svc.src_w,
        .height    = s_svc.src_h,
        .axis      = {off_x, off_y},
        .width_cp  = s_svc.lcd_w,
        .height_cp = s_svc.lcd_h,
    };

    TKL_DMA2D_FRAME_INFO_T dst = {
        .type      = TUYA_FRAME_FMT_RGB565,
        .pbuf      = (CHAR_T *)node->frame.frame,
        .width     = s_svc.lcd_w,
        .height    = s_svc.lcd_h,
        .axis      = {0, 0},
        .width_cp  = s_svc.lcd_w,
        .height_cp = s_svc.lcd_h,
    };

    OPERATE_RET rt = tkl_dma2d_convert(&src, &dst);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("yuv2rgb: dma2d_convert failed: %d", rt);
        tuya_lcd_pbuf_release(node);
        return OPRT_COM_ERROR;
    }

    /* Wait for DMA2D completion */
    rt = tal_semaphore_wait(s_svc.dma2d_sem, 1000);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("yuv2rgb: dma2d timeout");
        tuya_lcd_pbuf_release(node);
        return OPRT_COM_ERROR;
    }

    if (s_svc.byte_swap) {
        __rgb565_swap((UINT16_T *)node->frame.frame,
                      (const UINT16_T *)node->frame.frame,
                      s_svc.lcd_w, s_svc.lcd_h);
    }

    *out = node;
    return OPRT_OK;
}

/* ------------------------------------------------------------------ */
/*  MJPEG → RGB565 via hardware JPEG decoder                           */
/* ------------------------------------------------------------------ */

OPERATE_RET tal_lcd_service_mjpeg2rgb(const UINT8_T *src_data,
                                      UINT32_T src_len,
                                      tuya_lcd_pbuf_node_t **out)
{
    if (!s_svc.init || !src_data || !src_len || !out) {
        return OPRT_INVALID_PARM;
    }
    if (!s_svc.dma2d_init || s_svc.dma2d_mode != TAL_LCD_DMA2D_MODE_JPEG) {
        TAL_PR_ERR("mjpeg2rgb: DMA2D not in JPEG mode, call tal_lcd_service_dma2d_init(JPEG) first");
        return OPRT_COM_ERROR;
    }

    tuya_lcd_pbuf_node_t *node = tuya_lcd_pbuf_acquire(300);
    if (!node) {
        TAL_PR_WARN("mjpeg2rgb: no pbuf available");
        return OPRT_COM_ERROR;
    }

    /* Step 1: decode full JPEG into the pre-allocated decode_buf (src_w × src_h).
     * bk_jpeg_hw requires dimensions to be multiples of 32/8. */
    LCD_PERF_START(t_decode);
    int ret = bk_jpeg_hw_decode_to_mem(
        (uint8_t *)src_data,
        (uint8_t *)s_svc.decode_buf,
        src_len,
        s_svc.src_w,
        s_svc.src_h
    );
    LCD_PERF_END(t_decode, "mjpeg2rgb: hw decode");
    if (ret != 0) {
        TAL_PR_ERR("mjpeg2rgb: hw decode failed: %d", ret);
        tuya_lcd_pbuf_release(node);
        return OPRT_COM_ERROR;
    }

    /* Step 2: copy/swap lcd_w × lcd_h from decode_buf into pbuf. */
    const UINT16_T *src_ptr = (const UINT16_T *)s_svc.decode_buf;
    UINT16_T       *dst_ptr = (UINT16_T *)node->frame.frame;
    LCD_PERF_START(t_copy);

#if 1   /* no crop: camera outputs lcd resolution directly */
    if (s_svc.byte_swap) {
        __rgb565_swap(dst_ptr, src_ptr, s_svc.lcd_w, s_svc.lcd_h);
    } else {
        memcpy(dst_ptr, src_ptr, (UINT32_T)s_svc.lcd_w * s_svc.lcd_h * 2);
    }
#else   /* centre-crop: src_w × src_h → lcd_w × lcd_h */
    UINT16_T off_x = (s_svc.src_w - s_svc.lcd_w) / 2;
    UINT16_T off_y = (s_svc.src_h - s_svc.lcd_h) / 2;
    src_ptr += off_y * s_svc.src_w + off_x;
    for (UINT16_T row = 0; row < s_svc.lcd_h; row++) {
        const UINT16_T *s = src_ptr + row * s_svc.src_w;
        UINT16_T       *d = dst_ptr + row * s_svc.lcd_w;
        if (s_svc.byte_swap) {
            for (UINT16_T col = 0; col < s_svc.lcd_w; col++) {
                d[col] = (UINT16_T)((s[col] >> 8) | (s[col] << 8));
            }
        } else {
            memcpy(d, s, s_svc.lcd_w * 2);
        }
    }
#endif
    LCD_PERF_END(t_copy, "mjpeg2rgb: copy/swap(%dx%d)", s_svc.lcd_w, s_svc.lcd_h);
    *out = node;
    return OPRT_OK;
}

/* ------------------------------------------------------------------ */
/*  Buffer release                                                      */
/* ------------------------------------------------------------------ */

OPERATE_RET tal_lcd_service_release_pbuf(tuya_lcd_pbuf_node_t *node)
{
    return tuya_lcd_pbuf_release(node);
}
