#include "app_ipc_command.h"
#include "tuya_app_gui_gw_core1.h"
#ifdef TUYA_APP_MULTI_CORE_IPC
#include "tkl_ipc.h"
#endif
#include "tkl_display.h"
#include "tkl_memory.h"

#ifdef TUYA_APP_USE_TAL_IPC
STATIC IPC_HANDLE s_ipc_app_handle = NULL;
STATIC TKL_LVGL_EVENT_CB _ipc_hdl_cb = NULL;
#endif

#ifndef TUYA_APP_MULTI_CORE_IPC
STATIC app_no_ipc_callback app_no_ipc_cp0_recv_cb = NULL;
STATIC app_no_ipc_callback app_no_ipc_cp1_recv_cb = NULL;
#endif

void __attribute__((weak)) tuya_app_gui_user_private_event_process(void *data)
{
    TY_GUI_LOG_PRINT("[%s][%d]>>>>>>>>>>>>>>entry weak common process ???\r\n", __func__, __LINE__);
}

static OPERATE_RET aic_msg_send(uint8_t *msg, uint32_t length, APP_IPC_DIR_TYPE_E type)
{
    OPERATE_RET ret = OPRT_OK;

#ifdef TUYA_APP_MULTI_CORE_IPC          //核间通讯!
    #ifdef TUYA_APP_USE_TAL_IPC
        IPC_PACKET packet = ipc_packet_init(SIZEOF(uint8_t *));
        if (packet) {
            ipc_packet_push_ptr(packet, (VOID *)msg);
            tal_ipc_notify(s_ipc_app_handle, packet);
            ipc_packet_uninit(packet);
        }
    #else
        ret = tkl_lvgl_msg_sync(msg, length);
    #endif
#else
    if (type == AIC_DIR_CP02CP1) {          //核间通讯发送方向:cp0->cp1
        if (app_no_ipc_cp1_recv_cb)
            app_no_ipc_cp1_recv_cb((void *)msg, length, NULL);
        else {
            TY_GUI_LOG_PRINT("[%s][%d]-----------------cp1 recv cb no ready ???\r\n", __func__, __LINE__);
        }
    }
    else if (type == AIC_DIR_CP12CP0) {     //核间通讯发送方向:cp1->cp0
        if (app_no_ipc_cp0_recv_cb)
            app_no_ipc_cp0_recv_cb((void *)msg, length, NULL);
        else {
            TY_GUI_LOG_PRINT("[%s][%d]-----------------cp0 recv cb no ready ???\r\n", __func__, __LINE__);
        }
    }
    else {
        TY_GUI_LOG_PRINT("[%s][%d]-----------------unknown ipc dir '%d' ???\r\n", __func__, __LINE__, type);
    }
#endif
    return ret;
}

