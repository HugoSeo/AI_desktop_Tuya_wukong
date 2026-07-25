/**
 * @file ui_music_list.c
 * @brief Music playlist screen for T5AI_BOARD (320x480)
 *
 * Reachable from the main music screen "列表" button. Renders the
 * locally-cached playlist (returned by wukong_playback_playlist_list) as a
 * vertically scrolling list, marks the current track with a yellow song name +
 * a slightly brighter card and a play/pause symbol on the right, and offers a
 * per-row "删除" button that calls wukong_playback_playlist_remove.
 *
 * The status symbol uses LVGL's built-in glyphs (LV_SYMBOL_PLAY / PAUSE) at
 * lv_font_montserrat_24 instead of dedicated PNGs to keep flash usage minimal.
 *
 * Screen lifecycle follows the view convention (lazy create + per-visit
 * subscribe/unsubscribe), mirroring ui_call.c. The event callback drives
 * UI updates synchronously - same threading model the rest of view uses.
 *
 * @version 1.0
 * @date 2026-05-21
 * @copyright Copyright (c) Tuya Inc.
 */
#include <string.h>
#include <stdint.h>
#include "ui_common.h"
#include "ui_music_list.h"
#include "base_event.h"
#include "ty_cJSON.h"
#include "wukong_playback_ctrl.h"
#include "wukong_audio_player.h"
#include "wukong_ai_skills.h"
#include "svc_ai_player.h"

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define MUSIC_LIST_TITLE_TEXT       "所有歌曲"
#define MUSIC_LIST_EVT_SUBSCRIBER   "ui_music_list"

#define MUSIC_LIST_ITEM_W           280
#define MUSIC_LIST_ITEM_H           72
#define MUSIC_LIST_ITEM_RADIUS      16
#define MUSIC_LIST_ITEM_PAD_H       16
#define MUSIC_LIST_ITEM_PAD_V       12
#define MUSIC_LIST_LABEL_W          120
#define MUSIC_LIST_SONG_H           24
#define MUSIC_LIST_ARTIST_Y         28
#define MUSIC_LIST_ARTIST_H         22
#define MUSIC_LIST_DELETE_W         52
#define MUSIC_LIST_DELETE_H         28
#define MUSIC_LIST_DELETE_RADIUS    14
#define MUSIC_LIST_STATE_ICON_OFF   (-76)

#define MUSIC_LIST_BG_NORMAL        0x353740
#define MUSIC_LIST_BG_CURRENT       0x3A3B46
#define MUSIC_LIST_TEXT_CURRENT     0xF3E55D
#define MUSIC_LIST_TEXT_ARTIST      0xB8BDDE
#define MUSIC_LIST_DELETE_BG        0x4B4D59

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef struct {
    lv_obj_t *scr;
    lv_obj_t *title_bar;
    lv_obj_t *title_lbl;
    lv_obj_t *back_btn;
    lv_obj_t *list_cont;
    BOOL_T    evt_subscribed;
} MUSIC_LIST_UI_T;

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC MUSIC_LIST_UI_T s_music_list = {0};

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC VOID_T __music_list_back_cb(lv_event_t *e);
STATIC VOID_T __music_list_item_clicked_cb(lv_event_t *e);
STATIC VOID_T __music_list_delete_cb(lv_event_t *e);
STATIC VOID_T __music_list_make_item(INT_T id, CONST CHAR_T *song,
                                     CONST CHAR_T *singer, BOOL_T is_current,
                                     BOOL_T is_playing);
STATIC VOID_T __music_list_rebuild(VOID_T);
STATIC VOID_T __music_list_build_title_bar(VOID_T);
STATIC INT_T  __music_list_event_cb(VOID_T *data);
STATIC INT_T  __music_list_break_cb(VOID_T *data);

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */

/**
 * @brief Back button callback: pop the music list off the nav stack
 * @param[in] e LVGL event (unused)
 * @return none
 * @note Internal music↔music_list navigation goes through ui_nav_back directly
 *       (precedent: ui_device_mode_btn_cb uses ui_nav_replace). No dispatch
 *       action is needed since both screens live inside the view module.
 */
