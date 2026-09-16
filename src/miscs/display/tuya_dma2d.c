/**
 * @file tuya_dma2d.c
 * @brief Single owner of the DMA2D hardware engine (see tuya_dma2d.h).
 *
 * Decoupled from the UI: the engine is brought up by whichever consumer needs it
 * first (camera service or UI display port) via a reference count, and every op
 * (camera YUV->RGB conversion + LVGL flush copy) is serialised on one mutex so
 * the two issuing threads never overlap on the single engine.
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */
#include "tuya_dma2d.h"
#include "tuya_app_config.h"
#include "tal_mutex.h"
#include "tal_semaphore.h"
#include "tal_log.h"
#include "tkl_dma2d.h"
#include <string.h>

#if defined(ENABLE_TAL_IMAGE) && (ENABLE_TAL_IMAGE == 1)
#include "tal_image_yuv422_to_rgb.h"
#define TUYA_DMA2D_HAVE_SW 1
#endif

/* Wait budget for a single DMA2D transfer (conversion or copy). A full-frame
 * RGB565 copy is sub-millisecond, so this is a WEDGE detector, not a normal
 * wait: it also bounds how long the UI thread (which holds the LVGL lock during
 * a flush copy) can stall on a stuck engine — a 1s stall here tripped the
 * watchdog. On timeout the caller falls back to a CPU copy / SW convert.
 *
 * Kept deliberately generous (not tens of ms): tkl has no abort, so on timeout
 * the abandoned transfer keeps running. A too-short budget would mistake a
 * merely slow transfer (e.g. contending with the DVP camera for PSRAM bandwidth)
 * for a wedge and fall back to a CPU/SW write into the SAME out_buf the engine
 * is still writing. 500ms is far above any real transfer yet well under the
 * watchdog window. Truly closing this needs abort-on-timeout — see the
 * tkl_dma2d_stop() follow-up noted in dma2d_op_run. */
#define TUYA_DMA2D_OP_TMO_MS 500

/* s_init_mtx guards the reference count and the first/last resource
 * create/destroy, so init()/deinit() are safe even when the two consumers
 * (camera service / UI display port) bring the engine up or down from
 * different threads at runtime (e.g. toggling the camera while the UI runs).
 * It is created lazily on the first init — that first call happens during
 * single-threaded device bring-up, so creating it there is race-free. */
static MUTEX_HANDLE s_init_mtx;
static int          s_ref;
static SEM_HANDLE   s_dma2d_sem;     /* posted by the IRQ on every terminal event */
static MUTEX_HANDLE s_dma2d_op_mtx;  /* serialises the one engine across threads */

/* Diagnostics — a non-zero count preceding a UI heap crash implicates a wedged
 * or failing DMA2D transfer (see dma2d_op_run). Read via tuya_dma2d_get_diag(). */
static volatile uint32_t         s_diag_timeout_cnt;
static volatile uint32_t         s_diag_error_cnt;
static volatile TUYA_DMA2D_IRQ_E s_last_evt;   /* last terminal event seen by the ISR */

static void dma2d_irq_cb(TUYA_DMA2D_IRQ_E type, VOID_T *args)
{
    (void)args;
    s_last_evt = type;
    if (type != TUYA_DMA2D_TRANS_COMPLETE_ISR) {
        s_diag_error_cnt++;   /* CFG_ERROR / TRANS_ERROR */
    }
    /* Wake the waiter on EVERY terminal event (complete OR error). Posting only
     * on completion left a failed transfer to stall the issuing thread for the
     * whole timeout, and a completion arriving *after* that timeout would leave
     * a stale post that desyncs the next op's wait. */
    tal_semaphore_post(s_dma2d_sem);
}

OPERATE_RET tuya_dma2d_init(VOID)
{
    /* Lazily create the lifecycle guard on the first (bring-up, single-threaded)
     * call; it then serialises every later runtime init/deinit. */
    if (s_init_mtx == NULL) {
        OPERATE_RET mrt = tal_mutex_create_init(&s_init_mtx);
        if (mrt != OPRT_OK) {
            return mrt;
        }
    }

    tal_mutex_lock(s_init_mtx);

    if (s_ref > 0) {
        s_ref++;
        tal_mutex_unlock(s_init_mtx);
        return OPRT_OK;
    }

    OPERATE_RET rt = tal_semaphore_create_init(&s_dma2d_sem, 0, 1);
    if (rt != OPRT_OK) {
        tal_mutex_unlock(s_init_mtx);
        return rt;
    }
    rt = tal_mutex_create_init(&s_dma2d_op_mtx);
    if (rt != OPRT_OK) {
        tal_semaphore_release(s_dma2d_sem);
        s_dma2d_sem = NULL;
        tal_mutex_unlock(s_init_mtx);
        return rt;
    }

    static const TUYA_DMA2D_BASE_CFG_T dma2d_cfg = {
        .cb  = dma2d_irq_cb,
        .arg = NULL,
    };
    rt = tkl_dma2d_init(&dma2d_cfg);
    if (rt != OPRT_OK) {
        tal_mutex_release(s_dma2d_op_mtx);
        s_dma2d_op_mtx = NULL;
        tal_semaphore_release(s_dma2d_sem);
        s_dma2d_sem = NULL;
        tal_mutex_unlock(s_init_mtx);
        return rt;
    }

    s_ref = 1;
    tal_mutex_unlock(s_init_mtx);
    return OPRT_OK;
}

