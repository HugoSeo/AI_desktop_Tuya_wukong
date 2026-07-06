/**
 * @file tuya_ai_display.c
 * @author www.tuya.com
 * @version 0.1
 * @date 2025-02-07
 *
 * @copyright Copyright (c) tuya.inc 2025
 *
 */

#include <stdio.h>
#include "tuya_app_gui.h"
#include "tuya_app_gui_display_text.h"
#include "tuya_app_gui_gw_core1.h"
#include "tuya_app_gui_gw_core0.h"
#include "tuya_app_gui_core_config.h"
#include "tuya_app_gui_config.h"
#include "img_utility.h"

#include "tal_time_service.h"
#include "tal_sw_timer.h"
#include "tal_system.h"
#include "tkl_display.h"
#include "tal_thread.h"
#include "tkl_queue.h"
#include "tal_semaphore.h"
#include "gw_intf.h"
#include "smart_frame.h"
#include "tuya_ai_display.h"
#include "tal_tp_service.h"
#include "tuya_port_disp.h"
#include "tal_system.h"

 /***********************************************************
 *********************** macro define ***********************
 ***********************************************************/
#define AI_SYNC_DISPLAY
 /***********************************************************
 ********************** typedef define **********************
 ***********************************************************/
STATIC TKL_QUEUE_HANDLE disp_msg_queue = NULL;
STATIC THREAD_HANDLE sg_display_thrd_hdl = NULL;
STATIC ty_ai_proc_text_notify_cb_t ty_ai_proc_text_notify = NULL;
STATIC TKL_QUEUE_HANDLE s_ui_action_queue = NULL;
STATIC THREAD_HANDLE s_ui_action_thrd_hdl = NULL;
STATIC ty_ai_disp_action_cb_t s_disp_action_cb = NULL;

typedef struct {
    UINT8_T *msg;
    INT_T len;
    TY_DISPLAY_ACTION_E action;
} TY_UI_ACTION_MSG_T;

/***********************************************************
 ********************** function define *********************
***********************************************************/
void app_ui_init(void);
void app_ui_msg_handler(TY_DISPLAY_MSG_T *msg);

void tuya_ui_init(void)
{
    app_ui_init();
}

void tuya_ui_app(TY_DISPLAY_MSG_T *msg)
{
    app_ui_msg_handler(msg);
}

static void __ai_dp_update_cb(unsigned char is_raw_data, void *pdata)
{
    lvglMsg_t lv_msg = {0};

    if (!is_raw_data) {     //obj data type
        DP_CNTL_S *p_dev_cntl = (DP_CNTL_S *)pdata;
        bool dp_identified = true;

        switch (p_dev_cntl->dp_desc.prop_tp)
        {
        case PROP_BOOL:
            TY_GUI_LOG_PRINT("DP %d bool:%d \r\n", p_dev_cntl->dp_desc.dp_id, p_dev_cntl->prop.prop_bool.value);
            break;
        case PROP_VALUE:
            TY_GUI_LOG_PRINT("DP %d value:%d \r\n",p_dev_cntl->dp_desc.dp_id, p_dev_cntl->prop.prop_value.value);
            break;
        case PROP_STR:
            if (p_dev_cntl->prop.prop_str.value != NULL)
                TY_GUI_LOG_PRINT("DP %d string:'%s' \r\n",p_dev_cntl->dp_desc.dp_id, p_dev_cntl->prop.prop_str.value);
            else
                TY_GUI_LOG_PRINT("DP %d string:'null' \r\n",p_dev_cntl->dp_desc.dp_id);
            break;
        case PROP_ENUM:
            TY_GUI_LOG_PRINT("DP %d enum:%d \r\n",p_dev_cntl->dp_desc.dp_id, p_dev_cntl->prop.prop_enum.value);
            break;
        case PROP_BITMAP:
            TY_GUI_LOG_PRINT("DP %d bitmap:%d \r\n",p_dev_cntl->dp_desc.dp_id, p_dev_cntl->prop.prop_bitmap.value);
            break;
        default:
            TY_GUI_LOG_PRINT("???unknown data type '%d'", p_dev_cntl->dp_desc.prop_tp);
            dp_identified = false;
            break;
        }

        if(dp_identified == true){
          if (p_dev_cntl->dp_desc.dp_id == 3) {     //ai demo产品中的dpid:3 ('音量')
              //lv_msg.tag = "ai_volume";
              //lv_msg.event = LV_EVENT_VALUE_CHANGED;
              //lv_msg.param = (uint32_t)p_dev_cntl->prop.prop_value.value;
              //lvMsgSendToLvgl(&lv_msg);
          }
        }
    }
}

#ifndef AI_SYNC_DISPLAY
static void __ai_display_msg_async_process(void *data)
{
    if(NULL == data) {
        return;
    }

    TY_DISPLAY_MSG_T *msg_data = (TY_DISPLAY_MSG_T *)data;
    tuya_ui_app(msg_data);
    if (msg_data->type == TY_DISPLAY_TP_STAT_SLEEP) {    //熄屏
        tuya_app_gui_screen_saver_entry_request();
    }
    if (msg_data->data != NULL)
        tkl_system_psram_free(msg_data->data);
    tkl_system_psram_free(data);
}
#endif

STATIC VOID_T __tuya_ai_display_thread(VOID_T *arg)
{
    TY_DISPLAY_MSG_T msg_data;
    OPERATE_RET rt = OPRT_OK;

    //tuya_ui_init();                   //!!!此处直接运行(未使用lv_vendor_disp_lock/lv_vendor_disp_unlock),会造成和lvgl主任务异步处理导致crash！
    for (;;) {
        rt  = tkl_queue_fetch(disp_msg_queue, &msg_data, SEM_WAIT_FOREVER);
        if (rt == OPRT_OK) {
            //TY_GUI_LOG_PRINT("[%s][%d]---------msg type = %d\r\n",  __func__, __LINE__, msg_data.type);
            tuya_app_gui_screen_saver_exit_request();
            #if defined(TUYA_IMG_DIRECT_FLUSH) && (TUYA_IMG_DIRECT_FLUSH == 1)
                //直接刷屏:未初始化lvlg,不能再使用lvgl相关接口!
                //TODO...
            #else
                #ifdef AI_SYNC_DISPLAY
                    extern void lv_vendor_disp_lock(void);
                    extern void lv_vendor_disp_unlock(void);
                    lv_vendor_disp_lock();
                    tuya_ui_app(&msg_data);
                    lv_vendor_disp_unlock();
                    if (msg_data.type == TY_DISPLAY_TP_STAT_SLEEP) {    //熄屏
                        tuya_app_gui_screen_saver_entry_request();
                    }
                    if (msg_data.data != NULL) {
                        tkl_system_psram_free(msg_data.data);
                    }
                #else
                    TY_DISPLAY_MSG_T *msg = (TY_DISPLAY_MSG_T *)tkl_system_psram_malloc(sizeof(TY_DISPLAY_MSG_T));
                    if (msg == NULL) {
                        TY_GUI_LOG_PRINT("[%s][%d]---------alloc mem fail ?\r\n",  __func__, __LINE__);
                        if (msg_data.data != NULL) {
                            tkl_system_psram_free(msg_data.data);
                        }
                    }
                    else {
                        memset((void *)msg, 0, sizeof(TY_DISPLAY_MSG_T));
                        memcpy((void *)msg, (void *)&msg_data, sizeof(TY_DISPLAY_MSG_T));
                        lv_async_call(__ai_display_msg_async_process, (void *)msg);
                    }
                #endif
            #endif
        }
        else {
            TY_GUI_LOG_PRINT("[%s][%d]---------queue fetch fail rt = %d ?\r\n",  __func__, __LINE__, rt);
        }
    }
}

void tuya_app_gui_user_private_event_process(void *data)
{
     LV_USER_PRIVATE_EVENT_S *prv_evt_s = (LV_USER_PRIVATE_EVENT_S *)data;
     TY_DISPLAY_MSG_T *msg_data = NULL;

     //TY_GUI_LOG_PRINT("[%s][%d]>>>>>>>>>>>>>>entry strong common process !\r\n", __func__, __LINE__);
     switch (prv_evt_s->event_typ) {
        case LV_USER_PRIVATE_EVENT_AI_TEXT:
            msg_data = (TY_DISPLAY_MSG_T *)(prv_evt_s->param1);
            tkl_queue_post(disp_msg_queue, msg_data, 0);
            break;

        default:
            TY_GUI_LOG_PRINT("[%s][%d]>>>>>>>>>>>>>>unknown user private event type '%d' !\r\n", __func__, __LINE__, prv_evt_s->event_typ);
            break;
     }
}

