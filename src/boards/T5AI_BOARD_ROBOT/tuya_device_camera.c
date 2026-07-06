#include "tuya_device_cfg.h"
#include "tal_camera.h"
#include "tal_lcd_service.h"
#include "tal_log.h"
#include "tal_semaphore.h"
#include "tal_mutex.h"
#include "tal_memory.h"

#if defined(ENABLE_TUYA_UI) && ENABLE_TUYA_UI == 1
#include "tuya_ai_display.h"
#endif

#if defined(ENABLE_MQTT_P2P) && (ENABLE_MQTT_P2P == 1)
#include "tkl_audio.h"
#include "tuya_ipc_media.h"
#include "tuya_ipc_media_stream.h"
#endif

/***********************************************************
************************macro define************************
***********************************************************/
typedef struct {
    TAL_CAMERA_HANDLE_T  handle;
    TAL_CAMERA_TYPE_E    type;

    /* JPEG capture sync */
    BYTE_T              *jpeg_data;
    UINT_T               jpeg_len;
    BOOL_T               need_capture;
    SEM_HANDLE           jpeg_sem;
    MUTEX_HANDLE         jpeg_mtx;
} ai_camera_ctx_t;

static ai_camera_ctx_t s_ctx = {
    .handle       = NULL,
    .type         = TAL_CAMERA_TYPE_MAX,
    .jpeg_data    = NULL,
    .jpeg_len     = 0,
    .need_capture = FALSE,
    .jpeg_sem     = NULL,
    .jpeg_mtx     = NULL,
};


/***********************************************************
************************function define************************
***********************************************************/

OPERATE_RET tuya_device_camera_get_jpeg_frame(BYTE_T **data, UINT_T *len, VOID *user_data)
{
    if (!data || !len) {
        return OPRT_INVALID_PARM;
    }
    if (!s_ctx.handle || !s_ctx.jpeg_mtx || !s_ctx.jpeg_sem) {
        TAL_PR_ERR("camera not initialized");
        return OPRT_COM_ERROR;
    }

    /* Ensure MJPEG stream is running */
    tal_camera_start_stream(s_ctx.handle, TAL_STREAM_MJPEG);

    /* Arm capture flag */
    tal_mutex_lock(s_ctx.jpeg_mtx);
    s_ctx.need_capture = TRUE;
    tal_mutex_unlock(s_ctx.jpeg_mtx);

    TAL_PR_DEBUG("waiting for JPEG frame...");

    /* Wait up to 3 s */
    OPERATE_RET rt = tal_semaphore_wait(s_ctx.jpeg_sem, 3000);
    if (rt != OPRT_OK) {
        tal_mutex_lock(s_ctx.jpeg_mtx);
        s_ctx.need_capture = FALSE;
        tal_mutex_unlock(s_ctx.jpeg_mtx);
        TAL_PR_ERR("JPEG capture timeout");
        return OPRT_COM_ERROR;
    }

    /* Hand off buffer to caller */
    tal_mutex_lock(s_ctx.jpeg_mtx);
    if (!s_ctx.jpeg_data || s_ctx.jpeg_len == 0) {
        tal_mutex_unlock(s_ctx.jpeg_mtx);
        TAL_PR_ERR("no valid JPEG data");
        return OPRT_COM_ERROR;
    }

    *data           = s_ctx.jpeg_data;
    *len            = s_ctx.jpeg_len;
    s_ctx.jpeg_data = NULL;
    s_ctx.jpeg_len  = 0;
    tal_mutex_unlock(s_ctx.jpeg_mtx);

    TAL_PR_DEBUG("JPEG frame ok, len=%u", *len);
    return OPRT_OK;
}

OPERATE_RET tuya_device_camera_h264_start()
{
    return tal_camera_start_stream(s_ctx.handle, TAL_STREAM_H264);
}

OPERATE_RET tuya_device_camera_h264_stop()
{
    return tal_camera_stop_stream(s_ctx.handle, TAL_STREAM_H264);
}

OPERATE_RET tuya_device_camera_switch_to_h264_mode(VOID)
{
    if (!s_ctx.handle || s_ctx.type != TAL_CAMERA_TYPE_DVP) {
        return OPRT_NOT_SUPPORTED;
    }
    TAL_CAMERA_CFG_T cfg = {0};
    if (tuya_board_get_camera_cfg(&cfg) != OPRT_OK) {
        return OPRT_COM_ERROR;
    }
    return tal_camera_switch_output_mode(s_ctx.handle, &cfg,
                                         TUYA_CAMERA_OUTPUT_H264_YUV422_BOTH);
}

