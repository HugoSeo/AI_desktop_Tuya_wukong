/**
 * @file ui_page_files.c
 * @brief File browser page — walks the UI_FS_MOUNT volume (the whole card,
 *        including user files alongside tuyaos/) one level at a time.
 *        Directories drill in; regular files open a read-only info popup.
 *        Listing runs off the UI thread via ui_fs_list_async (see ui_svc_fs.h);
 *        results land here on the UI thread, latest-wins by seq.
 *
 * Self-contained per RULES §3: rows are built inline (the shared ui_comp_list
 * widget can render neither LV_SYMBOL icons nor per-row user_data, both of which
 * this page needs).
 */

#include "lvgl.h"
#include "ui_route.h"
#include "ui_theme.h"
#include "ui_adaptive.h"
#include "ui_i18n.h"
#include "ui_state.h"
#include "ui_comp_btn.h"
#include "ui_comp_popup.h"
#include "ui_svc_fs.h"
#include "ui_page_ids.h"
#include "tal_memory.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

LV_IMG_DECLARE(icon_back_24_24);

#define FILES_STATUSBAR_H  32
#define FILES_TITLEBAR_H   48
#define FILES_ITEM_H       56
#define FILES_BG_NORMAL    0x353740
#define FILES_HINT         0xB8BDDE
#define FILES_ICON_COLOR   0xF3E55D
#define FILES_REL_MAX      256

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_title  = NULL;
static lv_obj_t *s_list   = NULL;

static char           s_cur_rel[FILES_REL_MAX] = {0};   /* "" = root */
static uint32_t       s_seq = 0;
static ui_fs_entry_t *s_entries = NULL;                 /* snapshot for row clicks */
static uint16_t       s_count = 0;
static int            s_pending_del = -1;

static void on_create(void *parent);
static void on_enter(uint32_t dirty);
static void on_leave(void);
static void on_destroy(void);

static void back_cb(lv_event_t *e);
static void gesture_cb(lv_event_t *e);
static void row_cb(lv_event_t *e);
static void file_info_cb(bool confirmed);

static void load_dir(const char *rel);
static void on_listed(OPERATE_RET rt, const ui_fs_entry_t *entries,
                      uint16_t count, uint32_t seq, void *user);
static void make_row(uint16_t i);
static void update_title(void);
static void show_message(ui_i18n_key_t key);
static void show_file_info(const ui_fs_entry_t *e);
static void go_back(void);

/* ---------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------*/
static bool ci_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if ((*a | 0x20) != (*b | 0x20)) return false;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

static const char *pick_symbol(const ui_fs_entry_t *e)
{
    if (e->is_dir) return LV_SYMBOL_DIRECTORY;
    const char *dot = strrchr(e->name, '.');
    if (dot) {
        const char *x = dot + 1;
        if (ci_eq(x, "jpg") || ci_eq(x, "jpeg") || ci_eq(x, "png") ||
            ci_eq(x, "bmp") || ci_eq(x, "gif")) {
            return LV_SYMBOL_IMAGE;
        }
        if (ci_eq(x, "mp3") || ci_eq(x, "wav") || ci_eq(x, "opus") ||
            ci_eq(x, "aac") || ci_eq(x, "m4a")) {
            return LV_SYMBOL_AUDIO;
        }
        if (ci_eq(x, "mp4") || ci_eq(x, "avi") || ci_eq(x, "mov")) {
            return LV_SYMBOL_VIDEO;
        }
    }
    return LV_SYMBOL_FILE;
}

/* One decimal place, no float (printf %f is costly on this target). */
static void human_size(uint64_t bytes, char *buf, size_t len)
{
    if (bytes < 1024ULL) {
        snprintf(buf, len, "%u B", (unsigned)bytes);
    } else if (bytes < 1024ULL * 1024) {
        snprintf(buf, len, "%u.%u KB",
                 (unsigned)(bytes / 1024),
                 (unsigned)((bytes % 1024) * 10 / 1024));
    } else if (bytes < 1024ULL * 1024 * 1024) {
        snprintf(buf, len, "%u.%u MB",
                 (unsigned)(bytes / (1024ULL * 1024)),
                 (unsigned)((bytes % (1024ULL * 1024)) * 10 / (1024ULL * 1024)));
    } else {
        snprintf(buf, len, "%u.%u GB",
                 (unsigned)(bytes / (1024ULL * 1024 * 1024)),
                 (unsigned)((bytes % (1024ULL * 1024 * 1024)) * 10 / (1024ULL * 1024 * 1024)));
    }
}

static void update_title(void)
{
    if (!s_title) return;
    const char *t;
    if (s_cur_rel[0] == '\0') {
        t = ui_i18n_text(UI_TEXT_APP_FILES);
    } else {
        const char *slash = strrchr(s_cur_rel, '/');
        t = slash ? slash + 1 : s_cur_rel;   /* current dir's leaf name */
    }
    lv_label_set_text(s_title, t);
}

