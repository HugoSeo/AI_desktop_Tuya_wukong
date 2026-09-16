#include "tuya_ai_linkpolicy.h"
#include "tuya_svc_netmgr_linkage.h"
#include "tuya_cloud_wifi_defs.h"
#include "base_event_info.h"
#include "base_event.h"
#include "tuya_ws_db.h"
#include "mqc_app.h"
#include "tal_log.h"
#include "tal_wifi.h"
#include "gw_intf.h"
#include "ty_meta_report.h"

#define LINKPOLICY_KV_KEY      "ai.linkpolicy"
#define LINKPOLICY_INVALID     0xFF

STATIC INT_T s_linkpolicy = LINKPOLICY_AUTO_SWITCH;

STATIC VOID_T __linkpolicy_apply(INT_T mode)
{
    if (mode == LINKPOLICY_4G_ONLY) {
        tuya_svc_netmgr_linkage_set_default(LINKAGE_TYPE_CAT1);
        tal_wifi_reconn_stop();
    } else {
        tuya_svc_netmgr_linkage_set_default(LINKAGE_TYPE_DEFAULT);
        tal_wifi_reconn_start();
    }
    TAL_PR_NOTICE("linkpolicy apply: %d", mode);
}

STATIC OPERATE_RET __linkpolicy_save(INT_T mode)
{
    if (s_linkpolicy == mode) {
        return OPRT_OK;
    }

    CHAR_T val = '0' + (mode & 1);
    s_linkpolicy = mode;
    __linkpolicy_apply(mode);
    return wd_common_write(LINKPOLICY_KV_KEY, (CONST BYTE_T *)&val, 1);
}

STATIC VOID_T __linkpolicy_load(VOID_T)
{
    BYTE_T *value = NULL;
    UINT_T len = 0;

    OPERATE_RET rt = wd_common_read(LINKPOLICY_KV_KEY, &value, &len);
    if (OPRT_OK == rt && len >= 1 && value != NULL) {
        s_linkpolicy = (value[0] == '0') ? LINKPOLICY_4G_ONLY : LINKPOLICY_AUTO_SWITCH;
        wd_common_free_data(value);
    }
    TAL_PR_NOTICE("linkpolicy load: %d", s_linkpolicy);
}

STATIC OPERATE_RET __linkpolicy_report(VOID_T)
{
    CHAR_T body[64] = {0};
    snprintf(body, sizeof(body), "{\"reqType\":\"multLinkStatus\",\"mode\":%d}", s_linkpolicy);
    return mqc_app_ext_proto_data_rept(body, 1);
}

STATIC OPERATE_RET __linkpolicy_config_handler(ty_cJSON *root_json)
{
    ty_cJSON *mode_json = ty_cJSON_GetObjectItem(root_json, "mode");
    if (NULL == mode_json) {
        TAL_PR_ERR("linkpolicy config: missing mode");
        return OPRT_INVALID_PARM;
    }

    INT_T mode = mode_json->valueint;
    if (mode != LINKPOLICY_4G_ONLY && mode != LINKPOLICY_AUTO_SWITCH) {
        TAL_PR_ERR("linkpolicy config: invalid mode %d", mode);
        return OPRT_INVALID_PARM;
    }

    TAL_PR_NOTICE("linkpolicy config from cloud: %d", mode);
    __linkpolicy_save(mode);
    __linkpolicy_report();

    return OPRT_OK;
}

STATIC OPERATE_RET __linkpolicy_status_handler(ty_cJSON *root_json)
{
    TAL_PR_NOTICE("linkpolicy status query from cloud");
    __linkpolicy_report();
    return OPRT_OK;
}

STATIC OPERATE_RET __linkpolicy_wifi_status_cb(VOID_T *data)
{
    GW_WIFI_NW_STAT_E status = (GW_WIFI_NW_STAT_E)(size_t)data;

    if (STAT_UNPROVISION_AP_STA_UNCFG == status) {
        TAL_PR_NOTICE("linkpolicy: entering provisioning");
        tal_wifi_reconn_start();
        tuya_svc_netmgr_linkage_set_default(LINKAGE_TYPE_WIFI);
        tuya_svc_netmgr_linkage_set_default(LINKAGE_TYPE_DEFAULT);
        s_linkpolicy = LINKPOLICY_INVALID;
    }

    return OPRT_OK;
}

STATIC OPERATE_RET __linkpolicy_activate_cb(VOID_T *data)
{
    activate_info_t *info = (activate_info_t *)data;

    if (ACTIVATE_STAGE_SUCCESS != info->stage) {
        return OPRT_OK;
    }

    if (LINKAGE_TYPE_CAT1 == info->linkage) {
        TAL_PR_NOTICE("linkpolicy: 4G activated, set 4G only");
        __linkpolicy_save(LINKPOLICY_4G_ONLY);
    } else if (LINKAGE_TYPE_WIFI == info->linkage) {
        TAL_PR_NOTICE("linkpolicy: WiFi activated, set auto-switch");
        __linkpolicy_save(LINKPOLICY_AUTO_SWITCH);
    }

    return OPRT_OK;
}