OPERATE_RET tuya_device_camera_switch_to_jpeg_mode(VOID)
{
    if (!s_ctx.handle || s_ctx.type != TAL_CAMERA_TYPE_DVP) {
        return OPRT_NOT_SUPPORTED;
    }
    TAL_CAMERA_CFG_T cfg = {0};
    if (tuya_board_get_camera_cfg(&cfg) != OPRT_OK) {
        return OPRT_COM_ERROR;
    }
    return tal_camera_switch_output_mode(s_ctx.handle, &cfg,
                                         TUYA_CAMERA_OUTPUT_JPEG_YUV422_BOTH);
}

OPERATE_RET tuya_device_camera_start(VOID)
{
    if (!s_ctx.handle) {
        TAL_PR_ERR("camera not initialized");
        return OPRT_COM_ERROR;
    }

#if defined(ENABLE_TUYA_UI) && ENABLE_TUYA_UI == 1
    tuya_ai_display_pause();
#endif

    /* Claim DMA2D for conversion (UI has released it above) */
    tal_lcd_service_dma2d_init();

    /* Start display stream */
    tal_camera_start_stream(s_ctx.handle, TAL_STREAM_YUV422);
    tal_camera_start_stream(s_ctx.handle, TAL_STREAM_H264);
    tal_camera_start_stream(s_ctx.handle, TAL_STREAM_MJPEG);

    TAL_PR_DEBUG("camera start ok");
    return OPRT_OK;
}

OPERATE_RET tuya_device_camera_stop(VOID)
{
    if (!s_ctx.handle) {
        return OPRT_COM_ERROR;
    }

    tal_camera_stop_stream(s_ctx.handle, TAL_STREAM_YUV422);
    tal_camera_stop_stream(s_ctx.handle, TAL_STREAM_MJPEG);
    tal_camera_stop_stream(s_ctx.handle, TAL_STREAM_H264);

    /* Release DMA2D so the UI layer can reclaim it */
    tal_lcd_service_dma2d_deinit();

#if defined(ENABLE_TUYA_UI) && ENABLE_TUYA_UI == 1
    tuya_ai_display_resume();
#endif

    TAL_PR_DEBUG("camera stop ok");
    return OPRT_OK;
}

static void __on_camera_lcd_display(TAL_CAMERA_FRAME_T *frame)
{
#if defined(ENABLE_TUYA_UI) && ENABLE_TUYA_UI == 1
    tuya_lcd_pbuf_node_t *rgb_pbuf = NULL;
    OPERATE_RET ret = OPRT_COM_ERROR;

    if (s_ctx.type == TAL_CAMERA_TYPE_DVP &&
        frame->fmt  == TUYA_FRAME_FMT_YUV422) {
        ret = tal_lcd_service_yuv2rgb(frame->data, &rgb_pbuf);
    } else if (s_ctx.type == TAL_CAMERA_TYPE_UVC &&
               frame->fmt  == TUYA_FRAME_FMT_JPEG) {
        ret = tal_lcd_service_mjpeg2rgb(frame->data, frame->length,
                                        &rgb_pbuf);
    }

    if (ret != OPRT_OK || !rgb_pbuf) {
        return;
    }

    tuya_ai_display_flush(&rgb_pbuf->frame);
#endif
}

/**
 * DVP-only: YUV422 → LCD display (fast path via DMA2D).
 */
static void __on_yuv_frame(TAL_CAMERA_HANDLE_T handle, TAL_CAMERA_FRAME_T *frame, void *args)
{
    __on_camera_lcd_display(frame);
    /* frame auto-released by tal_camera after this cb returns */
}

/**
 * DVP + UVC: MJPEG frame handler.
 *   - Capture JPEG for AI photo if requested
 *   - UVC only: also push to LCD (DVP has a dedicated YUV422 display path)
 */
static void __on_mjpeg_frame(TAL_CAMERA_HANDLE_T handle, TAL_CAMERA_FRAME_T *frame, void *args)
{
    /* JPEG capture for AI upload */
    if (s_ctx.jpeg_mtx && s_ctx.need_capture) {
            tal_mutex_lock(s_ctx.jpeg_mtx);
            s_ctx.need_capture = FALSE;
        if (s_ctx.jpeg_data) {
            tal_psram_free(s_ctx.jpeg_data);
            s_ctx.jpeg_data = NULL;
        }
            s_ctx.jpeg_data = tal_psram_malloc(frame->length);
        if (s_ctx.jpeg_data) {
            memcpy(s_ctx.jpeg_data, frame->data, frame->length);
            s_ctx.jpeg_len = frame->length;
        if (s_ctx.jpeg_sem) {
            tal_semaphore_post(s_ctx.jpeg_sem);
        }
            TAL_PR_DEBUG("JPEG captured, len=%u", frame->length);
        } else {
            TAL_PR_ERR("malloc failed for JPEG frame");
            s_ctx.jpeg_len = 0;
        }
        tal_mutex_unlock(s_ctx.jpeg_mtx);
    }

    /* LCD display: UVC has no YUV422 stream, so MJPEG doubles as display source.
    DVP skips here because on_yuv_frame handles display. */
    __on_camera_lcd_display(frame);
    /* frame auto-released by tal_camera */
}