void tuya_ai_display_demo(void)
{
    TY_GUI_LOG_PRINT("-------[%s %d] \r\n", __func__, __LINE__);

    tuya_app_gui_dp_update_notify_callback_register(__ai_dp_update_cb);       //注册dp更新事件回调!
    if (disp_msg_queue == NULL)
       tkl_queue_create_init(&disp_msg_queue, sizeof(TY_DISPLAY_MSG_T), 50);
    if (NULL == sg_display_thrd_hdl) {
        tuya_ui_init();
        THREAD_CFG_T thrd_cfg = {
            .priority = 4,
            .thrdname = "ai_display",
            .stackDepth = 6 * 1024,
#if defined(ENABLE_EXT_RAM) && ENABLE_EXT_RAM == 1            
            .psram_mode = 1,
#endif
        };
        tal_thread_create_and_start(&sg_display_thrd_hdl, NULL, NULL, __tuya_ai_display_thread, NULL, &thrd_cfg);
    }
}

OPERATE_RET tuya_ai_display_msg(UINT8_T *msg, INT_T len, TY_DISPLAY_TYPE_E display_tp)
{
    if (ty_ai_proc_text_notify)
        return ty_ai_proc_text_notify(msg, len, display_tp);
    return OPRT_INVALID_PARM;
}

STATIC VOID_T __tuya_ai_action_thread(VOID_T *arg)
{
    TY_UI_ACTION_MSG_T msg = {0};
    OPERATE_RET rt = OPRT_OK;

    (VOID_T)arg;
    for (;;) {
        rt = tkl_queue_fetch(s_ui_action_queue, &msg, SEM_WAIT_FOREVER);
        if (rt != OPRT_OK) {
            TY_GUI_LOG_PRINT("[%s][%d] action queue fetch fail rt=%d", __func__, __LINE__, rt);
            continue;
        }

        if (s_disp_action_cb != NULL) {
            s_disp_action_cb(msg.msg, msg.len, msg.action);
        }
    }
}

OPERATE_RET tuya_ai_display_action_register(ty_ai_disp_action_cb_t cb)
{
    OPERATE_RET rt = OPRT_OK;

    s_disp_action_cb = cb;

    if (s_ui_action_queue == NULL) {
        TUYA_CALL_ERR_RETURN(tkl_queue_create_init(&s_ui_action_queue, sizeof(TY_UI_ACTION_MSG_T), 20));
    }

    if (s_ui_action_thrd_hdl == NULL) {
        THREAD_CFG_T thrd_cfg = {
            .priority = 4,
            .thrdname = "ui_action",
            .stackDepth = 4 * 1024,
#if defined(ENABLE_EXT_RAM) && ENABLE_EXT_RAM == 1
            .psram_mode = 1,
#endif
        };
        TUYA_CALL_ERR_RETURN(tal_thread_create_and_start(&s_ui_action_thrd_hdl, NULL, NULL,
                                                         __tuya_ai_action_thread, NULL, &thrd_cfg));
    }

    return OPRT_OK;
}

OPERATE_RET tuya_ai_display_action_post(UINT8_T *msg, INT_T len, TY_DISPLAY_ACTION_E disp_action)
{
    TY_UI_ACTION_MSG_T act_msg = {
        .action = disp_action,
        .msg = msg,
        .len = len,
    };

    if (s_ui_action_queue == NULL || s_disp_action_cb == NULL) {
        return OPRT_RESOURCE_NOT_READY;
    }

    return tkl_queue_post(s_ui_action_queue, &act_msg, 0);
}

STATIC BOOL_T _OTA_START_ = false;
STATIC BOOL_T __gui_mf_test = false;
STATIC UINT32_T gui_hb_lost_cnt = 0;

STATIC VOID tuya_gui_delay_reboot_cb(TIMER_ID timerID,PVOID_T pTimerArg)
{
    PR_NOTICE("---------------> reboot start!!!");
    tal_system_reset();
}

STATIC VOID tuya_gui_heartbeat_detect_cb(TIMER_ID timerID,PVOID_T pTimerArg)
{
    //PR_NOTICE("--------------->gui_hb_lost_cnt '%d' !!!", gui_hb_lost_cnt);
    if (!_OTA_START_) {
        if (gui_hb_lost_cnt++ >= 10) {
            PR_ERR("--------------->gui heartbeat lost too long, reboot start!!!");
            tal_system_reset();
        }
    }
}

STATIC OPERATE_RET tuya_gui_system_evt_hdl_cb(LV_DISP_EVENT_S *evt)
{
    LV_DISP_EVENT_S Sevt = {0};
    static BOOL_T screen_saver_enabled = FALSE;
    OPERATE_RET op_ret = OPRT_OK;
    STATIC GUI_LANGUAGE_E language = GUI_LG_CHINESE;        //当前系统默认语言类型

    //系统事件处理
    switch(evt->event) {
        case LLV_EVENT_VENDOR_HEART_BEAT:   //GUI发送->心跳事件!
            gui_hb_lost_cnt = 0;
            return op_ret;

        case LLV_EVENT_VENDOR_REBOOT:       //GUI发送->请求重启(收到此请求,不要做延时相关的操作)
            {
                TIMER_ID  delay_rb_timer_id = NULL;
                PR_NOTICE("--------------->recv system reboot request!!!");
                tal_sw_timer_create(tuya_gui_delay_reboot_cb,NULL,&delay_rb_timer_id);
                tal_sw_timer_start(delay_rb_timer_id,5000,TAL_TIMER_ONCE);
                return op_ret;
            }

        case LLV_EVENT_VENDOR_LCD_IDEL:      //GUI发送->屏幕长时间未被操作(超过10s以上),应用决定是否让屏幕进入休眠!
            //PR_NOTICE("--------------->recv tp no touched long time as '%u'sec [mem: %u]!!!\r\n", 
            //    evt->param, tal_system_get_free_heap_size());
            PR_NOTICE("--------------->screen entry saver mode !!!\r\n");
            tuya_disp_lcd_backlight_close();
            return op_ret;

        case LLV_EVENT_VENDOR_LCD_WAKE:      //GUI发送->屏幕长时间未被操作到被用户首次操作,应用决定是否唤醒屏幕!
            PR_NOTICE("--------------->recv tp detect wakeup !!!\r\n");
            tuya_disp_lcd_backlight_open();
            return op_ret;

        case LLV_EVENT_VENDOR_LANGUAGE_CHANGE:  //GUI发送->请求切换语言
            {
                language = (GUI_LANGUAGE_E)(evt->param);
                PR_NOTICE("--------------->recv language to '%d' @2!!!\r\n", language);
                Sevt.event = LLV_EVENT_VENDOR_LANGUAGE_CHANGE;
                Sevt.obj_type = LV_DISP_GUI_OBJ_OBJ;
                Sevt.tag = NULL;
                Sevt.param = (UINT32_T)language;
                tuya_disp_gui_event_send(&Sevt);            //通知GUI执行语言切换
                return op_ret;
            }

        case LLV_EVENT_VENDOR_LANGUAGE_INFO:    //GUI发送->请求获取当前系统语言类型
            {
                PR_NOTICE("--------------->recv requested default language as '%d' !!!\r\n", language);
                evt->param = (UINT32_T)language;            //语言类型
                return op_ret;
            }

        case LLV_EVENT_VENDOR_MF_TEST:          //GUI发送->请求获取当前是否产测模式
            {
                PR_NOTICE("--------------->recv requested mf_test mode as '%s' !!!\r\n", (__gui_mf_test==true)?"true":"false");
                evt->param = (UINT32_T)__gui_mf_test;      //进入产测模式标识
                return op_ret;
            }

        case LLV_EVENT_USER_PRIVATE:        //用户自定义事件()
            {
                //evt->param:用户传递事件的数据可由用户自定义(数据的结构可在tuya_app_gui_config.h中自定义)!
            #ifdef ENABLE_TUYA_AI_DEMO
                LV_USER_PRIVATE_EVENT_S *prv_evt_s = (LV_USER_PRIVATE_EVENT_S *)(evt->param);
                    if (prv_evt_s->event_typ == LV_USER_PRIVATE_EVENT_AI_TEXT) {
                        TY_DISPLAY_MSG_T *msg_data = (TY_DISPLAY_MSG_T *)(prv_evt_s->param1);
                         if(msg_data->type == TY_DISPLAY_TP_HUMAN_CHAT) {
                            if (msg_data->data != NULL){
                                PR_NOTICE("[%s][%d]---------recv keyboard ai text: '%s'\r\n", __func__, __LINE__, msg_data->data);
                                tkl_system_psram_free(msg_data->data);
                                msg_data->data = NULL;
                            }
                        }
                    }
                    else if (prv_evt_s->event_typ == LV_USER_PRIVATE_EVENT_AI_EXIT) {
                        PR_NOTICE("--------------->recv ai gui exit notify !!!\r\n");
                    }
            #endif
                return op_ret;
            }

        default:    //退出处理对象事件
            break;
    }
    return op_ret;
}

