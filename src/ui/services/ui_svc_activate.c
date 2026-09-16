#include "ui_svc_activate.h"
#include "ui_app.h"
#include "base_event.h"
#include "base_event_info.h"
#include "tuya_devos_utils.h"
#include "gw_intf.h"
#include "tal_memory.h"
#include "uni_log.h"
#include <string.h>

typedef struct {
    ui_svc_activate_event_t event;
    char url[UI_SVC_ACTIVATE_URL_MAX_LEN + 1];   /* SHORTURL events only */
} activate_notify_t;

static char s_shorturl[UI_SVC_ACTIVATE_URL_MAX_LEN + 1];   /* UI thread only */
static ui_svc_activate_cb_t s_cb = NULL;
static bool s_activate_event_queued = false;   /* activation-event thread */
static bool s_activation_complete = false;     /* UI thread */

/* UI thread. The URL travels inside the marshalled payload and the cache is
 * only ever written here, so readers (pages) never race a business thread. */
static void notify_ui_cb(void *data)
{
    activate_notify_t *notify = (activate_notify_t *)data;
    if (!notify) {
        return;
    }
    if (notify->event == UI_SVC_ACTIVATE_EVT_SHORTURL) {
        memcpy(s_shorturl, notify->url, sizeof(s_shorturl));
    } else if (notify->event == UI_SVC_ACTIVATE_EVT_ACTIVATED) {
        s_activation_complete = true;
    }
    if (s_cb) {
        s_cb(notify->event);
    }
    tal_free(notify);
}

bool ui_svc_activate_is_activated(void)
{
    return get_gw_active() >= ACTIVATED;
}

bool ui_svc_activate_is_complete(void)
{
    return s_activation_complete;
}

/* Business thread. EVENT_POST_ACTIVATE is the authoritative completion edge.
 * Direct-MQTT used by QR activation can connect before the app-side activation
 * transaction finishes, so EVENT_MQTT_CONNECTED must never complete the UI. */
static OPERATE_RET activate_evt_cb(VOID_T *data)
{
    (void)data;
    if (s_activate_event_queued) {
        return OPRT_OK;
    }
    activate_notify_t *notify = (activate_notify_t *)tal_malloc(sizeof(*notify));
    if (!notify) {
        PR_ERR("activate notify alloc failed");
        return OPRT_MALLOC_FAILED;
    }
    memset(notify, 0, sizeof(*notify));
    notify->event = UI_SVC_ACTIVATE_EVT_ACTIVATED;
    s_activate_event_queued = true;
    ui_app_async_call(notify_ui_cb, notify);
    return OPRT_OK;
}

void ui_svc_activate_init(void)
{
    s_shorturl[0] = '\0';
    s_cb = NULL;
    s_activation_complete = ui_svc_activate_is_activated();
    s_activate_event_queued = s_activation_complete;
    if (!s_activation_complete) {
        OPERATE_RET rt = ty_subscribe_event(EVENT_POST_ACTIVATE, "ui_act",
                                            activate_evt_cb,
                                            SUBSCRIBE_TYPE_ONETIME);
        if (rt != OPRT_OK) {
            PR_ERR("subscribe EVENT_POST_ACTIVATE failed: %d", rt);
        }
    }
}

void ui_svc_activate_set_cb(ui_svc_activate_cb_t cb)
{
    s_cb = cb;
}

const char *ui_svc_activate_shorturl(void)
{
    return s_shorturl;
}

void ui_svc_activate_notify_shorturl(const char *url)
{
    activate_notify_t *notify = (activate_notify_t *)tal_malloc(sizeof(*notify));
    if (!notify) {
        PR_ERR("activate shorturl alloc failed");
        return;
    }
    memset(notify, 0, sizeof(*notify));
    notify->event = UI_SVC_ACTIVATE_EVT_SHORTURL;
    if (url) {
        strncpy(notify->url, url, sizeof(notify->url) - 1);
    }
    PR_NOTICE("activate shorturl %s", notify->url[0] ? "updated" : "cleared");
    ui_app_async_call(notify_ui_cb, notify);
}