/**
* DVP-only: H264 → P2P push.
*/
static void __on_h264_frame(TAL_CAMERA_HANDLE_T handle, TAL_CAMERA_FRAME_T *frame, void *args)
{
#if defined(ENABLE_MQTT_P2P) && (ENABLE_MQTT_P2P == 1)
    if (tuya_ipc_get_client_online_num() <= 0) {
        return;
    }
    MEDIA_FRAME_T mf = {
        .type      = frame->is_i_frame ? E_VIDEO_I_FRAME : E_VIDEO_PB_FRAME,
        .p_buf     = frame->data,
        .size      = frame->length,
        .pts       = tuya_p2p_misc_get_current_time_ms(),
        .timestamp = tuya_p2p_misc_get_current_time_ms(),
    };
    int rt = tuya_p2p_sdk_put_video_frame(&mf);
    TAL_PR_TRACE("H264 frame size=%u rt=%d", frame->length, rt);
#endif
/* frame auto-released by tal_camera */
}

OPERATE_RET tuya_device_camera_init(VOID)
{
    TAL_PR_INFO("tuya device camera init");
    OPERATE_RET rt = OPRT_OK;

    TAL_CAMERA_CFG_T cfg = {0};
    rt = tuya_board_get_camera_cfg(&cfg);
    if (rt != OPRT_OK) {
        TAL_PR_WARN("No camera on this board, skip init");
        return rt;
    }

    s_ctx.type = cfg.type;

    /* LCD service init (pbuf pool + hardware accelerators) */
#if defined(ENABLE_TUYA_UI) && ENABLE_TUYA_UI == 1
    BOOL_T byte_swap = (strncmp(TUYA_LCD_IC_NAME, "spi_", 4) == 0);
    TAL_LCD_DMA2D_MODE_E lcd_mode = (cfg.type == TAL_CAMERA_TYPE_UVC)
                                    ? TAL_LCD_DMA2D_MODE_JPEG
                                    : TAL_LCD_DMA2D_MODE_YUV;
    rt = tal_lcd_service_init(TUYA_AI_TOY_ISP_WIDTH, TUYA_AI_TOY_ISP_HEIGHT,
                              TUYA_LCD_WIDTH, TUYA_LCD_HEIGHT,
                              3, lcd_mode, byte_swap);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("lcd_service init failed: %d", rt);
        return rt;
    }
#endif

    /* Camera init — tal_camera writes cb fields into cfg->cfg internally */
    s_ctx.handle = tal_camera_init(&cfg);
    if (!s_ctx.handle) {
        TAL_PR_ERR("tal_camera_init failed");
        return OPRT_COM_ERROR;
    }

    /* Register callbacks — MJPEG always; YUV422/H264 for DVP only */
    tal_camera_register_cb(s_ctx.handle, TAL_STREAM_MJPEG,  __on_mjpeg_frame, NULL);
    tal_camera_register_cb(s_ctx.handle, TAL_STREAM_YUV422, __on_yuv_frame,   NULL);
    tal_camera_register_cb(s_ctx.handle, TAL_STREAM_H264,   __on_h264_frame,  NULL);

    /* JPEG capture sync primitives */
    if (!s_ctx.jpeg_mtx) {
        tal_mutex_create_init(&s_ctx.jpeg_mtx);
    }
    if (!s_ctx.jpeg_sem) {
        tal_semaphore_create_init(&s_ctx.jpeg_sem, 0, 1);
    }

    TAL_PR_DEBUG("camera init ok, type=%d", s_ctx.type);
    return OPRT_OK;
}

OPERATE_RET tuya_device_camera_deinit(VOID)
{
    TAL_PR_INFO("tuya device camera deinit");
    if (s_ctx.handle) {
        tal_camera_deinit(s_ctx.handle);
        s_ctx.handle = NULL;
    }

#if defined(ENABLE_TUYA_UI) && ENABLE_TUYA_UI == 1
    tal_lcd_service_deinit();
#endif

    if (s_ctx.jpeg_mtx) {
        tal_mutex_lock(s_ctx.jpeg_mtx);
        if (s_ctx.jpeg_data) {
            tal_free(s_ctx.jpeg_data);
            s_ctx.jpeg_data = NULL;
        }
        s_ctx.jpeg_len     = 0;
        s_ctx.need_capture = FALSE;
        tal_mutex_unlock(s_ctx.jpeg_mtx);
        tal_mutex_release(s_ctx.jpeg_mtx);
        s_ctx.jpeg_mtx = NULL;
    }

    if (s_ctx.jpeg_sem) {
        tal_semaphore_release(s_ctx.jpeg_sem);
        s_ctx.jpeg_sem = NULL;
    }

    s_ctx.type = TAL_CAMERA_TYPE_MAX;
    return OPRT_OK;
}