STATIC OPERATE_RET tuya_gui_obj_evt_hdl_cb(LV_DISP_EVENT_S *evt)
{
    OPERATE_RET op_ret = OPRT_OK;
#if 0
    LV_DISP_EVENT_S Sevt = {0};

    switch(evt->obj_type) {
        case LV_DISP_GUI_OBJ_SWITCH:
            PR_NOTICE("--------------->widget '%s' evt=%d %s", evt->tag, evt->event, evt->param?"on":"off");
            if (strcmp(evt->tag, "sw1") == 0) {
                STATIC BOOL_T val1 = TRUE;
                Sevt.event = LLV_EVENT_CLICKED;
                Sevt.obj_type = LV_DISP_GUI_OBJ_SWITCH;
                Sevt.tag = "sw4";
                Sevt.param = val1;
                val1 = !val1;
                tuya_disp_gui_event_send(&Sevt);
            #if 0
                {       //set language
                    Sevt.event = LLV_EVENT_VENDOR_LANGUAGE_CHANGE;
                    Sevt.obj_type = LV_DISP_GUI_OBJ_OBJ;
                    Sevt.tag = NULL;
                    Sevt.param = (UINT32_T)GUI_LG_ENGLISH;      //语言类型
                    tuya_disp_gui_event_send(&Sevt);
                }
            #endif
            }
            else if (strcmp(evt->tag, "sw4") == 0){         //LV_EVENT_CLICKED:只会触发事件,不会改变控件的状态变化;LV_EVENT_VALUE_CHANGED同时改变控件的状态变化!
                STATIC BOOL_T val2 = TRUE;
                Sevt.event = LLV_EVENT_CLICKED;
                Sevt.obj_type = LV_DISP_GUI_OBJ_SWITCH;
                Sevt.tag = "sw2";
                Sevt.param = val2;
                val2 = !val2;
                tuya_disp_gui_event_send(&Sevt);
            #if 0
                {       //set language
                    Sevt.event = LLV_EVENT_VENDOR_LANGUAGE_CHANGE;
                    Sevt.obj_type = LV_DISP_GUI_OBJ_OBJ;
                    Sevt.tag = NULL;
                    Sevt.param = (UINT32_T)GUI_LG_CHINESE;      //语言类型
                    tuya_disp_gui_event_send(&Sevt);
                }
            #endif
            }
            break;

        case LV_DISP_GUI_OBJ_OBJ:
            PR_NOTICE("--------------->widget '%s' evt='%d'", evt->tag, evt->event);
            break;

        default:
            break;
    }
#endif
    return op_ret;
}

STATIC OPERATE_RET tuya_gui_dp2obj_pre_init_hdl_cb(VOID)
{
    OPERATE_RET rt = OPRT_NOT_EXIST;
    LIST_HEAD gui_list_head;
    GUI_DISP_EVENT_LIST_S *gui_evt = NULL;

    if (get_gw_active() == ACTIVATED) {
        PR_NOTICE("[%s:%d]--------------->dev activated, ignore dp2obj pre init !\r\n", __func__, __LINE__);
        return rt;
    }

    PR_NOTICE("[%s:%d]--------------->dev not activated, start dp2obj pre init !\r\n", __func__, __LINE__);
    INIT_LIST_HEAD(&gui_list_head);
    if (!__gui_mf_test) {
        //init sw1 state
        gui_evt = (GUI_DISP_EVENT_LIST_S *)tal_malloc(sizeof(GUI_DISP_EVENT_LIST_S));
        memset(gui_evt, 0, sizeof(GUI_DISP_EVENT_LIST_S));
        gui_evt->evt.tag = "sw1";
        gui_evt->evt.obj_type = LV_DISP_GUI_OBJ_SWITCH;
        gui_evt->evt.event = LLV_EVENT_CLICKED;
        gui_evt->evt.param = 0;         //Off
        tuya_list_add_tail(&(gui_evt->node), &gui_list_head);
        //init sw2 state
        gui_evt = (GUI_DISP_EVENT_LIST_S *)tal_malloc(sizeof(GUI_DISP_EVENT_LIST_S));
        memset(gui_evt, 0, sizeof(GUI_DISP_EVENT_LIST_S));
        gui_evt->evt.tag = "sw2";
        gui_evt->evt.obj_type = LV_DISP_GUI_OBJ_SWITCH;
        gui_evt->evt.event = LLV_EVENT_CLICKED;
        gui_evt->evt.param = 0;         //Off
        tuya_list_add_tail(&(gui_evt->node), &gui_list_head);
        //init sw4 state
        gui_evt = (GUI_DISP_EVENT_LIST_S *)tal_malloc(sizeof(GUI_DISP_EVENT_LIST_S));
        memset(gui_evt, 0, sizeof(GUI_DISP_EVENT_LIST_S));
        gui_evt->evt.tag = "sw4";
        gui_evt->evt.obj_type = LV_DISP_GUI_OBJ_SWITCH;
        gui_evt->evt.event = LLV_EVENT_CLICKED;
        gui_evt->evt.param = 1;         //On
        tuya_list_add_tail(&(gui_evt->node), &gui_list_head);

        tuya_disp_gui_event_init(&gui_list_head);   //批量初始化屏幕控件
        rt = OPRT_OK;                               //返回成功,表示有事件发送,为防止刷新带来的屏幕闪烁,组件会适当延时亮屏!
    }
    //release gui node list
    LIST_HEAD *p_list = NULL;
    tuya_list_for_each(p_list, &gui_list_head) {
        gui_evt = tuya_list_entry(p_list, GUI_DISP_EVENT_LIST_S, node);
        DeleteNodeAndFree(gui_evt, node);
    }

    return rt;
}

STATIC DP_REPT_CB_DATA* tuya_gui_obj2dp_convert_hdl_cb(LV_DISP_EVENT_S *evt)
{
    OPERATE_RET rt = OPRT_COM_ERROR;
    DP_REPT_CB_DATA* dp_data = NULL;
    TY_OBJ_DP_REPT_S *obj_dp_s = NULL;
    TY_RAW_DP_REPT_S *raw_dp_s = NULL;

    switch(evt->obj_type) {
        case LV_DISP_GUI_OBJ_SWITCH:
            PR_NOTICE("%s:[%d]--------------->widget '%s' evt=%d %s", __func__, __LINE__, evt->tag, evt->event, evt->param?"on":"off");
            if (strcmp(evt->tag, "sw1") == 0) {         //sample code for obj dp!
                dp_data = (DP_REPT_CB_DATA*)Malloc(SIZEOF(DP_REPT_CB_DATA));
                if( dp_data == NULL){
                    PR_ERR("dp_data Malloc Failed");
                    goto err_out;
                }
                memset(dp_data, 0, SIZEOF(DP_REPT_CB_DATA));
                dp_data->dp_rept_type = T_OBJ_REPT;
                obj_dp_s = (TY_OBJ_DP_REPT_S *)Malloc(SIZEOF(TY_OBJ_DP_REPT_S));
                if( obj_dp_s == NULL){
                    PR_ERR("dp_data Malloc Failed");
                    goto err_out;
                }
                dp_data->dp_data = (VOID_T *)obj_dp_s;
                obj_dp_s->obj_dp.data = (TY_OBJ_DP_S *)Malloc(SIZEOF(TY_OBJ_DP_S));
                if( obj_dp_s->obj_dp.data == NULL){
                    PR_ERR("TY_OBJ_DP_S Malloc Failed");
                    goto err_out;
                }
                obj_dp_s->obj_dp.cnt = 1;
                memset(obj_dp_s->obj_dp.data, 0, SIZEOF(TY_OBJ_DP_S));
                //start fill data...
                TY_OBJ_DP_S* obj_dp_data = obj_dp_s->obj_dp.data;
                obj_dp_data->dpid = 1;             //测试产品中的dpid:1 ('开关')
                obj_dp_data->type = PROP_BOOL;
                obj_dp_data->value.dp_bool = evt->param?1:0;
                
            }
            else if (strcmp(evt->tag, "sw4") == 0){     //sample code for raw dp!
                dp_data = (DP_REPT_CB_DATA*)Malloc(SIZEOF(DP_REPT_CB_DATA));
                if( dp_data == NULL){
                    PR_ERR("dp_data Malloc Failed");
                    goto err_out;
                }
                memset(dp_data, 0, SIZEOF(DP_REPT_CB_DATA));
                dp_data->dp_rept_type = T_RAW_REPT;
                raw_dp_s = (TY_RAW_DP_REPT_S *)Malloc(SIZEOF(TY_RAW_DP_REPT_S));
                if( raw_dp_s == NULL){
                    PR_ERR("raw_dp_data Malloc Failed");
                    goto err_out;
                }
                dp_data->dp_data = (VOID_T *)raw_dp_s;
                memset(raw_dp_s, 0, SIZEOF(TY_RAW_DP_REPT_S));
                raw_dp_s->dpid = 30;        //测试产品中的dpid:30 ('云食谱ID')
                raw_dp_s->len = 255;
                raw_dp_s->data = (BYTE_T*)Malloc(raw_dp_s->len);
                if( raw_dp_s->data == NULL){
                    PR_ERR("TY_RAW_DP_S Malloc Failed");
                    goto err_out;
                }
                UINT32_T test_raw = 0x11223344;
                memset(raw_dp_s->data, 0, raw_dp_s->len);
                memcpy(raw_dp_s->data, &test_raw, raw_dp_s->len);
            }
            else {
                PR_WARN("no need to process obj tag '%s'", evt->tag);
                goto err_out;
            }
            rt = OPRT_OK;
            break;

        default:
            PR_ERR("unknown obj_type '%d'", evt->obj_type);
            break;
    }

    err_out:
    if (rt != OPRT_OK) {
        if (dp_data != NULL)
            tuya_gui_dp_source_release(dp_data);
        dp_data = NULL;
    }
    return dp_data;
}

