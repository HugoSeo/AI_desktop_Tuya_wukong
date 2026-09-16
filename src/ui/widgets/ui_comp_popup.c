#include "ui_comp_popup.h"
#include "ui_theme.h"
#include "ui_layout.h"
#include "ui_comp_btn.h"
#include "ui_comp_label.h"
#include "ui_adaptive.h"
#include "ui_i18n.h"

static lv_obj_t *s_overlay = NULL;
static lv_timer_t *s_toast_timer = NULL;
static ui_comp_popup_cb_t s_cb = NULL;

static void on_confirm(lv_event_t *e) {
    (void)e;
    if (s_cb) s_cb(true);
    ui_comp_popup_dismiss();
}

static void on_cancel(lv_event_t *e) {
    (void)e;
    if (s_cb) s_cb(false);
    ui_comp_popup_dismiss();
}

static void toast_timer_cb(lv_timer_t *timer) {
    (void)timer;
    s_toast_timer = NULL;
    ui_comp_popup_dismiss();
}

void ui_comp_popup_show(ui_comp_popup_type_t type, const char *title,
                        const char *msg, ui_comp_popup_cb_t cb) {
    ui_comp_popup_dismiss();
    s_cb = cb;

    lv_obj_t *scr = lv_layer_top();
    s_overlay = lv_obj_create(scr);
    lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
    /* Drop the default theme decoration (border/radius/pad/scrollbar) — on a
     * full-screen overlay the 2px border would trace the whole screen edge. */
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_set_style_radius(s_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_overlay, 0, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_50, 0);
    lv_obj_set_layout(s_overlay, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_overlay, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    if (type == UI_COMP_POPUP_TOAST) {
        lv_obj_set_style_bg_opa(s_overlay, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *pill = lv_obj_create(s_overlay);
        lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(pill, lv_color_hex(0x333333), 0);
        lv_obj_set_style_bg_opa(pill, LV_OPA_90, 0);
        lv_obj_set_style_radius(pill, ui_adapt(UI_RADIUS_FULL), 0);
        lv_obj_set_style_pad_hor(pill, ui_adapt(UI_SPACE_LG), 0);
        lv_obj_set_style_pad_ver(pill, ui_adapt(UI_SPACE_SM), 0);
        lv_obj_set_style_border_width(pill, 0, 0);
        lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl = lv_label_create(pill);
        lv_label_set_text(lbl, msg ? msg : "");
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        return;
    }

    lv_obj_t *box = lv_obj_create(s_overlay);
    lv_obj_set_size(box, LV_PCT(80), LV_SIZE_CONTENT);
    lv_obj_add_style(box, &ui_style_card, 0);
    lv_obj_set_layout(box, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(box, ui_adapt(UI_SPACE_MD), 0);

    if (title) {
        lv_obj_t *lbl = ui_comp_label_create_variant(box, title, UI_COMP_LABEL_TITLE);
        lv_obj_set_width(lbl, LV_PCT(100));
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    }
    if (msg) {
        lv_obj_t *lbl = ui_comp_label_create(box, msg);
        lv_obj_set_width(lbl, LV_PCT(100));
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    }

    if (type == UI_COMP_POPUP_LOADING) {
        lv_obj_t *spinner = lv_spinner_create(box, 1000, 60);
        lv_obj_set_size(spinner, ui_adapt(40), ui_adapt(40));
        lv_obj_set_style_align(spinner, LV_ALIGN_CENTER, 0);
    }

    if (type == UI_COMP_POPUP_CONFIRM) {
        lv_obj_t *row = ui_layout_row(box);
        lv_obj_set_width(row, LV_PCT(100));
        ui_layout_align(row, UI_ALIGN_CENTER, UI_ALIGN_CENTER);
        ui_layout_gap(row, UI_SPACE_XL);
        ui_comp_btn_create_styled(row, ui_i18n_text(UI_TEXT_CANCEL),
                                  UI_COMP_BTN_SECONDARY, on_cancel);
        ui_comp_btn_create(row, ui_i18n_text(UI_TEXT_OK), on_confirm);
    }

    if (type == UI_COMP_POPUP_INFO) {
        lv_obj_t *row = ui_layout_row(box);
        lv_obj_set_width(row, LV_PCT(100));
        ui_layout_align(row, UI_ALIGN_CENTER, UI_ALIGN_CENTER);
        ui_comp_btn_create(row, ui_i18n_text(UI_TEXT_OK), on_confirm);
    }

    if (type == UI_COMP_POPUP_INFO_DELETE) {
        lv_obj_t *row = ui_layout_row(box);
        lv_obj_set_width(row, LV_PCT(100));
        ui_layout_align(row, UI_ALIGN_CENTER, UI_ALIGN_CENTER);
        ui_layout_gap(row, UI_SPACE_XL);
        ui_comp_btn_create_styled(row, ui_i18n_text(UI_TEXT_OK),
                                  UI_COMP_BTN_SECONDARY, on_cancel);
        lv_obj_t *del = ui_comp_btn_create(row, ui_i18n_text(UI_TEXT_DELETE), on_confirm);
        lv_obj_set_style_bg_color(del, lv_color_hex(0xE5484D), 0);
        lv_obj_set_style_bg_color(del, lv_color_hex(0xC13B3F), LV_STATE_PRESSED);
    }
}

void ui_comp_popup_toast(const char *msg, uint32_t duration_ms) {
    ui_comp_popup_show(UI_COMP_POPUP_TOAST, NULL, msg, NULL);
    if (duration_ms == 0) duration_ms = 2000;
    s_toast_timer = lv_timer_create(toast_timer_cb, duration_ms, NULL);
    lv_timer_set_repeat_count(s_toast_timer, 1);
}

void ui_comp_popup_dismiss(void) {
    if (s_toast_timer) {
        lv_timer_del(s_toast_timer);
        s_toast_timer = NULL;
    }
    if (s_overlay) {
        lv_obj_del(s_overlay);
        s_overlay = NULL;
    }
    s_cb = NULL;
}
