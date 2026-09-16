#include "ui_svc_wlan.h"
#include "ui_app.h"
#include "tal_memory.h"
#include "tal_workq_service.h"
#include "tuya_error_code.h"
#include "uni_log.h"
#include <string.h>

#define WLAN_CONNECT_POLL_MS       500
#define WLAN_CONNECT_MAX_POLLS     30

#if defined(ENABLE_WIFI_SERVICE) && (ENABLE_WIFI_SERVICE == 1)
#include "tal_wifi.h"
#include "tuya_devos_utils.h"
#include "tuya_wifi_netcfg.h"
#endif

typedef struct {
    ui_svc_wlan_result_t result;
} wlan_scan_work_t;

typedef struct {
    char ssid[UI_SVC_WLAN_SSID_MAX_LEN + 1];
    char password[UI_SVC_WLAN_PASSWORD_MAX_LEN + 1];
} wlan_connect_work_t;

typedef struct {
    bool connected;
    char ssid[UI_SVC_WLAN_SSID_MAX_LEN + 1];
} wlan_status_work_t;

typedef struct {
    bool success;
    ui_svc_wlan_connect_error_t error;
    char ssid[UI_SVC_WLAN_SSID_MAX_LEN + 1];
} wlan_connect_done_t;

static ui_svc_wlan_result_t s_result;
static ui_svc_wlan_cb_t s_cb = NULL;
static bool s_scanning = false;
static bool s_status_refreshing = false;
static bool s_connecting = false;
static uint8_t s_connect_poll_count = 0;
static DELAYED_WORK_HANDLE s_connect_monitor_work = NULL;
static char s_connect_target[UI_SVC_WLAN_SSID_MAX_LEN + 1];
static wlan_connect_done_t s_connect_done;

static void notify_ui_cb(void *data)
{
    (void)data;
    if (s_cb) {
        s_cb(&s_result);
    }
}

static void scan_done_ui_cb(void *data)
{
    wlan_scan_work_t *work = (wlan_scan_work_t *)data;
    if (!work) {
        s_scanning = false;
        return;
    }

    ui_svc_wlan_connect_state_t connect_state = s_result.connect_state;
    ui_svc_wlan_connect_error_t connect_error = s_result.connect_error;
    char connecting_ssid[sizeof(s_result.connecting_ssid)];
    memcpy(connecting_ssid, s_result.connecting_ssid,
           sizeof(connecting_ssid));

    s_result = work->result;
    s_result.connect_state = connect_state;
    s_result.connect_error = connect_error;
    memcpy(s_result.connecting_ssid, connecting_ssid,
           sizeof(s_result.connecting_ssid));
    if (s_connecting) {
        s_result.connected = false;
        s_result.connected_ssid[0] = '\0';
    }
    s_scanning = false;
    tal_free(work);

    if (s_cb) {
        s_cb(&s_result);
    }
}

static void scan_failed_ui_cb(void *data)
{
    (void)data;
    if (s_cb) {
        s_cb(&s_result);
    }
}

static void status_done_ui_cb(void *data)
{
    wlan_status_work_t *work = (wlan_status_work_t *)data;
    s_status_refreshing = false;
    if (!work) {
        return;
    }

    if (!s_connecting) {
        s_result.connected = work->connected;
        memcpy(s_result.connected_ssid, work->ssid,
               sizeof(s_result.connected_ssid));
    }
    tal_free(work);

    if (s_cb) {
        s_cb(&s_result);
    }
}

static void connect_done_ui_cb(void *data)
{
    wlan_connect_done_t *done = (wlan_connect_done_t *)data;
    if (!done) {
        return;
    }

    s_connecting = false;
    s_result.connect_state = done->success ?
        UI_SVC_WLAN_CONNECT_SUCCESS : UI_SVC_WLAN_CONNECT_FAILED;
    s_result.connect_error = done->error;
    if (done->success) {
        s_result.connected = true;
        memcpy(s_result.connected_ssid, done->ssid,
               sizeof(s_result.connected_ssid));
    } else {
        s_result.connected = false;
        s_result.connected_ssid[0] = '\0';
    }
    memcpy(s_result.connecting_ssid, done->ssid,
           sizeof(s_result.connecting_ssid));

    if (s_cb) {
        s_cb(&s_result);
    }

    if (s_result.connect_state == UI_SVC_WLAN_CONNECT_SUCCESS) {
        ui_svc_wlan_refresh();
    } else {
        ui_svc_wlan_refresh_status();
    }
}

