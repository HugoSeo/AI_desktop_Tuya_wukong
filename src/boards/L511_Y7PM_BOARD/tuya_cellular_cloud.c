#include "tuya_cellular_cloud.h"

#include "tal_log.h"
#include "tal_cellular_base.h"
#include "tuya_svc_mqtt_direct.h"
#include "tal_cellular_mds.h"
#include "tuya_cellular_sys.h"
#include "tuya_cellular.h"



STATIC OPERATE_RET  __cellular_netreg_event(VOID *data)
{
    TUYA_CELLULAR_MDS_STATUS_E st = *((TUYA_CELLULAR_MDS_STATUS_E*)data);
    CHAR_T *reg[5] = {"UNKOWN","SEARCHING","REGED","PDP_AVCTIVE","CAPMED"};
    TAL_PR_DEBUG("__cellular_netreg_event st = %d,%s",st,reg[st]);
    return 0;
}

STATIC OPERATE_RET  __cellular_netpdp_event(VOID *data)
{
    TUYA_CELLULAR_MDS_NET_STATUS_E st = *((TUYA_CELLULAR_MDS_NET_STATUS_E*)data);
    CHAR_T *pdp_string[3] = {"unknown","connected","disconnected"};
    TAL_PR_DEBUG("__cellular_netpdp_event st = %d %s",st,pdp_string[st]);
    return 0;
}

STATIC OPERATE_RET  __cellular_sim_event(VOID *data)
{
    TKL_SIM_STATE_E st = *((TKL_SIM_STATE_E*)data);
    CHAR_T *sim_string[5] = {"no sim","sim ready","sim init","wait pin","wait puk"};
    TAL_PR_DEBUG("__cellular_sim_event st = %d %s",st,sim_string[st]);
    return 0;
}

STATIC OPERATE_RET  __cellular_netissue_event(VOID *data)
{
    CELL_NET_ISSUE_E issue =*((CELL_NET_ISSUE_E*)data);
    TAL_PR_DEBUG("__cellular_netissue_event issue = %d",issue);
    return 0;
}

/**
 *  设备网络状态变化通知
 */
STATIC VOID __get_cell_status(IN CONST GW_NW_STAT_T stat)
{
    TAL_PR_DEBUG("mobile status is :%d",stat);
}

STATIC INT_T wait_active_cb(VOID_T *data)
{
    return 0;
}

/**
 * @brief 日志序 mq 下发处理回调
 *
 * @param root_json 下发json
 * @return OPRT_OK if success, other if error, error code should refer to tuya_error_code.h
 */
STATIC OPERATE_RET online_log_proc_mqtt_log_config(ty_cJSON *root_json)
{
    if (NULL == root_json) {
        return OPRT_INVALID_PARM;
    }

    CHAR_T *p_resp = "not support";
    OPERATE_RET op_result = OPRT_OK;
    ty_cJSON *req_type = ty_cJSON_GetObjectItem((ty_cJSON *)root_json, "reqType");
    ty_cJSON *data_json = ty_cJSON_GetObjectItem((ty_cJSON *)root_json, "data");
    if ((NULL == data_json) || (NULL == req_type)) {
        TAL_PR_ERR("json err");
        return OPRT_CJSON_GET_ERR;
    }
    if (strcmp(req_type->valuestring, "enable_log_sequence") == 0) {
        ty_cJSON *status_json = ty_cJSON_GetObjectItem((ty_cJSON *)data_json, "status");
        if (NULL == status_json) {
            TAL_PR_ERR("json err");
            return OPRT_CJSON_GET_ERR;
        }
        p_resp = "{\"reqType\":\"enable_log_sequence\",\"data\":{\"ret\":true}}";
        op_result = OPRT_OK;
    }
    mqc_ng_ext_proto_data_rept(p_resp, 0);
    return op_result;
}

OPERATE_RET tuya_cellular_cloud_start(TY_IOT_CBS_S* cbs,CONST CHAR_T* pid,CONST CHAR_T* appv,CONST CHAR_T* fw_key,UINT_T mqtt_time)
{
    OPERATE_RET op_ret = OPRT_OK;

    op_ret = tuya_cniot_start_iot_device_mqtt_keepalive(cbs,pid,appv,fw_key,NULL,0,mqtt_time);

    //120秒， mqtt心跳间隔，最大为300s
    if(OPRT_OK != op_ret) {
        TAL_PR_ERR("tuya_iot_cat1_opencpu_dev_init err:%d",op_ret);
        return -1;
    } else {
        TAL_PR_DEBUG("ty_cloud_conenct start success");
    }
    TAL_PR_DEBUG("tuya_iot_cat1_opencpu_dev_init end\r\n");

    /* 授权，校准校验，未授权，打印错误信息 */
    op_ret = tuya_iot_cellular_dev_is_auth_info_restored();
    if (!op_ret) {
        TAL_PR_ERR("authorization information not download %d", op_ret);
        return OPRT_COM_ERROR;
    }
    TAL_PR_DEBUG("tuya_iot_cellular_dev_is_auth_info_restored ret %d",op_ret);

    // 默认关闭设备日志序上传云端，以减少流量
    log_seq_set_enable(FALSE);
    // 支持后台单独开启设备日志，用以设备调试
    mqc_ng_reg_ext_proto("enable_log_seq", online_log_proc_mqtt_log_config);
    return OPRT_OK;
}

OPERATE_RET tuya_cellular_cloud_init(VOID)
{
    OPERATE_RET op_ret = OPRT_OK;

    tuya_cniot_init();
    tuya_cniot_start_cellular();
    tuya_cniot_start_tuyaos();

    //订阅蜂窝网络注册事件
    tuya_iot_cellular_subscribe_net_registion_notify("cloud.demo",__cellular_netreg_event);
    //订阅蜂窝网络PDP事件
    tuya_iot_cellular_subscribe_pdp_notify("cloud.demo",__cellular_netpdp_event);
    //订阅蜂窝设备SIM卡的事件
    tuya_iot_cellular_subscribe_sim_notify("cloud.demo",__cellular_sim_event);
    //订阅蜂窝设备网络告警事件
    tuya_iot_cellular_subscribe_health_warning("cloud.demo",__cellular_netissue_event);

    // 蜂窝pdp激活，国内版本可以不传apn等参数，若为国外版本，需根据运营商要求传入相应的apn等参数，apn参数可以通过授权写入kv
    tal_cellular_mds_pdp_active(0,NULL,NULL,NULL);

    // 订阅激活mqtt直连成功事件
    ty_subscribe_event(EVENT_MQTT_DIRECT_CONNECTED, "wait_active", wait_active_cb, 0);

    // 订阅网络状态变化回调
    op_ret = tuya_iot_reg_get_cellular_stat_cb(__get_cell_status);
    if(OPRT_OK != op_ret) {
        TAL_PR_ERR("tuya_iot_reg_get_cellular_stat_cb err:%d",op_ret);
    }

    return op_ret;
}
