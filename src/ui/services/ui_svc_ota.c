#include "ui_svc_ota.h"
#include "ui_app.h"
#include "base_event.h"
#include "base_event_info.h"
#include "tal_memory.h"
#include "tal_workq_service.h"
#include "tuya_iot_internal_api.h"
#include "tuya_svc_upgrade.h"   /* TUYA_UPGRADE_PROGRESS_T */
#include "ty_cJSON.h"
#include "uni_log.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define OTA_EVENT_SUBSCRIBER "ui_ota"
#define OTA_ATOP_VERSION     "1.0"
#define OTA_ATOP_CHECK_API   "tuya.device.screen.ota.check"
#define OTA_ATOP_CHECK_BODY  "{\"lang\":\"zh-Hans\"}"
#define OTA_ATOP_CONFIRM_API "tuya.device.screen.ota.confirm"
#define OTA_ATOP_CONFIRM_BODY "{\"channel\":0}"

#define OTA_PERCENT_UNKNOWN  (-1)

typedef struct {
    ui_svc_ota_check_state_t state;
    ui_svc_ota_info_t info;
} ota_check_work_t;

typedef struct {
    OPERATE_RET rt;
} ota_confirm_work_t;

typedef enum {
    OTA_UI_EVENT_START = 0,
    OTA_UI_EVENT_PROCESS,
    OTA_UI_EVENT_FINISHED,
    OTA_UI_EVENT_FAILED,
} ota_ui_event_t;

static ui_svc_ota_state_t s_state = UI_SVC_OTA_IDLE; /* UI thread only */
static ui_svc_ota_check_state_t s_check_state = UI_SVC_OTA_CHECK_IDLE;
static ui_svc_ota_confirm_state_t s_confirm_state = UI_SVC_OTA_CONFIRM_IDLE;
static ui_svc_ota_info_t s_info;
static int  s_percent = OTA_PERCENT_UNKNOWN;         /* UI thread only */
static bool s_ended = false;                         /* stop event seen: ignore late progress */
static bool s_inited = false;
static ui_svc_ota_cb_t s_cb = NULL;
static ui_svc_ota_cb_t s_global_cb = NULL;

static bool state_is_upgrading(ui_svc_ota_state_t state)
{
    return state == UI_SVC_OTA_PREPARING ||
           state == UI_SVC_OTA_DOWNLOADING ||
           state == UI_SVC_OTA_VERIFYING ||
           state == UI_SVC_OTA_INSTALLING;
}

static void notify_change(void)
{
    bool upgrading = state_is_upgrading(s_state);
    /* Global routing runs first. It may replace the foreground subscriber as
     * the OTA page is created, so notify the now-current page afterwards. */
    if (s_global_cb) {
        s_global_cb(upgrading);
    }
    if (s_cb) {
        s_cb(upgrading);
    }
}

static void ota_event_ui_cb(void *data)
{
    ota_ui_event_t event = (ota_ui_event_t)((uintptr_t)data - 1u);
    ui_svc_ota_state_t next = s_state;

    switch (event) {
        case OTA_UI_EVENT_START:
            next = UI_SVC_OTA_PREPARING;
            s_confirm_state = UI_SVC_OTA_CONFIRM_ACCEPTED;
            s_percent = OTA_PERCENT_UNKNOWN;
            s_ended = false;
            break;
        case OTA_UI_EVENT_PROCESS:
            next = UI_SVC_OTA_DOWNLOADING;
            if (s_ended) {
                s_percent = OTA_PERCENT_UNKNOWN;
            }
            s_ended = false;
            break;
        case OTA_UI_EVENT_FINISHED:
            /* "finished" is the package download+hmac result. The platform's
             * flash apply callback and reboot still run after this event. */
            next = UI_SVC_OTA_INSTALLING;
            s_percent = 100;
            s_ended = true;
            break;
        case OTA_UI_EVENT_FAILED:
            next = UI_SVC_OTA_FAILED;
            s_confirm_state = UI_SVC_OTA_CONFIRM_FAILED;
            s_ended = true;
            break;
        default:
            return;
    }

    if (s_state != next || event == OTA_UI_EVENT_START) {
        s_state = next;
        notify_change();
    }
}