//#if (CONFIG_CPU_INDEX == 1)
void aic_cpu1_callback(void *data, unsigned int size, void *param)
{
    extern void lv_vendor_disp_lock(void);
    extern void lv_vendor_disp_unlock(void);
    app_message_t *msg = NULL;
#ifdef TUYA_APP_MULTI_CORE_IPC
#ifdef TUYA_APP_USE_TAL_IPC
    msg = (app_message_t *)data;
#else
    struct ipc_msg_s *ipc_msg = (struct ipc_msg_s *)data;

    if (ipc_msg->type != TKL_IPC_TYPE_LVGL) {
        TY_GUI_LOG_PRINT("[%s][%d]>>>>>>>>>>>>>>recv unknown msg_type '0x%x' \r\n", __func__, __LINE__, ipc_msg->type);
        return;
    }
    msg = (app_message_t *)(ipc_msg->buf32[0]);
#endif
#else
    msg = (app_message_t *)data;
#endif
    if (msg == NULL) {
        TY_GUI_LOG_PRINT("[%s][%d]---------recv null msg ??? \r\n", __func__, __LINE__);
        return;
    }
    //TY_GUI_LOG_PRINT("[%s][%d]>>>>>>>>>>>>>>recv cmd:%d\r\n", __func__, __LINE__, msg->cmd);
    switch(msg->cmd)
    {
        case AIC_LVGL_SEND_MSG_EVENT: 
            {
                lvglMsg_t *lv_msg = (lvglMsg_t *)(msg->param1);
                uint32_t event = lv_msg->event;
                event &= ~LLV_EVENT_BY_SYNC_PROCESS;
                if (event == LLV_EVENT_VENDOR_LANGUAGE_CHANGE) {
                    TY_GUI_LOG_PRINT("[%s][%d]<-----evt set language to '%d'\r\n", __func__, __LINE__, (GUI_LANGUAGE_E)(lv_msg->param));
                    extern int ed_set_language(GUI_LANGUAGE_E language);
                    ed_set_language((GUI_LANGUAGE_E)(lv_msg->param)); 
                }
                else if (event == LLV_EVENT_USER_PRIVATE) {     //用户私有定义事件
                    TY_GUI_LOG_PRINT("[%s][%d]<-----evt user private \r\n", __func__, __LINE__);
                    extern void tuya_app_gui_user_private_event_process(void *data);
                    tuya_app_gui_user_private_event_process((void *)lv_msg->param);
                }
                else if (event == LLV_EVENT_VENDOR_TUYAOS) {
                    //TY_GUI_LOG_PRINT("[%s][%d]<-----evt get common process request\r\n", __func__, __LINE__);
                    tuya_app_gui_common_process((void *)lv_msg->param);
                }
                else {    //widgets相关事件
                    if (lv_msg->event & LLV_EVENT_BY_SYNC_PROCESS) {     //同步处理widget改变
                        lv_msg->event = event;
                        lv_vendor_disp_lock();
                        lvMsgSyncHandle(lv_msg);
                        lv_vendor_disp_unlock();
                    }
                    else {    //异步处理widget改变
                        lv_msg->event = event;
                        lvMsgSendToLvgl(lv_msg);
                    }
                }
            }
            break;
        case AIC_LCD_BACKLIGHT_ON:     //点亮LCD屏幕
            {
            //lcd_driver_backlight_open();
            extern void tuya_app_gui_screen_saver_exit(void);
            tuya_app_gui_screen_saver_exit();
            }
            break;
        case AIC_LCD_BACKLIGHT_OFF:
            {
            //lcd_driver_backlight_close();
            extern void tuya_app_gui_screen_saver_entry(void);
            tuya_app_gui_screen_saver_entry();
            }
            break;
        case AIC_LCD_BACKLIGHT_SET:
            {
                extern void tuya_app_gui_screen_saver_exit(void);
                extern void tuya_app_gui_screen_saver_entry(void);
                if ((uint8_t)(msg->param1) <= 0) {  //bl off !
                    tuya_app_gui_screen_saver_entry();
                }
                else {  //bl on !
                    tuya_app_gui_screen_saver_exit();
                }
            }
            break;

        default:
            break;
    }
}

//cp1->cp0
int aic_lvgl_msg_event_change(void *lv_msg)
{
#ifdef TUYA_APP_MULTI_CORE_IPC
#ifdef TUYA_APP_USE_TAL_IPC

#else
    struct ipc_msg_s ipc_msg;
#endif
#endif
    app_message_t *msg = NULL;
    int ret = 0;

    do{
        //TY_GUI_LOG_PRINT("[%s][%d]\r\n", __func__, __LINE__);
        msg = tkl_system_malloc(sizeof(app_message_t));
        if(!msg)
        {
            TY_GUI_LOG_PRINT("[%s][%d]---malloc fail ?\r\n", __func__, __LINE__);
            ret = OPRT_MALLOC_FAILED;
            break;
        }

        msg->cmd = AIC_LV_MSG_EVT_CHANGE;
        msg->param1 = (uint32_t)lv_msg;
#ifdef TUYA_APP_MULTI_CORE_IPC
    #ifdef TUYA_APP_USE_TAL_IPC
        ret = aic_msg_send((uint8_t *)msg, sizeof(app_message_t), AIC_DIR_CP12CP0);
    #else
        ipc_msg.type = TKL_IPC_TYPE_LVGL;
        ipc_msg.buf32[0] = (uint32_t)msg;
        ret = aic_msg_send((uint8_t *)&ipc_msg, sizeof(struct ipc_msg_s), AIC_DIR_CP12CP0);
    #endif
#else
    ret = aic_msg_send((uint8_t *)msg, sizeof(app_message_t), AIC_DIR_CP12CP0);
#endif
        //ret = msg->ret;

        tkl_system_free(msg);
    }while(0);

    return ret;
}

