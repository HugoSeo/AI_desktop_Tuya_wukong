#include "desk_event_handle.h"
#include "desk_handle_ui.h"
#include "desk_chat.h"
personal_scr_res_t s_personal_res = {0};

/* ---------------------------------------------------------------------------
 * Device mode page
 * --------------------------------------------------------------------------- */
STATIC CONST AI_DEVICE_MODE_E s_device_modes[] = {
    AI_DEVICE_MODE_CHAT,
    AI_DEVICE_MODE_TRANSLATE,
// #if defined(ENABLE_AI_MODE_P2P) && (ENABLE_AI_MODE_P2P == 1)
//     AI_DEVICE_MODE_P2P,
// #endif
//     AI_DEVICE_MODE_RECORD,
    AI_DEVICE_MODE_PICTURE,
    AI_DEVICE_MODE_DETECTION
};

STATIC CONST char *s_device_mode_names[] = {
    "闲聊模式", 
    "翻译模式",
// #if defined(ENABLE_AI_MODE_P2P) && (ENABLE_AI_MODE_P2P == 1)
//     "P2P模式",
// #endif
//     "录音模式", 
    "生图模式", 
    "侦测模式"
};

STATIC CONST char *s_device_mode_icon_res[] = {
    DEVICE_MODE_CHAT, 
    DEVICE_MODE_TRANSLATION,
// #if defined(ENABLE_AI_MODE_P2P) && (ENABLE_AI_MODE_P2P == 1)
//     DEVICE_MODE_P2P,
// #endif
//     DEVICE_MODE_RECORDING,
    DEVICE_MODE_PICTURE, 
    DEVICE_MODE_DETECTION
};

#define DEVICE_MODE_BTN_COUNT (sizeof(s_device_modes) / sizeof(s_device_modes[0]))

STATIC CONST char *s_chat_sub_mode_names[] = {
    "长按", "按键", "唤醒", "自由",
};

/**
 * @brief Get current mode display text for role_name label
 * @return mode text string (static buffer, do not free)
 * @note For chat mode, returns "闲聊模式:  <sub-mode>"; otherwise returns device mode name
 */
STATIC CONST char *__get_role_name_text(void)
{
    STATIC char s_role_buf[64];
    AI_DEVICE_MODE_E cur_mode = tuya_ai_toy_device_mode_get();

    if (cur_mode == AI_DEVICE_MODE_CHAT) {
        AI_CHAT_SUB_MODE_E sub = tuya_ai_toy_trigger_mode_get();
        CONST char *sub_name = (sub >= 0 && sub < AI_CHAT_SUB_MAX)
                               ? s_chat_sub_mode_names[sub] : "";
        snprintf(s_role_buf, sizeof(s_role_buf), "闲聊模式:  %s", sub_name);
        return s_role_buf;
    }

    if (cur_mode == AI_DEVICE_MODE_P2P) {
        return "P2P模式";
    }

    for (int i = 0; i < (int)DEVICE_MODE_BTN_COUNT; i++) {
        if (s_device_modes[i] == cur_mode) {
            return s_device_mode_names[i];
        }
    }
    return s_device_mode_names[0];
}

/**
 * @brief Back button event handler for device mode page
 * @param[in] e LVGL event object
 * @return none
 */
static void device_mode_back_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) 
    {
        TAL_PR_DEBUG("[%s] clicked !!!!!!", __func__);

        lv_indev_wait_release(lv_indev_get_act());
        desk_handle_ui_back_to(DHUI_SCREEN_ID_PERSONAL_CENTER, LV_SCR_LOAD_ANIM_FADE_ON, DHUI_SWITCH_PERMANENT);
    }       
}

STATIC lv_obj_t *s_selected_mode_btn = NULL;

/**
 * @brief Device mode button click handler, sets the selected device mode
 * @param[in] e LVGL event object, user_data points to AI_DEVICE_MODE_E value
 * @return none
 */
static void device_mode_btn_clicked(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        CONST AI_DEVICE_MODE_E *mode_ptr = (CONST AI_DEVICE_MODE_E *)lv_event_get_user_data(e);
        if (mode_ptr == NULL) {
            return;
        }

        TAL_PR_DEBUG("device mode selected: %d", *mode_ptr);

        wukong_ai_device_mode_switch(*mode_ptr);
        desk_event_update_device_mode(*mode_ptr);

        lv_indev_wait_release(lv_indev_get_act());

        CHAR_T notify_buf[64];
        CONST char *mode_name = NULL;
        for (int i = 0; i < (int)DEVICE_MODE_BTN_COUNT; i++) {
            if (s_device_modes[i] == *mode_ptr) {
                mode_name = s_device_mode_names[i];
                break;
            }
        }
        snprintf(notify_buf, sizeof(notify_buf), "已成功切换到%s", mode_name ? mode_name : "未知模式");
        desk_chat_set_pending_notify(notify_buf);

        desk_handle_ui_switch_to(DHUI_SCREEN_ID_CHAT, LV_SCR_LOAD_ANIM_FADE_ON, DHUI_SWITCH_PERMANENT);
    }
}

/**
 * @brief Create the device mode selection screen with 6 mode buttons
 * @return none
 */
