#include "lvgl.h"
#include "ui_comp_ring_alarm.h"
#include "ui_comp_btn.h"
#include "ui_theme.h"
#include "ui_adaptive.h"
#include "ui_i18n.h"
#include <string.h>

static lv_obj_t *s_overlay = NULL;
static char s_id[40];
static ui_ring_kind_t s_kind;
static ui_ring_stop_cb_t s_on_stop = NULL;

static void stop_cb(lv_event_t *e)
{
    (void)e;
    if (s_on_stop) s_on_stop(s_kind, s_id);
    ui_comp_ring_alarm_dismiss();
}

static void snooze_cb(lv_event_t *e)
{
    (void)e;  /* backend snooze is automatic on the unanswered window; explicit snooze just dismisses the UI */
    ui_comp_ring_alarm_dismiss();
}

void ui_comp_ring_alarm_dismiss(void)
{
    if (s_overlay) {
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_overlay);
        s_overlay = NULL;
    }
}

void ui_comp_ring_alarm_show(ui_ring_kind_t kind, const char *id,
                             const char *message, ui_ring_stop_cb_t on_stop)
{
    ui_comp_ring_alarm_dismiss();   /* single instance */
    s_kind = kind;
    s_on_stop = on_stop;
    s_id[0] = '\0';
    if (id) { strncpy(s_id, id, sizeof(s_id) - 1); s_id[sizeof(s_id) - 1] = '\0'; }

    s_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_60, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *card = lv_obj_create(s_overlay);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, LV_PCT(80), LV_SIZE_CONTENT);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, ui_adapt(16), 0);
    lv_obj_set_style_pad_all(card, ui_adapt(18), 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, ui_adapt(10), 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    int title_key = (kind == UI_RING_KIND_REMINDER) ? UI_TEXT_REMINDER_FIRING
                  : (kind == UI_RING_KIND_COUNTDOWN) ? UI_TEXT_COUNTDOWN_DONE
                  : UI_TEXT_ALARM_RINGING;
    lv_obj_t *ttl = lv_label_create(card);
    lv_label_set_text(ttl, ui_i18n_text(title_key));
    lv_obj_set_style_text_color(ttl, UI_COLOR_TEXT, 0);

    if (message && message[0]) {
        lv_obj_t *msg = lv_label_create(card);
        lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(msg, LV_PCT(100));
        lv_label_set_text(msg, message);
        lv_obj_set_style_text_color(msg, UI_COLOR_TEXT_SEC, 0);
        lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    }

    lv_obj_t *btnrow = lv_obj_create(card);
    lv_obj_remove_style_all(btnrow);
    lv_obj_set_size(btnrow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btnrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnrow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btnrow, LV_OBJ_FLAG_SCROLLABLE);

    if (kind == UI_RING_KIND_ALARM) {
        ui_comp_btn_create_styled(btnrow, ui_i18n_text(UI_TEXT_ALARM_SNOOZE),
                                  UI_COMP_BTN_SECONDARY, snooze_cb);
    }
    int action_key = kind == UI_RING_KIND_COUNTDOWN
                         ? UI_TEXT_CONFIRM
                         : UI_TEXT_TM_STOP;
    lv_obj_t *st = ui_comp_btn_create_styled(
        btnrow, ui_i18n_text(action_key), UI_COMP_BTN_PRIMARY, stop_cb);
    lv_obj_set_style_bg_color(st, UI_COLOR_ERROR, 0);   /* destructive accent */
}
