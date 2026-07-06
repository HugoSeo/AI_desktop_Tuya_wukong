#include "env_data.h"
#include "tuya_app_gui_display_text.h"

static GUI_LANGUAGE_E g_language = GUI_LG_ENGLISH;              //当前的语言环境
static GUI_LANGUAGE_E g_language_changing = GUI_LG_UNKNOWN;     //已经配置,但还未生效的语言环境!

extern bool tuya_app_gui_language_live_update_enabled(void);

#if 0
static void reboot_spinner_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * spinner = lv_event_get_target(e);
    lv_obj_t * half_bg = lv_obj_get_parent(spinner);

    if(code == LV_EVENT_CLICKED) {
        LV_LOG_WARN("------------>reboot Spinner clicked\n");
    }
}
#endif

static void reboot_update_label_text(lv_timer_t * t)
{
    static int i = 0;
    static bool notified = false;

    lv_obj_t *label = (lv_obj_t *)(t->user_data);
    //lv_obj_t * half_bg = lv_obj_get_parent(label);
    const char *sta[] = {"Rebooting","Rebooting.","Rebooting..","Rebooting..."};

    lv_label_set_text(label, sta[i%4]);
    if ((++i)>=4)
        i=0;
    if (!notified) {
        //通知业务端
        LV_DISP_EVENT_S lv_msg = {.tag = NULL, .event = LLV_EVENT_VENDOR_REBOOT, .param = 0};
        aic_lvgl_msg_event_change((void *)&lv_msg);
        notified = true;
    }
}

static OPERATE_RET ed_set_default_language(GUI_LANGUAGE_E language)
{
    if (language > GUI_LG_UNKNOWN && language < GUI_LG_MAX)
        g_language = language;
    TY_GUI_LOG_PRINT("[%s][%d]current default language is '%d' !\r\n", __FUNCTION__, __LINE__, g_language);
    return OPRT_OK;
}

void ed_init_language(void)
{
    LV_DISP_EVENT_S lv_msg = {.tag = NULL, .event = LLV_EVENT_VENDOR_LANGUAGE_INFO, .param = 0};
    aic_lvgl_msg_event_change((void *)&lv_msg);
    ed_set_default_language((GUI_LANGUAGE_E)(lv_msg.param));
}

GUI_LANGUAGE_E ed_get_language(void)
{
    return g_language;
}

OPERATE_RET ed_set_language(GUI_LANGUAGE_E language)
{
    g_language_changing = language;
    return OPRT_OK;
}

void ed_update_language(void)
{
    if (g_language_changing != GUI_LG_UNKNOWN && g_language_changing != g_language) {
        lv_obj_t * act_scr = lv_scr_act();          //获取当前活动屏幕
        g_language = g_language_changing;
        if (tuya_app_gui_language_live_update_enabled()) {
            if (tuya_app_display_text_init() == OPRT_OK) {
                if (act_scr->obj_tag != NULL) {
                    lvglMsg_t lv_msg = {.tag = act_scr->obj_tag, .event = LLV_EVENT_VENDOR_LANGUAGE_CHANGE, .param = 0};
                    lv_msg.obj_type = LV_DISP_GUI_OBJ_OBJ;
                    lvMsgSendToLvgl(&lv_msg);
                }
                else {
                    TY_GUI_LOG_PRINT("[%s][%d]current screen does not set tag when language changing !\r\n", __FUNCTION__, __LINE__);
                }
            }
        }
        else {
            //提示系统重启!
            static lv_obj_t *half_bg = NULL;
            half_bg = lv_obj_create(act_scr);
            lv_obj_set_pos(half_bg, 0, 0);
            lv_obj_set_size(half_bg, LV_HOR_RES, LV_VER_RES);
            lv_obj_set_style_bg_opa(half_bg, LV_OPA_TRANSP, LV_PART_MAIN|LV_STATE_DEFAULT);
            lv_obj_move_foreground(half_bg);
            lv_obj_set_flex_flow(half_bg, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(half_bg, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            //创建重启spinner
        #if LVGL_VERSION_MAJOR < 9
            lv_obj_t *spinner = lv_spinner_create(half_bg, 1000, 50);
        #else
            lv_obj_t *spinner = lv_spinner_create(half_bg);
            lv_spinner_set_anim_params(spinner, 1000, 50);
        #endif
            lv_obj_set_size(spinner, 100, 100);
            lv_obj_center(spinner);
            lv_obj_add_flag(spinner, LV_OBJ_FLAG_CLICKABLE);
            //lv_obj_add_event_cb(spinner, reboot_spinner_event_cb, LV_EVENT_ALL, NULL);
            //设置标签文本
            lv_obj_t *labell = lv_label_create(half_bg);
            lv_label_set_text(labell, "Language Changed !");
            lv_obj_set_size(labell, 300, 32);
            lv_label_set_long_mode(labell, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_set_style_text_color(labell, lv_color_hex(0x00ff00), LV_PART_MAIN|LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(labell, LV_TEXT_ALIGN_CENTER/*LV_TEXT_ALIGN_CENTER*/, LV_PART_MAIN|LV_STATE_DEFAULT);//设置label的文本居中
            lv_obj_align(labell, LV_ALIGN_TOP_MID, 0, 0);    //设置label居中

            lv_obj_t *label = lv_label_create(half_bg);
            lv_label_set_text(label, "");
            lv_obj_set_size(label, 300, 32);
            lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_set_style_text_color(label, lv_color_hex(0xff0000), LV_PART_MAIN|LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER/*LV_TEXT_ALIGN_CENTER*/, LV_PART_MAIN|LV_STATE_DEFAULT);//设置label的文本居中
            lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);    //设置label居中
            //lv_obj_center(label);
            lv_timer_create(reboot_update_label_text, 500, (void *)label); // 创建一个500ms间隔的定时器
        }
    }
}
