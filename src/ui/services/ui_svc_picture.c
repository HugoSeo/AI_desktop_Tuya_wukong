/**
 * @file ui_svc_picture.c
 * @brief Picture data service (see ui_svc_picture.h, ADR-0002).
 *
 * Layer: services/ — may use business (wukong_*) and platform (tal_*) headers,
 * MUST NOT touch LVGL. Every wukong_picture_* call runs on WORKQ_SYSTEM so the
 * album scan session is only ever touched by a single (serialised) thread and
 * the UI thread never stalls on decode/IO. Decoded RGB565 lives in PSRAM and is
 * handed to the UI thread via ui_app_async_call.
 */

#include "ui_svc_picture.h"
#include "tuya_app_config.h"   /* ENABLE_IMAGE_ALBUM */

#if defined(ENABLE_IMAGE_ALBUM) && (ENABLE_IMAGE_ALBUM == 1)

#include "ui_app.h"                  /* ui_app_async_call — marshal to UI thread */
#include "ui_state.h"                /* online + device mode synchronization */
#include "tal_workq_service.h"       /* WORKQ_SYSTEM, tal_workq_schedule */
#include "tal_memory.h"              /* tal_malloc/free, tal_psram_malloc/free */
#include "tal_image_jpeg_codec.h"    /* tal_image_jpeg_decode_rgb565 */
#include "tal_image_scale.h"         /* tal_image_jpeg_scale_rgb565 — attachment thumb */
#include "wukong_picture.h"          /* album scan + thumbnails */
#include "wukong_picture_input.h"    /* wukong_picture_input_add_from_album */
#include "tuya_ai_toy.h"             /* current device mode */
#include "wukong_ai_mode.h"          /* mode switch + recognition dispatch */
#include "uni_log.h"
#include <string.h>

/* UI_PICTURE_NAME_MAX must mirror the backend's filename cap so names round-trip. */
typedef char __ui_picture_name_size_check[(UI_PICTURE_NAME_MAX == WUKONG_PICTURE_NAME_MAX_LEN) ? 1 : -1];

/* Thumbnail pixel size requested from the backend (square). The grid cell scales
 * its container via ui_adapt() and centres/clips this fixed-size thumbnail. */
#define UI_PICTURE_THUMB_PX 96

/* Chat attachment preview size (square), mirrors the old view chat bar. */
#define UI_PICTURE_ATTACH_PX 44

/* Large viewer bounding box. Camera photos are square, so the current 480x480
 * JPEG becomes a native 320x320 RGB565 canvas while the saved JPEG stays intact. */
#define UI_PICTURE_VIEW_MAX_PX 320u

/* ---- file-scope state ------------------------------------------------------*/
static ui_picture_count_cb_t    s_on_count   = NULL;
static ui_picture_cb_t    s_on_picture   = NULL;
static ui_picture_thumbs_cb_t   s_on_thumbs  = NULL;
static ui_picture_cb_t    s_on_view    = NULL;
static ui_picture_ai_done_cb_t  s_ai_done = NULL;

/* Current thumbnail pixels referenced by the grid canvases. This list is only
 * swapped/freed on the UI thread, after the page callback has deleted the old
 * canvases, so LVGL never observes freed pixel memory. */
static WUKONG_PICTURE_THUMB_LIST_T s_thumb_list = {0};

/* Pending image-to-image attachment preview (ADR-0003). Written on the worker,
 * cleared on the UI thread (open/take/clear); the prepare->navigate->read
 * sequence is ordered by the done callback so there is no concurrent access. */
static ui_picture_t s_pending = {0};
static bool               s_has_pending = false;

/* ---- request work context --------------------------------------------------*/
typedef struct {
    uint32_t index;
    uint32_t seq;
    char     name[UI_PICTURE_NAME_MAX + 1];
} req_ctx_t;
typedef struct {
    uint32_t count;
    char     names[UI_PICTURE_MAX_THUMBS][UI_PICTURE_NAME_MAX + 1];
} batch_ctx_t;
typedef struct { char name[UI_PICTURE_NAME_MAX + 1]; } view_ctx_t;
typedef struct {
    WUKONG_PICTURE_THUMB_LIST_T list;
    ui_picture_thumb_t          view[UI_PICTURE_MAX_THUMBS];
    uint32_t                    count;
} thumb_result_t;

