#include "lvgl.h"
#include "ui_route.h"
#include "ui_theme.h"
#include "ui_adaptive.h"
#include "ui_i18n.h"
#include "ui_state.h"
#include "ui_comp_btn.h"
#include "ui_comp_popup.h"
#include "ui_svc_recording.h"
#include "ui_page_ids.h"
#include "ui_page_recording_transcribe.h"
#include "ty_cJSON.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

LV_IMG_DECLARE(icon_back_24_24);
LV_IMG_DECLARE(icon_ai_icon);   /* 复用 chat 页 AI 图标（48x48） */

#define RLIST_STATUSBAR_H  32
#define RLIST_TITLEBAR_H   48
#define RLIST_ITEM_H       64
#define RLIST_BG_NORMAL    0x353740
#define RLIST_BG_SELECTED  0x2E4A6E   /* selected row — same as music list (MLIST_BG_CURRENT) */
#define RLIST_MAX_ROWS     20
#define RLIST_ACCENT       0xF3E55D
#define RLIST_HINT         0xB8BDDE
/* Inline player card: distinct (lighter) fill + border so it stands out from
 * the same-coloured list rows underneath; bar track is dark so the empty
 * progress bar is visible before playback starts. */
#define RLIST_PLAYER_BG    0x4A5266
#define RLIST_PLAYER_BORDER 0x6FCF97   /* 浅绿描边：与黄色强调/蓝色选中行都区分得开 */
#define RLIST_BAR_TRACK    0x2B2F3A
#define RLIST_PLAYER_H     168

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_title  = NULL;
static lv_obj_t *s_count  = NULL;   /* 标题栏右侧：录音总条数 */
static lv_obj_t *s_list   = NULL;

static lv_obj_t *s_player      = NULL;
static lv_obj_t *s_pl_name     = NULL;
static lv_obj_t *s_pl_bar      = NULL;
static lv_obj_t *s_pl_cur      = NULL;
static lv_obj_t *s_pl_total    = NULL;
static lv_obj_t *s_pl_playicon = NULL;
static lv_obj_t *s_pl_ai       = NULL;  /* AI 图标：进入转写子页面 */
static int       s_player_id   = -1;
static int       s_selected_id = -1;   /* 列表当前高亮行（独立于播放卡，收起后仍保留） */
static uint32_t  s_player_dur  = 0;
static int32_t   s_shown_sec   = -1;   /* last rendered playback second; -1 = fresh card */

static int  s_row_ids[RLIST_MAX_ROWS];
static int  s_row_cnt = 0;
static bool s_cb_attached = false;

static void on_create(void *parent);
static void on_enter(uint32_t dirty);
static void on_leave(void);
static void on_destroy(void);
static void back_cb(lv_event_t *e);
static void gesture_cb(lv_event_t *e);
static void row_cb(lv_event_t *e);
static void request_list(void);
static void build_rows(ty_cJSON *list);
static void on_list_data(ty_cJSON *list);
static void on_rec_status(const ui_recording_status_t *st);
static void show_placeholder(ui_i18n_key_t key);
static void highlight_row(int sel_id);
static void set_selected(int id);
static void refresh_count(void);
static void player_build(void);
static void player_show(int id, const char *name, uint32_t dur);
static void player_hide(void);
static void player_refresh_progress(void);
static void pl_toggle_cb(lv_event_t *e);
static void pl_delete_cb(lv_event_t *e);
static void pl_collapse_cb(lv_event_t *e);
static void pl_ai_cb(lv_event_t *e);

static void show_placeholder(ui_i18n_key_t key)
{
    lv_obj_clean(s_list);
    s_row_cnt = 0;
    lv_obj_t *lbl = lv_label_create(s_list);
    lv_label_set_text(lbl, ui_i18n_text(key));
    lv_obj_set_style_text_color(lbl, lv_color_hex(RLIST_HINT), 0);
    lv_obj_center(lbl);
}