static void finish_connect_from_work(bool success,
                                     ui_svc_wlan_connect_error_t error)
{
    memset(&s_connect_done, 0, sizeof(s_connect_done));
    s_connect_done.success = success;
    s_connect_done.error = error;
    memcpy(s_connect_done.ssid, s_connect_target,
           sizeof(s_connect_done.ssid));
    ui_app_async_call(connect_done_ui_cb, &s_connect_done);
}

#if defined(ENABLE_WIFI_SERVICE) && (ENABLE_WIFI_SERVICE == 1)
static void copy_connected_ssid(ui_svc_wlan_result_t *result)
{
    WF_STATION_STAT_E status = WSS_IDLE;
    if (tal_wifi_station_get_status(&status) != OPRT_OK ||
        (status != WSS_CONN_SUCCESS && status != WSS_GOT_IP)) {
        return;
    }

    const char *ssid = get_gw_ssid();
    if (!ssid || ssid[0] == '\0') {
        return;
    }

    strncpy(result->connected_ssid, ssid, sizeof(result->connected_ssid) - 1);
    result->connected_ssid[sizeof(result->connected_ssid) - 1] = '\0';
    result->connected = true;
}

static void status_work_cb(void *data)
{
    wlan_status_work_t *work = (wlan_status_work_t *)data;
    if (!work) {
        return;
    }

    ui_svc_wlan_result_t status = {0};
    copy_connected_ssid(&status);
    work->connected = status.connected;
    memcpy(work->ssid, status.connected_ssid, sizeof(work->ssid));
    ui_app_async_call(status_done_ui_cb, work);
}

static bool add_or_update_ap(ui_svc_wlan_result_t *result, const AP_IF_S *ap)
{
    if (!ap || ap->s_len == 0 || ap->ssid[0] == '\0') {
        return false;
    }

    uint8_t len = ap->s_len;
    if (len > UI_SVC_WLAN_SSID_MAX_LEN) {
        len = UI_SVC_WLAN_SSID_MAX_LEN;
    }

    char ssid[UI_SVC_WLAN_SSID_MAX_LEN + 1];
    memcpy(ssid, ap->ssid, len);
    ssid[len] = '\0';

    if (result->connected && strcmp(ssid, result->connected_ssid) == 0) {
        return false;
    }

    for (uint8_t i = 0; i < result->ap_count; i++) {
        if (strcmp(ssid, result->aps[i].ssid) == 0) {
            if (ap->rssi > result->aps[i].rssi) {
                result->aps[i].rssi = ap->rssi;
            }
            return false;
        }
    }

    if (result->ap_count >= UI_SVC_WLAN_AP_MAX) {
        return false;
    }

    ui_svc_wlan_ap_t *dst = &result->aps[result->ap_count++];
    memcpy(dst->ssid, ssid, len + 1);
    dst->rssi = ap->rssi;
    return true;
}

static void sort_by_signal(ui_svc_wlan_result_t *result)
{
    for (uint8_t i = 1; i < result->ap_count; i++) {
        ui_svc_wlan_ap_t current = result->aps[i];
        uint8_t j = i;
        while (j > 0 && result->aps[j - 1].rssi < current.rssi) {
            result->aps[j] = result->aps[j - 1];
            j--;
        }
        result->aps[j] = current;
    }
}

static void scan_work_cb(void *data)
{
    wlan_scan_work_t *work = (wlan_scan_work_t *)data;
    if (!work) {
        return;
    }

    AP_IF_S *aps = NULL;
    UINT_T ap_count = 0;
    copy_connected_ssid(&work->result);

    OPERATE_RET rt = tal_wifi_all_ap_scan(&aps, &ap_count);
    if (rt == OPRT_OK) {
        work->result.scan_ok = true;
        for (UINT_T i = 0; i < ap_count; i++) {
            add_or_update_ap(&work->result, &aps[i]);
        }
        sort_by_signal(&work->result);
    } else {
        PR_WARN("wlan scan failed: %d", rt);
    }

    if (aps) {
        tal_wifi_release_ap(aps);
    }
    ui_app_async_call(scan_done_ui_cb, work);
}

