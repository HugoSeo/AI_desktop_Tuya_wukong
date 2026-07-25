#include "desk_event_handle.h"
#include "desk_handle_ui.h"

#define AI_DESKTOP_RESET_COUNT_DOWN_TIME   5   //重置设备时的倒计时时长，单位为秒
settings_scr_res_t s_settings_res = {0};
static lv_timer_t *s_count_down_tm = NULL;
static int s_count_down_num = AI_DESKTOP_RESET_COUNT_DOWN_TIME;

static void settings_chinese_btn_clicked(lv_event_t *e)
{
    lv_settings_ui_t *ui = &getContent()->st_func_settings;
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) 
    {
        TAL_PR_DEBUG("Chinese selected");

        setDeskLanguage(DESK_CHINESE);

        lv_indev_wait_release(lv_indev_get_act());

        lv_obj_del(ui->language_cont);

        settings_homepage_create();

        lv_obj_update_layout(ui->settings_scr); 
    }    
}

static void settings_english_btn_clicked(lv_event_t *e)
{
    lv_settings_ui_t *ui = &getContent()->st_func_settings;
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) 
    {
        TAL_PR_DEBUG("English selected");

        setDeskLanguage(DESK_ENGLISH);
        
        lv_indev_wait_release(lv_indev_get_act());

        lv_obj_del(ui->language_cont);

        settings_homepage_create();

        lv_obj_update_layout(ui->settings_scr); 
    }    
}

static void settings_language_back_event(lv_event_t *e)
{
    lv_settings_ui_t *ui = &getContent()->st_func_settings;
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) 
    {
        TAL_PR_DEBUG("[%s] clicked !!!!!!", __func__);

        lv_indev_wait_release(lv_indev_get_act());

        lv_obj_del(ui->language_cont);

        settings_homepage_create();

        lv_obj_update_layout(ui->settings_scr); 
    }       
}