STATIC VOID_T __music_list_back_cb(lv_event_t *e)
{
    (VOID_T)e;
    PR_DEBUG("music_list: back");
    ui_nav_back();
}

/**
 * @brief Row click handler: switch to the tapped track via async API
 * @param[in] e LVGL event, user_data carries the playlist item id
 * @return none
 */
STATIC VOID_T __music_list_item_clicked_cb(lv_event_t *e)
{
    INT_T music_id = (INT_T)(uintptr_t)lv_event_get_user_data(e);
    OPERATE_RET rt = OPRT_OK;

    PR_DEBUG("music_list: play id=%d", music_id);
    rt = wukong_playback_playlist_play_async(music_id);
    if (rt != OPRT_OK) {
        PR_WARN("music_list: play_async failed id=%d rt=%d", music_id, rt);
    }
}

/**
 * @brief Delete button handler: drop the row from the local playlist
 * @param[in] e LVGL event, user_data carries the playlist item id
 * @return none
 * @note lv_indev_wait_release prevents the click from leaking down to the
 *       row container after the row is destroyed during list rebuild.
 */
STATIC VOID_T __music_list_delete_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_get_act();
    INT_T music_id = (INT_T)(uintptr_t)lv_event_get_user_data(e);
    OPERATE_RET rt = OPRT_OK;

    PR_INFO("music_list: delete id=%d", music_id);
    rt = wukong_playback_playlist_remove(music_id);
    if (rt != OPRT_OK) {
        PR_WARN("music_list: remove failed id=%d rt=%d", music_id, rt);
        return;
    }

    if (indev != NULL) {
        lv_indev_wait_release(indev);
    }
}

/**
 * @brief Build a single playlist row inside list_cont
 * @param[in] id          playlist item id (passed back to click/delete handlers)
 * @param[in] song        song display name (NULL/empty falls back to "未知歌曲")
 * @param[in] singer      artist display name (NULL/empty falls back to "未知歌手")
 * @param[in] is_current  TRUE if this row matches the currently playing track
 * @param[in] is_playing  TRUE if the player is in PLAYING state (drives PAUSE icon)
 * @return none
 */
