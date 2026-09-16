/**
 * @file ui_svc_camera.c
 * @brief Camera data service (see ui_svc_camera.h).
 *
 * Layer: services/ — uses platform (tuya_camera, tal_*) and business
 * (wukong_picture) headers only. It NEVER touches LVGL (RULES §6/§8): preview
 * frames, capture thumbnails and album thumbnails are all marshalled to the UI
 * thread with ui_app_async_call() and handed to the page via callbacks, which
 * are the ones that refresh the canvas.
 *
 * All pixel conversion happens in the camera business layer (tuya_camera.c).
 * The live preview is a continuous stream, so the service keeps only the newest
 * pending frame (drop-old) and posts a single async callback at a time — this
 * bounds the async queue to one item and always shows the latest frame. Album IO
 * runs on WORKQ_SYSTEM so the album session is touched by a single serialised
 * thread (ADR-0002).
 */

#include "ui_svc_camera.h"
#include "tuya_app_config.h"   /* ENABLE_TUYA_CAMERA / ENABLE_IMAGE_ALBUM */

#if defined(ENABLE_TUYA_CAMERA) && (ENABLE_TUYA_CAMERA == 1)

#include "ui_app.h"               /* ui_app_async_call — marshal to UI thread */
#include "tuya_camera.h"
#include "wukong_ai_mode.h"       /* interrupt active chat turn */
#include "tal_workq_service.h"    /* WORKQ_SYSTEM, tal_workq_schedule */
#include "tal_mutex.h"
#include "tal_memory.h"
#include "uni_log.h"
#include <string.h>

#if defined(ENABLE_IMAGE_ALBUM) && (ENABLE_IMAGE_ALBUM == 1)
#include "wukong_picture.h"       /* save / scan album */
#define CAM_SVC_HAVE_ALBUM 1
#endif

/* Thumbnail size (square) for the camera's album-preview widget. */
#define CAM_SVC_THUMB_PX        48
/* Snapshot wait — matches the legacy view path. */
#define CAM_SVC_SNAPSHOT_TMO_MS 3000

/* ---- file-scope state ------------------------------------------------------*/
static ui_svc_camera_preview_cb_t s_preview_cb = NULL;
static ui_svc_camera_thumb_cb_t   s_thumb_cb   = NULL;
static ui_svc_camera_capture_cb_t s_capture_cb = NULL;

/* Newest pending preview frame, shared between the camera thread (producer) and
 * the UI thread (consumer). Guarded by s_frame_mtx; only the newest frame is
 * kept (drop-old) and a single async notify is in flight at a time. */
static MUTEX_HANDLE s_frame_mtx     = NULL;
static uint8_t     *s_frame_rgb     = NULL;
static uint16_t     s_frame_w       = 0;
static uint16_t     s_frame_h       = 0;
static uint32_t     s_frame_len     = 0;
static bool         s_notify_inflight = false;

/* True while the live preview stream is running. Maintained by
 * ui_svc_camera_preview_start/stop and read by the WORKQ workers so a worker
 * that needs to pause the preview (album thumbnail decode — see ADR-0005 fix A)
 * only resumes it when it was actually running, regardless of whether the page
 * has reached on_enter yet. */
static bool         s_preview_active  = false;

/* ---- thumbnail marshal payload --------------------------------------------*/
typedef struct {
    uint8_t  *rgb;     /**< RGB565 thumbnail; NULL when there is nothing to show */
    uint16_t  width;
    uint16_t  height;
    uint32_t  len;
    bool      capture_done; /**< true only for a new snapshot, false for album refresh */
    bool      capture_ok;   /**< snapshot was saved successfully */
} thumb_result_t;

/* ---- preview frame: producer (camera thread) + consumer (UI thread) --------*/

/**
 * @brief UI-thread notifier: deliver the newest pending preview frame.
 * @note Drains exactly the latest frame; the producer posts one notify per
 *       newly-pending frame, drop-old guarantees no backlog.
 */
static void __notify_preview(void *p)
{
    (void)p;

    tal_mutex_lock(s_frame_mtx);
    uint8_t *rgb = s_frame_rgb;
    uint16_t w   = s_frame_w;
    uint16_t h   = s_frame_h;
    uint32_t len = s_frame_len;
    s_frame_rgb       = NULL;
    s_notify_inflight = false;
    tal_mutex_unlock(s_frame_mtx);

    if (rgb == NULL) {
        return;   /* stopped before this notify ran */
    }
    if (s_preview_cb != NULL) {
        s_preview_cb(w, h, rgb, len);          /* takes ownership */
    } else {
        tuya_camera_rgb_free(rgb);      /* page gone — release */
    }
}

