#ifndef __UI_SVC_DETECTION_H__
#define __UI_SVC_DETECTION_H__

#include <stdbool.h>
#include <stdint.h>

/* Detection record list service.
 *
 * Read-only browser data for cloud-pushed AI detection alerts
 * (`thing.ipc.ai.robot.msg.list`). The blocking HTTP fetch runs on
 * WORKQ_SYSTEM and the result is marshalled back to the UI thread via
 * ui_app_async_call before the page callback fires — so the page callback
 * may touch LVGL directly (RULES §8). The service owns the parsed page
 * buffer + pagination metadata; the page reads them through the getters.
 */

#define UI_SVC_DETECTION_PAGE_SIZE   10
#define UI_SVC_DETECTION_MAX_PAGE    20
/* Cap for a record's attachPics URL. The sketch used 256; bumped to 512 so
 * signed temp-storage URLs (OSS + query/signature) don't get truncated. */
#define UI_SVC_DETECTION_URL_MAX     512

typedef struct {
    char title[64];
    char datetime[32];
    char attachPics[UI_SVC_DETECTION_URL_MAX];  /**< picture URL (single, V3-encrypted) */
} ui_svc_detection_item_t;

/* Fired on the UI thread after a fetch completes. ok == true => the page
 * buffers were refreshed; ok == false => request/parse failed (buffers stale). */
typedef void (*ui_svc_detection_cb_t)(bool ok);

void ui_svc_detection_init(void);

/** Acquire the item page buffer (call from the detection page's on_create).
 *  Idempotent. On allocation failure the list stays empty rather than failing. */
void ui_svc_detection_acquire(void);

/** Release the item page buffer (call from the detection page's on_destroy).
 *  Also clears the data callback. If a list query is still in flight the free is
 *  deferred until it completes, so the worker never writes released memory. */
void ui_svc_detection_release(void);

/* Single-slot result callback (last writer wins). Pass NULL to clear. */
void ui_svc_detection_set_cb(ui_svc_detection_cb_t cb);

/* Kick an async fetch of the given 1-based page. Re-entrancy guarded: a
 * request issued while one is already in flight is dropped silently. */
void ui_svc_detection_fetch(int page_num);

/* Ask the device to perform one manual detection now ("一键总结"). The cloud
 * pushes a new record asynchronously; it becomes visible on the next fetch.
 * No-op (logs a warning) when ENABLE_AI_MODE_DETECTION is compiled out. */
void ui_svc_detection_trigger(void);

const ui_svc_detection_item_t *ui_svc_detection_items(void);
int ui_svc_detection_item_count(void);
int ui_svc_detection_total_pages(void);
int ui_svc_detection_current_page(void);

/* ---- Detection record image (download + V3 decrypt + decode) ---------------
 *
 * A Detection Record points at a single V3-encrypted JPEG via its attachPics
 * URL. ui_svc_detection_image_request() downloads it (tuya_ai_http_dld_image),
 * decrypts the self-describing V3 file (device secret key + AES-128-GCM, params
 * from the file's own EN_PIC_INFO header — see docs/adr/0001), JPEG-decodes to
 * RGB565 (scaled to fit the panel), and delivers it on the UI thread. All heavy
 * work runs off the UI thread; only the callback touches the UI thread.
 * Single-flight: a request issued while one is in flight is dropped.
 */

/** A decoded detection image. @data is RGB565 whose ownership passes to the
 *  receiver: hand it to ui_comp_picture_set_rgb565() with
 *  ui_svc_detection_free_rgb565 as free_fn, or release it directly. NULL + ok=false
 *  on any failure (download/decrypt/decode). */
typedef struct {
    bool     ok;
    uint16_t width;
    uint16_t height;
    uint8_t *data;
    uint32_t seq;     /**< echoes the request seq (latest-wins, future-proofing) */
} ui_svc_detection_image_t;

/** Fired on the UI thread when an image request completes (ok or not). */
typedef void (*ui_svc_detection_image_cb_t)(const ui_svc_detection_image_t *img);

/** Single-slot image callback (last writer wins). Pass NULL to clear; the page
 *  MUST clear it (and call ui_svc_detection_image_cancel) before destroy. */
void ui_svc_detection_set_image_cb(ui_svc_detection_image_cb_t cb);

/** Kick an async download+decrypt+decode of @p url. @p seq is echoed back on the
 *  result. No-op if a request is already in flight or @p url is empty. */
void ui_svc_detection_image_request(const char *url, uint32_t seq);

/** Abort any in-flight image download and drop its result. Call on overlay close. */
void ui_svc_detection_image_cancel(void);

/** Release an RGB565 buffer obtained via ui_svc_detection_image_t.data. */
void ui_svc_detection_free_rgb565(void *buf);

#endif /* __UI_SVC_DETECTION_H__ */
