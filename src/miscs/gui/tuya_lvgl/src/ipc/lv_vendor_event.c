#include "lvgl.h"
#include "dl_list.h"
#include "env_data.h"
#include "lv_vendor_event.h"
#include "tkl_memory.h"
#include "tkl_mutex.h"
#include "tkl_queue.h"
#include <assert.h>

typedef struct{
    lv_obj_t            *obj;
    llv_event_code_t    eventCode;
    struct dl_list      m_list;
}lvVendorEventNode_t;


static struct dl_list gLvVendorEventHead;
static TKL_MUTEX_HANDLE gLvVendorEventMutex = NULL;
static TKL_QUEUE_HANDLE gLvglMsgQueue = NULL;
static bool msg_from_app = false;
static bool lvMsgInited = false;

static const char *_msgId2Name(uint32_t eventId)
{
    const char *name = NULL;

    switch(eventId)
    {
        case LLV_EVENT_VENDOR_MF_TEST:
            name = "VENDOR_MF_TEST";
            break;

        case LLV_EVENT_VENDOR_HEART_BEAT:
            name = "VENDOR_HEART_BEAT";
            break;

        case LLV_EVENT_VENDOR_LCD_IDEL:
            name = "VENDOR_LCD_IDEL";
            break;

        case LLV_EVENT_VENDOR_LCD_WAKE:
            name = "VENDOR_LCD_WAKE";
            break;

        case LLV_EVENT_VENDOR_LCD_LIGHT_PERCENT:
            name = "VENDOR_LCD_LIGHT_PERCENT";
            break;

        case LLV_EVENT_VENDOR_LANGUAGE_CHANGE:
            name = "VENDOR_LANGUAGE_CHANGE";
            break;

        case LLV_EVENT_VENDOR_LANGUAGE_INFO:
            name = "VENDOR_LANGUAGE_INFO";
            break;

        case LLV_EVENT_VENDOR_REBOOT:
            name = "VENDOR_REBOOT";
            break;

        case LLV_EVENT_USER_PRIVATE:
            name = "USER_PRIVATE";
            break;

        case LLV_EVENT_VENDOR_TUYAOS:
            name = "VENDOR_TUYAOS";
            break;

        default:
            name = "undef";
            break;
    }

    return name;
}

//#if (CONFIG_SYS_CPU1)
static bool lvTagIsSame(char *str1, char *str2)
{
    if (str1 == NULL) {
        //TY_GUI_LOG_PRINT("[%s][%d] error: empty tag1!!!\r\n", __FUNCTION__, __LINE__);
        return false;
    }
    else if (str2 == NULL) {
        //TY_GUI_LOG_PRINT("[%s][%d] error: empty tag2!!!\r\n", __FUNCTION__, __LINE__);
        return false;
    }
    else if (strcmp(str1,str2) == 0)
        return true;
    return false;
}

static bool lvMsgEventUserDataRepeat(void *desired_user_data)
{
    lvVendorEventNode_t *tmp, *n;
    bool is_repeat = false;

    dl_list_for_each_safe(tmp, n, &gLvVendorEventHead, lvVendorEventNode_t, m_list) {
        if(lvTagIsSame((char *)desired_user_data, (char *)tmp->obj->obj_tag) == true){
            TY_GUI_LOG_PRINT("\r\n************************************************************\r\n");
            TY_GUI_LOG_PRINT("*************** err : set user data '%s' repeat************\r\n", (char *)tmp->obj->obj_tag);
            TY_GUI_LOG_PRINT("************************************************************\r\n");
            is_repeat = true;
            break;
        }
    }
    return is_repeat;
}