/**
 * @brief Preview frame from the adaptation layer (camera thread). Stores the
 *        newest frame (releasing any unconsumed one) and posts a single notify.
 * @note Touches no LVGL; ownership of @p rgb565 moves into s_frame_rgb.
 */
static VOID __preview_frame_cb(USHORT_T width, USHORT_T height,
                               UCHAR_T *rgb565, UINT_T len, VOID *ctx)
{
    (VOID)ctx;
    if (rgb565 == NULL) {
        return;
    }

    bool post = false;
    tal_mutex_lock(s_frame_mtx);
    if (s_frame_rgb != NULL) {
        tuya_camera_rgb_free(s_frame_rgb);   /* drop the unconsumed frame */
    }
    s_frame_rgb = rgb565;
    s_frame_w   = width;
    s_frame_h   = height;
    s_frame_len = len;
    if (!s_notify_inflight) {
        s_notify_inflight = true;
        post = true;
    }
    tal_mutex_unlock(s_frame_mtx);

    if (post) {
        ui_app_async_call(__notify_preview, NULL);
    }
}

/* ---- thumbnail delivery (UI thread) ---------------------------------------*/

static void __notify_thumb(void *p)
{
    thumb_result_t *r = (thumb_result_t *)p;
    if (r == NULL) {
        return;
    }
    /* A failed capture must not erase the last valid album thumbnail. An empty
     * entry-time refresh, on the other hand, intentionally hides it. */
    bool deliver_thumb = !(r->capture_done && r->rgb == NULL);
    if (deliver_thumb && s_thumb_cb != NULL) {
        s_thumb_cb(r->width, r->height, r->rgb, r->len);   /* takes/drops r->rgb */
    } else if (r->rgb != NULL) {
        tuya_camera_rgb_free(r->rgb);               /* page gone — release */
    }
    if (r->capture_done && s_capture_cb != NULL) {
        s_capture_cb(r->capture_ok);
    }
    tal_free(r);
}

static thumb_result_t *__thumb_result_new(void)
{
    thumb_result_t *r = (thumb_result_t *)tal_malloc(sizeof(*r));
    if (r != NULL) {
        memset(r, 0, sizeof(*r));
    }
    return r;
}

static void __notify_capture_alloc_failed(void *p)
{
    (void)p;
    if (s_capture_cb != NULL) {
        s_capture_cb(false);
    }
}

/* ---- capture (WORKQ_SYSTEM worker) ----------------------------------------*/

static void __capture_work(void *data)
{
    (void)data;

    /* Reserve the tiny completion payload before stopping the live stream. If
     * allocation itself fails, still release the page's busy state on the UI
     * thread instead of leaving the shutter disabled indefinitely. */
    thumb_result_t *r = __thumb_result_new();
    if (r == NULL) {
        PR_ERR("camera svc: capture alloc thumb_result failed");
        ui_app_async_call(__notify_capture_alloc_failed, NULL);
        return;
    }
    r->capture_done = true;

    PR_INFO("camera svc: capture work start, preview_cb=%p, thumb_cb=%p",
            s_preview_cb, s_thumb_cb);

    /* Pause the live preview before snapshot so the two large RGB565
     * preview buffers (~460 KB each, in PSRAM) are released, leaving
     * enough headroom for the snapshot JPEG, album save copy, and
     * thumbnail RGB565 to coexist without PSRAM exhaustion. */
    PR_INFO("camera svc: capture stop preview before snapshot");
    ui_svc_camera_preview_stop();

    BYTE_T *jpeg     = NULL;
    UINT_T  jpeg_len = 0;
    PR_INFO("camera svc: snapshot start, timeout=%u ms", (unsigned)CAM_SVC_SNAPSHOT_TMO_MS);
    OPERATE_RET rt = tuya_camera_snapshot(&jpeg, &jpeg_len,
                                          CAM_SVC_SNAPSHOT_TMO_MS);
    PR_INFO("camera svc: snapshot done, rt=%d, jpeg=%p, len=%u",
            rt, jpeg, (unsigned)jpeg_len);
    bool have_jpeg = (rt == OPRT_OK && jpeg != NULL && jpeg_len > 0);
    bool capture_ok = have_jpeg;
    if (!have_jpeg) {
        PR_WARN("camera capture: snapshot failed, rt=%d", rt);
    }

#if defined(CAM_SVC_HAVE_ALBUM)
    if (have_jpeg) {
        char name[WUKONG_PICTURE_NAME_MAX_LEN + 1] = {0};
        OPERATE_RET save_rt = wukong_picture_save_to_album(
            (uint8_t *)jpeg, (uint32_t)jpeg_len, name);
        capture_ok = (save_rt == OPRT_OK);
        PR_INFO("camera svc: album save done, rt=%d, name=%s, jpeg_len=%u",
                save_rt, name, (unsigned)jpeg_len);
    }
#endif

    r->capture_ok = capture_ok;

    if (capture_ok) {
        UCHAR_T *rgb = NULL;
        USHORT_T tw = 0, th = 0;
        if (tuya_camera_jpeg_to_rgb565(jpeg, jpeg_len, CAM_SVC_THUMB_PX,
                                       &rgb, &tw, &th) == OPRT_OK) {
            r->rgb    = rgb;
            r->width  = tw;
            r->height = th;
            r->len    = (uint32_t)tw * th * 2;
            PR_INFO("camera svc: thumb decode ok, w=%u, h=%u, len=%u",
                    (unsigned)tw, (unsigned)th, (unsigned)r->len);
        } else {
            PR_WARN("camera svc: thumb decode failed, jpeg_len=%u", (unsigned)jpeg_len);
        }
    }

    if (jpeg != NULL) {
        tal_psram_free(jpeg);
        PR_INFO("camera svc: jpeg buffer freed");
    }
    ui_app_async_call(__notify_thumb, r);
    PR_INFO("camera svc: thumb notify posted, has_rgb=%d",
            r->rgb != NULL ? 1 : 0);

    if (s_preview_cb != NULL) {
        PR_INFO("camera svc: capture restart preview");
        ui_svc_camera_preview_start();
    } else {
        PR_INFO("camera svc: capture skip preview restart, preview_cb=NULL");
    }
}