STATIC OPERATE_RET tuya_gui_lcd_driver_init(VOID *_info)
{
    STATIC BOOL_T lcd_driver_inited = FALSE;

    if (lcd_driver_inited) {
        PR_NOTICE("%s-%d:------------------lcd dirver *re-inited* !", __func__, __LINE__);
        return OPRT_OK;
    }
#ifdef TUYA_MULTI_TYPES_LCD
        GUI_LCDS_INFO_S *info = (GUI_LCDS_INFO_S *)_info;
        UINT32_T i = 0;
    
    #if defined(T5AI_BOARD_EYES) && T5AI_BOARD_EYES == 1
        info->exp_type = TUYA_SCREEN_EXPANSION_V_EXP;         //初始化为双屏幕扩展
        info->lcd_num = 2;                                   //初始化为2个屏幕(最大为:2)
    #else
        info->exp_type = TUYA_SCREEN_EXPANSION_NONE;         //初始化为无扩展
        info->lcd_num = 1;                                   //初始化为1个屏幕(最大为:2)
    #endif

        if (info->lcd_num < 1 || info->lcd_num > 2) {
            PR_ERR("%s-%d:------------------lcd num '%d' config error ???", __func__, __LINE__, info->lcd_num);
            return OPRT_COM_ERROR;
        }
        info->lcd_info = (GUI_LCD_INFO_S **)Malloc(SIZEOF(GUI_LCD_INFO_S *) * info->lcd_num);
        if (info->lcd_info == NULL) {
            PR_ERR("%s-%d:------------------malloc fail?", __func__, __LINE__);
            return OPRT_COM_ERROR;
        }
        memset(info->lcd_info, 0, SIZEOF(GUI_LCD_INFO_S *) * info->lcd_num);
        for (i = 0; i < info->lcd_num; i++) {
            info->lcd_info[i] = (GUI_LCD_INFO_S *)Malloc(SIZEOF(GUI_LCD_INFO_S));
            if (info->lcd_info[i] == NULL) {
                PR_ERR("%s-%d:------------------malloc fail?", __func__, __LINE__);
                return OPRT_COM_ERROR;
            }
            memset(info->lcd_info[i], 0, SIZEOF(GUI_LCD_INFO_S));
        }
    
    #if defined(USING_TUYA_T5_MAIN_BOARD)                                //3.5英寸RGB接口绿色开发版(LCD module: T35P128CQ)
            //屏幕驱动初始化:
            static const ty_display_cfg t35p128cq_cfg = {
                .rgb_cfg = {
                  .spi_clk = TUYA_GPIO_NUM_33,
                  .spi_csx = TUYA_GPIO_NUM_34,
                  .spi_sda = TUYA_GPIO_NUM_35,
                  .bl = {
                    .pin = TUYA_GPIO_NUM_32,
                    .active_level = TUYA_GPIO_LEVEL_HIGH
                  },
                  .reset = {
                    .pin = TUYA_GPIO_NUM_MAX,
                  },
                  .power_ctrl = {
                    .pin = TUYA_GPIO_NUM_MAX,
                  }
                }
            };
            GUI_LCD_INFO_S *lcd_info = info->lcd_info[0];
    
            lcd_info->display_device = tdd_lcd_driver_query("rgb_ili9488", DISPLAY_RGB);
            lcd_info->display_cfg = &t35p128cq_cfg;                          //选择指定的屏幕设备GPIO配置
            //TP驱动初始化:
        #ifdef TUYA_APP_DRIVERS_TP
            ty_tp_usr_cfg_t tp_cfg = {
                .tp_i2c_clk = {
                    .pin = TUYA_GPIO_NUM_38,
                },
                .tp_i2c_sda = {
                    .pin = TUYA_GPIO_NUM_39,
                }, 
                .tp_rst = {
                    .pin = TUYA_GPIO_NUM_9,
                    .active_level = TUYA_GPIO_LEVEL_HIGH,
                },
                .tp_intr = {
                    .pin = TUYA_GPIO_NUM_8,                             //该中断脚需同步到配置文件app_resource_config.json中的"gpio_id".
                },
                .tp_pwr_ctrl = {
                    .pin = TUYA_GPIO_NUM_MAX,
                    .active_level = TUYA_GPIO_LEVEL_HIGH,
                },
                .tp_i2c_idx = TUYA_I2C_NUM_2,
            };
            extern const ty_tp_device_cfg_t tp_gt1151_device;
            tkl_lvgl_tp_handle_register((VOID *)&tp_gt1151_device, (VOID *)&tp_cfg);
        #else
            extern void tkl_disp_update_ll_config(void *config);
            TKL_DISP_LL_CTRL_S ll_ctrl = { 0 };
            ll_ctrl.tp.tp_i2c_clk = TUYA_GPIO_NUM_38;
            ll_ctrl.tp.tp_i2c_sda = TUYA_GPIO_NUM_39;
            ll_ctrl.tp.tp_rst = TUYA_GPIO_NUM_9;
            ll_ctrl.tp.tp_intr = TUYA_GPIO_NUM_8;                     //该中断脚需同步到配置文件app_resource_config.json中的"gpio_id".
            tkl_disp_update_ll_config((void *)&ll_ctrl);
        #endif
    #elif defined(USING_TUYA_T5_E1_IPEX_DEV_BOARD)                  //5英寸RGB接口黑色开发板(LCD module: T50P181CQ)
            //屏幕驱动初始化:
            static const ty_display_cfg t50p181cq_cfg = {
                .rgb_cfg = {
                  .spi_clk = TUYA_GPIO_NUM_35,
                  .spi_csx = TUYA_GPIO_NUM_34,
                  .spi_sda = TUYA_GPIO_NUM_32,
                  .bl = {
                    .pin = TUYA_GPIO_NUM_36,
                    .active_level = TUYA_GPIO_LEVEL_HIGH
                  },
                  .reset = {
                    .pin = TUYA_GPIO_NUM_28,
                  },
                  .power_ctrl = {
                    .pin = TUYA_GPIO_NUM_MAX,
                  }
                }
            };
            GUI_LCD_INFO_S *lcd_info = info->lcd_info[0];
    
            lcd_info->display_device = tdd_lcd_driver_query("rgb_st7701sn", DISPLAY_RGB);
            lcd_info->display_cfg = &t50p181cq_cfg;                          //选择指定的屏幕设备GPIO配置
            //TP驱动初始化:
        #ifdef TUYA_APP_DRIVERS_TP
            ty_tp_usr_cfg_t tp_cfg = {
                .tp_i2c_clk = {
                    .pin = TUYA_GPIO_NUM_13,
                },
                .tp_i2c_sda = {
                    .pin = TUYA_GPIO_NUM_15,
                }, 
                .tp_rst = {
                    .pin = TUYA_GPIO_NUM_27,
                    .active_level = TUYA_GPIO_LEVEL_HIGH,
                },
                .tp_intr = {
                    .pin = TUYA_GPIO_NUM_38,                            //该中断脚需同步到配置文件app_resource_config.json中的"gpio_id".
                },
                .tp_pwr_ctrl = {
                    .pin = TUYA_GPIO_NUM_MAX,
                    .active_level = TUYA_GPIO_LEVEL_HIGH,
                },
                .tp_i2c_idx = TUYA_I2C_NUM_2,
            };
            extern const ty_tp_device_cfg_t tp_gt1151_device;
            tkl_lvgl_tp_handle_register((VOID *)&tp_gt1151_device, (VOID *)&tp_cfg);
        #else
            extern void tkl_disp_update_ll_config(void *config);
            TKL_DISP_LL_CTRL_S ll_ctrl = { 0 };
            ll_ctrl.tp.tp_i2c_clk = TUYA_GPIO_NUM_13;
            ll_ctrl.tp.tp_i2c_sda = TUYA_GPIO_NUM_15;
            ll_ctrl.tp.tp_rst = TUYA_GPIO_NUM_27;
            ll_ctrl.tp.tp_intr = TUYA_GPIO_NUM_38;                      //该中断脚需同步到配置文件app_resource_config.json中的"gpio_id".
            tkl_disp_update_ll_config((void *)&ll_ctrl);
        #endif
    #elif defined(USING_TUYA_T5AI_BOARD) || defined(T5AI_BOARD)                     //3.5英寸RGB接口T5AI开发板(LCD module: T35P128CQ)
            //屏幕驱动初始化:
            static const ty_display_cfg cfg1 = {
              .rgb_cfg = {
                .spi_clk = TUYA_GPIO_NUM_49,
                .spi_csx = TUYA_GPIO_NUM_48,
                .spi_sda = TUYA_GPIO_NUM_50,
                .bl = {
                  .pin = TUYA_GPIO_NUM_9,
                  .active_level = TUYA_GPIO_LEVEL_HIGH
                },
                .reset = {
                  .pin = TUYA_GPIO_NUM_53,
                },
                .power_ctrl = {
                  .pin = TUYA_GPIO_NUM_MAX,
                }
              }
            };
            GUI_LCD_INFO_S *lcd_info = info->lcd_info[0];
    
            lcd_info->display_device = tdd_lcd_driver_query("rgb_ili9488", DISPLAY_RGB);
            lcd_info->display_cfg = &cfg1;                                   //选择指定的屏幕设备GPIO配置
            //TP驱动初始化:
        #ifdef TUYA_APP_DRIVERS_TP
            ty_tp_usr_cfg_t tp_cfg = {
                .tp_i2c_clk = {
                    .pin = TUYA_GPIO_NUM_13,
                },
                .tp_i2c_sda = {
                    .pin = TUYA_GPIO_NUM_15,
                }, 
                .tp_rst = {
                    .pin = TUYA_GPIO_NUM_54,
                    .active_level = TUYA_GPIO_LEVEL_HIGH,
                },
                .tp_intr = {
                    .pin = TUYA_GPIO_NUM_55,                            //该中断脚需同步到配置文件app_resource_config.json中的"gpio_id".
                },
                .tp_pwr_ctrl = {
                    .pin = TUYA_GPIO_NUM_MAX,
                    .active_level = TUYA_GPIO_LEVEL_HIGH,
                },
                .tp_i2c_idx = TUYA_I2C_NUM_2,
            };
            extern const ty_tp_device_cfg_t tp_gt1151_device;
            tkl_lvgl_tp_handle_register((VOID *)&tp_gt1151_device, (VOID *)&tp_cfg);
        #else
            extern void tkl_disp_update_ll_config(void *config);
            TKL_DISP_LL_CTRL_S ll_ctrl = { 0 };
            ll_ctrl.tp.tp_i2c_clk = TUYA_GPIO_NUM_13;
            ll_ctrl.tp.tp_i2c_sda = TUYA_GPIO_NUM_15;
            ll_ctrl.tp.tp_rst = TUYA_GPIO_NUM_54;
            ll_ctrl.tp.tp_intr = TUYA_GPIO_NUM_55;                     //该中断脚需同步到配置文件app_resource_config.json中的"gpio_id".
            tkl_disp_update_ll_config((void *)&ll_ctrl);
        #endif
    #elif defined(USING_FRD028UQV1M_PCBA_BOARD)                                             //2.4英寸8080接口开发板(LCD module: FRD240B28009-A)
            //屏幕驱动初始化:
            static const ty_display_cfg i8080_st7789P3_cfg = {
                .mcu8080_cfg = {
                    .power_ctrl = {
                      .pin = TUYA_GPIO_NUM_48,
                      .active_level = TUYA_GPIO_LEVEL_HIGH
                    },
                    .bl = {
                      .pin = TUYA_GPIO_NUM_19,
                      .active_level = TUYA_GPIO_LEVEL_HIGH
                    },
                    .te_pin = TUYA_GPIO_NUM_MAX,
                  }
            };
            GUI_LCD_INFO_S *lcd_info = info->lcd_info[0];
    
            lcd_info->display_device = tdd_lcd_driver_query("8080_st7789P3", DISPLAY_8080);
            lcd_info->display_cfg = &i8080_st7789P3_cfg;                         //选择指定的屏幕设备GPIO配置
            //TP驱动初始化::TODO...
    #elif defined(USING_TUYA_T5AI_EVB_BOARD) || defined(T5AI_BOARD_EVB)                        //1.54英寸SPI接口开发板(LCD module: GMT154-7P)
            //屏幕驱动初始化:
            //screen 1
            static const ty_display_cfg spi_st7789P3_cfg0 = {
                .spi_cfg = {
                    .port = TUYA_SPI_NUM_0,
                    .reset = {
                        .pin = TUYA_GPIO_NUM_6,
                        .active_level = TUYA_GPIO_LEVEL_LOW
                    },
                    .bl = {
                       .pin = TUYA_GPIO_NUM_5,
                       .active_level = TUYA_GPIO_LEVEL_HIGH
                    },
                    .power_ctrl = {
                        .pin = TUYA_GPIO_NUM_7,
                        .active_level = TUYA_GPIO_LEVEL_HIGH
                    },
                    .rs = {
                        .pin = TUYA_GPIO_NUM_17,
                        .active_level = TUYA_GPIO_LEVEL_LOW
                    },
                    .soft_cs = {
                        .pin = TUYA_GPIO_NUM_MAX,                       // 不使用一定要配置成无效设置!!!
                        .active_level = TUYA_GPIO_LEVEL_LOW
                    }
                }
            };
            GUI_LCD_INFO_S *lcd_info = info->lcd_info[0];
    
            lcd_info->display_device = tdd_lcd_driver_query(TUYA_LCD_IC_NAME, DISPLAY_SPI);
            lcd_info->display_cfg = &spi_st7789P3_cfg0;                         //选择指定的屏幕设备GPIO配置
            //screen 2
            if (info->lcd_num > 1) {
                static const ty_display_cfg spi_st7789P3_cfg1 = {
                    .spi_cfg = {
                        .port = TUYA_SPI_NUM_1,
                        .reset = {
                            .pin = TUYA_GPIO_NUM_26,
                            .active_level = TUYA_GPIO_LEVEL_LOW
                        },
                        .bl = {
                           .pin = TUYA_GPIO_NUM_46,
                           .active_level = TUYA_GPIO_LEVEL_HIGH
                        },
                        .power_ctrl = {
                            .pin = TUYA_GPIO_NUM_7,
                            .active_level = TUYA_GPIO_LEVEL_HIGH
                        },
                        .rs = {
                            .pin = TUYA_GPIO_NUM_24,
                            .active_level = TUYA_GPIO_LEVEL_LOW
                        },
                        .soft_cs = {
                            .pin = TUYA_GPIO_NUM_MAX,                       // 不使用一定要配置成无效设置!!!
                            .active_level = TUYA_GPIO_LEVEL_LOW
                        }
                    }
                };
                lcd_info = info->lcd_info[1];
                lcd_info->display_device = tdd_lcd_driver_query(TUYA_LCD_IC_NAME, DISPLAY_SPI);
                lcd_info->display_cfg = &spi_st7789P3_cfg1;                         //选择指定的屏幕设备GPIO配置
            }
            //TP驱动初始化::TODO...
    #elif defined(USING_TUYA_T5_QSPI_DEV_BOARD)                                             //4.3英寸QSPI接口开发板(LCD module: NV3041)
            //屏幕驱动初始化:
            static const ty_display_cfg qspi_nv3041_cfg0 = {
                  .qspi_cfg = {
                    .port = TUYA_QSPI_NUM_0,
                    .bl = {
                      .pin = TUYA_GPIO_NUM_MAX,
                    },
                    .power_ctrl = {
                      .pin = TUYA_GPIO_NUM_MAX,
                    },
                    .reset = {
                      .pin = TUYA_GPIO_NUM_5,
                      .active_level = TUYA_GPIO_LEVEL_LOW
                    }
                  }
            };
            GUI_LCD_INFO_S *lcd_info = info->lcd_info[0];
            
            lcd_info->display_device = tdd_lcd_driver_query("qspi_nv3041", DISPLAY_QSPI);
            lcd_info->display_cfg = &qspi_nv3041_cfg0;                         //选择指定的屏幕设备GPIO配置
            //TP驱动初始化::TODO...
    #elif defined(T5AI_BOARD_DESKTOP)
            //屏幕驱动初始化:
            static const ty_display_cfg spi_st7789v2_cfg0 = {
                .spi_cfg = {
                    .port = TUYA_SPI_NUM_0,
                    .reset = {
                        .pin = TUYA_GPIO_NUM_43,
                        .active_level = TUYA_GPIO_LEVEL_LOW
                    },
                    .bl = {
                       .pin = TUYA_GPIO_NUM_42,
                       .active_level = TUYA_GPIO_LEVEL_HIGH
                    },
                    .power_ctrl = {
                        .pin = TUYA_GPIO_NUM_5,
                        .active_level = TUYA_GPIO_LEVEL_HIGH
                    },
                    .rs = {
                        .pin = TUYA_GPIO_NUM_47,
                        .active_level = TUYA_GPIO_LEVEL_LOW
                    },
                    .soft_cs = {
                        .pin = TUYA_GPIO_NUM_MAX,                       // 不使用一定要配置成无效设置!!!
                        .active_level = TUYA_GPIO_LEVEL_LOW
                    }
                }
            };
            GUI_LCD_INFO_S *lcd_info = info->lcd_info[0];
    
            lcd_info->display_device = tdd_lcd_driver_query("spi_st7789v2", DISPLAY_SPI);
            lcd_info->display_cfg = &spi_st7789v2_cfg0; 

            ty_tp_usr_cfg_t tp_cfg = {
                .tp_i2c_clk = {
                    .pin = TUYA_GPIO_NUM_20,
                },
                .tp_i2c_sda = {
                    .pin = TUYA_GPIO_NUM_21,
                }, 
                .tp_rst = {
                    .pin = TUYA_GPIO_NUM_53,
                    .active_level = TUYA_GPIO_LEVEL_LOW,
                },
                .tp_intr = {
                    .pin = TUYA_GPIO_NUM_54,                             //该中断脚需同步到配置文件app_resource_config.json中的"gpio_id".
                },
                .tp_pwr_ctrl = {
                    .pin = TUYA_GPIO_NUM_MAX,
                    .active_level = TUYA_GPIO_LEVEL_HIGH,
                },
                .tp_i2c_idx = TUYA_I2C_NUM_0,
            };
            extern const ty_tp_device_cfg_t tp_cst816x_device;
            tkl_lvgl_tp_handle_register((VOID *)&tp_cst816x_device, (VOID *)&tp_cfg);

    #elif defined(T5AI_BOARD_EYES)
            //屏幕驱动初始化:
            //screen 1
            static const ty_display_cfg spi_st7735s_cfg0 = {
                .spi_cfg = {
                    .port = TUYA_SPI_NUM_2,
                    .reset = {
                        .pin = TUYA_GPIO_NUM_6,
                        .active_level = TUYA_GPIO_LEVEL_LOW
                    },
                    .bl = {
                       .pin = TUYA_GPIO_NUM_25,
                       .active_level = TUYA_GPIO_LEVEL_HIGH
                    },
                    .power_ctrl = {
                        .pin = TUYA_GPIO_NUM_MAX,
                    },
                    .rs = {
                        .pin = TUYA_GPIO_NUM_7,
                        .active_level = TUYA_GPIO_LEVEL_LOW
                    },
                    .soft_cs = {
                        .pin = TUYA_GPIO_NUM_MAX,                       // 不使用一定要配置成无效设置!!!
                    }
                }
            };
            GUI_LCD_INFO_S *lcd_info = info->lcd_info[0];
    
            lcd_info->display_device = tdd_lcd_driver_query("spi_st7735s", DISPLAY_SPI);
            lcd_info->display_cfg = &spi_st7735s_cfg0;                         //选择指定的屏幕设备GPIO配置
            //screen 2
            if (info->lcd_num > 1) {
                static const ty_display_cfg spi_st7735s_cfg1 = {
                    .spi_cfg = {
                        .port = TUYA_SPI_NUM_3,
                        .reset = {
                            .pin = TUYA_GPIO_NUM_45,
                            .active_level = TUYA_GPIO_LEVEL_LOW
                        },
                        .bl = {
                           .pin = TUYA_GPIO_NUM_25,
                           .active_level = TUYA_GPIO_LEVEL_HIGH
                        },
                        .power_ctrl = {
                            .pin = TUYA_GPIO_NUM_MAX,
                            .active_level = TUYA_GPIO_LEVEL_HIGH
                        },
                        .rs = {
                            .pin = TUYA_GPIO_NUM_5,
                            .active_level = TUYA_GPIO_LEVEL_LOW
                        },
                        .soft_cs = {
                            .pin = TUYA_GPIO_NUM_MAX,                       // 不使用一定要配置成无效设置!!!
                            .active_level = TUYA_GPIO_LEVEL_LOW
                        }
                    }
                };
                lcd_info = info->lcd_info[1];
                lcd_info->display_device = tdd_lcd_driver_query("spi_st7735s", DISPLAY_SPI);
                lcd_info->display_cfg = &spi_st7735s_cfg1;                         //选择指定的屏幕设备GPIO配置
            }
            tkl_lvgl_display_offset_set(0, 0x20); //! set x+y offset
    #elif defined(T5AI_BOARD_EVB_PRO)

            //screen 1
            static const ty_display_cfg spi_st7789_cfg0 = {
                .spi_cfg = {
                    .port = TUYA_SPI_NUM_0,
                    .reset = {
                        .pin = TUYA_GPIO_NUM_18,
                        .active_level = TUYA_GPIO_LEVEL_LOW
                    },
                    .bl = {
                       .pin = TUYA_GPIO_NUM_19,
                       .active_level = TUYA_GPIO_LEVEL_HIGH
                    },
                    .power_ctrl = {
                        .pin = TUYA_GPIO_NUM_17,
                        .active_level = TUYA_GPIO_LEVEL_HIGH
                    },
                    .rs = {
                        .pin = TUYA_GPIO_NUM_47,
                        .active_level = TUYA_GPIO_LEVEL_LOW
                    },
                    .soft_cs = {
                        .pin = TUYA_GPIO_NUM_MAX,                       // 不使用一定要配置成无效设置!!!
                    }
                }
            };

            tkl_io_pinmux_config(TUYA_IO_PIN_45, TUYA_SPI0_CS);
            tkl_io_pinmux_config(TUYA_IO_PIN_44, TUYA_SPI0_CLK);
            tkl_io_pinmux_config(TUYA_IO_PIN_46, TUYA_SPI0_MOSI);
            tkl_io_pinmux_config(TUYA_IO_PIN_47, TUYA_SPI0_MISO);
            GUI_LCD_INFO_S *lcd_info = info->lcd_info[0];
            
            lcd_info->display_device = tdd_lcd_driver_query(TUYA_LCD_IC_NAME, DISPLAY_SPI);
            lcd_info->display_cfg = &spi_st7789_cfg0; 

    #elif defined(T5AI_BOARD_ROBOT)
            //屏幕驱动初始化:
            static const ty_display_cfg spi_st7789p3_cfg0 = {
                .spi_cfg = {
                    .port = TUYA_SPI_NUM_0,
                    .reset = {
                        .pin = TUYA_GPIO_NUM_16,
                        .active_level = TUYA_GPIO_LEVEL_LOW
                    },
                    .bl = {
                       .pin = TUYA_GPIO_NUM_14,
                       .active_level = TUYA_GPIO_LEVEL_HIGH
                    },
                    .power_ctrl = {
                        .pin = TUYA_GPIO_NUM_19,
                        .active_level = TUYA_GPIO_LEVEL_HIGH
                    },
                    .rs = {
                        .pin = TUYA_GPIO_NUM_47,
                        .active_level = TUYA_GPIO_LEVEL_LOW
                    },
                    .soft_cs = {
                        .pin = TUYA_GPIO_NUM_MAX,                       // 不使用一定要配置成无效设置!!!
                        .active_level = TUYA_GPIO_LEVEL_LOW
                    }
                }
            };

            tkl_io_pinmux_config(TUYA_IO_PIN_45, TUYA_SPI0_CS);
            tkl_io_pinmux_config(TUYA_IO_PIN_44, TUYA_SPI0_CLK);
            tkl_io_pinmux_config(TUYA_IO_PIN_46, TUYA_SPI0_MOSI);
            tkl_io_pinmux_config(TUYA_IO_PIN_47, TUYA_SPI0_MISO);

            GUI_LCD_INFO_S *lcd_info = info->lcd_info[0];

            tkl_lvgl_display_offset_set(0, 0x22); //! set x+y offset
    
            lcd_info->display_device = tdd_lcd_driver_query("spi_st7789p3", DISPLAY_SPI);
            lcd_info->display_cfg = &spi_st7789p3_cfg0; 
            
    #else
        #error Unknown lcd_driver_info
    #endif
        //show lcd info
        for (i = 0; i < info->lcd_num; i++) {
            GUI_LCD_INFO_S *lcd_info = info->lcd_info[i];
            if (lcd_info->display_device != NULL) {
                PR_INFO("%s-%d: find %d/%d lcd disp driver type '%d', name '%s'", __func__, __LINE__,
                    i+1, info->lcd_num, lcd_info->display_device->type, lcd_info->display_device->name);
            }
            else {
                PR_ERR("%s-%d: No. %d lcd disp driver is empty ?", __func__, __LINE__, i);
                return OPRT_COM_ERROR;
            }
        }
#else
        TKL_DISP_INFO_S *info = (TKL_DISP_INFO_S *)_info;
        TKL_DISP_ROTATION_E lcd_rotation = TKL_DISP_ROTATION_0;
    
        memset((VOID *)info, 0, sizeof(TKL_DISP_INFO_S));
        info->width = TUYA_LCD_WIDTH;
        info->height = TUYA_LCD_HEIGHT;
        int len = (IC_NAME_LENGTH < strlen(TUYA_LCD_IC_NAME))? IC_NAME_LENGTH: strlen(TUYA_LCD_IC_NAME);
        memcpy(info->ll_ctrl.ic_name, TUYA_LCD_IC_NAME, len);
        info->ll_ctrl.enable_lcd_pipeline = 1;       //使能lcd pipeline
    
        switch (TUYA_LCD_ROTATION) {
            case TUYA_SCREEN_ROTATION_0:
                lcd_rotation = TKL_DISP_ROTATION_0;
                break;
    
            case TUYA_SCREEN_ROTATION_90:
                lcd_rotation = TKL_DISP_ROTATION_90;
                break;
    
            case TUYA_SCREEN_ROTATION_180:
                lcd_rotation = TKL_DISP_ROTATION_180;
                break;
    
            case TUYA_SCREEN_ROTATION_270:
                lcd_rotation = TKL_DISP_ROTATION_270;
                break;
    
            default:
                lcd_rotation = TKL_DISP_ROTATION_0;
                break;
        }
    
    #if defined(USING_TUYA_T5_MAIN_BOARD)                               //3.5英寸RGB接口绿色开发版(LCD module: T35P128CQ)
            info->fps = 15;
            info->format = TKL_DISP_PIXEL_FMT_RGB565;
            info->rotation = lcd_rotation;
        
            info->ll_ctrl.bl.mode            = TKL_DISP_BL_GPIO;
            info->ll_ctrl.bl.io              = TUYA_GPIO_NUM_32;            //屏幕背光的控制!
            info->ll_ctrl.bl.active_level    = TUYA_GPIO_LEVEL_HIGH;
            info->ll_ctrl.spi.clk            = TUYA_GPIO_NUM_33;
            info->ll_ctrl.spi.csx            = TUYA_GPIO_NUM_34;
            info->ll_ctrl.spi.sda            = TUYA_GPIO_NUM_35;
            info->ll_ctrl.spi.rst_mode       = TKL_DISP_POWERON_RESET;
            info->ll_ctrl.spi.rst            = TUYA_GPIO_NUM_MAX;           //屏幕的复位控制脚!(屏幕没有复位控制脚,就配置成一个无效的脚)
            info->ll_ctrl.power_ctrl_pin     = TUYA_GPIO_NUM_MAX;           //屏幕的电源控制脚!(屏幕没有电源控制脚,就配置成一个无效的脚)
            info->ll_ctrl.power_active_level = TUYA_GPIO_LEVEL_HIGH;
            info->ll_ctrl.rgb_mode           = TKL_DISP_PIXEL_FMT_RGB565;
            info->ll_ctrl.rst_pin            = TUYA_GPIO_NUM_MAX;           //复位脚!(屏幕没有复位控制脚,就配置成一个无效的脚)
            //设置TP引脚
            info->ll_ctrl.tp.tp_i2c_clk = TUYA_GPIO_NUM_38;
            info->ll_ctrl.tp.tp_i2c_sda = TUYA_GPIO_NUM_39;
            info->ll_ctrl.tp.tp_rst = TUYA_GPIO_NUM_9;
            info->ll_ctrl.tp.tp_intr = TUYA_GPIO_NUM_8;                      //该中断脚需同步到配置文件app_resource_config.json中的"gpio_id".
            //屏幕驱动选择:底层开发环境有对应的屏幕就将该参数置空,否者上层app driver实现!(调用该函数接口指定)
            //info.ll_ctrl.init_param = tdd_lcd_driver_query(TUYA_LCD_IC_NAME, 0);
    #elif defined(USING_TUYA_T5_E1_IPEX_DEV_BOARD)                      //5英寸RGB接口黑色开发板(LCD module: T50P181CQ)
            info->fps = 15;
            info->format = TKL_DISP_PIXEL_FMT_RGB565;
            info->rotation = lcd_rotation;
        
            info->ll_ctrl.bl.mode            = TKL_DISP_BL_GPIO;
            info->ll_ctrl.bl.io              = TUYA_GPIO_NUM_36;            //屏幕背光的控制!
            info->ll_ctrl.bl.active_level    = TUYA_GPIO_LEVEL_HIGH;
            info->ll_ctrl.spi.clk            = TUYA_GPIO_NUM_35;
            info->ll_ctrl.spi.csx            = TUYA_GPIO_NUM_34;
            info->ll_ctrl.spi.sda            = TUYA_GPIO_NUM_32;
            info->ll_ctrl.spi.rst_mode       = TKL_DISP_GPIO_RESET;
            info->ll_ctrl.spi.rst            = TUYA_GPIO_NUM_28;            //屏幕的复位控制脚!
            info->ll_ctrl.power_ctrl_pin     = TUYA_GPIO_NUM_MAX;           //屏幕的电源控制脚!(屏幕没有电源控制脚,就配置成一个无效的脚)
            info->ll_ctrl.power_active_level = TUYA_GPIO_LEVEL_HIGH;
            info->ll_ctrl.rgb_mode           = TKL_DISP_PIXEL_FMT_RGB888;
            info->ll_ctrl.rst_pin            = TUYA_GPIO_NUM_MAX;           //复位脚!(屏幕没有复位控制脚,就配置成一个无效的脚)
            //设置TP引脚
            info->ll_ctrl.tp.tp_i2c_clk = TUYA_GPIO_NUM_13;
            info->ll_ctrl.tp.tp_i2c_sda = TUYA_GPIO_NUM_15;
            info->ll_ctrl.tp.tp_rst = TUYA_GPIO_NUM_27;
            info->ll_ctrl.tp.tp_intr = TUYA_GPIO_NUM_38;                     //该中断脚需同步到配置文件app_resource_config.json中的"gpio_id".
            //屏幕驱动选择:底层开发环境有对应的屏幕就将该参数置空,否者上层app driver实现!(调用该函数接口指定)
            info->ll_ctrl.init_param = tdd_lcd_driver_query(TUYA_LCD_IC_NAME, 0);
    #elif defined(USING_TUYA_T5AI_BOARD) || defined(T5AI_BOARD)                                    //3.5英寸RGB接口T5AI开发板(LCD module: T35P128CQ)
            info->fps = 15;
            info->format = TKL_DISP_PIXEL_FMT_RGB565;
            info->rotation = lcd_rotation;
    
            info->ll_ctrl.bl.mode            = TKL_DISP_BL_GPIO;
            info->ll_ctrl.bl.io              = TUYA_GPIO_NUM_9;            //屏幕背光的控制!
            info->ll_ctrl.bl.active_level    = TUYA_GPIO_LEVEL_HIGH;
            info->ll_ctrl.spi.clk            = TUYA_GPIO_NUM_49;
            info->ll_ctrl.spi.csx            = TUYA_GPIO_NUM_48;
            info->ll_ctrl.spi.sda            = TUYA_GPIO_NUM_50;
            info->ll_ctrl.spi.rst_mode       = TKL_DISP_GPIO_RESET;
            info->ll_ctrl.spi.rst            = TUYA_GPIO_NUM_MAX;           //屏幕的复位控制脚!(屏幕没有电源控制脚,就配置成一个无效的脚)
            info->ll_ctrl.power_ctrl_pin     = TUYA_GPIO_NUM_MAX;           //屏幕的电源控制脚!(屏幕没有电源控制脚,就配置成一个无效的脚)
            info->ll_ctrl.power_active_level = TUYA_GPIO_LEVEL_HIGH;
            info->ll_ctrl.rgb_mode           = TKL_DISP_PIXEL_FMT_RGB565;
            info->ll_ctrl.rst_pin            = TUYA_GPIO_NUM_53;
            info->ll_ctrl.rst_active_level   = TUYA_GPIO_LEVEL_LOW;
            //设置TP引脚
            info->ll_ctrl.tp.tp_i2c_clk = TUYA_GPIO_NUM_13;
            info->ll_ctrl.tp.tp_i2c_sda = TUYA_GPIO_NUM_15;
            info->ll_ctrl.tp.tp_rst = TUYA_GPIO_NUM_54;
            info->ll_ctrl.tp.tp_intr = TUYA_GPIO_NUM_55;                     //该中断脚需同步到配置文件app_resource_config.json中的"gpio_id".
    
            //屏幕驱动选择:底层开发环境有对应的屏幕就将该参数置空,否者上层app driver实现!(调用该函数接口指定)
            info->ll_ctrl.init_param = NULL;
    
            // 拉高 lcd rst 引脚
            TUYA_GPIO_BASE_CFG_T gpio_cfg = {
                .direct = TUYA_GPIO_OUTPUT,
                .mode = TUYA_GPIO_PULLUP,
                .level = TUYA_GPIO_LEVEL_HIGH,
            };
            tkl_gpio_init(TUYA_GPIO_NUM_53, &gpio_cfg);
            tkl_gpio_write(TUYA_GPIO_NUM_53, 1);
    #endif
#endif
    lcd_driver_inited = TRUE;
    return OPRT_OK;
}

