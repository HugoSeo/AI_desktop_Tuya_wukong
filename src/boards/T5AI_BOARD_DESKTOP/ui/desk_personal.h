#ifndef __DESK_PERSONAL_H__
#define __DESK_PERSONAL_H__

#include "desk_ui_res.h"

typedef struct 
{
    lv_img_dsc_t back_icon;

    lv_img_dsc_t role_icon;
    lv_img_dsc_t arrow_black;
    lv_img_dsc_t arrow_yellow;

    lv_img_dsc_t photo_icon;
    lv_img_dsc_t camera_icon;
    lv_img_dsc_t music_icon;
    lv_img_dsc_t weather_icon;
    lv_img_dsc_t clock_icon;
    lv_img_dsc_t alarm_icon;
    lv_img_dsc_t calendar_icon;
    lv_img_dsc_t record_icon;
    lv_img_dsc_t file_icon;
#define DEVICE_MODE_ICON_MAX 6
    lv_img_dsc_t setting_icon;
    lv_img_dsc_t detection_icon;
    lv_img_dsc_t call_icon;
    lv_img_dsc_t device_mode_icons[DEVICE_MODE_ICON_MAX];

}personal_scr_res_t;

typedef struct
{
    lv_obj_t *personal_scr;
    lv_obj_t *title;
    lv_obj_t *content;
    lv_obj_t *role_name;

}lv_personal_ui_t;

typedef struct
{
    lv_obj_t *device_mode_scr;
}lv_device_mode_ui_t;

void setup_personal_center_scr(void);

void personal_center_scr_res_clear(void);

/**
 * @brief Refresh role_name label with current device mode and chat sub-mode
 * @return none
 */
void desk_personal_refresh_role_name(void);

void setup_device_mode_scr(void);

void device_mode_scr_res_clear(void);

#endif  // __DESK_PERSONAL_H__