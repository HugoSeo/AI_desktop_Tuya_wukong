#include "vtn_auth_adapter.h"
#include "tal_log.h"
#include "tal_system.h"
#include "tal_workq_service.h"
#include "tal_wifi.h"
#include "tuya_ws_db.h"
#include "base_event.h"
#include "base_event_info.h"

#define IFLY_AUTH_APPID       "d02e8d57"
#define IFLY_AUTH_TOKEN_KEY   "ifly_auth_token"
#define IFLY_AUTH_RETRY_MAX   3
#define IFLY_AUTH_RETRY_INTERVAL_MS  3000

static char *s_auth_token = "OldBv7sTsnRohw9nrFG31JP0jldQ+xXZXLRSlUo8cS1RfPHb1WsIyJQhqloIH7FLrEscZaGE/5hBlxcnryCVc9GvuBK7rN3uDLy424/NBFo=";

static DELAYED_WORK_HANDLE s_auth_delayed_work = NULL;
static int s_auth_retry_cnt = 0;

char* aiui_get_device_id(void)
{
    static char device_id[64] = {0};
    static bool initialized = false;

    if (!initialized) {
        uint8_t mac_addr[6];
        OPERATE_RET ret = tal_wifi_get_mac(WF_STATION, mac_addr);
        if (ret != OPRT_OK) {
            snprintf(device_id, sizeof(device_id), "00112233445566778899AABBCCDDEEFF");
        } else {
            snprintf(device_id, sizeof(device_id),
                     "%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
                     mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5],
                     mac_addr[5], mac_addr[4], mac_addr[3], mac_addr[2], mac_addr[1], mac_addr[0],
                     mac_addr[0]^0xAA, mac_addr[1]^0xBB, mac_addr[2]^0xCC, mac_addr[3]^0xDD);
        }
        initialized = true;
    }

    return device_id;
}

static void __ifly_auth_verify_work(VOID_T *data)
{
    s_auth_retry_cnt++;
    TAL_PR_DEBUG("ifly auth verify attempt %d/%d", s_auth_retry_cnt, IFLY_AUTH_RETRY_MAX);

    int current_status = vtn_net_auth_get_status();
    if (current_status == 0) {
        TAL_PR_DEBUG("ifly auth already verified, skip");
        return;
    }

    char *result = vtn_net_auth_verify();
    if (result != NULL) {
        TAL_PR_DEBUG("ifly auth verify success, token len: %d", strlen(result));

        OPERATE_RET rt = wd_common_write(IFLY_AUTH_TOKEN_KEY, (CONST BYTE_T *)result, strlen(result) + 1);
        if (rt == OPRT_OK) {
            TAL_PR_DEBUG("ifly auth token saved to kv");
        } else {
            TAL_PR_ERR("ifly auth token save failed, rt: %d", rt);
        }
        return;
    }

    TAL_PR_ERR("ifly auth verify failed, attempt %d/%d", s_auth_retry_cnt, IFLY_AUTH_RETRY_MAX);

    if (s_auth_retry_cnt < IFLY_AUTH_RETRY_MAX && s_auth_delayed_work) {
        TIME_MS delay = IFLY_AUTH_RETRY_INTERVAL_MS * s_auth_retry_cnt;
        TAL_PR_DEBUG("ifly auth retry after %d ms", delay);
        OPERATE_RET rt = tal_workq_start_delayed(s_auth_delayed_work, delay, LOOP_ONCE);
        if (rt != OPRT_OK) {
            TAL_PR_ERR("ifly auth schedule retry failed, rt: %d", rt);
        }
    } else if (s_auth_retry_cnt >= IFLY_AUTH_RETRY_MAX) {
        TAL_PR_ERR("ifly auth verify failed after %d retries", IFLY_AUTH_RETRY_MAX);
    }
}

static INT_T __ifly_auth_mqtt_event_cb(VOID_T *data)
{
    TAL_PR_DEBUG("ifly auth mqtt connected, trigger auth verify");

    if (vtn_net_auth_get_status() == 0) {
        TAL_PR_DEBUG("ifly auth already verified, skip");
        return OPRT_OK;
    }

    s_auth_retry_cnt = 0;

    OPERATE_RET rt = tal_workq_start_delayed(s_auth_delayed_work, 0, LOOP_ONCE);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("ifly auth schedule verify work failed, rt: %d", rt);
    }

    return OPRT_OK;
}

void ifly_auth_init(void)
{
    char *sn = aiui_get_device_id();
    TAL_PR_DEBUG("ifly auth sn=%s", sn);

    /* Try to read saved token from kv */
    BYTE_T *saved_token = NULL;
    UINT_T token_len = 0;
    const char *init_token = s_auth_token;

    OPERATE_RET rt = wd_common_read(IFLY_AUTH_TOKEN_KEY, &saved_token, &token_len);
    if (rt == OPRT_OK && saved_token != NULL && token_len > 0) {
        TAL_PR_DEBUG("ifly auth found saved token in kv, len: %d", token_len);
        init_token = (const char *)saved_token;
    }

    int ret = vtn_net_auth_init(IFLY_AUTH_APPID, sn, init_token);
    TAL_PR_DEBUG("vtn_net_auth_init ret=%d", ret);

    if (saved_token) {
        wd_common_free_data(saved_token);
    }

    /* Check if already authed, if so no need to subscribe */
    if (vtn_net_auth_get_status() == 0) {
        TAL_PR_DEBUG("ifly auth already verified, no need to subscribe mqtt event");
        return;
    }

    /* Init delayed work for auth verify */
    rt = tal_workq_init_delayed(WORKQ_HIGHTPRI, __ifly_auth_verify_work, NULL, &s_auth_delayed_work);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("ifly auth init delayed work failed, rt: %d", rt);
        return;
    }

    /* Subscribe mqtt connected event (one-time) to trigger auth verify */
    ty_subscribe_event(EVENT_MQTT_CONNECTED, "ifly_auth", __ifly_auth_mqtt_event_cb, SUBSCRIBE_TYPE_ONETIME);
    TAL_PR_DEBUG("ifly auth subscribed EVENT_MQTT_CONNECTED");
}

extern void on_trigger_key_proess(void);


void cli_vtn_auth_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    if (argc < 2) {
        TAL_PR_ERR("usage: vtn_auth <verify|status>");
        return;
    }

    if (strcmp(argv[1], "trigger") == 0) {
        on_trigger_key_proess();
        return;
    } 

    if (0 == strcmp(argv[1], "reset")) {
        tuya_iot_wf_gw_unactive();
        return;
    }

    if (!strcmp(argv[1], "verify")) {
        s_auth_retry_cnt = IFLY_AUTH_RETRY_MAX;
        OPERATE_RET rt = tal_workq_schedule(WORKQ_HIGHTPRI, __ifly_auth_verify_work, NULL);
        if (rt != OPRT_OK) {
            TAL_PR_ERR("schedule verify work failed, rt: %d", rt);
        }
    } else if (!strcmp(argv[1], "status")) {
        int status = vtn_net_auth_get_status();
        TAL_PR_DEBUG("vtn_net_auth_get_status status=%d", status);
    } else {
        TAL_PR_ERR("unknown sub-cmd: %s", argv[1]);
    } 
}