static void connect_work_cb(void *data)
{
    wlan_connect_work_t *work = (wlan_connect_work_t *)data;
    if (!work) {
        return;
    }

    OPERATE_RET rt = tuya_wifi_modify_and_conn(work->ssid, work->password);
    if (rt != OPRT_OK) {
        PR_WARN("wlan connect request failed: %d", rt);
        finish_connect_from_work(false, UI_SVC_WLAN_CONNECT_ERR_REQUEST);
    } else {
        s_connect_poll_count = 0;
        rt = tal_workq_start_delayed(s_connect_monitor_work,
                                     WLAN_CONNECT_POLL_MS, LOOP_ONCE);
        if (rt != OPRT_OK) {
            PR_ERR("wlan connect monitor start failed: %d", rt);
            finish_connect_from_work(false,
                                     UI_SVC_WLAN_CONNECT_ERR_REQUEST);
        }
    }

    memset(work, 0, sizeof(*work));
    tal_free(work);
}

static void connect_monitor_cb(void *data)
{
    (void)data;
    WF_STATION_STAT_E status = WSS_IDLE;
    OPERATE_RET rt = tal_wifi_station_get_status(&status);
    s_connect_poll_count++;

    if (rt == OPRT_OK && status == WSS_GOT_IP &&
        s_connect_poll_count > 1) {
        finish_connect_from_work(true, UI_SVC_WLAN_CONNECT_ERR_NONE);
        return;
    }

    if (rt == OPRT_OK) {
        ui_svc_wlan_connect_error_t error = UI_SVC_WLAN_CONNECT_ERR_NONE;
        switch (status) {
            case WSS_PASSWD_WRONG:
                error = UI_SVC_WLAN_CONNECT_ERR_PASSWORD;
                break;
            case WSS_NO_AP_FOUND:
                error = UI_SVC_WLAN_CONNECT_ERR_NOT_FOUND;
                break;
            case WSS_DHCP_FAIL:
                error = UI_SVC_WLAN_CONNECT_ERR_DHCP;
                break;
            case WSS_CONN_FAIL:
                error = UI_SVC_WLAN_CONNECT_ERR_REQUEST;
                break;
            default:
                break;
        }
        if (error != UI_SVC_WLAN_CONNECT_ERR_NONE &&
            s_connect_poll_count > 1) {
            finish_connect_from_work(false, error);
            return;
        }
    }

    if (s_connect_poll_count >= WLAN_CONNECT_MAX_POLLS) {
        finish_connect_from_work(false, UI_SVC_WLAN_CONNECT_ERR_TIMEOUT);
        return;
    }

    rt = tal_workq_start_delayed(s_connect_monitor_work,
                                 WLAN_CONNECT_POLL_MS, LOOP_ONCE);
    if (rt != OPRT_OK) {
        PR_ERR("wlan connect monitor restart failed: %d", rt);
        finish_connect_from_work(false, UI_SVC_WLAN_CONNECT_ERR_REQUEST);
    }
}
#endif

static void scan_start_ui_cb(void *data)
{
    wlan_scan_work_t *work = (wlan_scan_work_t *)data;
    if (!work) {
        s_scanning = false;
        return;
    }

    if (s_cb) {
        s_cb(&s_result);
    }

#if defined(ENABLE_WIFI_SERVICE) && (ENABLE_WIFI_SERVICE == 1)
    OPERATE_RET rt = tal_workq_schedule(WORKQ_SYSTEM, scan_work_cb, work);
    if (rt == OPRT_OK) {
        return;
    }
    PR_ERR("wlan scan schedule failed: %d", rt);
#else
    PR_WARN("wlan scan unavailable: Wi-Fi service is disabled");
#endif

    ui_app_async_call(scan_done_ui_cb, work);
}

