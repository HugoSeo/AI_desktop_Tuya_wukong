#include "tuya_ai_cellular.h"
#include "tuya_svc_cellular.h"
#include "tuya_svc_netmgr_linkage.h"
#include "svc_netcfg_qrcode.h"
#include "tal_cellular.h"
#include "mqc_app.h"
#include "tkl_gpio.h"
#include "tal_log.h"

#define TI_DEV_DEV_META_SAVE    "tuya.device.meta.save"
BOOL_T is_cellular_ccid_reported = FALSE;

VOID_T tuya_ai_cellular_module_boot(VOID_T)
{
    TUYA_GPIO_BASE_CFG_T cfg;
    
    memset(&cfg, 0, sizeof(cfg));
    cfg.direct = TUYA_GPIO_OUTPUT;
    cfg.level = TUYA_GPIO_LEVEL_HIGH;
 
    tkl_gpio_init(TUYA_GPIO_NUM_24, &cfg);
    tkl_gpio_write(TUYA_GPIO_NUM_24, TUYA_GPIO_LEVEL_HIGH);
    
    memset(&cfg, 0, sizeof(cfg));
    cfg.direct = TUYA_GPIO_OUTPUT;
    cfg.level = TUYA_GPIO_LEVEL_LOW;

    tkl_gpio_init(TUYA_GPIO_NUM_9, &cfg);
    tkl_gpio_write(TUYA_GPIO_NUM_9, TUYA_GPIO_LEVEL_LOW);
    return;
}

OPERATE_RET tuya_ai_cellular_init()
{
    OPERATE_RET rt = OPRT_OK;
    
    TAL_CELLULAR_BASE_CFG_T cfg;
    memset(&cfg, 0, sizeof(cfg));
    strcpy(cfg.apn, "");
    TUYA_CALL_ERR_LOG(tal_cellular_init(&cfg));
    TUYA_CALL_ERR_LOG(tuya_svc_cellular_init());
    TUYA_CALL_ERR_LOG(mqc_set_connection_switch(TRUE));
    // TUYA_CALL_ERR_LOG(tuya_svc_netcfg_qrcode_init());

    LINKAGE_TYPE_E linkage_pri[LINKAGE_TYPE_MAX] = { 0 };
    uint8_t cnt = 0;
    linkage_pri[cnt++] = LINKAGE_TYPE_WIFI;
    linkage_pri[cnt++] = LINKAGE_TYPE_CAT1;
    TUYA_CALL_ERR_LOG(tuya_svc_netmgr_linkage_set_priority(linkage_pri, cnt));

    unm_set_dns_force_system();

    return rt;
}


STATIC OPERATE_RET __httpc_put_iccid(IN CHAR_T iccid[21])
{
    OPERATE_RET op_ret = OPRT_OK;
    INT_T buffer_len = 72;
    CHAR_T *post_data = Malloc(buffer_len);
    if (post_data == NULL)
    {
        TAL_PR_ERR("Malloc Fail");
        return OPRT_MALLOC_FAILED;
    }

    memset(post_data, 0, buffer_len);
    snprintf(post_data, buffer_len, "{\"metas\":{\"catIccId\":\"%s\"}}", iccid);

    op_ret = iot_httpc_common_post_simple(TI_DEV_DEV_META_SAVE, "1.0", post_data, NULL,NULL);
    Free(post_data);
    return op_ret;
}

OPERATE_RET tuya_ai_cellular_upload_iccid(VOID_T)
{
    OPERATE_RET op_ret;
    CHAR_T iccid[TAL_CELLULAR_CCID_LEN+1] = { 0 };
    
    if (is_cellular_ccid_reported) {
        return OPRT_OK;
    }

    op_ret = tal_cellular_get_ccid(iccid);
    if (OPRT_OK != op_ret) {
        return OPRT_COM_ERROR;
    }

    if ('\0' == iccid[0]) {
        return OPRT_COM_ERROR;
    }
 
    op_ret = __httpc_put_iccid(iccid);
    if (OPRT_OK != op_ret) {
        return OPRT_COM_ERROR;
    }

    is_cellular_ccid_reported = TRUE;
    TAL_PR_NOTICE("cellular report ccid %s to Tuya cloud", iccid);
    return op_ret;
}