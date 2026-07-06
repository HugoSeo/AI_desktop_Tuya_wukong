#include "tuya_ai_display.h"
#include "gui_common.h"
#include "lvgl.h"

LV_IMG_DECLARE(neutral);
LV_IMG_DECLARE(annoyed);
LV_IMG_DECLARE(cool);
LV_IMG_DECLARE(delicious);
LV_IMG_DECLARE(fearful);
LV_IMG_DECLARE(lovestruck);
LV_IMG_DECLARE(unamused);
LV_IMG_DECLARE(winking);
LV_IMG_DECLARE(zany);

LV_FONT_DECLARE(puhui_robot);
LV_FONT_DECLARE(font_awesome_16_4);

LV_IMG_DECLARE(angry3);
LV_IMG_DECLARE(confused);
LV_IMG_DECLARE(disappointed2);
LV_IMG_DECLARE(embarrassed);
LV_IMG_DECLARE(happy);
LV_IMG_DECLARE(laughing);
LV_IMG_DECLARE(relaxed);
LV_IMG_DECLARE(sad);
LV_IMG_DECLARE(surprise);
LV_IMG_DECLARE(thinking2);

static lv_obj_t *gif_full;
static lv_obj_t *gif_stat;
static int current_gif_index = -1;

static const gui_emotion_t s_gif_emotion[] = {
    {&neutral,          "neutral"},
    {&annoyed,          "annoyed"},
    {&cool,             "cool"},
    {&fearful,          "fearful"},
    {&lovestruck,       "lovestruck"},
    {&annoyed,          "angry"},
    {&happy,            "happy"},
    {&sad,              "sad"},
    {&surprise,         "surprise"},
    {&thinking2,        "thinking"},
    /*----------------------------------- */ //! FLASH ROM OVERFLOW
    // {&delicious,        "delicious"},
    // {&unamused,         "unamused"},
    // {&winking,          "winking"},
    // {&zany,             "zany"},
    // {&confused,         "confused" },
    // {&disappointed2,    "disappointed"},
    // {&embarrassed,      "embarrassed"},
    // {&laughing,         "laughing"},
    // {&relaxed,          "relaxed"},
};


void robot_emotion_flush(char  *emotion)
{
    uint8_t index = 0;

    index = gui_emotion_find(s_gif_emotion, CNTSOF(s_gif_emotion), emotion);
        
    if (current_gif_index == index) {
        return;
    }

    current_gif_index = index;

    lv_gif_set_src(gif_full, s_gif_emotion[index].source); 
}


static  lv_obj_t    *container_ ;
static  lv_obj_t    *status_bar_ ;
static  lv_obj_t    *battery_label_;
static  lv_obj_t    *network_label_;
static  lv_obj_t    *status_label_;


void robot_status_bar_init(lv_obj_t *container)
{
    /* Status bar */
    status_bar_ = lv_obj_create(container);
    lv_obj_set_size(status_bar_, LV_HOR_RES, puhui_robot.line_height);
    lv_obj_set_style_text_font(status_bar_, &puhui_robot, 0);
    lv_obj_set_style_bg_color(status_bar_, lv_color_black(), 0);
    lv_obj_set_style_text_color(status_bar_, lv_color_white(), 0);
    lv_obj_set_style_radius(status_bar_, 0, 0);

    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);

    network_label_ = lv_label_create(status_bar_);
    lv_obj_set_style_text_font(network_label_, &font_awesome_16_4, 0);
    lv_label_set_text(network_label_, FONT_AWESOME_WIFI_OFF);
    lv_obj_align(network_label_, LV_ALIGN_LEFT_MID, 10, 0);

    gif_stat = lv_gif_create(status_bar_);
    lv_obj_set_height(gif_stat, puhui_robot.line_height);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_height(status_bar_, puhui_robot.line_height);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(status_label_);

    battery_label_ = lv_label_create(status_bar_);
    lv_label_set_text(battery_label_, FONT_AWESOME_BATTERY_FULL);
    lv_obj_set_style_text_font(battery_label_, &font_awesome_16_4, 0);
    lv_obj_align(battery_label_, LV_ALIGN_RIGHT_MID, -10, 0);
}


void robot_set_status(int stat)
{
    char          *text;
    lv_img_dsc_t  *gif;

    if (OPRT_OK != gui_status_desc_get(stat, &text, &gif)) {
        return;
    }
    
    lv_label_set_text(status_label_, text);
    lv_obj_align_to(gif_stat, status_label_, LV_ALIGN_OUT_LEFT_MID, -5, -1);
    lv_gif_set_src(gif_stat, gif); 
}


void app_ui_init(void)
{
    /* Container */
    lv_obj_t * container_ = lv_obj_create(lv_scr_act());
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_scrollbar_mode(container_, LV_SCROLLBAR_MODE_OFF);

    robot_status_bar_init(container_);

    gif_full = lv_gif_create(container_);
    lv_obj_set_size(gif_full, LV_HOR_RES, LV_VER_RES);

    robot_set_status(GUI_STAT_INIT);
    robot_emotion_flush("neutral");
    lv_obj_move_background(gif_full);
}

void bk_uvc_camera_stream_start(void);
void bk_uvc_camera_stream_stop(void);

void app_ui_msg_handler(TY_DISPLAY_MSG_T *msg)
{

    switch (msg->type)
    {
        
    case TY_DISPLAY_TP_LANGUAGE:
        gui_lang_set(msg->data[0]);
        robot_set_status(GUI_STAT_INIT);
        break;

    case TY_DISPLAY_TP_EMOJI:
        robot_emotion_flush(msg->data);
        break;    

    case TY_DISPLAY_TP_STAT_CHARGING:
        lv_label_set_text(battery_label_, FONT_AWESOME_BATTERY_CHARGING); 
        break;

    case TY_DISPLAY_TP_STAT_BATTERY: 
        lv_label_set_text(battery_label_, gui_battery_level_get(msg->data[0]));
        break;

    case TY_DISPLAY_TP_STAT_NETCFG:
        robot_set_status(GUI_STAT_PROV);
        break;

    case TY_DISPLAY_TP_CHAT_STAT: {
        if (GUI_STAT_IDLE == msg->data[0]) {
            robot_emotion_flush("neutral");
        } else if (GUI_STAT_LISTEN == msg->data[0]) {
            robot_emotion_flush("neutral");
        } else if (GUI_STAT_UPLOAD == msg->data[0]) {
        }
        robot_set_status(msg->data[0]);
    } break;
 
    case TY_DISPLAY_TP_STAT_NET:
        if (msg->data[0]) {
            // robot_gif_load();
            robot_set_status(GUI_STAT_IDLE);
        }
        lv_label_set_text(network_label_, gui_wifi_level_get(msg->data[0]));
        break;

    default:
        break;
    }
}
