#ifndef __UI_SVC_OTA_H__
#define __UI_SVC_OTA_H__

#include <stdbool.h>
#include <stdint.h>

#define UI_SVC_OTA_VERSION_LEN  32
#define UI_SVC_OTA_DESC_LEN     256

/* Fired on the UI thread whenever the check/confirm result, upgrade state, or
 * download percent changes; read the current values back with the getters. */
typedef void (*ui_svc_ota_cb_t)(bool upgrading);

typedef enum {
    UI_SVC_OTA_IDLE = 0,
    UI_SVC_OTA_PREPARING,
    UI_SVC_OTA_DOWNLOADING,
    UI_SVC_OTA_VERIFYING,
    UI_SVC_OTA_INSTALLING,
    UI_SVC_OTA_FAILED,
} ui_svc_ota_state_t;

typedef enum {
    UI_SVC_OTA_CHECK_IDLE = 0,
    UI_SVC_OTA_CHECKING,
    UI_SVC_OTA_CHECK_AVAILABLE,
    UI_SVC_OTA_CHECK_UP_TO_DATE,
    UI_SVC_OTA_CHECK_FAILED,
} ui_svc_ota_check_state_t;

typedef enum {
    UI_SVC_OTA_CONFIRM_IDLE = 0,
    UI_SVC_OTA_CONFIRMING,
    UI_SVC_OTA_CONFIRM_ACCEPTED,
    UI_SVC_OTA_CONFIRM_FAILED,
} ui_svc_ota_confirm_state_t;

typedef struct {
    int upgrade_status;
    uint32_t file_size;
    char current_version[UI_SVC_OTA_VERSION_LEN];
    char version[UI_SVC_OTA_VERSION_LEN];
    char desc[UI_SVC_OTA_DESC_LEN];
} ui_svc_ota_info_t;

void ui_svc_ota_init(void);
/* Foreground-page subscriber (single slot, cleared from page on_leave). */
void ui_svc_ota_set_cb(ui_svc_ota_cb_t cb);
/* App-level subscriber used for mandatory OTA page routing. */
void ui_svc_ota_set_global_cb(ui_svc_ota_cb_t cb);
bool ui_svc_ota_is_upgrading(void);
ui_svc_ota_state_t ui_svc_ota_get_state(void);

/* Download progress in percent, or -1 before the first data block. A failure
 * keeps the last known percent for diagnostics; verified download resolves to
 * 100 while flash apply/restart continues. The SDK caps download at 98%. */
int ui_svc_ota_get_percent(void);

/* Manual-check result. The returned info is service-owned and must not be freed. */
ui_svc_ota_check_state_t ui_svc_ota_get_check_state(void);
const ui_svc_ota_info_t *ui_svc_ota_get_info(void);
ui_svc_ota_confirm_state_t ui_svc_ota_get_confirm_state(void);

/* Schedule the OTA ATOP check request. Returns 0 when accepted. */
int ui_svc_ota_check_now(void);
/* Schedule the OTA ATOP confirm request. Returns 0 when accepted. */
int ui_svc_ota_confirm_now(void);

#endif /* __UI_SVC_OTA_H__ */