/* Percent is carried by value (+1 keeps NULL free as an async payload). */
static void ota_percent_ui_cb(void *data)
{
    /* The progress timer is deleted just after the finished/failed event is
     * published, so one last tick can land behind it — don't revive the panel. */
    if (s_ended) {
        return;
    }

    int percent = (int)(uintptr_t)data - 1;
    /* Progress implies a download is running: the panel may have missed the
     * start event (e.g. an A/B section download resumed before UI init). */
    ui_svc_ota_state_t next = percent >= 98 ? UI_SVC_OTA_VERIFYING
                                            : UI_SVC_OTA_DOWNLOADING;
    bool changed = (s_state != next || s_percent != percent);

    s_state = next;
    s_percent = percent;
    if (changed) {
        notify_change();
    }
}

/* OTA events are published from the upgrade worker. Copy the state into the
 * async payload so the page callback always runs on the UI thread. */
static INT_T ota_started_cb(VOID_T *data)
{
    (void)data;
    ui_app_async_call(ota_event_ui_cb,
                      (void *)(uintptr_t)(OTA_UI_EVENT_START + 1u));
    return 0;
}

static INT_T ota_process_cb(VOID_T *data)
{
    (void)data;
    ui_app_async_call(ota_event_ui_cb,
                      (void *)(uintptr_t)(OTA_UI_EVENT_PROCESS + 1u));
    return 0;
}

static INT_T ota_finished_cb(VOID_T *data)
{
    (void)data;
    ui_app_async_call(ota_event_ui_cb,
                      (void *)(uintptr_t)(OTA_UI_EVENT_FINISHED + 1u));
    return 0;
}

static INT_T ota_failed_cb(VOID_T *data)
{
    (void)data;
    ui_app_async_call(ota_event_ui_cb,
                      (void *)(uintptr_t)(OTA_UI_EVENT_FAILED + 1u));
    return 0;
}

/* EVENT_OTA_PROGRESS_NOTIFY payload is the publisher's stack, so read the
 * percent out here and forward only that value. */
static INT_T ota_progress_cb(VOID_T *data)
{
    const TUYA_UPGRADE_PROGRESS_T *progress = (const TUYA_UPGRADE_PROGRESS_T *)data;
    if (!progress) {
        return 0;
    }

    int percent = progress->percent;
    if (percent < 0) {
        percent = 0;
    } else if (percent > 100) {
        percent = 100;
    }

    ui_app_async_call(ota_percent_ui_cb, (void *)(uintptr_t)(percent + 1));
    return 0;
}

static void subscribe_event(const char *event, EVENT_SUBSCRIBE_CB cb)
{
    OPERATE_RET rt = ty_subscribe_event(event, OTA_EVENT_SUBSCRIBER, cb,
                                        SUBSCRIBE_TYPE_NORMAL);
    if (rt != OPRT_OK) {
        PR_ERR("subscribe OTA event %s failed: %d", event, rt);
    }
}

void ui_svc_ota_init(void)
{
    if (s_inited) {
        return;
    }

    s_state = UI_SVC_OTA_IDLE;
    s_check_state = UI_SVC_OTA_CHECK_IDLE;
    s_confirm_state = UI_SVC_OTA_CONFIRM_IDLE;
    memset(&s_info, 0, sizeof(s_info));
    s_percent = OTA_PERCENT_UNKNOWN;
    s_ended = false;
    s_cb = NULL;
    s_global_cb = NULL;
    subscribe_event(EVENT_OTA_START_NOTIFY, ota_started_cb);
    subscribe_event(EVENT_OTA_PROCESS_NOTIFY, ota_process_cb);
    subscribe_event(EVENT_OTA_PROGRESS_NOTIFY, ota_progress_cb);
    subscribe_event(EVENT_OTA_FINISHED_NOTIFY, ota_finished_cb);
    subscribe_event(EVENT_OTA_FAILED_NOTIFY, ota_failed_cb);
    s_inited = true;
}

void ui_svc_ota_set_cb(ui_svc_ota_cb_t cb)
{
    s_cb = cb;
}

void ui_svc_ota_set_global_cb(ui_svc_ota_cb_t cb)
{
    s_global_cb = cb;
}

bool ui_svc_ota_is_upgrading(void)
{
    return state_is_upgrading(s_state);
}

ui_svc_ota_state_t ui_svc_ota_get_state(void)
{
    return s_state;
}

int ui_svc_ota_get_percent(void)
{
    return s_percent;
}

ui_svc_ota_check_state_t ui_svc_ota_get_check_state(void)
{
    return s_check_state;
}

const ui_svc_ota_info_t *ui_svc_ota_get_info(void)
{
    return &s_info;
}

ui_svc_ota_confirm_state_t ui_svc_ota_get_confirm_state(void)
{
    return s_confirm_state;
}

