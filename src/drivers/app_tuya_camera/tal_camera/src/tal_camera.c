/**
 * @file tal_camera.c
 * @brief Unified camera stream management layer.
 *
 * @author linch
 * @date 2025-02-09
 * @version 1.0.0
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All rights reserved.
 */

#include "tal_camera.h"
#include "tal_log.h"
#include "tal_memory.h"

#include "tal_dvp.h"
#include "tal_uvc.h"

/* ------------------------------------------------------------------ */
/*  DVP sensor — defined in tdd_camera_driver                          */
/* ------------------------------------------------------------------ */
extern TUYA_DVP_SENSOR_CFG_T dvp_sensor_gc2145_cfg;

/* ------------------------------------------------------------------ */
/*  Internal per-stream state                                           */
/* ------------------------------------------------------------------ */
typedef struct {
    tal_camera_frame_cb_t  cb;
    void                  *args;
    BOOL_T                 running;
} stream_slot_t;

/* ------------------------------------------------------------------ */
/*  Camera handle                                                       */
/* ------------------------------------------------------------------ */
typedef struct {
    TAL_CAMERA_TYPE_E   type;
    void               *drv;               /* opaque driver handle */
    UINT32_T            valid_fmts;        /* bitmask: BIT(TAL_STREAM_*) */
    stream_slot_t       slot[TAL_STREAM_MAX];
} tal_camera_ctx_t;

/* ------------------------------------------------------------------ */
/*  valid_fmts bitmask from DVP output_mode                            */
/* ------------------------------------------------------------------ */
static void __mark_valid_fmts(tal_camera_ctx_t *ctx,
                               TUYA_CAMERA_OUTPUT_MODE mode)
{
    ctx->valid_fmts = 0;
    switch (mode) {
    case TUYA_CAMERA_OUTPUT_YUV422:
        ctx->valid_fmts = BIT(TAL_STREAM_YUV422);
        break;
    case TUYA_CAMERA_OUTPUT_JPEG:
        ctx->valid_fmts = BIT(TAL_STREAM_MJPEG);
        break;
    case TUYA_CAMERA_OUTPUT_H264:
        ctx->valid_fmts = BIT(TAL_STREAM_H264);
        break;
    case TUYA_CAMERA_OUTPUT_JPEG_YUV422_BOTH:
        ctx->valid_fmts = BIT(TAL_STREAM_YUV422) | BIT(TAL_STREAM_MJPEG);
        break;
    case TUYA_CAMERA_OUTPUT_H264_YUV422_BOTH:
        ctx->valid_fmts = BIT(TAL_STREAM_YUV422) | BIT(TAL_STREAM_H264);
        break;
    default:
        TAL_PR_WARN("unknown output_mode %d, no streams marked", mode);
        break;
    }
}

/* ------------------------------------------------------------------ */
/*  Stream-type ↔ frame-format mapping                                 */
/* ------------------------------------------------------------------ */
static TAL_STREAM_TYPE_E __fmt_to_stream(TUYA_FRAME_FMT_E fmt)
{
    switch (fmt) {
    case TUYA_FRAME_FMT_YUV422: return TAL_STREAM_YUV422;
    case TUYA_FRAME_FMT_JPEG:   return TAL_STREAM_MJPEG;
    case TUYA_FRAME_FMT_H264:   return TAL_STREAM_H264;
    default:                    return TAL_STREAM_MAX;
    }
}

/* ------------------------------------------------------------------ */
/*  Internal frame dispatch                                             */
/* ------------------------------------------------------------------ */
static void __dispatch(tal_camera_ctx_t *ctx, TAL_CAMERA_FRAME_T *frame)
{
    TAL_STREAM_TYPE_E s = __fmt_to_stream(frame->fmt);
    if (s >= TAL_STREAM_MAX) {
        return;
    }

    stream_slot_t *slot = &ctx->slot[s];
    if (!slot->running || !slot->cb) {
        return;
    }

    slot->cb((TAL_CAMERA_HANDLE_T)ctx, frame, slot->args);
}