/* ---- helpers (worker thread) ----------------------------------------------*/

/* Compact a decoded RGB565 image toward the front of its own buffer. The walk
 * is overlap-safe for downscaling because selected source pixels are monotonic
 * and never precede their destination positions. */
static void __downscale_rgb565_in_place(uint8_t *buf,
                                        uint16_t src_w, uint16_t src_h,
                                        uint16_t dst_w, uint16_t dst_h)
{
    if (!buf || src_w == 0 || src_h == 0 || dst_w == 0 || dst_h == 0 ||
        (src_w == dst_w && src_h == dst_h)) {
        return;
    }

    uint16_t *pixels = (uint16_t *)buf;
    uint32_t x_step = ((uint32_t)src_w << 16) / dst_w;
    uint32_t y_step = ((uint32_t)src_h << 16) / dst_h;
    uint32_t y_fp = 0;

    for (uint16_t dy = 0; dy < dst_h; ++dy, y_fp += y_step) {
        uint32_t sy = y_fp >> 16;
        uint16_t *dst = pixels + (uint32_t)dy * dst_w;
        const uint16_t *src = pixels + sy * src_w;
        uint32_t x_fp = 0;
        for (uint16_t dx = 0; dx < dst_w; ++dx, x_fp += x_step) {
            dst[dx] = src[x_fp >> 16];
        }
    }
}

/** Decode a JPEG and fit it into the native 320px large-view canvas. */
static uint8_t *__decode_to_rgb565(const WUKONG_PICTURE_INFO_T *pic,
                                   uint16_t *view_w, uint16_t *view_h)
{
    if (pic->data == NULL || pic->len == 0 || pic->width == 0 || pic->height == 0 ||
        view_w == NULL || view_h == NULL) {
        return NULL;
    }

    uint16_t dst_w = pic->width;
    uint16_t dst_h = pic->height;
    if (dst_w > UI_PICTURE_VIEW_MAX_PX || dst_h > UI_PICTURE_VIEW_MAX_PX) {
        if (dst_w >= dst_h) {
            dst_h = (uint16_t)((uint32_t)dst_h * UI_PICTURE_VIEW_MAX_PX / dst_w);
            dst_w = UI_PICTURE_VIEW_MAX_PX;
        } else {
            dst_w = (uint16_t)((uint32_t)dst_w * UI_PICTURE_VIEW_MAX_PX / dst_h);
            dst_h = UI_PICTURE_VIEW_MAX_PX;
        }
    }

    /* Decode straight to the smallest size that still covers the canvas instead
     * of at native resolution, so the buffer stays proportional to the canvas
     * rather than to the photo. */
    uint16_t dec_w = pic->width;
    uint16_t dec_h = pic->height;
    if (tal_image_jpeg_calc_scaled_size(pic->width, pic->height,
                                        dst_w, dst_h, &dec_w, &dec_h) != OPRT_OK) {
        return NULL;
    }

    uint32_t size = (uint32_t)dec_w * dec_h * 2;
    uint8_t *buf = tal_psram_malloc(size);
    if (buf == NULL) {
        PR_ERR("picture: psram malloc %u failed", size);
        return NULL;
    }

    TAL_IMAGE_JPEG_OUTPUT_T out = {0};
    out.out_buf      = buf;
    out.out_buf_size = size;
    out.out_width    = dec_w;
    out.out_height   = dec_h;

    if (tal_image_jpeg_decode_rgb565(pic->data, pic->len, &out) != OPRT_OK) {
        PR_ERR("picture: jpeg decode failed");
        tal_psram_free(buf);
        return NULL;
    }

    __downscale_rgb565_in_place(buf, dec_w, dec_h, dst_w, dst_h);
    *view_w = dst_w;
    *view_h = dst_h;
    return buf;
}

/* Scale a JPEG to a small RGB565 preview in PSRAM (so the chat attachment frees
 * through the same ui_svc_picture_free_rgb565 path). Returns NULL on failure. */
