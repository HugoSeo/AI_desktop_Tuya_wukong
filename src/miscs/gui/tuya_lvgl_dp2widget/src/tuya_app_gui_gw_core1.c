/**
 * @file tuya_gw_core.c
 * @brief tuya_gw_core module is used to 
 * @version 0.1
 * @date 2024-07-29
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "gw_intf.h"
#include "smart_frame.h"
#include "lv_vendor_event.h"
#include "tuya_app_gui_gw_core1.h"
#include "tuya_app_gui_core_config.h"
#include "tuya_cloud_wifi_defs.h"
#include "app_ipc_command.h"
#include "lvgl.h"
#include "tuya_app_gui.h"
#ifdef LVGL_ENABLE_CLOUD_RECIPE
#include "ty_gfw_gui_cloud_recipe_param.h"

STATIC gui_cloud_recipe_rsp_cb_t cloud_recipe_rsp_hdl_cb = NULL;
#endif
/***********************************************************
************************macro define************************
***********************************************************/

/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef struct
{
    unsigned char has_res;                   //是否有新资源下载?
    uint32_t end_res;                        //资源下载成功或失败?
    lv_obj_t *half_bg;
    lv_obj_t *dl_start_spinner;
    lv_obj_t *dl_start_msgbox;
    lv_obj_t *check_err_msgbox;
    lv_obj_t *dl_progress_label;
    lv_obj_t *dl_progress_bar;
    lv_obj_t *dl_end_ok_msgbox;
    lv_obj_t *dl_end_fail_msgbox;
    lv_timer_t * update_text_timer;
}_DL_GUI_S;

/***********************************************************
********************function declaration********************
***********************************************************/
extern void tuya_app_gui_touch_monitor(void);
extern VOID_T* tkl_system_psram_malloc(CONST SIZE_T size);
extern VOID_T tkl_system_psram_free(VOID_T* ptr);

STATIC gui_dp_update_cb_t gui_dp_update_cb = NULL;
STATIC gui_disk_format_end_cb_t disk_format_result_cb = NULL;
STATIC gui_resource_event_cb_t gui_resource_event_cb = NULL;
STATIC gui_audio_play_status_update_cb_t gui_audio_play_status_cb = NULL;
/***********************************************************
***********************variable define**********************
***********************************************************/

STATIC DEV_CNTL_N_S *gp_dev_cntl = NULL;
STATIC BOOL_T ResourceUpdating = FALSE;
STATIC _DL_GUI_S *res_dl_obj = NULL;
STATIC lv_obj_t *unbind_half_bg = NULL;
/***********************************************************
***********************function define**********************
***********************************************************/

/***********************************************************
*  Function: tuya_app_gui_dev_dp_n_cntl
*  Input:empty
*  Output:
*  Return:
*  Note:
***********************************************************/
void *tuya_app_gui_get_dp_n_cntl(void)
{
    if (gp_dev_cntl == NULL) {
        TY_GUI_LOG_PRINT("%s:%d, not ready ?\r\n", __func__, __LINE__);
    }
    return gp_dev_cntl;
}

/***********************************************************
*  Function: tuya_app_gui_dev_dp_cntl
*  Input:dev_cntl
*        dpid
*  Output:
*  Return:
*  Note:
***********************************************************/
void *tuya_app_gui_get_dp_cntl(unsigned char dpid)
{
    DEV_CNTL_N_S *p_dev_cntl = gp_dev_cntl;
    DP_CNTL_S *dp_cntl = NULL;

    if (NULL == p_dev_cntl) {
        return NULL;
    }

    INT_T i = 0;
    for (i = 0; i < p_dev_cntl->dp_num; i++) {
        if (p_dev_cntl->dp[i].dp_desc.dp_id == dpid) {
            if (p_dev_cntl->dp[i].dp_desc.type == T_OBJ) {
                dp_cntl = (DP_CNTL_S *)(&p_dev_cntl->dp[i]);
            }
            else if(p_dev_cntl->dp[i].dp_desc.type == T_RAW) {
                //todo: raw type!
            }
            break;
        }
    }

    return (void *)dp_cntl;
}

STATIC void __ty_gw_DpInfoDump(void)
{
    UINT_T i;
    DP_CNTL_S *p_dev_cntl = NULL;

    TY_GUI_LOG_PRINT("%s:%d=================================dump start\r\n", __func__, __LINE__);
    if(gp_dev_cntl != NULL) {
         for (i=0; i<gp_dev_cntl->dp_num; i++) {
             p_dev_cntl = &gp_dev_cntl->dp[i];
             if (p_dev_cntl->dp_desc.type == T_OBJ) {
                 switch (p_dev_cntl->dp_desc.prop_tp)
                 {
                 case PROP_BOOL:
                     TY_GUI_LOG_PRINT("obj DP %d bool:%d \r\n", p_dev_cntl->dp_desc.dp_id, p_dev_cntl->prop.prop_bool.value);
                     break;
                 case PROP_VALUE:
                     TY_GUI_LOG_PRINT("obj DP %d value:%d \r\n",p_dev_cntl->dp_desc.dp_id, p_dev_cntl->prop.prop_value.value);
                     break;
                 case PROP_STR:
                     if (p_dev_cntl->prop.prop_str.value != NULL)
                         TY_GUI_LOG_PRINT("obj DP %d string:'%s' \r\n",p_dev_cntl->dp_desc.dp_id, p_dev_cntl->prop.prop_str.value);
                     else
                         TY_GUI_LOG_PRINT("obj DP %d string:'null' \r\n",p_dev_cntl->dp_desc.dp_id);
                     break;
                 case PROP_ENUM:
                     TY_GUI_LOG_PRINT("obj DP %d enum:%d \r\n",p_dev_cntl->dp_desc.dp_id, p_dev_cntl->prop.prop_enum.value);
                     break;
                 case PROP_BITMAP:
                     TY_GUI_LOG_PRINT("obj DP %d bitmap:%d \r\n",p_dev_cntl->dp_desc.dp_id, p_dev_cntl->prop.prop_bitmap.value);
                     break;
                 default:
                     TY_GUI_LOG_PRINT("???unknown data type '%d'", p_dev_cntl->dp_desc.prop_tp);
                     break;
                 }
             }
             else if (p_dev_cntl->dp_desc.type == T_RAW) {
                TY_GUI_LOG_PRINT("raw DP %d \r\n", p_dev_cntl->dp_desc.dp_id);
             }
         }
    }
    TY_GUI_LOG_PRINT("%s:%d=================================dump end\r\n", __func__, __LINE__);
}

int tuya_app_gui_gw_DpInfoCreate(void *_dev_cntl)
{
    OPERATE_RET rt =  OPRT_COM_ERROR;
    UINT_T i;
    DEV_CNTL_N_S *dev_cntl = (DEV_CNTL_N_S *)_dev_cntl;

    //释放之前的句柄
    if (gp_dev_cntl != NULL) {
        for (i=0; i<gp_dev_cntl->dp_num; i++) {
            if (gp_dev_cntl->dp[i].dp_desc.prop_tp == PROP_STR && gp_dev_cntl->dp[i].prop.prop_str.value != NULL) {
                tkl_system_psram_free(gp_dev_cntl->dp[i].prop.prop_str.value);
            }
        }
        tkl_system_psram_free(gp_dev_cntl);
        gp_dev_cntl = NULL;
    }

    if(gp_dev_cntl == NULL) {
         gp_dev_cntl = (DEV_CNTL_N_S *)tkl_system_psram_malloc(sizeof(DEV_CNTL_N_S) + dev_cntl->dp_num * sizeof(DP_CNTL_S));
         if (NULL == gp_dev_cntl) {
             TY_GUI_LOG_PRINT("%s:%d---os malloc fail\r\n", __func__, __LINE__);
             return OPRT_MALLOC_FAILED;
         }
         memset(gp_dev_cntl, 0, sizeof(DEV_CNTL_N_S) + dev_cntl->dp_num * sizeof(DP_CNTL_S));
         memcpy(gp_dev_cntl, dev_cntl, sizeof(DEV_CNTL_N_S) + dev_cntl->dp_num * sizeof(DP_CNTL_S));
         for (i=0; i<dev_cntl->dp_num; i++) {
            if (gp_dev_cntl->dp[i].dp_desc.prop_tp == PROP_STR) {
                if (dev_cntl->dp[i].prop.prop_str.value != NULL) {
                    gp_dev_cntl->dp[i].prop.prop_str.value = (CHAR_T *)tkl_system_psram_malloc(strlen(dev_cntl->dp[i].prop.prop_str.value) +1);
                    memset(gp_dev_cntl->dp[i].prop.prop_str.value, 0, strlen(dev_cntl->dp[i].prop.prop_str.value) +1);
                    strcpy(gp_dev_cntl->dp[i].prop.prop_str.value, dev_cntl->dp[i].prop.prop_str.value);
                }
                else
                    gp_dev_cntl->dp[i].prop.prop_str.value = NULL;
            }
         }
         rt = OPRT_OK;
    }
    else
        rt = OPRT_OK;

    if (rt == OPRT_OK) {
        TY_GUI_LOG_PRINT("############################## dev_cntl num:%d ####################################\r\n", gp_dev_cntl->dp_num);
        __ty_gw_DpInfoDump();
    }
    else
        TY_GUI_LOG_PRINT("??????? create schema info fail code :%d ???????\r\n", rt);

    return rt;
}

