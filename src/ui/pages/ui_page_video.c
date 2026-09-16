/**
 * @file ui_page_video.c
 * @brief Local AVI library, multi-select deletion and playback page.
 *
 * File enumeration, deletion, AVI control and JPEG decode stay behind
 * ui_svc_video_playback. This page only renders state and RGB565 frames handed
 * to it on the UI thread.
 */

#include "lvgl.h"
#include "ui_route.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "ui_adaptive.h"
#include "ui_i18n.h"
#include "ui_comp_btn.h"
#include "ui_comp_picture.h"
#include "ui_comp_popup.h"
#include "ui_comp_statusbar.h"
#include "ui_svc_video_playback.h"
#include "ui_page_ids.h"

#include "tal_memory.h"
#include "tal_time_service.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

LV_IMG_DECLARE(icon_back_24_24);
LV_IMG_DECLARE(icon_choose);
LV_IMG_DECLARE(icon_delete);

#define VIDEO_STATUSBAR_H       32
#define VIDEO_TITLEBAR_H        48
#define VIDEO_ROW_H             72
#define VIDEO_FRAME_SIDE       320
#define VIDEO_PLAYER_HEADER_H   48
#define VIDEO_PLAYER_CTRL_H    112
#define VIDEO_BADGE_SIDE        46
#define VIDEO_CHECK_SIDE        22
#define VIDEO_CHECK_DOT          8
#define VIDEO_BOTTOM_BAR_H      64
#define VIDEO_LIST_PAD          10
#define VIDEO_NAME_W           174
#define VIDEO_TITLE_W          128
#define VIDEO_PLAYER_TITLE_W   210
#define VIDEO_RECORD_RED  0xFF3B30
#define VIDEO_INVALID_SEC 0xFFFFFFFFu
#define VIDEO_VALID_EPOCH_MS 946684800000ULL /* 2000-01-01 */

typedef enum {
    VIDEO_PAGE_LIST = 0,
    VIDEO_PAGE_PLAYER,
} video_page_mode_t;

typedef enum {
    VIDEO_LIST_LOADING = 0,
    VIDEO_LIST_READY,
    VIDEO_LIST_EMPTY,
    VIDEO_LIST_ERROR,
} video_list_state_t;

typedef struct {
    ui_video_playback_item_t media;
    lv_obj_t *row;
    lv_obj_t *title;
    lv_obj_t *meta;
    lv_obj_t *arrow;
    lv_obj_t *check;
    lv_obj_t *check_dot;
    bool selected;
} video_page_item_t;

static lv_obj_t *s_screen;
static lv_obj_t *s_titlebar;
static lv_obj_t *s_title;
static lv_obj_t *s_back_btn;
static lv_obj_t *s_choose_btn;
static lv_obj_t *s_cancel_btn;
static lv_obj_t *s_select_all_btn;
static lv_obj_t *s_list_cont;
static lv_obj_t *s_list;
static lv_obj_t *s_bottom_bar;
static lv_obj_t *s_delete_btn;
static lv_obj_t *s_player_cont;
static lv_obj_t *s_frame_box;
static lv_obj_t *s_picture;
static lv_obj_t *s_frame_hint;
static lv_obj_t *s_player_title;
static lv_obj_t *s_progress;
static lv_obj_t *s_pos_label;
static lv_obj_t *s_dur_label;
static lv_obj_t *s_play_btn;
static lv_obj_t *s_stop_btn;

static video_page_mode_t s_mode;
static video_list_state_t s_list_state;
static ui_video_playback_state_t s_play_state;
static video_page_item_t *s_items;
static uint16_t s_item_count;
static uint16_t s_select_count;
static uint32_t s_list_seq;
static uint32_t s_delete_seq;
static uint32_t s_last_pos_sec;
static uint32_t s_last_dur_sec;
static ui_i18n_key_t s_frame_hint_key;
static bool s_select_mode;
static bool s_delete_busy;
static bool s_frame_hint_visible;
static char s_selected[UI_VIDEO_PLAYBACK_NAME_MAX];

static void request_list(void);
static void set_mode(video_page_mode_t mode);
static void set_select_mode(bool enabled);
static void refresh_titlebar(void);
static void refresh_controls(void);
static void refresh_progress(void);

static bool playback_file_released(void)
{
    return s_play_state == UI_VIDEO_PLAYBACK_IDLE;
}

static void human_size(uint64_t bytes, char *buf, size_t len)
{
    if (bytes < 1024ULL) {
        snprintf(buf, len, ui_i18n_text(UI_TEXT_VIDEO_SIZE_B_FMT),
                 (unsigned)bytes);
    } else if (bytes < 1024ULL * 1024ULL) {
        snprintf(buf, len, ui_i18n_text(UI_TEXT_VIDEO_SIZE_KB_FMT),
                 (unsigned)(bytes / 1024ULL),
                 (unsigned)((bytes % 1024ULL) * 10ULL / 1024ULL));
    } else {
        snprintf(buf, len, ui_i18n_text(UI_TEXT_VIDEO_SIZE_MB_FMT),
                 (unsigned)(bytes / (1024ULL * 1024ULL)),
                 (unsigned)((bytes % (1024ULL * 1024ULL)) * 10ULL /
                            (1024ULL * 1024ULL)));
    }
}