static uint8_t *__scale_jpeg_to_psram(const uint8_t *jpeg, uint32_t len, uint16_t target,
                                      uint16_t *out_w, uint16_t *out_h)
{
    TAL_IMAGE_JPEG_SCALE_IN_T in = {0};
    in.method     = TAL_IMAGE_SCALE_MTH_BILINEAR;
    in.mode       = TAL_IMAGE_SCALE_MODE_SIZE;
    in.data       = jpeg;
    in.size       = len;
    in.out_width  = target;
    in.out_height = target;

    TAL_IMAGE_SCALE_OUT_T out = {0};
    if (tal_image_jpeg_scale_rgb565(&in, &out) != OPRT_OK || out.buf == NULL) {
        return NULL;
    }

    uint8_t *buf = tal_psram_malloc(out.size);
    if (buf) {
        memcpy(buf, out.buf, out.size);
        *out_w = out.width;
        *out_h = out.height;
    }
    tal_image_scale_buf_free(&out);
    return buf;
}

static void __free_pending(void)
{
    if (s_pending.data) {
        tal_psram_free(s_pending.data);
    }
    memset(&s_pending, 0, sizeof(s_pending));
    s_has_pending = false;
}

/* ---- marshal-back callbacks (UI thread via ui_app_async_call) --------------*/

static void __notify_count(void *p)
{
    uint32_t count = (uint32_t)(uintptr_t)p;
    if (s_on_count) {
        s_on_count(count);
    }
}

static void __notify_picture(void *p)
{
    ui_picture_t *picture = (ui_picture_t *)p;
    if (s_on_picture) {
        /* Receiver takes or drops picture->data (ownership contract). */
        s_on_picture(picture);
    } else if (picture->data) {
        /* No page listening (already destroyed) — release the orphan buffer. */
        tal_psram_free(picture->data);
    }
    tal_free(picture);
}

static void __notify_view(void *p)
{
    ui_picture_t *picture = (ui_picture_t *)p;
    if (s_on_view) {
        /* Receiver takes or drops picture->data (ownership contract). */
        s_on_view(picture);
    } else if (picture->data) {
        /* Chat page gone before the decode landed — release the orphan buffer. */
        tal_psram_free(picture->data);
    }
    tal_free(picture);
}

static void __notify_thumbs(void *p)
{
    thumb_result_t *result = (thumb_result_t *)p;
    if (result == NULL) {
        return;
    }

    if (s_on_thumbs) {
        /* The callback synchronously clears the old canvases and creates new
         * ones. Keep s_thumb_list alive until that transition is complete. */
        s_on_thumbs(result->view, result->count);
        wukong_picture_free_thumb_list(&s_thumb_list);
        s_thumb_list = result->list;
        memset(&result->list, 0, sizeof(result->list));
    } else {
        wukong_picture_free_thumb_list(&result->list);
    }
    tal_free(result);
}

static void __release_thumbs(void *p)
{
    (void)p;
    wukong_picture_free_thumb_list(&s_thumb_list);
    memset(&s_thumb_list, 0, sizeof(s_thumb_list));
}

static void __notify_ai_done(void *p)
{
    bool ok = (bool)(uintptr_t)p;
    ui_picture_ai_done_cb_t cb = s_ai_done;
    s_ai_done = NULL;
    if (cb) {
        cb(ok);
    }
}

/* ---- worker callbacks (WORKQ_SYSTEM) --------------------------------------*/

static void __open_work(void *data)
{
    (void)data;
    wukong_picture_open_album();   /* OPRT_COM_ERROR if already open — harmless */
    uint32_t count = wukong_picture_get_count();
    ui_app_async_call(__notify_count, (void *)(uintptr_t)count);
}

static void __decode_work(void *data)
{
    req_ctx_t *ctx = (req_ctx_t *)data;

    ui_picture_t *picture = (ui_picture_t *)tal_malloc(sizeof(*picture));
    if (picture == NULL) {
        tal_free(ctx);
        return;
    }
    memset(picture, 0, sizeof(*picture));
    picture->index = ctx->index;
    picture->seq   = ctx->seq;

    OPERATE_RET seek_rt;
    if (ctx->name[0] != '\0') {
        seek_rt = wukong_picture_seek_to_name_index(ctx->name, &picture->index);
    } else {
        seek_rt = wukong_picture_seek_to_photo(ctx->index + 1);
    }

    if (seek_rt == OPRT_OK) {
        WUKONG_PICTURE_INFO_T pic = {0};
        if (wukong_picture_get_next(&pic) == OPRT_OK) {
            picture->data = __decode_to_rgb565(&pic,
                                               &picture->width, &picture->height);
        }
        wukong_picture_free_pic_info(&pic);
    }

    tal_free(ctx);
    ui_app_async_call(__notify_picture, picture);
}