STATIC OPERATE_RET tuya_gui_poweron_page_quick_start(VOID_T)
{
#ifdef POWER_ON_PAGE_START
    extern const uint8_t _11_map[];
    //extern const uint8_t _22_map[];
    //extern const uint8_t _33_map[];
    //extern const uint8_t _44_map[];
    //extern const uint8_t _55_map[];
    static UCHAR_T *img_bin[] = { _11_map };
#else
    static UCHAR_T **img_bin = NULL;
#endif
    OPERATE_RET rt = OPRT_OK;

#ifdef TUYA_MULTI_TYPES_LCD
    GUI_LCDS_INFO_S info = { 0 };
#else
    TKL_DISP_INFO_S info;
#endif

    PR_NOTICE("%s-%d, run time here :'%d ms'", __func__, __LINE__, tal_system_get_millisecond());
    rt = tuya_gui_lcd_driver_init((VOID *)&info);
    if (rt != OPRT_OK) {
        PR_ERR("lcd driver init fail ???");
        return rt;
    }

    rt = tuya_gui_lcd_open((VOID *)&info);
    if (rt != OPRT_OK) {
        PR_ERR("lcd open fail ???");
        return rt;
    }

    rt = tuya_img_direct_flush_poweronpage_start(img_bin, (img_bin != NULL)?sizeof(img_bin)/sizeof(img_bin[0]):0, 100);
    if (rt != OPRT_OK) {
        PR_ERR("ignore img direct flush poweronpage !");
        return OPRT_OK;
    }
    tuya_disp_lcd_backlight_open();
    return rt;
}

