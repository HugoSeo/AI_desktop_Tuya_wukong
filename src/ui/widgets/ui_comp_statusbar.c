/**
 * @file ui_comp_statusbar.c
 * @brief Global pill-style statusbar singleton on lv_layer_top().
 *
 * Layout:  [ time ]  [ calendar-icon N  clock-icon N ]  [ wifi  battery% ]
 * The container is positioned above page content and survives page transitions
 * because it lives on lv_layer_top().
 */

#include "ui_comp_statusbar.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "ui_adaptive.h"
#include "ui_feature.h"
#include <stdio.h>

/* ---------------------------------------------------------------------------
 * Icon asset declarations (compiled as C arrays in assets/icon/)
 * -------------------------------------------------------------------------*/
LV_IMG_DECLARE(icon_wifi_24_24);   /* 24x24 white wifi icon               */
LV_IMG_DECLARE(icon_battery_icon); /* 19x11 battery outline, used rotated */
LV_IMG_DECLARE(icon_statusbar_calendar); /* 16x16 yellow calendar icon      */
LV_IMG_DECLARE(icon_statusbar_clock);    /* 16x16 yellow clock/alarm icon   */

/* ---------------------------------------------------------------------------
 * Internal state
 * -------------------------------------------------------------------------*/
typedef struct {
    lv_obj_t *lbl_time;        /* "HH:MM"                                  */
    lv_obj_t *grp_center;      /* center group (calendar + alarm)           */
    lv_obj_t *lbl_cal_count;   /* calendar event count, accent colour      */
    lv_obj_t *lbl_alarm_count; /* alarm count, accent colour               */
    lv_obj_t *img_wifi;        /* wifi icon image                          */
    lv_obj_t *bar_battery;     /* lv_bar 0–100 charge level; NULL when the
                                  device has no battery detection          */
} statusbar_widgets_t;

static lv_obj_t           *s_bar     = NULL; /* pill container (singleton) */
static statusbar_widgets_t s_widgets = {0};
static ui_statusbar_mode_t s_mode    = UI_STATUSBAR_MODE_FULL;

/* ---------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------*/

/**
 * Create a plain label with the given text colour.
 * No theme style added — we control every style property locally so the
 * statusbar appearance is self-contained regardless of global theme.
 */
static lv_obj_t *create_label(lv_obj_t *parent, const char *text,
                               lv_color_t color)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, color, 0);
    return lbl;
}

/**
 * Map a battery level 0–100 to the appropriate status colour.
 */
static lv_color_t battery_color(uint8_t level)
{
    if (level <= 10) return UI_COLOR_BATTERY_LOW;
    if (level <= 40) return UI_COLOR_BATTERY_WARN;
    return UI_COLOR_BATTERY_OK;
}