static void __view_work(void *data)
{
    view_ctx_t *ctx = (view_ctx_t *)data;

    ui_picture_t *picture = (ui_picture_t *)tal_malloc(sizeof(*picture));
    if (picture == NULL) {
        tal_free(ctx);
        return;
    }
    memset(picture, 0, sizeof(*picture));

    WUKONG_PICTURE_INFO_T pic = {0};
    if (wukong_picture_get_by_name(ctx->name, &pic) == OPRT_OK) {
        picture->data = __decode_to_rgb565(&pic,
                                           &picture->width, &picture->height);
    }
    wukong_picture_free_pic_info(&pic);

    tal_free(ctx);
    ui_app_async_call(__notify_view, picture);
}

static void __delete_work(void *data)
{
    (void)data;
    wukong_picture_delete_current();
    uint32_t count = wukong_picture_get_count();
    ui_app_async_call(__notify_count, (void *)(uintptr_t)count);
}

static void __thumbs_work(void *data)
{
    (void)data;

    thumb_result_t *result = (thumb_result_t *)tal_malloc(sizeof(*result));
    if (result == NULL) {
        return;
    }
    memset(result, 0, sizeof(*result));

    wukong_picture_get_thumb_list(UI_PICTURE_THUMB_PX, UI_PICTURE_THUMB_PX, &result->list);

    uint32_t n = result->list.count;
    if (n > UI_PICTURE_MAX_THUMBS) {
        n = UI_PICTURE_MAX_THUMBS;
    }
    for (uint32_t i = 0; i < n; i++) {
        const WUKONG_PICTURE_THUMB_T *t = &result->list.items[i];
        strncpy(result->view[i].name, t->name, UI_PICTURE_NAME_MAX);
        result->view[i].name[UI_PICTURE_NAME_MAX] = '\0';
        result->view[i].width  = t->width;
        result->view[i].height = t->height;
        result->view[i].data   = t->data;
    }
    result->count = n;

    ui_app_async_call(__notify_thumbs, result);
}

static void __batch_delete_work(void *data)
{
    batch_ctx_t *ctx = (batch_ctx_t *)data;

    const char *ptrs[UI_PICTURE_MAX_THUMBS];
    for (uint32_t i = 0; i < ctx->count; i++) {
        ptrs[i] = ctx->names[i];
    }
    wukong_picture_delete_batch(ptrs, ctx->count);

    uint32_t count = wukong_picture_get_count();
    tal_free(ctx);
    ui_app_async_call(__notify_count, (void *)(uintptr_t)count);
}

/* Switch the business mode first, then mirror it into ui_state immediately.
 * The business layer also posts its normal mode-change notification; the later
 * duplicate state write is harmless and keeps chat's selector correct even when
 * navigation follows this worker callback immediately. */
static bool __switch_device_mode(AI_DEVICE_MODE_E target)
{
    if (tuya_ai_toy_device_mode_get() != target &&
        wukong_ai_device_mode_switch(target) != OPRT_OK) {
        return false;
    }
    ui_state_set_device_mode((uint8_t)target);
    return true;
}

static void __recognize_current_work(void *data)
{
    (void)data;
    bool ok = false;

    if (!ui_state_get_system()->is_online) {
        PR_WARN("picture: recognition skipped while offline");
        ui_app_async_call(__notify_ai_done, NULL);
        return;
    }

    char name[WUKONG_PICTURE_NAME_MAX_LEN + 1] = {0};
    WUKONG_PICTURE_INFO_T pic = {0};
    if (wukong_picture_get_current_name(name) == OPRT_OK &&
        wukong_picture_get_by_name(name, &pic) == OPRT_OK &&
        pic.data != NULL && pic.len > 0 &&
        __switch_device_mode(AI_DEVICE_MODE_CHAT)) {
        ok = (wukong_ai_mode_dispatch(AI_MODE_OP_PICTURE,
                                      (VOID *)pic.data, (INT_T)pic.len) == OPRT_OK);
    }
    wukong_picture_free_pic_info(&pic);
    ui_app_async_call(__notify_ai_done, (void *)(uintptr_t)(ok ? 1 : 0));
}