void lvMsgEventReg(lv_obj_t *obj, lv_event_code_t eventCode)
{
    if (!lvMsgInited) {
        TY_GUI_LOG_PRINT("*****************err, not inited ?\r\n");
        return;
    }
    assert(lvMsgEventUserDataRepeat(obj->obj_tag) != true);

    if((int)eventCode < (int)LLV_EVENT_VENDOR_MAX)
    {
        lvVendorEventNode_t *eventNode = NULL;

        eventNode = (lvVendorEventNode_t *)tkl_system_malloc(sizeof(lvVendorEventNode_t));
        if(!eventNode){
            TY_GUI_LOG_PRINT("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
            return ;
        }

        //TY_GUI_LOG_PRINT("[%s][%d]this obj %x '%s' reg:%d\r\n", __FUNCTION__, __LINE__, 
        //    obj, (obj->obj_tag!=NULL)?(char *)(obj->obj_tag):"null",eventNode);
        eventNode->eventCode = eventCode;
        eventNode->obj = obj;
        tkl_mutex_lock(gLvVendorEventMutex);
        dl_list_add(&gLvVendorEventHead, &eventNode->m_list);
        tkl_mutex_unlock(gLvVendorEventMutex);
    }
}

void lvMsgEventDel(lv_obj_t *obj)
{
    lvVendorEventNode_t *tmp, *n;
    //static int flag = 0;

    tkl_mutex_lock(gLvVendorEventMutex);
    dl_list_for_each_safe(tmp, n, &gLvVendorEventHead, lvVendorEventNode_t, m_list) {
        if(obj == tmp->obj){
            TY_GUI_LOG_PRINT("[%s][%d]this obj %x (tag:%s) is free\r\n", __FUNCTION__, __LINE__, tmp->obj, 
                (tmp->obj->obj_tag==NULL)?"null":(char *)tmp->obj->obj_tag);
            dl_list_del(&tmp->m_list);
            tkl_system_free(tmp);
            break;
        }
    }
    tkl_mutex_unlock(gLvVendorEventMutex);
}

void lvMsgHandleTask(void)
{
    lvglMsg_t msg;
    OPERATE_RET ret = OPRT_COM_ERROR;
    extern void tuya_app_gui_screen_saver_process(void);

    do{
        ed_update_language();       //更新语言环境!
        tuya_app_gui_screen_saver_process();        //屏幕休眠处理!
        ret = tkl_queue_fetch(gLvglMsgQueue, &msg, 0);
        if (OPRT_OK != ret)
        {
            break;
        }
        lvMsgSyncHandle(&msg);
    }while(0);
}

void lvMsgSyncHandle(lvglMsg_t *msg)
{
    lvVendorEventNode_t *tmp, *n;
    lv_obj_t *target_obj = NULL;
#if LVGL_VERSION_MAJOR < 9
    lv_res_t ret = LV_RES_OK;
#else
    lv_result_t ret = LV_RES_OK;
#endif

    /*
        注意:程序运行到这里是处于gLvVendorEventMutex的锁定状态,所以下一步执行lv_event_send/lv_obj_send_event时,
            用户在控件的事件回调中:
            1.避免直接使用lv_obj_add_event_cb(因为这个函数会执行lvMsgEventReg又会调用gLvVendorEventMutex造成死锁),
            解决办法是在控件的事件回调中使用lv_async_call异步使用lv_obj_add_event_cb注册事件!!!!
            2.避免直接使用lv_obj_del(因为这个函数会执行lvMsgEventDel又会调用gLvVendorEventMutex造成死锁),
            解决办法是在控件的事件回调中使用lv_async_call异步使用lv_obj_del注册事件!!!!
            ******递归互斥量可解决以上问题*****
    */
    tkl_mutex_lock(gLvVendorEventMutex);
    target_obj = NULL;
    dl_list_for_each_safe(tmp, n, &gLvVendorEventHead, lvVendorEventNode_t, m_list) {
        if(lvTagIsSame(msg->tag, (char *)tmp->obj->obj_tag) == true){
            target_obj = tmp->obj;
            break;
        }
    }
    //tkl_mutex_unlock(gLvVendorEventMutex);

    //以下事件从互斥锁抽离处理,避免在控件事件回调中注册事件导致死锁!
    if (target_obj != NULL) {
        //TY_GUI_LOG_PRINT("[%s][%d] send event '%d' to obj tag '%s'\r\n", __FUNCTION__, __LINE__, msg->event, (char *)target_obj->obj_tag);
        msg_from_app = true;     //always indicate msg is from app!
    #if LVGL_VERSION_MAJOR < 9
        ret = lv_event_send(target_obj, msg->event, (void *)msg->param);
    #else
        ret = lv_obj_send_event(target_obj, msg->event, (void *)msg->param);
    #endif
        msg_from_app = false;
        (void)ret;
        //TY_GUI_LOG_PRINT("[%s][%d] post event '%d' to obj %x %s !\r\n", __FUNCTION__, __LINE__, msg->event, target_obj, (LV_RES_INV == ret)?"fail":"ok");
    }
    else {   //给非当前页面的控件发送事件!
        TY_GUI_LOG_PRINT("[%s][%d] tag obj '%s' not exist!\r\n", __FUNCTION__, __LINE__, (msg->tag==NULL)?"null":msg->tag);
    }
    tkl_mutex_unlock(gLvVendorEventMutex);
}

void lvMsgEventUserDataDbg(void)
{
    lvVendorEventNode_t *tmp, *n;
    int cnt = 0;

    dl_list_for_each_safe(tmp, n, &gLvVendorEventHead, lvVendorEventNode_t, m_list) {
        if (tmp->obj->obj_tag != NULL) {
            TY_GUI_LOG_PRINT("======registered event widget '%d' tag: '%s'======\r\n", cnt++, (char *)tmp->obj->obj_tag);
        }
    }
    TY_GUI_LOG_PRINT("======total registered event widget tag num : '%d'======\r\n", cnt);
}
//#endif

OPERATE_RET lvMsgSendToLvglWithEventid(uint32_t eventId)
{
    OPERATE_RET ret = OPRT_OK;
    lvglMsg_t msg = {0};

    if (gLvglMsgQueue)
    {
        TY_GUI_LOG_PRINT("[%s][%d] send eventId:%d, %s\r\n", __FUNCTION__, __LINE__, eventId, _msgId2Name(eventId));
        msg.event = eventId;
        msg.param = 0;
        ret = tkl_queue_post(gLvglMsgQueue, &msg, 0);
        if (OPRT_OK != ret)
        {
            TY_GUI_LOG_PRINT("[%s][%d] push queue failed\r\n", __FUNCTION__, __LINE__);
            return OPRT_COM_ERROR;
        }

        return ret;
    }
    return ret;
}

OPERATE_RET lvMsgSendToLvgl(lvglMsg_t *msg)
{
    OPERATE_RET ret = OPRT_OK;

    if (gLvglMsgQueue)
    {
        //TY_GUI_LOG_PRINT("[%s][%d] send eventId:0x%x, %s\r\n", __FUNCTION__, __LINE__, msg->event, _msgId2Name(msg->event));
        ret = tkl_queue_post(gLvglMsgQueue, msg, 0);
        if (OPRT_OK != ret)
        {
            TY_GUI_LOG_PRINT("[%s][%d] push queue failed\r\n", __FUNCTION__, __LINE__);
            return OPRT_COM_ERROR;
        }

        return ret;
    }
    return ret;
}

//#if (CONFIG_SYS_CPU1)
void lvMsgEventInit(void)
{
    if (OPRT_OK != tkl_queue_create_init(&gLvglMsgQueue, sizeof(lvglMsg_t), LVGL_MSG_QUEUE_SIZE)) {
        TY_GUI_LOG_PRINT("%s-%d: create queue fail? \r\n", __FUNCTION__, __LINE__);
        return;
    }
    dl_list_init(&gLvVendorEventHead);
    tkl_mutex_create_init(&gLvVendorEventMutex);
    lvMsgInited = true;
}
//#endif

bool lv_event_from_app(void *e)
{
    return msg_from_app;
}