OPERATE_RET tuya_dma2d_deinit(VOID)
{
    if (s_init_mtx == NULL) {
        return OPRT_OK;   /* never initialised */
    }

    tal_mutex_lock(s_init_mtx);

    if (s_ref <= 0) {
        tal_mutex_unlock(s_init_mtx);
        return OPRT_OK;
    }
    if (--s_ref > 0) {
        tal_mutex_unlock(s_init_mtx);
        return OPRT_OK;
    }

    /* Last consumer: make sure nothing is in flight, then tear down. Callers
     * must already have stopped their producers (camera streams / LVGL flush)
     * before dropping the final reference, so no convert/memcpy can start once
     * we are here — the drain only has to wait out an op already issued. */
    tuya_dma2d_drain();
    tkl_dma2d_deinit();

    if (s_dma2d_op_mtx) {
        tal_mutex_release(s_dma2d_op_mtx);
        s_dma2d_op_mtx = NULL;
    }
    if (s_dma2d_sem) {
        tal_semaphore_release(s_dma2d_sem);
        s_dma2d_sem = NULL;
    }
    tal_mutex_unlock(s_init_mtx);
    return OPRT_OK;
}

BOOL_T tuya_dma2d_is_ready(VOID)
{
    return (s_ref > 0 && s_dma2d_sem != NULL && s_dma2d_op_mtx != NULL);
}

VOID tuya_dma2d_drain(VOID)
{
    if (s_dma2d_op_mtx) {
        tal_mutex_lock(s_dma2d_op_mtx);
        tal_mutex_unlock(s_dma2d_op_mtx);
    }
}

VOID tuya_dma2d_get_diag(uint32_t *timeout_cnt, uint32_t *error_cnt)
{
    if (timeout_cnt) *timeout_cnt = s_diag_timeout_cnt;
    if (error_cnt)   *error_cnt   = s_diag_error_cnt;
}

/* Issue one DMA2D op and wait its completion, exclusively against every other
 * DMA2D user. tkl_dma2d_memcpy / tkl_dma2d_convert share the same signature, so
 * one helper serves both the flush copy and the camera conversion. */
static OPERATE_RET dma2d_op_run(BOOL_T is_convert,
                                TKL_DMA2D_FRAME_INFO_T *in_frame,
                                TKL_DMA2D_FRAME_INFO_T *out_frame)
{
    OPERATE_RET ret;

    tal_mutex_lock(s_dma2d_op_mtx);

    /* Absorb a stale completion left by a previously timed-out op: after a normal
     * completion nothing is in flight, but a timed-out op's transfer is NOT aborted
     * (tkl has no stop), so it can post its semaphore late. Draining here clears any
     * such post that has ALREADY arrived, so the common case (late post lands before
     * the next op starts) reflects the right transfer. Bounded (sem max count 1);
     * 0 = non-blocking.
     *
     * Residual window (NOT closed here): if the previous transfer's late completion
     * arrives AFTER this drain but DURING the wait below, our wait returns on that
     * foreign post while our own transfer is still writing — the desync this fix is
     * about. That can only happen for a transfer that survives the (generous) timeout
     * yet still completes, which requires abort-on-timeout to eliminate. See the
     * tkl_dma2d_stop() follow-up. */
    while (tal_semaphore_wait(s_dma2d_sem, 0) == OPRT_OK) { }

    s_last_evt = TUYA_DMA2D_TRANS_COMPLETE_ISR;   /* ISR overwrites this on error */
    ret = is_convert ? tkl_dma2d_convert(in_frame, out_frame)
                     : tkl_dma2d_memcpy(in_frame, out_frame);
    if (ret == OPRT_OK) {
        ret = tal_semaphore_wait(s_dma2d_sem, TUYA_DMA2D_OP_TMO_MS);
        if (ret != OPRT_OK) {
            /* Engine wedged (> timeout). Report it and fail so the caller falls
             * back to a CPU copy / SW convert. The next op's drain (above) absorbs
             * a late completion that arrives before that op starts, which NARROWS
             * the desync window that let a later op reuse a buffer the engine was
             * still writing (the idle UI heap crash). It does NOT fully close it:
             * without a tkl abort we cannot stop the abandoned transfer, so a late
             * completion landing during the next op's wait can still desync it. A
             * generous timeout keeps this to genuine wedges (rare); full closure
             * needs abort-on-timeout via a tkl_dma2d_stop() wrapper (follow-up). */
            s_diag_timeout_cnt++;
            TAL_PR_ERR("dma2d wedged (convert=%d) timeouts=%u errors=%u",
                       (int)is_convert, s_diag_timeout_cnt, s_diag_error_cnt);
        } else if (s_last_evt != TUYA_DMA2D_TRANS_COMPLETE_ISR) {
            /* Woke on an error IRQ, not completion — transfer did not finish. */
            ret = OPRT_COM_ERROR;
            TAL_PR_ERR("dma2d error evt=%d errors=%u", (int)s_last_evt, s_diag_error_cnt);
        }
    }

    tal_mutex_unlock(s_dma2d_op_mtx);
    return ret;
}