static void __generate_from_current_work(void *data)
{
    (void)data;
    bool ok = false;

    char name[WUKONG_PICTURE_NAME_MAX_LEN + 1] = {0};
    if (wukong_picture_get_current_name(name) == OPRT_OK) {
        WUKONG_PICTURE_INFO_T pic = {0};
        if (wukong_picture_get_by_name(name, &pic) == OPRT_OK && pic.data != NULL) {
            uint16_t tw = 0, th = 0;
            uint8_t *rgb = __scale_jpeg_to_psram(pic.data, pic.len,
                                                 UI_PICTURE_ATTACH_PX, &tw, &th);
            if (rgb) {
                __free_pending();
                s_pending.data   = rgb;
                s_pending.width  = tw;
                s_pending.height = th;
                s_has_pending    = true;
            }

            OPERATE_RET add_rt = wukong_picture_input_add_from_album(name, NULL);
            if (add_rt == OPRT_OK && __switch_device_mode(AI_DEVICE_MODE_PICTURE)) {
                ok = true;
            } else if (add_rt == OPRT_OK) {
                wukong_picture_input_del_from_album(name);
            }
            if (!ok) {
                __free_pending();
            }
        }
        wukong_picture_free_pic_info(&pic);
    }

    ui_app_async_call(__notify_ai_done, (void *)(uintptr_t)(ok ? 1 : 0));
}

static void __close_work(void *data)
{
    (void)data;
    wukong_picture_close_album();
    /* Thumbnail ownership lives on the UI thread. Queue its release behind any
     * pending thumbnail delivery callbacks to preserve callback ordering. */
    ui_app_async_call(__release_thumbs, NULL);
    /* Pending attachment is intentionally NOT freed here: image-to-image
     * stashes it before leaving the album, then chat consumes it. */
}

/* ---- public API ------------------------------------------------------------*/

void ui_svc_picture_init(void)
{
    memset(&s_thumb_list, 0, sizeof(s_thumb_list));
    memset(&s_pending, 0, sizeof(s_pending));
    s_has_pending = false;
    s_on_count = NULL;
    s_on_picture = NULL;
    s_on_thumbs = NULL;
    s_on_view = NULL;
    s_ai_done = NULL;
}

bool ui_svc_picture_available(void)
{
    return true;
}

void ui_svc_picture_set_cbs(ui_picture_count_cb_t on_count,
                            ui_picture_cb_t on_picture,
                            ui_picture_thumbs_cb_t on_thumbs)
{
    s_on_count  = on_count;
    s_on_picture  = on_picture;
    s_on_thumbs = on_thumbs;
}

void ui_svc_picture_open(void)
{
    __free_pending();   /* fresh session starts with no stale attachment */
    tal_workq_schedule(WORKQ_SYSTEM, __open_work, NULL);
}

void ui_svc_picture_request(uint32_t index, uint32_t seq)
{
    req_ctx_t *ctx = (req_ctx_t *)tal_malloc(sizeof(*ctx));
    if (ctx == NULL) {
        return;
    }
    ctx->index = index;
    ctx->seq   = seq;
    ctx->name[0] = '\0';
    tal_workq_schedule(WORKQ_SYSTEM, __decode_work, ctx);
}

void ui_svc_picture_request_named(const char *name, uint32_t seq)
{
    if (name == NULL || name[0] == '\0') {
        return;
    }
    req_ctx_t *ctx = (req_ctx_t *)tal_malloc(sizeof(*ctx));
    if (ctx == NULL) {
        return;
    }
    ctx->index = 0;
    ctx->seq = seq;
    strncpy(ctx->name, name, UI_PICTURE_NAME_MAX);
    ctx->name[UI_PICTURE_NAME_MAX] = '\0';
    tal_workq_schedule(WORKQ_SYSTEM, __decode_work, ctx);
}

void ui_svc_picture_delete_current(void)
{
    tal_workq_schedule(WORKQ_SYSTEM, __delete_work, NULL);
}

void ui_svc_picture_close(void)
{
    tal_workq_schedule(WORKQ_SYSTEM, __close_work, NULL);
}

void ui_svc_picture_free_rgb565(void *buf)
{
    if (buf) {
        tal_psram_free(buf);
    }
}

