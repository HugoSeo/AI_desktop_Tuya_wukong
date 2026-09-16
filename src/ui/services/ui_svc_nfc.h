#ifndef __UI_SVC_NFC_H__
#define __UI_SVC_NFC_H__

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    UI_SVC_NFC_OK = 0,
    UI_SVC_NFC_DISABLED,
    UI_SVC_NFC_NOT_READY,
    UI_SVC_NFC_BUSY,
    UI_SVC_NFC_NO_CARD,
    UI_SVC_NFC_UNSUPPORTED_CARD,
    UI_SVC_NFC_FAILED,
} ui_svc_nfc_result_t;

/* All operation callbacks run on the UI thread. For a successful read, uuid is
 * borrowed for the duration of the callback; write callbacks receive NULL. */
typedef void (*ui_svc_nfc_op_cb_t)(ui_svc_nfc_result_t result, const char *uuid);

/* Runtime settings state. Turning on starts NFC initialization; turning off
 * deliberately performs no hardware action. Persistence is owned by the
 * unified ui_svc_dev_ctrl load()/save() path. */
bool ui_svc_nfc_available(void);
bool ui_svc_nfc_ready(void);
bool ui_svc_nfc_switch_get(void);
void ui_svc_nfc_switch_set(bool enabled);

/* UI-thread entry points. PN532 polling/auth/read/write run on WORKQ_SYSTEM;
 * the result is marshalled back to the UI thread before cb is invoked. */
void ui_svc_nfc_read_async(ui_svc_nfc_op_cb_t cb);
void ui_svc_nfc_write_uuid_async(const char *uuid, ui_svc_nfc_op_cb_t cb);

#endif /* __UI_SVC_NFC_H__ */