static bool ota_parse_file_size(const char *text, uint32_t *file_size)
{
    uint32_t value = 0;
    const char *cursor = text;

    if (text == NULL || file_size == NULL || text[0] < '0' || text[0] > '9') {
        return false;
    }

    while (*cursor != '\0') {
        uint32_t digit;

        if (*cursor < '0' || *cursor > '9') {
            return false;
        }
        digit = (uint32_t)(*cursor - '0');
        if (value > (UINT32_MAX - digit) / 10u) {
            return false;
        }
        value = value * 10u + digit;
        cursor++;
    }

    *file_size = value;
    return true;
}

static OPERATE_RET ota_parse_check_result(ty_cJSON *result, ui_svc_ota_info_t *info)
{
    ty_cJSON *main_module = NULL;
    ty_cJSON *field = NULL;
    const char *text = NULL;
    int count;
    int i;

    if (result == NULL || info == NULL || !ty_cJSON_IsArray(result)) {
        PR_ERR("OTA check parse failed: result is not an array");
        return OPRT_INVALID_PARM;
    }

    memset(info, 0, sizeof(*info));
    count = ty_cJSON_GetArraySize(result);
    for (i = 0; i < count; i++) {
        ty_cJSON *item = ty_cJSON_GetArrayItem(result, i);
        ty_cJSON *type = item ? ty_cJSON_GetObjectItem(item, "type") : NULL;

        if (type != NULL && ty_cJSON_IsNumber(type) && type->valueint == 0) {
            main_module = item;
            break;
        }
    }

    if (main_module == NULL) {
        PR_ERR("OTA check parse failed: main module(type=0) not found");
        return OPRT_NOT_FOUND;
    }

    field = ty_cJSON_GetObjectItem(main_module, "upgradeStatus");
    if (field == NULL || !ty_cJSON_IsNumber(field) || field->valueint < 0) {
        PR_ERR("OTA check parse failed: invalid upgradeStatus");
        return OPRT_COM_ERROR;
    }
    info->upgrade_status = field->valueint;

    field = ty_cJSON_GetObjectItem(main_module, "currentVersion");
    text = field ? ty_cJSON_GetStringValue(field) : NULL;
    if (text != NULL) {
        snprintf(info->current_version, sizeof(info->current_version), "%s", text);
    }

    if (info->upgrade_status == 0) {
        PR_NOTICE("OTA check parsed: main module is up to date, currentVersion=%s",
                  info->current_version[0] != '\0' ? info->current_version : "--");
        return OPRT_OK;
    }

    field = ty_cJSON_GetObjectItem(main_module, "version");
    text = field ? ty_cJSON_GetStringValue(field) : NULL;
    if (text == NULL || text[0] == '\0') {
        PR_ERR("OTA check parse failed: target version not found");
        return OPRT_COM_ERROR;
    }
    snprintf(info->version, sizeof(info->version), "%s", text);

    field = ty_cJSON_GetObjectItem(main_module, "desc");
    text = field ? ty_cJSON_GetStringValue(field) : NULL;
    if (text != NULL) {
        snprintf(info->desc, sizeof(info->desc), "%s", text);
    }

    field = ty_cJSON_GetObjectItem(main_module, "fileSize");
    text = field ? ty_cJSON_GetStringValue(field) : NULL;
    if (!ota_parse_file_size(text, &info->file_size)) {
        PR_WARN("OTA check parse: invalid or missing fileSize");
    }

    PR_NOTICE("OTA check parsed: upgradeStatus=%d, currentVersion=%s, "
              "version=%s, fileSize=%u, desc=%s",
              info->upgrade_status,
              info->current_version[0] != '\0' ? info->current_version : "--",
              info->version,
              (unsigned int)info->file_size,
              info->desc[0] != '\0' ? info->desc : "--");

    return OPRT_OK;
}

static OPERATE_RET ota_atop_request(const char *api, char *body, ty_cJSON **result)
{
    OPERATE_RET rt;
    char *result_json = NULL;

    if (result == NULL) {
        return OPRT_INVALID_PARM;
    }
    *result = NULL;

    rt = iot_httpc_common_post_simple(api, OTA_ATOP_VERSION, body, NULL, result);
    if (*result != NULL) {
        result_json = ty_cJSON_PrintUnformatted(*result);
    }

    PR_NOTICE("OTA ATOP response: api=%s, body=%s, rt=%d, result=%s",
              api, body != NULL ? body : "null", rt,
              result_json != NULL ? result_json : "null");

    if (result_json != NULL) {
        ty_cJSON_FreeBuffer(result_json);
    }
    return rt;
}

