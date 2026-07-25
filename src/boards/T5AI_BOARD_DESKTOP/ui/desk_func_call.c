/**
 * @file desk_func_call.c
 * @brief Call screen UI implementation
 * @version 1.0
 * @date 2025-04-16
 * @copyright Copyright (c) Tuya Inc.
 */
#include "desk_event_handle.h"
#include "desk_handle_ui.h"
#include "tuya_cloud_types.h"
#include "tuya_sdk_call.h"
#include "tal_sw_timer.h"

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define CALL_TITLE_BAR_W        320
#define CALL_TITLE_BAR_H        50
#define CALL_CONTENT_W          320
#define CALL_CONTENT_H          190
#define CALL_BACK_BTN_SIZE      50
#define CALL_IMG_SIZE           145
#define CALL_BTN_SIZE           65
#define CALL_LABEL_GAP          20
#define CALL_HALF_W             160

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC call_scr_res_t s_call_res = {0};
STATIC CALL_STATUS_E s_call_status = CALL_STATUS_IDLE;
STATIC TIMER_ID s_call_timeout_timer = NULL;
STATIC TIMER_ID s_call_fail_hide_timer = NULL;

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC VOID_T __call_title_bar_create(VOID_T);
STATIC VOID_T __call_content_create(VOID_T);
STATIC VOID_T __call_status_label_create(VOID_T);
STATIC VOID_T __call_update_status_label(VOID_T);
STATIC VOID_T __call_cancel_timers(VOID_T);
STATIC VOID_T __call_timeout_cb(TIMER_ID timer_id, VOID_T *arg);
STATIC VOID_T __call_fail_hide_cb(TIMER_ID timer_id, VOID_T *arg);
STATIC VOID_T __call_back_btn_clicked_cb(lv_event_t *e);
STATIC VOID_T __call_hangup_btn_clicked_cb(lv_event_t *e);
STATIC VOID_T __call_answer_btn_clicked_cb(lv_event_t *e);
STATIC VOID_T __call_action_item_create(lv_obj_t *parent, lv_img_dsc_t *icon_res,
                                         CONST CHAR_T *icon_path, CONST CHAR_T *label_text,
                                         lv_event_cb_t btn_cb, lv_obj_t **btn_out,
                                         lv_coord_t x_offset);

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */

int call_status_event(void *data)
{
    TUYA_CHECK_NULL_RETURN(data, OPRT_COM_ERROR);
    MEDIA_STREAM_EVENT_E event = *(MEDIA_STREAM_EVENT_E *)data;

    switch(event)
    {
        case MEDIA_STREAM_LIVE_AUDIO_START:
        {
            __call_cancel_timers();
            s_call_status = CALL_STATUS_IN_CALL;
            __call_update_status_label();
        }
        break;

        case MEDIA_STREAM_LIVE_AUDIO_STOP:
        {
            __call_cancel_timers();
            s_call_status = CALL_STATUS_IDLE;
            __call_update_status_label();
        }
        break;

        default:
        break;
    }

    return OPRT_OK;
}

/**
 * @brief Back button click callback
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __call_back_btn_clicked_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        __call_cancel_timers();
        if (s_call_status != CALL_STATUS_IDLE) {
            TAL_PR_INFO("[call_ui] leaving call page, hanging up");
            s_call_status = CALL_STATUS_IDLE;
            TUYA_IPC_hangup();
        }
        desk_handle_ui_back(LV_SCR_LOAD_ANIM_FADE_ON, DHUI_SWITCH_PERMANENT);
    }
}

/**
 * @brief Hangup button click callback
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __call_hangup_btn_clicked_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        TAL_PR_INFO("[call_ui] hangup button clicked");
        __call_cancel_timers();
        s_call_status = CALL_STATUS_IDLE;
        __call_update_status_label();
        TUYA_IPC_hangup();
    }
}

/**
 * @brief Answer button click callback
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __call_answer_btn_clicked_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        TAL_PR_INFO("[call_ui] answer button clicked");
        __call_cancel_timers();
        s_call_status = CALL_STATUS_CALLING;
        __call_update_status_label();
        TUYA_IPC_call_app();
        if (s_call_timeout_timer != NULL) {
            tal_sw_timer_start(s_call_timeout_timer, 30000, TAL_TIMER_ONCE);
        }
    }
}

/**
 * @brief Create title bar with back button and title label
 * @return none
 */
