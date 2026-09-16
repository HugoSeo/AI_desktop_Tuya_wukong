#include "lvgl.h"
#include "ui_route.h"
#include "ui_theme.h"
#include "ui_adaptive.h"
#include "ui_i18n.h"
#include "ui_state.h"
#include "ui_comp_btn.h"
#include "ui_svc_devinfo.h"
#include "ui_svc_ota.h"
#include "ui_page_ids.h"

#include <stdio.h>

/* Title-bar back icon (24x24) */
LV_IMG_DECLARE(icon_back_24_24);

/* ---------------------------------------------------------------------------
 * Style constants
 * -------------------------------------------------------------------------*/
#define ABOUT_STATUSBAR_H   32   /* global statusbar height (ui_comp_statusbar) */
#define ABOUT_TITLEBAR_H    48
#define ABOUT_ROW_MIN_H     48

/* ---------------------------------------------------------------------------
 * Internal state
 * -------------------------------------------------------------------------*/
static lv_obj_t *s_screen  = NULL;
static lv_obj_t *s_content = NULL;
static lv_obj_t *s_title   = NULL;            /* page title (i18n) */
static lv_obj_t *s_ota_label = NULL;
static lv_obj_t *s_ota_value = NULL;

/* Row label keys, kept so on_enter can re-resolve them on a language change.
 * Index order must match the info_row_create() calls in on_create. */
static const ui_i18n_key_t s_row_keys[] = {
    UI_TEXT_ABOUT_FW_VERSION,
    UI_TEXT_ABOUT_SDK,
    UI_TEXT_ABOUT_DEVICE_ID,
    UI_TEXT_ABOUT_UUID,
};
#define ABOUT_ROW_COUNT (sizeof(s_row_keys) / sizeof(s_row_keys[0]))
static lv_obj_t *s_row_labels[ABOUT_ROW_COUNT] = {NULL};

/* ---------------------------------------------------------------------------
 * Forward declarations
 * -------------------------------------------------------------------------*/
static void on_create(void *parent);
static void on_enter(uint32_t dirty);
static void on_leave(void);
static void on_destroy(void);
static void back_click_cb(lv_event_t *e);
static void gesture_cb(lv_event_t *e);
static void ota_click_cb(lv_event_t *e);
static void ota_state_cb(bool upgrading);
static void refresh_ota_row(void);

/* ---------------------------------------------------------------------------
 * Info row helper (page-local — about-only, not a shared widget)
 * -------------------------------------------------------------------------*/

/* One read-only row: label on the left, value (secondary color) on the right.
 * The value wraps to multiple lines for long strings (SDK info, device id). */
static void info_row_create(lv_obj_t *parent, ui_i18n_key_t label_key, const char *value,
                            lv_obj_t **out_label)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(row, ui_adapt(ABOUT_ROW_MIN_H), 0);
    lv_obj_set_style_bg_color(row, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, ui_adapt(UI_RADIUS_MD), 0);
    lv_obj_set_style_pad_hor(row, ui_adapt(UI_SPACE_MD), 0);
    lv_obj_set_style_pad_ver(row, ui_adapt(UI_SPACE_SM), 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row,
                          LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, ui_adapt(UI_SPACE_MD), 0);

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, ui_i18n_text(label_key));
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT, 0);
    if (out_label) {
        *out_label = label;
    }

    lv_obj_t *val = lv_label_create(row);
    lv_label_set_text(val, value ? value : "--");
    lv_obj_set_style_text_color(val, UI_COLOR_TEXT_SEC, 0);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, 0);
    /* Take the remaining width and wrap long values instead of overflowing. */
    lv_obj_set_flex_grow(val, 1);
    lv_label_set_long_mode(val, LV_LABEL_LONG_WRAP);
}

/* Navigation row kept page-local: About owns the firmware context and the
 * dedicated OTA page owns the interactive update flow. */
