#include "tuya_ai_toy_camera.h"
#include "tuya_device_cfg.h"
#include "tuya_device_camera.h"
#include "tal_log.h"
#include "tal_mutex.h"
#include "tal_semaphore.h"
#include "tal_memory.h"

/* ================================================================== */
/*  Subscription table                                                 */
/* ================================================================== */

typedef struct {
    CAM_FRAME_CB cb;
    VOID        *ctx;
    BOOL_T       active;
} cam_sub_slot_t;

static cam_sub_slot_t s_subs[CAM_STREAM_MAX][CAM_CONSUMER_MAX];
static UINT_T         s_stream_refcnt[CAM_STREAM_MAX];
static MUTEX_HANDLE   s_sub_mtx    = NULL;
static BOOL_T         s_adapt_init = FALSE;

/**
 * Frame dispatcher: invoked by the board driver on every raw frame via
 * tuya_device_camera_set_raw_cb. Holds the subscription mutex only long
 * enough to copy active callbacks to the stack, then invokes them
 * unlocked so callbacks may call subscribe/unsubscribe without deadlocking.
 */
static VOID __dispatch_frame(const CAM_FRAME_T *frame, VOID *raw_ctx)
{
    (VOID)raw_ctx;
    if (!frame || frame->stream >= CAM_STREAM_MAX || !s_adapt_init) {
        return;
    }

    CAM_FRAME_CB cbs[CAM_CONSUMER_MAX];
    VOID        *ctxs[CAM_CONSUMER_MAX];
    UINT_T       n = 0;

    tal_mutex_lock(s_sub_mtx);
    for (UINT_T i = 0; i < CAM_CONSUMER_MAX; i++) {
        if (s_subs[frame->stream][i].active) {
            cbs[n]  = s_subs[frame->stream][i].cb;
            ctxs[n] = s_subs[frame->stream][i].ctx;
            n++;
        }
    }
    tal_mutex_unlock(s_sub_mtx);

    for (UINT_T i = 0; i < n; i++) {
        cbs[i](frame, ctxs[i]);
    }
}

/* ================================================================== */
/*  Lifecycle                                                          */
/* ================================================================== */

OPERATE_RET tuya_ai_toy_camera_init(VOID)
{
    if (s_adapt_init) {
        return OPRT_OK;
    }

    OPERATE_RET rt = tal_mutex_create_init(&s_sub_mtx);
    if (OPRT_OK != rt) {
        TAL_PR_ERR("camera adapt mutex init failed: %d", rt);
        return rt;
    }
    memset(s_subs, 0, sizeof(s_subs));
    memset(s_stream_refcnt, 0, sizeof(s_stream_refcnt));
    s_adapt_init = TRUE;

    rt = tuya_device_camera_init();
    if (OPRT_OK != rt) {
        tal_mutex_release(s_sub_mtx);
        s_sub_mtx    = NULL;
        s_adapt_init = FALSE;
        return rt;
    }
    return OPRT_OK;
}

OPERATE_RET tuya_ai_toy_camera_deinit(VOID)
{
    OPERATE_RET rt = tuya_device_camera_deinit();

    if (s_sub_mtx) {
        tal_mutex_release(s_sub_mtx);
        s_sub_mtx = NULL;
    }
    memset(s_subs, 0, sizeof(s_subs));
    memset(s_stream_refcnt, 0, sizeof(s_stream_refcnt));
    s_adapt_init = FALSE;

    return rt;
}

/* ================================================================== */
/*  Subscribe / Unsubscribe                                            */
/* ================================================================== */

OPERATE_RET tuya_ai_toy_camera_subscribe(CAM_STREAM_E stream,
                                          CAM_CONSUMER_E consumer,
                                          CAM_FRAME_CB cb,
                                          VOID *ctx)
{
    if (stream >= CAM_STREAM_MAX || consumer >= CAM_CONSUMER_MAX || !cb) {
        return OPRT_INVALID_PARM;
    }
    if (!s_adapt_init) {
        TAL_PR_ERR("camera adapt not initialized");
        return OPRT_COM_ERROR;
    }

    BOOL_T first = FALSE;
    tal_mutex_lock(s_sub_mtx);
    if (s_subs[stream][consumer].active) {
        tal_mutex_unlock(s_sub_mtx);
        TAL_PR_ERR("stream %d consumer %d already subscribed", stream, consumer);
        return OPRT_COM_ERROR;
    }
    s_subs[stream][consumer].cb     = cb;
    s_subs[stream][consumer].ctx    = ctx;
    s_subs[stream][consumer].active = TRUE;
    first = (s_stream_refcnt[stream]++ == 0);
    tal_mutex_unlock(s_sub_mtx);

    if (first) {
        /* Install dispatcher then start hardware — ensures the first frame
         * after start_stream already sees __dispatch_frame in place. */
        tuya_device_camera_set_raw_cb(stream, __dispatch_frame, NULL);
        OPERATE_RET rt = tuya_device_camera_start_stream(stream);
        if (OPRT_OK != rt) {
            /* Roll back: remove the entry and the raw cb we just installed. */
            tal_mutex_lock(s_sub_mtx);
            s_subs[stream][consumer].active = FALSE;
            s_subs[stream][consumer].cb     = NULL;
            s_subs[stream][consumer].ctx    = NULL;
            if (s_stream_refcnt[stream] > 0) s_stream_refcnt[stream]--;
            tal_mutex_unlock(s_sub_mtx);
            tuya_device_camera_set_raw_cb(stream, NULL, NULL);
            return rt;
        }
    }
    return OPRT_OK;
}