static bool parse_record_timestamp_ms(const char *name, uint64_t *timestamp_ms)
{
    static const char prefix[] = "wukong_video_";
    const char *p;
    const char *end;
    uint64_t value = 0;
    size_t len;

    if (!name || !timestamp_ms) return false;
    len = strlen(name);
    if (len <= sizeof(prefix) - 1 + 4 ||
        strncmp(name, prefix, sizeof(prefix) - 1) != 0) {
        return false;
    }

    p = name + sizeof(prefix) - 1;
    end = name + len - 4;
    if (p >= end) return false;
    while (p < end) {
        uint8_t digit;
        if (*p < '0' || *p > '9') return false;
        digit = (uint8_t)(*p - '0');
        if (value > (UINT64_MAX - digit) / 10ULL) return false;
        value = value * 10ULL + digit;
        p++;
    }
    if (value < VIDEO_VALID_EPOCH_MS) return false;
    *timestamp_ms = value;
    return true;
}

static void format_video_title(const char *name, char *buf, size_t len)
{
    uint64_t timestamp_ms;
    POSIX_TM_S tm;

    if (parse_record_timestamp_ms(name, &timestamp_ms) &&
        tal_time_get_local_time_custom((TIME_T)(timestamp_ms / 1000ULL),
                                       &tm) == OPRT_OK) {
        snprintf(buf, len, ui_i18n_text(UI_TEXT_VIDEO_DATE_FMT),
                 tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
        return;
    }
    snprintf(buf, len, "%s", name ? name : "");
}

static void format_video_meta(uint64_t bytes, char *buf, size_t len)
{
    char size[24];
    human_size(bytes, size, sizeof(size));
    snprintf(buf, len, ui_i18n_text(UI_TEXT_VIDEO_META_FMT), size);
}

static lv_obj_t *create_video_badge(lv_obj_t *parent, lv_coord_t side)
{
    lv_obj_t *badge = lv_obj_create(parent);
    lv_obj_remove_style_all(badge);
    lv_obj_set_size(badge, side, side);
    lv_obj_set_style_radius(badge, ui_adapt(UI_RADIUS_MD), 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(0x161616), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(badge, lv_color_hex(0x454545), 0);
    lv_obj_set_style_border_width(badge, 1, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *label = lv_label_create(badge);
    lv_label_set_text(label, ui_i18n_text(UI_TEXT_VIDEO_FORMAT_AVI));
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT, 0);
    lv_obj_center(label);

    lv_obj_t *dot = lv_obj_create(badge);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, ui_adapt(7), ui_adapt(7));
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(VIDEO_RECORD_RED), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_align(dot, LV_ALIGN_TOP_RIGHT, -ui_adapt(5), ui_adapt(5));
    return badge;
}

static void set_frame_hint(ui_i18n_key_t key, bool visible)
{
    if (!s_frame_hint) return;
    s_frame_hint_key = key;
    s_frame_hint_visible = visible;
    if (visible) {
        lv_label_set_text(s_frame_hint, ui_i18n_text(key));
        lv_obj_clear_flag(s_frame_hint, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_frame_hint, LV_OBJ_FLAG_HIDDEN);
    }
}

static void clear_list(void)
{
    if (s_list) lv_obj_clean(s_list);
    tal_free(s_items);
    s_items = NULL;
    s_item_count = 0;
    s_select_count = 0;
    s_select_mode = false;
}

static void retry_cb(lv_event_t *e)
{
    (void)e;
    if (!s_delete_busy) request_list();
}

static void render_list_state(video_list_state_t state)
{
    lv_obj_t *panel;
    lv_obj_t *label;

    if (!s_list) return;
    lv_obj_clean(s_list);
    s_list_state = state;

    panel = lv_obj_create(s_list);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, LV_PCT(100), ui_adapt(280));
    lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(panel, ui_adapt(UI_SPACE_MD), 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    create_video_badge(panel, ui_adapt(62));
    label = lv_label_create(panel);
    if (state == VIDEO_LIST_LOADING) {
        lv_label_set_text(label, ui_i18n_text(UI_TEXT_LOADING));
    } else if (state == VIDEO_LIST_ERROR) {
        lv_label_set_text(label, ui_i18n_text(UI_TEXT_VIDEO_LIST_FAILED));
    } else {
        lv_label_set_text(label, ui_i18n_text(UI_TEXT_VIDEO_EMPTY));
    }
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT_SEC, 0);

    if (state == VIDEO_LIST_ERROR) {
        lv_obj_t *retry = ui_comp_btn_create_styled(
            panel, ui_i18n_text(UI_TEXT_REFRESH), UI_COMP_BTN_SECONDARY, retry_cb);
        lv_obj_set_size(retry, ui_adapt(108), ui_adapt(38));
    }
}

static void refresh_row_text(video_page_item_t *item)
{
    char title[UI_VIDEO_PLAYBACK_NAME_MAX];
    char meta[48];

    if (!item || !item->title || !item->meta) return;
    format_video_title(item->media.name, title, sizeof(title));
    format_video_meta(item->media.size, meta, sizeof(meta));
    lv_label_set_text(item->title, title);
    lv_label_set_text(item->meta, meta);
}