static void ota_check_done_ui_cb(void *data)
{
    ota_check_work_t *work = (ota_check_work_t *)data;

    if (work == NULL) {
        return;
    }

    s_check_state = work->state;
    s_info = work->info;
    notify_change();
    tal_free(work);
}

static void ota_check_work(void *data)
{
    ota_check_work_t *work = (ota_check_work_t *)data;
    ty_cJSON *result = NULL;
    OPERATE_RET rt;

    if (work == NULL) {
        return;
    }

    work->state = UI_SVC_OTA_CHECK_FAILED;
    rt = ota_atop_request(OTA_ATOP_CHECK_API, OTA_ATOP_CHECK_BODY, &result);
    if (rt == OPRT_OK && result != NULL &&
        ota_parse_check_result(result, &work->info) == OPRT_OK) {
        work->state = work->info.upgrade_status > 0
                          ? UI_SVC_OTA_CHECK_AVAILABLE
                          : UI_SVC_OTA_CHECK_UP_TO_DATE;
    }

    if (result != NULL) {
        ty_cJSON_Delete(result);
    }

    ui_app_async_call(ota_check_done_ui_cb, work);
}

int ui_svc_ota_check_now(void)
{
    ota_check_work_t *work;
    OPERATE_RET rt;

    if (s_check_state == UI_SVC_OTA_CHECKING ||
        s_confirm_state == UI_SVC_OTA_CONFIRMING ||
        s_confirm_state == UI_SVC_OTA_CONFIRM_ACCEPTED ||
        state_is_upgrading(s_state)) {
        return OPRT_RESOURCE_NOT_READY;
    }

    work = (ota_check_work_t *)tal_malloc(sizeof(*work));
    if (work == NULL) {
        return OPRT_MALLOC_FAILED;
    }
    memset(work, 0, sizeof(*work));

    /* Called by the page on the UI thread; expose CHECKING before scheduling. */
    if (s_state == UI_SVC_OTA_FAILED) {
        s_state = UI_SVC_OTA_IDLE;
        s_percent = OTA_PERCENT_UNKNOWN;
    }
    s_check_state = UI_SVC_OTA_CHECKING;
    s_confirm_state = UI_SVC_OTA_CONFIRM_IDLE;
    memset(&s_info, 0, sizeof(s_info));
    notify_change();

    rt = tal_workq_schedule(WORKQ_SYSTEM, ota_check_work, work);
    if (rt != OPRT_OK) {
        tal_free(work);
        s_check_state = UI_SVC_OTA_CHECK_FAILED;
        notify_change();
    }
    return rt;
}

static void ota_confirm_done_ui_cb(void *data)
{
    ota_confirm_work_t *work = (ota_confirm_work_t *)data;

    if (work == NULL) {
        return;
    }

    s_confirm_state = work->rt == OPRT_OK
                          ? UI_SVC_OTA_CONFIRM_ACCEPTED
                          : UI_SVC_OTA_CONFIRM_FAILED;
    notify_change();
    tal_free(work);
}

static void ota_confirm_work(void *data)
{
    ota_confirm_work_t *work = (ota_confirm_work_t *)data;
    ty_cJSON *result = NULL;

    if (work == NULL) {
        return;
    }

    work->rt = ota_atop_request(OTA_ATOP_CONFIRM_API,
                                OTA_ATOP_CONFIRM_BODY, &result);
    if (result != NULL) {
        ty_cJSON_Delete(result);
    }
    ui_app_async_call(ota_confirm_done_ui_cb, work);
}

int ui_svc_ota_confirm_now(void)
{
    ota_confirm_work_t *work;
    OPERATE_RET rt;

    if (s_check_state != UI_SVC_OTA_CHECK_AVAILABLE ||
        s_confirm_state == UI_SVC_OTA_CONFIRMING ||
        s_confirm_state == UI_SVC_OTA_CONFIRM_ACCEPTED ||
        state_is_upgrading(s_state)) {
        return OPRT_RESOURCE_NOT_READY;
    }

    work = (ota_confirm_work_t *)tal_malloc(sizeof(*work));
    if (work == NULL) {
        return OPRT_MALLOC_FAILED;
    }
    work->rt = OPRT_COM_ERROR;

    s_confirm_state = UI_SVC_OTA_CONFIRMING;
    notify_change();
    rt = tal_workq_schedule(WORKQ_SYSTEM, ota_confirm_work, work);
    if (rt != OPRT_OK) {
        tal_free(work);
        s_confirm_state = UI_SVC_OTA_CONFIRM_FAILED;
        notify_change();
    }
    return rt;
}