/* ---------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/

void ui_comp_statusbar_init(void)
{
    /* Guard: already created */
    if (s_bar != NULL) return;

    lv_obj_t *layer = lv_layer_top();
    uint16_t  sw    = ui_adapt_screen_w();

    /* ------------------------------------------------------------------ */
    /* Status bar container — full width, semi-transparent background      */
    /* ------------------------------------------------------------------ */
    s_bar = lv_obj_create(layer);
    lv_obj_set_pos(s_bar, 0, 0);
    lv_obj_set_size(s_bar, sw, ui_adapt(32));

    /* Semi-transparent background, no radius */
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_bar, 0, 0);
    lv_obj_set_style_border_width(s_bar, 0, 0);
    lv_obj_set_style_pad_hor(s_bar, ui_adapt(12), 0);
    lv_obj_set_style_pad_ver(s_bar, 0, 0);

    /* Flex row, space-between, items centred on the cross axis */
    lv_obj_set_layout(s_bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_bar,
                          LV_FLEX_ALIGN_SPACE_BETWEEN, /* main  */
                          LV_FLEX_ALIGN_CENTER,        /* cross */
                          LV_FLEX_ALIGN_CENTER);       /* track */

    /* No scrolling; not clickable; start hidden (home page doesn't need it) */
    lv_obj_add_flag(s_bar, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);

    /* ------------------------------------------------------------------ */
    /* LEFT GROUP — time label                                             */
    /* ------------------------------------------------------------------ */
    s_widgets.lbl_time = create_label(s_bar, "00:00",
                                      UI_COLOR_STATUSBAR_ICON);

    /* ------------------------------------------------------------------ */
    /* CENTER GROUP — calendar icon + count + alarm icon + count          */
    /* ------------------------------------------------------------------ */
    s_widgets.grp_center = lv_obj_create(s_bar);
    lv_obj_set_size(s_widgets.grp_center, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_widgets.grp_center, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_widgets.grp_center, 0, 0);
    lv_obj_set_style_pad_all(s_widgets.grp_center, 0, 0);
    lv_obj_clear_flag(s_widgets.grp_center, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_layout(s_widgets.grp_center, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_widgets.grp_center, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_widgets.grp_center,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_widgets.grp_center, ui_adapt(4), 0);

    /* Calendar icon */
    lv_obj_t *img_cal = lv_img_create(s_widgets.grp_center);
    lv_img_set_src(img_cal, &icon_statusbar_calendar);

    /* Calendar count label */
    s_widgets.lbl_cal_count = create_label(s_widgets.grp_center, "0",
                                           UI_COLOR_STATUSBAR_ACCENT);

    /* Small spacer between calendar and alarm groups */
    lv_obj_t *spacer = lv_obj_create(s_widgets.grp_center);
    lv_obj_set_size(spacer, ui_adapt(8), 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_style_pad_all(spacer, 0, 0);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_CLICKABLE);

    /* Clock/alarm icon */
    lv_obj_t *img_alarm = lv_img_create(s_widgets.grp_center);
    lv_img_set_src(img_alarm, &icon_statusbar_clock);

    /* Alarm count label */
    s_widgets.lbl_alarm_count = create_label(s_widgets.grp_center, "0",
                                             UI_COLOR_STATUSBAR_ACCENT);

    /* ------------------------------------------------------------------ */
    /* RIGHT GROUP — wifi icon + battery bar + battery % label            */
    /* ------------------------------------------------------------------ */
    lv_obj_t *right = lv_obj_create(s_bar);
    lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right, 0, 0);
    lv_obj_set_style_pad_all(right, 0, 0);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(right, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_layout(right, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right,
                          LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right, ui_adapt(4), 0);

    /* Wifi icon */
    s_widgets.img_wifi = lv_img_create(right);
    lv_img_set_src(s_widgets.img_wifi, &icon_wifi_24_24);
    lv_obj_set_size(s_widgets.img_wifi, 24, 24);

    /* Battery bar — horizontal 19×11, icon_battery_icon as outline frame.
     * Only created when the device supports battery detection; otherwise the
     * wifi icon is the last flex item and sits flush right. */
    if (ui_feature_available(UI_FEATURE_ID_BATTERY)) {
        s_widgets.bar_battery = lv_bar_create(right);
        lv_obj_set_size(s_widgets.bar_battery, ui_adapt(19), ui_adapt(11));
        lv_bar_set_range(s_widgets.bar_battery, 0, 100);
        lv_bar_set_value(s_widgets.bar_battery, 100, LV_ANIM_OFF);

        lv_obj_set_style_bg_opa(s_widgets.bar_battery, LV_OPA_TRANSP,
                                LV_PART_MAIN);
        lv_obj_set_style_border_width(s_widgets.bar_battery, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(s_widgets.bar_battery, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(s_widgets.bar_battery, ui_adapt(2),
                                 LV_PART_MAIN);
        lv_obj_set_style_bg_img_src(s_widgets.bar_battery, &icon_battery_icon,
                                    LV_PART_MAIN);
        lv_obj_set_style_bg_img_opa(s_widgets.bar_battery, LV_OPA_COVER,
                                    LV_PART_MAIN);

        lv_obj_set_style_bg_color(s_widgets.bar_battery, UI_COLOR_BATTERY_OK,
                                  LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(s_widgets.bar_battery, LV_OPA_COVER,
                                LV_PART_INDICATOR);
        lv_obj_set_style_radius(s_widgets.bar_battery, 1, LV_PART_INDICATOR);
    }
}

void ui_comp_statusbar_refresh(const ui_comp_statusbar_props_t *props)
{
    if (!s_bar || !props) return;
    if (lv_obj_has_flag(s_bar, LV_OBJ_FLAG_HIDDEN)) return;

    /* Time */
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", props->hour, props->minute);
    lv_label_set_text(s_widgets.lbl_time, buf);

    /* Calendar count */
    snprintf(buf, sizeof(buf), "%d", props->calendar_count);
    lv_label_set_text(s_widgets.lbl_cal_count, buf);

    /* Alarm count */
    snprintf(buf, sizeof(buf), "%d", props->alarm_count);
    lv_label_set_text(s_widgets.lbl_alarm_count, buf);

    /* Wifi icon: shown only while online. LVGL flag writes always
     * invalidate and this runs on the per-second SYSTEM tick, so only
     * touch the flag when visibility actually changes. */
    bool wifi_hidden = lv_obj_has_flag(s_widgets.img_wifi, LV_OBJ_FLAG_HIDDEN);
    if (props->wifi_connected && wifi_hidden) {
        lv_obj_clear_flag(s_widgets.img_wifi, LV_OBJ_FLAG_HIDDEN);
    } else if (!props->wifi_connected && !wifi_hidden) {
        lv_obj_add_flag(s_widgets.img_wifi, LV_OBJ_FLAG_HIDDEN);
    }

    /* Battery bar value and colour (absent without battery detection) */
    if (s_widgets.bar_battery) {
        lv_bar_set_value(s_widgets.bar_battery, props->battery_level,
                         LV_ANIM_OFF);
        lv_color_t bat_col = battery_color(props->battery_level);
        lv_obj_set_style_bg_color(s_widgets.bar_battery, bat_col,
                                  LV_PART_INDICATOR);
    }
}

void ui_comp_statusbar_set_visible(bool visible)
{
    if (!s_bar) return;
    if (visible) {
        lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
        const ui_state_system_t *sys = ui_state_get_system();
        ui_comp_statusbar_props_t props = {
            .hour = (uint8_t)(sys->timestamp / 3600 % 24),
            .minute = (uint8_t)(sys->timestamp / 60 % 60),
            .calendar_count = sys->calendar_count,
            .alarm_count = sys->alarm_count,
            .wifi_strength = sys->wifi_strength,
            .wifi_connected = sys->is_online,
            .battery_level = sys->battery_level,
            .is_charging = sys->is_charging,
        };
        ui_comp_statusbar_refresh(&props);
    } else {
        lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_comp_statusbar_raise(void)
{
    if (!s_bar) return;
    /* The statusbar is created once at init, so any overlay created later on
     * lv_layer_top() (e.g. the pulldown panel) renders above it and hides it.
     * Move it back to the foreground so it stays visible over such overlays. */
    lv_obj_move_foreground(s_bar);
}

void ui_comp_statusbar_set_mode(ui_statusbar_mode_t mode)
{
    if (!s_bar) return;
    s_mode = mode;
    if (mode == UI_STATUSBAR_MODE_MINIMAL) {
        lv_obj_add_flag(s_widgets.lbl_time, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_widgets.grp_center, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_flex_align(s_bar,
                              LV_FLEX_ALIGN_END,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
    } else {
        lv_obj_clear_flag(s_widgets.lbl_time, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_widgets.grp_center, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_flex_align(s_bar,
                              LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
    }
}