static void update_row_selection(video_page_item_t *item)
{
    if (!item || !item->row) return;

    if (s_select_mode) {
        lv_obj_add_flag(item->arrow, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(item->check, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(item->arrow, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(item->check, LV_OBJ_FLAG_HIDDEN);
    }

    if (item->selected) {
        lv_obj_set_style_bg_color(item->check, UI_COLOR_PRIMARY, 0);
        lv_obj_set_style_bg_opa(item->check, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(item->check, UI_COLOR_PRIMARY, 0);
        lv_obj_clear_flag(item->check_dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_border_color(item->row, UI_COLOR_PRIMARY, 0);
        lv_obj_set_style_border_width(item->row, 2, 0);
    } else {
        lv_obj_set_style_bg_color(item->check, lv_color_hex(0x707070), 0);
        lv_obj_set_style_bg_opa(item->check, LV_OPA_20, 0);
        lv_obj_set_style_border_color(item->check, UI_COLOR_TEXT_SEC, 0);
        lv_obj_add_flag(item->check_dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_border_width(item->row, 0, 0);
    }
}

static void refresh_selection_ui(void)
{
    char text[64];

    if (!s_select_mode) {
        refresh_titlebar();
        return;
    }

    snprintf(text, sizeof(text), ui_i18n_text(UI_TEXT_VIDEO_SELECTED_FMT),
             (unsigned)s_select_count);
    lv_label_set_text(s_title, text);
    ui_comp_btn_set_text(
        s_select_all_btn,
        ui_i18n_text(s_select_count == s_item_count ?
                     UI_TEXT_VIDEO_DESELECT_ALL : UI_TEXT_VIDEO_SELECT_ALL));

    if (s_select_count > 0) {
        snprintf(text, sizeof(text),
                 ui_i18n_text(UI_TEXT_VIDEO_DELETE_COUNT_FMT),
                 (unsigned)s_select_count);
    } else {
        snprintf(text, sizeof(text), "%s", ui_i18n_text(UI_TEXT_DELETE));
    }
    ui_comp_btn_set_text(s_delete_btn, text);
    ui_comp_btn_set_enabled(s_delete_btn, s_select_count > 0 && !s_delete_busy);
    ui_comp_btn_set_enabled(s_select_all_btn, s_item_count > 0 && !s_delete_busy);
    ui_comp_btn_set_enabled(s_cancel_btn, !s_delete_busy);
}

static void row_cb(lv_event_t *e)
{
    uint16_t index = (uint16_t)(uintptr_t)lv_event_get_user_data(e);
    video_page_item_t *item;

    if (!s_items || index >= s_item_count || s_delete_busy) return;
    item = &s_items[index];

    if (s_select_mode) {
        item->selected = !item->selected;
        if (item->selected) {
            s_select_count++;
        } else if (s_select_count > 0) {
            s_select_count--;
        }
        update_row_selection(item);
        refresh_selection_ui();
        return;
    }

    if (!playback_file_released()) return;
    strncpy(s_selected, item->media.name, sizeof(s_selected) - 1);
    s_selected[sizeof(s_selected) - 1] = '\0';
    set_mode(VIDEO_PAGE_PLAYER);
    set_frame_hint(UI_TEXT_VIDEO_OPENING, true);
    ui_comp_picture_clear(s_picture);
    s_last_pos_sec = VIDEO_INVALID_SEC;
    s_last_dur_sec = VIDEO_INVALID_SEC;
    ui_svc_video_playback_play(s_selected);
}

static void make_row(uint16_t index)
{
    video_page_item_t *item = &s_items[index];
    lv_obj_t *badge;

    item->row = lv_obj_create(s_list);
    lv_obj_remove_style_all(item->row);
    lv_obj_set_size(item->row, LV_PCT(100), ui_adapt(VIDEO_ROW_H));
    lv_obj_set_style_radius(item->row, ui_adapt(UI_RADIUS_MD), 0);
    lv_obj_set_style_bg_color(item->row, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_bg_color(item->row, lv_color_hex(0x353535), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(item->row, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(item->row, ui_adapt(UI_SPACE_MD), 0);
    lv_obj_clear_flag(item->row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(item->row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(item->row, row_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)index);

    badge = create_video_badge(item->row, ui_adapt(VIDEO_BADGE_SIDE));
    lv_obj_align(badge, LV_ALIGN_LEFT_MID, 0, 0);

    item->title = lv_label_create(item->row);
    lv_label_set_long_mode(item->title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(item->title, ui_adapt(VIDEO_NAME_W));
    lv_obj_set_style_text_color(item->title, UI_COLOR_TEXT, 0);
    lv_obj_align(item->title, LV_ALIGN_LEFT_MID,
                 ui_adapt(VIDEO_BADGE_SIDE + UI_SPACE_MD), -ui_adapt(12));

    item->meta = lv_label_create(item->row);
    lv_obj_set_width(item->meta, ui_adapt(VIDEO_NAME_W));
    lv_obj_set_style_text_color(item->meta, UI_COLOR_TEXT_SEC, 0);
    lv_obj_align(item->meta, LV_ALIGN_LEFT_MID,
                 ui_adapt(VIDEO_BADGE_SIDE + UI_SPACE_MD), ui_adapt(14));

    item->arrow = lv_label_create(item->row);
    lv_label_set_text(item->arrow, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(item->arrow, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(item->arrow, UI_COLOR_TEXT_SEC, 0);
    lv_obj_align(item->arrow, LV_ALIGN_RIGHT_MID, 0, 0);

    item->check = lv_obj_create(item->row);
    lv_obj_remove_style_all(item->check);
    lv_obj_set_size(item->check, ui_adapt(VIDEO_CHECK_SIDE),
                    ui_adapt(VIDEO_CHECK_SIDE));
    lv_obj_set_style_radius(item->check, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(item->check, 2, 0);
    lv_obj_align(item->check, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_clear_flag(item->check,
                      LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    item->check_dot = lv_obj_create(item->check);
    lv_obj_remove_style_all(item->check_dot);
    lv_obj_set_size(item->check_dot, ui_adapt(VIDEO_CHECK_DOT),
                    ui_adapt(VIDEO_CHECK_DOT));
    lv_obj_set_style_radius(item->check_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(item->check_dot, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(item->check_dot, LV_OPA_COVER, 0);
    lv_obj_center(item->check_dot);

    refresh_row_text(item);
    update_row_selection(item);
}

static void on_list(OPERATE_RET rt, const ui_video_playback_item_t *items,
                    uint16_t count, uint32_t seq)
{
    if (!s_screen || seq != s_list_seq) return;
    clear_list();

    if (rt != OPRT_OK) {
        render_list_state(VIDEO_LIST_ERROR);
        refresh_titlebar();
        return;
    }
    if (count == 0) {
        render_list_state(VIDEO_LIST_EMPTY);
        refresh_titlebar();
        return;
    }

    s_items = (video_page_item_t *)tal_malloc(sizeof(*s_items) * count);
    if (!s_items) {
        render_list_state(VIDEO_LIST_ERROR);
        refresh_titlebar();
        return;
    }
    memset(s_items, 0, sizeof(*s_items) * count);
    s_item_count = count;
    s_list_state = VIDEO_LIST_READY;
    for (uint16_t i = 0; i < count; i++) {
        s_items[i].media = items[i];
        make_row(i);
    }
    refresh_titlebar();
}

static void on_frame(ui_video_playback_frame_t *frame)
{
    if (!frame || !frame->data) return;
    if (s_mode != VIDEO_PAGE_PLAYER || !s_picture) {
        ui_svc_video_playback_frame_free(frame->data);
        frame->data = NULL;
        return;
    }

    ui_comp_picture_set_rgb565(s_picture, frame->width, frame->height,
                               frame->data, ui_svc_video_playback_frame_free);
    frame->data = NULL;
    if (s_frame_hint_visible) set_frame_hint(s_frame_hint_key, false);
}

static void on_delete(OPERATE_RET rt, uint16_t deleted_count,
                      uint16_t failed_count, uint32_t seq)
{
    char text[96];

    if (!s_screen || seq != s_delete_seq) return;
    ui_comp_popup_dismiss();
    s_delete_busy = false;
    set_select_mode(false);
    s_selected[0] = '\0';

    if (rt == OPRT_OK && deleted_count > 0) {
        snprintf(text, sizeof(text),
                 ui_i18n_text(UI_TEXT_VIDEO_DELETE_SUCCESS_FMT),
                 (unsigned)deleted_count);
    } else if (deleted_count > 0) {
        snprintf(text, sizeof(text),
                 ui_i18n_text(UI_TEXT_VIDEO_DELETE_PARTIAL_FMT),
                 (unsigned)deleted_count, (unsigned)failed_count);
    } else {
        snprintf(text, sizeof(text), "%s",
                 ui_i18n_text(UI_TEXT_VIDEO_DELETE_FAILED));
    }
    ui_comp_popup_toast(text, 1800);
    request_list();
}

static void on_state(ui_video_playback_state_t state,
                     ui_video_playback_error_t error)
{
    ui_video_playback_state_t previous = s_play_state;
    s_play_state = state;
    refresh_controls();
    refresh_progress();
    refresh_titlebar();

    if (s_mode == VIDEO_PAGE_PLAYER) {
        if (state == UI_VIDEO_PLAYBACK_OPENING) {
            set_frame_hint(UI_TEXT_VIDEO_OPENING, true);
        } else if (state == UI_VIDEO_PLAYBACK_STOPPING) {
            set_frame_hint(UI_TEXT_VIDEO_STOPPING, true);
        } else if (state == UI_VIDEO_PLAYBACK_COMPLETED) {
            set_frame_hint(s_frame_hint_key, false);
        } else if (state == UI_VIDEO_PLAYBACK_IDLE) {
            set_frame_hint(UI_TEXT_VIDEO_STOPPED, true);
        } else if (state == UI_VIDEO_PLAYBACK_ERROR) {
            set_frame_hint(UI_TEXT_VIDEO_PLAY_FAILED, true);
        }
    }

    if (state == UI_VIDEO_PLAYBACK_COMPLETED &&
        previous == UI_VIDEO_PLAYBACK_STOPPING) {
        ui_comp_popup_toast(ui_i18n_text(UI_TEXT_VIDEO_PLAYBACK_ENDED), 1200);
    } else if (state == UI_VIDEO_PLAYBACK_ERROR) {
        ui_comp_popup_toast(ui_i18n_text(
            error == UI_VIDEO_PLAYBACK_ERROR_STORAGE ?
            UI_TEXT_VIDEO_STORAGE_UNAVAILABLE : UI_TEXT_VIDEO_PLAY_FAILED), 1800);
    }
}

static void refresh_progress(void)
{
    uint32_t pos_ms;
    uint32_t dur_ms;
    uint32_t pos_sec;
    uint32_t dur_sec;
    char text[16];
    int value = 0;

    if (!s_progress || !s_pos_label || !s_dur_label) return;
    pos_ms = ui_svc_video_playback_get_position_ms();
    dur_ms = ui_svc_video_playback_get_duration_ms();
    pos_sec = pos_ms / 1000u;
    dur_sec = dur_ms / 1000u;
    if (pos_sec == s_last_pos_sec && dur_sec == s_last_dur_sec) return;
    s_last_pos_sec = pos_sec;
    s_last_dur_sec = dur_sec;

    if (dur_ms > 0) value = (int)((uint64_t)pos_ms * 100u / dur_ms);
    if (value > 100) value = 100;
    lv_bar_set_value(s_progress, value, LV_ANIM_OFF);

    snprintf(text, sizeof(text), "%02u:%02u",
             (unsigned)(pos_sec / 60u), (unsigned)(pos_sec % 60u));
    lv_label_set_text(s_pos_label, text);
    snprintf(text, sizeof(text), "%02u:%02u",
             (unsigned)(dur_sec / 60u), (unsigned)(dur_sec % 60u));
    lv_label_set_text(s_dur_label, text);
}

static void refresh_controls(void)
{
    ui_i18n_key_t play_key;
    bool play_enabled;
    bool stop_enabled;

    if (!s_play_btn || !s_stop_btn) return;
    switch (s_play_state) {
    case UI_VIDEO_PLAYBACK_PLAYING:
        play_key = UI_TEXT_VIDEO_PAUSE;
        break;
    case UI_VIDEO_PLAYBACK_PAUSED:
        play_key = UI_TEXT_VIDEO_PLAY;
        break;
    default:
        play_key = UI_TEXT_VIDEO_REPLAY;
        break;
    }
    ui_comp_btn_set_text(s_play_btn, ui_i18n_text(play_key));
    ui_comp_btn_set_text(s_stop_btn, ui_i18n_text(UI_TEXT_VIDEO_STOP));

    play_enabled = s_play_state == UI_VIDEO_PLAYBACK_PLAYING ||
                   s_play_state == UI_VIDEO_PLAYBACK_PAUSED ||
                   s_play_state == UI_VIDEO_PLAYBACK_COMPLETED ||
                   s_play_state == UI_VIDEO_PLAYBACK_ERROR ||
                   s_play_state == UI_VIDEO_PLAYBACK_IDLE;
    stop_enabled = s_play_state == UI_VIDEO_PLAYBACK_PLAYING ||
                   s_play_state == UI_VIDEO_PLAYBACK_PAUSED ||
                   s_play_state == UI_VIDEO_PLAYBACK_OPENING ||
                   s_play_state == UI_VIDEO_PLAYBACK_ERROR;
    ui_comp_btn_set_enabled(s_play_btn, play_enabled);
    ui_comp_btn_set_enabled(s_stop_btn, stop_enabled);
}

static void play_cb(lv_event_t *e)
{
    (void)e;
    if (s_play_state == UI_VIDEO_PLAYBACK_PLAYING ||
        s_play_state == UI_VIDEO_PLAYBACK_PAUSED) {
        ui_svc_video_playback_pause_resume();
    } else if (s_selected[0]) {
        ui_comp_picture_clear(s_picture);
        set_frame_hint(UI_TEXT_VIDEO_OPENING, true);
        s_last_pos_sec = VIDEO_INVALID_SEC;
        s_last_dur_sec = VIDEO_INVALID_SEC;
        ui_svc_video_playback_play(s_selected);
    }
}

static void stop_cb(lv_event_t *e)
{
    (void)e;
    ui_comp_picture_clear(s_picture);
    set_frame_hint(UI_TEXT_VIDEO_STOPPING, true);
    ui_svc_video_playback_stop();
}

static void refresh_titlebar(void)
{
    char text[64];
    bool choose_enabled;

    if (!s_title || !s_back_btn || !s_choose_btn ||
        !s_cancel_btn || !s_select_all_btn) {
        return;
    }

    if (s_select_mode) {
        lv_obj_add_flag(s_back_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_choose_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_cancel_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_select_all_btn, LV_OBJ_FLAG_HIDDEN);
        refresh_selection_ui();
        return;
    }

    lv_obj_clear_flag(s_back_btn, LV_OBJ_FLAG_HIDDEN);
    if (s_item_count > 0) {
        lv_obj_clear_flag(s_choose_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_choose_btn, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(s_cancel_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_select_all_btn, LV_OBJ_FLAG_HIDDEN);
    if (s_list_state == VIDEO_LIST_READY && s_item_count > 0) {
        snprintf(text, sizeof(text), ui_i18n_text(UI_TEXT_VIDEO_COUNT_FMT),
                 (unsigned)s_item_count);
        lv_label_set_text(s_title, text);
    } else {
        lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_APP_VIDEO));
    }

    choose_enabled = s_mode == VIDEO_PAGE_LIST && s_item_count > 0 &&
                     playback_file_released() && !s_delete_busy;
    ui_comp_btn_set_enabled(s_choose_btn, choose_enabled);
}

static void apply_selection_layout(void)
{
    if (!s_bottom_bar || !s_list) return;
    if (s_select_mode) {
        lv_obj_clear_flag(s_bottom_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_pad_bottom(
            s_list, ui_adapt(VIDEO_BOTTOM_BAR_H + VIDEO_LIST_PAD), 0);
    } else {
        lv_obj_add_flag(s_bottom_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_pad_bottom(s_list, ui_adapt(VIDEO_LIST_PAD), 0);
    }
}

static void set_select_mode(bool enabled)
{
    if (enabled && (s_item_count == 0 || !playback_file_released() ||
                    s_delete_busy)) {
        return;
    }

    s_select_mode = enabled;
    s_select_count = 0;
    for (uint16_t i = 0; i < s_item_count; i++) {
        s_items[i].selected = false;
        update_row_selection(&s_items[i]);
    }
    apply_selection_layout();
    refresh_titlebar();
}

static void choose_cb(lv_event_t *e)
{
    (void)e;
    set_select_mode(true);
}

static void cancel_cb(lv_event_t *e)
{
    (void)e;
    set_select_mode(false);
}

static void select_all_cb(lv_event_t *e)
{
    bool select_all;
    (void)e;

    if (!s_select_mode || s_delete_busy) return;
    select_all = s_select_count < s_item_count;
    s_select_count = select_all ? s_item_count : 0;
    for (uint16_t i = 0; i < s_item_count; i++) {
        s_items[i].selected = select_all;
        update_row_selection(&s_items[i]);
    }
    refresh_selection_ui();
}

static void start_delete_async(void *data)
{
    const char *names[UI_VIDEO_PLAYBACK_LIST_MAX];
    uint16_t count = 0;
    OPERATE_RET rt;
    (void)data;

    if (!s_screen || !s_select_mode || s_select_count == 0 || s_delete_busy) {
        return;
    }
    for (uint16_t i = 0; i < s_item_count &&
         count < UI_VIDEO_PLAYBACK_LIST_MAX; i++) {
        if (s_items[i].selected && s_items[i].media.name[0]) {
            names[count++] = s_items[i].media.name;
        }
    }
    if (count == 0) return;

    s_delete_busy = true;
    refresh_selection_ui();
    ui_comp_popup_show(UI_COMP_POPUP_LOADING, NULL,
                       ui_i18n_text(UI_TEXT_VIDEO_DELETING), NULL);
    rt = ui_svc_video_playback_delete_batch(names, count, ++s_delete_seq);
    if (rt != OPRT_OK) {
        ui_comp_popup_dismiss();
        s_delete_busy = false;
        refresh_selection_ui();
        ui_comp_popup_toast(ui_i18n_text(UI_TEXT_VIDEO_DELETE_FAILED), 1800);
    }
}

static void delete_confirm_cb(bool confirmed)
{
    if (confirmed) lv_async_call(start_delete_async, NULL);
}

static void delete_cb(lv_event_t *e)
{
    char text[80];
    (void)e;

    if (!s_select_mode || s_select_count == 0 || s_delete_busy) return;
    snprintf(text, sizeof(text),
             ui_i18n_text(UI_TEXT_VIDEO_DELETE_CONFIRM_FMT),
             (unsigned)s_select_count);
    ui_comp_popup_show(UI_COMP_POPUP_CONFIRM, NULL, text, delete_confirm_cb);
}

static void set_mode(video_page_mode_t mode)
{
    char title[UI_VIDEO_PLAYBACK_NAME_MAX];

    s_mode = mode;
    if (mode == VIDEO_PAGE_LIST) {
        ui_comp_statusbar_set_visible(true);
        lv_obj_set_style_pad_top(s_screen, ui_adapt(VIDEO_STATUSBAR_H), 0);
        lv_obj_add_flag(s_player_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_titlebar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_list_cont, LV_OBJ_FLAG_HIDDEN);
        refresh_titlebar();
    } else {
        set_select_mode(false);
        ui_comp_statusbar_set_visible(false);
        lv_obj_set_style_pad_top(s_screen, 0, 0);
        lv_obj_add_flag(s_titlebar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_list_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_player_cont, LV_OBJ_FLAG_HIDDEN);
        format_video_title(s_selected, title, sizeof(title));
        lv_label_set_text(s_player_title, title);
    }
    lv_obj_update_layout(s_screen);
}

static void go_back(void)
{
    if (s_delete_busy) return;
    if (s_select_mode) {
        set_select_mode(false);
    } else if (s_mode == VIDEO_PAGE_PLAYER) {
        ui_comp_picture_clear(s_picture);
        ui_svc_video_playback_stop();
        s_selected[0] = '\0';
        set_mode(VIDEO_PAGE_LIST);
        request_list();
    } else {
        ui_route_pop();
    }
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    go_back();
}

static void gesture_cb(lv_event_t *e)
{
    (void)e;
    if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());
        go_back();
    }
}

static void build_titlebar(void)
{
    s_titlebar = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_titlebar);
    lv_obj_set_size(s_titlebar, LV_PCT(100), ui_adapt(VIDEO_TITLEBAR_H));
    lv_obj_set_style_bg_opa(s_titlebar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_titlebar, LV_OBJ_FLAG_SCROLLABLE);

    s_back_btn = ui_comp_btn_icon_create(s_titlebar, &icon_back_24_24, back_cb);
    lv_obj_align(s_back_btn, LV_ALIGN_LEFT_MID, ui_adapt(UI_SPACE_SM), 0);

    s_cancel_btn = ui_comp_btn_create_styled(
        s_titlebar, ui_i18n_text(UI_TEXT_CANCEL), UI_COMP_BTN_TEXT, cancel_cb);
    lv_obj_align(s_cancel_btn, LV_ALIGN_LEFT_MID, ui_adapt(UI_SPACE_SM), 0);
    lv_obj_add_flag(s_cancel_btn, LV_OBJ_FLAG_HIDDEN);

    s_title = lv_label_create(s_titlebar);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_title, ui_adapt(VIDEO_TITLE_W));
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_title, UI_COLOR_TEXT, 0);
    lv_obj_align(s_title, LV_ALIGN_CENTER, 0, 0);

    s_choose_btn = ui_comp_btn_icon_create(s_titlebar, &icon_choose, choose_cb);
    lv_obj_align(s_choose_btn, LV_ALIGN_RIGHT_MID, -ui_adapt(UI_SPACE_SM), 0);

    s_select_all_btn = ui_comp_btn_create_styled(
        s_titlebar, ui_i18n_text(UI_TEXT_VIDEO_SELECT_ALL),
        UI_COMP_BTN_TEXT, select_all_cb);
    lv_obj_align(s_select_all_btn, LV_ALIGN_RIGHT_MID,
                 -ui_adapt(UI_SPACE_XS), 0);
    lv_obj_add_flag(s_select_all_btn, LV_OBJ_FLAG_HIDDEN);
}

static void build_list(void)
{
    s_list_cont = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_list_cont);
    lv_obj_set_width(s_list_cont, LV_PCT(100));
    lv_obj_set_flex_grow(s_list_cont, 1);
    lv_obj_set_style_bg_opa(s_list_cont, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_list_cont, LV_OBJ_FLAG_SCROLLABLE);

    s_list = lv_obj_create(s_list_cont);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_size(s_list, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(s_list, ui_adapt(VIDEO_LIST_PAD), 0);
    lv_obj_set_style_pad_row(s_list, ui_adapt(UI_SPACE_SM), 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);

    s_bottom_bar = lv_obj_create(s_list_cont);
    lv_obj_remove_style_all(s_bottom_bar);
    lv_obj_set_size(s_bottom_bar, LV_PCT(100),
                    ui_adapt(VIDEO_BOTTOM_BAR_H));
    lv_obj_align(s_bottom_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_bottom_bar, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_bg_opa(s_bottom_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_bottom_bar, lv_color_hex(0x454545), 0);
    lv_obj_set_style_border_width(s_bottom_bar, 1, 0);
    lv_obj_set_style_border_side(s_bottom_bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_clear_flag(s_bottom_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_delete_btn = ui_comp_btn_create_with_icon(
        s_bottom_bar, &icon_delete, ui_i18n_text(UI_TEXT_DELETE),
        UI_COMP_BTN_PRIMARY, UI_COMP_BTN_ICON_LEFT, delete_cb);
    lv_obj_set_size(s_delete_btn, ui_adapt(176), ui_adapt(42));
    lv_obj_set_style_bg_color(s_delete_btn, UI_COLOR_ERROR, 0);
    lv_obj_set_style_bg_color(s_delete_btn, lv_color_hex(0xC62828),
                              LV_STATE_PRESSED);
    lv_obj_align(s_delete_btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_bottom_bar, LV_OBJ_FLAG_HIDDEN);
}

static void build_player(void)
{
    lv_obj_t *header;
    lv_obj_t *time_row;
    lv_obj_t *button_row;

    s_player_cont = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_player_cont);
    lv_obj_set_width(s_player_cont, LV_PCT(100));
    lv_obj_set_flex_grow(s_player_cont, 1);
    lv_obj_set_style_bg_color(s_player_cont, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_player_cont, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(s_player_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_player_cont, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_player_cont, LV_OBJ_FLAG_SCROLLABLE);

    header = lv_obj_create(s_player_cont);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), ui_adapt(VIDEO_PLAYER_HEADER_H));
    lv_obj_set_style_bg_color(header, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = ui_comp_btn_icon_create(header, &icon_back_24_24, back_cb);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, ui_adapt(UI_SPACE_SM), 0);

    s_player_title = lv_label_create(header);
    lv_label_set_long_mode(s_player_title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_player_title, ui_adapt(VIDEO_PLAYER_TITLE_W));
    lv_obj_set_style_text_align(s_player_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_player_title, UI_COLOR_TEXT, 0);
    lv_obj_align(s_player_title, LV_ALIGN_CENTER, 0, 0);

    s_frame_box = lv_obj_create(s_player_cont);
    lv_obj_remove_style_all(s_frame_box);
    lv_obj_set_size(s_frame_box, ui_adapt(VIDEO_FRAME_SIDE),
                    ui_adapt(VIDEO_FRAME_SIDE));
    lv_obj_set_style_bg_color(s_frame_box, lv_color_hex(0x101010), 0);
    lv_obj_set_style_bg_opa(s_frame_box, LV_OPA_COVER, 0);
    lv_obj_set_style_clip_corner(s_frame_box, true, 0);
    lv_obj_clear_flag(s_frame_box, LV_OBJ_FLAG_SCROLLABLE);

    s_picture = ui_comp_picture_create(s_frame_box);
    if (s_picture) lv_obj_clear_flag(s_picture, LV_OBJ_FLAG_CLICKABLE);

    s_frame_hint = lv_label_create(s_frame_box);
    lv_obj_set_width(s_frame_hint, LV_PCT(80));
    lv_obj_set_style_text_align(s_frame_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_frame_hint, UI_COLOR_TEXT_SEC, 0);
    lv_obj_center(s_frame_hint);

    lv_obj_t *controls = lv_obj_create(s_player_cont);
    lv_obj_remove_style_all(controls);
    lv_obj_set_size(controls, LV_PCT(100), ui_adapt(VIDEO_PLAYER_CTRL_H));
    lv_obj_set_style_pad_hor(controls, ui_adapt(UI_SPACE_MD), 0);
    lv_obj_set_style_pad_top(controls, ui_adapt(UI_SPACE_SM), 0);
    lv_obj_set_style_pad_bottom(controls, ui_adapt(UI_SPACE_XS), 0);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(controls, ui_adapt(UI_SPACE_XS), 0);
    lv_obj_clear_flag(controls, LV_OBJ_FLAG_SCROLLABLE);

    s_progress = lv_bar_create(controls);
    lv_obj_set_size(s_progress, LV_PCT(100), ui_adapt(5));
    lv_bar_set_range(s_progress, 0, 100);
    lv_bar_set_value(s_progress, 0, LV_ANIM_OFF);

    time_row = lv_obj_create(controls);
    lv_obj_remove_style_all(time_row);
    lv_obj_set_size(time_row, LV_PCT(100), ui_adapt(20));
    lv_obj_clear_flag(time_row, LV_OBJ_FLAG_SCROLLABLE);

    s_pos_label = lv_label_create(time_row);
    lv_label_set_text(s_pos_label, ui_i18n_text(UI_TEXT_VIDEO_TIME_ZERO));
    lv_obj_set_style_text_color(s_pos_label, UI_COLOR_TEXT_SEC, 0);
    lv_obj_align(s_pos_label, LV_ALIGN_LEFT_MID, 0, 0);

    s_dur_label = lv_label_create(time_row);
    lv_label_set_text(s_dur_label, ui_i18n_text(UI_TEXT_VIDEO_TIME_ZERO));
    lv_obj_set_style_text_color(s_dur_label, UI_COLOR_TEXT_SEC, 0);
    lv_obj_align(s_dur_label, LV_ALIGN_RIGHT_MID, 0, 0);

    button_row = lv_obj_create(controls);
    lv_obj_remove_style_all(button_row);
    lv_obj_set_width(button_row, LV_PCT(100));
    lv_obj_set_flex_grow(button_row, 1);
    lv_obj_set_flex_flow(button_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button_row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(button_row, ui_adapt(UI_SPACE_XL), 0);
    lv_obj_clear_flag(button_row, LV_OBJ_FLAG_SCROLLABLE);

    s_play_btn = ui_comp_btn_circle_text_create(
        button_row, ui_i18n_text(UI_TEXT_VIDEO_PLAY), UI_COLOR_PRIMARY, play_cb);
    s_stop_btn = ui_comp_btn_circle_text_create(
        button_row, ui_i18n_text(UI_TEXT_VIDEO_STOP), UI_COLOR_ERROR, stop_cb);

    lv_obj_add_flag(s_player_cont, LV_OBJ_FLAG_HIDDEN);
}

static void request_list(void)
{
    clear_list();
    apply_selection_layout();
    render_list_state(VIDEO_LIST_LOADING);
    refresh_titlebar();
    ui_svc_video_playback_list_request(++s_list_seq);
}

static void on_create(void *parent)
{
    (void)parent;
    s_mode = VIDEO_PAGE_LIST;
    s_list_state = VIDEO_LIST_LOADING;
    s_play_state = UI_VIDEO_PLAYBACK_IDLE;
    s_items = NULL;
    s_item_count = 0;
    s_select_count = 0;
    s_list_seq = 0;
    s_delete_seq = 0;
    s_last_pos_sec = VIDEO_INVALID_SEC;
    s_last_dur_sec = VIDEO_INVALID_SEC;
    s_select_mode = false;
    s_delete_busy = false;
    s_frame_hint_visible = true;
    s_frame_hint_key = UI_TEXT_VIDEO_OPENING;
    s_selected[0] = '\0';

    s_screen = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_screen, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_top(s_screen, ui_adapt(VIDEO_STATUSBAR_H), 0);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_screen,
                      LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_screen, gesture_cb, LV_EVENT_GESTURE, NULL);

    build_titlebar();
    build_list();
    build_player();
    lv_obj_update_layout(s_screen);
    set_frame_hint(UI_TEXT_VIDEO_OPENING, true);
    set_mode(VIDEO_PAGE_LIST);
    ui_svc_video_playback_set_cbs(on_list, on_frame, on_state, on_delete);
}

static void on_enter(uint32_t dirty)
{
    if (ui_dirty_is_enter(dirty)) {
        ui_svc_video_playback_set_cbs(on_list, on_frame, on_state, on_delete);
        s_play_state = ui_svc_video_playback_get_state();
        set_mode(VIDEO_PAGE_LIST);
        request_list();
    }
    if (dirty & (1u << UI_STATE_GROUP_SYSTEM)) {
        if (s_mode == VIDEO_PAGE_PLAYER) refresh_progress();
    }
    if (dirty & (1u << UI_STATE_GROUP_SETTINGS)) {
        ui_comp_btn_set_text(s_cancel_btn, ui_i18n_text(UI_TEXT_CANCEL));
        refresh_titlebar();
        if (s_list_state == VIDEO_LIST_READY) {
            for (uint16_t i = 0; i < s_item_count; i++) {
                refresh_row_text(&s_items[i]);
            }
        } else {
            render_list_state(s_list_state);
        }
        if (s_mode == VIDEO_PAGE_PLAYER && s_selected[0]) {
            char title[UI_VIDEO_PLAYBACK_NAME_MAX];
            format_video_title(s_selected, title, sizeof(title));
            lv_label_set_text(s_player_title, title);
        }
        refresh_controls();
        if (s_frame_hint_visible) {
            set_frame_hint(s_frame_hint_key, true);
        }
    }
}

static void on_leave(void)
{
    if (s_picture) ui_comp_picture_clear(s_picture);
    ui_svc_video_playback_set_cbs(NULL, NULL, NULL, NULL);
    ui_svc_video_playback_stop();
    if (s_delete_busy) {
        s_delete_seq++;
        s_delete_busy = false;
        ui_comp_popup_dismiss();
    }
    ui_comp_statusbar_set_visible(true);
}

static void on_destroy(void)
{
    clear_list();
    if (s_picture) ui_comp_picture_clear(s_picture);
    ui_svc_video_playback_set_cbs(NULL, NULL, NULL, NULL);
    ui_svc_video_playback_stop();
    if (s_delete_busy) ui_comp_popup_dismiss();
    ui_comp_statusbar_set_visible(true);

    if (s_screen) {
        lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_screen);
    }
    s_screen = NULL;
    s_titlebar = NULL;
    s_title = NULL;
    s_back_btn = NULL;
    s_choose_btn = NULL;
    s_cancel_btn = NULL;
    s_select_all_btn = NULL;
    s_list_cont = NULL;
    s_list = NULL;
    s_bottom_bar = NULL;
    s_delete_btn = NULL;
    s_player_cont = NULL;
    s_frame_box = NULL;
    s_picture = NULL;
    s_frame_hint = NULL;
    s_player_title = NULL;
    s_progress = NULL;
    s_pos_label = NULL;
    s_dur_label = NULL;
    s_play_btn = NULL;
    s_stop_btn = NULL;
    s_items = NULL;
    s_item_count = 0;
    s_select_count = 0;
    s_select_mode = false;
    s_delete_busy = false;
    s_frame_hint_visible = false;
    s_selected[0] = '\0';
}

const ui_page_entry_t ui_page_video_entry = {
    .id = UI_PAGE_VIDEO,
    .name = "video",
    .lifecycle = { on_create, on_enter, on_leave, on_destroy }
};