VOID tuya_ai_display_start(BOOL_T is_mf_test)
{
#if 0
#ifdef TUYA_MULTI_TYPES_LCD
    GUI_LCDS_INFO_S info = { 0 };
#else
    TKL_DISP_INFO_S info;
#endif
    if (tuya_gui_lcd_driver_init((VOID *)&info) != OPRT_OK) {
        PR_ERR("lcd driver init fail ???");
        return;
    }
#endif

#ifdef DEV_PACKAGE_TAG
    PR_NOTICE("------------------develop package version: '%s', sdk_ver: '%s'------------------", DEV_PACKAGE_TAG, IOT_SDK_VER);
#else
    PR_NOTICE("------------------not published develop package version------------------");
#endif

    __gui_mf_test = is_mf_test;
#if defined(TUYA_FILE_SYSTEM) && (TUYA_FILE_SYSTEM == 1)

#if defined(FILE_SYSTEM_LFS_SD) && FILE_SYSTEM_LFS_SD == 1
    tuya_app_gui_set_lfs_partiton_type(TUYA_GUI_LFS_SD);
#else
    tuya_app_gui_set_lfs_partiton_type(TUYA_GUI_LFS_SPI_FLASH);                //设置文件系统所在分区(内部flash,外部flash?)
#endif

#endif
    tuya_gui_system_event_hdl_register(tuya_gui_system_evt_hdl_cb);             //GUI系统事件处理回调注册
    // tuya_gui_obj_event_hdl_register(tuya_gui_obj_evt_hdl_cb);                   //需特殊处理的GUI控件变化事件处理回调注册
    // tuya_gui_dp2obj_pre_init_hdl_register(tuya_gui_dp2obj_pre_init_hdl_cb);     //用户手动初始化GUI控件状态的处理回调注册
    // tuya_gui_obj2dp_convert_hdl_register(tuya_gui_obj2dp_convert_hdl_cb);       //GUI控件至涂鸦dp转换回调注册
    //tuya_gui_init(is_mf_test, (VOID *)&info, NULL, NULL);
    tuya_gui_init(is_mf_test, NULL, NULL, NULL);
    //创建一个监控gui心跳的定时器
    // TIMER_ID  gui_hb_detect_timer_id = NULL;
    // tal_sw_timer_create(tuya_gui_heartbeat_detect_cb, NULL, &gui_hb_detect_timer_id);
    // tal_sw_timer_start(gui_hb_detect_timer_id, GUI_HEART_BEAT_INTERVAL_MS, TAL_TIMER_CYCLE);
    #ifdef TUYA_TFTP_SERVER
    tuya_app_gui_tftp_server_init();
    #endif
#ifdef GUI_MEM_USED_INFO_DBG
    tuya_gui_cp0_task_dump_enable();
#endif
}

