#include "ui_svc_weather.h"
#include "ui_state.h"
#include "ui_app.h"
#include "tal_workq_service.h"
#include "tal_time_service.h"
#include "gw_intf.h"
#include "ty_cJSON.h"
#include "uni_log.h"
#include <string.h>

#define WEATHER_REFRESH_INTERVAL_MS  (30 * 60 * 1000)
#define WEATHER_FIRST_DELAY_MS       (3 * 1000)
#define WEATHER_REQUEST_BODY         "{\"codes\":[\"w.temp\",\"w.thigh\",\"w.tlow\",\"w.condition\",\"w.date.1\"]}"
#define WEATHER_API                  "thing.weather.get"

static ui_svc_weather_t s_weather;
static bool s_available = false;
static DELAYED_WORK_HANDLE s_delayed_work = NULL;
static ui_svc_weather_cb_t s_cb = NULL;

bool ui_svc_weather_available(void)
{
    return s_available;
}

const ui_svc_weather_t *ui_svc_weather_get(void)
{
    return &s_weather;
}

void ui_svc_weather_set_cb(ui_svc_weather_cb_t cb)
{
    s_cb = cb;
}

static void weather_notify_cb(void *data)
{
    (void)data;
    if (s_cb) {
        s_cb(&s_weather);
    }
}

static void weather_fetch(void *data)
{
    (void)data;
    OPERATE_RET rt = OPRT_OK;
    ty_cJSON *result = NULL;
    ty_cJSON *obj = NULL;

    if (get_gw_nw_status() != GNS_WAN_VALID) {
        tal_workq_start_delayed(s_delayed_work, WEATHER_FIRST_DELAY_MS, LOOP_ONCE);
        return;
    }

    rt = iot_httpc_common_post_simple(WEATHER_API, "1.0", WEATHER_REQUEST_BODY, NULL, &result);
    if (rt != OPRT_OK || !result) {
        PR_WARN("weather fetch failed: %d", rt);
        goto FETCH_EXIT;
    }

    ty_cJSON *data_obj = ty_cJSON_GetObjectItem(result, "data");
    if (!data_obj) {
        ty_cJSON_Delete(result);
        goto FETCH_EXIT;
    }

    obj = ty_cJSON_GetObjectItem(data_obj, "w.temp.0");
    if (obj && ty_cJSON_IsNumber(obj)) {
        s_weather.temp = (int8_t)obj->valueint;
    }

    obj = ty_cJSON_GetObjectItem(data_obj, "w.thigh.0");
    if (obj && ty_cJSON_IsNumber(obj)) {
        s_weather.high = (int8_t)obj->valueint;
    }

    obj = ty_cJSON_GetObjectItem(data_obj, "w.tlow.0");
    if (obj && ty_cJSON_IsNumber(obj)) {
        s_weather.low = (int8_t)obj->valueint;
    }

    obj = ty_cJSON_GetObjectItem(data_obj, "w.condition.0");
    if (obj && ty_cJSON_IsString(obj)) {
        strncpy(s_weather.desc, obj->valuestring, sizeof(s_weather.desc) - 1);
        s_weather.desc[sizeof(s_weather.desc) - 1] = '\0';
    }

    s_available = true;
    ty_cJSON_Delete(result);

    if (s_cb) {
        ui_app_async_call(weather_notify_cb, NULL);
    }

FETCH_EXIT:
    tal_workq_start_delayed(s_delayed_work, WEATHER_REFRESH_INTERVAL_MS, LOOP_ONCE);
}

void ui_svc_weather_init(void)
{
    memset(&s_weather, 0, sizeof(s_weather));
    s_available = false;

    tal_workq_init_delayed(WORKQ_SYSTEM, weather_fetch, NULL, &s_delayed_work);
    tal_workq_start_delayed(s_delayed_work, WEATHER_FIRST_DELAY_MS, LOOP_ONCE);
}