STATIC VOID_T __music_list_make_item(INT_T id, CONST CHAR_T *song,
                                     CONST CHAR_T *singer, BOOL_T is_current,
                                     BOOL_T is_playing)
{
    lv_obj_t *item = lv_obj_create(s_music_list.list_cont);
    lv_obj_remove_style_all(item);
    lv_obj_set_size(item, MUSIC_LIST_ITEM_W, MUSIC_LIST_ITEM_H);
    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(item, MUSIC_LIST_ITEM_RADIUS, 0);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(item,
        lv_color_hex(is_current ? MUSIC_LIST_BG_CURRENT : MUSIC_LIST_BG_NORMAL), 0);
    lv_obj_set_style_pad_left(item, MUSIC_LIST_ITEM_PAD_H, 0);
    lv_obj_set_style_pad_right(item, MUSIC_LIST_ITEM_PAD_H, 0);
    lv_obj_set_style_pad_top(item, MUSIC_LIST_ITEM_PAD_V, 0);
    lv_obj_set_style_pad_bottom(item, MUSIC_LIST_ITEM_PAD_V, 0);
    lv_obj_set_style_border_width(item, 0, 0);
    lv_obj_set_scrollbar_mode(item, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(item, LV_DIR_NONE);
    lv_obj_add_event_cb(item, __music_list_item_clicked_cb,
                        LV_EVENT_CLICKED, (VOID_T *)(uintptr_t)id);

    /* Song title */
    lv_obj_t *song_lbl = lv_label_create(item);
    lv_obj_remove_style_all(song_lbl);
    lv_label_set_long_mode(song_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(song_lbl, (song != NULL && song[0] != '\0') ? song : "未知歌曲");
    lv_obj_set_size(song_lbl, MUSIC_LIST_LABEL_W, MUSIC_LIST_SONG_H);
    lv_obj_set_pos(song_lbl, 0, 0);
    lv_obj_set_style_text_font(song_lbl, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(song_lbl,
        is_current ? lv_color_hex(MUSIC_LIST_TEXT_CURRENT) : lv_color_white(), 0);
    lv_obj_set_style_text_align(song_lbl, LV_TEXT_ALIGN_LEFT, 0);

    /* Artist */
    lv_obj_t *artist_lbl = lv_label_create(item);
    lv_obj_remove_style_all(artist_lbl);
    lv_label_set_long_mode(artist_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(artist_lbl,
        (singer != NULL && singer[0] != '\0') ? singer : "未知歌手");
    lv_obj_set_size(artist_lbl, MUSIC_LIST_LABEL_W, MUSIC_LIST_ARTIST_H);
    lv_obj_set_pos(artist_lbl, 0, MUSIC_LIST_ARTIST_Y);
    lv_obj_set_style_text_font(artist_lbl, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(artist_lbl, lv_color_hex(MUSIC_LIST_TEXT_ARTIST), 0);
    lv_obj_set_style_text_align(artist_lbl, LV_TEXT_ALIGN_LEFT, 0);

    /* State indicator (current row only): LVGL symbol replaces desktop's PNGs */
    if (is_current) {
        lv_obj_t *state = lv_label_create(item);
        lv_obj_remove_style_all(state);
        lv_label_set_text(state, is_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
        lv_obj_set_style_text_font(state, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(state, lv_color_white(), 0);
        lv_obj_align(state, LV_ALIGN_RIGHT_MID, MUSIC_LIST_STATE_ICON_OFF, 0);
    }

    /* Delete button */
    lv_obj_t *del_btn = lv_btn_create(item);
    lv_obj_remove_style_all(del_btn);
    lv_obj_set_size(del_btn, MUSIC_LIST_DELETE_W, MUSIC_LIST_DELETE_H);
    lv_obj_align(del_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(del_btn, MUSIC_LIST_DELETE_RADIUS, 0);
    lv_obj_set_style_bg_opa(del_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(del_btn, lv_color_hex(MUSIC_LIST_DELETE_BG), 0);
    lv_obj_set_style_border_width(del_btn, 0, 0);
    lv_obj_add_event_cb(del_btn, __music_list_delete_cb,
                        LV_EVENT_CLICKED, (VOID_T *)(uintptr_t)id);

    lv_obj_t *del_lbl = lv_label_create(del_btn);
    lv_obj_remove_style_all(del_lbl);
    lv_label_set_text(del_lbl, "删除");
    lv_obj_center(del_lbl);
    lv_obj_set_style_text_font(del_lbl, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(del_lbl, lv_color_hex(MUSIC_LIST_TEXT_CURRENT), 0);
}

/**
 * @brief Wipe and re-populate the list from the current playlist JSON
 * @return none
 */
STATIC VOID_T __music_list_rebuild(VOID_T)
{
    if (s_music_list.list_cont == NULL) {
        return;
    }

    lv_obj_clean(s_music_list.list_cont);

    ty_cJSON *list = NULL;
    OPERATE_RET rt = wukong_playback_playlist_list(&list);
    if (rt != OPRT_OK || list == NULL || !ty_cJSON_IsArray(list)) {
        if (list != NULL) {
            ty_cJSON_Delete(list);
        }
        lv_obj_t *empty = lv_label_create(s_music_list.list_cont);
        lv_obj_remove_style_all(empty);
        lv_label_set_text(empty, "暂无歌曲");
        lv_obj_set_size(empty, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_text_font(empty, &AlibabaPuHuiTi3_Regular18_Static, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(MUSIC_LIST_TEXT_ARTIST), 0);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        return;
    }

    INT_T cur_id = wukong_playback_ctrl_get_current_play_id();
    WUKONG_MUSIC_PLAYER_T status = {0};
    BOOL_T is_playing = FALSE;
    if (wukong_playback_ctrl_get_status(&status) == OPRT_OK) {
        is_playing = (status.state == AI_PLAYER_PLAYING);
    }

    INT_T n = ty_cJSON_GetArraySize(list);
    INT_T i = 0;
    for (i = 0; i < n; i++) {
        ty_cJSON *row = ty_cJSON_GetArrayItem(list, i);
        if (row == NULL || !ty_cJSON_IsObject(row)) {
            continue;
        }
        ty_cJSON *j_id = ty_cJSON_GetObjectItem(row, "id");
        ty_cJSON *j_name = ty_cJSON_GetObjectItem(row, "song_name");
        ty_cJSON *j_artist = ty_cJSON_GetObjectItem(row, "artist");
        INT_T music_id = (j_id != NULL && ty_cJSON_IsNumber(j_id)) ? j_id->valueint : -1;
        CONST CHAR_T *song = (j_name != NULL && ty_cJSON_IsString(j_name)) ? j_name->valuestring : "";
        CONST CHAR_T *singer = (j_artist != NULL && ty_cJSON_IsString(j_artist)) ? j_artist->valuestring : "";
        BOOL_T is_current = (music_id >= 0 && music_id == cur_id);
        __music_list_make_item(music_id, song, singer, is_current, is_playing);
    }

    ty_cJSON_Delete(list);
}

/**
 * @brief Build the standard view title bar (back button + centered title)
 * @return none
 */
STATIC VOID_T __music_list_build_title_bar(VOID_T)
{
    s_music_list.title_bar = lv_obj_create(s_music_list.scr);
    lv_obj_remove_style_all(s_music_list.title_bar);
    lv_obj_set_pos(s_music_list.title_bar, 0, 0);
    lv_obj_set_size(s_music_list.title_bar, LV_HOR_RES, UI_TITLE_BAR_H);
    lv_obj_set_style_bg_opa(s_music_list.title_bar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_music_list.title_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_music_list.back_btn = lv_btn_create(s_music_list.title_bar);
    lv_obj_remove_style_all(s_music_list.back_btn);
    lv_obj_set_size(s_music_list.back_btn, UI_TITLE_BTN_W, UI_TITLE_BAR_H);
    lv_obj_set_pos(s_music_list.back_btn, 0, 0);
    lv_obj_set_style_bg_opa(s_music_list.back_btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(s_music_list.back_btn, __music_list_back_cb,
                        LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_icon = lv_img_create(s_music_list.back_btn);
    lv_img_set_src(back_icon, &icon_back_24_24);
    lv_obj_set_size(back_icon, UI_TITLE_ICON_SIZE, UI_TITLE_ICON_SIZE);
    lv_obj_center(back_icon);

    s_music_list.title_lbl = lv_label_create(s_music_list.title_bar);
    lv_label_set_text(s_music_list.title_lbl, MUSIC_LIST_TITLE_TEXT);
    lv_obj_set_style_text_font(s_music_list.title_lbl,
                               &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(s_music_list.title_lbl, lv_color_white(), 0);
    lv_obj_center(s_music_list.title_lbl);
}

/**
 * @brief EVENT_MUSIC_PLAYER handler: rebuild list so current-row mark/state stay live
 * @param[in] data WUKONG_MUSIC_PLAYER_T pointer (unused fields, just trigger)
 * @return OPRT_OK
 */
STATIC INT_T __music_list_event_cb(VOID_T *data)
{
    (VOID_T)data;
    if (s_music_list.scr == NULL) {
        return OPRT_OK;
    }
    if (lv_scr_act() != s_music_list.scr) {
        return OPRT_OK;
    }
    __music_list_rebuild();
    return OPRT_OK;
}

/**
 * @brief EVENT_MUSIC_BREAK handler: refresh the row state symbol after a break
 * @param[in] data unused
 * @return OPRT_OK
 */
STATIC INT_T __music_list_break_cb(VOID_T *data)
{
    (VOID_T)data;
    if (s_music_list.scr == NULL) {
        return OPRT_OK;
    }
    if (lv_scr_act() != s_music_list.scr) {
        return OPRT_OK;
    }
    __music_list_rebuild();
    return OPRT_OK;
}

/**
 * @brief Lazy-create the music list screen
 * @return none
 */
VOID_T setup_scr_music_list(VOID_T)
{
    if (s_music_list.scr != NULL) {
        return;
    }

    memset(&s_music_list, 0, sizeof(s_music_list));

    s_music_list.scr = lv_obj_create(NULL);
    lv_obj_set_size(s_music_list.scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_music_list.scr, lv_color_hex(UI_BG_COLOR_DARK), 0);
    lv_obj_set_style_bg_opa(s_music_list.scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_music_list.scr, 0, 0);
    lv_obj_set_scrollbar_mode(s_music_list.scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_music_list.scr, LV_OBJ_FLAG_SCROLLABLE);

    __music_list_build_title_bar();

    s_music_list.list_cont = lv_obj_create(s_music_list.scr);
    lv_obj_remove_style_all(s_music_list.list_cont);
    lv_obj_set_pos(s_music_list.list_cont, 0, UI_TITLE_BAR_H);
    lv_obj_set_size(s_music_list.list_cont, LV_HOR_RES, LV_VER_RES - UI_TITLE_BAR_H);
    lv_obj_set_style_bg_opa(s_music_list.list_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_music_list.list_cont, 0, 0);
    lv_obj_set_style_pad_ver(s_music_list.list_cont, 12, 0);
    lv_obj_set_style_pad_hor(s_music_list.list_cont, 20, 0);
    lv_obj_set_style_pad_row(s_music_list.list_cont, 10, 0);
    lv_obj_set_style_pad_column(s_music_list.list_cont, 0, 0);
    lv_obj_set_flex_flow(s_music_list.list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_music_list.list_cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_music_list.list_cont, LV_SCROLLBAR_MODE_OFF);

    ui_control_center_register_gesture(s_music_list.scr);

    lv_obj_update_layout(s_music_list.scr);
}

/**
 * @brief Show the music list screen (lazy create + per-visit subscribe)
 * @return none
 */
VOID_T ui_music_list_show(VOID_T)
{
    if (s_music_list.scr == NULL) {
        setup_scr_music_list();
    }

    __music_list_rebuild();

    if (!s_music_list.evt_subscribed) {
        OPERATE_RET rt = ty_subscribe_event(EVENT_MUSIC_PLAYER,
                                            MUSIC_LIST_EVT_SUBSCRIBER,
                                            __music_list_event_cb,
                                            SUBSCRIBE_TYPE_NORMAL);
        if (rt == OPRT_OK) {
            (VOID_T)ty_subscribe_event(EVENT_MUSIC_BREAK,
                                       MUSIC_LIST_EVT_SUBSCRIBER,
                                       __music_list_break_cb,
                                       SUBSCRIBE_TYPE_NORMAL);
            s_music_list.evt_subscribed = TRUE;
        } else {
            PR_ERR("music_list: subscribe EVENT_MUSIC_PLAYER failed rt=%d", rt);
        }
    }

    if (lv_scr_act() != s_music_list.scr) {
        lv_scr_load(s_music_list.scr);
    }
}

/**
 * @brief Hide the music list screen, drop per-visit subscriptions
 * @return none
 */
VOID_T ui_music_list_hide(VOID_T)
{
    if (s_music_list.evt_subscribed) {
        ty_unsubscribe_event(EVENT_MUSIC_PLAYER, MUSIC_LIST_EVT_SUBSCRIBER,
                             __music_list_event_cb);
        ty_unsubscribe_event(EVENT_MUSIC_BREAK, MUSIC_LIST_EVT_SUBSCRIBER,
                             __music_list_break_cb);
        s_music_list.evt_subscribed = FALSE;
    }
}

/**
 * @brief Get the music list screen object
 * @return music list screen pointer, NULL if not created
 */
lv_obj_t *ui_music_list_get_scr(VOID_T)
{
    return s_music_list.scr;
}