static void device_mode_page_create(void)
{
    lv_device_mode_ui_t *ui = &getContent()->st_device_mode;
    ui->device_mode_scr = lv_obj_create(NULL);
    lv_obj_set_size(ui->device_mode_scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(ui->device_mode_scr, lv_color_hex(0x25262A), 0);
    lv_obj_set_style_pad_all(ui->device_mode_scr, 0, 0);
    lv_obj_set_style_text_font(ui->device_mode_scr, &AlibabaPuHuiTi3_Regular16, 0);
    lv_obj_set_style_text_color(ui->device_mode_scr, lv_color_white(), 0);
    lv_obj_set_scrollbar_mode(ui->device_mode_scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(ui->device_mode_scr, LV_DIR_NONE);

    lv_obj_t *title = lv_obj_create(ui->device_mode_scr);
    lv_obj_remove_style_all(title);
    lv_obj_set_size(title, LV_HOR_RES, 50);
    lv_obj_set_pos(title, 0, 0);
    lv_obj_set_style_bg_opa(title, LV_OPA_TRANSP, 0);

    lv_obj_t *title_name = lv_label_create(title);
    lv_label_set_text(title_name, "设备模式");
    lv_obj_set_style_text_font(title_name, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_size(title_name, LV_SIZE_CONTENT, 20);
    lv_obj_align(title_name, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_align(title_name, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *back_btn = lv_btn_create(title);
    lv_obj_remove_style_all(back_btn);
    lv_obj_set_size(back_btn, 50, 50);
    lv_obj_set_pos(back_btn, 0, 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_tag(back_btn, NULL);
    lv_obj_add_event_cb(back_btn, device_mode_back_event, LV_EVENT_CLICKED, NULL);

    if (png_img_load(tuya_app_gui_get_picture_full_path(ICON_BACK_LEFT_24_24), &s_personal_res.back_icon) == 0) 
    {
        lv_obj_t *back_icon = lv_img_create(back_btn);
        lv_img_set_src(back_icon, &s_personal_res.back_icon);
        lv_obj_set_pos(back_icon, 13, 13);
        lv_obj_set_size(back_icon, 24, 24);
    }

    lv_obj_t *content = lv_obj_create(ui->device_mode_scr);
    lv_obj_set_size(content, LV_HOR_RES, LV_VER_RES - 50); 
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_pos(content, 0, 50);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);  

    AI_DEVICE_MODE_E cur_mode = tuya_ai_toy_device_mode_get();

    for (int i = 0; i < (int)DEVICE_MODE_BTN_COUNT; i++) {
        int col = i % 2;
        int row = i / 2;

        lv_obj_t *btn = lv_btn_create(content);
        lv_obj_remove_style_all(btn);
        lv_obj_set_size(btn, 144, 80);
        lv_obj_set_pos(btn, 10 + col * 156, 8 + row * 90);
        lv_obj_set_style_radius(btn, 32, 0);
        lv_obj_set_tag(btn, NULL);
        lv_obj_add_event_cb(btn, device_mode_btn_clicked, LV_EVENT_CLICKED,
                            (void *)&s_device_modes[i]);

        if (s_device_modes[i] == cur_mode) {
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x005CC4), 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
            s_selected_mode_btn = btn;
        } else {
            lv_obj_set_style_bg_color(btn, lv_color_hex(0xB8BDDE), 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_10, 0);
        }

        if (png_img_load(tuya_app_gui_get_picture_full_path(s_device_mode_icon_res[i]),
                         &s_personal_res.device_mode_icons[i]) == 0) {
            lv_obj_t *icon = lv_img_create(btn);
            lv_img_set_src(icon, &s_personal_res.device_mode_icons[i]);
            lv_obj_set_pos(icon, 0, 0);
            lv_obj_set_size(icon, 144, 80);
        }

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, s_device_mode_names[i]);
        lv_obj_set_size(label, LV_SIZE_CONTENT, 18);
        lv_obj_set_pos(label, 12, 48);
        lv_obj_set_style_text_font(label, &AlibabaPuHuiTi3_Regular18_Static, 0);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
    }
}

static void role_choices_clicked_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) 
    {
        TAL_PR_DEBUG("[%s] clicked !!!!!!", __func__);

        lv_indev_wait_release(lv_indev_get_act());
        desk_handle_ui_switch_to(DHUI_SCREEN_ID_DEVICE_MODE, LV_SCR_LOAD_ANIM_FADE_ON, DHUI_SWITCH_PERMANENT);
    }   
}

static void photo_func_clicked_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) 
    {
        TAL_PR_DEBUG("[%s] clicked !!!!!!", __func__);

        lv_indev_wait_release(lv_indev_get_act());
        desk_handle_ui_switch_to(DHUI_SCREEN_ID_PHOTO, LV_SCR_LOAD_ANIM_FADE_ON, DHUI_SWITCH_PERMANENT);
    }       
}

static void camera_func_clicked_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) 
    {
        TAL_PR_DEBUG("[%s] clicked !!!!!!", __func__);

        // lv_indev_wait_release(lv_indev_get_act());

        /* 通过 desk_handle_ui 异步切换到相机屏，由框架负责当前屏 res_clear 与目标屏 setup */
        desk_handle_ui_switch_to(DHUI_SCREEN_ID_CAMERA, LV_SCR_LOAD_ANIM_FADE_ON, DHUI_SWITCH_PERMANENT);
    }       
}

static void music_func_clicked_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) 
    {
        TAL_PR_DEBUG("[%s] clicked !!!!!!", __func__);
        
        lv_indev_wait_release(lv_indev_get_act());
        desk_handle_ui_switch_to(DHUI_SCREEN_ID_MUSIC, LV_SCR_LOAD_ANIM_FADE_ON, DHUI_SWITCH_PERMANENT);
    }       
}

static void weather_func_clicked_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) 
    {
        TAL_PR_DEBUG("[%s] clicked !!!!!!", __func__);
    }       
}

static void clock_func_clicked_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) 
    {
        TAL_PR_DEBUG("[%s] clicked !!!!!!", __func__);
    }       
}

static void alarm_func_clicked_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) 
    {
        TAL_PR_DEBUG("[%s] clicked !!!!!!", __func__);
    }       
}

static void calendar_func_clicked_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) 
    {
        TAL_PR_DEBUG("[%s] clicked !!!!!!", __func__);
    }       
}

static void record_func_clicked_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) 
    {
        TAL_PR_DEBUG("[%s] clicked !!!!!!", __func__);
        lv_indev_wait_release(lv_indev_get_act());
        desk_handle_ui_switch_to(DHUI_SCREEN_ID_RECORD, LV_SCR_LOAD_ANIM_FADE_ON, DHUI_SWITCH_PERMANENT);
    }       
}

static void file_func_clicked_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) 
    {
        TAL_PR_DEBUG("[%s] clicked !!!!!!", __func__);
    }       
}