static void show_message(ui_i18n_key_t key)
{
    if (!s_list) return;
    lv_obj_clean(s_list);
    lv_obj_t *lbl = lv_label_create(s_list);
    lv_label_set_text(lbl, ui_i18n_text(key));
    lv_obj_set_style_text_color(lbl, lv_color_hex(FILES_HINT), 0);
    lv_obj_center(lbl);
}

/* ---------------------------------------------------------------------------
 * Listing
 * -------------------------------------------------------------------------*/
static void load_dir(const char *rel)
{
    strncpy(s_cur_rel, rel, sizeof(s_cur_rel) - 1);
    s_cur_rel[sizeof(s_cur_rel) - 1] = '\0';
    s_seq++;

    update_title();
    show_message(UI_TEXT_LOADING);

    if (ui_fs_list_async(s_cur_rel, s_seq, on_listed, NULL) != OPRT_OK) {
        show_message(UI_TEXT_FILES_FS_ERROR);
    }
}

static void on_listed(OPERATE_RET rt, const ui_fs_entry_t *entries,
                      uint16_t count, uint32_t seq, void *user)
{
    (void)user;
    if (s_screen == NULL) return;   /* page destroyed before result landed */
    if (seq != s_seq) return;       /* stale (user navigated on) — drop it */

    if (s_list) lv_obj_clean(s_list);
    if (s_entries) { tal_free(s_entries); s_entries = NULL; }
    s_count = 0;

    if (rt != OPRT_OK) {
        show_message(UI_TEXT_FILES_FS_ERROR);
        return;
    }
    if (count == 0) {
        show_message(UI_TEXT_FILES_EMPTY);
        return;
    }

    /* Snapshot so row_cb can resolve name/type/size after the service buffer
     * is freed (the callback contract says entries are valid only here). */
    s_entries = (ui_fs_entry_t *)tal_malloc(sizeof(ui_fs_entry_t) * count);
    if (s_entries == NULL) {
        show_message(UI_TEXT_FILES_FS_ERROR);
        return;
    }
    memcpy(s_entries, entries, sizeof(ui_fs_entry_t) * count);
    s_count = count;

    for (uint16_t i = 0; i < count; i++) {
        make_row(i);
    }
}

