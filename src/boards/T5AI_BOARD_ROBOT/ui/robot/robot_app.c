#include <string.h>
#include "tuya_ai_display.h"
#include "gui_common.h"
#include "lvgl.h"
#include "lv_tef.h"
#include "tef_assets.h"   /* robot_tef/: 表情 + 状态栏动图注册表 */

LV_FONT_DECLARE(puhui_robot);
LV_FONT_DECLARE(font_awesome_16_4);

static lv_obj_t *tef_full;
static lv_obj_t *tef_stat;
static int current_tef_index = -1;

/* GUI_STAT_x -> 状态栏 TEF 素材（顺序与 gui_common 的 index 表一致） */
typedef struct {
    const uint8_t *data;
    const uint32_t *len;
} robot_stat_tef_t;

static const robot_stat_tef_t s_stat_tef[GUI_STAT_MAX] = {
    [GUI_STAT_INIT]   = {g_tef_stat_initializing, &g_tef_stat_initializing_len},
    [GUI_STAT_IDLE]   = {g_tef_stat_waiting,      &g_tef_stat_waiting_len},
    [GUI_STAT_LISTEN] = {g_tef_stat_listening,    &g_tef_stat_listening_len},
    [GUI_STAT_UPLOAD] = {g_tef_stat_uploading,    &g_tef_stat_uploading_len},
    [GUI_STAT_THINK]  = {g_tef_stat_thinking,     &g_tef_stat_thinking_len},
    [GUI_STAT_SPEAK]  = {g_tef_stat_speaking,     &g_tef_stat_speaking_len},
    [GUI_STAT_PROV]   = {g_tef_stat_provisioning, &g_tef_stat_provisioning_len},
    [GUI_STAT_CONN]   = {g_tef_stat_connecting,   &g_tef_stat_connecting_len},
};

/* 云端表情名 -> 素材名别名（无独立素材时复用近似表情） */
static const char *robot_emotion_alias(const char *emotion)
{
    if (emotion && strcmp(emotion, "angry") == 0) {
        return "annoyed";
    }
    return emotion;
}

void robot_emotion_flush(char *emotion)
{
    const char *name = robot_emotion_alias(emotion);
    int index = 0;   /* 未命中回落注册表第 0 项（neutral） */

    for (uint32_t i = 0; i < g_tef_asset_cnt; i++) {
        if (name && strcmp(g_tef_assets[i].name, name) == 0) {
            index = (int)i;
            break;
        }
    }

    if (current_tef_index == index) {
        return;
    }

    current_tef_index = index;

    lv_tef_set_src(tef_full, g_tef_assets[index].data, g_tef_assets[index].len);
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

    tef_stat = lv_tef_create(status_bar_);
    lv_obj_set_height(tef_stat, puhui_robot.line_height);

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
    char *text;

    if (OPRT_OK != gui_status_desc_get(stat, (VOID **)&text)) {
        return;
    }

    lv_label_set_text(status_label_, text);
    lv_obj_align_to(tef_stat, status_label_, LV_ALIGN_OUT_LEFT_MID, -5, -1);
    if (stat >= 0 && stat < GUI_STAT_MAX && s_stat_tef[stat].data) {
        lv_tef_set_src(tef_stat, s_stat_tef[stat].data, *s_stat_tef[stat].len);
    }
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

    tef_full = lv_tef_create(container_);
    lv_obj_set_size(tef_full, LV_HOR_RES, LV_VER_RES);

    robot_set_status(GUI_STAT_INIT);
    robot_emotion_flush("neutral");
    lv_obj_move_background(tef_full);
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