/* ---- latest-thumbnail load (WORKQ_SYSTEM worker) --------------------------*/

static void __refresh_thumb_work(void *data)
{
    (void)data;

    thumb_result_t *r = __thumb_result_new();
    if (r == NULL) {
        return;
    }

#if defined(CAM_SVC_HAVE_ALBUM)
    /* Transient scan session: only close it if we were the ones who opened it,
     * so a session already owned elsewhere is left intact. */
    bool     opened = (wukong_picture_open_album() == OPRT_OK);
    uint32_t count  = wukong_picture_get_count();
    if (count > 0 && wukong_picture_seek_to_photo(count) == OPRT_OK) {
        WUKONG_PICTURE_INFO_T pic = {0};
        if (wukong_picture_get_next(&pic) == OPRT_OK && pic.data != NULL && pic.len > 0) {
            /* The JPEG->RGB565 decode (tal_image_jpeg_scale_rgb565) shares the
             * camera image codec with the live preview. Decoding while the
             * preview streams starves the codec and stretches it from ~0.5s to
             * several seconds — and since capture rides the same serial WORKQ,
             * that latency is charged straight to the shutter. Pause the preview
             * just for the decode, then resume it (ADR-0005 fix A).
             *
             * Sample s_preview_active HERE, immediately before the decode — NOT
             * at function entry. This worker is scheduled at the end of the
             * page's on_create and can start a few ms before on_enter brings the
             * preview up; an entry-time sample races that start and reads false,
             * skipping the pause while the preview comes up during the decode and
             * contends anyway. By the time get_next() returns (after open_album +
             * flash read) on_enter has reliably started the preview. */
            bool was_preview = s_preview_active;
            if (was_preview) {
                PR_INFO("camera svc: thumb refresh pausing preview to avoid codec contention");
                ui_svc_camera_preview_stop();
            }
            UCHAR_T *rgb = NULL;
            USHORT_T tw = 0, th = 0;
            OPERATE_RET drt = tuya_camera_jpeg_to_rgb565(pic.data, pic.len,
                                                         CAM_SVC_THUMB_PX,
                                                         &rgb, &tw, &th);
            if (was_preview && s_preview_cb != NULL) {
                PR_INFO("camera svc: thumb refresh resuming preview");
                ui_svc_camera_preview_start();
            }
            if (drt == OPRT_OK) {
                r->rgb    = rgb;
                r->width  = tw;
                r->height = th;
                r->len    = (uint32_t)tw * th * 2;
            } else {
                PR_WARN("camera svc: thumb refresh decode failed, rt=%d, len=%u",
                        drt, (unsigned)pic.len);
            }
        }
        wukong_picture_free_pic_info(&pic);
    }
    if (opened) {
        wukong_picture_close_album();
    }
#endif

    ui_app_async_call(__notify_thumb, r);   /* r->rgb == NULL hides the preview */
}

