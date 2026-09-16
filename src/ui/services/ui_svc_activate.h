#ifndef __UI_SVC_ACTIVATE_H__
#define __UI_SVC_ACTIVATE_H__

#include <stdbool.h>

/*
 * Cloud-activation bridge for the first-boot activation wizard.
 *
 * - is_activated() is a synchronous query of the DevOS activation state.
 * - The activation short URL (fetched by the SDK qrcode-netcfg service once
 *   Wi-Fi is up) is pushed in from the business layer via notify_shorturl();
 *   the service caches it and forwards an event to the single-slot page
 *   callback on the UI thread.
 * - Activation completion is observed exclusively via EVENT_POST_ACTIVATE and
 *   forwarded the same way, exactly once per boot. MQTT connection is not an
 *   activation-complete signal because QR direct-MQTT connects earlier.
 */

#define UI_SVC_ACTIVATE_URL_MAX_LEN 255

typedef enum {
    UI_SVC_ACTIVATE_EVT_SHORTURL = 0,  /* cached short URL changed (may be cleared) */
    UI_SVC_ACTIVATE_EVT_ACTIVATED,     /* device just got activated */
} ui_svc_activate_event_t;

typedef void (*ui_svc_activate_cb_t)(ui_svc_activate_event_t event);

void ui_svc_activate_init(void);

bool ui_svc_activate_is_activated(void);

/* UI-thread completion latch. For a device that started unactivated this only
 * becomes true after EVENT_POST_ACTIVATE reaches the UI thread; it is also true
 * immediately when the device was already activated before service init. */
bool ui_svc_activate_is_complete(void);

/* Single-slot page callback (last-writer-wins). Register in on_enter, clear
 * (NULL) in on_leave/on_destroy. Invoked on the UI thread; the page may touch
 * LVGL directly inside the callback. */
void ui_svc_activate_set_cb(ui_svc_activate_cb_t cb);

/* Cached activation short URL; "" when none yet. UI thread only. */
const char *ui_svc_activate_shorturl(void);

/* Business layer, any thread: plain short URL string, or NULL to clear
 * (e.g. the SDK dropped the direct-MQTT link and the URL is stale). */
void ui_svc_activate_notify_shorturl(const char *url);

#endif /* __UI_SVC_ACTIVATE_H__ */
