#include "ui_svc_nfc.h"
#include "tuya_app_config.h"

#if defined(ENABLE_TUYA_NFC) && (ENABLE_TUYA_NFC == 1)

#include "tal_memory.h"
#include "tal_system.h"
#include "tal_workq_service.h"
#include "tuya_pn532_hsu.h"
#include "tuya_device_board.h"
#include "uni_log.h"
#include "ui_app.h"
#include "ui_state.h"
#include "ui_svc_dev_ctrl.h"
#include <stdio.h>
#include <string.h>

#define UI_NFC_UUID_MAX_LEN 32
#define UI_NFC_RETRY_MAX    3

static volatile bool s_switch_on = false;
static volatile bool s_init_pending = false;
static volatile bool s_initialized = false;
static volatile bool s_op_pending = false;

typedef struct {
    bool write;
    char uuid[UI_NFC_UUID_MAX_LEN + 1];
    ui_svc_nfc_result_t result;
    ui_svc_nfc_op_cb_t cb;
} ui_nfc_op_work_t;

static void __nfc_init_failed(OPERATE_RET rt)
{
    /* Initialization failure is terminal for this enable attempt: immediately
     * roll the desired switch back to OFF, refresh a visible settings page,
     * and persist the corrected value through the unified settings KV path. */
    s_switch_on = false;
    s_initialized = false;
    s_init_pending = false;
    ui_state_mark_dirty(UI_STATE_GROUP_SETTINGS);
    ui_svc_dev_ctrl_save();
    PR_ERR("NFC runtime init failed, switch restored to OFF: %d", rt);
}

static void __nfc_init_work(void *data)
{
    (void)data;
    OPERATE_RET rt = tuya_device_board_nfc_init();
    if (rt != OPRT_OK) {
        __nfc_init_failed(rt);
        return;
    }
    s_initialized = true;
    s_init_pending = false;
    ui_state_mark_dirty(UI_STATE_GROUP_SETTINGS);
}

static void __nfc_op_deliver_ui(void *data)
{
    ui_nfc_op_work_t *work = (ui_nfc_op_work_t *)data;
    if (work == NULL) {
        return;
    }
    if (work->cb != NULL) {
        work->cb(work->result,
                 (!work->write && work->result == UI_SVC_NFC_OK) ? work->uuid : NULL);
    }
    tal_free(work);
}

static bool __nfc_uuid_is_text(const char *uuid)
{
    if (uuid == NULL) {
        return false;
    }
    for (size_t i = 0; i < UI_NFC_UUID_MAX_LEN && uuid[i] != '\0'; i++) {
        unsigned char c = (unsigned char)uuid[i];
        if (c < 0x20 || c > 0x7e) {
            return false;
        }
    }
    return true;
}

static size_t __nfc_uuid_len(const char *uuid)
{
    size_t len = 0;
    while (len < UI_NFC_UUID_MAX_LEN && uuid[len] != '\0') {
        len++;
    }
    return len;
}

static ui_svc_nfc_result_t __nfc_poll(PN532_CARD_INFO_S *card_info)
{
    OPERATE_RET rt = tuya_pn532_poll_card(card_info);
    if (rt == OPRT_TIMEOUT) {
        return UI_SVC_NFC_NO_CARD;
    }
    if (rt != OPRT_OK) {
        PR_ERR("NFC card poll failed: %d", rt);
        return UI_SVC_NFC_FAILED;
    }
    if (card_info->type != PN532_CARD_TYPE_MIFARE_1K &&
        card_info->type != PN532_CARD_TYPE_MIFARE_4K) {
        return UI_SVC_NFC_UNSUPPORTED_CARD;
    }
    return UI_SVC_NFC_OK;
}

static void __nfc_read_work(ui_nfc_op_work_t *work)
{
    PN532_CARD_INFO_S card_info;
    uint8_t data1[16] = {0};
    uint8_t data2[16] = {0};

    work->result = __nfc_poll(&card_info);
    if (work->result != UI_SVC_NFC_OK) {
        return;
    }

    tal_system_sleep(20);
    for (int retry = 0; retry < UI_NFC_RETRY_MAX; retry++) {
        OPERATE_RET rt = tuya_pn532_read_block(&card_info, 1, PN532_KEY_A, NULL, data1);
        if (rt == OPRT_OK) {
            rt = tuya_pn532_read_block(&card_info, 2, PN532_KEY_A, NULL, data2);
        }
        if (rt == OPRT_OK) {
            memcpy(work->uuid, data1, 16);
            memcpy(work->uuid + 16, data2, 16);
            work->uuid[UI_NFC_UUID_MAX_LEN] = '\0';
            work->result = __nfc_uuid_is_text(work->uuid) ? UI_SVC_NFC_OK
                                                          : UI_SVC_NFC_FAILED;
            return;
        }

        PR_WARN("NFC block read retry %d failed: %d", retry + 1, rt);
        if (tuya_pn532_poll_card(&card_info) == OPRT_OK) {
            tal_system_sleep(20);
        }
    }
    work->result = UI_SVC_NFC_FAILED;
}