void ui_svc_wlan_init(void)
{
    memset(&s_result, 0, sizeof(s_result));
    s_cb = NULL;
    s_scanning = false;
    s_status_refreshing = false;
    s_connecting = false;
    s_connect_poll_count = 0;
    s_connect_target[0] = '\0';
    memset(&s_connect_done, 0, sizeof(s_connect_done));

#if defined(ENABLE_WIFI_SERVICE) && (ENABLE_WIFI_SERVICE == 1)
    OPERATE_RET rt = tal_workq_init_delayed(WORKQ_SYSTEM,
                                            connect_monitor_cb, NULL,
                                            &s_connect_monitor_work);
    if (rt != OPRT_OK) {
        s_connect_monitor_work = NULL;
        PR_ERR("wlan connect monitor init failed: %d", rt);
    }
#endif
}

void ui_svc_wlan_set_cb(ui_svc_wlan_cb_t cb)
{
    s_cb = cb;
}

void ui_svc_wlan_refresh(void)
{
    if (s_scanning) {
        return;
    }

    wlan_scan_work_t *work = tal_malloc(sizeof(*work));
    if (!work) {
        s_result.scan_ok = false;
        PR_ERR("wlan refresh alloc failed");
        ui_app_async_call(scan_failed_ui_cb, NULL);
        return;
    }
    memset(work, 0, sizeof(*work));
    s_scanning = true;
    ui_app_async_call(scan_start_ui_cb, work);
}

void ui_svc_wlan_refresh_status(void)
{
    if (s_status_refreshing) {
        return;
    }

    wlan_status_work_t *work = tal_malloc(sizeof(*work));
    if (!work) {
        PR_ERR("wlan status alloc failed");
        return;
    }
    memset(work, 0, sizeof(*work));
    s_status_refreshing = true;

#if defined(ENABLE_WIFI_SERVICE) && (ENABLE_WIFI_SERVICE == 1)
    OPERATE_RET rt = tal_workq_schedule(WORKQ_SYSTEM, status_work_cb, work);
    if (rt == OPRT_OK) {
        return;
    }
    PR_ERR("wlan status schedule failed: %d", rt);
#else
    PR_WARN("wlan status unavailable: Wi-Fi service is disabled");
#endif

    ui_app_async_call(status_done_ui_cb, work);
}

bool ui_svc_wlan_connect(const char *ssid, const char *password)
{
    if (!ssid || ssid[0] == '\0' || !password || s_connecting ||
        !s_connect_monitor_work) {
        return false;
    }

    size_t ssid_len = strlen(ssid);
    size_t password_len = strlen(password);
    if (ssid_len > UI_SVC_WLAN_SSID_MAX_LEN ||
        password_len > UI_SVC_WLAN_PASSWORD_MAX_LEN) {
        return false;
    }

    wlan_connect_work_t *work = tal_malloc(sizeof(*work));
    if (!work) {
        PR_ERR("wlan connect alloc failed");
        return false;
    }
    memset(work, 0, sizeof(*work));
    memcpy(work->ssid, ssid, ssid_len + 1);
    memcpy(work->password, password, password_len + 1);

    s_connecting = true;
    memcpy(s_connect_target, ssid, ssid_len + 1);
    s_result.connect_state = UI_SVC_WLAN_CONNECTING;
    s_result.connect_error = UI_SVC_WLAN_CONNECT_ERR_NONE;
    memcpy(s_result.connecting_ssid, ssid, ssid_len + 1);
    s_result.connected = false;
    s_result.connected_ssid[0] = '\0';

#if defined(ENABLE_WIFI_SERVICE) && (ENABLE_WIFI_SERVICE == 1)
    OPERATE_RET rt = tal_workq_schedule(WORKQ_SYSTEM, connect_work_cb, work);
    if (rt == OPRT_OK) {
        ui_app_async_call(notify_ui_cb, NULL);
        return true;
    }
    PR_ERR("wlan connect schedule failed: %d", rt);
#else
    PR_WARN("wlan connect unavailable: Wi-Fi service is disabled");
#endif

    memset(work, 0, sizeof(*work));
    tal_free(work);
    s_connecting = false;
    s_result.connect_state = UI_SVC_WLAN_CONNECT_FAILED;
    s_result.connect_error = UI_SVC_WLAN_CONNECT_ERR_REQUEST;
    return false;
}

bool ui_svc_wlan_is_scanning(void)
{
    return s_scanning;
}

const ui_svc_wlan_result_t *ui_svc_wlan_get(void)
{
    return &s_result;
}