void ui_svc_picture_thumbs_request(void)
{
    tal_workq_schedule(WORKQ_SYSTEM, __thumbs_work, NULL);
}

void ui_svc_picture_delete_batch(const char *const names[], uint32_t count)
{
    if (names == NULL || count == 0) {
        return;
    }

    batch_ctx_t *ctx = (batch_ctx_t *)tal_malloc(sizeof(*ctx));
    if (ctx == NULL) {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));

    if (count > UI_PICTURE_MAX_THUMBS) {
        count = UI_PICTURE_MAX_THUMBS;
    }
    for (uint32_t i = 0; i < count; i++) {
        if (names[i]) {
            strncpy(ctx->names[i], names[i], UI_PICTURE_NAME_MAX);
            ctx->names[i][UI_PICTURE_NAME_MAX] = '\0';
        }
    }
    ctx->count = count;

    tal_workq_schedule(WORKQ_SYSTEM, __batch_delete_work, ctx);
}

void ui_svc_picture_set_view_cb(ui_picture_cb_t on_view)
{
    s_on_view = on_view;
}

void ui_svc_picture_view_request(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return;
    }
    view_ctx_t *ctx = (view_ctx_t *)tal_malloc(sizeof(*ctx));
    if (ctx == NULL) {
        return;
    }
    strncpy(ctx->name, name, UI_PICTURE_NAME_MAX);
    ctx->name[UI_PICTURE_NAME_MAX] = '\0';
    tal_workq_schedule(WORKQ_SYSTEM, __view_work, ctx);
}

bool ui_svc_picture_take_pending_attachment(ui_picture_t *out)
{
    if (out == NULL || !s_has_pending) {
        return false;
    }
    *out = s_pending;                  /* ownership of out->data transfers out */
    memset(&s_pending, 0, sizeof(s_pending));
    s_has_pending = false;
    return true;
}

void ui_svc_picture_clear_pending_attachment(void)
{
    __free_pending();
}

void ui_svc_picture_recognize_current(ui_picture_ai_done_cb_t done_cb)
{
    s_ai_done = done_cb;
    if (tal_workq_schedule(WORKQ_SYSTEM, __recognize_current_work, NULL) != OPRT_OK) {
        s_ai_done = NULL;
        if (done_cb) {
            done_cb(false);
        }
    }
}

void ui_svc_picture_generate_from_current(ui_picture_ai_done_cb_t done_cb)
{
    s_ai_done = done_cb;
    if (tal_workq_schedule(WORKQ_SYSTEM, __generate_from_current_work, NULL) != OPRT_OK) {
        s_ai_done = NULL;
        if (done_cb) {
            done_cb(false);
        }
    }
}

#else /* image-album feature compiled out — inert stubs */

void ui_svc_picture_init(void) {}
bool ui_svc_picture_available(void) { return false; }
void ui_svc_picture_set_cbs(ui_picture_count_cb_t a, ui_picture_cb_t b, ui_picture_thumbs_cb_t c)
{ (void)a; (void)b; (void)c; }
void ui_svc_picture_open(void) {}
void ui_svc_picture_request(uint32_t index, uint32_t seq) { (void)index; (void)seq; }
void ui_svc_picture_request_named(const char *name, uint32_t seq) { (void)name; (void)seq; }
void ui_svc_picture_delete_current(void) {}
void ui_svc_picture_close(void) {}
void ui_svc_picture_free_rgb565(void *buf) { (void)buf; }
void ui_svc_picture_thumbs_request(void) {}
void ui_svc_picture_delete_batch(const char *const names[], uint32_t count) { (void)names; (void)count; }
void ui_svc_picture_set_view_cb(ui_picture_cb_t on_view) { (void)on_view; }
void ui_svc_picture_view_request(const char *name) { (void)name; }
bool ui_svc_picture_take_pending_attachment(ui_picture_t *out) { (void)out; return false; }
void ui_svc_picture_clear_pending_attachment(void) {}
void ui_svc_picture_recognize_current(ui_picture_ai_done_cb_t cb) { if (cb) cb(false); }
void ui_svc_picture_generate_from_current(ui_picture_ai_done_cb_t cb) { if (cb) cb(false); }

#endif /* ENABLE_IMAGE_ALBUM */