OPERATE_RET tuya_ai_toy_camera_unsubscribe(CAM_STREAM_E stream, CAM_CONSUMER_E consumer)
{
    if (stream >= CAM_STREAM_MAX || consumer >= CAM_CONSUMER_MAX) {
        return OPRT_INVALID_PARM;
    }
    if (!s_adapt_init) {
        return OPRT_COM_ERROR;
    }

    BOOL_T last = FALSE;
    tal_mutex_lock(s_sub_mtx);
    if (!s_subs[stream][consumer].active) {
        tal_mutex_unlock(s_sub_mtx);
        return OPRT_COM_ERROR;
    }
    s_subs[stream][consumer].active = FALSE;
    s_subs[stream][consumer].cb     = NULL;
    s_subs[stream][consumer].ctx    = NULL;
    if (s_stream_refcnt[stream] > 0) {
        last = (--s_stream_refcnt[stream] == 0);
    }
    tal_mutex_unlock(s_sub_mtx);

    if (last) {
        /* Stop hardware first, then clear the cb — any in-flight frame sees
         * a valid dispatcher; once stop_stream returns, no more frames come. */
        tuya_device_camera_stop_stream(stream);
        tuya_device_camera_set_raw_cb(stream, NULL, NULL);
    }
    return OPRT_OK;
}

/* ================================================================== */
/*  Snapshot — temporary subscribe + semaphore                         */
/* ================================================================== */

typedef struct {
    SEM_HANDLE sem;
    BYTE_T    *data;
    UINT_T     len;
} snap_ctx_t;

static VOID __snap_cb(const CAM_FRAME_T *f, VOID *ctx)
{
    snap_ctx_t *s = (snap_ctx_t *)ctx;
    if (!f || !s || s->data) {
        return;  /* already captured; ignore later frames */
    }
    BYTE_T *buf = (BYTE_T *)tal_psram_malloc(f->length);
    if (!buf) {
        TAL_PR_ERR("snapshot psram_malloc failed, len=%u", f->length);
        return;
    }
    memcpy(buf, f->data, f->length);
    s->data = buf;
    s->len  = f->length;
    tal_semaphore_post(s->sem);
}

OPERATE_RET tuya_ai_toy_camera_snapshot(CAM_SNAP_FMT_E fmt,
                                         BYTE_T **out_data,
                                         UINT_T *out_len,
                                         UINT_T timeout_ms)
{
    if (fmt != CAM_SNAP_JPEG || !out_data || !out_len) {
        return OPRT_INVALID_PARM;
    }
    *out_data = NULL;
    *out_len  = 0;

    snap_ctx_t s = {0};
    OPERATE_RET rt = tal_semaphore_create_init(&s.sem, 0, 1);
    if (OPRT_OK != rt) {
        return rt;
    }

    rt = tuya_ai_toy_camera_subscribe(CAM_STREAM_MJPEG, CAM_CONSUMER_SNAPSHOT,
                                       __snap_cb, &s);
    if (OPRT_OK != rt) {
        tal_semaphore_release(s.sem);
        return rt;
    }

    rt = tal_semaphore_wait(s.sem, timeout_ms);
    tuya_ai_toy_camera_unsubscribe(CAM_STREAM_MJPEG, CAM_CONSUMER_SNAPSHOT);

    if (OPRT_OK != rt) {
        if (s.data) {
            tal_psram_free(s.data);
        }
        tal_semaphore_release(s.sem);
        TAL_PR_ERR("snapshot timeout (%u ms)", timeout_ms);
        return OPRT_TIMEOUT;
    }

    *out_data = s.data;
    *out_len  = s.len;
    tal_semaphore_release(s.sem);
    return OPRT_OK;
}

/* ================================================================== */
/*  Output mode                                                        */
/* ================================================================== */

OPERATE_RET tuya_ai_toy_camera_switch_mode(CAM_OUTPUT_MODE_E mode)
{
    return tuya_device_camera_switch_output_mode(mode);
}