static void settings_language_page_create(void)
{
    lv_settings_ui_t *ui = &getContent()->st_func_settings;
    ui->language_cont = lv_obj_create(ui->settings_scr);
    lv_obj_remove_style_all(ui->language_cont);
    lv_obj_set_size(ui->language_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(ui->language_cont, 0, 0);
    lv_obj_set_style_bg_opa(ui->language_cont, LV_OPA_TRANSP, 0);     

        //标题容器
    lv_obj_t *title = lv_obj_create(ui->language_cont);
    lv_obj_remove_style_all(title);
    lv_obj_set_size(title, LV_HOR_RES, 50);
    lv_obj_set_pos(title, 0, 0);
    lv_obj_set_style_bg_opa(title, LV_OPA_TRANSP, 0);

    lv_obj_t *title_name = lv_label_create(title);
    lv_label_set_text(title_name, "语言");
    lv_obj_set_style_text_font(title_name, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_size(title_name, LV_SIZE_CONTENT, 20);
    lv_obj_align(title_name, LV_ALIGN_CENTER, 0, 0);    //相对于父对象居中
    lv_obj_set_style_text_align(title_name, LV_TEXT_ALIGN_CENTER, 0);   //label内部文本居中

    lv_obj_t *back_btn = lv_btn_create(title);
    lv_obj_remove_style_all(back_btn);
    lv_obj_set_size(back_btn, 50, 50);
    lv_obj_set_pos(back_btn, 0, 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_tag(back_btn, NULL);
    lv_obj_add_event_cb(back_btn, settings_language_back_event, LV_EVENT_CLICKED, NULL);

    if(png_img_load(tuya_app_gui_get_picture_full_path(ICON_BACK_LEFT_24_24), &s_settings_res.back_icon) == 0) 
    {
        lv_obj_t *back_icon = lv_img_create(back_btn);
        lv_img_set_src(back_icon, &s_settings_res.back_icon);
        lv_obj_set_pos(back_icon, 13, 13);
        lv_obj_set_size(back_icon, 24, 24);
    }

    //内容容器
    lv_obj_t *content = lv_obj_create(ui->language_cont);
    lv_obj_set_size(content, LV_HOR_RES, LV_VER_RES - 50); 
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_pos(content, 0, 50);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);  
    
    lv_obj_t *chinese_btn = lv_btn_create(content);
    lv_obj_remove_style_all(chinese_btn);
    lv_obj_set_size(chinese_btn, 240, 54);
    lv_obj_set_pos(chinese_btn, 40, 26);
    lv_obj_set_style_bg_opa(chinese_btn, LV_OPA_20, 0);
    lv_obj_set_style_bg_color(chinese_btn, lv_color_hex(0xFFDC7B), 0);
    lv_obj_set_style_radius(chinese_btn, 27, 0);
    lv_obj_set_tag(chinese_btn, NULL);
    lv_obj_add_event_cb(chinese_btn, settings_chinese_btn_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *chinese_text = lv_label_create(chinese_btn);
    lv_label_set_text(chinese_text, "中文");
    lv_obj_set_size(chinese_text, LV_SIZE_CONTENT, 30);
    lv_obj_align(chinese_text, LV_ALIGN_CENTER, 0, 0);    //相对于父对象居中
    lv_obj_set_style_text_font(chinese_text, &AlibabaPuHuiTi3_Regular30, 0);
    lv_obj_set_style_text_color(chinese_text, lv_color_hex(0xFFF37B), 0);
    lv_obj_set_style_text_align(chinese_text, LV_TEXT_ALIGN_CENTER, 0);   //label内部文本居中
    
    lv_obj_t *english_btn = lv_btn_create(content);
    lv_obj_remove_style_all(english_btn);
    lv_obj_set_size(english_btn, 240, 54);
    lv_obj_set_pos(english_btn, 40, 90);
    lv_obj_set_style_bg_opa(english_btn, LV_OPA_10, 0);
    lv_obj_set_style_bg_color(english_btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(english_btn, 27, 0);
    lv_obj_set_tag(english_btn, NULL);
    lv_obj_add_event_cb(english_btn, settings_english_btn_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *english_text = lv_label_create(english_btn);
    lv_label_set_text(english_text, "English");
    lv_obj_set_size(english_text, LV_SIZE_CONTENT, 30);
    lv_obj_align(english_text, LV_ALIGN_CENTER, 0, 0);    //相对于父对象居中
    lv_obj_set_style_text_font(english_text, &AlibabaPuHuiTi3_Regular30, 0);
    lv_obj_set_style_text_color(english_text, lv_color_white(), 0);
    lv_obj_set_style_text_align(english_text, LV_TEXT_ALIGN_CENTER, 0);   //label内部文本居中
    
    setDeskUIIndex(DESKUI_INDEX_SETTINGS_LANGUAGE);
}


static void settings_network_back_event(lv_event_t *e)
{
    lv_settings_ui_t *ui = &getContent()->st_func_settings;
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) 
    {
        TAL_PR_DEBUG("[%s] clicked !!!!!!", __func__);

        lv_indev_wait_release(lv_indev_get_act());

        if(s_count_down_tm != NULL) {
            lv_timer_del(s_count_down_tm);
            s_count_down_tm = NULL;
            s_count_down_num = AI_DESKTOP_RESET_COUNT_DOWN_TIME;
        }

        lv_obj_del(ui->network_cont);

        settings_homepage_create();

        lv_obj_update_layout(ui->settings_scr); 
    }       
}

static void settings_network_reset_event(lv_event_t *e)
{
    static uint32_t s_reset_last_click_time = 0;
    static int s_reset_click_count = 0;

    lv_settings_ui_t *ui = &getContent()->st_func_settings;
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) 
    {
        uint32_t now = lv_tick_get();

        if (s_reset_last_click_time != 0 && (now - s_reset_last_click_time) <= CLICKED_RESET_TIME) {
            s_reset_click_count++;
        } else {
            s_reset_click_count = 1; /* restart counting */
        }
        s_reset_last_click_time = now;

        TAL_PR_DEBUG("network reset clicked count:%d", s_reset_click_count);

        if (s_reset_click_count >= CLICKED_RESET_NUM) {
            TAL_PR_NOTICE("Reset trigger: %d quick clicks detected", s_reset_click_count);

            /* 防止触摸事件残留 */
            lv_indev_wait_release(lv_indev_get_act());

            lv_obj_add_flag(ui->reset_content, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui->confirm_content, LV_OBJ_FLAG_HIDDEN);

            s_count_down_num = AI_DESKTOP_RESET_COUNT_DOWN_TIME;
            if(s_count_down_tm == NULL) {
                s_count_down_tm = lv_timer_create(count_down_timer_cb, 1000, NULL);
            } else {
                lv_timer_reset(s_count_down_tm);
            }

            /* 重置计数器，避免重复触发 */
            s_reset_click_count = 0;
            s_reset_last_click_time = 0;
        }
    }
}

static void count_down_timer_cb(lv_timer_t *timer)
{
    TAL_PR_DEBUG("count down timer cb, count down num:%d", s_count_down_num);
    lv_settings_ui_t *ui = &getContent()->st_func_settings;
    char count_down_str[32] = {0};

    s_count_down_num--;
    if(s_count_down_num > 0) 
    {
        snprintf(count_down_str, sizeof(count_down_str), "确认(%d)", s_count_down_num);
        lv_label_set_text(ui->confirm_text, count_down_str);
        lv_obj_update_layout(ui->confirm_content);
    } 
    else 
    {
        lv_timer_del(s_count_down_tm);
        s_count_down_tm = NULL;
        s_count_down_num = AI_DESKTOP_RESET_COUNT_DOWN_TIME;
        lv_label_set_text(ui->confirm_text, "确认");
        lv_obj_set_style_bg_color(ui->confirm_btn, lv_color_hex(0xFFF37B), 0);
        lv_obj_add_flag(ui->confirm_btn, LV_OBJ_FLAG_CLICKABLE); //倒计时结束后设置按钮可点击
        lv_obj_update_layout(ui->confirm_content);
    }
}

static void settings_network_reset_confirm_event(lv_event_t *e)
{
    lv_settings_ui_t *ui = &getContent()->st_func_settings;
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) 
    {
        TAL_PR_DEBUG("[%s] clicked !!!!!!", __func__);
#if defined(ENABLE_WIFI_SERVICE) && (ENABLE_WIFI_SERVICE == 1)
        tuya_iot_wf_gw_fast_unactive(GWCM_OLD, WF_START_SMART_AP_CONCURRENT);
#endif        
    }       
}

static void settings_network_reset_cancel_event(lv_event_t *e)
{
    lv_settings_ui_t *ui = &getContent()->st_func_settings;
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) 
    {
        TAL_PR_DEBUG("[%s] clicked !!!!!!", __func__);
        if(s_count_down_tm != NULL) {
            lv_timer_del(s_count_down_tm);
            s_count_down_tm = NULL;
        }
        s_count_down_num = AI_DESKTOP_RESET_COUNT_DOWN_TIME;

        lv_label_set_text(ui->confirm_text, "确认(5)");
        lv_obj_set_style_bg_color(ui->confirm_btn, lv_color_hex(0xB8BDDE), 0);
        lv_obj_clear_flag(ui->confirm_btn, LV_OBJ_FLAG_CLICKABLE); //初始状态下不可点击
        
        lv_obj_add_flag(ui->confirm_content, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui->reset_content, LV_OBJ_FLAG_HIDDEN);
    }       
}

static void settings_network_reset_confirm_create(void)
{
    lv_settings_ui_t *ui = &getContent()->st_func_settings;
    
    //创建确认框背景
    ui->confirm_content = lv_obj_create(ui->network_cont);
    lv_obj_set_size(ui->confirm_content, LV_HOR_RES, LV_VER_RES - 50); 
    lv_obj_set_style_border_width(ui->confirm_content, 0, 0);
    lv_obj_set_pos(ui->confirm_content, 0, 50);
    lv_obj_set_scroll_dir(ui->confirm_content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ui->confirm_content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(ui->confirm_content, 0, 0);
    lv_obj_set_style_bg_opa(ui->confirm_content, LV_OPA_TRANSP, 0);  

    lv_obj_t *confirm_tip = lv_label_create(ui->confirm_content);
    lv_label_set_text(confirm_tip, "确认重置设备");
    lv_obj_set_style_text_font(confirm_tip, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(confirm_tip, lv_color_hex(0xFFF37B), 0);
    lv_obj_set_size(confirm_tip, LV_SIZE_CONTENT, 20);
    lv_obj_align(confirm_tip, LV_ALIGN_TOP_MID, 0, 30);   //相对于父对象水平居中，垂直30
    lv_obj_set_style_text_align(confirm_tip, LV_TEXT_ALIGN_CENTER, 0);   //label内部文本居中

    ui->confirm_btn = lv_btn_create(ui->confirm_content);
    lv_obj_remove_style_all(ui->confirm_btn);
    lv_obj_set_size(ui->confirm_btn, 100, 50);
    lv_obj_set_pos(ui->confirm_btn, 45, 90);
    lv_obj_set_style_bg_color(ui->confirm_btn, lv_color_hex(0xB8BDDE), 0);
    lv_obj_set_style_bg_opa(ui->confirm_btn, LV_OPA_30, 0);
    lv_obj_set_style_radius(ui->confirm_btn, 25, 0);
    lv_obj_set_tag(ui->confirm_btn, NULL);
    lv_obj_add_event_cb(ui->confirm_btn, settings_network_reset_confirm_event, LV_EVENT_CLICKED, NULL);
    lv_obj_clear_flag(ui->confirm_btn, LV_OBJ_FLAG_CLICKABLE); //初始状态下不可点击

    ui->confirm_text = lv_label_create(ui->confirm_btn);
    lv_label_set_text(ui->confirm_text, "确认(5)");
    lv_obj_set_size(ui->confirm_text, LV_SIZE_CONTENT, 20);
    lv_obj_align(ui->confirm_text, LV_ALIGN_CENTER, 0, 0);    //相对于父对象居中
    lv_obj_set_style_text_font(ui->confirm_text, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(ui->confirm_text, lv_color_white(), 0);
    lv_obj_set_style_text_align(ui->confirm_text, LV_TEXT_ALIGN_CENTER, 0);    

    lv_obj_t *cancel_btn = lv_btn_create(ui->confirm_content);
    lv_obj_remove_style_all(cancel_btn);
    lv_obj_set_size(cancel_btn, 100, 50);
    lv_obj_set_pos(cancel_btn, 180, 90);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0xB8BDDE), 0);
    lv_obj_set_style_bg_opa(cancel_btn, LV_OPA_30, 0);
    lv_obj_set_style_radius(cancel_btn, 25, 0);
    lv_obj_set_tag(cancel_btn, NULL);
    lv_obj_add_event_cb(cancel_btn, settings_network_reset_cancel_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_text = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_text, "取消");
    lv_obj_set_size(cancel_text, LV_SIZE_CONTENT, 20);
    lv_obj_align(cancel_text, LV_ALIGN_CENTER, 0, 0);    //相对于父对象居中
    lv_obj_set_style_text_font(cancel_text, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(cancel_text, lv_color_white(), 0);
    lv_obj_set_style_text_align(cancel_text, LV_TEXT_ALIGN_CENTER, 0);


    lv_obj_add_flag(ui->confirm_content, LV_OBJ_FLAG_HIDDEN);
}

static void settings_network_page_create(void)
{
    lv_settings_ui_t *ui = &getContent()->st_func_settings;
    ui->network_cont = lv_obj_create(ui->settings_scr);
    lv_obj_remove_style_all(ui->network_cont);
    lv_obj_set_size(ui->network_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(ui->network_cont, 0, 0);
    lv_obj_set_style_bg_opa(ui->network_cont, LV_OPA_TRANSP, 0);     

        //标题容器
    lv_obj_t *title = lv_obj_create(ui->network_cont);
    lv_obj_remove_style_all(title);
    lv_obj_set_size(title, LV_HOR_RES, 50);
    lv_obj_set_pos(title, 0, 0);
    lv_obj_set_style_bg_opa(title, LV_OPA_TRANSP, 0);

    lv_obj_t *title_name = lv_label_create(title);
    lv_label_set_text(title_name, "网络连接");
    lv_obj_set_style_text_font(title_name, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_size(title_name, LV_SIZE_CONTENT, 20);
    lv_obj_align(title_name, LV_ALIGN_CENTER, 0, 0);    //相对于父对象居中
    lv_obj_set_style_text_align(title_name, LV_TEXT_ALIGN_CENTER, 0);   //label内部文本居中

    lv_obj_t *back_btn = lv_btn_create(title);
    lv_obj_remove_style_all(back_btn);
    lv_obj_set_size(back_btn, 50, 50);
    lv_obj_set_pos(back_btn, 0, 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_tag(back_btn, NULL);
    lv_obj_add_event_cb(back_btn, settings_network_back_event, LV_EVENT_CLICKED, NULL);

    if(png_img_load(tuya_app_gui_get_picture_full_path(ICON_BACK_LEFT_24_24), &s_settings_res.back_icon) == 0) 
    {
        lv_obj_t *back_icon = lv_img_create(back_btn);
        lv_img_set_src(back_icon, &s_settings_res.back_icon);
        lv_obj_set_pos(back_icon, 13, 13);
        lv_obj_set_size(back_icon, 24, 24);
    }

    //内容容器
    ui->reset_content = lv_obj_create(ui->network_cont);
    lv_obj_set_size(ui->reset_content, LV_HOR_RES, LV_VER_RES - 50); 
    lv_obj_set_style_border_width(ui->reset_content, 0, 0);
    lv_obj_set_pos(ui->reset_content, 0, 50);
    lv_obj_set_scroll_dir(ui->reset_content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ui->reset_content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(ui->reset_content, 0, 0);
    lv_obj_set_style_bg_opa(ui->reset_content, LV_OPA_TRANSP, 0);  
    
    lv_obj_t *qrcode = lv_qrcode_create(ui->reset_content, 90, lv_color_white(), lv_color_black());
    lv_qrcode_update(qrcode, "https://www.tuya.com/cn/", 24);
    lv_obj_set_pos(qrcode, 115, 8);
    lv_obj_set_size(qrcode, 90, 90);

    lv_obj_t *keyword_text_1 = lv_label_create(ui->reset_content);
    lv_label_set_text(keyword_text_1, "扫描二维码");
    lv_obj_set_size(keyword_text_1, LV_SIZE_CONTENT, 20);
    lv_obj_set_pos(keyword_text_1, 110, 98);
    lv_obj_set_style_text_font(keyword_text_1, &AlibabaPuHuiTi3_Regular20, 0);
    lv_obj_set_style_text_color(keyword_text_1, lv_color_white(), 0);
    lv_obj_set_style_text_align(keyword_text_1, LV_TEXT_ALIGN_CENTER, 0);   //label内部文本居中

    lv_obj_t *keyword_text_2 = lv_label_create(ui->reset_content);
    lv_label_set_text(keyword_text_2, "下载APP添加设备");
    lv_obj_set_size(keyword_text_2, LV_SIZE_CONTENT, 18);
    lv_obj_set_pos(keyword_text_2, 88, 128);
    lv_obj_set_style_text_font(keyword_text_2, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(keyword_text_2, lv_color_white(), 0);
    lv_obj_set_style_text_align(keyword_text_2, LV_TEXT_ALIGN_CENTER, 0);   //label内部文本居中

    lv_obj_add_flag(ui->reset_content, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_tag(ui->reset_content, NULL);
    lv_obj_add_event_cb(ui->reset_content, settings_network_reset_event, LV_EVENT_CLICKED, NULL);
    

    settings_network_reset_confirm_create();
    setDeskUIIndex(DESKUI_INDEX_SETTINGS_NETWORK);
    
}

static void settings_back_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) 
    {
        TAL_PR_DEBUG("[%s] clicked !!!!!!", __func__);

        lv_indev_wait_release(lv_indev_get_act());
        desk_handle_ui_back_to(DHUI_SCREEN_ID_PERSONAL_CENTER, LV_SCR_LOAD_ANIM_FADE_ON, DHUI_SWITCH_PERMANENT);
    }       
}

static void settings_network_choice_event(lv_event_t *e)
{
    lv_settings_ui_t *ui = &getContent()->st_func_settings;
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) 
    {
        TAL_PR_DEBUG("[%s] clicked !!!!!!", __func__);

        ui->network_stat_label = NULL;
        lv_obj_del(ui->home_cont);

        settings_network_page_create();

        lv_obj_update_layout(ui->settings_scr); 
    }       
}

static void settings_language_choice_event(lv_event_t *e)
{
    lv_settings_ui_t *ui = &getContent()->st_func_settings;
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) 
    {
        TAL_PR_DEBUG("[%s] clicked !!!!!!", __func__);

        ui->network_stat_label = NULL;
        lv_obj_del(ui->home_cont);

        settings_language_page_create();

        lv_obj_update_layout(ui->settings_scr); 
    }       
}

static void settings_func_list_create(lv_obj_t  **content)
{
    lv_settings_ui_t *settings_ui = &getContent()->st_func_settings;

    //箭头icon
    int res = png_img_load(tuya_app_gui_get_picture_full_path(ICON_ICON_ARROW_WHITE), &s_settings_res.arrow_icon);

    //网络连接
    lv_obj_t *network_btn = lv_btn_create(*content);
    lv_obj_remove_style_all(network_btn);
    lv_obj_set_size(network_btn, 280, 70);
    lv_obj_set_pos(network_btn, 20, 10);
    lv_obj_set_style_bg_color(network_btn, lv_color_hex(0xB8BDDE), 0);
    lv_obj_set_style_bg_opa(network_btn, LV_OPA_10, 0);
    lv_obj_set_style_radius(network_btn, 16, 0);
    lv_obj_set_tag(network_btn, NULL);
    lv_obj_add_event_cb(network_btn, settings_network_choice_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *network_btn_name = lv_label_create(network_btn);
    lv_label_set_text(network_btn_name, "网络连接");
    lv_obj_set_size(network_btn_name, LV_SIZE_CONTENT, 20);
    lv_obj_set_pos(network_btn_name, 60, 16);
    lv_obj_set_style_text_font(network_btn_name, &AlibabaPuHuiTi3_Regular20, 0);
    lv_obj_set_style_text_color(network_btn_name, lv_color_white(), 0);
    lv_obj_set_style_text_align(network_btn_name, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *network_stat = lv_label_create(network_btn);
    settings_ui->network_stat_label = network_stat;
    if (getContent()->network_status == 1) {
        lv_label_set_text(network_stat, "已连接");
    } else {
        lv_label_set_text(network_stat, "未连接");
    }
    lv_obj_set_size(network_stat, LV_SIZE_CONTENT, 16);
    lv_obj_set_pos(network_stat, 60, 38);
    lv_obj_set_style_text_font(network_stat, &AlibabaPuHuiTi3_Regular16, 0);
    lv_obj_set_style_text_color(network_stat, lv_color_white(), 0);
    lv_obj_set_style_text_align(network_stat, LV_TEXT_ALIGN_CENTER, 0);

    if(png_img_load(tuya_app_gui_get_picture_full_path(ICON_WIFI_30_30), &s_settings_res.network_icon) == 0) 
    {
        lv_obj_t *network_icon = lv_img_create(network_btn);
        lv_img_set_src(network_icon, &s_settings_res.network_icon);
        lv_obj_set_pos(network_icon, 20, 20);
        lv_obj_set_size(network_icon, 30, 30);
    }

    if(res == OPRT_OK)
    {
        lv_obj_t *network_arrow = lv_img_create(network_btn);
        lv_img_set_src(network_arrow, &s_settings_res.arrow_icon);
        lv_obj_set_pos(network_arrow, 236, 20);
        lv_obj_set_size(network_arrow, 24, 24);
    }

    //语言选择
    lv_obj_t *language_btn = lv_btn_create(*content);
    lv_obj_remove_style_all(language_btn);
    lv_obj_set_size(language_btn, 280, 70);
    lv_obj_set_pos(language_btn, 20, 90);
    lv_obj_set_style_bg_color(language_btn, lv_color_hex(0xB8BDDE), 0);
    lv_obj_set_style_bg_opa(language_btn, LV_OPA_10, 0);
    lv_obj_set_style_radius(language_btn, 16, 0);
    lv_obj_set_tag(language_btn, NULL);
    lv_obj_add_event_cb(language_btn, settings_language_choice_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *language_btn_name = lv_label_create(language_btn);
    lv_label_set_text(language_btn_name, "语言");
    lv_obj_set_size(language_btn_name, LV_SIZE_CONTENT, 20);
    lv_obj_set_pos(language_btn_name, 60, 16);
    lv_obj_set_style_text_font(language_btn_name, &AlibabaPuHuiTi3_Regular20, 0);
    lv_obj_set_style_text_color(language_btn_name, lv_color_white(), 0);
    lv_obj_set_style_text_align(language_btn_name, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *language_stat = lv_label_create(language_btn);
    if(DESK_CHINESE == getDeskLanguage())
    {
        lv_label_set_text(language_stat, "中文");
    }
    else if(DESK_ENGLISH == getDeskLanguage())
    {
        lv_label_set_text(language_stat, "English");
    }
    lv_obj_set_size(language_stat, LV_SIZE_CONTENT, 16);
    lv_obj_set_pos(language_stat, 60, 38);
    lv_obj_set_style_text_font(language_stat, &AlibabaPuHuiTi3_Regular16, 0);
    lv_obj_set_style_text_color(language_stat, lv_color_white(), 0);
    lv_obj_set_style_text_align(language_stat, LV_TEXT_ALIGN_CENTER, 0);

    if(png_img_load(tuya_app_gui_get_picture_full_path(ICON_ICON_NETWORK_30_30), &s_settings_res.language_icon) == 0) 
    {
        lv_obj_t *language_icon = lv_img_create(language_btn);
        lv_img_set_src(language_icon, &s_settings_res.language_icon);
        lv_obj_set_pos(language_icon, 20, 20);
        lv_obj_set_size(language_icon, 30, 30);
    }

    if(res == OPRT_OK)
    {
        lv_obj_t *language_arrow = lv_img_create(language_btn);
        lv_img_set_src(language_arrow, &s_settings_res.arrow_icon);
        lv_obj_set_pos(language_arrow, 236, 20);
        lv_obj_set_size(language_arrow, 24, 24);
    }

}

static void settings_homepage_create(void)
{
    lv_settings_ui_t *ui = &getContent()->st_func_settings;
    ui->home_cont = lv_obj_create(ui->settings_scr);
    lv_obj_remove_style_all(ui->home_cont);
    lv_obj_set_size(ui->home_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(ui->home_cont, 0, 0);
    lv_obj_set_style_bg_opa(ui->home_cont, LV_OPA_TRANSP, 0);     

        //标题容器
    lv_obj_t *title = lv_obj_create(ui->home_cont);
    lv_obj_remove_style_all(title);
    lv_obj_set_size(title, LV_HOR_RES, 50);
    lv_obj_set_pos(title, 0, 0);
    lv_obj_set_style_bg_opa(title, LV_OPA_TRANSP, 0);

    lv_obj_t *title_name = lv_label_create(title);
    lv_label_set_text(title_name, "设置");
    lv_obj_set_style_text_font(title_name, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_size(title_name, LV_SIZE_CONTENT, 20);
    lv_obj_align(title_name, LV_ALIGN_CENTER, 0, 0);    //相对于父对象居中
    lv_obj_set_style_text_align(title_name, LV_TEXT_ALIGN_CENTER, 0);   //label内部文本居中

    lv_obj_t *back_btn = lv_btn_create(title);
    lv_obj_remove_style_all(back_btn);
    lv_obj_set_size(back_btn, 50, 50);
    lv_obj_set_pos(back_btn, 0, 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_tag(back_btn, NULL);
    lv_obj_add_event_cb(back_btn, settings_back_event, LV_EVENT_CLICKED, NULL);

    if(png_img_load(tuya_app_gui_get_picture_full_path(ICON_BACK_LEFT_24_24), &s_settings_res.back_icon) == 0) 
    {
        lv_obj_t *back_icon = lv_img_create(back_btn);
        lv_img_set_src(back_icon, &s_settings_res.back_icon);
        lv_obj_set_pos(back_icon, 13, 13);
        lv_obj_set_size(back_icon, 24, 24);
    }

    //内容容器
    lv_obj_t *content = lv_obj_create(ui->home_cont);
    lv_obj_set_size(content, LV_HOR_RES, LV_VER_RES - 50); 
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_pos(content, 0, 50);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);    

    settings_func_list_create(&content);
}

void desk_settings_refresh_network_status_label(void)
{
    lv_settings_ui_t *ui = &getContent()->st_func_settings;

    if (ui->network_stat_label == NULL) {
        return;
    }

    if (getContent()->network_status == 1) {
        lv_label_set_text(ui->network_stat_label, "已连接");
    } else {
        lv_label_set_text(ui->network_stat_label, "未连接");
    }
}

void setup_settings_scr(void)
{
    lv_settings_ui_t *ui = &getContent()->st_func_settings;

    ui->settings_scr = lv_obj_create(NULL);
    lv_obj_set_size(ui->settings_scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(ui->settings_scr, lv_color_hex(0x25262A), 0);
    lv_obj_set_style_pad_all(ui->settings_scr, 0, 0);
    lv_obj_set_style_text_font(ui->settings_scr, &AlibabaPuHuiTi3_Regular16, 0);
    lv_obj_set_style_text_color(ui->settings_scr, lv_color_white(), 0);
    lv_obj_set_scrollbar_mode(ui->settings_scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(ui->settings_scr, LV_DIR_NONE);

    settings_homepage_create();

    lv_obj_update_layout(ui->settings_scr);

    setDeskUIIndex(DESKUI_INDEX_SETTINGS);
}

void settings_scr_res_clear(void)
{
    TAL_PR_INFO("[%s] enter.", __func__);

    //清除图片资源
    png_img_unload(&s_settings_res.back_icon);
    png_img_unload(&s_settings_res.arrow_icon);
    png_img_unload(&s_settings_res.network_icon);
    png_img_unload(&s_settings_res.language_icon);

    memset(&s_settings_res, 0, sizeof(chat_scr_res_t));
}