OPERATE_RET tuya_ai_linkpolicy_set(INT_T mode)
{
    if (mode != LINKPOLICY_4G_ONLY && mode != LINKPOLICY_AUTO_SWITCH) {
        TAL_PR_ERR("linkpolicy set: invalid mode %d", mode);
        return OPRT_INVALID_PARM;
    }

    TAL_PR_NOTICE("linkpolicy set locally: %d", mode);
    return __linkpolicy_save(mode);
}

#include "tuya_iot_wifi_api.h"


void tuya_ai_linkpolicy_cli_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    if (argc < 2) {
        TAL_PR_NOTICE("usage: linkpolicy <cmd>");
        TAL_PR_NOTICE("  get              - show current mode");
        TAL_PR_NOTICE("  set <0|1|2|0xFF> - 0=auto_switch, 1=4g_only, 2=4g_only+mqc_restart, 0xFF=provision");
        return;
    }

    extern uint32_t get_cloud_enable;
    if (0 == strcmp(argv[1], "enable")) {
        get_cloud_enable = 1;
        return;
    }

    if (0 == strcmp(argv[1], "disable")) {
        get_cloud_enable = 0;
        return;
    }
    
    if (0 == strcmp(argv[1], "reset")) {
        tuya_iot_wf_gw_unactive();
        return;
    }

    if (0 == strcmp(argv[1], "get")) {
        TAL_PR_NOTICE("linkpolicy: %d (%s)", s_linkpolicy, s_linkpolicy ? "AUTO_SWITCH" : "4G_ONLY");
    } else if (0 == strcmp(argv[1], "set")) {
        if (argc < 3) {
            return;
        }
        INT_T mode = (INT_T)strtol(argv[2], NULL, 0);

        if (mode == s_linkpolicy) {
            return;
        }

        if (0xFF == mode) { //! 进入配网状态
            s_linkpolicy = 0;
            tal_wifi_reconn_start();
            tuya_svc_netmgr_linkage_set_default(LINKAGE_TYPE_WIFI);
            tuya_svc_netmgr_linkage_set_default(LINKAGE_TYPE_DEFAULT);
            wd_common_delete(LINKPOLICY_KV_KEY);
        } else if (mode) {  //! 设置cat1_only
            s_linkpolicy = LINKPOLICY_4G_ONLY;
            tuya_svc_netmgr_linkage_set_default(LINKAGE_TYPE_CAT1);
            wd_common_write(LINKPOLICY_KV_KEY, (CONST BYTE_T *)&s_linkpolicy, 1);
            //! 停止重连
            tal_wifi_reconn_stop();
            tal_wifi_station_disconnect();
            if (2 == mode) {
                //! 测试主动切换
                mqc_app_restart();
            }
        } else {    //! 非配网状态，设置wifi优先
            s_linkpolicy = LINKPOLICY_AUTO_SWITCH;
            tuya_svc_netmgr_linkage_set_default(LINKAGE_TYPE_DEFAULT);
            wd_common_delete(LINKPOLICY_KV_KEY);
            //! 恢复网络监控
            tal_wifi_reconn_start();
            if (get_gw_cntl()->gw_wsm.stat >= ACTIVATED && get_gw_cntl()->gw_wsm.ssid[0]) {
                tal_wifi_station_connect(get_gw_cntl()->gw_wsm.ssid, get_gw_cntl()->gw_wsm.passwd);
            }
        }
    }
}

STATIC OPERATE_RET __linkpolicy_meta_report_cb(VOID_T *data)
{
    OPERATE_RET op_ret = OPRT_OK;

    ty_cJSON *meta = ty_cJSON_CreateObject();
    ty_cJSON_AddBoolToObject(meta, "supportMultLink", TRUE);
    op_ret = ty_meta_report(meta, REPORT_MODE_DEFAULT);
    if (OPRT_OK != op_ret) {
        TAL_PR_ERR("linkpolicy meta report error %d", op_ret);
    }
    ty_cJSON_Delete(meta);

    return op_ret;
}

OPERATE_RET tuya_ai_linkpolicy_init(VOID_T)
{
    __linkpolicy_load();
    __linkpolicy_apply(s_linkpolicy);

    mqc_app_reg_ext_proto("multLinkConfig", __linkpolicy_config_handler);
    mqc_app_reg_ext_proto("multLinkStatus", __linkpolicy_status_handler);

    ty_subscribe_event(EVENT_WIFI_NETWORK_STATUS, "linkpolicy", __linkpolicy_wifi_status_cb, SUBSCRIBE_TYPE_NORMAL);
    ty_subscribe_event(EVENT_LINK_ACTIVATE, "linkpolicy", __linkpolicy_activate_cb, SUBSCRIBE_TYPE_NORMAL);
    ty_subscribe_event(EVENT_RUN, "linkpolicy", __linkpolicy_meta_report_cb, SUBSCRIBE_TYPE_ONETIME);

    TAL_PR_NOTICE("linkpolicy init done, mode=%d", s_linkpolicy);
    return OPRT_OK;
}
