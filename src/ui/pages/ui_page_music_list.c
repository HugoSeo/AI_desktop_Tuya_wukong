#include "lvgl.h"
#include "ui_route.h"
#include "ui_theme.h"
#include "ui_adaptive.h"
#include "ui_i18n.h"
#include "ui_state.h"
#include "ui_comp_btn.h"
#include "ui_svc_music.h"     /* music control — no business SDK headers in pages */
#include "ui_page_ids.h"
#include "ty_cJSON.h"
#include "tal_memory.h"
#include "uni_log.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

LV_IMG_DECLARE(icon_back_24_24);

#define MLIST_STATUSBAR_H  32   /* global statusbar band (ui_comp_statusbar) */
#define MLIST_TITLEBAR_H   48
#define MLIST_ITEM_H       72
#define MLIST_BG_NORMAL    0x353740
#define MLIST_BG_CURRENT   0x2E4A6E   /* primary-tinted dark: clearly apart from BG_NORMAL */
#define MLIST_TEXT_CURRENT 0xF3E55D
#define MLIST_TEXT_ARTIST  0xB8BDDE

/* music source classification — reserved for future local(SD) support. */
typedef enum {
    MLIST_SRC_NETWORK = 0,
    MLIST_SRC_LOCAL,           /* not produced yet; structure reserved */
} mlist_src_t;

/* mirrors PLAYBACK_PLAYLIST_HARD_CAP in the backend */
#define MLIST_MAX_ROWS     128

static lv_obj_t *s_screen   = NULL;
static lv_obj_t *s_title    = NULL;
static lv_obj_t *s_counter  = NULL;   /* "current/total" indicator (titlebar right) */
static lv_obj_t *s_list     = NULL;
static bool      s_cb_attached = false;   /* service callback registered (cleared by on_leave) */

/* Row registry for incremental updates: moving the current-row highlight only
 * restyles two rows instead of destroying/recreating the whole list. */
/* One entry per rendered row. The four fields used to be four parallel arrays;
 * merging them into one struct means a single allocation instead of four.
 * Allocated in on_create, released in on_destroy — NULL means the allocation
 * failed, and s_row_cnt then stays 0 so no row is ever registered. */
typedef struct {
    lv_obj_t *row;      /* the row container */
    lv_obj_t *song;     /* song-title label */
    lv_obj_t *state;    /* play/pause/loading glyph */
    int       id;       /* playlist id, for row_index_of */
} mlist_row_t;

static mlist_row_t *s_rows;
static int          s_row_cnt = 0;
static int       s_shown_cur_id  = -1;     /* row currently highlighted */
static bool      s_shown_playing = false;  /* icon state on that row */
static int       s_missing_cur_id = -1;    /* current id a rebuild failed to resolve (don't rebuild again for it) */
static int       s_pending_play_id = -1;   /* user-clicked row awaiting backend confirmation (intent latch) */

static void on_create(void *parent);
static void on_enter(uint32_t dirty);
static void on_leave(void);
static void on_destroy(void);
static void back_cb(lv_event_t *e);
static void gesture_cb(lv_event_t *e);
static void row_cb(lv_event_t *e);
static void on_music_status(const ui_music_status_t *st);
static void request_list(void);
static void build_rows(ty_cJSON *list);
static void on_list_data(ty_cJSON *list);
static void refresh_counter(void);
static int  row_index_of(int id);
static void apply_row_style(int idx, bool is_current, bool playing);
static void update_current_row(int cur_id, bool playing);

/* Classify an item's source. Network items carry a channel_code; local (future)
 * would be a bare file path. Currently always network. */
static mlist_src_t classify_source(ty_cJSON *row)
{
    ty_cJSON *ch = ty_cJSON_GetObjectItem(row, "channel_code");
    if (ch && ty_cJSON_IsString(ch) && ch->valuestring[0] != '\0') {
        return MLIST_SRC_NETWORK;
    }
    return MLIST_SRC_NETWORK;   /* reserved: classify local later */
}

