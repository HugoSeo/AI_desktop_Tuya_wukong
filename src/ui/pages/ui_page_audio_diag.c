#include "lvgl.h"
#include "ui_route.h"
#include "ui_theme.h"
#include "ui_adaptive.h"
#include "ui_i18n.h"
#include "ui_state.h"
#include "ui_comp_btn.h"
#include "ui_svc_audio_diag.h"
#include "ui_page_ids.h"

LV_IMG_DECLARE(icon_back_24_24);

#define AD_STATUSBAR_H  32
#define AD_TITLEBAR_H   48
#define AD_ROW_H        56

/* 行项：i18n key + 对应通道值。索引即通道值（OFF=0..SD=3）。 */
typedef struct {
    ui_i18n_key_t key;
    uint8_t       channel;   /* AUDIO_DUMP_CHANNEL_E */
} ad_item_t;

static const ad_item_t s_items[] = {
    { UI_TEXT_AUDIO_DIAG_OFF,  0 },
    { UI_TEXT_AUDIO_DIAG_UART, 1 },
    { UI_TEXT_AUDIO_DIAG_LAN,  2 },
    { UI_TEXT_AUDIO_DIAG_SD,   3 },
};
#define AD_COUNT (sizeof(s_items) / sizeof(s_items[0]))

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_title  = NULL;
static lv_obj_t *s_rows[AD_COUNT]       = {NULL};
static lv_obj_t *s_row_labels[AD_COUNT] = {NULL};

/* Pending selection — tapping a row only updates this + the highlight; the heavy
 * backend apply (audio_dump_set_channel: PSRAM alloc for SD, flush-on-teardown)
 * runs once in on_leave. Avoids alloc/free + near-empty .pcm churn from
 * intermediate taps, and matches the settings/mode pages' apply-on-leave pattern. */
static uint8_t s_pending_ch = 0;

static void on_create(void *parent);
static void on_enter(uint32_t dirty);
static void on_leave(void);
static void on_destroy(void);
static void back_click_cb(lv_event_t *e);
static void gesture_cb(lv_event_t *e);
static void row_cb(lv_event_t *e);
static void refresh_selection(void);

static void row_set_selected(uint8_t idx, bool selected)
{
    lv_obj_t *row = s_rows[idx], *label = s_row_labels[idx];
    if (!row || !label) {
        return;
    }
    if (selected) {
        lv_obj_set_style_bg_color(row, UI_COLOR_PRIMARY, 0);
        lv_obj_set_style_text_color(label, UI_COLOR_TEXT, 0);
    } else {
        lv_obj_set_style_bg_color(row, UI_COLOR_BG_CARD, 0);
        lv_obj_set_style_text_color(label, UI_COLOR_TEXT_SEC, 0);
    }
}

static lv_obj_t *create_row(lv_obj_t *parent, uint8_t idx, bool enabled)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, ui_adapt(AD_ROW_H));
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, ui_adapt(UI_RADIUS_MD), 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    s_rows[idx] = row;

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, ui_i18n_text(s_items[idx].key));
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    s_row_labels[idx] = label;

    if (enabled) {
        lv_obj_set_style_bg_opa(row, LV_OPA_80, LV_STATE_PRESSED);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, row_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)idx);
    } else {
        /* 不可用通道：灰显、不可点 */
        lv_obj_set_style_text_color(label, UI_COLOR_TEXT_SEC, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_50, 0);
    }
    return row;
}

static void on_create(void *parent)
{
    (void)parent;
    s_screen = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_screen, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_screen, gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(s_screen, ui_adapt(AD_STATUSBAR_H), 0);

    lv_obj_t *titlebar = lv_obj_create(s_screen);
    lv_obj_remove_style_all(titlebar);
    lv_obj_set_size(titlebar, LV_PCT(100), ui_adapt(AD_TITLEBAR_H));
    lv_obj_set_style_bg_opa(titlebar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(titlebar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = ui_comp_btn_icon_create(titlebar, &icon_back_24_24, back_click_cb);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, ui_adapt(8), 0);

    s_title = lv_label_create(titlebar);
    lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_DIAG_AUDIO_DIAG));
    lv_obj_set_style_text_color(s_title, UI_COLOR_TEXT, 0);
    lv_obj_align(s_title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *content = lv_obj_create(s_screen);
    lv_obj_remove_style_all(content);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(content, ui_adapt(UI_SPACE_MD), 0);
    lv_obj_set_style_pad_row(content, ui_adapt(UI_SPACE_SM), 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    s_pending_ch = ui_svc_audio_diag_channel_get();   /* start from the live channel */

    uint32_t caps = ui_svc_audio_diag_caps();
    for (uint8_t i = 0; i < AD_COUNT; i++) {
        bool enabled = (caps & (1u << s_items[i].channel)) != 0;
        create_row(content, i, enabled);
    }
    refresh_selection();
}

static void on_enter(uint32_t dirty)
{
    /* Language change (RULES §5): re-resolve the title + channel row labels. */
    if (dirty & (1u << UI_STATE_GROUP_SETTINGS)) {
        if (s_title) {
            lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_DIAG_AUDIO_DIAG));
        }
        for (uint8_t i = 0; i < AD_COUNT; i++) {
            if (s_row_labels[i]) {
                lv_label_set_text(s_row_labels[i], ui_i18n_text(s_items[i].key));
            }
        }
    }
}

/* Commit the pending channel once, on page exit — only if it actually changed. */
static void on_leave(void)
{
    if (s_pending_ch != ui_svc_audio_diag_channel_get()) {
        ui_svc_audio_diag_channel_set(s_pending_ch);
    }
}

static void on_destroy(void)
{
    if (s_screen) {
        lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_screen);
        s_screen = NULL;
        s_title  = NULL;
        for (uint8_t i = 0; i < AD_COUNT; i++) {
            s_rows[i]       = NULL;
            s_row_labels[i] = NULL;
        }
    }
}

static void refresh_selection(void)
{
    uint8_t  cur  = s_pending_ch;   /* highlight reflects the pending choice, not the live channel */
    uint32_t caps = ui_svc_audio_diag_caps();
    for (uint8_t i = 0; i < AD_COUNT; i++) {
        if ((caps & (1u << s_items[i].channel)) == 0) {
            continue;   /* 不可用通道保持灰显，不参与选中高亮 */
        }
        row_set_selected(i, s_items[i].channel == cur);
    }
}

static void back_click_cb(lv_event_t *e) { (void)e; ui_route_pop(); }

static void gesture_cb(lv_event_t *e)
{
    (void)e;
    if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());
        ui_route_pop();
    }
}

static void row_cb(lv_event_t *e)
{
    uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    uint8_t ch  = s_items[idx].channel;
    if (ch == s_pending_ch) {
        return;
    }
    s_pending_ch = ch;        /* defer the apply to on_leave */
    refresh_selection();      /* 留在本页，仅刷新高亮 */
}

const ui_page_entry_t ui_page_audio_diag_entry = {
    .id = UI_PAGE_AUDIO_DIAG,
    .name = "audio_diag",
    .lifecycle = { on_create, on_enter, on_leave, on_destroy }
};