//#elif (CONFIG_CPU_INDEX == 0)
//cp0->cp1
static OPERATE_RET aic_backlight_open(void)
{
#ifdef TUYA_APP_MULTI_CORE_IPC
#ifdef TUYA_APP_USE_TAL_IPC
    
#else
    struct ipc_msg_s ipc_msg;
#endif
#endif
    app_message_t *msg = NULL;
    OPERATE_RET ret = OPRT_OK;

    do{
        TY_GUI_LOG_PRINT("[%s][%d]\r\n", __func__, __LINE__);
        msg = tkl_system_malloc(sizeof(app_message_t));
        if(!msg)
        {
            ret = OPRT_COM_ERROR;
            break;
        }

        msg->cmd = AIC_LCD_BACKLIGHT_ON;
#ifdef TUYA_APP_MULTI_CORE_IPC
    #ifdef TUYA_APP_USE_TAL_IPC
        ret = aic_msg_send((uint8_t *)msg, sizeof(app_message_t), AIC_DIR_CP02CP1);
    #else
        ipc_msg.type = TKL_IPC_TYPE_LVGL;
        ipc_msg.buf32[0] = (uint32_t)msg;
        ret = aic_msg_send((uint8_t *)&ipc_msg, sizeof(struct ipc_msg_s), AIC_DIR_CP02CP1);
    #endif
#else
    ret = aic_msg_send((uint8_t *)msg, sizeof(app_message_t), AIC_DIR_CP02CP1);
#endif
        //ret = msg->ret;

        tkl_system_free(msg);
    }while(0);

    return ret;
}

//cp0->cp1
static OPERATE_RET aic_backlight_close(void)
{
#ifdef TUYA_APP_MULTI_CORE_IPC
#ifdef TUYA_APP_USE_TAL_IPC
    
#else
    struct ipc_msg_s ipc_msg;
#endif
#endif
    app_message_t *msg = NULL;
    OPERATE_RET ret = OPRT_OK;

    do{
        //TY_GUI_LOG_PRINT("[%s][%d]\r\n", __func__, __LINE__);
        msg = tkl_system_malloc(sizeof(app_message_t));
        if(!msg)
        {
            ret = OPRT_COM_ERROR;
            break;
        }

        msg->cmd = AIC_LCD_BACKLIGHT_OFF;
#ifdef TUYA_APP_MULTI_CORE_IPC
    #ifdef TUYA_APP_USE_TAL_IPC
        ret = aic_msg_send((uint8_t *)msg, sizeof(app_message_t), AIC_DIR_CP02CP1);
    #else
        ipc_msg.type = TKL_IPC_TYPE_LVGL;
        ipc_msg.buf32[0] = (uint32_t)msg;
        ret = aic_msg_send((uint8_t *)&ipc_msg, sizeof(struct ipc_msg_s), AIC_DIR_CP02CP1);
    #endif
#else
    ret = aic_msg_send((uint8_t *)msg, sizeof(app_message_t), AIC_DIR_CP02CP1);
#endif
        //ret = msg->ret;

        tkl_system_free(msg);
    }while(0);

    return ret;
}

//cp0->cp1
static OPERATE_RET aic_backlight_set(uint8_t percent)
{
#ifdef TUYA_APP_MULTI_CORE_IPC
#ifdef TUYA_APP_USE_TAL_IPC
        
#else
    struct ipc_msg_s ipc_msg;
#endif
#endif
    app_message_t *msg = NULL;
    OPERATE_RET ret = OPRT_OK;

    do{
        TY_GUI_LOG_PRINT("[%s][%d]\r\n", __func__, __LINE__);
        msg = tkl_system_malloc(sizeof(app_message_t));
        if(!msg)
        {
            ret = OPRT_COM_ERROR;
            break;
        }

        msg->cmd = AIC_LCD_BACKLIGHT_SET;
        msg->param1 = (uint32_t)percent;
#ifdef TUYA_APP_MULTI_CORE_IPC
    #ifdef TUYA_APP_USE_TAL_IPC
        ret = aic_msg_send((uint8_t *)msg, sizeof(app_message_t), AIC_DIR_CP02CP1);
    #else
        ipc_msg.type = TKL_IPC_TYPE_LVGL;
        ipc_msg.buf32[0] = (uint32_t)msg;
        ret = aic_msg_send((uint8_t *)&ipc_msg, sizeof(struct ipc_msg_s), AIC_DIR_CP02CP1);
    #endif
#else
    ret = aic_msg_send((uint8_t *)msg, sizeof(app_message_t), AIC_DIR_CP02CP1);
#endif
        //ret = msg->ret;

        tkl_system_free(msg);
    }while(0);

    return ret;
}