OPERATE_RET tuya_dma2d_memcpy(TKL_DMA2D_FRAME_INFO_T *in_frame, TKL_DMA2D_FRAME_INFO_T *out_frame)
{
    if (in_frame == NULL || out_frame == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (!tuya_dma2d_is_ready()) {
        return OPRT_COM_ERROR;   /* caller falls back to a CPU copy */
    }
    return dma2d_op_run(FALSE, in_frame, out_frame);
}

static OPERATE_RET sw_convert(TUYA_FRAME_FMT_E out_type,
                              uint8_t *in_buf, uint16_t in_w, uint16_t in_h,
                              uint8_t *out_buf, uint16_t out_w, uint16_t out_h)
{
#if defined(TUYA_DMA2D_HAVE_SW)
    TAL_IMAGE_YUV422_TO_RGB_T cfg = {0};
    cfg.in_buf     = in_buf;
    cfg.in_width   = in_w;
    cfg.in_height  = in_h;
    cfg.out_buf    = out_buf;
    cfg.out_width  = out_w;
    cfg.out_height = out_h;

    if (out_type == TUYA_FRAME_FMT_RGB888) {
        return tal_image_sw_convert_yuv422_to_rgb888(&cfg);
    }
    return tal_image_sw_convert_yuv422_to_rgb565(&cfg);
#else
    (void)out_type; (void)in_buf; (void)in_w; (void)in_h;
    (void)out_buf; (void)out_w; (void)out_h;
    return OPRT_NOT_SUPPORTED;
#endif
}

/* Hardware YUV422->RGB conversion, software fallback when the engine is not yet
 * up (e.g. preview started before display init) or the transfer fails. */
static OPERATE_RET dma2d_convert(TUYA_FRAME_FMT_E out_type,
                                 uint8_t *in_buf, uint16_t in_w, uint16_t in_h,
                                 uint8_t *out_buf, uint16_t out_w, uint16_t out_h)
{
    if (in_buf == NULL || out_buf == NULL || in_w == 0 || in_h == 0 ||
        out_w == 0 || out_h == 0) {
        return OPRT_INVALID_PARM;
    }

    if (!tuya_dma2d_is_ready()) {
        return sw_convert(out_type, in_buf, in_w, in_h, out_buf, out_w, out_h);
    }

    TKL_DMA2D_FRAME_INFO_T in_frame = {0};
    TKL_DMA2D_FRAME_INFO_T out_frame = {0};

    in_frame.type      = TUYA_FRAME_FMT_YUV422;
    in_frame.pbuf      = (CHAR_T *)in_buf;
    in_frame.width     = in_w;
    in_frame.height    = in_h;
    in_frame.width_cp  = (in_w <= out_w) ? in_w : out_w;
    in_frame.height_cp = (in_h <= out_h) ? in_h : out_h;

    out_frame.type     = out_type;
    out_frame.pbuf     = (CHAR_T *)out_buf;
    out_frame.width    = out_w;
    out_frame.height   = out_h;

    OPERATE_RET ret = dma2d_op_run(TRUE, &in_frame, &out_frame);
    if (ret != OPRT_OK) {
        return sw_convert(out_type, in_buf, in_w, in_h, out_buf, out_w, out_h);
    }
    return OPRT_OK;
}

OPERATE_RET tuya_dma2d_yuv422_to_rgb565(uint8_t *in_buf, uint16_t in_w, uint16_t in_h,
                                        uint8_t *out_buf, uint16_t out_w, uint16_t out_h)
{
    return dma2d_convert(TUYA_FRAME_FMT_RGB565, in_buf, in_w, in_h, out_buf, out_w, out_h);
}

OPERATE_RET tuya_dma2d_yuv422_to_rgb888(uint8_t *in_buf, uint16_t in_w, uint16_t in_h,
                                        uint8_t *out_buf, uint16_t out_w, uint16_t out_h)
{
    return dma2d_convert(TUYA_FRAME_FMT_RGB888, in_buf, in_w, in_h, out_buf, out_w, out_h);
}