//拒绝更新资源
static void tuya_app_gui_refuse_resource_update(void)
{
    LV_DISP_EVENT_S lv_msg = {0};
    LV_TUYAOS_EVENT_S g_dp_evt_s = {.event_typ = LV_TUYAOS_EVENT_REFUSE_RESOURCE_UPDATE};

    lv_msg.event = LLV_EVENT_VENDOR_TUYAOS;
    lv_msg.param = (UINT_T)&g_dp_evt_s;
    aic_lvgl_msg_event_change((void *)&lv_msg);
}

//同意更新资源
static void tuya_app_gui_agree_resource_update(void)
{
    LV_DISP_EVENT_S lv_msg = {0};
    LV_TUYAOS_EVENT_S g_dp_evt_s = {.event_typ = LV_TUYAOS_EVENT_AGREE_RESOURCE_UPDATE};

    lv_msg.event = LLV_EVENT_VENDOR_TUYAOS;
    lv_msg.param = (UINT_T)&g_dp_evt_s;
    aic_lvgl_msg_event_change((void *)&lv_msg);
}

lv_obj_t * tuya_msgbox_create(lv_obj_t * parent, const char * title, const char * txt, const char * btn_txts[],
                            bool add_close_btn, lv_event_cb_t event_cb)
{
    lv_obj_t *msgbox = NULL;

#if LVGL_VERSION_MAJOR < 9
    msgbox = lv_msgbox_create(parent, title, txt, btn_txts, add_close_btn);
    lv_obj_add_event_cb(msgbox, event_cb, LV_EVENT_ALL, NULL);
#else
    msgbox = lv_msgbox_create(parent);
    lv_msgbox_add_title(msgbox, title);

    lv_msgbox_add_text(msgbox, txt);
    if (add_close_btn)
        lv_msgbox_add_close_button(msgbox);

    lv_obj_t * btn;
    uint32_t btn_cnt = 0;
    while(btn_txts[btn_cnt] && btn_txts[btn_cnt][0] != '\0') {
        btn = lv_msgbox_add_footer_button(msgbox, btn_txts[btn_cnt]);
        lv_obj_add_event_cb(btn, event_cb, LV_EVENT_ALL, NULL);
        btn_cnt++;
    }
#endif
    return msgbox;
}

void tuya_msgbox_del(lv_obj_t * msgbox)
{
#if LVGL_VERSION_MAJOR < 9
    lv_obj_del(msgbox);
#else
    lv_msgbox_close(msgbox);
#endif
}

static void resource_dl_resource_exit(void)
{
    if (res_dl_obj != NULL) {
        if (res_dl_obj->update_text_timer != NULL)
            lv_timer_del(res_dl_obj->update_text_timer);
        if (res_dl_obj->half_bg != NULL)
            lv_obj_del(res_dl_obj->half_bg);
        tkl_system_psram_free(res_dl_obj);
    }
    res_dl_obj = NULL;
    ResourceUpdating = FALSE;
}

static void resource_dl_update_label_text(lv_timer_t * t)
{
    static int i = 0;
    const char *sta[] = {"Updating","Updating.","Updating..","Updating..."};

    tuya_app_gui_touch_monitor();       //禁止黑屏!
    lv_label_set_text(res_dl_obj->dl_progress_label, sta[i%4]);
    if ((++i)>=4)
        i=0;
}

static void resource_check_update_label_text(lv_timer_t * t)
{
    static int i = 0;
    const char *sta[] = {"Checking","Checking.","Checking..","Checking..."};

    tuya_app_gui_touch_monitor();       //禁止黑屏!
    lv_label_set_text(res_dl_obj->dl_progress_label, sta[i%4]);
    if ((++i)>=4)
        i=0;
}

static void resource_dl_end_msgbox_event_cb(lv_event_t * e) 
{
    lv_event_code_t code = lv_event_get_code(e);
    uint16_t id = 0xff;
#if LVGL_VERSION_MAJOR < 9
    //nothing
#else
        lv_obj_t * btn = lv_event_get_target(e);
        lv_obj_t * label = lv_obj_get_child(btn, 0);

        if (strcmp(lv_label_get_text(label), "OK") == 0)
            id = 0;
        else if (strcmp(lv_label_get_text(label), "Cancel") == 0)
            id = 1;
        else {
            LV_LOG_ERROR("Button %s unknown ???", lv_label_get_text(label));
        }
#endif

    switch (code) {
        case LV_EVENT_CLICKED:
        {
        #if LVGL_VERSION_MAJOR < 9
            lv_obj_t * msgbox = lv_event_get_current_target(e);
            id = lv_msgbox_get_active_btn(msgbox);
        #else
            LV_LOG_USER("Button '%s' clicked, id '%d'", lv_label_get_text(label), id);
        #endif
            switch(id) {
                case 0:             //ok
                    if (res_dl_obj->end_res == OPRT_OK) {      //准备重新启动!
                        LV_DISP_EVENT_S lv_msg = {.tag = NULL, .event = LLV_EVENT_VENDOR_REBOOT, .param = 0};
                        LV_LOG_WARN("%s================>ready to reboot !!!\n", __func__);
                        aic_lvgl_msg_event_change((void *)&lv_msg);
                    }
                    resource_dl_resource_exit();
                    break;
                case 1:             //cancel
                    resource_dl_resource_exit();
                    break;
                default:
                    break;
            }
            break;
        }
        default:
            break;
    }
}