static void ota_row_create(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, ui_adapt(ABOUT_ROW_MIN_H));
    lv_obj_set_style_bg_color(row, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_set_style_radius(row, ui_adapt(UI_RADIUS_MD), 0);
    lv_obj_set_style_pad_hor(row, ui_adapt(UI_SPACE_MD), 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, ui_adapt(UI_SPACE_SM), 0);

    s_ota_label = lv_label_create(row);
    lv_label_set_text(s_ota_label, ui_i18n_text(UI_TEXT_OTA_ENTRY));
    lv_obj_set_style_text_color(s_ota_label, UI_COLOR_TEXT, 0);
    lv_obj_set_flex_grow(s_ota_label, 1);

    s_ota_value = lv_label_create(row);
    lv_obj_set_width(s_ota_value, LV_PCT(48));
    lv_label_set_long_mode(s_ota_value, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_ota_value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(s_ota_value, UI_COLOR_TEXT_SEC, 0);

    lv_obj_t *chevron = lv_label_create(row);
    lv_label_set_text(chevron, ">");
    lv_obj_set_style_text_color(chevron, UI_COLOR_TEXT_SEC, 0);

    lv_obj_add_event_cb(row, ota_click_cb, LV_EVENT_CLICKED, NULL);
}

static void refresh_ota_row(void)
{
    ui_i18n_key_t value_key = UI_TEXT_OTA_CHECK;
    lv_color_t value_color = UI_COLOR_TEXT_SEC;

    if (!s_ota_label || !s_ota_value) {
        return;
    }

    lv_label_set_text(s_ota_label, ui_i18n_text(UI_TEXT_OTA_ENTRY));
    ui_svc_ota_state_t state = ui_svc_ota_get_state();
    int percent = ui_svc_ota_get_percent();
    char text[48];

    switch (state) {
        case UI_SVC_OTA_PREPARING:
            value_key = UI_TEXT_OTA_PREPARING;
            value_color = UI_COLOR_PRIMARY;
            break;
        case UI_SVC_OTA_DOWNLOADING:
            if (percent >= 0) {
                snprintf(text, sizeof(text), ui_i18n_text(UI_TEXT_OTA_PROGRESS_FMT), percent);
                lv_label_set_text(s_ota_value, text);
                lv_obj_set_style_text_color(s_ota_value, UI_COLOR_PRIMARY, 0);
                return;
            } else {
                value_key = UI_TEXT_OTA_DOWNLOADING;
                value_color = UI_COLOR_PRIMARY;
            }
            break;
        case UI_SVC_OTA_VERIFYING:
            value_key = UI_TEXT_OTA_VERIFYING;
            value_color = UI_COLOR_PRIMARY;
            break;
        case UI_SVC_OTA_INSTALLING:
            value_key = UI_TEXT_OTA_INSTALLING;
            value_color = UI_COLOR_PRIMARY;
            break;
        case UI_SVC_OTA_FAILED:
            value_key = UI_TEXT_OTA_FAILED;
            value_color = UI_COLOR_ERROR;
            break;
        case UI_SVC_OTA_IDLE:
        default:
            if (ui_svc_ota_get_confirm_state() == UI_SVC_OTA_CONFIRMING ||
                ui_svc_ota_get_confirm_state() == UI_SVC_OTA_CONFIRM_ACCEPTED) {
                value_key = UI_TEXT_OTA_PREPARING;
                value_color = UI_COLOR_PRIMARY;
                break;
            }
            switch (ui_svc_ota_get_check_state()) {
                case UI_SVC_OTA_CHECKING:
                    value_key = UI_TEXT_OTA_CHECKING;
                    value_color = UI_COLOR_PRIMARY;
                    break;
                case UI_SVC_OTA_CHECK_AVAILABLE:
                    value_key = UI_TEXT_OTA_AVAILABLE;
                    value_color = UI_COLOR_PRIMARY;
                    break;
                case UI_SVC_OTA_CHECK_UP_TO_DATE:
                    value_key = UI_TEXT_OTA_UP_TO_DATE;
                    value_color = UI_COLOR_SUCCESS;
                    break;
                case UI_SVC_OTA_CHECK_FAILED:
                    value_key = UI_TEXT_OTA_CHECK_FAILED;
                    value_color = UI_COLOR_ERROR;
                    break;
                case UI_SVC_OTA_CHECK_IDLE:
                default:
                    break;
            }
            break;
    }

    lv_label_set_text(s_ota_value, ui_i18n_text(value_key));
    lv_obj_set_style_text_color(s_ota_value, value_color, 0);
}

/* ---------------------------------------------------------------------------
 * Page lifecycle
 * -------------------------------------------------------------------------*/
static void on_create(void *parent)
{
    (void)parent;

    /* Full-screen root, column layout: title bar on top, content below */
    s_screen = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_screen, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_screen, gesture_cb, LV_EVENT_GESTURE, NULL);   /* swipe-right = back */
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_screen,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    /* Clear the global statusbar (lv_layer_top pill, height 32) so the title
     * bar sits below it — same offset chat/home/settings use. */
    lv_obj_set_style_pad_top(s_screen, ui_adapt(ABOUT_STATUSBAR_H), 0);

    /* Title bar: icon-only back (left), page title centered */
    lv_obj_t *titlebar = lv_obj_create(s_screen);
    lv_obj_remove_style_all(titlebar);
    lv_obj_set_size(titlebar, LV_PCT(100), ui_adapt(ABOUT_TITLEBAR_H));
    lv_obj_set_style_bg_opa(titlebar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(titlebar, LV_OBJ_FLAG_SCROLLABLE);

    /* Icon-only back button, left-aligned -> returns to the settings page. */
    lv_obj_t *back = ui_comp_btn_icon_create(titlebar, &icon_back_24_24, back_click_cb);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, ui_adapt(8), 0);

    /* Centered page title */
    s_title = lv_label_create(titlebar);
    lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_SETTINGS_ABOUT));
    lv_obj_set_style_text_color(s_title, UI_COLOR_TEXT, 0);
    lv_obj_align(s_title, LV_ALIGN_CENTER, 0, 0);

    /* Content area — vertical list of read-only info rows. */
    s_content = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_content);
    lv_obj_set_width(s_content, LV_PCT(100));
    lv_obj_set_flex_grow(s_content, 1);
    lv_obj_set_flex_flow(s_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_content, ui_adapt(UI_SPACE_MD), 0);
    lv_obj_set_style_pad_row(s_content, ui_adapt(UI_SPACE_SM), 0);

    info_row_create(s_content, UI_TEXT_ABOUT_FW_VERSION, ui_svc_devinfo_fw_version(), &s_row_labels[0]);
    ota_row_create(s_content);
    info_row_create(s_content, UI_TEXT_ABOUT_SDK,        ui_svc_devinfo_sdk_info(),   &s_row_labels[1]);
    info_row_create(s_content, UI_TEXT_ABOUT_DEVICE_ID,  ui_svc_devinfo_device_id(),  &s_row_labels[2]);
    info_row_create(s_content, UI_TEXT_ABOUT_UUID,       ui_svc_devinfo_uuid(),       &s_row_labels[3]);
}