STATIC LIST_HEAD ai_txt_list_head;
STATIC MUTEX_HANDLE ai_txt_mutex = NULL;
typedef struct
{
    LIST_HEAD node;
    TY_DISPLAY_MSG_T msg_data;
} AI_MSG_LIST_NODE_S;

OPERATE_RET tuya_ai_text_msg_enqueue(UINT8_T *msg, INT_T len, TY_DISPLAY_TYPE_E display_tp)
{
    AI_MSG_LIST_NODE_S *msg_node = (AI_MSG_LIST_NODE_S *)Malloc(sizeof(AI_MSG_LIST_NODE_S));
    UINT8_T *p_msg_bak = NULL;

    if (msg_node != NULL) {
        memset(&msg_node->msg_data, 0, sizeof(TY_DISPLAY_MSG_T));
        msg_node->msg_data.type = display_tp;
        if (msg != NULL && len > 0) {
            p_msg_bak = cp0_req_cp1_malloc_psram(len + 1);
            if (NULL == p_msg_bak) {
                #ifndef TUYA_APP_MULTI_CORE_IPC         //大核模式下:重试一次本地直接申请内存!
                    p_msg_bak = (UINT8_T *)tkl_system_psram_malloc(len + 1);
                #endif
            }
            if(NULL == p_msg_bak) {
                PR_ERR("[%s] ai msg node req malloc fail...", __func__);
                return OPRT_MALLOC_FAILED;
            }
            memset(p_msg_bak, 0x00, len + 1);
            memcpy(p_msg_bak, msg, len);
            msg_node->msg_data.data = p_msg_bak;
            msg_node->msg_data.len = len + 1;
        }
        else {
            msg_node->msg_data.data = NULL;
            msg_node->msg_data.len = 0;
        }
        tal_mutex_lock(ai_txt_mutex);
        tuya_list_add_tail(&(msg_node->node), &ai_txt_list_head);
        tal_mutex_unlock(ai_txt_mutex);
        //PR_NOTICE("[%s] ai msg node enqueue...[type:%d]", __func__, display_tp);
        return OPRT_OK;
    }
    else {
        PR_ERR("[%s] ai msg node malloc fail...", __func__);
        return OPRT_MALLOC_FAILED;
    }
}

