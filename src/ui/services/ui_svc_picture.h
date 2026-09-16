#ifndef __UI_SVC_PICTURE_H__
#define __UI_SVC_PICTURE_H__

/**
 * @file ui_svc_picture.h
 * @brief Picture data service — the UI layer's only door to the picture backend.
 *
 * Wraps all wukong_picture_* access and JPEG->RGB565 decode so pages never touch
 * business (wukong_picture.h) or platform (tal_image_*) headers. Every operation
 * that touches the album scan session runs on WORKQ_SYSTEM (serialised, no flash
 * stall on the UI thread); results are marshalled back to the UI thread and
 * delivered through the callbacks below (see ADR-0002).
 *
 * Big-picture navigation uses an index for swipes and an exact filename for grid
 * taps. Both paths use latest-wins coalescing: each decoded picture echoes the seq
 * so the page can drop stale frames during fast interaction.
 */

#include <stdint.h>
#include <stdbool.h>

/** Must equal WUKONG_PICTURE_NAME_MAX_LEN (static-asserted in the .c). */
#define UI_PICTURE_NAME_MAX   64
/** Upper bound on thumbnails surfaced to the grid. */
#define UI_PICTURE_MAX_THUMBS 30

/**
 * @brief A decoded full picture for the viewer.
 *
 * @data is an RGB565 buffer whose ownership passes to the @ref ui_picture_cb_t
 * receiver: either hand it to ui_comp_picture_set_rgb565() with
 * @ref ui_svc_picture_free_rgb565 as the free_fn, or release it directly via
 * ui_svc_picture_free_rgb565() when dropping a stale frame.
 */
typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t *data;     /**< RGB565; NULL when decode failed */
    uint32_t index;    /**< 0-based index in the album scan order */
    uint32_t seq;      /**< echoes the request seq for latest-wins */
} ui_picture_t;

/**
 * @brief A grid thumbnail.
 *
 * @data is RGB565 borrowed from the service-owned thumb list. During refresh the
 * old list remains valid until the callback has synchronously replaced its canvas
 * objects; afterwards the service releases it. The receiver MUST NOT free it.
 */
typedef struct {
    char     name[UI_PICTURE_NAME_MAX + 1];
    uint16_t width;
    uint16_t height;
    uint8_t *data;
} ui_picture_thumb_t;

/* All callbacks fire on the UI thread. */
typedef void (*ui_picture_count_cb_t)(uint32_t count);
typedef void (*ui_picture_cb_t)(const ui_picture_t *picture);
typedef void (*ui_picture_thumbs_cb_t)(const ui_picture_thumb_t *items, uint32_t count);
typedef void (*ui_picture_ai_done_cb_t)(bool ok);

void ui_svc_picture_init(void);
/** @return false when the image-album feature is compiled out. */
bool ui_svc_picture_available(void);

/**
 * @brief Register viewer/grid callbacks. Pass NULL for any to clear.
 * @note A page MUST clear these (all NULL) in its on_destroy before close.
 */
void ui_svc_picture_set_cbs(ui_picture_count_cb_t  on_count,
                            ui_picture_cb_t  on_picture,
                            ui_picture_thumbs_cb_t on_thumbs);

/* ---- Viewer (async; results via callbacks) ---------------------------------*/

/** Open the scan session, then deliver the picture count via on_count. */
void ui_svc_picture_open(void);

/** Decode the picture at @p index; on_picture echoes @p seq for latest-wins. */
void ui_svc_picture_request(uint32_t index, uint32_t seq);

/** Decode the named picture and report its resolved scan index via on_picture. */
void ui_svc_picture_request_named(const char *name, uint32_t seq);

/** Delete the last-loaded picture, then re-deliver the new count via on_count. */
void ui_svc_picture_delete_current(void);

/** Close the scan session and free the thumb list + any pending attachment. */
void ui_svc_picture_close(void);

/** Release a full-picture buffer obtained via ui_picture_t.data. */
void ui_svc_picture_free_rgb565(void *buf);

/* ---- Grid (async) ----------------------------------------------------------*/

/** Build the thumbnail list, then deliver it via on_thumbs. */
void ui_svc_picture_thumbs_request(void);

/** Delete pictures by filename (e.g. the grid's selected names). */
void ui_svc_picture_delete_batch(const char *const names[], uint32_t count);

/* ---- Chat "view image" hyperlink (生图模式) --------------------------------*/

/**
 * @brief Register the one-shot decode callback for ui_svc_picture_view_request.
 *        Pass NULL to clear (the chat page MUST clear it in on_destroy).
 * @note Independent of ui_svc_picture_set_cbs; no album session is required —
 *       the picture is read standalone by name.
 */
void ui_svc_picture_set_view_cb(ui_picture_cb_t on_view);

/**
 * @brief Decode the named picture to RGB565 and deliver it via the view callback.
 *        The buffer ownership passes to the receiver (free via
 *        ui_svc_picture_free_rgb565). When no callback is registered the buffer
 *        is released by the service so nothing leaks.
 */
void ui_svc_picture_view_request(const char *name);

/* ---- Pending image-to-image attachment preview (ADR-0003) ----------------*/

/**
 * @brief Take the pending chat attachment (ownership of @p out->data transfers
 *        to the caller, to be released via ui_svc_picture_free_rgb565()).
 * @return false when there is no pending attachment.
 */
bool ui_svc_picture_take_pending_attachment(ui_picture_t *out);

/** Drop the pending chat attachment without taking it. */
void ui_svc_picture_clear_pending_attachment(void);

/* ---- Album AI actions ----------------------------------------------------*/

/**
 * @brief Switch to chat mode when needed, then immediately upload the current
 *        album picture with the fixed image-recognition prompt.
 * @param[in] done_cb fires on the UI thread after the upload is dispatched.
 */
void ui_svc_picture_recognize_current(ui_picture_ai_done_cb_t done_cb);

/**
 * @brief Queue the current album picture as a chat attachment, switch to picture
 *        generation mode when needed, and prepare its RGB565 attachment preview.
 * @param[in] done_cb fires on the UI thread when the handoff is ready.
 */
void ui_svc_picture_generate_from_current(ui_picture_ai_done_cb_t done_cb);

#endif /* __UI_SVC_PICTURE_H__ */