static void resource_bar_event_handler(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    bool evt_from_app = lv_event_from_app(e);       //event is from app or touch screen ?
    LV_RESOURCE_DL_EVENT_SS *g_res_evt_s = NULL;

    if (evt_from_app) {
        if (code == LV_EVENT_CLICKED) {         //进度展示!
            gui_res_lang_s *lang_file = NULL;
            gui_res_file_s *normal_file = NULL;
            char progress_txt[64] = {0};
            g_res_evt_s = (LV_RESOURCE_DL_EVENT_SS *)lv_event_get_param(e);

            memset(progress_txt, 0, sizeof(progress_txt));
            if (g_res_evt_s->curr_dl_is_config) {
                lang_file = (gui_res_lang_s *)g_res_evt_s->curr_dl_node;
                if (lang_file != NULL) {
                    //LV_LOG_WARN("%s================>progress notify: config file '%s' !!!\n",__func__,  lang_file->file_name);
                    lv_snprintf(progress_txt, sizeof(progress_txt), "Total: %02d/%02d, Progress: %03d%%, file: %s", 
                        g_res_evt_s->files_dl_count, g_res_evt_s->files_num, g_res_evt_s->dl_prog, lang_file->file_name);
                }
                else {
                    LV_LOG_ERROR("%s================>empty config file !!!\n", __func__);
                    strcpy(progress_txt, "config file buffering...");
                }
            }
            else {
                normal_file = (gui_res_file_s *)g_res_evt_s->curr_dl_node;
                if (normal_file != NULL) {
                    //LV_LOG_WARN("%s================>progress notify:normal file '%s' !!!\n",__func__,  normal_file->file_name);
                    lv_snprintf(progress_txt, sizeof(progress_txt), "Total: %02d/%02d, Progress: %03d%%, file: %s", 
                        g_res_evt_s->files_dl_count, g_res_evt_s->files_num, g_res_evt_s->dl_prog, normal_file->file_name);
                }
                else {
                    LV_LOG_ERROR("%s================>empty normal file !!!\n", __func__);
                    strcpy(progress_txt, "file buffering...");
                }
            }
            LV_LOG_WARN("Downlaod Progress Info :'%s'\r\n", progress_txt);
            lv_label_set_text(res_dl_obj->dl_progress_label, progress_txt);
            lv_bar_set_value(res_dl_obj->dl_progress_bar, g_res_evt_s->dl_prog, LV_ANIM_ON);
            if (g_res_evt_s->files_dl_count == g_res_evt_s->files_num && g_res_evt_s->dl_prog == 100) {     //所有下载完成!
                lv_obj_align_to(res_dl_obj->dl_progress_label, res_dl_obj->dl_progress_bar, LV_ALIGN_OUT_TOP_MID, 0, -10);    //设置label居中在bar上方
                if (res_dl_obj->update_text_timer == NULL)
                    res_dl_obj->update_text_timer = lv_timer_create(resource_dl_update_label_text, 500, NULL);
                else
                    lv_timer_resume(res_dl_obj->update_text_timer);
            }
            //资源更新过程,禁止屏幕睡眠!
            tuya_app_gui_touch_monitor();
        }
        else if (code == LV_EVENT_DRAW_MAIN_END) {             //下载结束!
            lv_obj_add_flag(res_dl_obj->dl_progress_label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(res_dl_obj->dl_progress_bar, LV_OBJ_FLAG_HIDDEN);
            if (res_dl_obj->update_text_timer != NULL)
                lv_timer_pause(res_dl_obj->update_text_timer);
            res_dl_obj->end_res = (uint32_t)(lv_event_get_param(e));
            if (res_dl_obj->end_res == OPRT_OK) {
                LV_LOG_WARN("Downlaod Resource Completed successful !!!\r\n");
                lv_obj_clear_flag(res_dl_obj->dl_end_ok_msgbox, LV_OBJ_FLAG_HIDDEN);
            }
            else {
                LV_LOG_WARN("Downlaod Resource Incompleted (fail)!!!\r\n");
                lv_obj_clear_flag(res_dl_obj->dl_end_fail_msgbox, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

void tuya_msgbox_adjust_auto_size(lv_obj_t *msgbox)
{
    uint32_t i = 0;
    lv_coord_t msgbox_width = LV_HOR_RES * 0.8;   // 80% 的屏幕宽度
    lv_coord_t msgbox_height = LV_VER_RES * 0.4;  // 40% 的屏幕高度
    // 计算按钮的宽度和高度，调整按钮的尺寸
    lv_coord_t btn_width = msgbox_width / 2 - 10;  // 设置按钮宽度为消息框的一半，并减去边距
    lv_coord_t btn_height = msgbox_height * 0.3;    // 设置按钮的高度为消息框高度的 30%
    lv_obj_t * btn = NULL;
    lv_obj_t * label = NULL;
    const lv_font_t * new_font = NULL;

    // 设置消息框尺寸
    lv_obj_set_size(msgbox, msgbox_width, msgbox_height);

    // 居中显示消息框
    lv_obj_center(msgbox);

    if (btn_height > 40) {
        LV_FONT_DECLARE(lv_font_montserrat_24);
        new_font = &lv_font_montserrat_24;  // 较大按钮使用大字体
    } else {
        new_font = &lv_font_montserrat_14;  // 较小按钮使用小字体
    }

    // 获取消息框的按钮容器
#if LVGL_VERSION_MAJOR < 9
    lv_obj_t * btns_container = lv_msgbox_get_btns(msgbox);
    for(i = 0; i < lv_obj_get_child_cnt(btns_container); i++) {
        btn = lv_obj_get_child(btns_container, i);
        lv_obj_set_size(btn, btn_width, btn_height);
        label = lv_obj_get_child(btn, 0);  // 获取按钮上的标签（文字）
        lv_obj_set_style_text_font(label, new_font, LV_PART_MAIN|LV_STATE_DEFAULT);
    }
#else
    /*消息框的按钮通常位于页脚容器(Footer Container)中，而非直接作为消息框的子对象*/
    lv_obj_t *footer = lv_msgbox_get_footer(msgbox);        // 获取页脚容器
    while ((btn = lv_obj_get_child(footer, i)) != NULL) {   //遍历页脚容器的子对象(按钮)
        if (lv_obj_check_type(btn, &lv_msgbox_footer_button_class)) {       //为页脚容器的按钮对象!
            lv_obj_set_size(btn, btn_width, btn_height);
            label = lv_obj_get_child(btn, 0);  // 获取按钮上的标签(文字)
            LV_LOG_USER("start adjust msgbox title '%s' button label '%s' font",    \
                lv_label_get_text(lv_msgbox_get_title(msgbox)), lv_label_get_text(label));
            lv_obj_set_style_text_font(label, new_font, LV_PART_MAIN|LV_STATE_DEFAULT);
        }
        i++;
    }
    lv_obj_set_style_text_font(lv_msgbox_get_title(msgbox), new_font, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lv_msgbox_get_content(msgbox), new_font, LV_PART_MAIN|LV_STATE_DEFAULT);
#endif
}

static void resource_msgbox_event_cb(lv_event_t * e) 
{
    lv_event_code_t code = lv_event_get_code(e);
    uint16_t id = 0xff;
#if LVGL_VERSION_MAJOR < 9
//nothing
#else
    lv_obj_t * btn = lv_event_get_target(e);
    lv_obj_t * label = lv_obj_get_child(btn, 0);

    if (strcmp(lv_label_get_text(label), "OK") == 0)
        id = 0;
    else if (strcmp(lv_label_get_text(label), "Cancel") == 0)
        id = 1;
    else {
        LV_LOG_ERROR("Button %s unknown ???", lv_label_get_text(label));
    }
#endif

    switch (code) {
        case LV_EVENT_CLICKED:
        {
        #if LVGL_VERSION_MAJOR < 9
            lv_obj_t * msgbox = lv_event_get_current_target(e);
            id = lv_msgbox_get_active_btn(msgbox);
        #else
            LV_LOG_USER("Button '%s' clicked, id '%d'", lv_label_get_text(label), id);
        #endif
            switch(id) {
                case 0:             //ok
                {
                    if (res_dl_obj->has_res) {
                        lv_obj_add_flag(res_dl_obj->dl_start_msgbox, LV_OBJ_FLAG_HIDDEN);
                        lv_obj_clear_flag(res_dl_obj->dl_progress_label, LV_OBJ_FLAG_HIDDEN);
                        lv_obj_clear_flag(res_dl_obj->dl_progress_bar, LV_OBJ_FLAG_HIDDEN);
                        tuya_app_gui_agree_resource_update();
                    }
                    else {
                        resource_dl_resource_exit();
                    }
                    break;
                }
                case 1:             //cancel
                {
                    if (res_dl_obj->has_res)
                        tuya_app_gui_refuse_resource_update();
                    resource_dl_resource_exit();
                    break;
                }
                default:
                    break;
            }
            break;
        }
        default:
            break;
    }
}

static void check_err_msgbox_event_cb(lv_event_t * e) 
{
    lv_event_code_t code = lv_event_get_code(e);
    uint16_t id = 0xff;
#if LVGL_VERSION_MAJOR < 9
    //nothing
#else
        lv_obj_t * btn = lv_event_get_target(e);
        lv_obj_t * label = lv_obj_get_child(btn, 0);

        if (strcmp(lv_label_get_text(label), "OK") == 0)
            id = 0;
        else if (strcmp(lv_label_get_text(label), "Cancel") == 0)
            id = 1;
        else {
            LV_LOG_ERROR("Button %s unknown ???", lv_label_get_text(label));
        }
#endif

    switch (code) {
        case LV_EVENT_CLICKED:
        {
        #if LVGL_VERSION_MAJOR < 9
            lv_obj_t * msgbox = lv_event_get_current_target(e);
            id = lv_msgbox_get_active_btn(msgbox);
        #else
            LV_LOG_USER("Button '%s' clicked, id '%d'", lv_label_get_text(label), id);
        #endif
            switch(id) {
                case 0:             //ok
                {
                    resource_dl_resource_exit();
                    break;
                }
                case 1:             //cancel
                {
                    resource_dl_resource_exit();
                    break;
                }
                default:
                    break;
            }
            break;
        }
        default:
            break;
    }
}

static void app_gui_resource_update_create_modal_msgbox(void) {
    // 1. 创建下载起始提示消息框
    static const char * msgbox_start_btns[] = {"OK", "Cancel", ""};
    if (res_dl_obj->has_res) {
        res_dl_obj->dl_start_msgbox = tuya_msgbox_create(res_dl_obj->half_bg, "New Res Notice:", "There are new resources available, download now ?", msgbox_start_btns, false, resource_msgbox_event_cb);
    }
    else {
        res_dl_obj->dl_start_msgbox = tuya_msgbox_create(res_dl_obj->half_bg, "No Res Notice:", "The resources are already up-to-date, exit ?", msgbox_start_btns, false, resource_msgbox_event_cb);
    }
    //lv_obj_set_size(lv_msgbox_get_btns(res_dl_obj->dl_start_msgbox), LV_PCT(40), LV_PCT(30));
    //lv_obj_set_size(res_dl_obj->dl_start_msgbox, LV_PCT(50), LV_PCT(50));
    //lv_obj_center(res_dl_obj->dl_start_msgbox);
    tuya_msgbox_adjust_auto_size(res_dl_obj->dl_start_msgbox);

    // 2.创建进度条:默认隐藏
    res_dl_obj->dl_progress_bar = lv_bar_create(res_dl_obj->half_bg);
    lv_obj_set_size(res_dl_obj->dl_progress_bar, LV_PCT(50), LV_PCT(10));
    lv_bar_set_range(res_dl_obj->dl_progress_bar, 0, 100);
    lv_obj_align(res_dl_obj->dl_progress_bar, LV_ALIGN_CENTER, 0, 0);  // 居中显示
    //lv_obj_set_style_bg_color(res_dl_obj->dl_progress_bar, lv_color_make(0, 0xFF, 0), LV_PART_MAIN|LV_STATE_DEFAULT);         //背景颜色
    lv_obj_set_style_bg_color(res_dl_obj->dl_progress_bar, lv_color_make(0, 0xFF, 0), LV_PART_INDICATOR|LV_STATE_DEFAULT);    //指示器颜色
    lv_bar_set_start_value(res_dl_obj->dl_progress_bar, 0, LV_ANIM_ON);
    lv_obj_set_tag(res_dl_obj->dl_progress_bar, TyResUpdateTag);
    lv_obj_add_event_cb(res_dl_obj->dl_progress_bar, resource_bar_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_add_flag(res_dl_obj->dl_progress_bar, LV_OBJ_FLAG_HIDDEN);

    // 3.设置提示文本:默认隐藏
    lv_label_set_text(res_dl_obj->dl_progress_label, "");
    lv_obj_align_to(res_dl_obj->dl_progress_label, res_dl_obj->dl_progress_bar, LV_ALIGN_OUT_TOP_LEFT, 0, -10);    //设置label居左在bar上方
    lv_obj_add_flag(res_dl_obj->dl_progress_label, LV_OBJ_FLAG_HIDDEN);

    // 4.创建下载成功结束提示消息框:默认隐藏
    //static const char * msgbox_end_ok_btns[] = {"OK", "Cancel", ""};
    static const char * msgbox_end_ok_btns[] = {"OK", ""};
    res_dl_obj->dl_end_ok_msgbox = tuya_msgbox_create(res_dl_obj->half_bg, "Res Dl Ok Notice:", "Downlaod Resource Completed successful, reboot now ?", msgbox_end_ok_btns, false, resource_dl_end_msgbox_event_cb);
    //lv_obj_set_size(lv_msgbox_get_btns(res_dl_obj->dl_end_ok_msgbox), LV_PCT(40), LV_PCT(30));
    //lv_obj_set_size(res_dl_obj->dl_end_ok_msgbox, LV_PCT(50), LV_PCT(50));
    //lv_obj_center(res_dl_obj->dl_end_ok_msgbox);
    tuya_msgbox_adjust_auto_size(res_dl_obj->dl_end_ok_msgbox);
    lv_obj_add_flag(res_dl_obj->dl_end_ok_msgbox, LV_OBJ_FLAG_HIDDEN);

    // 4.创建下载失败结束提示消息框:默认隐藏
    static const char * msgbox_end_fail_btns[] = {"OK", "Cancel", ""};
    res_dl_obj->dl_end_fail_msgbox = tuya_msgbox_create(res_dl_obj->half_bg, "Res Dl Fail Notice:", "Downlaod Resource fail, exit now ?", msgbox_end_fail_btns, false, resource_dl_end_msgbox_event_cb);
    //lv_obj_set_size(lv_msgbox_get_btns(res_dl_obj->dl_end_fail_msgbox), LV_PCT(40), LV_PCT(30));
    //lv_obj_set_size(res_dl_obj->dl_end_fail_msgbox, LV_PCT(50), LV_PCT(50));
    //lv_obj_center(res_dl_obj->dl_end_fail_msgbox);
    tuya_msgbox_adjust_auto_size(res_dl_obj->dl_end_fail_msgbox);
    lv_obj_add_flag(res_dl_obj->dl_end_fail_msgbox, LV_OBJ_FLAG_HIDDEN);
}

STATIC void tuya_app_gui_query_resource_update_prepare(void)
{
    res_dl_obj = (_DL_GUI_S *)tkl_system_psram_malloc(sizeof(_DL_GUI_S));
    if (res_dl_obj == NULL) {
        LV_LOG_WARN("------------>res dl hdl malloc fail\n");
        return;
    }
    memset(res_dl_obj, 0, sizeof(_DL_GUI_S));
    res_dl_obj->update_text_timer = NULL;
    res_dl_obj->has_res = 0;
    //1.创建屏罩
    res_dl_obj->half_bg = lv_obj_create(lv_scr_act());
    lv_obj_set_size(res_dl_obj->half_bg, LV_HOR_RES, LV_VER_RES);
    lv_obj_align(res_dl_obj->half_bg, LV_ALIGN_CENTER, 0, 0);  // 将遮罩居中
    lv_obj_set_style_bg_opa(res_dl_obj->half_bg, 0, LV_PART_MAIN|LV_STATE_DEFAULT);         //完全透明
    lv_obj_move_foreground(res_dl_obj->half_bg);
    //设置背景的弹性布局
    //lv_obj_set_flex_flow(res_dl_obj->half_bg, LV_FLEX_FLOW_COLUMN);
    //lv_obj_set_flex_align(res_dl_obj->half_bg, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    //2.创建加载条
#if LVGL_VERSION_MAJOR < 9
    res_dl_obj->dl_start_spinner = lv_spinner_create(res_dl_obj->half_bg, 1000, 50);
#else
    res_dl_obj->dl_start_spinner = lv_spinner_create(res_dl_obj->half_bg);
    lv_spinner_set_anim_params(res_dl_obj->dl_start_spinner, 1000, 50);
#endif
    lv_obj_set_size(res_dl_obj->dl_start_spinner, LV_PCT(30), LV_PCT(30));
    lv_obj_center(res_dl_obj->dl_start_spinner);
    // 3.创建提示文本:
    res_dl_obj->dl_progress_label = lv_label_create(res_dl_obj->half_bg);
    lv_label_set_text(res_dl_obj->dl_progress_label, "");
    lv_obj_set_size(res_dl_obj->dl_progress_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(res_dl_obj->dl_progress_label, lv_color_hex(0xff0000), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_align_to(res_dl_obj->dl_progress_label, res_dl_obj->dl_start_spinner, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);    //设置spinner下方
    lv_obj_set_style_text_align(res_dl_obj->dl_progress_label, LV_TEXT_ALIGN_LEFT/*LV_TEXT_ALIGN_CENTER*/, LV_PART_MAIN|LV_STATE_DEFAULT);//设置label的文本居中
    res_dl_obj->update_text_timer = lv_timer_create(resource_check_update_label_text, 500, NULL);
}

STATIC void app_gui_resource_update_notify_cb(struct _lv_timer_t *timer)
{
    unsigned char has_res = *(unsigned char *)(timer->user_data);

    if (res_dl_obj == NULL) {
        LV_LOG_ERROR("------------>res dl hdl null fail\n");
        return;
    }

    if (res_dl_obj->dl_start_spinner != NULL)
        lv_obj_add_flag(res_dl_obj->dl_start_spinner, LV_OBJ_FLAG_HIDDEN);
    if (res_dl_obj->update_text_timer != NULL)
        lv_timer_pause(res_dl_obj->update_text_timer);

    res_dl_obj->has_res = has_res;
    if (res_dl_obj->has_res) {
        LV_LOG_WARN("------------>gen has resource msgbox\n");
    }
    else {
        LV_LOG_WARN("------------>gen no resource msgbox\n");
        
    }
    app_gui_resource_update_create_modal_msgbox();
}

STATIC void app_gui_resource_check_err_notify_cb(struct _lv_timer_t *timer)
{
    if (res_dl_obj == NULL) {
        LV_LOG_ERROR("------------>res dl hdl null fail\n");
        return;
    }

    if (res_dl_obj->dl_start_spinner != NULL)
        lv_obj_add_flag(res_dl_obj->dl_start_spinner, LV_OBJ_FLAG_HIDDEN);
    if (res_dl_obj->update_text_timer != NULL)
        lv_timer_pause(res_dl_obj->update_text_timer);

    // 1. 创建下载起始提示消息框
    static const char * msgbox_check_err_btns[] = {"OK", "Cancel", ""};
    res_dl_obj->check_err_msgbox = tuya_msgbox_create(res_dl_obj->half_bg, "Check Res Warning:", "Checking resources error, exit ?", msgbox_check_err_btns, false, check_err_msgbox_event_cb);
    //lv_obj_set_size(lv_msgbox_get_btns(res_dl_obj->check_err_msgbox), LV_PCT(40), LV_PCT(30));
    //lv_obj_set_size(res_dl_obj->check_err_msgbox, LV_PCT(50), LV_PCT(50));
    //lv_obj_center(res_dl_obj->check_err_msgbox);
    tuya_msgbox_adjust_auto_size(res_dl_obj->check_err_msgbox);
}

static void dev_unbind_msgbox_event_cb(lv_event_t * e) 
{
    lv_event_code_t code = lv_event_get_code(e);
    //lv_obj_t * msgbox = NULL;
    uint16_t id = 0xff;
#if LVGL_VERSION_MAJOR < 9
    //nothing
#else
        lv_obj_t * btn = lv_event_get_target(e);
        lv_obj_t * label = lv_obj_get_child(btn, 0);
        //msgbox = lv_obj_get_parent(btn);                //获取父对象msgbox!

        if (strcmp(lv_label_get_text(label), "OK") == 0)
            id = 0;
        else if (strcmp(lv_label_get_text(label), "Cancel") == 0)
            id = 1;
        else {
            LV_LOG_ERROR("Button %s unknown ???", lv_label_get_text(label));
        }
#endif

    switch (code) {
        case LV_EVENT_CLICKED:
        {
        #if LVGL_VERSION_MAJOR < 9
            lv_obj_t * msgbox = lv_event_get_current_target(e);
            id = lv_msgbox_get_active_btn(msgbox);
        #else
            LV_LOG_USER("Button '%s' clicked, id '%d'", lv_label_get_text(label), id);
        #endif
            switch(id) {
                case 0:             //ok
                    LV_LOG_WARN("------------>del unbind msgbox\n");
                    //tuya_msgbox_del(msgbox);
                    lv_obj_del(unbind_half_bg);
                    unbind_half_bg = NULL;
                    break;
                case 1:             //cancel
                    //tuya_msgbox_del(msgbox);
                    lv_obj_del(unbind_half_bg);
                    unbind_half_bg = NULL;
                    break;
                default:
                    break;
            }
            break;
        }
        default:
            break;
    }
}

void tuya_app_gui_common_process (void *data)
{
    LV_TUYAOS_EVENT_S *dp_evt_s = (LV_TUYAOS_EVENT_S *)data;
    OPERATE_RET rt =  OPRT_OK;
    UINT_T i;
    DP_CNTL_S *p_dev_cntl = NULL;
    BOOL_T dp_identified = FALSE;
    lv_timer_t * res_new_timer = NULL;
    static unsigned char has_resource = 0;

        switch (dp_evt_s->event_typ) {
        case LV_TUYAOS_EVENT_DP_INFO_CREATE:
        {
            DEV_CNTL_N_S *dev_cntl = (DEV_CNTL_N_S *)dp_evt_s->param1;
            TY_GUI_LOG_PRINT("############################# recv dp info create event #############################\r\n");
            rt = (OPERATE_RET)tuya_app_gui_gw_DpInfoCreate(dev_cntl);
            dp_evt_s->param2 = (UINT_T)rt;          //返回结果!
            break;
        }

        case LV_TUYAOS_EVENT_DP_REPORT:
        {
            if(dp_evt_s->param1 == 0){
                TY_GUI_LOG_PRINT("ERROR:DP_REPORT param NULL \r\n");
                break;
            }
            DP_REPT_CB_DATA *dp_rept = (DP_REPT_CB_DATA *)dp_evt_s->param1;
            TY_GUI_LOG_PRINT("############################# recv LV_TUYAOS_EVENT_DP_REPORT #############################\r\n");
            tuya_app_gui_screen_saver_exit_request();

            TY_OBJ_DP_REPT_S *rept_obj_dp_p = NULL;
            TY_RAW_DP_REPT_S *rept_raw_dp_p = NULL;

            if(dp_rept->dp_rept_type == T_OBJ_REPT) {
                rept_obj_dp_p = (TY_OBJ_DP_REPT_S *)dp_rept->dp_data;

                TY_GUI_LOG_PRINT("############################# recv DP cnt:%d #############################\r\n", rept_obj_dp_p->obj_dp.cnt);
                TY_GUI_LOG_PRINT("DP id:%d \r\n", rept_obj_dp_p->obj_dp.data[0].dpid);
                TY_GUI_LOG_PRINT("DP type:%d \r\n", rept_obj_dp_p->obj_dp.data[0].type);
                for(i = 0; i < rept_obj_dp_p->obj_dp.cnt; i++) {
                    dp_identified = TRUE;
                    p_dev_cntl =  (DP_CNTL_S *)tuya_app_gui_get_dp_cntl(rept_obj_dp_p->obj_dp.data[i].dpid);
                    if (p_dev_cntl == NULL) {
                        TY_GUI_LOG_PRINT("%s:%d############################# device not ready !!!\r\n", __func__, __LINE__);
                        break;
                    }
                    switch (rept_obj_dp_p->obj_dp.data[i].type)
                    {
                    case PROP_BOOL:
                        TY_GUI_LOG_PRINT("DP %d bool:%d \r\n", p_dev_cntl->dp_desc.dp_id, rept_obj_dp_p->obj_dp.data[i].value.dp_bool);
                        p_dev_cntl->prop.prop_bool.value = rept_obj_dp_p->obj_dp.data[i].value.dp_bool;
                        break;
                    case PROP_VALUE:
                        TY_GUI_LOG_PRINT("DP %d value:%d \r\n",p_dev_cntl->dp_desc.dp_id, rept_obj_dp_p->obj_dp.data[i].value.dp_value);
                        p_dev_cntl->prop.prop_value.value = rept_obj_dp_p->obj_dp.data[i].value.dp_value;
                        break;
                    case PROP_STR:
                        TY_GUI_LOG_PRINT("DP %d string:%s \r\n",p_dev_cntl->dp_desc.dp_id, rept_obj_dp_p->obj_dp.data[i].value.dp_str);
                        if(p_dev_cntl->prop.prop_str.value != NULL) {
                            tkl_system_psram_free(p_dev_cntl->prop.prop_str.value);
                        }
                        p_dev_cntl->prop.prop_str.value = (CHAR_T *)tkl_system_psram_malloc(strlen(rept_obj_dp_p->obj_dp.data[i].value.dp_str) +1);
                        memset(p_dev_cntl->prop.prop_str.value, 0, strlen(rept_obj_dp_p->obj_dp.data[i].value.dp_str) +1);
                        strcpy(p_dev_cntl->prop.prop_str.value, rept_obj_dp_p->obj_dp.data[i].value.dp_str);
                        break;
                    case PROP_ENUM:
                        TY_GUI_LOG_PRINT("DP %d enum:%d \r\n",p_dev_cntl->dp_desc.dp_id, rept_obj_dp_p->obj_dp.data[i].value.dp_enum);
                        p_dev_cntl->prop.prop_enum.value = rept_obj_dp_p->obj_dp.data[i].value.dp_value;
                        break;
                    case PROP_BITMAP:
                        TY_GUI_LOG_PRINT("DP %d bitmap:%d \r\n",p_dev_cntl->dp_desc.dp_id, rept_obj_dp_p->obj_dp.data[i].value.dp_bitmap);
                        p_dev_cntl->prop.prop_bitmap.value = rept_obj_dp_p->obj_dp.data[i].value.dp_value;
                        break;
                    default:
                        dp_identified = FALSE;
                        break;
                    }
                    if (dp_identified == TRUE) {
                        if (gui_dp_update_cb != NULL)
                            gui_dp_update_cb(false, (void *)p_dev_cntl);
                    }
                }
            }
            else if(dp_rept->dp_rept_type == T_RAW_REPT) {
                rept_raw_dp_p = (TY_RAW_DP_REPT_S *)dp_rept->dp_data;
                if (gui_dp_update_cb != NULL)
                    gui_dp_update_cb(true, (void *)rept_raw_dp_p);
            }
            break;
        }

        case LV_TUYAOS_EVENT_FS_FORMAT_REPORT:          //分区格式化结果返回
            {
                TY_GUI_LOG_PRINT("%s:%d############################# disk format result '%s' !!!\r\n", __func__, __LINE__,
                    (dp_evt_s->param2 == OPRT_OK)?"successful":"fail");
                if (dp_evt_s->param2 == OPRT_OK) {
                    if (disk_format_result_cb)
                        disk_format_result_cb(true);
                }
                else {
                    if (disk_format_result_cb)
                        disk_format_result_cb(false);
                }
            }
            break;

        case LV_TUYAOS_EVENT_HAS_NEW_RESOURCE:          //平台有新资源更新
            {
            #if defined(TUYA_IMG_DIRECT_FLUSH) && (TUYA_IMG_DIRECT_FLUSH == 1)
                if (gui_resource_event_cb)
                    gui_resource_event_cb(LV_TUYAOS_EVENT_HAS_NEW_RESOURCE, NULL);
            #else
                has_resource = 1;
                res_new_timer = lv_timer_create(app_gui_resource_update_notify_cb, 100, &has_resource);
                if (res_new_timer != NULL)
                    lv_timer_set_repeat_count(res_new_timer, 1);
            #endif
            }
            break;

        case LV_TUYAOS_EVENT_NO_NEW_RESOURCE:           //平台没有新资源更新
            {
            #if defined(TUYA_IMG_DIRECT_FLUSH) && (TUYA_IMG_DIRECT_FLUSH == 1)
                if (gui_resource_event_cb)
                    gui_resource_event_cb(LV_TUYAOS_EVENT_NO_NEW_RESOURCE, NULL);
            #else
                has_resource = 0;
                res_new_timer = lv_timer_create(app_gui_resource_update_notify_cb, 100, &has_resource);
                if (res_new_timer != NULL)
                    lv_timer_set_repeat_count(res_new_timer, 1);
            #endif
            }
            break;

        case LV_TUYAOS_EVENT_RESOURCE_UPDATE_PROGRESS:  //资源更新进度通知(保留:目前直接由对象事件处理)
            {
            #if defined(TUYA_IMG_DIRECT_FLUSH) && (TUYA_IMG_DIRECT_FLUSH == 1)
                if (gui_resource_event_cb)
                    gui_resource_event_cb(LV_TUYAOS_EVENT_RESOURCE_UPDATE_PROGRESS, (void *)(dp_evt_s->param1));
            #else
            #if 0
                LV_RESOURCE_DL_EVENT_SS *g_res_evt_s = (LV_RESOURCE_DL_EVENT_SS *)dp_evt_s->param1;
                gui_res_lang_s *lang_file = NULL;
                gui_res_file_s *normal_file = NULL;

                if (g_res_evt_s->curr_dl_is_config) {
                    lang_file = (gui_res_lang_s *)g_res_evt_s->curr_dl_node;
                }
                else {
                    normal_file = (gui_res_file_s *)g_res_evt_s->curr_dl_node;
                }
                TY_GUI_LOG_PRINT("%s:%d############################# resource update progress !!!\r\n", __func__, __LINE__);
            #endif
            #endif
            }
            break;

        case LV_TUYAOS_EVENT_RESOURCE_UPDATE_END:
            {
            #if defined(TUYA_IMG_DIRECT_FLUSH) && (TUYA_IMG_DIRECT_FLUSH == 1)
                if (gui_resource_event_cb) {
                    UINT_T res_dl_stat = dp_evt_s->param2;
                    gui_resource_event_cb(LV_TUYAOS_EVENT_RESOURCE_UPDATE_END, (void *)&res_dl_stat);
                }
            #else
                if (res_dl_obj != NULL) {
                    if (res_dl_obj->dl_progress_bar != NULL) {
                        lvglMsg_t lv_msg = {.tag = TyResUpdateTag, .event = LV_EVENT_DRAW_MAIN_END, .param = 0};
                        lv_msg.param = dp_evt_s->param2;
                        TY_GUI_LOG_PRINT("%s:%d############################# resource update %s end !!!\r\n", __func__, __LINE__,
                            (dp_evt_s->param2 == OPRT_OK)?"successful":"fail");
                        lvMsgSendToLvgl(&lv_msg);
                    }
                    else {      //checking error!
                        TY_GUI_LOG_PRINT("%s:%d############################# resource update checking error !!!\r\n", __func__, __LINE__);
                        res_new_timer = lv_timer_create(app_gui_resource_check_err_notify_cb, 100, NULL);
                        if (res_new_timer != NULL)
                            lv_timer_set_repeat_count(res_new_timer, 1);
                    }
                }
                else
                    ResourceUpdating = FALSE;
            #endif
            }
            break;

        case LV_TUYAOS_EVENT_CP1_MALLOC:                    //来自cp0:请求申请内存
            {
                void *p_buff = (void *)tkl_system_psram_malloc(dp_evt_s->param3);
                if (p_buff != NULL) {
                    memset(p_buff, 0, dp_evt_s->param3);
                    *(void **)dp_evt_s->param1 = p_buff;
                    dp_evt_s->param2 = OPRT_OK;
                }
                else
                    dp_evt_s->param2 = 1;
                p_buff = NULL;
            }
            break;

        case LV_TUYAOS_EVENT_CP1_FREE:                      //来自cp0:请求释放内存
            {
                tkl_system_psram_free((void *)dp_evt_s->param1);
                dp_evt_s->param2 = OPRT_OK;
            }
            break;

        case LV_TUYAOS_EVENT_AUDIO_PLAY_STATUS:             //来自cp0:音频播放状态
            {
                if (gui_audio_play_status_cb) {
                    gui_audio_play_status_cb(dp_evt_s->param1);
                }
            }
            break;

    #ifdef LVGL_ENABLE_CLOUD_RECIPE
        case LV_TUYAOS_EVENT_CLOUD_RECIPE:                  //云食谱响应
            {
                gui_cloud_recipe_interactive_s *cl_rsp = (gui_cloud_recipe_interactive_s *)dp_evt_s->param1;
                TY_GUI_LOG_PRINT("%s:%d############################# get cloud recipe response type '%d' !!!\r\n", __func__, __LINE__, cl_rsp->req_type);
                if (cl_rsp != NULL && cloud_recipe_rsp_hdl_cb != NULL) {
                    cloud_recipe_rsp_hdl_cb((void *)cl_rsp);
                }
            }
            break;
    #endif

        default:
            break;
        }
	
}

bool tuya_app_gui_is_wifi_connected(signed char *out_rssi, char *ssid_buff, int ssid_buff_size)
{
    LV_DISP_EVENT_S lv_msg = {0};
    LV_TUYAOS_EVENT_S g_dp_evt_s = {.event_typ = LV_TUYAOS_EVENT_WIFI_STATUS_REPORT};

    lv_msg.event = LLV_EVENT_VENDOR_TUYAOS;
    lv_msg.param = (UINT_T)&g_dp_evt_s;
    aic_lvgl_msg_event_change((void *)&lv_msg);
    TY_GUI_LOG_PRINT("%s:%d############################# wifi status '%s'\r\n", __func__, __LINE__, (g_dp_evt_s.param1 != 0)?"on":"off");
    if (g_dp_evt_s.param1 != 0) {
        if (out_rssi)
            *out_rssi = *(signed char *)(g_dp_evt_s.param2);
        if (ssid_buff != NULL && ssid_buff_size > 0)
            snprintf(ssid_buff, ssid_buff_size, "%s", (const char *)(g_dp_evt_s.param3));
        return true;
    }
    else {
        return false;
    }
}

bool tuya_app_gui_is_dev_activeted(void)
{
    bool activeted = false;
    LV_DISP_EVENT_S lv_msg = {0};
    LV_TUYAOS_EVENT_S g_dp_evt_s = {.event_typ = LV_TUYAOS_EVENT_ACTIVE_STATE_REPORT};

    lv_msg.event = LLV_EVENT_VENDOR_TUYAOS;
    lv_msg.param = (UINT_T)&g_dp_evt_s;
    aic_lvgl_msg_event_change((void *)&lv_msg);
    TY_GUI_LOG_PRINT("%s:%d############################# dev activeted state '%s'\r\n", __func__, __LINE__, (g_dp_evt_s.param1 != 0)?"true":"false");
    if (g_dp_evt_s.param1 != 0) {
        activeted = true;
    }

    return activeted;
}

void tuya_app_gui_request_dev_unbind(void)
{
    LV_DISP_EVENT_S lv_msg = {0};
    LV_TUYAOS_EVENT_S g_dp_evt_s = {.event_typ = LV_TUYAOS_EVENT_UNBIND_DEV_EXECUTE};

    lv_msg.event = LLV_EVENT_VENDOR_TUYAOS;
    lv_msg.param = (UINT_T)&g_dp_evt_s;
    aic_lvgl_msg_event_change((void *)&lv_msg);
}

bool tuya_app_gui_request_local_time(char *time_buff, int time_buff_size)
{
    bool ret = false;
    LV_DISP_EVENT_S lv_msg = {0};
    LV_TUYAOS_EVENT_S g_dp_evt_s = {.event_typ = LV_TUYAOS_EVENT_LOCAL_TIME_REPORT};

    g_dp_evt_s.param2 = 1;
    lv_msg.event = LLV_EVENT_VENDOR_TUYAOS;
    lv_msg.param = (UINT_T)&g_dp_evt_s;
    aic_lvgl_msg_event_change((void *)&lv_msg);
    if (g_dp_evt_s.param2 == OPRT_OK) {
        if (time_buff != NULL && time_buff_size > 0)
            snprintf(time_buff, time_buff_size, "%s", (char *)g_dp_evt_s.param1);
        ret = true;
    }
    return ret;
}

bool tuya_app_gui_request_local_weather(char **local_weather, uint32_t *weather_len)
{
    bool ret = false;
    LV_DISP_EVENT_S lv_msg = {0};
    LV_TUYAOS_EVENT_S g_dp_evt_s = {.event_typ = LV_TUYAOS_EVENT_WEATHER_REPORT};

    g_dp_evt_s.param2 = 1;
    lv_msg.event = LLV_EVENT_VENDOR_TUYAOS;
    lv_msg.param = (UINT_T)&g_dp_evt_s;
    aic_lvgl_msg_event_change((void *)&lv_msg);
    if (g_dp_evt_s.param2 == OPRT_OK) {
        if (local_weather != NULL && weather_len != NULL) {
            *weather_len = 0;
            *local_weather = (char *)tkl_system_psram_malloc(g_dp_evt_s.param3);
            if (*local_weather != NULL) {
                memcpy(*local_weather, (void *)(g_dp_evt_s.param1), g_dp_evt_s.param3);
                *weather_len = g_dp_evt_s.param3;
                ret = true;
            }
        }
        else
            ret = true;
    }
    TY_GUI_LOG_PRINT("%s:%d############################# get local weather '%s'\r\n", __func__, __LINE__, (ret == true)?"ok":"fail");
    return ret;
}

//开始文件系统格式化
bool tuya_app_gui_disk_format_start(gui_disk_format_end_cb_t disk_mkfs_result_cb)
{
    bool ret = false;
    LV_DISP_EVENT_S lv_msg = {0};
    LV_TUYAOS_EVENT_S g_dp_evt_s = {.event_typ = LV_TUYAOS_EVENT_FS_FORMAT_REPORT};

    lv_msg.event = LLV_EVENT_VENDOR_TUYAOS;
    lv_msg.param = (UINT_T)&g_dp_evt_s;
    aic_lvgl_msg_event_change((void *)&lv_msg);
    if (g_dp_evt_s.param2 == OPRT_OK) {
        ret = true;
        disk_format_result_cb = disk_mkfs_result_cb;
    }
    TY_GUI_LOG_PRINT("%s:%d############################# start disk format '%s'\r\n", __func__, __LINE__, (ret == true)?"ok":"fail");
    return ret;
}

//kv写操作
bool tuya_app_gui_kv_common_write(char *key, unsigned char *value, uint32_t len)
{
    bool ret = false;
#ifdef TUYA_APP_USE_TAL_IPC

#else
    LV_DISP_EVENT_S lv_msg = {0};
    LV_TUYAOS_EVENT_S g_dp_evt_s = {.event_typ = LV_TUYAOS_EVENT_KV_WRITE};

    g_dp_evt_s.param1 = (uint32_t)key;
    g_dp_evt_s.param3 = (uint32_t)value;
    g_dp_evt_s.param4 = len;
    lv_msg.event = LLV_EVENT_VENDOR_TUYAOS;
    lv_msg.param = (UINT_T)&g_dp_evt_s;
    aic_lvgl_msg_event_change((void *)&lv_msg);
    if (g_dp_evt_s.param2 == OPRT_OK) {
        ret = true;
    }
#endif
    TY_GUI_LOG_PRINT("kv write ############################# '%s'  %s !\r\n", key, (ret == true)?"ok":"fail");
    return ret;
}

//kv读操作
bool tuya_app_gui_kv_common_read(char *key, unsigned char **value, uint32_t *p_len)
{
    bool ret = false;
#ifdef TUYA_APP_USE_TAL_IPC
    
#else
    unsigned char *out_val = NULL;
    LV_DISP_EVENT_S lv_msg = {0};
    LV_TUYAOS_EVENT_S g_dp_evt_s = {.event_typ = LV_TUYAOS_EVENT_KV_READ};

    g_dp_evt_s.param1 = (uint32_t)key;
    g_dp_evt_s.param4 = (uint32_t)p_len;
    lv_msg.event = LLV_EVENT_VENDOR_TUYAOS;
    lv_msg.param = (UINT_T)&g_dp_evt_s;
    aic_lvgl_msg_event_change((void *)&lv_msg);
    if (g_dp_evt_s.param2 == OPRT_OK) {
        out_val = *(unsigned char **)g_dp_evt_s.param3;
        *value = (unsigned char *)tkl_system_psram_malloc(*p_len);
        if (*value != NULL) {
            memcpy(*value, (void *)out_val, *p_len);
            ret = true;
        }
    }
#endif
    TY_GUI_LOG_PRINT("kv read ############################# '%s'  %s!\r\n", key, (ret == true)?"ok":"fail");
    return ret;
}

//cp1请求从cp0申请psram内存
void *cp1_req_cp0_malloc_psram(uint32_t buf_len)
{
    LV_DISP_EVENT_S lv_msg = {0};
    LV_TUYAOS_EVENT_S g_dp_evt_s = {0};
    void *buff = NULL;

    lv_msg.event = LLV_EVENT_VENDOR_TUYAOS;
    g_dp_evt_s.event_typ = LV_TUYAOS_EVENT_CP0_MALLOC;
    g_dp_evt_s.param1 = (uint32_t)&buff;
    g_dp_evt_s.param3 = buf_len;
    lv_msg.param = (UINT_T)&g_dp_evt_s;
    aic_lvgl_msg_event_change((void *)&lv_msg);

    if (g_dp_evt_s.param2 != OPRT_OK) {
        TY_GUI_LOG_PRINT(" !!!! req cp0 malloc fail !\r\n");
    }
    return buff;
}

//cp1请求从cp0释放psram内存
int cp1_req_cp0_free_psram(void *ptr)
{
    int op_ret = OPRT_COM_ERROR;
    LV_DISP_EVENT_S lv_msg = {0};
    LV_TUYAOS_EVENT_S g_dp_evt_s = {0};

    lv_msg.event = LLV_EVENT_VENDOR_TUYAOS;
    g_dp_evt_s.event_typ = LV_TUYAOS_EVENT_CP0_FREE;
    g_dp_evt_s.param1 = (uint32_t)ptr;
    lv_msg.param = (UINT_T)&g_dp_evt_s;
    aic_lvgl_msg_event_change((void *)&lv_msg);
    if (g_dp_evt_s.param2 != OPRT_OK) {
        TY_GUI_LOG_PRINT(" !!!! req cp0 free fail !\r\n");
    }
    else
        op_ret = OPRT_OK;
    return op_ret;
}

static void hexStringToBytes(const char* hexString, unsigned char* bytes, size_t byteCount) {
    for (size_t i = 0; i < byteCount; ++i) {
        sscanf(hexString + 2 * i, "%2hhx", &bytes[i]);
    }
}

static void bytesToHexString(const unsigned char* bytes, size_t byteCount, char* hexString) {
    for (size_t i = 0; i < byteCount; ++i) {
        sprintf(hexString + 2 * i, "%02x", bytes[i]);
    }
}

//kv文件写操作
bool tuya_app_gui_fs_kv_common_write(char *key, unsigned char *value, uint32_t len)
{
    bool ret = false;
    LV_DISP_EVENT_S lv_msg = {0};
    LV_TUYAOS_EVENT_S g_dp_evt_s = {.event_typ = LV_TUYAOS_EVENT_FS_KV_WRITE};
    char *hexString = NULL;

    hexString = (char *)tkl_system_psram_malloc(len * 2 + 1);  // 每字节2字符+空终止符
    if (hexString != NULL) {
        bytesToHexString(value, len, hexString);
        hexString[len * 2] = '\0'; // 结尾加上字符串终止符
        //TY_GUI_LOG_PRINT("key %s bytes to Hex String res: '%s'\r\n", key, hexString);
        g_dp_evt_s.param1 = (uint32_t)key;
        g_dp_evt_s.param3 = (uint32_t)hexString;
        g_dp_evt_s.param4 = len * 2;                //不带空终止符长度
        lv_msg.event = LLV_EVENT_VENDOR_TUYAOS;
        lv_msg.param = (UINT_T)&g_dp_evt_s;
        aic_lvgl_msg_event_change((void *)&lv_msg);
        if (g_dp_evt_s.param2 == OPRT_OK) {
            ret = true;
        }
        tkl_system_psram_free(hexString);
    }
    TY_GUI_LOG_PRINT("fs kv write ############################# '%s'  %s !\r\n", key, (ret == true)?"ok":"fail");
    return ret;
}

//kv文件读操作
bool tuya_app_gui_fs_kv_common_read(char *key, unsigned char **value, uint32_t *p_len)
{
    bool ret = false;
    char *hexString = NULL;
    unsigned char *bytes = NULL;
    uint32_t out_len = 0;
    LV_DISP_EVENT_S lv_msg = {0};
    LV_TUYAOS_EVENT_S g_dp_evt_s = {.event_typ = LV_TUYAOS_EVENT_FS_KV_READ};

    g_dp_evt_s.param1 = (uint32_t)key;
    g_dp_evt_s.param4 = (uint32_t)&out_len;             //不带空终止符长度
    lv_msg.event = LLV_EVENT_VENDOR_TUYAOS;
    lv_msg.param = (UINT_T)&g_dp_evt_s;
    aic_lvgl_msg_event_change((void *)&lv_msg);
    if (g_dp_evt_s.param2 == OPRT_OK) {
        hexString = *(char **)g_dp_evt_s.param3;
        *p_len = out_len/2;
        bytes = (unsigned char *)tkl_system_psram_malloc(*p_len);
        if (bytes != NULL) {
            hexStringToBytes(hexString, bytes, *p_len);
            *value = bytes;
            ret = true;
        }
    }
    TY_GUI_LOG_PRINT("fs kv read ############################# '%s'  %s!\r\n", key, (ret == true)?"ok":"fail");
    return ret;
}

//kv文件配置删除操作
void tuya_app_gui_fs_kv_common_delete_config(bool config_file/*配置或备份文件*/)
{
    LV_DISP_EVENT_S lv_msg = {0};
    LV_TUYAOS_EVENT_S g_dp_evt_s = {.event_typ = LV_TUYAOS_EVENT_FS_KV_DELETE};

    g_dp_evt_s.param1 = (uint32_t)config_file;
    lv_msg.event = LLV_EVENT_VENDOR_TUYAOS;
    lv_msg.param = (UINT_T)&g_dp_evt_s;
    aic_lvgl_msg_event_change((void *)&lv_msg);
    TY_GUI_LOG_PRINT("fs kv delete ############################# '%s'!\r\n", (config_file == true)?"config file":"bk file");
}

//查询平台资源更新信息
void tuya_app_gui_query_resource_update(void)
{
    LV_DISP_EVENT_S lv_msg = {0};
    LV_TUYAOS_EVENT_S g_dp_evt_s = {.event_typ = LV_TUYAOS_EVENT_RESOURCE_UPDATE};

    if (ResourceUpdating) {
        TY_GUI_LOG_PRINT("%s:%d############################# Repeatedly Resouce Update Request !!!\r\n", __func__, __LINE__);
        return;
    }

    lv_msg.event = LLV_EVENT_VENDOR_TUYAOS;
    lv_msg.param = (UINT_T)&g_dp_evt_s;
    aic_lvgl_msg_event_change((void *)&lv_msg);
    if (g_dp_evt_s.param2 == OPRT_OK) {
        TY_GUI_LOG_PRINT("%s:%d############################# execute resource successful !!!\r\n", __func__, __LINE__);
        ResourceUpdating = TRUE;
        tuya_app_gui_query_resource_update_prepare();
    }
    else {
        TY_GUI_LOG_PRINT("%s:%d#############################execute resource fail code '%d'???\r\n", __func__, __LINE__, g_dp_evt_s.param2);
        if (g_dp_evt_s.param2 == 2) {   //设备未绑定!
            //1.创建屏罩
            unbind_half_bg = lv_obj_create(lv_scr_act());
            lv_obj_set_size(unbind_half_bg, LV_HOR_RES, LV_VER_RES);
            lv_obj_align(unbind_half_bg, LV_ALIGN_CENTER, 0, 0);  // 将遮罩居中
            lv_obj_set_style_bg_opa(unbind_half_bg, 0, LV_PART_MAIN|LV_STATE_DEFAULT);         //完全透明
            lv_obj_move_foreground(unbind_half_bg);
            //2.提示消息框
            static const char * msgbox_unbind_btns[] = {"OK", ""};
            lv_obj_t *dev_unbind_msgbox = tuya_msgbox_create(unbind_half_bg, "Dev Unbinded Warning:", "Device is not binded, retry !", msgbox_unbind_btns, false, dev_unbind_msgbox_event_cb);
            //lv_obj_set_size(lv_msgbox_get_btns(dev_unbind_msgbox), LV_PCT(40), LV_PCT(30));
            //lv_obj_set_size(dev_unbind_msgbox, LV_PCT(50), LV_PCT(50));
            //lv_obj_center(dev_unbind_msgbox);
            tuya_msgbox_adjust_auto_size(dev_unbind_msgbox);
        }
    }
}

//查询平台资源更新信息事件回调注册
void tuya_app_gui_query_resource_event_cb_register(gui_resource_event_cb_t cb)
{
    gui_resource_event_cb = cb;
}

#ifdef LVGL_ENABLE_CLOUD_RECIPE
void tuya_app_gui_cloud_recipe_rsp_hdl_register(gui_cloud_recipe_rsp_cb_t cr_rsp_hdl_cb)
{
    cloud_recipe_rsp_hdl_cb = cr_rsp_hdl_cb;
}

bool tuya_app_gui_cloud_recipe_request(uint32_t req_type, char *req_json_param)
{
    bool ret = false;
    LV_DISP_EVENT_S lv_msg = {0};
    LV_TUYAOS_EVENT_S g_dp_evt_s = {.event_typ = LV_TUYAOS_EVENT_CLOUD_RECIPE};
    gui_cloud_recipe_interactive_s cl_req;

    cl_req.req_type = req_type;
    cl_req.req_param = (void *)req_json_param;
    g_dp_evt_s.param1 = (uint32_t)&cl_req;
    g_dp_evt_s.param2 = 1;
    lv_msg.event = LLV_EVENT_VENDOR_TUYAOS;
    lv_msg.param = (UINT_T)&g_dp_evt_s;
    aic_lvgl_msg_event_change((void *)&lv_msg);
    if (g_dp_evt_s.param2 == OPRT_OK) {
        ret = true;
    }
    TY_GUI_LOG_PRINT("%s############################# req type '%d' for cloud recipe '%s'\r\n", __func__, req_type, (ret == true)?"ok":"fail");
    return ret;
}
#else
void tuya_app_gui_cloud_recipe_rsp_hdl_register(gui_cloud_recipe_rsp_cb_t cr_rsp_hdl_cb)
{
    TY_GUI_LOG_PRINT("%s:%d############################# unsupport rsp cloud recipe register!!!\r\n", __func__, __LINE__);
}

bool tuya_app_gui_cloud_recipe_request(uint32_t req_type, char *req_json_param)
{
    bool ret = false;
    TY_GUI_LOG_PRINT("%s:%d############################# unsupport req cloud recipe !!!\r\n", __func__, __LINE__);
    return ret;
}
#endif

//播放本地音频文件
bool tuya_app_gui_audio_file_play(char *audio_file)
{
    bool ret = false;
    LV_DISP_EVENT_S lv_msg = {0};
    LV_TUYAOS_EVENT_S g_dp_evt_s = {.event_typ = LV_TUYAOS_EVENT_AUDIO_FILE_PLAY};

    g_dp_evt_s.param1 = (uint32_t)audio_file;
    lv_msg.event = LLV_EVENT_VENDOR_TUYAOS;
    lv_msg.param = (UINT_T)&g_dp_evt_s;
    aic_lvgl_msg_event_change((void *)&lv_msg);
    if (g_dp_evt_s.param2 == OPRT_OK) {
        ret = true;
    }
    TY_GUI_LOG_PRINT("%s############################# audio file '%s' play '%s'!!!\r\n", __func__, audio_file, (ret == true)?"ok":"fail");
    return ret;
}

//播放暂停
bool tuya_app_gui_audio_play_pause(void)
{
    bool ret = false;
    LV_DISP_EVENT_S lv_msg = {0};
    LV_TUYAOS_EVENT_S g_dp_evt_s = {.event_typ = LV_TUYAOS_EVENT_AUDIO_PLAY_PAUSE};

    lv_msg.event = LLV_EVENT_VENDOR_TUYAOS;
    lv_msg.param = (UINT_T)&g_dp_evt_s;
    aic_lvgl_msg_event_change((void *)&lv_msg);
    if (g_dp_evt_s.param2 == OPRT_OK) {
        ret = true;
    }
    TY_GUI_LOG_PRINT("%s############################# audio play pause '%s'!!!\r\n", __func__, (ret == true)?"ok":"fail");
    return ret;
}

//播放继续
bool tuya_app_gui_audio_play_resume(void)
{
    bool ret = false;
    LV_DISP_EVENT_S lv_msg = {0};
    LV_TUYAOS_EVENT_S g_dp_evt_s = {.event_typ = LV_TUYAOS_EVENT_AUDIO_PLAY_RESUME};

    lv_msg.event = LLV_EVENT_VENDOR_TUYAOS;
    lv_msg.param = (UINT_T)&g_dp_evt_s;
    aic_lvgl_msg_event_change((void *)&lv_msg);
    if (g_dp_evt_s.param2 == OPRT_OK) {
        ret = true;
    }
    TY_GUI_LOG_PRINT("%s############################# audio play resume '%s'!!!\r\n", __func__, (ret == true)?"ok":"fail");
    return ret;
}

//设置本地音频音量
bool tuya_app_gui_audio_volume_set(uint32_t volume)
{
    bool ret = false;
    LV_DISP_EVENT_S lv_msg = {0};
    LV_TUYAOS_EVENT_S g_dp_evt_s = {.event_typ = LV_TUYAOS_EVENT_AUDIO_VOLUME_SET};

    g_dp_evt_s.param1 = volume;
    lv_msg.event = LLV_EVENT_VENDOR_TUYAOS;
    lv_msg.param = (UINT_T)&g_dp_evt_s;
    aic_lvgl_msg_event_change((void *)&lv_msg);
    if (g_dp_evt_s.param2 == OPRT_OK) {
        ret = true;
    }
    TY_GUI_LOG_PRINT("%s############################# set audio volume '%d' '%s'!!!\r\n", __func__, volume, (ret == true)?"ok":"fail");
    return ret;
}

//获取本地音频音量
bool tuya_app_gui_audio_volume_get(uint32_t *volume)
{
    bool ret = false;
    LV_DISP_EVENT_S lv_msg = {0};
    LV_TUYAOS_EVENT_S g_dp_evt_s = {.event_typ = LV_TUYAOS_EVENT_AUDIO_VOLUME_GET};

    lv_msg.event = LLV_EVENT_VENDOR_TUYAOS;
    lv_msg.param = (UINT_T)&g_dp_evt_s;
    aic_lvgl_msg_event_change((void *)&lv_msg);
    if (g_dp_evt_s.param2 == OPRT_OK) {
        *volume = g_dp_evt_s.param1;
        ret = true;
    }
    TY_GUI_LOG_PRINT("%s############################# get audio volume '%d' '%s'!!!\r\n", __func__, *volume, (ret == true)?"ok":"fail");
    return ret;
}

//设置屏幕背光亮度百分比
bool tuya_app_gui_set_lcd_backlight_percent(unsigned char light_percent)
{
    LV_DISP_EVENT_S lv_msg = {0};

    lv_msg.event = LLV_EVENT_VENDOR_LCD_LIGHT_PERCENT;
    lv_msg.param = (UINT_T)light_percent;
    aic_lvgl_msg_event_change((void *)&lv_msg);

    TY_GUI_LOG_PRINT("%s############################# set lcd backlight percent to '%d' !!!\r\n", __func__, light_percent);
    return true;
}

//cp1获取本地音频文件播放状态回调注册
void tuya_app_gui_audio_play_status_callback_register(gui_audio_play_status_update_cb_t cb)
{
    gui_audio_play_status_cb = cb;
}

void tuya_app_gui_dp_update_notify_callback_register(gui_dp_update_cb_t cb)
{
    gui_dp_update_cb = cb;
}