STATIC VOID_T __call_title_bar_create(VOID_T)
{
    lv_call_ui_t *ui = &getContent()->st_func_call;

    ui->title_bar = lv_obj_create(ui->call_scr);
    lv_obj_remove_style_all(ui->title_bar);
    lv_obj_set_size(ui->title_bar, CALL_TITLE_BAR_W, CALL_TITLE_BAR_H);
    lv_obj_set_pos(ui->title_bar, 0, 0);
    lv_obj_set_style_bg_opa(ui->title_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui->title_bar, 0, 0);
    lv_obj_set_style_pad_all(ui->title_bar, 0, 0);

    /* Back button: top-left 50x50 */
    lv_obj_t *back_btn = lv_btn_create(ui->title_bar);
    lv_obj_remove_style_all(back_btn);
    lv_obj_set_size(back_btn, CALL_BACK_BTN_SIZE, CALL_BACK_BTN_SIZE);
    lv_obj_set_pos(back_btn, 0, 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(back_btn, __call_back_btn_clicked_cb, LV_EVENT_CLICKED, NULL);
    if (png_img_load(tuya_app_gui_get_picture_full_path(ICON_BACK_LEFT_24_24),
                     &s_call_res.back_icon) == 0) {
        lv_obj_t *back_icon = lv_img_create(back_btn);
        lv_img_set_src(back_icon, &s_call_res.back_icon);
        lv_obj_align(back_icon, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_size(back_icon, 24, 24);
    }

    /* Title label: centered in title bar */
    lv_obj_t *title_label = lv_label_create(ui->title_bar);
    lv_label_set_text(title_label, "通话");
    lv_obj_set_style_text_font(title_label, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
    lv_obj_set_size(title_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(title_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
}

/**
 * @brief Create a call action item (image + overlaid button + label)
 * @param[in] parent parent container
 * @param[in] icon_res pointer to image resource descriptor
 * @param[in] icon_path resource file name
 * @param[in] label_text text for the label below button
 * @param[in] btn_cb button click event callback
 * @param[out] btn_out optional, store the button object pointer
 * @param[in] x_offset horizontal position of the item container
 * @return none
 */
STATIC VOID_T __call_action_item_create(lv_obj_t *parent, lv_img_dsc_t *icon_res,
                                         CONST CHAR_T *icon_path, CONST CHAR_T *label_text,
                                         lv_event_cb_t btn_cb, lv_obj_t **btn_out,
                                         lv_coord_t x_offset)
{
    /* Half-width container for one action item */
    lv_obj_t *item_cont = lv_obj_create(parent);
    lv_obj_remove_style_all(item_cont);
    lv_obj_set_size(item_cont, CALL_HALF_W, CALL_CONTENT_H);
    lv_obj_set_pos(item_cont, x_offset, 0);
    lv_obj_set_style_bg_opa(item_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(item_cont, 0, 0);
    lv_obj_set_style_pad_all(item_cont, 0, 0);
    lv_obj_clear_flag(item_cont, LV_OBJ_FLAG_SCROLLABLE);

    /* Image: 145x145, centered with slight upward shift for label space */
    lv_obj_t *img_obj = NULL;
    if (png_img_load(tuya_app_gui_get_picture_full_path(icon_path), icon_res) == 0) {
        img_obj = lv_img_create(item_cont);
        lv_img_set_src(img_obj, icon_res);
        lv_obj_set_size(img_obj, CALL_IMG_SIZE, CALL_IMG_SIZE);
        lv_obj_align(img_obj, LV_ALIGN_CENTER, 0, -12);
    }

    /* Button: 65x65, centered on image */
    lv_obj_t *btn = lv_btn_create(item_cont);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, CALL_BTN_SIZE, CALL_BTN_SIZE);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, -12);
    lv_obj_add_event_cb(btn, btn_cb, LV_EVENT_CLICKED, NULL);
    if (btn_out != NULL) {
        *btn_out = btn;
    }

    /* Label: font 20, 20px below button bottom */
    lv_obj_t *label = lv_label_create(item_cont);
    lv_label_set_text(label, label_text);
    lv_obj_set_style_text_font(label, &AlibabaPuHuiTi3_Regular20, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_size(label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(label, btn, LV_ALIGN_OUT_BOTTOM_MID, 0, CALL_LABEL_GAP);
}

/**
 * @brief Create content area with hangup and answer action items
 * @return none
 */
STATIC VOID_T __call_content_create(VOID_T)
{
    lv_call_ui_t *ui = &getContent()->st_func_call;

    ui->content_cont = lv_obj_create(ui->call_scr);
    lv_obj_remove_style_all(ui->content_cont);
    lv_obj_set_size(ui->content_cont, CALL_CONTENT_W, CALL_CONTENT_H);
    lv_obj_set_pos(ui->content_cont, 0, CALL_TITLE_BAR_H);
    lv_obj_set_style_bg_opa(ui->content_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui->content_cont, 0, 0);
    lv_obj_set_style_pad_all(ui->content_cont, 0, 0);
    lv_obj_clear_flag(ui->content_cont, LV_OBJ_FLAG_SCROLLABLE);

    /* Left side: hangup action */
    __call_action_item_create(ui->content_cont, &s_call_res.hangup_icon,
                              CALL_HANGUP_ICON, "挂断",
                              __call_hangup_btn_clicked_cb, &ui->hangup_btn, 0);

    /* Right side: answer action */
    __call_action_item_create(ui->content_cont, &s_call_res.answer_icon,
                              CALL_ANSWER_ICON, "呼叫",
                              __call_answer_btn_clicked_cb, &ui->answer_btn, CALL_HALF_W);
}

/**
 * @brief Update status label visibility and text based on current call status
 * @return none
 */
STATIC VOID_T __call_update_status_label(VOID_T)
{
    lv_call_ui_t *ui = &getContent()->st_func_call;
    if (ui->status_label == NULL) {
        return;
    }

    switch (s_call_status) {
        case CALL_STATUS_CALLING:
            lv_label_set_text(ui->status_label, "呼叫中...");
            lv_obj_clear_flag(ui->status_label, LV_OBJ_FLAG_HIDDEN);
            break;
        case CALL_STATUS_IN_CALL:
            lv_label_set_text(ui->status_label, "通话中...");
            lv_obj_clear_flag(ui->status_label, LV_OBJ_FLAG_HIDDEN);
            break;
        default:
            lv_obj_add_flag(ui->status_label, LV_OBJ_FLAG_HIDDEN);
            break;
    }
}

/**
 * @brief Create status label (hidden by default, centered above buttons)
 * @return none
 */
STATIC VOID_T __call_status_label_create(VOID_T)
{
    lv_call_ui_t *ui = &getContent()->st_func_call;

    ui->status_label = lv_label_create(ui->call_scr);
    lv_label_set_text(ui->status_label, "");
    lv_obj_set_style_text_font(ui->status_label, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(ui->status_label, lv_color_white(), 0);
    lv_obj_set_size(ui->status_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(ui->status_label, LV_ALIGN_TOP_MID, 0, CALL_TITLE_BAR_H + 8);
    lv_obj_set_style_text_align(ui->status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(ui->status_label, LV_OBJ_FLAG_HIDDEN);
}

STATIC VOID_T __call_cancel_timers(VOID_T)
{
    if (s_call_timeout_timer != NULL) {
        tal_sw_timer_stop(s_call_timeout_timer);
    }
    if (s_call_fail_hide_timer != NULL) {
        tal_sw_timer_stop(s_call_fail_hide_timer);
    }
}

STATIC VOID_T __call_fail_hide_cb(TIMER_ID timer_id, VOID_T *arg)
{
    s_call_status = CALL_STATUS_IDLE;
    __call_update_status_label();
}

STATIC VOID_T __call_timeout_cb(TIMER_ID timer_id, VOID_T *arg)
{
    TAL_PR_WARN("[call_ui] call timeout, hanging up");

    s_call_status = CALL_STATUS_IDLE;
    lv_call_ui_t *ui = &getContent()->st_func_call;
    if (ui->status_label != NULL) {
        lv_label_set_text(ui->status_label, "呼叫失败");
        lv_obj_clear_flag(ui->status_label, LV_OBJ_FLAG_HIDDEN);
    }

    TUYA_IPC_hangup();

    if (s_call_fail_hide_timer != NULL) {
        tal_sw_timer_start(s_call_fail_hide_timer, 1500, TAL_TIMER_ONCE);
    }
}

/**
 * @brief Create and initialize the call screen
 * @return none
 */
void setup_call_scr(void)
{
    TAL_PR_INFO("[%s] enter", __func__);
    lv_call_ui_t *ui = &getContent()->st_func_call;

    ui->call_scr = lv_obj_create(NULL);
    lv_obj_set_size(ui->call_scr, DESK_LCD_WIDTH, DESK_LCD_HEIGHT);
    lv_obj_set_style_bg_color(ui->call_scr, lv_color_hex(0x25262A), 0);
    lv_obj_set_style_bg_opa(ui->call_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(ui->call_scr, 0, 0);
    lv_obj_set_scrollbar_mode(ui->call_scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(ui->call_scr, LV_DIR_NONE);

    __call_title_bar_create();
    __call_content_create();
    __call_status_label_create();
    __call_update_status_label();

    if (s_call_timeout_timer == NULL) {
        tal_sw_timer_create(__call_timeout_cb, NULL, &s_call_timeout_timer);
    }
    if (s_call_fail_hide_timer == NULL) {
        tal_sw_timer_create(__call_fail_hide_cb, NULL, &s_call_fail_hide_timer);
    }

    lv_obj_update_layout(ui->call_scr);
}

/**
 * @brief Release all resources held by the call screen
 * @return none
 */
void call_scr_res_clear(void)
{
    TAL_PR_INFO("[%s] enter", __func__);
    lv_call_ui_t *ui = &getContent()->st_func_call;

    __call_cancel_timers();
    if (s_call_timeout_timer != NULL) {
        tal_sw_timer_delete(s_call_timeout_timer);
        s_call_timeout_timer = NULL;
    }
    if (s_call_fail_hide_timer != NULL) {
        tal_sw_timer_delete(s_call_fail_hide_timer);
        s_call_fail_hide_timer = NULL;
    }

    png_img_unload(&s_call_res.back_icon);
    png_img_unload(&s_call_res.hangup_icon);
    png_img_unload(&s_call_res.answer_icon);
    memset(&s_call_res, 0, sizeof(call_scr_res_t));

    ui->call_scr = NULL;
    ui->title_bar = NULL;
    ui->content_cont = NULL;
    ui->hangup_btn = NULL;
    ui->answer_btn = NULL;
    ui->status_label = NULL;
}
