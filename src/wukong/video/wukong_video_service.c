#include "wukong_video_service.h"
#include "tuya_app_config.h"
#include "tal_log.h"
#include "tal_mutex.h"
#include <string.h>

#if defined(USING_CAMERA_VIDEO_INPUT) && (USING_CAMERA_VIDEO_INPUT == 1)
extern OPERATE_RET video_input_camera_register(VOID);
#endif

typedef struct {
    VIDEO_FRAME_CB cb;
    VOID          *ctx;
    BOOL_T         active;
} video_sub_slot_t;

static video_sub_slot_t s_subs[VIDEO_FMT_MAX][VIDEO_CONSUMER_MAX];
static UINT_T           s_refcnt[VIDEO_FMT_MAX];
static MUTEX_HANDLE     s_mtx       = NULL;
static BOOL_T           s_inited    = FALSE;
static BOOL_T           s_src_ready = FALSE;

/* Dispatcher: copy active callbacks under lock, invoke unlocked so a callback
 * may subscribe/unsubscribe without deadlocking (same pattern as the retired
 * tuya_ai_toy_camera adapter). */
static VOID __dispatch(const VIDEO_FRAME_T *frame, VOID *raw_ctx)
{
    (VOID)raw_ctx;
    if (!frame || frame->fmt >= VIDEO_FMT_MAX || !s_inited) {
        return;
    }

    VIDEO_FRAME_CB cbs[VIDEO_CONSUMER_MAX];
    VOID          *ctxs[VIDEO_CONSUMER_MAX];
    UINT_T         n = 0;

    tal_mutex_lock(s_mtx);
    for (UINT_T i = 0; i < VIDEO_CONSUMER_MAX; i++) {
        if (s_subs[frame->fmt][i].active) {
            cbs[n]  = s_subs[frame->fmt][i].cb;
            ctxs[n] = s_subs[frame->fmt][i].ctx;
            n++;
        }
    }
    tal_mutex_unlock(s_mtx);

    for (UINT_T i = 0; i < n; i++) {
        cbs[i](frame, ctxs[i]);
    }
}

OPERATE_RET wukong_video_init(VOID)
{
    if (s_inited) {
        return OPRT_OK;
    }

    OPERATE_RET rt = tal_mutex_create_init(&s_mtx);
    if (OPRT_OK != rt) {
        TAL_PR_ERR("video service mutex init failed: %d", rt);
        return rt;
    }
    memset(s_subs, 0, sizeof(s_subs));
    memset(s_refcnt, 0, sizeof(s_refcnt));
    s_inited = TRUE;

#if defined(USING_CAMERA_VIDEO_INPUT) && (USING_CAMERA_VIDEO_INPUT == 1)
    video_input_camera_register();
#endif

    const VIDEO_INPUT_OPS_T *ops = wukong_video_input_get();
    if (!ops) {
        TAL_PR_NOTICE("video: no input source configured, running degraded");
        return OPRT_OK;
    }
    if (ops->init && ops->init() != OPRT_OK) {
        TAL_PR_WARN("video: input source init failed, running degraded");
        return OPRT_OK;
    }
    s_src_ready = TRUE;
    return OPRT_OK;
}

BOOL_T wukong_video_available(VIDEO_FMT_E fmt)
{
    const VIDEO_INPUT_OPS_T *ops = wukong_video_input_get();
    VIDEO_INPUT_CAPS_T caps = {0};

    if (!s_src_ready || !ops || fmt >= VIDEO_FMT_MAX) {
        return FALSE;
    }
    if (ops->get_caps(&caps) != OPRT_OK) {
        return FALSE;
    }
    return (caps.fmt_mask & VIDEO_FMT_BIT(fmt)) ? TRUE : FALSE;
}

OPERATE_RET wukong_video_get_caps(VIDEO_INPUT_CAPS_T *caps)
{
    const VIDEO_INPUT_OPS_T *ops = wukong_video_input_get();

    if (!caps) {
        return OPRT_INVALID_PARM;
    }
    if (!s_src_ready || !ops) {
        return OPRT_NOT_SUPPORTED;
    }
    return ops->get_caps(caps);
}

OPERATE_RET wukong_video_subscribe(VIDEO_FMT_E fmt, VIDEO_CONSUMER_E consumer,
                                   VIDEO_FRAME_CB cb, VOID *ctx)
{
    const VIDEO_INPUT_OPS_T *ops = wukong_video_input_get();

    if (fmt >= VIDEO_FMT_MAX || consumer >= VIDEO_CONSUMER_MAX || !cb) {
        return OPRT_INVALID_PARM;
    }
    if (!s_inited || !s_src_ready || !ops) {
        TAL_PR_NOTICE("video subscribe: no input source (fmt=%d consumer=%d)", fmt, consumer);
        return OPRT_NOT_SUPPORTED;
    }

    BOOL_T first = FALSE;
    tal_mutex_lock(s_mtx);
    if (s_subs[fmt][consumer].active) {
        tal_mutex_unlock(s_mtx);
        TAL_PR_ERR("fmt %d consumer %d already subscribed", fmt, consumer);
        return OPRT_COM_ERROR;
    }
    s_subs[fmt][consumer].cb     = cb;
    s_subs[fmt][consumer].ctx    = ctx;
    s_subs[fmt][consumer].active = TRUE;
    first = (s_refcnt[fmt]++ == 0);
    tal_mutex_unlock(s_mtx);

    if (first) {
        OPERATE_RET rt = ops->start(fmt, __dispatch, NULL);
        if (OPRT_OK != rt) {
            tal_mutex_lock(s_mtx);
            s_subs[fmt][consumer].active = FALSE;
            s_subs[fmt][consumer].cb     = NULL;
            s_subs[fmt][consumer].ctx    = NULL;
            if (s_refcnt[fmt] > 0) s_refcnt[fmt]--;
            tal_mutex_unlock(s_mtx);
            return rt;
        }
    }
    return OPRT_OK;
}

OPERATE_RET wukong_video_unsubscribe(VIDEO_FMT_E fmt, VIDEO_CONSUMER_E consumer)
{
    const VIDEO_INPUT_OPS_T *ops = wukong_video_input_get();

    if (fmt >= VIDEO_FMT_MAX || consumer >= VIDEO_CONSUMER_MAX) {
        return OPRT_INVALID_PARM;
    }
    if (!s_inited || !ops) {
        return OPRT_NOT_SUPPORTED;
    }

    BOOL_T last = FALSE;
    tal_mutex_lock(s_mtx);
    if (!s_subs[fmt][consumer].active) {
        tal_mutex_unlock(s_mtx);
        return OPRT_COM_ERROR;
    }
    s_subs[fmt][consumer].active = FALSE;
    s_subs[fmt][consumer].cb     = NULL;
    s_subs[fmt][consumer].ctx    = NULL;
    if (s_refcnt[fmt] > 0) {
        last = (--s_refcnt[fmt] == 0);
    }
    tal_mutex_unlock(s_mtx);

    if (last) {
        ops->stop(fmt);
    }
    return OPRT_OK;
}

OPERATE_RET wukong_video_snapshot(BYTE_T **jpeg, UINT_T *len, UINT_T timeout_ms)
{
    const VIDEO_INPUT_OPS_T *ops = wukong_video_input_get();

    if (!jpeg || !len) {
        return OPRT_INVALID_PARM;
    }
    *jpeg = NULL;
    *len  = 0;
    if (!s_src_ready || !ops || !ops->snapshot) {
        return OPRT_NOT_SUPPORTED;
    }
    return ops->snapshot(jpeg, len, timeout_ms);
}