static void settings_func_clicked_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        TAL_PR_DEBUG("[%s] clicked !!!!!!", __func__);

        lv_indev_wait_release(lv_indev_get_act());
        desk_handle_ui_switch_to(DHUI_SCREEN_ID_SETTINGS, LV_SCR_LOAD_ANIM_FADE_ON, DHUI_SWITCH_PERMANENT);
    }
}

static void detection_func_clicked_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        TAL_PR_DEBUG("[%s] clicked !!!!!!", __func__);

        lv_indev_wait_release(lv_indev_get_act());
        desk_handle_ui_switch_to(DHUI_SCREEN_ID_DETECTION, LV_SCR_LOAD_ANIM_FADE_ON, DHUI_SWITCH_PERMANENT);
    }
}

static void photo_func_btn_create(lv_obj_t **func_cont, int res)
{
    if (func_cont == NULL || *func_cont == NULL)
    {
        TAL_PR_ERR("[%s] create failed: func_cont or *func_cont is null.", __func__);
        return;
    }  

    lv_obj_t *func_btn = lv_btn_create(*func_cont);
    lv_obj_remove_style_all(func_btn);
    lv_obj_set_size(func_btn, 135, 96);
    lv_obj_set_pos(func_btn, 20, 82);
    lv_obj_set_style_bg_color(func_btn, lv_color_hex(0xB8BDDE), 0);
    lv_obj_set_style_bg_opa(func_btn, LV_OPA_10, 0);
    lv_obj_set_style_radius(func_btn, 24, 0);
    lv_obj_set_tag(func_btn, NULL);
    lv_obj_add_event_cb(func_btn, photo_func_clicked_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *func_label = lv_label_create(func_btn);
    lv_label_set_text(func_label, "相册");
    lv_obj_set_size(func_label, LV_SIZE_CONTENT, 20);
    lv_obj_set_pos(func_label, 20, 56);
    lv_obj_set_style_text_font(func_label, &AlibabaPuHuiTi3_Regular20, 0);
    lv_obj_set_style_text_color(func_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(func_label, LV_TEXT_ALIGN_CENTER, 0);   //label内部文本居中

    if(png_img_load(tuya_app_gui_get_picture_full_path(APP_ICON_PHOTO), &s_personal_res.photo_icon) == 0) 
    {
        lv_obj_t *func_icon = lv_img_create(func_btn);
        lv_img_set_src(func_icon, &s_personal_res.photo_icon);
        lv_obj_set_pos(func_icon, 20, 20);
        lv_obj_set_size(func_icon, 24, 24);
    }

    if(res == OPRT_OK)
    {
        lv_obj_t *arrow_icon = lv_img_create(func_btn);
        lv_img_set_src(arrow_icon, &s_personal_res.arrow_yellow);
        lv_obj_set_pos(arrow_icon, 95, 20);
        lv_obj_set_size(arrow_icon, 24, 24);
    }  
}

static void camera_func_btn_create(lv_obj_t **func_cont, int res)
{
    if (func_cont == NULL || *func_cont == NULL)
    {
        TAL_PR_ERR("[%s] create failed: func_cont or *func_cont is null.", __func__);
        return;
    }  

    lv_obj_t *func_btn = lv_btn_create(*func_cont);
    lv_obj_remove_style_all(func_btn);
    lv_obj_set_size(func_btn, 135, 96);
    lv_obj_set_pos(func_btn, 165, 82);
    lv_obj_set_style_bg_color(func_btn, lv_color_hex(0xB8BDDE), 0);
    lv_obj_set_style_bg_opa(func_btn, LV_OPA_10, 0);
    lv_obj_set_style_radius(func_btn, 24, 0);
    lv_obj_set_tag(func_btn, NULL);
    lv_obj_add_event_cb(func_btn, camera_func_clicked_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *func_label = lv_label_create(func_btn);
    lv_label_set_text(func_label, "相机");
    lv_obj_set_size(func_label, LV_SIZE_CONTENT, 20);
    lv_obj_set_pos(func_label, 20, 56);
    lv_obj_set_style_text_font(func_label, &AlibabaPuHuiTi3_Regular20, 0);
    lv_obj_set_style_text_color(func_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(func_label, LV_TEXT_ALIGN_CENTER, 0);   //label内部文本居中

    if(png_img_load(tuya_app_gui_get_picture_full_path(APP_ICON_CAMERA), &s_personal_res.camera_icon) == 0) 
    {
        lv_obj_t *func_icon = lv_img_create(func_btn);
        lv_img_set_src(func_icon, &s_personal_res.camera_icon);
        lv_obj_set_pos(func_icon, 20, 20);
        lv_obj_set_size(func_icon, 24, 24);
    }

    if(res == OPRT_OK)
    {
        lv_obj_t *arrow_icon = lv_img_create(func_btn);
        lv_img_set_src(arrow_icon, &s_personal_res.arrow_yellow);
        lv_obj_set_pos(arrow_icon, 95, 20);
        lv_obj_set_size(arrow_icon, 24, 24);
    }  
}

static void music_func_btn_create(lv_obj_t **func_cont, int res)
{
    if (func_cont == NULL || *func_cont == NULL)
    {
        TAL_PR_ERR("[%s] create failed: func_cont or *func_cont is null.", __func__);
        return;
    }  

    lv_obj_t *func_btn = lv_btn_create(*func_cont);
    lv_obj_remove_style_all(func_btn);
    lv_obj_set_size(func_btn, 135, 96);
    lv_obj_set_pos(func_btn, 20, 188);
    lv_obj_set_style_bg_color(func_btn, lv_color_hex(0xB8BDDE), 0);
    lv_obj_set_style_bg_opa(func_btn, LV_OPA_10, 0);
    lv_obj_set_style_radius(func_btn, 24, 0);
    lv_obj_set_tag(func_btn, NULL);
    lv_obj_add_event_cb(func_btn, music_func_clicked_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *func_label = lv_label_create(func_btn);
    lv_label_set_text(func_label, "音乐");
    lv_obj_set_size(func_label, LV_SIZE_CONTENT, 20);
    lv_obj_set_pos(func_label, 20, 56);
    lv_obj_set_style_text_font(func_label, &AlibabaPuHuiTi3_Regular20, 0);
    lv_obj_set_style_text_color(func_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(func_label, LV_TEXT_ALIGN_CENTER, 0);   //label内部文本居中

    if(png_img_load(tuya_app_gui_get_picture_full_path(APP_ICON_MUSIC), &s_personal_res.music_icon) == 0) 
    {
        lv_obj_t *func_icon = lv_img_create(func_btn);
        lv_img_set_src(func_icon, &s_personal_res.music_icon);
        lv_obj_set_pos(func_icon, 20, 20);
        lv_obj_set_size(func_icon, 24, 24);
    }

    if(res == OPRT_OK)
    {
        lv_obj_t *arrow_icon = lv_img_create(func_btn);
        lv_img_set_src(arrow_icon, &s_personal_res.arrow_yellow);
        lv_obj_set_pos(arrow_icon, 95, 20);
        lv_obj_set_size(arrow_icon, 24, 24);
    } 
}

static void weather_func_btn_create(lv_obj_t **func_cont, int res)
{
    if (func_cont == NULL || *func_cont == NULL)
    {
        TAL_PR_ERR("[%s] create failed: func_cont or *func_cont is null.", __func__);
        return;
    }  

    lv_obj_t *func_btn = lv_btn_create(*func_cont);
    lv_obj_remove_style_all(func_btn);
    lv_obj_set_size(func_btn, 135, 96);
    lv_obj_set_pos(func_btn, 165, 188);
    lv_obj_set_style_bg_color(func_btn, lv_color_hex(0xB8BDDE), 0);
    lv_obj_set_style_bg_opa(func_btn, LV_OPA_10, 0);
    lv_obj_set_style_radius(func_btn, 24, 0);
    lv_obj_set_tag(func_btn, NULL);
    lv_obj_add_event_cb(func_btn, weather_func_clicked_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *func_label = lv_label_create(func_btn);
    lv_label_set_text(func_label, "天气");
    lv_obj_set_size(func_label, LV_SIZE_CONTENT, 20);
    lv_obj_set_pos(func_label, 20, 56);
    lv_obj_set_style_text_font(func_label, &AlibabaPuHuiTi3_Regular20, 0);
    lv_obj_set_style_text_color(func_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(func_label, LV_TEXT_ALIGN_CENTER, 0);   //label内部文本居中

    if(png_img_load(tuya_app_gui_get_picture_full_path(APP_ICON_WEATHER), &s_personal_res.weather_icon) == 0) 
    {
        lv_obj_t *func_icon = lv_img_create(func_btn);
        lv_img_set_src(func_icon, &s_personal_res.weather_icon);
        lv_obj_set_pos(func_icon, 20, 20);
        lv_obj_set_size(func_icon, 24, 24);
    }

    if(res == OPRT_OK)
    {
        lv_obj_t *arrow_icon = lv_img_create(func_btn);
        lv_img_set_src(arrow_icon, &s_personal_res.arrow_yellow);
        lv_obj_set_pos(arrow_icon, 95, 20);
        lv_obj_set_size(arrow_icon, 24, 24);
    }  
}

static void clock_func_btn_create(lv_obj_t **func_cont, int res)
{
    if (func_cont == NULL || *func_cont == NULL)
    {
        TAL_PR_ERR("[%s] create failed: func_cont or *func_cont is null.", __func__);
        return;
    }  

    lv_obj_t *func_btn = lv_btn_create(*func_cont);
    lv_obj_remove_style_all(func_btn);
    lv_obj_set_size(func_btn, 135, 96);
    lv_obj_set_pos(func_btn, 20, 294);
    lv_obj_set_style_bg_color(func_btn, lv_color_hex(0xB8BDDE), 0);
    lv_obj_set_style_bg_opa(func_btn, LV_OPA_10, 0);
    lv_obj_set_style_radius(func_btn, 24, 0);
    lv_obj_set_tag(func_btn, NULL);
    lv_obj_add_event_cb(func_btn, clock_func_clicked_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *func_label = lv_label_create(func_btn);
    lv_label_set_text(func_label, "时钟");
    lv_obj_set_size(func_label, LV_SIZE_CONTENT, 20);
    lv_obj_set_pos(func_label, 20, 56);
    lv_obj_set_style_text_font(func_label, &AlibabaPuHuiTi3_Regular20, 0);
    lv_obj_set_style_text_color(func_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(func_label, LV_TEXT_ALIGN_CENTER, 0);   //label内部文本居中

    if(png_img_load(tuya_app_gui_get_picture_full_path(APP_ICON_CLOCK), &s_personal_res.clock_icon) == 0) 
    {
        lv_obj_t *func_icon = lv_img_create(func_btn);
        lv_img_set_src(func_icon, &s_personal_res.clock_icon);
        lv_obj_set_pos(func_icon, 20, 20);
        lv_obj_set_size(func_icon, 24, 24);
    }

    if(res == OPRT_OK)
    {
        lv_obj_t *arrow_icon = lv_img_create(func_btn);
        lv_img_set_src(arrow_icon, &s_personal_res.arrow_yellow);
        lv_obj_set_pos(arrow_icon, 95, 20);
        lv_obj_set_size(arrow_icon, 24, 24);
    }  
}

static void alarm_func_btn_create(lv_obj_t **func_cont, int res)
{
    if (func_cont == NULL || *func_cont == NULL)
    {
        TAL_PR_ERR("[%s] create failed: func_cont or *func_cont is null.", __func__);
        return;
    }  

    lv_obj_t *func_btn = lv_btn_create(*func_cont);
    lv_obj_remove_style_all(func_btn);
    lv_obj_set_size(func_btn, 135, 96);
    lv_obj_set_pos(func_btn, 165, 294);
    lv_obj_set_style_bg_color(func_btn, lv_color_hex(0xB8BDDE), 0);
    lv_obj_set_style_bg_opa(func_btn, LV_OPA_10, 0);
    lv_obj_set_style_radius(func_btn, 24, 0);
    lv_obj_set_tag(func_btn, NULL);
    lv_obj_add_event_cb(func_btn, alarm_func_clicked_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *func_label = lv_label_create(func_btn);
    lv_label_set_text(func_label, "闹钟");
    lv_obj_set_size(func_label, LV_SIZE_CONTENT, 20);
    lv_obj_set_pos(func_label, 20, 56);
    lv_obj_set_style_text_font(func_label, &AlibabaPuHuiTi3_Regular20, 0);
    lv_obj_set_style_text_color(func_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(func_label, LV_TEXT_ALIGN_CENTER, 0);   //label内部文本居中

    if(png_img_load(tuya_app_gui_get_picture_full_path(APP_ICON_ALARM), &s_personal_res.alarm_icon) == 0) 
    {
        lv_obj_t *func_icon = lv_img_create(func_btn);
        lv_img_set_src(func_icon, &s_personal_res.alarm_icon);
        lv_obj_set_pos(func_icon, 20, 20);
        lv_obj_set_size(func_icon, 24, 24);
    }

    if(res == OPRT_OK)
    {
        lv_obj_t *arrow_icon = lv_img_create(func_btn);
        lv_img_set_src(arrow_icon, &s_personal_res.arrow_yellow);
        lv_obj_set_pos(arrow_icon, 95, 20);
        lv_obj_set_size(arrow_icon, 24, 24);
    }    
}

static void calendar_func_btn_create(lv_obj_t **func_cont, int res)
{
    if (func_cont == NULL || *func_cont == NULL)
    {
        TAL_PR_ERR("[%s] create failed: func_cont or *func_cont is null.", __func__);
        return;
    }  

    lv_obj_t *func_btn = lv_btn_create(*func_cont);
    lv_obj_remove_style_all(func_btn);
    lv_obj_set_size(func_btn, 135, 96);
    lv_obj_set_pos(func_btn, 20, 400);
    lv_obj_set_style_bg_color(func_btn, lv_color_hex(0xB8BDDE), 0);
    lv_obj_set_style_bg_opa(func_btn, LV_OPA_10, 0);
    lv_obj_set_style_radius(func_btn, 24, 0);
    lv_obj_set_tag(func_btn, NULL);
    lv_obj_add_event_cb(func_btn, calendar_func_clicked_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *func_label = lv_label_create(func_btn);
    lv_label_set_text(func_label, "日历");
    lv_obj_set_size(func_label, LV_SIZE_CONTENT, 20);
    lv_obj_set_pos(func_label, 20, 56);
    lv_obj_set_style_text_font(func_label, &AlibabaPuHuiTi3_Regular20, 0);
    lv_obj_set_style_text_color(func_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(func_label, LV_TEXT_ALIGN_CENTER, 0);   //label内部文本居中

    if(png_img_load(tuya_app_gui_get_picture_full_path(APP_ICON_CALENDAR), &s_personal_res.calendar_icon) == 0) 
    {
        lv_obj_t *func_icon = lv_img_create(func_btn);
        lv_img_set_src(func_icon, &s_personal_res.calendar_icon);
        lv_obj_set_pos(func_icon, 20, 20);
        lv_obj_set_size(func_icon, 24, 24);
    }

    if(res == OPRT_OK)
    {
        lv_obj_t *arrow_icon = lv_img_create(func_btn);
        lv_img_set_src(arrow_icon, &s_personal_res.arrow_yellow);
        lv_obj_set_pos(arrow_icon, 95, 20);
        lv_obj_set_size(arrow_icon, 24, 24);
    }    
}

static void record_func_btn_create(lv_obj_t **func_cont, int res)
{
    if (func_cont == NULL || *func_cont == NULL)
    {
        TAL_PR_ERR("[%s] create failed: func_cont or *func_cont is null.", __func__);
        return;
    }  

    lv_obj_t *func_btn = lv_btn_create(*func_cont);
    lv_obj_remove_style_all(func_btn);
    lv_obj_set_size(func_btn, 135, 96);
    // lv_obj_set_pos(func_btn, 165, 400);
    lv_obj_set_pos(func_btn, 165, 188);
    lv_obj_set_style_bg_color(func_btn, lv_color_hex(0xB8BDDE), 0);
    lv_obj_set_style_bg_opa(func_btn, LV_OPA_10, 0);
    lv_obj_set_style_radius(func_btn, 24, 0);
    lv_obj_set_tag(func_btn, NULL);
    lv_obj_add_event_cb(func_btn, record_func_clicked_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *func_label = lv_label_create(func_btn);
    lv_label_set_text(func_label, "录音");
    lv_obj_set_size(func_label, LV_SIZE_CONTENT, 20);
    lv_obj_set_pos(func_label, 20, 56);
    lv_obj_set_style_text_font(func_label, &AlibabaPuHuiTi3_Regular20, 0);
    lv_obj_set_style_text_color(func_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(func_label, LV_TEXT_ALIGN_CENTER, 0);   //label内部文本居中

    if(png_img_load(tuya_app_gui_get_picture_full_path(APP_ICON_RECORD), &s_personal_res.record_icon) == 0) 
    {
        lv_obj_t *func_icon = lv_img_create(func_btn);
        lv_img_set_src(func_icon, &s_personal_res.record_icon);
        lv_obj_set_pos(func_icon, 20, 20);
        lv_obj_set_size(func_icon, 24, 24);
    }

    if(res == OPRT_OK)
    {
        lv_obj_t *arrow_icon = lv_img_create(func_btn);
        lv_img_set_src(arrow_icon, &s_personal_res.arrow_yellow);
        lv_obj_set_pos(arrow_icon, 95, 20);
        lv_obj_set_size(arrow_icon, 24, 24);
    }    
}

static void file_func_btn_create(lv_obj_t **func_cont, int res)
{
    if (func_cont == NULL || *func_cont == NULL)
    {
        TAL_PR_ERR("[%s] create failed: func_cont or *func_cont is null.", __func__);
        return;
    }  

    lv_obj_t *func_btn = lv_btn_create(*func_cont);
    lv_obj_remove_style_all(func_btn);
    lv_obj_set_size(func_btn, 135, 96);
    lv_obj_set_pos(func_btn, 20, 506);
    lv_obj_set_style_bg_color(func_btn, lv_color_hex(0xB8BDDE), 0);
    lv_obj_set_style_bg_opa(func_btn, LV_OPA_10, 0);
    lv_obj_set_style_radius(func_btn, 24, 0);
    lv_obj_set_tag(func_btn, NULL);
    lv_obj_add_event_cb(func_btn, file_func_clicked_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *func_label = lv_label_create(func_btn);
    lv_label_set_text(func_label, "文件管理");
    lv_obj_set_size(func_label, LV_SIZE_CONTENT, 20);
    lv_obj_set_pos(func_label, 20, 56);
    lv_obj_set_style_text_font(func_label, &AlibabaPuHuiTi3_Regular20, 0);
    lv_obj_set_style_text_color(func_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(func_label, LV_TEXT_ALIGN_CENTER, 0);   //label内部文本居中

    if(png_img_load(tuya_app_gui_get_picture_full_path(APP_ICON_FILE), &s_personal_res.file_icon) == 0) 
    {
        lv_obj_t *func_icon = lv_img_create(func_btn);
        lv_img_set_src(func_icon, &s_personal_res.file_icon);
        lv_obj_set_pos(func_icon, 20, 20);
        lv_obj_set_size(func_icon, 24, 24);
    }

    if(res == OPRT_OK)
    {
        lv_obj_t *arrow_icon = lv_img_create(func_btn);
        lv_img_set_src(arrow_icon, &s_personal_res.arrow_yellow);
        lv_obj_set_pos(arrow_icon, 95, 20);
        lv_obj_set_size(arrow_icon, 24, 24);
    }     
}

static void settings_func_btn_create(lv_obj_t **func_cont, int res)
{
    if (func_cont == NULL || *func_cont == NULL)
    {
        TAL_PR_ERR("[%s] create failed: func_cont or *func_cont is null.", __func__);
        return;
    }  

    lv_obj_t *func_btn = lv_btn_create(*func_cont);
    lv_obj_remove_style_all(func_btn);
    lv_obj_set_size(func_btn, 135, 96);
    // lv_obj_set_pos(func_btn, 165, 506);
    lv_obj_set_pos(func_btn, 20, 400);
    lv_obj_set_style_bg_color(func_btn, lv_color_hex(0xB8BDDE), 0);
    lv_obj_set_style_bg_opa(func_btn, LV_OPA_10, 0);
    lv_obj_set_style_radius(func_btn, 24, 0);
    lv_obj_set_tag(func_btn, NULL);
    lv_obj_add_event_cb(func_btn, settings_func_clicked_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *func_label = lv_label_create(func_btn);
    lv_label_set_text(func_label, "设置");
    lv_obj_set_size(func_label, LV_SIZE_CONTENT, 20);
    lv_obj_set_pos(func_label, 20, 56);
    lv_obj_set_style_text_font(func_label, &AlibabaPuHuiTi3_Regular20, 0);
    lv_obj_set_style_text_color(func_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(func_label, LV_TEXT_ALIGN_CENTER, 0);   //label内部文本居中

    if(png_img_load(tuya_app_gui_get_picture_full_path(APP_ICON_SETTINGS), &s_personal_res.setting_icon) == 0) 
    {
        lv_obj_t *func_icon = lv_img_create(func_btn);
        lv_img_set_src(func_icon, &s_personal_res.setting_icon);
        lv_obj_set_pos(func_icon, 20, 20);
        lv_obj_set_size(func_icon, 24, 24);
    }

    if(res == OPRT_OK)
    {
        lv_obj_t *arrow_icon = lv_img_create(func_btn);
        lv_img_set_src(arrow_icon, &s_personal_res.arrow_yellow);
        lv_obj_set_pos(arrow_icon, 95, 20);
        lv_obj_set_size(arrow_icon, 24, 24);
    }
}

/**
 * @brief Call button click handler, navigates to chat screen
 * @param[in] e LVGL event object
 * @return none
 */
static void call_func_clicked_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        TAL_PR_DEBUG("[%s] clicked !!!!!!", __func__);

        lv_indev_wait_release(lv_indev_get_act());
        desk_handle_ui_switch_to(DHUI_SCREEN_ID_CALL, LV_SCR_LOAD_ANIM_FADE_ON, DHUI_SWITCH_PERMANENT);
    }
}

/**
 * @brief Create call function button
 * @param[in] func_cont pointer to parent container object
 * @param[in] res arrow icon load result
 * @return none
 */
static void call_func_btn_create(lv_obj_t **func_cont, int res)
{
    if (func_cont == NULL || *func_cont == NULL)
    {
        TAL_PR_ERR("[%s] create failed: func_cont or *func_cont is null.", __func__);
        return;
    }

    lv_obj_t *func_btn = lv_btn_create(*func_cont);
    lv_obj_remove_style_all(func_btn);
    lv_obj_set_size(func_btn, 135, 96);
    lv_obj_set_pos(func_btn, 20, 294);
    lv_obj_set_style_bg_color(func_btn, lv_color_hex(0xB8BDDE), 0);
    lv_obj_set_style_bg_opa(func_btn, LV_OPA_10, 0);
    lv_obj_set_style_radius(func_btn, 24, 0);
    lv_obj_set_tag(func_btn, NULL);
    lv_obj_add_event_cb(func_btn, call_func_clicked_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *func_label = lv_label_create(func_btn);
    lv_label_set_text(func_label, "通话");
    lv_obj_set_size(func_label, LV_SIZE_CONTENT, 20);
    lv_obj_set_pos(func_label, 20, 56);
    lv_obj_set_style_text_font(func_label, &AlibabaPuHuiTi3_Regular20, 0);
    lv_obj_set_style_text_color(func_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(func_label, LV_TEXT_ALIGN_CENTER, 0);

    if(png_img_load(tuya_app_gui_get_picture_full_path(APP_ICON_CALL), &s_personal_res.call_icon) == 0)
    {
        lv_obj_t *func_icon = lv_img_create(func_btn);
        lv_img_set_src(func_icon, &s_personal_res.call_icon);
        lv_obj_set_pos(func_icon, 20, 20);
        lv_obj_set_size(func_icon, 24, 24);
    }

    if(res == OPRT_OK)
    {
        lv_obj_t *arrow_icon = lv_img_create(func_btn);
        lv_img_set_src(arrow_icon, &s_personal_res.arrow_yellow);
        lv_obj_set_pos(arrow_icon, 95, 20);
        lv_obj_set_size(arrow_icon, 24, 24);
    }
}

static void detection_func_btn_create(lv_obj_t **func_cont, int res)
{
    if (func_cont == NULL || *func_cont == NULL)
    {
        TAL_PR_ERR("[%s] create failed: func_cont or *func_cont is null.", __func__);
        return;
    }

    lv_obj_t *func_btn = lv_btn_create(*func_cont);
    lv_obj_remove_style_all(func_btn);
    lv_obj_set_size(func_btn, 135, 96);
    lv_obj_set_pos(func_btn, 165, 294);
    lv_obj_set_style_bg_color(func_btn, lv_color_hex(0xB8BDDE), 0);
    lv_obj_set_style_bg_opa(func_btn, LV_OPA_10, 0);
    lv_obj_set_style_radius(func_btn, 24, 0);
    lv_obj_set_tag(func_btn, NULL);
    lv_obj_add_event_cb(func_btn, detection_func_clicked_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *func_label = lv_label_create(func_btn);
    lv_label_set_text(func_label, "侦测");
    lv_obj_set_size(func_label, LV_SIZE_CONTENT, 20);
    lv_obj_set_pos(func_label, 20, 56);
    lv_obj_set_style_text_font(func_label, &AlibabaPuHuiTi3_Regular20, 0);
    lv_obj_set_style_text_color(func_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(func_label, LV_TEXT_ALIGN_CENTER, 0);

    if(png_img_load(tuya_app_gui_get_picture_full_path(APP_ICON_DETECTION), &s_personal_res.detection_icon) == 0)
    {
        lv_obj_t *func_icon = lv_img_create(func_btn);
        lv_img_set_src(func_icon, &s_personal_res.detection_icon);
        lv_obj_set_pos(func_icon, 20, 20);
        lv_obj_set_size(func_icon, 24, 24);
    }

    if(res == OPRT_OK)
    {
        lv_obj_t *arrow_icon = lv_img_create(func_btn);
        lv_img_set_src(arrow_icon, &s_personal_res.arrow_yellow);
        lv_obj_set_pos(arrow_icon, 95, 20);
        lv_obj_set_size(arrow_icon, 24, 24);
    }
}

static void create_func_btn_list(void)
{
    lv_personal_ui_t *ui = &getContent()->st_personal;   

    // 功能按钮容器
    lv_obj_t *func_cont = lv_obj_create(ui->content);
    lv_obj_remove_style_all(func_cont);
    // lv_obj_set_size(func_cont, 320, 627);
    lv_obj_set_size(func_cont, 320, LV_SIZE_CONTENT);
    lv_obj_set_pos(func_cont, 0, 0);
    lv_obj_set_style_border_width(func_cont, 0, 0);
    lv_obj_set_scroll_dir(func_cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(func_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(func_cont, 0, 0);
    lv_obj_set_style_pad_bottom(func_cont, 20, 0);
    lv_obj_set_style_bg_opa(ui->content, LV_OPA_TRANSP, 0);

    lv_obj_t *role_choice_btn = lv_btn_create(func_cont);
    lv_obj_remove_style_all(role_choice_btn);
    lv_obj_set_size(role_choice_btn, 280, 64);
    lv_obj_set_pos(role_choice_btn, 20, 8);
    lv_obj_set_style_bg_color(role_choice_btn, lv_color_hex(0x005CC4), 0);
    lv_obj_set_style_bg_opa(role_choice_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(role_choice_btn, 32, 0);
    lv_obj_set_tag(role_choice_btn, NULL);
    lv_obj_add_event_cb(role_choice_btn, role_choices_clicked_event, LV_EVENT_CLICKED, NULL);

    if(png_img_load(tuya_app_gui_get_picture_full_path(DEVICE_MODE_SWITCHER_ENTRY), &s_personal_res.role_icon) == 0) 
    {
        lv_obj_t *role_img = lv_img_create(role_choice_btn);
        lv_img_set_src(role_img, &s_personal_res.role_icon);
        lv_obj_set_pos(role_img, 0, 0);
        lv_obj_set_size(role_img, 280, 64);
    }

    ui->role_name = lv_label_create(role_choice_btn);
    lv_label_set_text(ui->role_name, __get_role_name_text());
    lv_obj_set_size(ui->role_name, LV_SIZE_CONTENT, 20);
    lv_obj_align(ui->role_name, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_set_style_text_font(ui->role_name, &AlibabaPuHuiTi3_Regular20, 0);
    lv_obj_set_style_text_color(ui->role_name, lv_color_white(), 0);
    lv_obj_set_style_text_align(ui->role_name, LV_TEXT_ALIGN_LEFT, 0);

    int res = png_img_load(tuya_app_gui_get_picture_full_path(ICON_ICON_ARROW_YELLOW), &s_personal_res.arrow_yellow);

    //相册
    photo_func_btn_create(&func_cont, res);

    //相机
    camera_func_btn_create(&func_cont, res);

    //音乐
    music_func_btn_create(&func_cont, res);

    //天气
    // weather_func_btn_create(&func_cont, res);

    //时钟
    // clock_func_btn_create(&func_cont, res);

    //闹钟
    // alarm_func_btn_create(&func_cont, res);

    //日历
    // calendar_func_btn_create(&func_cont, res);

    //录音
    record_func_btn_create(&func_cont, res);

    //文件管理
    // file_func_btn_create(&func_cont, res);

    //通话
    call_func_btn_create(&func_cont, res);

    //侦测
    detection_func_btn_create(&func_cont, res);

    //设置
    settings_func_btn_create(&func_cont, res);
}

void personal_center_gesture_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_GESTURE)
    {
        TAL_PR_INFO("[%s] gesture", __func__);
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());

        if(dir == LV_DIR_LEFT)
        {
            lv_indev_wait_release(lv_indev_get_act());
            desk_handle_ui_back_to(DHUI_SCREEN_ID_CHAT,LV_SCR_LOAD_ANIM_MOVE_LEFT, DHUI_SWITCH_PERMANENT);
        }      
    }
}

void setup_personal_center_scr(void)
{
    lv_personal_ui_t *ui = &getContent()->st_personal;
    ui->personal_scr = lv_obj_create(NULL);
    lv_obj_set_size(ui->personal_scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(ui->personal_scr, lv_color_hex(0x25262A), 0);
    lv_obj_set_style_pad_all(ui->personal_scr, 0, 0);
    lv_obj_set_style_text_font(ui->personal_scr, &AlibabaPuHuiTi3_Regular16, 0);
    lv_obj_set_style_text_color(ui->personal_scr, lv_color_white(), 0);
    lv_obj_set_scrollbar_mode(ui->personal_scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(ui->personal_scr, LV_DIR_NONE);

    //标题容器
    ui->title = lv_obj_create(ui->personal_scr);
    lv_obj_remove_style_all(ui->title);
    lv_obj_set_size(ui->title, 320, 50);
    lv_obj_set_pos(ui->title, 0, 0);
    lv_obj_set_style_bg_opa(ui->title, LV_OPA_TRANSP, 0);

    lv_obj_t *title_name = lv_label_create(ui->title);
    lv_label_set_text(title_name, "个人中心");
    lv_obj_set_style_text_font(title_name, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_size(title_name, LV_SIZE_CONTENT, 20);
    lv_obj_align(title_name, LV_ALIGN_CENTER, 0, 0);    //相对于父对象居中
    lv_obj_set_style_text_align(title_name, LV_TEXT_ALIGN_CENTER, 0);   //label内部文本居中

    if(png_img_load(tuya_app_gui_get_picture_full_path(ICON_BACK_LEFT_24_24), &s_personal_res.back_icon) == 0) 
    {
        lv_obj_t *back_btn = lv_btn_create(ui->title);
        lv_obj_remove_style_all(back_btn);
        lv_obj_set_size(back_btn, 50, 50);
        lv_obj_set_pos(back_btn, 0, 0);
        lv_obj_set_style_bg_opa(back_btn, LV_OPA_TRANSP, 0);
        lv_obj_set_tag(back_btn, NULL);
        lv_obj_add_event_cb(back_btn, handle_personal_center_back_event, LV_EVENT_CLICKED, NULL);

        lv_obj_t *back_icon = lv_img_create(back_btn);
        lv_img_set_src(back_icon, &s_personal_res.back_icon);
        lv_obj_set_pos(back_icon, 13, 13);
        lv_obj_set_size(back_icon, 24, 24);

    }

    //内容容器
    ui->content = lv_obj_create(ui->personal_scr);
    lv_obj_set_size(ui->content, 320, 240 - 50); 
    lv_obj_set_flex_flow(ui->content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_border_width(ui->content, 0, 0);
    lv_obj_set_pos(ui->content, 0, 50);
    lv_obj_move_background(ui->content);
    lv_obj_set_scroll_dir(ui->content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ui->content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(ui->content, 0, 0);
    lv_obj_set_style_bg_opa(ui->content, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(ui->content, personal_center_gesture_event, LV_EVENT_GESTURE, NULL);

    //创建功能按键列表
    create_func_btn_list();

    lv_obj_update_layout(ui->personal_scr);

    setDeskUIIndex(DESKUI_INDEX_PERSONAL_CENTER);
}

void personal_center_scr_res_clear(void)
{
    TAL_PR_INFO("[%s] enter.", __func__);

    png_img_unload(&s_personal_res.back_icon);
    png_img_unload(&s_personal_res.role_icon);
    png_img_unload(&s_personal_res.arrow_yellow);
    png_img_unload(&s_personal_res.photo_icon);
    png_img_unload(&s_personal_res.camera_icon);
    png_img_unload(&s_personal_res.music_icon);
    png_img_unload(&s_personal_res.weather_icon);
    png_img_unload(&s_personal_res.clock_icon);
    png_img_unload(&s_personal_res.alarm_icon);
    png_img_unload(&s_personal_res.calendar_icon);
    png_img_unload(&s_personal_res.record_icon);
    png_img_unload(&s_personal_res.file_icon);
    png_img_unload(&s_personal_res.setting_icon);
    png_img_unload(&s_personal_res.detection_icon);
    png_img_unload(&s_personal_res.call_icon);
    memset(&s_personal_res, 0, sizeof(personal_scr_res_t));
}

/**
 * @brief Refresh role_name label with current device mode and chat sub-mode
 * @return none
 */
void desk_personal_refresh_role_name(void)
{
    lv_personal_ui_t *ui = &getContent()->st_personal;
    if (ui->role_name == NULL || lv_obj_is_valid(ui->role_name) == false) {
        return;
    }
    lv_label_set_text(ui->role_name, __get_role_name_text());
}

/**
 * @brief Setup device mode selection screen
 * @return none
 */
void setup_device_mode_scr(void)
{
    device_mode_page_create();
}

/**
 * @brief Release resources for device mode screen
 * @return none
 */
void device_mode_scr_res_clear(void)
{
    TAL_PR_INFO("[%s] enter.", __func__);
    png_img_unload(&s_personal_res.back_icon);
    for (int i = 0; i < DEVICE_MODE_ICON_MAX; i++) {
        png_img_unload(&s_personal_res.device_mode_icons[i]);
    }
}