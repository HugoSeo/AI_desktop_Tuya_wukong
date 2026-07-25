#include "desk_event_handle.h"
#include "desk_ui_res.h"

home1_picture_res_t s_home1_ui = {0};
home2_picture_res_t s_home2_ui = {0};
home3_picture_res_t s_home3_ui = {0};

void setup_scr_home_scr1(void)
{
    home1_scr_t *ui = &getContent()->st_home.home1_lv;
    ui->home_scr1 = lv_obj_create(NULL);
    lv_obj_set_size(ui->home_scr1, 320, 240);
    lv_obj_set_scrollbar_mode(ui->home_scr1, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(ui->home_scr1, lv_color_hex(0x25262A), LV_PART_MAIN|LV_STATE_DEFAULT);

    if (gif_img_load(tuya_app_gui_get_picture_full_path(GIF_DEFAULT_EMOJ), &s_home1_ui.emoj_gif) == 0) 
    {
        ui->home_scr1_gif = lv_gif_create(ui->home_scr1);
        lv_obj_align(ui->home_scr1_gif, LV_ALIGN_CENTER, 0, 0);
        lv_gif_set_src(ui->home_scr1_gif, &s_home1_ui.emoj_gif);
    }

    lv_obj_update_layout(ui->home_scr1);

    lv_obj_set_tag(ui->home_scr1, NULL);
    lv_obj_add_event_cb(ui->home_scr1, handle_home1_event, LV_EVENT_ALL, NULL);

    setDeskUIIndex(DESKUI_INDEX_HOME1);
}

void handle_home1_emo_timer(lv_timer_t * timer)
{
    home1_scr_t *ui = &getContent()->st_home.home1_lv;
    gif_img_unload(&s_home1_ui.emoj_gif);
    if(s_home1_ui.emoj_gif.data != NULL)
    {
        TAL_PR_ERR("[%s] free home1 emoj gif fail", __func__);
        return;
    }

    if (gif_img_load(tuya_app_gui_get_picture_full_path(GIF_DEFAULT_EMOJ), &s_home1_ui.emoj_gif) == 0) 
    {
        lv_gif_set_src(ui->home_scr1_gif, &s_home1_ui.emoj_gif);
    } 

    lv_obj_update_layout(ui->home_scr1);

    lv_timer_del(timer);

    s_home1_ui.timer = NULL;
}

void home1_gif_switch(char *path, bool start_time)
{
    home1_scr_t *ui = &getContent()->st_home.home1_lv;
    gif_img_unload(&s_home1_ui.emoj_gif);

    if(s_home1_ui.emoj_gif.data != NULL)
    {
        TAL_PR_ERR("[%s] free home1 emoj gif fail", __func__);
        return;
    }

    if(NULL == path)
    {
        if (gif_img_load(tuya_app_gui_get_picture_full_path(GIF_DEFAULT_EMOJ), &s_home1_ui.emoj_gif) == 0) 
        {
            lv_gif_set_src(ui->home_scr1_gif, &s_home1_ui.emoj_gif);
        }        
    }
    else
    {
        if (gif_img_load(tuya_app_gui_get_picture_full_path(path), &s_home1_ui.emoj_gif) == 0) 
        {
            lv_gif_set_src(ui->home_scr1_gif, &s_home1_ui.emoj_gif);
        }  
    }

    lv_obj_update_layout(ui->home_scr1);

    if(start_time)
    {
        if(s_home1_ui.timer != NULL)
        {
            lv_timer_del(s_home1_ui.timer);
            s_home1_ui.timer = NULL;
        }

        s_home1_ui.timer = lv_timer_create(handle_home1_emo_timer, 5000, NULL);
    }

}

void home_scr1_res_clear(void)
{
    TAL_PR_DEBUG("[%s] enter.", __func__);
    gif_img_unload(&s_home1_ui.emoj_gif);
    if(s_home1_ui.timer != NULL)
    {
        lv_timer_del(s_home1_ui.timer);
        s_home1_ui.timer = NULL;
        TAL_PR_INFO("[%s] delete home1 timer.", __func__);
    }
    memset(&s_home1_ui, 0, sizeof(home1_picture_res_t));
}

void setup_scr_home_scr2(void)
{
    get_current_time();    //更新时间
    DESKTOP_LANGUAGE language_choice = getDeskLanguage();

    home2_scr_t *ui = &getContent()->st_home.home2_lv;
    ui->home_scr2 = lv_obj_create(NULL);
    lv_obj_set_size(ui->home_scr2, 320, 240);
    lv_obj_set_scrollbar_mode(ui->home_scr2, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(ui->home_scr2, lv_color_hex(0x25262A), 0);
    
    ui->home_scr2_title = lv_label_create(ui->home_scr2);
    lv_obj_remove_style_all(ui->home_scr2_title);
    lv_label_set_text(ui->home_scr2_title, s_home2_ui.time_hms);
    lv_obj_set_pos(ui->home_scr2_title, 16, 8);
    lv_obj_set_size(ui->home_scr2_title, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(ui->home_scr2_title, lv_color_hex(0xB8BDDE), 0);
    lv_obj_set_style_bg_opa(ui->home_scr2_title, LV_OPA_10, 0);
    lv_obj_set_style_radius(ui->home_scr2_title, 15, 0);
    lv_obj_set_style_text_color(ui->home_scr2_title, lv_color_white(), 0);
    lv_obj_set_style_text_font(ui->home_scr2_title, &AlibabaPuHuiTi3_Regular20, 0);
    lv_obj_set_style_text_align(ui->home_scr2_title, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_pad_hor(ui->home_scr2_title, 12, 0);
    lv_obj_set_style_pad_ver(ui->home_scr2_title, 6, 0);
    lv_obj_set_style_shadow_width(ui->home_scr2_title, 0, 0);


    if (png_img_load(tuya_app_gui_get_picture_full_path(ICON_WIFI_24_24), &s_home2_ui.wifi_png) == 0) 
    {
        ui->home_scr2_wifi_icon = lv_img_create(ui->home_scr2);
        lv_img_set_src(ui->home_scr2_wifi_icon, &s_home2_ui.wifi_png);
        lv_obj_set_pos(ui->home_scr2_wifi_icon, 252, 8);
        lv_obj_set_size(ui->home_scr2_wifi_icon, 24, 24);
        lv_obj_add_flag(ui->home_scr2_wifi_icon, LV_OBJ_FLAG_HIDDEN);
        if (getContent()->network_status == 1) {
            lv_obj_clear_flag(ui->home_scr2_wifi_icon, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (png_img_load(tuya_app_gui_get_picture_full_path("battery_icon.png"), &s_home2_ui.battery_png) == 0) 
    {
        ui->home_scr2_battery_icon = lv_bar_create(ui->home_scr2);
        lv_obj_remove_style_all(ui->home_scr2_battery_icon);
        lv_bar_set_mode(ui->home_scr2_battery_icon, LV_BAR_MODE_NORMAL);
        lv_bar_set_range(ui->home_scr2_battery_icon, 0, 100);
        lv_bar_set_value(ui->home_scr2_battery_icon, 100, LV_ANIM_OFF);
        lv_obj_set_pos(ui->home_scr2_battery_icon, 283, 15);
        lv_obj_set_size(ui->home_scr2_battery_icon, 19, 11);

        //背景
        lv_obj_set_style_bg_opa(ui->home_scr2_battery_icon, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_img_src(ui->home_scr2_battery_icon, &s_home2_ui.battery_png, 0);
        lv_obj_set_style_pad_all(ui->home_scr2_battery_icon, 2, 0);

        //填充
        lv_obj_set_style_bg_opa(ui->home_scr2_battery_icon, LV_OPA_COVER, LV_PART_INDICATOR|LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(ui->home_scr2_battery_icon, lv_color_hex(0x4CD964), LV_PART_INDICATOR|LV_STATE_DEFAULT);

        if(s_home2_ui.battery_timer == NULL)
        {
            TAL_PR_INFO("[%s] create battery update timer.", __func__);
            s_home2_ui.battery_timer = lv_timer_create(handle_home2_battery_timer, 60*1000, NULL);
        }
    }

    if (png_img_load(tuya_app_gui_get_picture_full_path(ICON_POINT_1), &s_home2_ui.point1_png) == 0) 
    {
        ui->home_scr2_point1_icon = lv_img_create(ui->home_scr2);
        lv_img_set_src(ui->home_scr2_point1_icon, &s_home2_ui.point1_png);
        lv_obj_set_pos(ui->home_scr2_point1_icon, 162, 224);
        lv_obj_set_size(ui->home_scr2_point1_icon, 6, 6);
    }

    if (png_img_load(tuya_app_gui_get_picture_full_path(ICON_POINT_2), &s_home2_ui.point2_png) == 0) 
    {
        ui->home_scr2_point2_icon = lv_img_create(ui->home_scr2);
        lv_img_set_src(ui->home_scr2_point2_icon, &s_home2_ui.point2_png);
        lv_obj_set_pos(ui->home_scr2_point2_icon, 152, 224);
        lv_obj_set_size(ui->home_scr2_point2_icon, 6, 6);
    }
    if(language_choice == DESK_CHINESE)
    {
        ui->home_scr2_date_month = lv_label_create(ui->home_scr2);
        lv_label_set_text(ui->home_scr2_date_month, s_home2_ui.month);
        lv_obj_set_pos(ui->home_scr2_date_month, 20, 77);
        lv_obj_set_size(ui->home_scr2_date_month, 79, 66);
        lv_obj_set_style_text_color(ui->home_scr2_date_month, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ui->home_scr2_date_month, &AlibabaPuHuiTi3_Regular65, LV_PART_MAIN|LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(ui->home_scr2_date_month, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);
        
        ui->home_scr2_date_month_text = lv_label_create(ui->home_scr2);
        lv_label_set_text(ui->home_scr2_date_month_text, "月");
        lv_obj_set_pos(ui->home_scr2_date_month_text, 89, 102);
        lv_obj_set_size(ui->home_scr2_date_month_text, 59, 41);
        lv_obj_set_style_text_color(ui->home_scr2_date_month_text, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ui->home_scr2_date_month_text, &AlibabaPuHuiTi3_Regular40, LV_PART_MAIN|LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(ui->home_scr2_date_month_text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);

        ui->home_scr2_date_day = lv_label_create(ui->home_scr2);
        lv_label_set_text(ui->home_scr2_date_day, s_home2_ui.day);
        lv_obj_set_pos(ui->home_scr2_date_day, 152, 46);
        lv_obj_set_size(ui->home_scr2_date_day, 153, 165);
        lv_obj_set_style_radius(ui->home_scr2_date_day, 30, LV_PART_MAIN|LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(ui->home_scr2_date_day, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ui->home_scr2_date_day, &AlibabaPuHuiTi3_Regular120, LV_PART_MAIN|LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(ui->home_scr2_date_day, 28, LV_PART_MAIN|LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(ui->home_scr2_date_day, lv_color_hex(0xB8BDDE), LV_PART_MAIN|LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(ui->home_scr2_date_day, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);
        lv_obj_set_style_pad_top(ui->home_scr2_date_day, 20, LV_PART_MAIN|LV_STATE_DEFAULT);


        ui->home_scr2_date_week = lv_label_create(ui->home_scr2);
        lv_label_set_text(ui->home_scr2_date_week, s_home2_ui.weekday);
        lv_label_set_long_mode(ui->home_scr2_date_week, LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(ui->home_scr2_date_week, 2, 153);
        lv_obj_set_size(ui->home_scr2_date_week, 155, 40);
        lv_obj_set_style_text_color(ui->home_scr2_date_week, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ui->home_scr2_date_week, &AlibabaPuHuiTi3_Regular40, LV_PART_MAIN|LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(ui->home_scr2_date_week, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);
    }
    else
    {
        ui->home_scr2_date_month = lv_label_create(ui->home_scr2);
        lv_label_set_text(ui->home_scr2_date_month, s_home2_ui.month);
        lv_obj_set_pos(ui->home_scr2_date_month, 178, 88);
        lv_obj_set_size(ui->home_scr2_date_month, 138, 74);
        lv_obj_set_style_text_color(ui->home_scr2_date_month, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ui->home_scr2_date_month, &AlibabaPuHuiTi3_Regular65, LV_PART_MAIN|LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(ui->home_scr2_date_month, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);

        ui->home_scr2_date_day = lv_label_create(ui->home_scr2);
        lv_label_set_text(ui->home_scr2_date_day, s_home2_ui.day);
        lv_obj_set_pos(ui->home_scr2_date_day, 15, 46);
        lv_obj_set_size(ui->home_scr2_date_day, 153, 165);
        lv_obj_set_style_radius(ui->home_scr2_date_day, 30, LV_PART_MAIN|LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(ui->home_scr2_date_day, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ui->home_scr2_date_day, &AlibabaPuHuiTi3_Regular120, LV_PART_MAIN|LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(ui->home_scr2_date_day, 28, LV_PART_MAIN|LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(ui->home_scr2_date_day, lv_color_hex(0xB8BDDE), LV_PART_MAIN|LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(ui->home_scr2_date_day, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);
        lv_obj_set_style_pad_top(ui->home_scr2_date_day, 20, LV_PART_MAIN|LV_STATE_DEFAULT);

        ui->home_scr2_date_week = lv_label_create(ui->home_scr2);
        lv_label_set_text(ui->home_scr2_date_week, s_home2_ui.weekday);
        lv_label_set_long_mode(ui->home_scr2_date_week, LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(ui->home_scr2_date_week, 178, 166);
        lv_obj_set_size(ui->home_scr2_date_week, 133, 28);
        lv_obj_set_style_text_color(ui->home_scr2_date_week, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ui->home_scr2_date_week, &AlibabaPuHuiTi3_Regular20, LV_PART_MAIN|LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(ui->home_scr2_date_week, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);

    }


    lv_obj_update_layout(ui->home_scr2);

    lv_obj_set_tag(ui->home_scr2, NULL);
    lv_obj_add_event_cb(ui->home_scr2, handle_home2_event, LV_EVENT_GESTURE, NULL);

    if(s_home2_ui.timer == NULL)
    {
        TAL_PR_INFO("[%s] create get time timer.", __func__);
        s_home2_ui.timer = lv_timer_create(handle_home2_update_timer, 200, NULL);
    }

    setDeskUIIndex(DESKUI_INDEX_HOME2);
}

void handle_home2_battery_timer(lv_timer_t * timer)
{
#if defined(TUYA_AI_TOY_BATTERY_ENABLE) && (TUYA_AI_TOY_BATTERY_ENABLE == 1)

    // BOOL is_charging = tuya_ai_toy_charge_state_get();

    // if(!is_charging)
    // {
    //     UINT8_T capacity_value = tuya_ai_toy_capacity_value_get();

    // }
    // else
    // {

    // }

#endif
}

void handle_home2_update_timer(lv_timer_t * timer)
{
    update_current_time_ui();
}

// 基姆拉尔森公式【终极精准版】
// 入参：年/月/日（整型）；返回值：0=周一、1=周二、2=周三、3=周四、4=周五、5=周六、6=周日
int date_to_week(int year, int month, int day)
{
    // TAL_PR_INFO("[%s]date %d-%d-%d", __func__, year, month, day);
    // 公式强制规则：1月、2月 归属上一年的13、14月
    if (month == 1 || month == 2)
    {
        year--;
        month += 12;
    }
    // 核心公式（无任何偏移，直接计算）
    int week = (day + 2*month + 3*(month+1)/5 + year + year/4 - year/100 + year/400) % 7;
    return week;
}

void get_current_time(void)
{
    // TAL_PR_INFO("[%s] enter.", __func__);
    POSIX_TM_S st_time;
    STATIC CHAR_T time_str[32] = { 0 };
    memset(&st_time, 0, SIZEOF(POSIX_TM_S));
    DESKTOP_LANGUAGE language_choice = getDeskLanguage();

    const char *week_name_cn[] = {"星期一", "星期二", "星期三", "星期四", "星期五", "星期六", "星期日"};
    const char *week_name_en[] = {"MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY", "SUNDAY"};
    const char *month_name_en[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};


    if (tal_time_check_time_sync() == OPRT_OK && tal_time_check_time_zone_sync() == OPRT_OK)
    {
        if (OPRT_OK == tal_time_get_local_time_custom(0, &st_time)) 
        {
            memset(time_str, 0, SIZEOF(time_str));
            snprintf(time_str, SIZEOF(time_str), "%d %02d-%02d %02d:%02d:%02d",
                                    st_time.tm_year+1900, st_time.tm_mon + 1, st_time.tm_mday, st_time.tm_hour, st_time.tm_min, st_time.tm_sec);
            // TAL_PR_INFO("[%s] time_str: %s", __func__, time_str);

            memset(s_home2_ui.month, 0, sizeof(s_home2_ui.month));
            memset(s_home2_ui.day, 0, sizeof(s_home2_ui.day));
            memset(s_home2_ui.weekday, 0, sizeof(s_home2_ui.weekday));
            memset(s_home2_ui.time_hms, 0, sizeof(s_home2_ui.time_hms));

            snprintf(s_home2_ui.time_hms, sizeof(s_home2_ui.time_hms), "%02d:%02d:%02d",
                     st_time.tm_hour, st_time.tm_min, st_time.tm_sec);

            if(language_choice == DESK_CHINESE)
            {
                snprintf(s_home2_ui.month, sizeof(s_home2_ui.month), "%02d", st_time.tm_mon + 1);
                snprintf(s_home2_ui.day, sizeof(s_home2_ui.day), "%02d", st_time.tm_mday);
                snprintf(s_home2_ui.weekday, sizeof(s_home2_ui.weekday), "%s", week_name_cn[date_to_week(st_time.tm_year+1900, st_time.tm_mon + 1, st_time.tm_mday)]);
            }
            else
            {
                snprintf(s_home2_ui.month, sizeof(s_home2_ui.month), "%s", month_name_en[st_time.tm_mon]);
                snprintf(s_home2_ui.day, sizeof(s_home2_ui.day), "%02d", st_time.tm_mday);
                snprintf(s_home2_ui.weekday, sizeof(s_home2_ui.weekday), "%s", week_name_en[date_to_week(st_time.tm_year+1900, st_time.tm_mon + 1, st_time.tm_mday)]);
            }

            // TAL_PR_INFO("[%s] date: %s-%s, weekday:%s, time:%s", __func__, s_home2_ui.month, s_home2_ui.day, s_home2_ui.weekday, s_home2_ui.time_hms);
        }
    }
}

void update_current_time_ui()
{
    lv_obj_t * act_scr = lv_scr_act();
    home2_scr_t *ui = &getContent()->st_home.home2_lv;

    if(act_scr != ui->home_scr2)
    {
        return;
    }

    get_current_time();

    if(NULL != ui->home_scr2_title)
    {
        lv_label_set_text(ui->home_scr2_title, s_home2_ui.time_hms);
    }

    if(NULL != ui->home_scr2_date_month)
    {
        lv_label_set_text(ui->home_scr2_date_month, s_home2_ui.month);
    }

    if(NULL != ui->home_scr2_date_day)
    {
        lv_label_set_text(ui->home_scr2_date_day, s_home2_ui.day);
    }

    if(NULL != ui->home_scr2_date_week)
    {
        lv_label_set_text(ui->home_scr2_date_week, s_home2_ui.weekday);
    }
}

void home_scr2_res_clear(void)
{
    TAL_PR_INFO("[%s] enter.", __func__);
    png_img_unload(&s_home2_ui.wifi_png);
    png_img_unload(&s_home2_ui.battery_png);
    png_img_unload(&s_home2_ui.point1_png);
    png_img_unload(&s_home2_ui.point2_png);

    if(s_home2_ui.timer != NULL)
    {
        lv_timer_del(s_home2_ui.timer);
        s_home2_ui.timer = NULL;
        TAL_PR_INFO("[%s] del home2 timer.", __func__);
    }

    if(s_home2_ui.battery_timer != NULL)
    {
        lv_timer_del(s_home2_ui.battery_timer);
        s_home2_ui.battery_timer = NULL;
        TAL_PR_INFO("[%s] del home2 battery timer.", __func__);
    }

    memset(&s_home2_ui, 0, sizeof(home2_picture_res_t));
}

void setup_scr_home_scr3(void)
{
    home3_scr_t *ui = &getContent()->st_home.home3_lv;
    int volume_v = tuya_ai_toy_volume_get();

    ui->home_scr3 = lv_obj_create(NULL);
    lv_obj_set_size(ui->home_scr3, 320, 240);
    lv_obj_set_scrollbar_mode(ui->home_scr3, LV_SCROLLBAR_MODE_OFF); 
    lv_obj_set_style_bg_color(ui->home_scr3, lv_color_hex(0x25262A), LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes home_scr3_volume_sli
    ui->home_scr3_volume_sli = lv_slider_create(ui->home_scr3);
    lv_slider_set_range(ui->home_scr3_volume_sli, 0, 100);
    lv_slider_set_value(ui->home_scr3_volume_sli, volume_v, LV_ANIM_OFF);
    lv_obj_set_pos(ui->home_scr3_volume_sli, 40, 36);
    lv_obj_set_size(ui->home_scr3_volume_sli, 64, 156);
    lv_obj_set_style_bg_opa(ui->home_scr3_volume_sli, 28, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->home_scr3_volume_sli, lv_color_hex(0xB8BDDE), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->home_scr3_volume_sli, lv_color_hex(0xFFF37B), LV_PART_INDICATOR|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->home_scr3_volume_sli, LV_OPA_TRANSP, LV_PART_KNOB|LV_STATE_DEFAULT);

    //Write codes home_scr3_brightness_sli
    ui->home_scr3_brightness_sli = lv_slider_create(ui->home_scr3);
    lv_slider_set_range(ui->home_scr3_brightness_sli, 0, 100);
    lv_slider_set_value(ui->home_scr3_brightness_sli, 50, LV_ANIM_OFF);
    lv_obj_set_pos(ui->home_scr3_brightness_sli, 128, 36);
    lv_obj_set_size(ui->home_scr3_brightness_sli, 64, 156);
    lv_obj_set_style_bg_opa(ui->home_scr3_brightness_sli, 28, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->home_scr3_brightness_sli, lv_color_hex(0xB8BDDE), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->home_scr3_brightness_sli, lv_color_hex(0xFFF37B), LV_PART_INDICATOR|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->home_scr3_brightness_sli, LV_OPA_TRANSP, LV_PART_KNOB|LV_STATE_DEFAULT);

    //Write codes home_scr3_clock_vol_sli
    ui->home_scr3_clock_vol_sli = lv_slider_create(ui->home_scr3);
    lv_slider_set_range(ui->home_scr3_clock_vol_sli, 0, 100);
    lv_slider_set_mode(ui->home_scr3_clock_vol_sli, LV_SLIDER_MODE_NORMAL);
    lv_slider_set_value(ui->home_scr3_clock_vol_sli, 50, LV_ANIM_OFF);
    lv_obj_set_pos(ui->home_scr3_clock_vol_sli, 216, 36);
    lv_obj_set_size(ui->home_scr3_clock_vol_sli, 64, 156);
    lv_obj_set_style_bg_opa(ui->home_scr3_clock_vol_sli, 28, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->home_scr3_clock_vol_sli, lv_color_hex(0xB8BDDE), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->home_scr3_clock_vol_sli, lv_color_hex(0xFFF37B), LV_PART_INDICATOR|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->home_scr3_clock_vol_sli, LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_DEFAULT);


    if (png_img_load(tuya_app_gui_get_picture_full_path(ICON_VOLUMD), &s_home3_ui.volume_png) == 0) 
    {
        ui->home_scr3_volume_icon = lv_img_create(ui->home_scr3);
        lv_img_set_src(ui->home_scr3_volume_icon, &s_home3_ui.volume_png);
        lv_obj_set_pos(ui->home_scr3_volume_icon, 60, 148);
        lv_obj_set_size(ui->home_scr3_volume_icon, 24, 24);
    }

    if (png_img_load(tuya_app_gui_get_picture_full_path(ICON_CLOCK_VOL), &s_home3_ui.clock_png) == 0) 
    {
        ui->home_scr3_clock_icon = lv_img_create(ui->home_scr3);
        lv_img_set_src(ui->home_scr3_clock_icon, &s_home3_ui.clock_png);
        lv_obj_set_pos(ui->home_scr3_clock_icon, 236, 148);
        lv_obj_set_size(ui->home_scr3_clock_icon, 24, 24);
    }

    if (png_img_load(tuya_app_gui_get_picture_full_path(ICON_BRIGHTNESS), &s_home3_ui.brightness_png) == 0) 
    {
        ui->home_scr3_brightness_icon = lv_img_create(ui->home_scr3);
        lv_img_set_src(ui->home_scr3_brightness_icon, &s_home3_ui.brightness_png);
        lv_obj_set_pos(ui->home_scr3_brightness_icon, 148, 148);
        lv_obj_set_size(ui->home_scr3_brightness_icon, 24, 24);
    }

    if (png_img_load(tuya_app_gui_get_picture_full_path(ICON_UP), &s_home3_ui.up_png) == 0) 
    {
        ui->home_scr3_up_icon = lv_img_create(ui->home_scr3);
        lv_img_set_src(ui->home_scr3_up_icon, &s_home3_ui.up_png);
        lv_obj_set_pos(ui->home_scr3_up_icon, 145, 208);
        lv_obj_set_size(ui->home_scr3_up_icon, 33, 18);
    }

    ui->up_btn = lv_btn_create(ui->home_scr3);
    lv_obj_remove_style_all(ui->up_btn);
    lv_obj_set_pos(ui->up_btn, 110, 210);
    lv_obj_set_size(ui->up_btn, 100, 30);
    lv_obj_set_style_bg_img_opa(ui->up_btn, LV_OPA_COVER, 0);
    extern void handle_home3_clicked_event(lv_event_t *e);
    lv_obj_set_tag(ui->up_btn, NULL);
    lv_obj_add_event_cb(ui->up_btn, handle_home3_clicked_event, LV_EVENT_CLICKED, NULL);
    


    lv_obj_update_layout(ui->home_scr3);

    lv_obj_set_tag(ui->home_scr3, NULL);
    lv_obj_add_event_cb(ui->home_scr3, handle_home3_event, LV_EVENT_GESTURE, NULL);
    lv_obj_set_tag(ui->home_scr3_volume_sli, NULL);
    lv_obj_add_event_cb(ui->home_scr3_volume_sli, handle_home3_slider_event, LV_EVENT_ALL, NULL);
    lv_obj_set_tag(ui->home_scr3_brightness_sli, NULL);
    lv_obj_add_event_cb(ui->home_scr3_brightness_sli, handle_home3_slider_event, LV_EVENT_ALL, NULL);
    lv_obj_set_tag(ui->home_scr3_clock_vol_sli, NULL);
    lv_obj_add_event_cb(ui->home_scr3_clock_vol_sli, handle_home3_slider_event, LV_EVENT_ALL, NULL);

    setDeskUIIndex(DESKUI_INDEX_HOME3);
}

void home_scr3_res_clear(void)
{
    TAL_PR_INFO("[%s] enter.", __func__);
    png_img_unload(&s_home3_ui.volume_png);
    png_img_unload(&s_home3_ui.clock_png);
    png_img_unload(&s_home3_ui.brightness_png);
    png_img_unload(&s_home3_ui.up_png);
    memset(&s_home3_ui, 0, sizeof(home3_picture_res_t));
}