/* ---- public API ------------------------------------------------------------*/

void ui_svc_camera_init(void)
{
    s_preview_cb      = NULL;
    s_thumb_cb        = NULL;
    s_capture_cb      = NULL;
    s_frame_rgb       = NULL;
    s_frame_w         = 0;
    s_frame_h         = 0;
    s_frame_len       = 0;
    s_notify_inflight = false;
    s_preview_active  = false;
    if (s_frame_mtx == NULL) {
        tal_mutex_create_init(&s_frame_mtx);
    }
}

bool ui_svc_camera_available(void)
{
    return tuya_camera_supported() ? true : false;
}

void ui_svc_camera_set_preview_cb(ui_svc_camera_preview_cb_t cb)
{
    s_preview_cb = cb;
}

void ui_svc_camera_preview_start(void)
{
    PR_INFO("camera svc: preview start request, cb=%p", s_preview_cb);
    /* Start clean: drop any frame left over from a previous session. */
    tal_mutex_lock(s_frame_mtx);
    if (s_frame_rgb != NULL) {
        tuya_camera_rgb_free(s_frame_rgb);
        s_frame_rgb = NULL;
    }
    s_notify_inflight = false;
    tal_mutex_unlock(s_frame_mtx);

    OPERATE_RET rt = tuya_camera_preview_start(__preview_frame_cb, NULL);
    if (rt != OPRT_OK) {
        PR_WARN("camera preview start failed: %d", rt);
    } else {
        s_preview_active = true;
        PR_INFO("camera svc: preview start ok");
    }
}

void ui_svc_camera_preview_stop(void)
{
    PR_INFO("camera svc: preview stop request");
    s_preview_active = false;
    /* Stop the stream first: after this returns no further frames reach
     * __preview_frame_cb, so it is safe to drop the pending frame. A notify that
     * was already posted will run later, see s_frame_rgb == NULL and no-op. */
    tuya_camera_preview_stop();

    tal_mutex_lock(s_frame_mtx);
    if (s_frame_rgb != NULL) {
        tuya_camera_rgb_free(s_frame_rgb);
        s_frame_rgb = NULL;
    }
    tal_mutex_unlock(s_frame_mtx);
    PR_INFO("camera svc: preview stop done");
}

void ui_svc_camera_set_thumb_cb(ui_svc_camera_thumb_cb_t cb)
{
    s_thumb_cb = cb;
}

void ui_svc_camera_set_capture_cb(ui_svc_camera_capture_cb_t cb)
{
    s_capture_cb = cb;
}

void ui_svc_camera_capture(void)
{
    PR_INFO("camera svc: capture scheduled");
    tal_workq_schedule(WORKQ_SYSTEM, __capture_work, NULL);
}

void ui_svc_camera_refresh_thumb(void)
{
    tal_workq_schedule(WORKQ_SYSTEM, __refresh_thumb_work, NULL);
}

void ui_svc_camera_rgb_free(void *buf)
{
    tuya_camera_rgb_free(buf);
}

void ui_svc_camera_interrupt_chat(void)
{
    /* Hand off to the active chat sub-mode so it barges in and resets its own
     * state machine (ADR-0005); non-chat modes have no on_interrupt and are
     * safely skipped. */
    PR_INFO("camera svc: interrupt current chat turn");
    wukong_ai_mode_dispatch(AI_MODE_OP_INTERRUPT, NULL, 0);
}

#else /* camera feature compiled out — inert stubs */

void ui_svc_camera_init(void) {}
bool ui_svc_camera_available(void) { return false; }
void ui_svc_camera_set_preview_cb(ui_svc_camera_preview_cb_t cb) { (void)cb; }
void ui_svc_camera_preview_start(void) {}
void ui_svc_camera_preview_stop(void) {}
void ui_svc_camera_set_thumb_cb(ui_svc_camera_thumb_cb_t cb) { (void)cb; }
void ui_svc_camera_set_capture_cb(ui_svc_camera_capture_cb_t cb) { (void)cb; }
void ui_svc_camera_capture(void) {}
void ui_svc_camera_refresh_thumb(void) {}
void ui_svc_camera_rgb_free(void *buf) { (void)buf; }
void ui_svc_camera_interrupt_chat(void) {}

#endif /* ENABLE_TUYA_CAMERA */