static void __nfc_write_work(ui_nfc_op_work_t *work)
{
    PN532_CARD_INFO_S card_info;
    uint8_t data1[16] = {0};
    uint8_t data2[16] = {0};

    work->result = __nfc_poll(&card_info);
    if (work->result != UI_SVC_NFC_OK) {
        return;
    }

    size_t uuid_len = __nfc_uuid_len(work->uuid);
    memcpy(data1, work->uuid, uuid_len > 16 ? 16 : uuid_len);
    if (uuid_len > 16) {
        memcpy(data2, work->uuid + 16, uuid_len - 16);
    }

    tal_system_sleep(20);
    for (int retry = 0; retry < UI_NFC_RETRY_MAX; retry++) {
        OPERATE_RET rt = tuya_pn532_write_block(&card_info, 1, PN532_KEY_A, NULL, data1);
        if (rt == OPRT_OK) {
            rt = tuya_pn532_write_block(&card_info, 2, PN532_KEY_A, NULL, data2);
        }
        if (rt == OPRT_OK) {
            work->result = UI_SVC_NFC_OK;
            return;
        }

        PR_WARN("NFC block write retry %d failed: %d", retry + 1, rt);
        if (tuya_pn532_poll_card(&card_info) == OPRT_OK) {
            tal_system_sleep(20);
        }
    }
    work->result = UI_SVC_NFC_FAILED;
}

static void __nfc_op_work(void *data)
{
    ui_nfc_op_work_t *work = (ui_nfc_op_work_t *)data;
    if (work == NULL) {
        s_op_pending = false;
        return;
    }

    if (!s_switch_on) {
        work->result = UI_SVC_NFC_DISABLED;
    } else if (!s_initialized) {
        work->result = UI_SVC_NFC_NOT_READY;
    } else {
        tuya_pn532_manual_begin();
        if (work->write) {
            __nfc_write_work(work);
        } else {
            __nfc_read_work(work);
        }
        tuya_pn532_manual_end();
    }

    s_op_pending = false;
    ui_app_async_call(__nfc_op_deliver_ui, work);
}

static void __nfc_op_start(bool write, const char *uuid, ui_svc_nfc_op_cb_t cb)
{
    ui_svc_nfc_result_t immediate = UI_SVC_NFC_OK;
    if (!s_switch_on) {
        immediate = UI_SVC_NFC_DISABLED;
    } else if (!s_initialized) {
        immediate = UI_SVC_NFC_NOT_READY;
    } else if (s_op_pending) {
        immediate = UI_SVC_NFC_BUSY;
    } else if (write && (uuid == NULL || uuid[0] == '\0' || !__nfc_uuid_is_text(uuid))) {
        immediate = UI_SVC_NFC_FAILED;
    }
    if (immediate != UI_SVC_NFC_OK) {
        if (cb != NULL) {
            cb(immediate, NULL);
        }
        return;
    }

    ui_nfc_op_work_t *work = (ui_nfc_op_work_t *)tal_malloc(sizeof(*work));
    if (work == NULL) {
        if (cb != NULL) {
            cb(UI_SVC_NFC_FAILED, NULL);
        }
        return;
    }
    memset(work, 0, sizeof(*work));
    work->write = write;
    work->cb = cb;
    if (write) {
        snprintf(work->uuid, sizeof(work->uuid), "%s", uuid);
    }

    s_op_pending = true;
    OPERATE_RET rt = tal_workq_schedule(WORKQ_SYSTEM, __nfc_op_work, work);
    if (rt != OPRT_OK) {
        s_op_pending = false;
        tal_free(work);
        PR_ERR("NFC operation schedule failed: %d", rt);
        if (cb != NULL) {
            cb(UI_SVC_NFC_FAILED, NULL);
        }
    }
}

bool ui_svc_nfc_available(void)
{
    return true;
}

bool ui_svc_nfc_ready(void)
{
    return s_switch_on && s_initialized;
}

bool ui_svc_nfc_switch_get(void)
{
    return s_switch_on;
}

void ui_svc_nfc_switch_set(bool enabled)
{
    s_switch_on = enabled;

    /* OFF is intentionally UI-state-only: do not stop polling or deinit UART. */
    if (!enabled || s_initialized || s_init_pending) {
        return;
    }

    s_init_pending = true;
    OPERATE_RET rt = tal_workq_schedule(WORKQ_SYSTEM, __nfc_init_work, NULL);
    if (rt != OPRT_OK) {
        __nfc_init_failed(rt);
    }
}

void ui_svc_nfc_read_async(ui_svc_nfc_op_cb_t cb)
{
    __nfc_op_start(false, NULL, cb);
}

void ui_svc_nfc_write_uuid_async(const char *uuid, ui_svc_nfc_op_cb_t cb)
{
    __nfc_op_start(true, uuid, cb);
}

#else

bool ui_svc_nfc_available(void)
{
    return false;
}

bool ui_svc_nfc_ready(void)
{
    return false;
}

bool ui_svc_nfc_switch_get(void)
{
    return false;
}

void ui_svc_nfc_switch_set(bool enabled)
{
    (void)enabled;
}

void ui_svc_nfc_read_async(ui_svc_nfc_op_cb_t cb)
{
    if (cb != NULL) {
        cb(UI_SVC_NFC_DISABLED, NULL);
    }
}

void ui_svc_nfc_write_uuid_async(const char *uuid, ui_svc_nfc_op_cb_t cb)
{
    (void)uuid;
    if (cb != NULL) {
        cb(UI_SVC_NFC_DISABLED, NULL);
    }
}

#endif
