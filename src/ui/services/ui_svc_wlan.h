#ifndef __UI_SVC_WLAN_H__
#define __UI_SVC_WLAN_H__

#include <stdbool.h>
#include <stdint.h>

#define UI_SVC_WLAN_SSID_MAX_LEN  32
#define UI_SVC_WLAN_PASSWORD_MAX_LEN  64
#define UI_SVC_WLAN_AP_MAX        16

typedef enum {
    UI_SVC_WLAN_CONNECT_IDLE = 0,
    UI_SVC_WLAN_CONNECTING,
    UI_SVC_WLAN_CONNECT_SUCCESS,
    UI_SVC_WLAN_CONNECT_FAILED,
} ui_svc_wlan_connect_state_t;

typedef enum {
    UI_SVC_WLAN_CONNECT_ERR_NONE = 0,
    UI_SVC_WLAN_CONNECT_ERR_REQUEST,
    UI_SVC_WLAN_CONNECT_ERR_PASSWORD,
    UI_SVC_WLAN_CONNECT_ERR_NOT_FOUND,
    UI_SVC_WLAN_CONNECT_ERR_DHCP,
    UI_SVC_WLAN_CONNECT_ERR_TIMEOUT,
} ui_svc_wlan_connect_error_t;

typedef struct {
    char   ssid[UI_SVC_WLAN_SSID_MAX_LEN + 1];
    int8_t rssi;
} ui_svc_wlan_ap_t;

typedef struct {
    bool                        scan_ok;
    bool                        connected;
    char                        connected_ssid[UI_SVC_WLAN_SSID_MAX_LEN + 1];
    uint8_t                     ap_count;
    ui_svc_wlan_ap_t            aps[UI_SVC_WLAN_AP_MAX];
    ui_svc_wlan_connect_state_t connect_state;
    ui_svc_wlan_connect_error_t connect_error;
    char                        connecting_ssid[UI_SVC_WLAN_SSID_MAX_LEN + 1];
} ui_svc_wlan_result_t;

typedef void (*ui_svc_wlan_cb_t)(const ui_svc_wlan_result_t *result);

void ui_svc_wlan_init(void);
void ui_svc_wlan_set_cb(ui_svc_wlan_cb_t cb);
void ui_svc_wlan_refresh(void);
void ui_svc_wlan_refresh_status(void);
bool ui_svc_wlan_connect(const char *ssid, const char *password);

bool ui_svc_wlan_is_scanning(void);
const ui_svc_wlan_result_t *ui_svc_wlan_get(void);

#endif /* __UI_SVC_WLAN_H__ */