static void on_enter(uint32_t dirty)
{
    if (ui_dirty_is_enter(dirty)) {
        ui_svc_ota_set_cb(ota_state_cb);
        refresh_ota_row();
    }

    /* Language change (RULES §5): re-resolve the title + row labels. The values
     * (fw/sdk/device id) are not translated, so they're left untouched. */
    if (dirty & (1u << UI_STATE_GROUP_SETTINGS)) {
        if (s_title) {
            lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_SETTINGS_ABOUT));
        }
        for (uint8_t i = 0; i < ABOUT_ROW_COUNT; i++) {
            if (s_row_labels[i]) {
                lv_label_set_text(s_row_labels[i], ui_i18n_text(s_row_keys[i]));
            }
        }
        refresh_ota_row();
    }
}

static void on_leave(void)
{
    ui_svc_ota_set_cb(NULL);
}

static void on_destroy(void)
{
    ui_svc_ota_set_cb(NULL);
    if (s_screen) {
        /* on_destroy runs inside the event-callback chain (back -> ui_route_pop);
         * hide immediately then defer deletion to avoid freeing the object while
         * LVGL is still processing events on it. */
        lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_screen);
        s_screen  = NULL;
        s_content = NULL;
        s_title   = NULL;
        s_ota_label = NULL;
        s_ota_value = NULL;
        for (uint8_t i = 0; i < ABOUT_ROW_COUNT; i++) {
            s_row_labels[i] = NULL;
        }
    }
}

static void ota_click_cb(lv_event_t *e)
{
    (void)e;
    ui_route_push(UI_PAGE_OTA);
}

static void ota_state_cb(bool upgrading)
{
    (void)upgrading;
    refresh_ota_row();
}

/* ---------------------------------------------------------------------------
 * Callbacks
 * -------------------------------------------------------------------------*/
static void back_click_cb(lv_event_t *e)
{
    (void)e;
    ui_route_pop();
}

/* Swipe right anywhere on the page = go back (same as the back button). */
static void gesture_cb(lv_event_t *e)
{
    (void)e;
    if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());   /* swallow rest of touch so the release doesn't click the page below */
        ui_route_pop();
    }
}

/* ---------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/
const ui_page_entry_t ui_page_about_entry = {
    .id = UI_PAGE_ABOUT,
    .name = "about",
    .lifecycle = { on_create, on_enter, on_leave, on_destroy }
};
