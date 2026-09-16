#ifndef __UI_SVC_CAMERA_H__
#define __UI_SVC_CAMERA_H__

/**
 * @file ui_svc_camera.h
 * @brief Camera data service — the UI layer's only door to the camera backend.
 *
 * Wraps all tuya_camera_* and album (wukong_picture_*) access so the
 * camera page stays free of business/platform headers (see src/ui/RULES.md §6/§8).
 * All pixel conversion (YUV422->RGB565 preview, JPEG->RGB565 thumbnail) lives in
 * the camera business layer (tuya_camera.c); this service only moves the
 * target stream / thumbnail to the UI thread and hands it to the page via callback.
 *
 * Threading (RULES §8 data-service model — this service NEVER touches LVGL):
 *   - The camera thread produces RGB565 preview frames; the service keeps only the
 *     newest pending frame (drop-old) and marshals it to the UI thread with
 *     ui_app_async_call(). The page's preview callback then runs on the UI thread
 *     and refreshes the canvas there.
 *   - Capture and latest-thumbnail load run on WORKQ_SYSTEM (snapshot/album IO
 *     block) and are delivered the same way.
 *
 * Both callbacks fire on the UI thread and transfer RGB565 buffer ownership to the
 * callee, which releases it via @ref ui_svc_camera_rgb_free (or hands it to
 * ui_comp_picture_set_rgb565 with that free_fn). A NULL buffer means "nothing to
 * show" and carries no ownership.
 */

#include <stdint.h>
#include <stdbool.h>

/** Live-preview frame callback (UI thread); takes ownership of @p rgb565. */
typedef void (*ui_svc_camera_preview_cb_t)(uint16_t width, uint16_t height,
                                           uint8_t *rgb565, uint32_t len);

/** Album-preview thumbnail callback (UI thread); takes ownership of @p rgb565. */
typedef void (*ui_svc_camera_thumb_cb_t)(uint16_t width, uint16_t height,
                                         uint8_t *rgb565, uint32_t len);

/** Capture completion callback (UI thread). */
typedef void (*ui_svc_camera_capture_cb_t)(bool success);

/** Reset service state + create the frame lock. Call once from ui_services_init(). */
void ui_svc_camera_init(void);

/** @return true when the camera feature is compiled in. */
bool ui_svc_camera_available(void);

/**
 * @brief Register the live-preview callback (UI thread). Pass NULL to clear.
 * @note The page MUST clear it (NULL) in its on_destroy before the page is gone.
 */
void ui_svc_camera_set_preview_cb(ui_svc_camera_preview_cb_t cb);

/** Start the live preview (YUV422 stream -> RGB565 frames via the preview cb). */
void ui_svc_camera_preview_start(void);

/** Stop the live preview; safe to call when not started. */
void ui_svc_camera_preview_stop(void);

/**
 * @brief Register the thumbnail-ready callback (UI thread). Pass NULL to clear.
 * @note The page MUST clear it (NULL) in its on_destroy before the page is gone.
 */
void ui_svc_camera_set_thumb_cb(ui_svc_camera_thumb_cb_t cb);

/**
 * @brief Register the capture-completion callback (UI thread). Pass NULL to clear.
 * @note Kept separate from the thumbnail callback so an entry-time album refresh
 *       cannot be mistaken for completion of a newly requested capture.
 */
void ui_svc_camera_set_capture_cb(ui_svc_camera_capture_cb_t cb);

/** Capture one photo (snapshot JPEG -> save to album -> thumbnail), async. */
void ui_svc_camera_capture(void);

/** Load the most recent album photo as a thumbnail (NULL if album empty), async. */
void ui_svc_camera_refresh_thumb(void);

/** Release an RGB565 buffer delivered through either callback. */
void ui_svc_camera_rgb_free(void *buf);

/**
 * @brief Unconditionally terminate any dialogue turn in progress.
 *
 * Called when the camera page opens: the active chat sub-mode barges in
 * (stop TTS/playback, reset the audio frontend, break the cloud chat) and
 * settles back to IDLE, so opening the camera cleanly stops an ongoing
 * conversation. No-op for non-chat modes (see docs/adr/0005).
 */
void ui_svc_camera_interrupt_chat(void);

#endif /* __UI_SVC_CAMERA_H__ */