static void make_row(int id, const char *name, const char *date, uint32_t dur)
{
    if (s_row_cnt >= RLIST_MAX_ROWS) return;
    lv_obj_t *item = lv_obj_create(s_list);
    lv_obj_remove_style_all(item);
    lv_obj_set_size(item, LV_PCT(100), ui_adapt(RLIST_ITEM_H));
    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(item, ui_adapt(16), 0);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(item, lv_color_hex(RLIST_BG_NORMAL), 0);
    lv_obj_set_style_pad_hor(item, ui_adapt(16), 0);
    lv_obj_set_style_pad_ver(item, ui_adapt(10), 0);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(item, row_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)id);

    /* Date is intentionally not shown — the recording filename is already
     * timestamp-based, so a separate datetime line would be redundant. */
    (void)date;

    lv_obj_t *nm = lv_label_create(item);
    lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
    lv_label_set_text(nm, (name && name[0]) ? name : "REC");
    lv_obj_set_width(nm, ui_adapt(150));
    lv_obj_align(nm, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_text_color(nm, lv_color_white(), 0);

    lv_obj_t *du = lv_label_create(item);
    lv_label_set_text_fmt(du, "%02u:%02u:%02u", dur / 3600, (dur % 3600) / 60, dur % 60);
    lv_obj_align(du, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_text_color(du, lv_color_hex(RLIST_ACCENT), 0);
    /* Stash the raw seconds so row_cb gets the authoritative value back
     * without re-parsing the formatted hh:mm:ss string. */
    lv_obj_set_user_data(du, (void *)(uintptr_t)dur);

    s_row_ids[s_row_cnt++] = id;
}

static void request_list(void)
{
    if (!s_list) return;
    if (s_row_cnt == 0) show_placeholder(UI_TEXT_LOADING);
    ui_svc_recording_list_async(on_list_data);
}

static void build_rows(ty_cJSON *list)
{
    lv_obj_clean(s_list);
    s_row_cnt = 0;
    int n = (list && ty_cJSON_IsArray(list)) ? ty_cJSON_GetArraySize(list) : 0;
    if (n == 0) {
        show_placeholder(UI_TEXT_RECORDING_EMPTY);
        refresh_count();   /* 空列表时隐藏计数 */
        return;
    }
    for (int i = 0; i < n; i++) {
        ty_cJSON *row = ty_cJSON_GetArrayItem(list, i);
        if (!row || !ty_cJSON_IsObject(row)) continue;
        ty_cJSON *jid = ty_cJSON_GetObjectItem(row, "id");
        ty_cJSON *jnm = ty_cJSON_GetObjectItem(row, "name");
        ty_cJSON *jdt = ty_cJSON_GetObjectItem(row, "datetime");
        ty_cJSON *jdu = ty_cJSON_GetObjectItem(row, "duration");
        int id = (jid && ty_cJSON_IsNumber(jid)) ? jid->valueint : -1;
        const char *nm = (jnm && ty_cJSON_IsString(jnm)) ? jnm->valuestring : "";
        const char *dt = (jdt && ty_cJSON_IsString(jdt)) ? jdt->valuestring : "";
        uint32_t du = (jdu && ty_cJSON_IsNumber(jdu)) ? (uint32_t)jdu->valueint : 0;
        make_row(id, nm, dt, du);
    }
    /* 选中行：沿用上次选中（若仍在列表中），否则默认第一条。刷新计数。 */
    int sel = s_row_ids[0];
    for (int i = 0; i < s_row_cnt; i++) {
        if (s_row_ids[i] == s_selected_id) { sel = s_selected_id; break; }
    }
    set_selected(sel);
}

static void on_list_data(ty_cJSON *list)
{
    if (!s_list) { if (list) ty_cJSON_Delete(list); return; }
    build_rows(list);
    if (list) ty_cJSON_Delete(list);
}

static void row_cb(lv_event_t *e)
{
    int id = (int)(uintptr_t)lv_event_get_user_data(e);
    lv_obj_t *item = lv_event_get_target(e);
    /* Children created in make_row order: 0=name, 1=duration. name is a display
     * string; duration's raw seconds live in the duration label's user_data. */
    const char *nm = lv_label_get_text(lv_obj_get_child(item, 0));
    uint32_t dur = (uint32_t)(uintptr_t)lv_obj_get_user_data(lv_obj_get_child(item, 1));
    /* 仅弹出播放卡片（暂停态），等用户点播放再开始——不默认自动播放。 */
    player_show(id, nm, dur);
}

/* Highlight the row whose recording id == sel_id (pass -1 to clear all).
 * Rows are children of s_list in the same order as s_row_ids[]. Matches the
 * music list: selected row gets a primary-tinted fill + accent-coloured name,
 * no border. (name label is child 0 of the row — see make_row.) */
static void highlight_row(int sel_id)
{
    if (!s_list) return;
    uint32_t n = lv_obj_get_child_cnt(s_list);
    for (uint32_t i = 0; i < n && i < (uint32_t)s_row_cnt; i++) {
        lv_obj_t *item = lv_obj_get_child(s_list, i);
        bool sel = (s_row_ids[i] == sel_id);
        lv_obj_set_style_bg_color(item,
            lv_color_hex(sel ? RLIST_BG_SELECTED : RLIST_BG_NORMAL), 0);
        lv_obj_t *nm = lv_obj_get_child(item, 0);
        if (nm) {
            lv_obj_set_style_text_color(nm,
                sel ? lv_color_hex(RLIST_ACCENT) : lv_color_white(), 0);
        }
    }
}

/* Title-bar counter "selected/total", mirroring the music list. Hidden when
 * the list is empty; shows "-/N" if the selected id isn't a current row. */
static void refresh_count(void)
{
    if (!s_count) return;
    if (s_row_cnt == 0) {
        lv_obj_add_flag(s_count, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    int idx = -1;
    for (int i = 0; i < s_row_cnt; i++) {
        if (s_row_ids[i] == s_selected_id) { idx = i; break; }
    }
    if (idx >= 0) lv_label_set_text_fmt(s_count, "%d/%d", idx + 1, s_row_cnt);
    else          lv_label_set_text_fmt(s_count, "-/%d", s_row_cnt);
    lv_obj_clear_flag(s_count, LV_OBJ_FLAG_HIDDEN);
}

/* Set the highlighted row and refresh the counter. */
static void set_selected(int id)
{
    s_selected_id = id;
    highlight_row(id);
    refresh_count();
}

static void player_build(void)
{
    s_player = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_player);
    lv_obj_set_size(s_player, LV_PCT(94), ui_adapt(RLIST_PLAYER_H));
    /* Float above the list (ignore the screen's flex layout) so BOTTOM_MID
     * actually centres it horizontally instead of being placed at the flex
     * cross-axis start (which left-shifted it). */
    lv_obj_add_flag(s_player, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(s_player, LV_ALIGN_BOTTOM_MID, 0, -ui_adapt(10));
    lv_obj_set_style_radius(s_player, ui_adapt(16), 0);
    lv_obj_set_style_bg_color(s_player, lv_color_hex(RLIST_PLAYER_BG), 0);
    lv_obj_set_style_bg_opa(s_player, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_player, ui_adapt(2), 0);
    lv_obj_set_style_border_color(s_player, lv_color_hex(RLIST_PLAYER_BORDER), 0);
    lv_obj_set_style_pad_all(s_player, ui_adapt(14), 0);
    lv_obj_clear_flag(s_player, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_player, LV_OBJ_FLAG_HIDDEN);

    s_pl_name = lv_label_create(s_player);
    lv_label_set_long_mode(s_pl_name, LV_LABEL_LONG_WRAP);  /* wrap, don't "..." */
    lv_obj_set_width(s_pl_name, LV_PCT(78));   /* leave a little room for collapse button */
    lv_obj_align(s_pl_name, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_color(s_pl_name, lv_color_white(), 0);

    lv_obj_t *collapse = ui_comp_btn_create_styled(s_player,
                    ui_i18n_text(UI_TEXT_RECORDING_COLLAPSE),
                    UI_COMP_BTN_TEXT, pl_collapse_cb);
    lv_obj_align(collapse, LV_ALIGN_TOP_RIGHT, 0, 0);

    s_pl_bar = lv_bar_create(s_player);
    lv_obj_set_size(s_pl_bar, LV_PCT(100), ui_adapt(6));
    /* 上移进度条与时间，给底部 48px 控件行让位（避免与时间标签重叠）。 */
    lv_obj_align(s_pl_bar, LV_ALIGN_CENTER, 0, -ui_adapt(10));
    /* Dark track stays visible against the lighter card even at 0% (before
     * playback), so the bar doesn't look like it's missing. */
    lv_obj_set_style_bg_color(s_pl_bar, lv_color_hex(RLIST_BAR_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_pl_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_pl_bar, ui_adapt(3), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_pl_bar, lv_color_hex(RLIST_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_pl_bar, ui_adapt(3), LV_PART_INDICATOR);
    lv_bar_set_range(s_pl_bar, 0, 1000);   /* permille: 0.1% steps keep long files smooth */
    lv_bar_set_value(s_pl_bar, 0, LV_ANIM_OFF);

    s_pl_cur = lv_label_create(s_player);
    lv_label_set_text(s_pl_cur, "00:00");
    lv_obj_align(s_pl_cur, LV_ALIGN_LEFT_MID, 0, ui_adapt(6));
    lv_obj_set_style_text_color(s_pl_cur, lv_color_hex(RLIST_HINT), 0);

    s_pl_total = lv_label_create(s_player);
    lv_label_set_text(s_pl_total, "00:00");
    lv_obj_align(s_pl_total, LV_ALIGN_RIGHT_MID, 0, ui_adapt(6));
    lv_obj_set_style_text_color(s_pl_total, lv_color_hex(RLIST_HINT), 0);

    /* 底部控件行：播放/暂停 · 删除 · AI 转写，等距分布。用 flex 行统一布局与
     * 垂直居中，避免按钮挨太近；三者按压时降透明度作反馈。 */
    lv_obj_t *ctrl = lv_obj_create(s_player);
    lv_obj_remove_style_all(ctrl);
    lv_obj_set_size(ctrl, LV_PCT(100), ui_adapt(48));
    lv_obj_align(ctrl, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(ctrl, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(ctrl, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(ctrl, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctrl, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 播放/暂停（左） */
    s_pl_playicon = lv_label_create(ctrl);
    lv_label_set_text(s_pl_playicon, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_font(s_pl_playicon, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_pl_playicon, lv_color_hex(RLIST_ACCENT), 0);
    lv_obj_add_flag(s_pl_playicon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_opa(s_pl_playicon, LV_OPA_50, LV_STATE_PRESSED);
    lv_obj_add_event_cb(s_pl_playicon, pl_toggle_cb, LV_EVENT_CLICKED, NULL);

    /* 删除（中） */
    lv_obj_t *del = lv_label_create(ctrl);
    lv_label_set_text(del, LV_SYMBOL_TRASH);
    lv_obj_set_style_text_font(del, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(del, lv_color_hex(0xEC5C5C), 0);
    lv_obj_add_flag(del, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_opa(del, LV_OPA_50, LV_STATE_PRESSED);
    lv_obj_add_event_cb(del, pl_delete_cb, LV_EVENT_CLICKED, NULL);

    /* AI 转写图标（右）。保持原始 48x48 不缩放，靠 flex 垂直居中与另两个图标
     * 中心对齐。 */
    s_pl_ai = lv_img_create(ctrl);
    lv_img_set_src(s_pl_ai, &icon_ai_icon);
    lv_obj_add_flag(s_pl_ai, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_opa(s_pl_ai, LV_OPA_50, LV_STATE_PRESSED);
    lv_obj_add_event_cb(s_pl_ai, pl_ai_cb, LV_EVENT_CLICKED, NULL);
}

static void player_show(int id, const char *name, uint32_t dur)
{
    s_player_id = id;
    s_player_dur = dur;
    s_shown_sec = -1;
    lv_label_set_text(s_pl_name, (name && name[0]) ? name : "REC");
    lv_label_set_text_fmt(s_pl_total, "%02u:%02u:%02u", dur / 3600, (dur % 3600) / 60, dur % 60);
    lv_label_set_text(s_pl_cur, "00:00:00");
    lv_bar_set_value(s_pl_bar, 0, LV_ANIM_OFF);
    lv_label_set_text(s_pl_playicon, LV_SYMBOL_PLAY);   /* 待播：显示播放，不自动播放 */
    lv_obj_clear_flag(s_player, LV_OBJ_FLAG_HIDDEN);
    set_selected(id);
}

static void player_hide(void)
{
    if (s_player) lv_obj_add_flag(s_player, LV_OBJ_FLAG_HIDDEN);
    s_player_id = -1;
    /* 收起播放卡后保持当前选中行高亮（不清除、不重置），选中态随 s_selected_id 保留。 */
}

static void player_refresh_progress(void)
{
    uint32_t off = 0, len = 0, cur = 0;
    if (!s_player || lv_obj_has_flag(s_player, LV_OBJ_FLAG_HIDDEN)) return;
    /* No active playback (or unknown length): keep whatever is shown —
     * player_show already reset the card to zero. */
    if (ui_svc_recording_play_progress_bytes(&off, &len) != OPRT_OK) return;

    /* Byte-linear time mapping is exact for the CBR capture stream and gives
     * 1s resolution regardless of file length (the old integer-percent API
     * stepped ~4 min at a time on a 6h recording). */
    if (s_player_dur > 0) cur = (uint32_t)((uint64_t)s_player_dur * off / len);
    if ((int32_t)cur != s_shown_sec) {
        s_shown_sec = (int32_t)cur;
        lv_bar_set_value(s_pl_bar, (int32_t)(((uint64_t)off * 1000u) / len), LV_ANIM_OFF);
        lv_label_set_text_fmt(s_pl_cur, "%02u:%02u:%02u", cur / 3600, (cur % 3600) / 60, cur % 60);
    }
    if (off >= len) {
        ui_svc_recording_play_stop();
        lv_label_set_text(s_pl_playicon, LV_SYMBOL_PLAY);
    }
}

static void pl_toggle_cb(lv_event_t *e)
{
    (void)e;
    ui_recording_status_t st;
    ui_svc_recording_get_status(&st);
    if (st.play_state == AI_PLAYER_PLAYING) {
        ui_svc_recording_play_pause();
        lv_label_set_text(s_pl_playicon, LV_SYMBOL_PLAY);
    } else if (st.play_state == AI_PLAYER_PAUSED) {
        ui_svc_recording_play_resume();
        lv_label_set_text(s_pl_playicon, LV_SYMBOL_PAUSE);
    } else {
        ui_svc_recording_play_id(s_player_id);
        lv_label_set_text(s_pl_playicon, LV_SYMBOL_PAUSE);
    }
}

static void del_confirm_cb(bool confirmed)
{
    if (!confirmed) return;
    int id = s_player_id;
    /* 删除后选中"上一条"：先定位被删行下标 di，目标 = di>0 ? 前一条
     * : 删除后的新首条（删第一条时）。仅一条时删完为空、无选中。 */
    int di = -1;
    for (int i = 0; i < s_row_cnt; i++) {
        if (s_row_ids[i] == id) { di = i; break; }
    }
    int target = -1;
    if (di > 0)             target = s_row_ids[di - 1];
    else if (s_row_cnt > 1) target = s_row_ids[1];

    ui_svc_recording_play_stop();
    ui_svc_recording_remove_id(id);
    s_selected_id = target;   /* 重建后由 build_rows 落到这一条（仍在列表中） */
    player_hide();
    request_list();
}

static void pl_delete_cb(lv_event_t *e)
{
    (void)e;
    ui_comp_popup_show(UI_COMP_POPUP_CONFIRM, NULL,
                       ui_i18n_text(UI_TEXT_RECORDING_DEL_CONFIRM), del_confirm_cb);
}

static void pl_collapse_cb(lv_event_t *e)
{
    (void)e;
    ui_svc_recording_play_stop();
    player_hide();
}

/* AI 图标：停掉本地回放，带目标 id 进入转写子页面。 */
static void pl_ai_cb(lv_event_t *e)
{
    (void)e;
    if (s_player_id < 0) return;
    ui_svc_recording_play_stop();
    ui_page_recording_transcribe_set_target(s_player_id);
    ui_route_push(UI_PAGE_RECORDING_TRANSCRIBE);
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
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_top(s_screen, ui_adapt(RLIST_STATUSBAR_H), 0);
    lv_obj_add_event_cb(s_screen, gesture_cb, LV_EVENT_GESTURE, NULL);

    lv_obj_t *titlebar = lv_obj_create(s_screen);
    lv_obj_remove_style_all(titlebar);
    lv_obj_set_size(titlebar, LV_PCT(100), ui_adapt(RLIST_TITLEBAR_H));
    lv_obj_set_style_bg_opa(titlebar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(titlebar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = ui_comp_btn_icon_create(titlebar, &icon_back_24_24, back_cb);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, ui_adapt(8), 0);

    s_title = lv_label_create(titlebar);
    lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_RECORDING_LIST_TITLE));
    lv_obj_set_style_text_color(s_title, UI_COLOR_TEXT, 0);
    lv_obj_align(s_title, LV_ALIGN_CENTER, 0, 0);

    /* 右侧计数 "当前/总数"（build_rows / 选中变化时刷新；空列表隐藏）。 */
    s_count = lv_label_create(titlebar);
    lv_label_set_text(s_count, "");
    lv_obj_set_style_text_color(s_count, lv_color_hex(RLIST_HINT), 0);
    lv_obj_align(s_count, LV_ALIGN_RIGHT_MID, -ui_adapt(12), 0);

    s_list = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_hor(s_list, ui_adapt(20), 0);
    lv_obj_set_style_pad_ver(s_list, ui_adapt(12), 0);
    lv_obj_set_style_pad_row(s_list, ui_adapt(10), 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);

    player_build();
    request_list();
    ui_svc_recording_set_cb(on_rec_status);
    s_cb_attached = true;
}

static void on_rec_status(const ui_recording_status_t *st)
{
    if (!s_screen || ui_route_current() != UI_PAGE_RECORDING_LIST) return;
    if (!s_player || lv_obj_has_flag(s_player, LV_OBJ_FLAG_HIDDEN)) return;
    if (st->play_state == AI_PLAYER_PLAYING)      lv_label_set_text(s_pl_playicon, LV_SYMBOL_PAUSE);
    else if (st->play_state == AI_PLAYER_PAUSED)  lv_label_set_text(s_pl_playicon, LV_SYMBOL_PLAY);
    else                                          lv_label_set_text(s_pl_playicon, LV_SYMBOL_PLAY);
}

static void on_enter(uint32_t dirty)
{
    if (!s_cb_attached) {
        ui_svc_recording_set_cb(on_rec_status);
        s_cb_attached = true;
        request_list();
    }
    if (dirty & (1u << UI_STATE_GROUP_SYSTEM))   player_refresh_progress();
    if (dirty & (1u << UI_STATE_GROUP_SETTINGS)) {
        if (s_title) lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_RECORDING_LIST_TITLE));
    }
}

static void on_leave(void)
{
    ui_svc_recording_set_cb(NULL);
    s_cb_attached = false;
}

static void on_destroy(void)
{
    /* The delete-confirm popup lives on lv_layer_top and outlives s_screen;
     * dismiss it so a stray dialog (and its callback into this gone page)
     * can't linger if we're torn down by an external route change. Idempotent. */
    ui_comp_popup_dismiss();
    ui_svc_recording_set_cb(NULL);
    s_cb_attached = false;
    ui_svc_recording_play_stop();
    if (s_screen) {
        lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_screen);
        s_screen = NULL; s_title = NULL; s_count = NULL; s_list = NULL;
        s_player = NULL; s_pl_name = NULL; s_pl_bar = NULL;
        s_pl_cur = NULL; s_pl_total = NULL; s_pl_playicon = NULL;
        s_pl_ai = NULL;
    }
    s_row_cnt = 0; s_player_id = -1; s_selected_id = -1; s_shown_sec = -1;
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    if (s_player && !lv_obj_has_flag(s_player, LV_OBJ_FLAG_HIDDEN)) {
        ui_svc_recording_play_stop();
        player_hide();
        return;
    }
    ui_route_pop();
}

static void gesture_cb(lv_event_t *e)
{
    (void)e;
    if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());
        if (s_player && !lv_obj_has_flag(s_player, LV_OBJ_FLAG_HIDDEN)) {
            ui_svc_recording_play_stop();
            player_hide();
            return;
        }
        ui_route_pop();
    }
}

const ui_page_entry_t ui_page_recording_list_entry = {
    .id = UI_PAGE_RECORDING_LIST,
    .name = "recording_list",
    .lifecycle = { on_create, on_enter, on_leave, on_destroy }
};