static void make_row(uint16_t i)
{
    const ui_fs_entry_t *e = &s_entries[i];

    lv_obj_t *item = lv_obj_create(s_list);
    lv_obj_remove_style_all(item);
    lv_obj_set_size(item, LV_PCT(100), ui_adapt(FILES_ITEM_H));
    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(item, ui_adapt(16), 0);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(item, lv_color_hex(FILES_BG_NORMAL), 0);
    lv_obj_set_style_pad_hor(item, ui_adapt(14), 0);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(item, row_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

    /* Leading type icon — LVGL built-in symbol rendered with Montserrat (the
     * default CJK font has no FontAwesome glyphs; see RULES "Default Font"). */
    lv_obj_t *icon = lv_label_create(item);
    lv_label_set_text(icon, pick_symbol(e));
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(FILES_ICON_COLOR), 0);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);

    /* Fixed name width (not LV_PCT) to dodge the LONG_DOT + percent pitfall
     * (RULES §9): icon column + a right gutter for the size/chevron. */
    lv_coord_t text_x = ui_adapt(40);
    lv_coord_t name_w = ui_adapt_screen_w() - ui_adapt(14) * 2 - text_x - ui_adapt(24);
    if (name_w < ui_adapt(60)) name_w = ui_adapt(60);

    lv_obj_t *nm = lv_label_create(item);
    lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
    lv_obj_set_width(nm, name_w);
    lv_label_set_text(nm, e->name);
    lv_obj_set_style_text_color(nm, lv_color_white(), 0);
    lv_obj_align(nm, LV_ALIGN_LEFT_MID, text_x, 0);

    /* Directories get a chevron; files show the name only — the file size lives
     * in the info popup opened on tap, so there is no per-row size subtitle. */
    if (e->is_dir) {
        lv_obj_t *chev = lv_label_create(item);
        lv_label_set_text(chev, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_font(chev, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(chev, lv_color_hex(FILES_HINT), 0);
        lv_obj_align(chev, LV_ALIGN_RIGHT_MID, 0, 0);
    }
}

static void show_file_info(const ui_fs_entry_t *e)
{
    char sz[32];
    human_size(e->size, sz, sizeof(sz));

    /* UI_FS_MOUNT is a compile-time macro that doesn't even exist when the
     * storage backend is NONE; use the runtime accessor instead (NULL in
     * that case — this page won't normally be reachable there since listing
     * is unsupported too, but keep the info popup safe regardless). */
    const char *mount = ui_fs_mount();
    char path[FILES_REL_MAX];
    if (mount == NULL) {
        path[0] = '\0';
    } else if (s_cur_rel[0] == '\0') {
        snprintf(path, sizeof(path), "%s/%s", mount, e->name);
    } else {
        snprintf(path, sizeof(path), "%s/%s/%s", mount, s_cur_rel, e->name);
    }

    char msg[FILES_REL_MAX + 96];
    snprintf(msg, sizeof(msg), "%s: %s\n%s: %s",
             ui_i18n_text(UI_TEXT_FILES_INFO_SIZE), sz,
             ui_i18n_text(UI_TEXT_FILES_INFO_PATH), path);

    ui_comp_popup_show(UI_COMP_POPUP_INFO_DELETE, e->name, msg, file_info_cb);
}

/* ---------------------------------------------------------------------------
 * Navigation
 * -------------------------------------------------------------------------*/
static void go_back(void)
{
    if (s_cur_rel[0] == '\0') {
        ui_route_pop();   /* at root → leave the page (back to pulldown) */
        return;
    }
    /* Drop the last path segment → parent directory. */
    char parent[FILES_REL_MAX];
    strncpy(parent, s_cur_rel, sizeof(parent) - 1);
    parent[sizeof(parent) - 1] = '\0';
    char *slash = strrchr(parent, '/');
    if (slash) *slash = '\0';
    else        parent[0] = '\0';
    load_dir(parent);
}

static void row_cb(lv_event_t *e)
{
    uint16_t i = (uint16_t)(uintptr_t)lv_event_get_user_data(e);
    if (s_entries == NULL || i >= s_count) return;
    const ui_fs_entry_t *ent = &s_entries[i];

    if (ent->is_dir) {
        char child[FILES_REL_MAX];
        if (s_cur_rel[0] == '\0') {
            snprintf(child, sizeof(child), "%s", ent->name);
        } else {
            snprintf(child, sizeof(child), "%s/%s", s_cur_rel, ent->name);
        }
        load_dir(child);
    } else {
        s_pending_del = i;
        show_file_info(ent);
    }
}

static void file_info_cb(bool confirmed)
{
    int i = s_pending_del;
    s_pending_del = -1;

    if (!confirmed) return;
    if (s_screen == NULL) return;
    if (s_entries == NULL || i < 0 || i >= s_count) return;

    char name[UI_FS_NAME_MAX];
    strncpy(name, s_entries[i].name, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';

    if (ui_fs_remove(s_cur_rel, name) != OPRT_OK) {
        ui_comp_popup_toast(ui_i18n_text(UI_TEXT_FILES_DEL_FAIL), 1500);
        return;
    }
    load_dir(s_cur_rel);
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
        lv_indev_wait_release(lv_indev_get_act());   /* don't leak click below */
        go_back();
    }
}

/* ---------------------------------------------------------------------------
 * Lifecycle
 * -------------------------------------------------------------------------*/
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
    lv_obj_set_style_pad_top(s_screen, ui_adapt(FILES_STATUSBAR_H), 0);
    lv_obj_add_event_cb(s_screen, gesture_cb, LV_EVENT_GESTURE, NULL);

    lv_obj_t *titlebar = lv_obj_create(s_screen);
    lv_obj_remove_style_all(titlebar);
    lv_obj_set_size(titlebar, LV_PCT(100), ui_adapt(FILES_TITLEBAR_H));
    lv_obj_set_style_bg_opa(titlebar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(titlebar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = ui_comp_btn_icon_create(titlebar, &icon_back_24_24, back_cb);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, ui_adapt(8), 0);

    s_title = lv_label_create(titlebar);
    lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_APP_FILES));
    lv_obj_set_style_text_color(s_title, UI_COLOR_TEXT, 0);
    lv_obj_align(s_title, LV_ALIGN_CENTER, 0, 0);

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

    load_dir("");
}

static void on_enter(uint32_t dirty)
{
    if (dirty & (1u << UI_STATE_GROUP_SETTINGS)) {
        /* Re-list so the title + folder/empty i18n strings pick up the new
         * language (file names themselves are not translated). */
        load_dir(s_cur_rel);
    }
}

static void on_leave(void)
{
}

static void on_destroy(void)
{
    /* The info popup lives on lv_layer_top and outlives s_screen; dismiss it so
     * a stray dialog can't linger if we're torn down by a route change. */
    ui_comp_popup_dismiss();

    if (s_screen) {
        lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_screen);
        s_screen = NULL;
        s_title  = NULL;
        s_list   = NULL;
    }
    if (s_entries) { tal_free(s_entries); s_entries = NULL; }
    s_count = 0;
    s_cur_rel[0] = '\0';
    s_pending_del = -1;
}

const ui_page_entry_t ui_page_files_entry = {
    .id = UI_PAGE_FILES,
    .name = "files",
    .lifecycle = { on_create, on_enter, on_leave, on_destroy }
};