//cp0->cp1
static OPERATE_RET aic_lvMsgSendToLvgl(void *lv_msg)
{
#ifdef TUYA_APP_MULTI_CORE_IPC
#ifdef TUYA_APP_USE_TAL_IPC
        
#else
    struct ipc_msg_s ipc_msg;
#endif
#endif
    app_message_t *msg = NULL;
    OPERATE_RET ret = OPRT_OK;

    do{
        //TY_GUI_LOG_PRINT("[%s][%d]\r\n", __func__, __LINE__);
        msg = tkl_system_malloc(sizeof(app_message_t));
        if(!msg)
        {
            TY_GUI_LOG_PRINT("[%s][%d]---malloc fail ?\r\n", __func__, __LINE__);
            ret = OPRT_COM_ERROR;
            break;
        }

        msg->param1 = (uint32_t)lv_msg;
        msg->cmd = AIC_LVGL_SEND_MSG_EVENT;
#ifdef TUYA_APP_MULTI_CORE_IPC
    #ifdef TUYA_APP_USE_TAL_IPC
        ret = aic_msg_send((uint8_t *)msg, sizeof(app_message_t), AIC_DIR_CP02CP1);
    #else
        ipc_msg.type = TKL_IPC_TYPE_LVGL;
        ipc_msg.buf32[0] = (uint32_t)msg;
        ret = aic_msg_send((uint8_t *)&ipc_msg, sizeof(struct ipc_msg_s), AIC_DIR_CP02CP1);
    #endif
#else
    ret = aic_msg_send((uint8_t *)msg, sizeof(app_message_t), AIC_DIR_CP02CP1);
#endif
        //ret = msg->ret;

        tkl_system_free(msg);
    }while(0);

    return ret;
}

OPERATE_RET ty_disp_lcd_backlight_open(VOID_T)
{
    aic_backlight_open();
    return OPRT_OK;
}

OPERATE_RET ty_disp_lcd_backlight_close(VOID_T)
{
    aic_backlight_close();
    return OPRT_OK;
}

OPERATE_RET ty_disp_lcd_backlight_set(UCHAR_T light_percent)
{
    aic_backlight_set(light_percent);
    return OPRT_OK;
}

OPERATE_RET tuya_disp_gui_event_init(LIST_HEAD *evt_list_head)
{
    struct tuya_list_head *p_list = NULL;
    GUI_DISP_EVENT_LIST_S *evt_list = NULL;

    tuya_list_for_each(p_list, evt_list_head) {
        evt_list = tuya_list_entry(p_list, GUI_DISP_EVENT_LIST_S, node);
        evt_list->evt.event |= LLV_EVENT_BY_SYNC_PROCESS;
        tuya_disp_gui_event_send(&evt_list->evt);
    }
    return OPRT_OK;
}

OPERATE_RET tuya_disp_gui_event_send(LV_DISP_EVENT_S *evt)
{
    extern BOOL_T tuya_gui_screen_is_loaded(VOID);
    if (!tuya_gui_screen_is_loaded() && ((evt->event & 0xFFFF) < LLV_EVENT_VENDOR_MIN)) {
        TY_GUI_LOG_PRINT("[%s][%d]---screen is not ready, ignore evt '%d' ?\r\n", __func__, __LINE__, (evt->event & 0xFFFF));
        return OPRT_COM_ERROR;
    }
    if (aic_lvMsgSendToLvgl((void *)evt) == OPRT_OK)
        return OPRT_OK;
    return OPRT_COM_ERROR;
}

#ifdef TUYA_APP_USE_TAL_IPC
STATIC VOID __app_ipc_recv_cb(IPC_PACKET req, IPC_RET_T *ret)
{
    VOID *buf = NULL;

    ipc_packet_pop_ptr(req, &buf);
    if (_ipc_hdl_cb != NULL)
        _ipc_hdl_cb(buf, 0, NULL);
}

OPERATE_RET tuya_app_init_ipc(TKL_LVGL_EVENT_CB ipc_hdl_cb)
{
    _ipc_hdl_cb = ipc_hdl_cb;
    return tal_ipc_init(GUI_IPC_EVENT_NAME, IPC_ROLE_P2P, __app_ipc_recv_cb, &s_ipc_app_handle);
}
#endif

#ifndef TUYA_APP_MULTI_CORE_IPC
VOID app_no_ipc_cp0_recv_callback_register(app_no_ipc_callback cb)
{
    app_no_ipc_cp0_recv_cb = cb;
}

VOID app_no_ipc_cp1_recv_callback_register(app_no_ipc_callback cb)
{
    app_no_ipc_cp1_recv_cb = cb;
}
#endif
//#endif
