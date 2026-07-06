#ifndef __DESK_FUNC_SETTINGS_H__
#define __DESK_FUNC_SETTINGS_H__
#include "desk_ui_res.h"

typedef struct 
{
    lv_img_dsc_t back_icon;

    lv_img_dsc_t arrow_icon;
    lv_img_dsc_t network_icon;
    lv_img_dsc_t language_icon;
}settings_scr_res_t;

typedef struct
{
    lv_obj_t *settings_scr;
    lv_obj_t *home_cont;
    lv_obj_t *network_cont;
    lv_obj_t *language_cont;
    lv_obj_t *network_stat_label;

    lv_obj_t *reset_content;
    lv_obj_t *confirm_content;
    lv_obj_t *confirm_btn;
    lv_obj_t *confirm_text;
}lv_settings_ui_t;

static void settings_chinese_btn_clicked(lv_event_t *e);

static void settings_english_btn_clicked(lv_event_t *e);

static void settings_language_back_event(lv_event_t *e);

static void settings_language_page_create(void);

static void settings_network_back_event(lv_event_t *e);

static void settings_network_page_create(void);

static void settings_back_event(lv_event_t *e);

static void settings_network_choice_event(lv_event_t *e);

static void settings_language_choice_event(lv_event_t *e);

static void settings_func_list_create(lv_obj_t  **content);

static void settings_homepage_create(void);

static void count_down_timer_cb(lv_timer_t *timer);

void setup_settings_scr(void);

void settings_scr_res_clear(void);

void desk_settings_refresh_network_status_label(void);

#endif  //__DESK_FUNC_SETTINGS_H__