STATIC AI_MSG_LIST_NODE_S *tuya_ai_text_msg_dequeue(VOID)
{
    struct tuya_list_head *p = NULL;
    struct tuya_list_head *n = NULL;

    tal_mutex_lock(ai_txt_mutex);
    AI_MSG_LIST_NODE_S* entry = NULL;
    tuya_list_for_each_safe(p, n, &ai_txt_list_head) {
        entry = tuya_list_entry(p, AI_MSG_LIST_NODE_S, node);
        DeleteNode(entry, node);
        break;
    }
    tal_mutex_unlock(ai_txt_mutex);
    return entry;
}

STATIC OPERATE_RET  tuya_ai_text_msg_process(VOID *data)
{
    OPERATE_RET ret = OPRT_OK;
    AI_MSG_LIST_NODE_S *msg_node = NULL;
    LV_DISP_EVENT_S Sevt = {0};
    LV_USER_PRIVATE_EVENT_S g_prv_evt_s = {0};

    if (!tuya_gui_screen_is_loaded()) {
        TAL_PR_NOTICE("%s: screen does not loaded, delay to process ai msg!!!\r\n", __func__);
        return ret;
    }

    while ((msg_node = tuya_ai_text_msg_dequeue()) != NULL) {
        // TAL_PR_NOTICE("[%s] tx ai msg type: %d, data %d\r\n", __func__, msg_node->msg_data.type, msg_node->msg_data.data[0]);
        Sevt.event = LLV_EVENT_USER_PRIVATE;
        g_prv_evt_s.event_typ = LV_USER_PRIVATE_EVENT_AI_TEXT;
        g_prv_evt_s.param1 = (UINT32_T)(&msg_node->msg_data);
        Sevt.param = (UINT_T)&g_prv_evt_s;
        ret = tuya_disp_gui_event_send(&Sevt);
        Free(msg_node);
        msg_node = NULL;
    }
    return ret;
}

STATIC OPERATE_RET tuya_ai_text_msg_list_init(VOID)
{
    OPERATE_RET op_ret = OPRT_OK;

    op_ret = tal_mutex_create_init(&ai_txt_mutex);
    if (op_ret != OPRT_OK) {
        PR_ERR("[%s:%d]:------ai msg mutex init fail ???\r\n", __func__, __LINE__);
        return op_ret;
    }
    INIT_LIST_HEAD(&ai_txt_list_head);
    ty_subscribe_event(EVENT_GUI_READY_NOTIFY, "ai_msg_display", tuya_ai_text_msg_process, SUBSCRIBE_TYPE_NORMAL);
    return op_ret;
}

STATIC OPERATE_RET tuya_ai_text_msg_notify(UINT8_T *msg, INT_T len, TY_DISPLAY_TYPE_E display_tp)
{
    OPERATE_RET op_ret = OPRT_OK;

    op_ret = tuya_ai_text_msg_enqueue(msg, len, display_tp);
    if (op_ret != OPRT_OK) {
        return op_ret;
    }
    return tuya_ai_text_msg_process(NULL);
}

VOID tuya_ai_display_init(VOID)
{
    ty_ai_proc_text_notify = tuya_ai_text_msg_notify;
    tuya_gui_poweron_page_quick_start();
    tuya_ai_text_msg_list_init();   
}