static void make_row(int id, const char *song, const char *artist,
                     bool is_current, bool is_playing)
{
    /* Registry full: don't create the row at all. An unregistered row could
     * never be resolved by row_index_of, so the current id landing on it would
     * make on_music_status fall back to a full rebuild on every player event. */
    if (s_rows == NULL || s_row_cnt >= MLIST_MAX_ROWS) {
        return;
    }

    lv_obj_t *item = lv_obj_create(s_list);
    lv_obj_remove_style_all(item);
    lv_obj_set_size(item, LV_PCT(100), ui_adapt(MLIST_ITEM_H));
    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(item, ui_adapt(16), 0);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(item,
        lv_color_hex(is_current ? MLIST_BG_CURRENT : MLIST_BG_NORMAL), 0);
    lv_obj_set_style_pad_hor(item, ui_adapt(16), 0);
    lv_obj_set_style_pad_ver(item, ui_adapt(12), 0);
    lv_obj_set_style_border_width(item, 0, 0);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(item, row_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)id);

    /* LV_LABEL_LONG_DOT, not SCROLL_CIRCULAR: marquee animations redraw
     * continuously and starve the audio threads (no marquee anywhere on
     * the music pages by design). */
    lv_obj_t *song_lbl = lv_label_create(item);
    lv_label_set_long_mode(song_lbl, LV_LABEL_LONG_DOT);
    lv_label_set_text(song_lbl,
        (song && song[0]) ? song : ui_i18n_text(UI_TEXT_MUSIC_UNKNOWN_SONG));
    lv_obj_set_width(song_lbl, ui_adapt(200));
    lv_obj_align(song_lbl, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_color(song_lbl,
        is_current ? lv_color_hex(MLIST_TEXT_CURRENT) : lv_color_white(), 0);

    lv_obj_t *artist_lbl = lv_label_create(item);
    lv_label_set_long_mode(artist_lbl, LV_LABEL_LONG_DOT);
    lv_label_set_text(artist_lbl,
        (artist && artist[0]) ? artist : ui_i18n_text(UI_TEXT_MUSIC_UNKNOWN_ARTIST));
    lv_obj_set_width(artist_lbl, ui_adapt(200));
    lv_obj_align(artist_lbl, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_text_color(artist_lbl, lv_color_hex(MLIST_TEXT_ARTIST), 0);

    /* State symbol exists on every row (hidden unless current) so the
     * highlight can move between rows without rebuilding the list.
     * A click-to-play still in flight shows REFRESH (loading), not a
     * misleading paused/playing glyph (static symbol on purpose — a spinner
     * would redraw continuously through the URL-refresh window, RULES §9). */
    lv_obj_t *state = lv_label_create(item);
    lv_label_set_text(state, (id == s_pending_play_id) ? LV_SYMBOL_REFRESH
                             : (is_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY));
    lv_obj_set_style_text_font(state, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(state, lv_color_white(), 0);
    lv_obj_align(state, LV_ALIGN_RIGHT_MID, 0, 0);
    if (!is_current) {
        lv_obj_add_flag(state, LV_OBJ_FLAG_HIDDEN);
    }

    s_rows[s_row_cnt].row   = item;
    s_rows[s_row_cnt].song  = song_lbl;
    s_rows[s_row_cnt].state = state;
    s_rows[s_row_cnt].id    = id;
    s_row_cnt++;
}

/* Clear all rows and show a single centred hint (loading / empty). */
static void show_placeholder(ui_i18n_key_t key)
{
    lv_obj_clean(s_list);
    s_row_cnt = 0;
    s_shown_cur_id = -1;
    s_shown_playing = false;

    lv_obj_t *lbl = lv_label_create(s_list);
    lv_label_set_text(lbl, ui_i18n_text(key));
    lv_obj_set_style_text_color(lbl, lv_color_hex(MLIST_TEXT_ARTIST), 0);
    lv_obj_center(lbl);

    refresh_counter();   /* no rows: hides the current/total indicator */
}

/* Kick an async playlist fetch (WORKQ fetch → on_list_data on the UI thread).
 * The page renders immediately; rows arrive when the data does. With rows
 * already on screen they stay visible until the fresh data replaces them
 * (no flicker); only a bare first load shows the loading hint. */
static void request_list(void)
{
    if (!s_list) {
        return;
    }
    if (s_row_cnt == 0) {
        show_placeholder(UI_TEXT_LOADING);
    }
    ui_svc_music_list_async(on_list_data);
}

/* Build the row widgets from a fetched playlist (UI thread, data owned by
 * caller). Split from the fetch so the playback-mutex wait never happens on
 * the UI thread (see ui_svc_music_list_async). */
static void build_rows(ty_cJSON *list)
{
    lv_obj_clean(s_list);
    s_row_cnt = 0;
    s_shown_cur_id = -1;
    s_shown_playing = false;

    if (list == NULL || !ty_cJSON_IsArray(list) || ty_cJSON_GetArraySize(list) == 0) {
        show_placeholder(UI_TEXT_MUSIC_EMPTY);
        return;
    }

    int cur_id = ui_svc_music_current_id();
    ui_music_status_t st;
    ui_svc_music_get_status(&st);
    bool playing = (st.state == AI_PLAYER_PLAYING);

    /* A clicked play is still in flight: highlight the user's intent, not the
     * old track that current_play_id keeps reporting until the switch lands. */
    if (s_pending_play_id >= 0 && !playing) {
        cur_id = s_pending_play_id;
    }

    int n = ty_cJSON_GetArraySize(list);
    for (int i = 0; i < n; i++) {
        ty_cJSON *row = ty_cJSON_GetArrayItem(list, i);
        if (!row || !ty_cJSON_IsObject(row)) {
            continue;
        }
        (void)classify_source(row);   /* reserved multi-source hook */
        ty_cJSON *j_id = ty_cJSON_GetObjectItem(row, "id");
        ty_cJSON *j_name = ty_cJSON_GetObjectItem(row, "song_name");
        ty_cJSON *j_artist = ty_cJSON_GetObjectItem(row, "artist");
        int id = (j_id && ty_cJSON_IsNumber(j_id)) ? j_id->valueint : -1;
        const char *song = (j_name && ty_cJSON_IsString(j_name)) ? j_name->valuestring : "";
        const char *artist = (j_artist && ty_cJSON_IsString(j_artist)) ? j_artist->valuestring : "";
        make_row(id, song, artist, (id >= 0 && id == cur_id), playing);
    }

    s_shown_cur_id  = cur_id;
    s_shown_playing = playing;
    refresh_counter();

    /* Fresh data resolved a previously-missing current id: release the latch
     * so on_music_status goes back to the cheap incremental path. */
    if (s_missing_cur_id >= 0 && row_index_of(s_missing_cur_id) >= 0) {
        s_missing_cur_id = -1;
    }

    /* Anchor the view on the current track. lv_obj_clean above reset the
     * scroll position to the top, so without this the user would have to
     * hunt for the playing row after every (re)build. Resolve the pending
     * flex layout first — the freshly created rows have no coords yet. */
    int cur_idx = row_index_of(cur_id);
    if (cur_idx >= 0) {
        lv_obj_update_layout(s_list);
        lv_obj_scroll_to_view(s_rows[cur_idx].row, LV_ANIM_OFF);
    }
}

/* UI-thread delivery from ui_svc_music_list_async (we own @list). */
static void on_list_data(ty_cJSON *list)
{
    if (!s_list) {            /* page destroyed while the fetch was in flight */
        if (list) {
            ty_cJSON_Delete(list);
        }
        return;
    }
    build_rows(list);
    if (list) {
        ty_cJSON_Delete(list);
    }
}

static int row_index_of(int id)
{
    if (id < 0) {
        return -1;
    }
    for (int i = 0; i < s_row_cnt; i++) {
        if (s_rows[i].id == id) {
            return i;
        }
    }
    return -1;
}

static void apply_row_style(int idx, bool is_current, bool playing)
{
    if (idx < 0 || idx >= s_row_cnt) {
        return;
    }
    lv_obj_set_style_bg_color(s_rows[idx].row,
        lv_color_hex(is_current ? MLIST_BG_CURRENT : MLIST_BG_NORMAL), 0);
    lv_obj_set_style_text_color(s_rows[idx].song,
        is_current ? lv_color_hex(MLIST_TEXT_CURRENT) : lv_color_white(), 0);
    if (is_current) {
        /* In-flight click-to-play shows a loading glyph until the backend
         * confirms (intent latch releases in on_music_status). */
        lv_label_set_text(s_rows[idx].state,
                          (s_rows[idx].id == s_pending_play_id) ? LV_SYMBOL_REFRESH
                          : (playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY));
        lv_obj_clear_flag(s_rows[idx].state, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_rows[idx].state, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Show "current/total" when rows exist; hidden while loading / empty.
 * "-/N" when the current track has no row (e.g. cloud-browse playback). */
static void refresh_counter(void)
{
    if (!s_counter) {
        return;
    }
    if (s_row_cnt == 0) {
        lv_obj_add_flag(s_counter, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    char buf[16];
    int idx = row_index_of(s_shown_cur_id);
    if (idx >= 0) {
        snprintf(buf, sizeof(buf), "%d/%d", idx + 1, s_row_cnt);
    } else {
        snprintf(buf, sizeof(buf), "-/%d", s_row_cnt);
    }
    lv_label_set_text(s_counter, buf);
    lv_obj_clear_flag(s_counter, LV_OBJ_FLAG_HIDDEN);
}

/* Move the current-row highlight / play state incrementally (no rebuild). */
static void update_current_row(int cur_id, bool playing)
{
    if (cur_id == s_shown_cur_id && playing == s_shown_playing) {
        return;
    }
    int old_idx = row_index_of(s_shown_cur_id);
    int new_idx = row_index_of(cur_id);
    if (old_idx >= 0 && old_idx != new_idx) {
        apply_row_style(old_idx, false, false);
    }
    apply_row_style(new_idx, true, playing);
    s_shown_cur_id  = cur_id;
    s_shown_playing = playing;
    refresh_counter();
}

static void on_create(void *parent)
{
    (void)parent;

    /* Row registry lives only as long as the page. Failure is non-fatal:
     * make_row bails out, so the list stays empty (see the declaration). */
    s_rows = tal_malloc(sizeof(*s_rows) * MLIST_MAX_ROWS);
    if (s_rows) {
        memset(s_rows, 0, sizeof(*s_rows) * MLIST_MAX_ROWS);
    } else {
        PR_ERR("music_list: row registry alloc %d bytes failed, list disabled",
               (int)(sizeof(*s_rows) * MLIST_MAX_ROWS));
    }

    s_screen = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_screen, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    /* Clear the global statusbar band so the title bar renders below it. */
    lv_obj_set_style_pad_top(s_screen, ui_adapt(MLIST_STATUSBAR_H), 0);
    lv_obj_add_event_cb(s_screen, gesture_cb, LV_EVENT_GESTURE, NULL);   /* swipe-right = back */

    lv_obj_t *titlebar = lv_obj_create(s_screen);
    lv_obj_remove_style_all(titlebar);
    lv_obj_set_size(titlebar, LV_PCT(100), ui_adapt(MLIST_TITLEBAR_H));
    lv_obj_set_style_bg_opa(titlebar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(titlebar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = ui_comp_btn_icon_create(titlebar, &icon_back_24_24, back_cb);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, ui_adapt(8), 0);

    s_title = lv_label_create(titlebar);
    lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_MUSIC_LIST_TITLE));
    lv_obj_set_style_text_color(s_title, UI_COLOR_TEXT, 0);
    lv_obj_align(s_title, LV_ALIGN_CENTER, 0, 0);

    /* "current/total" indicator — hidden until rows actually exist (stays
     * hidden through the loading placeholder and the empty state). */
    s_counter = lv_label_create(titlebar);
    lv_obj_set_style_text_color(s_counter, lv_color_hex(MLIST_TEXT_ARTIST), 0);
    lv_obj_align(s_counter, LV_ALIGN_RIGHT_MID, -ui_adapt(12), 0);
    lv_obj_add_flag(s_counter, LV_OBJ_FLAG_HIDDEN);

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

    request_list();   /* page renders now; rows arrive via on_list_data */
    ui_svc_music_set_cb(on_music_status);
    s_cb_attached = true;
}

static void on_enter(uint32_t dirty)
{
    bool requested = false;
    /* Returning from a covering page (pulldown overlay): on_leave cleared the
     * single service callback slot, so re-register and refresh to catch up. */
    if (!s_cb_attached) {
        ui_svc_music_set_cb(on_music_status);
        s_cb_attached = true;
        request_list();
        requested = true;
    }
    if (dirty & (1u << UI_STATE_GROUP_SETTINGS)) {
        if (s_title) {
            lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_MUSIC_LIST_TITLE));
        }
        if (!requested) {
            request_list();   /* re-resolve row i18n (unknown-song/artist labels) */
        }
    }
}

static void on_leave(void)
{
    ui_svc_music_set_cb(NULL);
    s_cb_attached = false;
}

static void on_destroy(void)
{
    ui_svc_music_set_cb(NULL);
    s_cb_attached = false;
    if (s_screen) {
        lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_screen);
        s_screen  = NULL;
        s_title   = NULL;
        s_counter = NULL;
        s_list    = NULL;
    }
    /* Rows died with s_list's subtree above; drop the registry and its count in
     * the same step so no cached lv_obj_* pointer outlives them (RULES §7.1). */
    tal_free(s_rows);
    s_rows = NULL;
    s_row_cnt = 0;
    s_shown_cur_id = -1;
    s_shown_playing = false;
    s_missing_cur_id = -1;
    s_pending_play_id = -1;
}

static void back_cb(lv_event_t *e)   { (void)e; ui_route_pop(); }

/* Swipe right anywhere on the page = go back (same as the back button). */
static void gesture_cb(lv_event_t *e)
{
    (void)e;
    if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());   /* swallow rest of touch so the release doesn't click the page below */
        ui_route_pop();
    }
}

static void row_cb(lv_event_t *e)
{
    int id = (int)(uintptr_t)lv_event_get_user_data(e);

    if (id == s_pending_play_id) {
        return;   /* switch to this row already in flight; swallow repeat taps */
    }

    /* Tapping the CURRENT row toggles play/pause (standard music-app
     * convention). It must not go through play_id, which restarts the track
     * from scratch (URL refresh + replay) — that's what made the state icon
     * flicker and "pause" silently turn into a replay. */
    if (id == ui_svc_music_current_id()) {
        ui_music_status_t st;
        ui_svc_music_get_status(&st);
        ui_svc_music_play_pause();
        /* Optimistic flip; the PAUSED/PLAYING event confirms it (deduped). */
        update_current_row(id, st.state != AI_PLAYER_PLAYING);
        return;
    }

    ui_svc_music_play_id(id);
    /* Instant feedback: the backend only confirms via events after the play
     * URL has been refreshed over MQTT (seconds on first play of an item).
     * Move the highlight now and latch the intent: until the backend either
     * confirms this id or actually starts something else, the old track's
     * teardown events must not yank the highlight back (see on_music_status). */
    s_pending_play_id = id;
    update_current_row(id, false);
}

/* UI-thread callback from ui_svc_music: keep current-row highlight/state live.
 * Incremental: a full rebuild (250+ object churn) on every player event used
 * to stutter audio; restyling two rows is enough. Only fall back to a rebuild
 * when the current id has no row — the list itself changed (cloud push). */
static void on_music_status(const ui_music_status_t *st)
{
    if (!s_screen || ui_route_current() != UI_PAGE_MUSIC_LIST) {
        return;
    }
    int cur = ui_svc_music_current_id();
    bool playing = (st->state == AI_PLAYER_PLAYING);

    /* Intent latch: while a clicked play is in flight, current_play_id still
     * holds the OLD track until the new one actually starts (URL refresh can
     * take seconds), and stopping the old player fires STOPPED events first.
     * Without this guard the highlight bounces clicked → old → clicked. */
    if (s_pending_play_id >= 0) {
        if (cur == s_pending_play_id) {
            s_pending_play_id = -1;   /* backend confirmed the clicked track */
        } else if (playing) {
            s_pending_play_id = -1;   /* something else genuinely started: accept reality */
        } else {
            return;                   /* old-track teardown event: keep the clicked highlight */
        }
    }

    if (cur >= 0 && row_index_of(cur) < 0) {
        /* Latch ids we've already requested a refresh for (empty list while a
         * cloud-browse track plays): without it every player event would
         * trigger a fetch + full rebuild — the exact churn the incremental
         * path exists to avoid. Latched before the request since the result
         * is async; build_rows releases the latch once the id resolves. */
        if (cur == s_missing_cur_id) {
            return;
        }
        s_missing_cur_id = cur;
        request_list();
        return;
    }
    update_current_row(cur, playing);
}

const ui_page_entry_t ui_page_music_list_entry = {
    .id = UI_PAGE_MUSIC_LIST,
    .name = "music_list",
    .lifecycle = { on_create, on_enter, on_leave, on_destroy }
};
