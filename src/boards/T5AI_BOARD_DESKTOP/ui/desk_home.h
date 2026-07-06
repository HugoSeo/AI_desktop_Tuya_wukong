#ifndef __DESK_HOME_H__
#define __DESK_HOME_H__ 
#include "desk_ui_res.h"

typedef struct 
{
    lv_img_dsc_t emoj_gif;
    lv_timer_t *timer;
}home1_picture_res_t;

typedef struct 
{
    lv_img_dsc_t wifi_png;
    lv_img_dsc_t battery_png;
    lv_img_dsc_t point1_png;
    lv_img_dsc_t point2_png;
    lv_timer_t   *timer;
    lv_timer_t   *battery_timer;
    char         month[8];
    char         day[8];
    char         weekday[16];
    char         time_hms[16];
}home2_picture_res_t;

typedef struct 
{
    lv_img_dsc_t volume_png;
    lv_img_dsc_t clock_png;
    lv_img_dsc_t brightness_png;
    lv_img_dsc_t up_png;
}home3_picture_res_t;

typedef enum
{
    HOME_SCREEN_INDEX1 = 0,
    HOME_SCREEN_INDEX2,
    HOME_SCREEN_INDEX3,
    HOME_SCREEN_MAX
}home_index_e;

typedef struct
{
	lv_obj_t *home_scr1;
	lv_obj_t *home_scr1_gif;
}home1_scr_t;   //表情展示界面

typedef struct
{
	lv_obj_t *home_scr2;
	lv_obj_t *home_scr2_title;
    lv_obj_t *home_scr2_wifi_icon;
    lv_obj_t *home_scr2_battery_icon;
    lv_obj_t *home_scr2_point1_icon;
    lv_obj_t *home_scr2_point2_icon;
    lv_obj_t *home_scr2_date_month;
    lv_obj_t *home_scr2_date_month_text;
    lv_obj_t *home_scr2_date_day;
    lv_obj_t *home_scr2_date_week;
}home2_scr_t;   //日期展示页面

typedef struct
{
	lv_obj_t *home_scr3;
	lv_obj_t *home_scr3_volume_sli;
	lv_obj_t *home_scr3_brightness_sli;
	lv_obj_t *home_scr3_clock_vol_sli;
	lv_obj_t *home_scr3_volume_icon;
	lv_obj_t *home_scr3_clock_icon;
	lv_obj_t *home_scr3_brightness_icon;
	lv_obj_t *home_scr3_up_icon;
    lv_obj_t *up_btn;
    home_index_e last_screen;
}home3_scr_t;   //下拉页面

typedef struct
{
    home1_scr_t home1_lv;
    home2_scr_t home2_lv;
    home3_scr_t home3_lv;
}lv_home_ui_t;

void setup_scr_home_scr1(void);

void setup_scr_home_scr2(void);

void setup_scr_home_scr3(void);

void home_scr1_res_clear(void);

void home_scr2_res_clear(void);

void home_scr3_res_clear(void);

void home1_gif_switch(char *path, bool start_time);

void get_current_time(void);

void update_current_time_ui();

void handle_home2_update_timer(lv_timer_t * timer);

void handle_home2_battery_timer(lv_timer_t * timer);

#endif  // __DESK_HOME_H__