/* ------------------------------------------------------------------ */
/*  DVP frame callback — user_data carries the ctx pointer             */
/* ------------------------------------------------------------------ */
static void __dvp_frame_cb(TUYA_DVP_FRAME_MANAGE_T *dvp_frame, void *user_data)
{
    tal_camera_ctx_t *ctx = (tal_camera_ctx_t *)user_data;
    if (!ctx || !dvp_frame || !dvp_frame->is_frame_complete) {
        return;
    }

    TAL_CAMERA_FRAME_T frame = {
        .data       = dvp_frame->data,
        .length     = dvp_frame->data_len,
        .fmt        = dvp_frame->frame_fmt,
        .width      = dvp_frame->width,
        .height     = dvp_frame->height,
        .timestamp  = 0,
        .is_i_frame = dvp_frame->is_i_frame,
    };

    __dispatch(ctx, &frame);
}

/* ------------------------------------------------------------------ */
/*  UVC frame callback                                                  */
/* ------------------------------------------------------------------ */
static void __uvc_frame_cb(TAL_UVC_HANDLE_T handle,
                           TAL_UVC_FRAME_T *uvc_frame, void *args)
{
    tal_camera_ctx_t *ctx = (tal_camera_ctx_t *)args;
    if (!ctx || !uvc_frame) {
        return;
    }

    TAL_CAMERA_FRAME_T frame = {
        .data       = uvc_frame->frame,
        .length     = uvc_frame->length,
        .fmt        = uvc_frame->fmt,
        .width      = uvc_frame->width,
        .height     = uvc_frame->height,
        .timestamp  = uvc_frame->timestamp,
        .is_i_frame = FALSE,
    };

    __dispatch(ctx, &frame);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

TAL_CAMERA_HANDLE_T tal_camera_init(TAL_CAMERA_CFG_T *cfg)
{
    if (!cfg || !cfg->cfg) {
        TAL_PR_ERR("tal_camera_init: invalid cfg");
        return NULL;
    }
    if (cfg->type >= TAL_CAMERA_TYPE_MAX) {
        TAL_PR_ERR("tal_camera_init: unknown camera type %d", cfg->type);
        return NULL;
    }

    tal_camera_ctx_t *ctx = tal_malloc(sizeof(tal_camera_ctx_t));
    if (!ctx) {
        TAL_PR_ERR("tal_camera_init: malloc failed");
        return NULL;
    }
    memset(ctx, 0, sizeof(tal_camera_ctx_t));
    ctx->type = cfg->type;

    if (cfg->type == TAL_CAMERA_TYPE_DVP) {
        TUYA_DVP_USR_CFG_T *dvp_cfg = (TUYA_DVP_USR_CFG_T *)cfg->cfg;

        dvp_cfg->dvp_frame_handle = __dvp_frame_cb;
        dvp_cfg->frame_user_data  = ctx;

        __mark_valid_fmts(ctx, dvp_cfg->dvp_cfg.output_mode);

        TUYA_DVP_DEVICE_T *dev = tal_dvp_init(&dvp_sensor_gc2145_cfg, dvp_cfg);
        if (!dev) {
            TAL_PR_ERR("tal_dvp_init failed");
            goto err;
        }
        ctx->drv = dev;

        /* DVP hardware is ready but stream is off by default.
         * Call tal_camera_start_stream() to begin output. */
        OPERATE_RET rt = tal_dvp_stop(dev);
        if (rt != OPRT_OK) {
            TAL_PR_ERR("tal_dvp_stop (default-off) failed: %d", rt);
            goto err;
        }
        TAL_PR_DEBUG("DVP init ok (stream off), output_mode=%d, valid_fmts=0x%x",
                     dvp_cfg->dvp_cfg.output_mode, ctx->valid_fmts);

    } else if (cfg->type == TAL_CAMERA_TYPE_UVC) {
        TAL_UVC_CFG_T *uvc_cfg = (TAL_UVC_CFG_T *)cfg->cfg;

        uvc_cfg->frame_cb = __uvc_frame_cb;
        uvc_cfg->args     = ctx;

        /* Reuse __mark_valid_fmts — UVC uses the same TUYA_CAMERA_OUTPUT_MODE */
        __mark_valid_fmts(ctx, uvc_cfg->output_mode);

        TAL_UVC_HANDLE_T uvc_h = NULL;
        OPERATE_RET rt = tal_uvc_init(&uvc_h, uvc_cfg);
        if (rt != OPRT_OK) {
            TAL_PR_ERR("tal_uvc_init failed: %d", rt);
            goto err;
        }
        ctx->drv = uvc_h;
        TAL_PR_DEBUG("UVC init ok");
    }

    TAL_PR_DEBUG("tal_camera_init ok, type=%d", cfg->type);
    return (TAL_CAMERA_HANDLE_T)ctx;

err:
    tal_free(ctx);
    return NULL;
}

OPERATE_RET tal_camera_deinit(TAL_CAMERA_HANDLE_T handle)
{
    tal_camera_ctx_t *ctx = (tal_camera_ctx_t *)handle;
    if (!ctx) {
        return OPRT_INVALID_PARM;
    }
    if (!ctx->drv) {
        TAL_PR_ERR("tal_camera_deinit: drv is NULL");
        return OPRT_COM_ERROR;
    }

    memset(ctx->slot, 0, sizeof(ctx->slot));

    if (ctx->type == TAL_CAMERA_TYPE_DVP) {
        tal_dvp_stop((TUYA_DVP_DEVICE_T *)ctx->drv);
        tal_dvp_deinit((TUYA_DVP_DEVICE_T *)ctx->drv);
    } else if (ctx->type == TAL_CAMERA_TYPE_UVC) {
        tal_uvc_stop((TAL_UVC_HANDLE_T)ctx->drv);
        tal_uvc_deinit((TAL_UVC_HANDLE_T)ctx->drv);
    }
    ctx->drv = NULL;

    tal_free(ctx);
    TAL_PR_DEBUG("tal_camera_deinit ok");
    return OPRT_OK;
}

OPERATE_RET tal_camera_register_cb(TAL_CAMERA_HANDLE_T   handle,
                                   TAL_STREAM_TYPE_E     stream,
                                   tal_camera_frame_cb_t cb,
                                   void                 *args)
{
    tal_camera_ctx_t *ctx = (tal_camera_ctx_t *)handle;
    if (!ctx || stream >= TAL_STREAM_MAX) {
        return OPRT_INVALID_PARM;
    }

    ctx->slot[stream].cb   = cb;
    ctx->slot[stream].args = args;
    TAL_PR_DEBUG("stream %d cb registered (valid_fmts=0x%x, active=%d)",
                 stream, ctx->valid_fmts, !!(ctx->valid_fmts & BIT(stream)));
    return OPRT_OK;
}

OPERATE_RET tal_camera_start_stream(TAL_CAMERA_HANDLE_T handle,
                                    TAL_STREAM_TYPE_E   stream)
{
    tal_camera_ctx_t *ctx = (tal_camera_ctx_t *)handle;
    if (!ctx || stream >= TAL_STREAM_MAX) {
        return OPRT_INVALID_PARM;
    }
    if (!(ctx->valid_fmts & BIT(stream))) {
        return OPRT_NOT_SUPPORTED;
    }

    BOOL_T was_any_running = FALSE;
    for (int i = 0; i < TAL_STREAM_MAX; i++) {
        if (ctx->slot[i].running) { was_any_running = TRUE; break; }
    }
    ctx->slot[stream].running = TRUE;

    if (was_any_running) {
        TAL_PR_DEBUG("stream %d started (driver already running)", stream);
        return OPRT_OK;
    }

    OPERATE_RET rt = OPRT_OK;
    if (ctx->type == TAL_CAMERA_TYPE_DVP) {
        rt = tal_dvp_start((TUYA_DVP_DEVICE_T *)ctx->drv);
        if (rt != OPRT_OK) {
            TAL_PR_ERR("tal_dvp_start failed: %d", rt);
            ctx->slot[stream].running = FALSE;
            return rt;
        }
        TAL_PR_DEBUG("DVP started");
    } else if (ctx->type == TAL_CAMERA_TYPE_UVC) {
        rt = tal_uvc_start((TAL_UVC_HANDLE_T)ctx->drv);
        if (rt != OPRT_OK) {
            TAL_PR_ERR("tal_uvc_start failed: %d", rt);
            ctx->slot[stream].running = FALSE;
            return rt;
        }
        TAL_PR_DEBUG("UVC started");
    }

    TAL_PR_DEBUG("stream %d started", stream);
    return OPRT_OK;
}

OPERATE_RET tal_camera_stop_stream(TAL_CAMERA_HANDLE_T handle,
                                   TAL_STREAM_TYPE_E   stream)
{
    tal_camera_ctx_t *ctx = (tal_camera_ctx_t *)handle;
    if (!ctx || stream >= TAL_STREAM_MAX) {
        return OPRT_INVALID_PARM;
    }

    if (!ctx->slot[stream].running) {
        TAL_PR_DEBUG("stream %d already stopped, skip", stream);
        return OPRT_OK;
    }

    ctx->slot[stream].running = FALSE;

    BOOL_T any_running = FALSE;
    for (int i = 0; i < TAL_STREAM_MAX; i++) {
        if (ctx->slot[i].running) { any_running = TRUE; break; }
    }

    if (any_running) {
        TAL_PR_DEBUG("stream %d stopped (other streams still running)", stream);
        return OPRT_OK;
    }

    if (!ctx->drv) {
        TAL_PR_ERR("tal_camera_stop_stream: drv is NULL");
        return OPRT_COM_ERROR;
    }

    if (ctx->type == TAL_CAMERA_TYPE_DVP) {
        tal_dvp_stop((TUYA_DVP_DEVICE_T *)ctx->drv);
        TAL_PR_DEBUG("DVP stopped");
    } else if (ctx->type == TAL_CAMERA_TYPE_UVC) {
        tal_uvc_stop((TAL_UVC_HANDLE_T)ctx->drv);
        TAL_PR_DEBUG("UVC stopped");
    }

    TAL_PR_DEBUG("stream %d stopped", stream);
    return OPRT_OK;
}

OPERATE_RET tal_camera_switch_output_mode(TAL_CAMERA_HANDLE_T     handle,
                                          TAL_CAMERA_CFG_T       *cfg,
                                          TUYA_CAMERA_OUTPUT_MODE mode)
{
    tal_camera_ctx_t *ctx = (tal_camera_ctx_t *)handle;
    if (!ctx || !cfg || !cfg->cfg) {
        return OPRT_INVALID_PARM;
    }
    if (ctx->type != TAL_CAMERA_TYPE_DVP) {
        TAL_PR_WARN("switch_output_mode is DVP-only");
        return OPRT_NOT_SUPPORTED;
    }
    if (!ctx->drv) {
        TAL_PR_ERR("tal_camera_switch_output_mode: drv is NULL");
        return OPRT_COM_ERROR;
    }

    tal_dvp_stop((TUYA_DVP_DEVICE_T *)ctx->drv);
    tal_dvp_deinit((TUYA_DVP_DEVICE_T *)ctx->drv);
    ctx->drv = NULL;

    TUYA_DVP_USR_CFG_T *dvp_cfg = (TUYA_DVP_USR_CFG_T *)cfg->cfg;
    dvp_cfg->dvp_cfg.output_mode = mode;
    dvp_cfg->dvp_frame_handle    = __dvp_frame_cb;
    dvp_cfg->frame_user_data     = ctx;

    __mark_valid_fmts(ctx, mode);

    TUYA_DVP_DEVICE_T *dev = tal_dvp_init(&dvp_sensor_gc2145_cfg, dvp_cfg);
    if (!dev) {
        TAL_PR_ERR("switch_output_mode: DVP reinit failed");
        return OPRT_COM_ERROR;
    }
    ctx->drv = dev;

    TAL_PR_DEBUG("switch_output_mode ok, mode=%d, valid_fmts=0x%x", mode, ctx->valid_fmts);
    return OPRT_OK;
}
