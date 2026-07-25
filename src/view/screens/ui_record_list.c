/**
 * @file ui_record_list.c
 * @brief Record list screen + in-screen playback overlay for T5AI_BOARD (320x480)
 *
 * Layout: a top title bar followed by a content area that toggles between
 * a scrollable list of recordings and a card-style playback panel. The
 * playback panel is a sibling container that overlays the list (same
 * parent) and is hidden by default. All meaningful actions (play, delete,
 * upload, navigation) are forwarded through TY_DISP_ACT_* so the UI can
 * remain decoupled from the recording back-end.
 *
 * Function-layer responsibilities (file I/O, real playback, real upload)
 * are intentionally left to the dispatch stubs. The UI keeps its own mock
 * progress timer for the playback slider and exposes
 * ui_record_list_set_upload_progress() so the real path can drive the
 * upload bar later.
 *
 * @version 1.0
 * @date 2026-05-12
 * @copyright Copyright (c) Tuya Inc.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "ui_common.h"
#include "tuya_ai_display.h"
#include "tuya_list.h"
#include "tal_memory.h"
#include "ui_record_runtime.h"

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define RECORD_LIST_PLAY_GLYPH_COLOR   0xFFF37B
#define RECORD_LIST_PLAY_CIRCLE_BG     0xB8BDDE

#define RECORD_LIST_ITEM_W             280
#define RECORD_LIST_ITEM_H             72
#define RECORD_LIST_ITEM_RADIUS        16
#define RECORD_LIST_ITEM_BG            0x353740
#define RECORD_LIST_ITEM_LABEL_HINT    0xB8BDDE
#define RECORD_LIST_DELETE_BTN_W       52
#define RECORD_LIST_DELETE_BTN_H       28
#define RECORD_LIST_DELETE_BTN_RADIUS  14
#define RECORD_LIST_DELETE_BTN_BG      0x4B4D59
#define RECORD_LIST_DELETE_BTN_FG      0xF3E55D

#define RECORD_LIST_PLAY_CARD_W        280
#define RECORD_LIST_PLAY_CARD_H        170
#define RECORD_LIST_PLAY_CARD_RADIUS   16
#define RECORD_LIST_PLAY_CARD_BG       0x353740
#define RECORD_LIST_SLIDER_W           248
#define RECORD_LIST_PLAY_BTN_SIZE      40
#define RECORD_LIST_NAV_BTN_SIZE       26
#define RECORD_LIST_DEL_ICON_SIZE      24

#define RECORD_LIST_SLIDER_BG          0x3D4A6B
#define RECORD_LIST_SLIDER_FILL        0xF3E55D

#define RECORD_LIST_UPLOAD_BAR_W       200
#define RECORD_LIST_UPLOAD_BAR_H       8
#define RECORD_LIST_UPLOAD_BAR_RADIUS  4
#define RECORD_LIST_UPLOAD_FILL        0xFFA500

/* Transcribe-status badge — see ADR-0002.
 * Style derived from the delete-button style (radius / bg / font-size 18),
 * but width auto-fits the text via LV_SIZE_CONTENT + horizontal padding. */
#define RECORD_LIST_STATUS_BADGE_H         RECORD_LIST_DELETE_BTN_H
#define RECORD_LIST_STATUS_BADGE_RADIUS    RECORD_LIST_DELETE_BTN_RADIUS
#define RECORD_LIST_STATUS_BADGE_BG        RECORD_LIST_DELETE_BTN_BG
#define RECORD_LIST_STATUS_BADGE_PAD_HOR   8
#define RECORD_LIST_STATUS_BADGE_GAP       8
#define RECORD_LIST_STATUS_BADGE_FG_DEFAULT 0xF3E55D  /* matches delete glyph */
#define RECORD_LIST_STATUS_BADGE_FG_GREEN  0x4CAF50
#define RECORD_LIST_STATUS_BADGE_FG_RED    0xFF5252

#define RECORD_LIST_UPLOAD_BTN_DISABLED_FG 0x6E738C

#define RECORD_LIST_MAX_ITEMS          20

/* Reading card — sits 5 px below the play card, ends 5 px from the screen
 * bottom, same width. Side arrows fill the 20 px gutters left and right of
 * the play card (320 - 280) / 2 = 20. Geometry numbers below are derived
 * from these constants and verified in __record_list_build_reading_container.
 *   y_top  = 10(play_card y) + 170(play_card h) + 5     = 185
 *   height = 430(play_cont h) - 185 - 5                  = 240
 *   text W = 280 - 2 * 16(padding)                       = 248
 *   text H = 240 - 2 * 16 - 28(top row) - 8(gap)         = 172
 *   page H = 8 lines * 20 px line_height (Alibaba 18pt)  = 160
 */
#define RECORD_LIST_READ_GAP_TOP        5
#define RECORD_LIST_READ_GAP_BOT        5
#define RECORD_LIST_READ_CARD_W         RECORD_LIST_PLAY_CARD_W
#define RECORD_LIST_READ_CARD_H         240
#define RECORD_LIST_READ_CARD_Y         (10 + RECORD_LIST_PLAY_CARD_H + RECORD_LIST_READ_GAP_TOP)
#define RECORD_LIST_READ_CARD_RADIUS    16
#define RECORD_LIST_READ_CARD_BG        RECORD_LIST_PLAY_CARD_BG
#define RECORD_LIST_READ_CARD_PAD       16
#define RECORD_LIST_READ_TEXT_W         (RECORD_LIST_READ_CARD_W - 2 * RECORD_LIST_READ_CARD_PAD)
#define RECORD_LIST_READ_TOP_ROW_H      28
#define RECORD_LIST_READ_TOP_ROW_GAP    8
#define RECORD_LIST_READ_TEXT_AREA_Y    (RECORD_LIST_READ_TOP_ROW_H + RECORD_LIST_READ_TOP_ROW_GAP)
#define RECORD_LIST_READ_TEXT_AREA_H    \
    (RECORD_LIST_READ_CARD_H - 2 * RECORD_LIST_READ_CARD_PAD - \
     RECORD_LIST_READ_TOP_ROW_H - RECORD_LIST_READ_TOP_ROW_GAP)
#define RECORD_LIST_READ_FONT_LINE_H    20
#define RECORD_LIST_READ_LINES_PER_PAGE (RECORD_LIST_READ_TEXT_AREA_H / RECORD_LIST_READ_FONT_LINE_H)
#define RECORD_LIST_READ_PAGE_PIXEL_H   (RECORD_LIST_READ_LINES_PER_PAGE * RECORD_LIST_READ_FONT_LINE_H)
#define RECORD_LIST_READ_SIDE_BTN_W     20
#define RECORD_LIST_READ_SIDE_BTN_H     RECORD_LIST_READ_CARD_H
#define RECORD_LIST_READ_SIDE_BTN_FG    0xF3E55D
#define RECORD_LIST_READ_SIDE_BTN_DISABLED_FG 0x6E738C
#define RECORD_LIST_READ_CHOICE_BTN_W   RECORD_LIST_READ_TEXT_W
#define RECORD_LIST_READ_CHOICE_BTN_H   48
#define RECORD_LIST_READ_CHOICE_GAP     12
#define RECORD_LIST_READ_NO_FILE_FG     0xB8BDDE
#define RECORD_LIST_READ_BACK_STACK_DEPTH 256
#define RECORD_LIST_READ_CHUNK_BYTES    1024

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef struct {
    lv_obj_t *scr;
    lv_obj_t *title_bar;
    lv_obj_t *title_lbl;
    lv_obj_t *back_btn;
    lv_obj_t *list_cont;
    lv_obj_t *empty_lbl;

    /* Playback overlay */
    lv_obj_t *play_cont;
    lv_obj_t *play_card;
    lv_obj_t *play_title_lbl;
    lv_obj_t *play_status_badge;
    lv_obj_t *play_status_lbl;
    lv_obj_t *play_date_lbl;
    lv_obj_t *play_collapse_btn;
    lv_obj_t *play_slider;
    lv_obj_t *play_cur_lbl;
    lv_obj_t *play_total_lbl;
    lv_obj_t *play_btn;
    lv_obj_t *play_btn_img;
    lv_obj_t *play_back_btn;
    lv_obj_t *play_fwd_btn;
    lv_obj_t *play_upload_btn;
    lv_obj_t *play_upload_icon;
    lv_obj_t *play_delete_btn;

    /* Upload overlay */
    lv_obj_t *upload_cont;
    lv_obj_t *upload_bar;
    lv_obj_t *upload_pct_lbl;

    /* Reading card overlay (sibling of play_card inside play_cont) */
    lv_obj_t *read_cont;
    lv_obj_t *read_back_btn;
    lv_obj_t *read_page_lbl;
    lv_obj_t *read_choice_cont;
    lv_obj_t *read_choice_btn[2];
    lv_obj_t *read_choice_lbl[2];
    lv_obj_t *read_text_lbl;
    lv_obj_t *read_no_file_lbl;
    lv_obj_t *read_left_btn;
    lv_obj_t *read_left_glyph;
    lv_obj_t *read_right_btn;
    lv_obj_t *read_right_glyph;

    /* Playback mock state */
    lv_timer_t *play_timer;
    BOOL_T      playing;
    BOOL_T      paused;
    UINT32_T    play_elapsed_sec;
} RECORD_LIST_UI_T;

/**
 * @brief Reading-card view state
 *
 * Three views layered behind the same card:
 *   - NO_FILE         transcribe_status != 1, only the "暂无文件" label
 *   - BUTTON_SELECT   status == 1, 1-2 choice buttons (transcribe / summary)
 *   - TEXT_VIEWING    user picked a kind, file is open and paginated
 *
 * Only TEXT_VIEWING owns a TUYA_FILE handle; transitions out of it must
 * close the handle to avoid leaking it across view changes.
 */
typedef enum {
    READING_VIEW_NO_FILE = 0,
    READING_VIEW_BUTTON_SELECT,
    READING_VIEW_TEXT_VIEWING,
} READING_VIEW_T;

/**
 * @brief Streaming-pagination state for the reading card
 *
 * Pagination is one-pass forward: each render reads a chunk starting at
 * cur_offset, binary-searches the largest UTF-8-aligned prefix that fits
 * RECORD_LIST_READ_PAGE_PIXEL_H, displays it, and remembers the byte-count
 * via cur_page_bytes. Going forward pushes cur_offset onto back_stack;
 * going back pops it. We never need a "total pages" count — the file is
 * never pre-scanned. Stack overflow drops the oldest entry so the user
 * can still go back the most recent 256 pages.
 */
typedef struct {
    READING_VIEW_T     view;

    UI_REC_FILE_KIND_T kind;
    TUYA_FILE          fp;
    UINT32_T           file_size;
    UINT32_T           cur_offset;
    UINT32_T           cur_page_index;
    UINT32_T           back_stack[RECORD_LIST_READ_BACK_STACK_DEPTH];
    UINT16_T           back_top;
    BOOL_T             at_last_page;

    CHAR_T             chunk_buf[RECORD_LIST_READ_CHUNK_BYTES + 1];
    UINT32_T           cur_page_bytes;
} READING_STATE_T;

/* ---------------------------------------------------------------------------
 * Type definitions (internal)
 * --------------------------------------------------------------------------- */
/**
 * @brief Internal linked-list node holding one displayed item
 * @note  Owned by this module; allocated via tal_malloc on push, freed
 *        on replace_begin / module clear. Avoids holding a large
 *        UI_RECORD_ITEM_T buffer on either the stack or BSS.
 */
typedef struct {
    LIST_HEAD        list_node;
    UI_RECORD_ITEM_T item;
} UI_RECORD_LIST_NODE_T;

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC RECORD_LIST_UI_T s_record_list = {0};

/* Internal linked-list snapshot of items currently displayed. Nodes are
 * dynamically allocated; replace_begin() releases all of them, push()
 * appends one, commit() rebuilds the LVGL view. */
STATIC LIST_HEAD s_ui_list_head;
STATIC BOOL_T    s_ui_list_inited = FALSE;
STATIC UINT32_T  s_ui_list_count  = 0;

STATIC UI_RECORD_ITEM_T  s_record_list_play_item = {0};
STATIC BOOL_T            s_record_list_play_item_valid = FALSE;

STATIC READING_STATE_T   s_reading = {0};

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC VOID_T __record_list_back_cb(lv_event_t *e);
STATIC VOID_T __record_list_item_clicked_cb(lv_event_t *e);
STATIC VOID_T __record_list_item_delete_cb(lv_event_t *e);
STATIC VOID_T __record_list_play_back_cb(lv_event_t *e);
STATIC VOID_T __record_list_play_toggle_cb(lv_event_t *e);
#if 0 /* rewind/forward: reserved for future re-enable */
STATIC VOID_T __record_list_play_rewind_cb(lv_event_t *e);
STATIC VOID_T __record_list_play_forward_cb(lv_event_t *e);
#endif
STATIC VOID_T __record_list_play_upload_cb(lv_event_t *e);
STATIC VOID_T __record_list_play_delete_cb(lv_event_t *e);
STATIC VOID_T __record_list_play_timer_cb(lv_timer_t *timer);
STATIC VOID_T __record_list_show_list_view(VOID_T);
STATIC VOID_T __record_list_show_play_view(CONST UI_RECORD_ITEM_T *item);
STATIC VOID_T __record_list_play_stop_mock(VOID_T);
STATIC VOID_T __record_list_play_pause_mock(VOID_T);
STATIC VOID_T __record_list_build_title_bar(VOID_T);
STATIC VOID_T __record_list_build_list_container(VOID_T);
STATIC VOID_T __record_list_build_play_container(VOID_T);
STATIC VOID_T __record_list_build_row(CONST UI_RECORD_ITEM_T *it);
STATIC VOID_T __record_list_rebuild_list(VOID_T);
STATIC VOID_T __ui_list_ensure_inited(VOID_T);
STATIC VOID_T __ui_list_clear(VOID_T);
STATIC UI_RECORD_LIST_NODE_T *__ui_list_find_by_id(INT_T id);
STATIC lv_obj_t *__record_list_make_status_badge(lv_obj_t *parent, lv_obj_t **out_lbl);
STATIC VOID_T __record_list_apply_status_badge(lv_obj_t *lbl, INT_T transcribe_status);
STATIC VOID_T __record_list_apply_upload_btn_enabled(BOOL_T enabled);
STATIC VOID_T __record_list_apply_btn_enabled(lv_obj_t *btn,
                                              lv_obj_t *glyph,
                                              UINT32_T enabled_fg,
                                              UINT32_T disabled_fg,
                                              BOOL_T enabled);

/* Reading card */
STATIC VOID_T __record_list_build_reading_container(VOID_T);
STATIC VOID_T __record_list_reading_show_no_file(VOID_T);
STATIC VOID_T __record_list_reading_show_button_select(VOID_T);
STATIC VOID_T __record_list_reading_show_text_view(UI_REC_FILE_KIND_T kind);
STATIC VOID_T __record_list_reading_close_file(VOID_T);
STATIC VOID_T __record_list_reading_render_page(VOID_T);
STATIC VOID_T __record_list_reading_apply_side_btn_state(VOID_T);
STATIC VOID_T __record_list_reading_show_card(VOID_T);
STATIC VOID_T __record_list_reading_hide_card(VOID_T);
STATIC VOID_T __record_list_reading_show_for_status(INT_T transcribe_status);
STATIC VOID_T __record_list_read_back_cb(lv_event_t *e);
STATIC VOID_T __record_list_read_choice_cb(lv_event_t *e);
STATIC VOID_T __record_list_read_left_cb(lv_event_t *e);
STATIC VOID_T __record_list_read_right_cb(lv_event_t *e);

/* ---------------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------------- */

/**
 * @brief Lazily initialize the internal linked list head
 * @return none
 */
STATIC VOID_T __ui_list_ensure_inited(VOID_T)
{
    if (s_ui_list_inited == FALSE) {
        INIT_LIST_HEAD(&s_ui_list_head);
        s_ui_list_inited = TRUE;
    }
}

/**
 * @brief Free every node in the internal linked list
 * @return none
 */
STATIC VOID_T __ui_list_clear(VOID_T)
{
    LIST_HEAD *pos = NULL;
    LIST_HEAD *next = NULL;

    __ui_list_ensure_inited();
    tuya_list_for_each_safe(pos, next, &s_ui_list_head) {
        UI_RECORD_LIST_NODE_T *n = tuya_list_entry(pos, UI_RECORD_LIST_NODE_T, list_node);
        if (n == NULL) {
            continue;
        }
        tuya_list_del(&n->list_node);
        tal_free(n);
    }
    s_ui_list_count = 0;
}

/**
 * @brief Locate the internal node whose item id matches
 * @param[in] id record id to look up
 * @return pointer to matching node, NULL if not found
 */
STATIC UI_RECORD_LIST_NODE_T *__ui_list_find_by_id(INT_T id)
{
    LIST_HEAD *pos = NULL;

    __ui_list_ensure_inited();
    tuya_list_for_each(pos, &s_ui_list_head) {
        UI_RECORD_LIST_NODE_T *n = tuya_list_entry(pos, UI_RECORD_LIST_NODE_T, list_node);
        if (n != NULL && n->item.id == id) {
            return n;
        }
    }
    return NULL;
}

/**
 * @brief Format seconds as MM:SS into a fixed buffer
 * @param[out] buf output buffer
 * @param[in] buf_size size of output buffer
 * @param[in] sec input seconds
 * @return none
 */
STATIC VOID_T __record_list_format_mmss(CHAR_T *buf, UINT32_T buf_size, UINT32_T sec)
{
    snprintf(buf, buf_size, "%02u:%02u",
             (unsigned)(sec / 60), (unsigned)(sec % 60));
}

/**
 * @brief Build a transcribe-status badge object aligned to the right of "录音"
 *
 * Visual style follows the delete button (same radius/bg/font-size 18) but
 * the width is content-driven (LV_SIZE_CONTENT + 8px horizontal padding) so
 * that "未上传"/"处理中"/"完成"/"失败" all render unclipped — see ADR-0002.
 *
 * @param[in]  parent parent container that holds the "录音" title label
 *                    (the row container in list view, the play_card in
 *                    playback view); the badge is created in the same
 *                    container so it can align relative to that label
 * @param[out] out_lbl receives the inner label handle so callers can later
 *                     recolor / retext it via __record_list_apply_status_badge
 * @return badge container object
 */
STATIC lv_obj_t *__record_list_make_status_badge(lv_obj_t *parent, lv_obj_t **out_lbl)
{
    lv_obj_t *badge = lv_obj_create(parent);
    lv_obj_t *lbl;

    lv_obj_remove_style_all(badge);
    lv_obj_set_height(badge, RECORD_LIST_STATUS_BADGE_H);
    lv_obj_set_width(badge, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(badge, RECORD_LIST_STATUS_BADGE_RADIUS, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(badge,
                              lv_color_hex(RECORD_LIST_STATUS_BADGE_BG), 0);
    lv_obj_set_style_pad_hor(badge, RECORD_LIST_STATUS_BADGE_PAD_HOR, 0);
    lv_obj_set_style_pad_ver(badge, 0, 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_set_scrollbar_mode(badge, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);

    lbl = lv_label_create(badge);
    lv_obj_remove_style_all(lbl);
    lv_label_set_text(lbl, "");
    lv_obj_set_style_text_font(lbl, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_center(lbl);

    if (out_lbl != NULL) {
        *out_lbl = lbl;
    }
    return badge;
}

/**
 * @brief Set the badge's text + color to reflect a transcribe_status value
 * @param[in] lbl   inner label handle returned by __record_list_make_status_badge
 * @param[in] transcribe_status -1/0/1/2 — see ADR-0002
 * @return none
 * @note Maps:
 *         -1 → "未上传"  fg = 0xF3E55D (matches delete glyph)
 *          0 → "处理中"  fg = 0x4CAF50 (green)
 *          1 → "完成"    fg = 0xF3E55D (matches delete glyph)
 *          2 → "失败"    fg = 0xFF5252 (red)
 *       Out-of-range values render as "未上传" so the badge never goes
 *       blank in the face of a future enum extension.
 */
STATIC VOID_T __record_list_apply_status_badge(lv_obj_t *lbl, INT_T transcribe_status)
{
    CONST CHAR_T *text = "未上传";
    UINT32_T fg = RECORD_LIST_STATUS_BADGE_FG_DEFAULT;

    if (lbl == NULL) {
        return;
    }
    switch (transcribe_status) {
        case 0:
            text = "处理中";
            fg = RECORD_LIST_STATUS_BADGE_FG_GREEN;
            break;
        case 1:
            text = "完成";
            fg = RECORD_LIST_STATUS_BADGE_FG_GREEN;
            break;
        case 2:
            text = "失败";
            fg = RECORD_LIST_STATUS_BADGE_FG_RED;
            break;
        case -1:
        default:
            text = "未上传";
            fg = RECORD_LIST_STATUS_BADGE_FG_DEFAULT;
            break;
    }
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(fg), 0);
}

/**
 * @brief Toggle the upload button's enabled state on the play card
 * @param[in] enabled TRUE = clickable & full opacity, FALSE = inactive
 *                    & 40% opacity (visual cue that the action is unavailable)
 * @return none
 * @note Disabled iff md5 is unavailable (ADR-0001 hash failure / legacy json),
 *       which makes the recording impossible to associate with a cloud
 *       transcription. UI-side guard in addition to runtime-side reject in
 *       ui_record_runtime_upload — see ADR-0002.
 */
STATIC VOID_T __record_list_apply_upload_btn_enabled(BOOL_T enabled)
{
    __record_list_apply_btn_enabled(s_record_list.play_upload_btn,
                                    s_record_list.play_upload_icon,
                                    0xFFFFFF,
                                    RECORD_LIST_UPLOAD_BTN_DISABLED_FG,
                                    enabled);
}

/**
 * @brief Generic enabled/disabled toggle for any lv_btn the screen owns
 * @param[in] btn target button (NULL is a no-op so callers can stay terse)
 * @param[in] enabled TRUE = clickable & full opacity, FALSE = greyed out
 * @return none
 * @note Shared by the upload button and the reading-card side arrows.
 *       Keeping a single implementation guarantees the disabled visual
 *       (40% opa + LV_STATE_DISABLED + non-clickable) stays consistent.
 */
STATIC VOID_T __record_list_apply_btn_enabled(lv_obj_t *btn,
                                              lv_obj_t *glyph,
                                              UINT32_T enabled_fg,
                                              UINT32_T disabled_fg,
                                              BOOL_T enabled)
{
    if (btn == NULL) {
        return;
    }
    if (enabled == TRUE) {
        lv_obj_clear_state(btn, LV_STATE_DISABLED);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        if (glyph != NULL) {
            lv_obj_set_style_text_color(glyph, lv_color_hex(enabled_fg), 0);
        }
    } else {
        lv_obj_add_state(btn, LV_STATE_DISABLED);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        if (glyph != NULL) {
            lv_obj_set_style_text_color(glyph, lv_color_hex(disabled_fg), 0);
        }
    }
}

/**
 * @brief Stop the mock playback timer and reset the play button glyph
 * @return none
 * @note Hard stop: drops the timer, clears both playing/paused, resets
 *       the toggle glyph to the "ready to play" state. Used on EOF, on
 *       leaving the playback view, and on screen hide.
 */
STATIC VOID_T __record_list_play_stop_mock(VOID_T)
{
    if (s_record_list.play_timer != NULL) {
        lv_timer_del(s_record_list.play_timer);
        s_record_list.play_timer = NULL;
    }
    s_record_list.playing = FALSE;
    s_record_list.paused  = FALSE;
    if (s_record_list.play_btn_img != NULL) {
        lv_label_set_text(s_record_list.play_btn_img, LV_SYMBOL_PLAY);
    }
}

/**
 * @brief Soft pause the mock UI: keep timer alive, flip glyph, mark paused
 * @return none
 * @note The 1s mock timer is intentionally kept; its callback already
 *       early-returns when playing == FALSE, so progress stays frozen
 *       at the current second until resume flips playing back to TRUE.
 */
STATIC VOID_T __record_list_play_pause_mock(VOID_T)
{
    s_record_list.playing = FALSE;
    s_record_list.paused  = TRUE;
    if (s_record_list.play_btn_img != NULL) {
        lv_label_set_text(s_record_list.play_btn_img, LV_SYMBOL_PLAY);
    }
}

/**
 * @brief Switch back to list view: hide the play overlay
 * @return none
 * @note Posts RECORD_PLAY_STOP before tearing down the UI mock so the
 *       background audio player thread actually stops consuming the
 *       file; otherwise the playback would keep running until EOF even
 *       after the user collapses the play view.
 */
STATIC VOID_T __record_list_show_list_view(VOID_T)
{
    tuya_ai_display_action_post(NULL, 0, TY_DISP_ACT_RECORD_PLAY_STOP);
    __record_list_play_stop_mock();
    __record_list_reading_hide_card();
    if (s_record_list.play_cont != NULL) {
        lv_obj_add_flag(s_record_list.play_cont, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_record_list.list_cont != NULL) {
        lv_obj_clear_flag(s_record_list.list_cont, LV_OBJ_FLAG_HIDDEN);
    }
    s_record_list_play_item_valid = FALSE;
}

/**
 * @brief Switch to play view, populate fields from the given item
 * @param[in] item recording entry to play (caller-owned; copied locally)
 * @return none
 */
STATIC VOID_T __record_list_show_play_view(CONST UI_RECORD_ITEM_T *item)
{
    CHAR_T total[16];

    if (item == NULL) {
        return;
    }

    memcpy(&s_record_list_play_item, item, sizeof(s_record_list_play_item));
    s_record_list_play_item_valid = TRUE;
    s_record_list.play_elapsed_sec = 0;
    s_record_list.paused = FALSE;

    if (s_record_list.play_title_lbl != NULL) {
        lv_label_set_text(s_record_list.play_title_lbl,
                          item->name[0] ? item->name : "录音");
    }
    if (s_record_list.play_status_lbl != NULL) {
        __record_list_apply_status_badge(s_record_list.play_status_lbl,
                                         item->transcribe_status);
    }
    if (s_record_list.play_status_badge != NULL &&
        s_record_list.play_title_lbl != NULL) {
        lv_obj_align_to(s_record_list.play_status_badge,
                        s_record_list.play_title_lbl,
                        LV_ALIGN_OUT_RIGHT_MID,
                        RECORD_LIST_STATUS_BADGE_GAP, 0);
    }
    __record_list_apply_upload_btn_enabled(item->md5_unavailable == FALSE);
    if (s_record_list.play_date_lbl != NULL) {
        lv_label_set_text(s_record_list.play_date_lbl, item->datetime_str);
    }
    if (s_record_list.play_slider != NULL) {
        lv_slider_set_range(s_record_list.play_slider, 0,
                            (item->duration_sec > 0)
                                ? (int32_t)item->duration_sec : 1);
        lv_slider_set_value(s_record_list.play_slider, 0, LV_ANIM_OFF);
    }
    if (s_record_list.play_cur_lbl != NULL) {
        lv_label_set_text(s_record_list.play_cur_lbl, "00:00");
    }
    if (s_record_list.play_total_lbl != NULL) {
        __record_list_format_mmss(total, sizeof(total), item->duration_sec);
        lv_label_set_text(s_record_list.play_total_lbl, total);
    }

    /* Hide upload progress until upload starts */
    if (s_record_list.upload_cont != NULL) {
        lv_obj_add_flag(s_record_list.upload_cont, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_record_list.list_cont != NULL) {
        lv_obj_add_flag(s_record_list.list_cont, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_record_list.play_cont != NULL) {
        lv_obj_clear_flag(s_record_list.play_cont, LV_OBJ_FLAG_HIDDEN);
    }

    /* Reading card lifecycle ties to the play view: show it now and pick
     * the sub-view based on transcribe_status. */
    __record_list_reading_show_card();
    __record_list_reading_show_for_status(item->transcribe_status);
}

/* ---------------------------------------------------------------------------
 * Event callbacks
 * --------------------------------------------------------------------------- */

/**
 * @brief Back-button callback: close the record list screen
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __record_list_back_cb(lv_event_t *e)
{
    (VOID_T)e;
    /* If the playback view is on top, collapse it first before popping
     * the whole screen. This mirrors the desktop UI's two-step back. */
    if (s_record_list.play_cont != NULL &&
        !lv_obj_has_flag(s_record_list.play_cont, LV_OBJ_FLAG_HIDDEN)) {
        __record_list_show_list_view();
        return;
    }
    PR_DEBUG("record_list: back");
    tuya_ai_display_action_post(NULL, 0, TY_DISP_ACT_CLOSE_RECORD_LIST);
}

/**
 * @brief List item click: switch to playback view for that item
 * @param[in] e LVGL event (user_data carries record id)
 * @return none
 */
STATIC VOID_T __record_list_item_clicked_cb(lv_event_t *e)
{
    INT_T id = (INT_T)(intptr_t)lv_event_get_user_data(e);
    UI_RECORD_LIST_NODE_T *node = __ui_list_find_by_id(id);

    if (node == NULL) {
        return;
    }

    PR_DEBUG("record_list: item clicked, id=%d", id);
    ui_record_runtime_dump_info(id);
    __record_list_show_play_view(&node->item);
}

/**
 * @brief Delete button on a list row: post DELETE action with item id
 * @param[in] e LVGL event (user_data carries item id)
 * @return none
 * @note The UI does not remove the row eagerly; the back-end is expected
 *       to drive ui_record_list_replace_begin/push/commit() with the
 *       updated list to refresh the view once the underlying file is
 *       removed.
 */
STATIC VOID_T __record_list_item_delete_cb(lv_event_t *e)
{
    INT_T id = (INT_T)(intptr_t)lv_event_get_user_data(e);
    UINT8_T payload[sizeof(INT_T)];

    memcpy(payload, &id, sizeof(payload));
    PR_DEBUG("record_list: delete item id=%d", id);
    tuya_ai_display_action_post(payload, sizeof(payload),
                                TY_DISP_ACT_RECORD_DELETE);
}

/**
 * @brief Playback view back/collapse: return to list view
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __record_list_play_back_cb(lv_event_t *e)
{
    (VOID_T)e;
    PR_DEBUG("record_list: play view back");
    __record_list_show_list_view();
}

/**
 * @brief Mock-progress timer: advance elapsed seconds, refresh slider/label
 * @param[in] timer LVGL timer handle
 * @return none
 */
STATIC VOID_T __record_list_play_timer_cb(lv_timer_t *timer)
{
    CHAR_T buf[16];

    (VOID_T)timer;

    if (!s_record_list.playing) {
        return;
    }

    s_record_list.play_elapsed_sec++;
    if (s_record_list_play_item.duration_sec > 0 &&
        s_record_list.play_elapsed_sec >= s_record_list_play_item.duration_sec) {
        s_record_list.play_elapsed_sec = s_record_list_play_item.duration_sec;
        __record_list_play_stop_mock();
    }

    if (s_record_list.play_slider != NULL) {
        lv_slider_set_value(s_record_list.play_slider,
                            (int32_t)s_record_list.play_elapsed_sec, LV_ANIM_OFF);
    }
    if (s_record_list.play_cur_lbl != NULL) {
        __record_list_format_mmss(buf, sizeof(buf), s_record_list.play_elapsed_sec);
        lv_label_set_text(s_record_list.play_cur_lbl, buf);
    }
}

/**
 * @brief Play/pause toggle: three-state machine driving UI + ACTION channel
 * @param[in] e LVGL event
 * @return none
 * @note Branches:
 *       1. playing -> paused: post RECORD_PLAY_PAUSE, soft pause UI.
 *       2. idle / completed -> playing: post RECORD_PLAY (id), start UI
 *          mock timer from 0.
 *       3. paused -> playing (resume): post RECORD_PLAY_RESUME, keep
 *          elapsed second, flip glyph back.
 */
STATIC VOID_T __record_list_play_toggle_cb(lv_event_t *e)
{
    UINT8_T payload[sizeof(INT_T)];
    INT_T id;
    BOOL_T is_completed;
    BOOL_T is_first_play;

    (VOID_T)e;

    if (!s_record_list_play_item_valid) {
        return;
    }

    /* Branch 1: playing -> paused */
    if (s_record_list.playing) {
        PR_DEBUG("record_list: pause play, id=%d", s_record_list_play_item.id);
        __record_list_play_pause_mock();
        tuya_ai_display_action_post(NULL, 0, TY_DISP_ACT_RECORD_PLAY_PAUSE);
        return;
    }

    /* Branch 2: idle / completed -> playing (start from beginning).
     * "Completed" = elapsed reached duration (EOF tail). "Idle" = the
     * very first toggle in this play view, identified by no timer alive
     * AND no paused flag. */
    is_completed = (s_record_list_play_item.duration_sec > 0 &&
                    s_record_list.play_elapsed_sec >= s_record_list_play_item.duration_sec);
    is_first_play = (!s_record_list.paused && s_record_list.play_timer == NULL);

    if (is_completed || is_first_play) {
        s_record_list.play_elapsed_sec = 0;
        s_record_list.paused = FALSE;
        s_record_list.playing = TRUE;
        if (s_record_list.play_slider != NULL) {
            lv_slider_set_value(s_record_list.play_slider, 0, LV_ANIM_OFF);
        }
        if (s_record_list.play_btn_img != NULL) {
            lv_label_set_text(s_record_list.play_btn_img, LV_SYMBOL_PAUSE);
        }
        if (s_record_list.play_timer == NULL) {
            s_record_list.play_timer = lv_timer_create(__record_list_play_timer_cb, 1000, NULL);
        }
        id = s_record_list_play_item.id;
        memcpy(payload, &id, sizeof(payload));
        PR_DEBUG("record_list: start play, id=%d", id);
        tuya_ai_display_action_post(payload, sizeof(payload),
                                    TY_DISP_ACT_RECORD_PLAY);
        return;
    }

    /* Branch 3: paused -> playing (resume) */
    s_record_list.playing = TRUE;
    s_record_list.paused  = FALSE;
    if (s_record_list.play_btn_img != NULL) {
        lv_label_set_text(s_record_list.play_btn_img, LV_SYMBOL_PAUSE);
    }
    if (s_record_list.play_timer == NULL) {
        s_record_list.play_timer = lv_timer_create(__record_list_play_timer_cb, 1000, NULL);
    }
    PR_DEBUG("record_list: resume play, id=%d", s_record_list_play_item.id);
    tuya_ai_display_action_post(NULL, 0, TY_DISP_ACT_RECORD_PLAY_RESUME);
}

#if 0 /* rewind/forward: reserved for future re-enable */
/**
 * @brief Rewind 15s on the mock progress bar
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __record_list_play_rewind_cb(lv_event_t *e)
{
    (VOID_T)e;
    if (!s_record_list_play_item_valid) {
        return;
    }

    if (s_record_list.play_elapsed_sec >= 15) {
        s_record_list.play_elapsed_sec -= 15;
    } else {
        s_record_list.play_elapsed_sec = 0;
    }
    PR_DEBUG("record_list: rewind 15s -> %u",
             (unsigned)s_record_list.play_elapsed_sec);

    if (s_record_list.play_slider != NULL) {
        lv_slider_set_value(s_record_list.play_slider,
                            (int32_t)s_record_list.play_elapsed_sec, LV_ANIM_OFF);
    }
    if (s_record_list.play_cur_lbl != NULL) {
        CHAR_T buf[16];
        __record_list_format_mmss(buf, sizeof(buf), s_record_list.play_elapsed_sec);
        lv_label_set_text(s_record_list.play_cur_lbl, buf);
    }
}

/**
 * @brief Forward 15s on the mock progress bar
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __record_list_play_forward_cb(lv_event_t *e)
{
    UINT32_T next;

    (VOID_T)e;
    if (!s_record_list_play_item_valid) {
        return;
    }

    next = s_record_list.play_elapsed_sec + 15;
    if (s_record_list_play_item.duration_sec > 0 && next > s_record_list_play_item.duration_sec) {
        next = s_record_list_play_item.duration_sec;
    }
    s_record_list.play_elapsed_sec = next;
    PR_DEBUG("record_list: forward 15s -> %u", (unsigned)next);

    if (s_record_list.play_slider != NULL) {
        lv_slider_set_value(s_record_list.play_slider,
                            (int32_t)s_record_list.play_elapsed_sec, LV_ANIM_OFF);
    }
    if (s_record_list.play_cur_lbl != NULL) {
        CHAR_T buf[16];
        __record_list_format_mmss(buf, sizeof(buf), s_record_list.play_elapsed_sec);
        lv_label_set_text(s_record_list.play_cur_lbl, buf);
    }
}
#endif /* rewind/forward */

/**
 * @brief Upload button: post UPLOAD action and reveal upload progress bar
 * @param[in] e LVGL event
 * @return none
 * @note Real progress should be driven by ui_record_list_set_upload_progress().
 */
STATIC VOID_T __record_list_play_upload_cb(lv_event_t *e)
{
    UINT8_T payload[sizeof(INT_T)];
    INT_T id;

    (VOID_T)e;
    if (!s_record_list_play_item_valid) {
        return;
    }

    /* Hide reading card while the upload bar occupies the same slot. The
     * 100% completion path in ui_record_list_set_upload_progress restores
     * it. */
    __record_list_reading_hide_card();

    if (s_record_list.upload_cont != NULL) {
        lv_obj_clear_flag(s_record_list.upload_cont, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_record_list.upload_bar != NULL) {
        lv_bar_set_value(s_record_list.upload_bar, 0, LV_ANIM_OFF);
    }
    if (s_record_list.upload_pct_lbl != NULL) {
        lv_label_set_text(s_record_list.upload_pct_lbl, "0%");
    }

    id = s_record_list_play_item.id;
    memcpy(payload, &id, sizeof(payload));
    PR_DEBUG("record_list: upload, id=%d", id);
    tuya_ai_display_action_post(payload, sizeof(payload),
                                TY_DISP_ACT_RECORD_UPLOAD);
}

/**
 * @brief Delete button on playback view: post DELETE and go back to list
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __record_list_play_delete_cb(lv_event_t *e)
{
    UINT8_T payload[sizeof(INT_T)];
    INT_T id;

    (VOID_T)e;
    if (!s_record_list_play_item_valid) {
        return;
    }

    id = s_record_list_play_item.id;
    memcpy(payload, &id, sizeof(payload));
    PR_DEBUG("record_list: delete from play view, id=%d", id);
    tuya_ai_display_action_post(payload, sizeof(payload),
                                TY_DISP_ACT_RECORD_DELETE);
    __record_list_show_list_view();
}

/* ---------------------------------------------------------------------------
 * Builders
 * --------------------------------------------------------------------------- */

/**
 * @brief Build the top title bar (back button + center title)
 * @return none
 */
STATIC VOID_T __record_list_build_title_bar(VOID_T)
{
    s_record_list.title_bar = lv_obj_create(s_record_list.scr);
    lv_obj_remove_style_all(s_record_list.title_bar);
    lv_obj_set_pos(s_record_list.title_bar, 0, 0);
    lv_obj_set_size(s_record_list.title_bar, LV_HOR_RES, UI_TITLE_BAR_H);
    lv_obj_set_style_bg_opa(s_record_list.title_bar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_record_list.title_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_record_list.back_btn = lv_btn_create(s_record_list.title_bar);
    lv_obj_remove_style_all(s_record_list.back_btn);
    lv_obj_set_size(s_record_list.back_btn, UI_TITLE_BTN_W, UI_TITLE_BAR_H);
    lv_obj_set_pos(s_record_list.back_btn, 0, 0);
    lv_obj_set_style_bg_opa(s_record_list.back_btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(s_record_list.back_btn, __record_list_back_cb, LV_EVENT_CLICKED,
                        NULL);

    lv_obj_t *back_icon = lv_img_create(s_record_list.back_btn);
    lv_img_set_src(back_icon, &icon_back_24_24);
    lv_obj_center(back_icon);

    s_record_list.title_lbl = lv_label_create(s_record_list.title_bar);
    lv_label_set_text(s_record_list.title_lbl, "录音文件");
    lv_obj_set_style_text_font(s_record_list.title_lbl,
                               &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(s_record_list.title_lbl, lv_color_white(), 0);
    lv_obj_center(s_record_list.title_lbl);
}

/**
 * @brief Build the scrollable list container (rows are added in rebuild)
 * @return none
 */
STATIC VOID_T __record_list_build_list_container(VOID_T)
{
    s_record_list.list_cont = lv_obj_create(s_record_list.scr);
    lv_obj_remove_style_all(s_record_list.list_cont);
    lv_obj_set_pos(s_record_list.list_cont, 0, UI_TITLE_BAR_H);
    lv_obj_set_size(s_record_list.list_cont, LV_HOR_RES,
                    LV_VER_RES - UI_TITLE_BAR_H);
    lv_obj_set_style_bg_opa(s_record_list.list_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_ver(s_record_list.list_cont, 12, 0);
    lv_obj_set_style_pad_hor(s_record_list.list_cont,
                             (LV_HOR_RES - RECORD_LIST_ITEM_W) / 2, 0);
    lv_obj_set_style_pad_row(s_record_list.list_cont, 10, 0);
    lv_obj_set_style_pad_column(s_record_list.list_cont, 0, 0);
    lv_obj_set_style_border_width(s_record_list.list_cont, 0, 0);
    lv_obj_set_flex_flow(s_record_list.list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_record_list.list_cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_record_list.list_cont, LV_SCROLLBAR_MODE_OFF);
}

/**
 * @brief Build the in-screen playback overlay container and its widgets
 * @return none
 */
STATIC VOID_T __record_list_build_play_container(VOID_T)
{
    lv_obj_t *icon;

    s_record_list.play_cont = lv_obj_create(s_record_list.scr);
    lv_obj_remove_style_all(s_record_list.play_cont);
    lv_obj_set_pos(s_record_list.play_cont, 0, UI_TITLE_BAR_H);
    lv_obj_set_size(s_record_list.play_cont, LV_HOR_RES,
                    LV_VER_RES - UI_TITLE_BAR_H);
    lv_obj_set_style_bg_opa(s_record_list.play_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_record_list.play_cont, 0, 0);
    lv_obj_set_style_border_width(s_record_list.play_cont, 0, 0);
    lv_obj_set_scrollbar_mode(s_record_list.play_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_record_list.play_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_record_list.play_cont, LV_OBJ_FLAG_HIDDEN);

    /* Player card */
    s_record_list.play_card = lv_obj_create(s_record_list.play_cont);
    lv_obj_remove_style_all(s_record_list.play_card);
    lv_obj_set_size(s_record_list.play_card, RECORD_LIST_PLAY_CARD_W, RECORD_LIST_PLAY_CARD_H);
    lv_obj_align(s_record_list.play_card, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_radius(s_record_list.play_card, RECORD_LIST_PLAY_CARD_RADIUS, 0);
    lv_obj_set_style_bg_color(s_record_list.play_card,
                              lv_color_hex(RECORD_LIST_PLAY_CARD_BG), 0);
    lv_obj_set_style_bg_opa(s_record_list.play_card, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_record_list.play_card, 16, 0);
    lv_obj_set_style_border_width(s_record_list.play_card, 0, 0);
    lv_obj_set_scrollbar_mode(s_record_list.play_card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_record_list.play_card, LV_OBJ_FLAG_SCROLLABLE);

    /* Title */
    s_record_list.play_title_lbl = lv_label_create(s_record_list.play_card);
    lv_label_set_text(s_record_list.play_title_lbl, "录音");
    lv_obj_set_style_text_font(s_record_list.play_title_lbl,
                               &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(s_record_list.play_title_lbl, lv_color_white(), 0);
    lv_obj_set_pos(s_record_list.play_title_lbl, 0, 0);

    /* Transcribe-status badge — right of "录音", auto-fits text. */
    s_record_list.play_status_badge =
        __record_list_make_status_badge(s_record_list.play_card,
                                        &s_record_list.play_status_lbl);
    __record_list_apply_status_badge(s_record_list.play_status_lbl, -1);
    lv_obj_align_to(s_record_list.play_status_badge,
                    s_record_list.play_title_lbl,
                    LV_ALIGN_OUT_RIGHT_MID,
                    RECORD_LIST_STATUS_BADGE_GAP, 0);

    /* Collapse / back-to-list button (reuses list-row delete style) */
    s_record_list.play_collapse_btn = lv_btn_create(s_record_list.play_card);
    lv_obj_remove_style_all(s_record_list.play_collapse_btn);
    lv_obj_set_size(s_record_list.play_collapse_btn,
                    RECORD_LIST_DELETE_BTN_W, RECORD_LIST_DELETE_BTN_H);
    lv_obj_align(s_record_list.play_collapse_btn, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_radius(s_record_list.play_collapse_btn,
                            RECORD_LIST_DELETE_BTN_RADIUS, 0);
    lv_obj_set_style_bg_opa(s_record_list.play_collapse_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_record_list.play_collapse_btn,
                              lv_color_hex(RECORD_LIST_DELETE_BTN_BG), 0);
    lv_obj_set_style_border_width(s_record_list.play_collapse_btn, 0, 0);
    lv_obj_add_event_cb(s_record_list.play_collapse_btn, __record_list_play_back_cb,
                        LV_EVENT_CLICKED, NULL);

    lv_obj_t *collapse_lbl = lv_label_create(s_record_list.play_collapse_btn);
    lv_obj_remove_style_all(collapse_lbl);
    lv_label_set_text(collapse_lbl, "收起");
    lv_obj_center(collapse_lbl);
    lv_obj_set_style_text_font(collapse_lbl,
                               &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(collapse_lbl,
                                lv_color_hex(RECORD_LIST_DELETE_BTN_FG), 0);

    /* Date */
    s_record_list.play_date_lbl = lv_label_create(s_record_list.play_card);
    lv_label_set_text(s_record_list.play_date_lbl, "");
    lv_obj_set_style_text_font(s_record_list.play_date_lbl,
                               &AlibabaPuHuiTi3_Regular16, 0);
    lv_obj_set_style_text_color(s_record_list.play_date_lbl,
                                lv_color_hex(RECORD_LIST_ITEM_LABEL_HINT), 0);
    lv_obj_set_pos(s_record_list.play_date_lbl, 0, 26);

    /* Slider */
    s_record_list.play_slider = lv_slider_create(s_record_list.play_card);
    lv_obj_set_size(s_record_list.play_slider, RECORD_LIST_SLIDER_W, 6);
    lv_obj_set_pos(s_record_list.play_slider, 0, 54);
    lv_slider_set_range(s_record_list.play_slider, 0, 1);
    lv_slider_set_value(s_record_list.play_slider, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_record_list.play_slider,
                              lv_color_hex(RECORD_LIST_SLIDER_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_record_list.play_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_record_list.play_slider, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_record_list.play_slider,
                              lv_color_hex(RECORD_LIST_SLIDER_FILL),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_record_list.play_slider, LV_OPA_COVER,
                            LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_record_list.play_slider, 3, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_record_list.play_slider, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_record_list.play_slider, 0, LV_PART_KNOB);
    lv_obj_clear_flag(s_record_list.play_slider, LV_OBJ_FLAG_CLICKABLE);

    /* Time labels */
    s_record_list.play_cur_lbl = lv_label_create(s_record_list.play_card);
    lv_label_set_text(s_record_list.play_cur_lbl, "00:00");
    lv_obj_set_style_text_font(s_record_list.play_cur_lbl,
                               &AlibabaPuHuiTi3_Regular16, 0);
    lv_obj_set_style_text_color(s_record_list.play_cur_lbl,
                                lv_color_hex(RECORD_LIST_ITEM_LABEL_HINT), 0);
    lv_obj_set_pos(s_record_list.play_cur_lbl, 0, 66);

    s_record_list.play_total_lbl = lv_label_create(s_record_list.play_card);
    lv_label_set_text(s_record_list.play_total_lbl, "00:00");
    lv_obj_set_style_text_font(s_record_list.play_total_lbl,
                               &AlibabaPuHuiTi3_Regular16, 0);
    lv_obj_set_style_text_color(s_record_list.play_total_lbl,
                                lv_color_hex(RECORD_LIST_ITEM_LABEL_HINT), 0);
    lv_obj_align(s_record_list.play_total_lbl, LV_ALIGN_TOP_RIGHT, 0, 66);

    /* Circular background, mimics the original record_play PNG shape */
    lv_obj_t *play_circle = lv_obj_create(s_record_list.play_card);
    lv_obj_remove_style_all(play_circle);
    lv_obj_set_size(play_circle, RECORD_LIST_PLAY_BTN_SIZE, RECORD_LIST_PLAY_BTN_SIZE);
    lv_obj_set_pos(play_circle, 104, 94);
    lv_obj_set_style_radius(play_circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(play_circle,
                              lv_color_hex(RECORD_LIST_PLAY_CIRCLE_BG), 0);
    lv_obj_set_style_bg_opa(play_circle, LV_OPA_20, 0);
    lv_obj_set_style_border_width(play_circle, 0, 0);
    lv_obj_set_style_pad_all(play_circle, 0, 0);
    lv_obj_set_scrollbar_mode(play_circle, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(play_circle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(play_circle, LV_OBJ_FLAG_CLICKABLE);

    /* Play / pause toggle */
    s_record_list.play_btn = lv_btn_create(play_circle);
    lv_obj_remove_style_all(s_record_list.play_btn);
    lv_obj_set_size(s_record_list.play_btn, RECORD_LIST_PLAY_BTN_SIZE, RECORD_LIST_PLAY_BTN_SIZE);
    lv_obj_set_pos(s_record_list.play_btn, 0, 0);
    lv_obj_set_style_bg_opa(s_record_list.play_btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(s_record_list.play_btn, __record_list_play_toggle_cb,
                        LV_EVENT_CLICKED, NULL);

    s_record_list.play_btn_img = lv_label_create(s_record_list.play_btn);
    lv_label_set_text(s_record_list.play_btn_img, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_font(s_record_list.play_btn_img, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_record_list.play_btn_img,
                                lv_color_hex(RECORD_LIST_PLAY_GLYPH_COLOR), 0);
    lv_obj_center(s_record_list.play_btn_img);

    /* AI / Upload icon button (left of play) */
    s_record_list.play_upload_btn = lv_btn_create(s_record_list.play_card);
    lv_obj_remove_style_all(s_record_list.play_upload_btn);
    lv_obj_set_size(s_record_list.play_upload_btn, RECORD_LIST_PLAY_BTN_SIZE,
                    RECORD_LIST_PLAY_BTN_SIZE);
    lv_obj_set_pos(s_record_list.play_upload_btn, 42, 94);
    lv_obj_set_style_bg_opa(s_record_list.play_upload_btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(s_record_list.play_upload_btn, __record_list_play_upload_cb,
                        LV_EVENT_CLICKED, NULL);

    s_record_list.play_upload_icon = lv_img_create(s_record_list.play_upload_btn);
    lv_img_set_src(s_record_list.play_upload_icon, &icon_ai_on);
    lv_obj_center(s_record_list.play_upload_icon);

#if 0 /* rewind/forward: reserved for future re-enable */
    /* Rewind 15s */
    s_record_list.play_back_btn = lv_btn_create(s_record_list.play_card);
    lv_obj_remove_style_all(s_record_list.play_back_btn);
    lv_obj_set_size(s_record_list.play_back_btn, RECORD_LIST_NAV_BTN_SIZE,
                    RECORD_LIST_NAV_BTN_SIZE);
    lv_obj_set_pos(s_record_list.play_back_btn, 58, 101);
    lv_obj_set_style_bg_opa(s_record_list.play_back_btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(s_record_list.play_back_btn, __record_list_play_rewind_cb,
                        LV_EVENT_CLICKED, NULL);

    /* TODO: replace with fast_back.png */
    lv_obj_t *rewind_lbl = lv_label_create(s_record_list.play_back_btn);
    lv_label_set_text(rewind_lbl, LV_SYMBOL_PREV);
    lv_obj_set_style_text_color(rewind_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(rewind_lbl,
                               &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_center(rewind_lbl);

    /* Forward 15s */
    s_record_list.play_fwd_btn = lv_btn_create(s_record_list.play_card);
    lv_obj_remove_style_all(s_record_list.play_fwd_btn);
    lv_obj_set_size(s_record_list.play_fwd_btn, RECORD_LIST_NAV_BTN_SIZE,
                    RECORD_LIST_NAV_BTN_SIZE);
    lv_obj_set_pos(s_record_list.play_fwd_btn, 164, 101);
    lv_obj_set_style_bg_opa(s_record_list.play_fwd_btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(s_record_list.play_fwd_btn, __record_list_play_forward_cb,
                        LV_EVENT_CLICKED, NULL);

    /* TODO: replace with fast_forward.png */
    lv_obj_t *fwd_lbl = lv_label_create(s_record_list.play_fwd_btn);
    lv_label_set_text(fwd_lbl, LV_SYMBOL_NEXT);
    lv_obj_set_style_text_color(fwd_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(fwd_lbl,
                               &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_center(fwd_lbl);
#endif /* rewind/forward */

    /* Delete */
    s_record_list.play_delete_btn = lv_btn_create(s_record_list.play_card);
    lv_obj_remove_style_all(s_record_list.play_delete_btn);
    lv_obj_set_size(s_record_list.play_delete_btn, RECORD_LIST_DEL_ICON_SIZE,
                    RECORD_LIST_DEL_ICON_SIZE);
    lv_obj_set_pos(s_record_list.play_delete_btn, 174, 102);
    lv_obj_set_style_bg_opa(s_record_list.play_delete_btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(s_record_list.play_delete_btn, __record_list_play_delete_cb,
                        LV_EVENT_CLICKED, NULL);

    icon = lv_img_create(s_record_list.play_delete_btn);
    lv_img_set_src(icon, &icon_delete);
    lv_obj_center(icon);

    /* Upload progress overlay (hidden until upload begins) */
    s_record_list.upload_cont = lv_obj_create(s_record_list.play_cont);
    lv_obj_remove_style_all(s_record_list.upload_cont);
    lv_obj_set_size(s_record_list.upload_cont, RECORD_LIST_PLAY_CARD_W, 36);
    lv_obj_align(s_record_list.upload_cont, LV_ALIGN_TOP_MID, 0,
                 RECORD_LIST_PLAY_CARD_H + 16);
    lv_obj_set_style_radius(s_record_list.upload_cont, 12, 0);
    lv_obj_set_style_bg_opa(s_record_list.upload_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_record_list.upload_cont, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_hor(s_record_list.upload_cont, 16, 0);
    lv_obj_set_style_border_width(s_record_list.upload_cont, 0, 0);
    lv_obj_set_scrollbar_mode(s_record_list.upload_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_record_list.upload_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_record_list.upload_cont, LV_OBJ_FLAG_HIDDEN);

    s_record_list.upload_bar = lv_bar_create(s_record_list.upload_cont);
    lv_obj_set_size(s_record_list.upload_bar, RECORD_LIST_UPLOAD_BAR_W, RECORD_LIST_UPLOAD_BAR_H);
    lv_obj_align(s_record_list.upload_bar, LV_ALIGN_LEFT_MID, 0, 0);
    lv_bar_set_range(s_record_list.upload_bar, 0, 100);
    lv_bar_set_value(s_record_list.upload_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_record_list.upload_bar, lv_color_hex(0x000000),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_record_list.upload_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_record_list.upload_bar, RECORD_LIST_UPLOAD_BAR_RADIUS,
                            LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_record_list.upload_bar, lv_color_hex(RECORD_LIST_UPLOAD_FILL),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_record_list.upload_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_record_list.upload_bar, RECORD_LIST_UPLOAD_BAR_RADIUS,
                            LV_PART_INDICATOR);

    s_record_list.upload_pct_lbl = lv_label_create(s_record_list.upload_cont);
    lv_label_set_text(s_record_list.upload_pct_lbl, "0%");
    lv_obj_set_style_text_font(s_record_list.upload_pct_lbl,
                               &AlibabaPuHuiTi3_Regular16, 0);
    lv_obj_set_style_text_color(s_record_list.upload_pct_lbl,
                                lv_color_hex(RECORD_LIST_UPLOAD_FILL), 0);
    lv_obj_align(s_record_list.upload_pct_lbl, LV_ALIGN_RIGHT_MID, 0, 0);

    __record_list_build_reading_container();
}

/**
 * @brief Build the reading-card overlay (card + side arrows + sub-views)
 * @return none
 * @note Hooked from __record_list_build_play_container so the reading
 *       card and side arrows live inside play_cont and inherit the same
 *       hidden-on-list-view lifecycle. The card itself starts hidden;
 *       __record_list_show_play_view drives the per-item show.
 */
STATIC VOID_T __record_list_build_reading_container(VOID_T)
{
    UINT16_T i;

    /* Side-arrow background buttons (parent = play_cont so they sit in
     * the gutters left/right of the card, not inside the card). */
    s_record_list.read_left_btn = lv_btn_create(s_record_list.play_cont);
    lv_obj_remove_style_all(s_record_list.read_left_btn);
    lv_obj_set_size(s_record_list.read_left_btn,
                    RECORD_LIST_READ_SIDE_BTN_W,
                    RECORD_LIST_READ_SIDE_BTN_H);
    lv_obj_align(s_record_list.read_left_btn, LV_ALIGN_TOP_LEFT,
                 0, RECORD_LIST_READ_CARD_Y);
    lv_obj_set_style_bg_opa(s_record_list.read_left_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_record_list.read_left_btn, 0, 0);
    lv_obj_add_event_cb(s_record_list.read_left_btn, __record_list_read_left_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_record_list.read_left_btn, LV_OBJ_FLAG_HIDDEN);

    s_record_list.read_left_glyph = lv_label_create(s_record_list.read_left_btn);
    lv_label_set_text(s_record_list.read_left_glyph, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(s_record_list.read_left_glyph,
                               &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_record_list.read_left_glyph,
                                lv_color_hex(RECORD_LIST_READ_SIDE_BTN_FG), 0);
    lv_obj_center(s_record_list.read_left_glyph);

    s_record_list.read_right_btn = lv_btn_create(s_record_list.play_cont);
    lv_obj_remove_style_all(s_record_list.read_right_btn);
    lv_obj_set_size(s_record_list.read_right_btn,
                    RECORD_LIST_READ_SIDE_BTN_W,
                    RECORD_LIST_READ_SIDE_BTN_H);
    lv_obj_align(s_record_list.read_right_btn, LV_ALIGN_TOP_RIGHT,
                 0, RECORD_LIST_READ_CARD_Y);
    lv_obj_set_style_bg_opa(s_record_list.read_right_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_record_list.read_right_btn, 0, 0);
    lv_obj_add_event_cb(s_record_list.read_right_btn, __record_list_read_right_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_record_list.read_right_btn, LV_OBJ_FLAG_HIDDEN);

    s_record_list.read_right_glyph = lv_label_create(s_record_list.read_right_btn);
    lv_label_set_text(s_record_list.read_right_glyph, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(s_record_list.read_right_glyph,
                               &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_record_list.read_right_glyph,
                                lv_color_hex(RECORD_LIST_READ_SIDE_BTN_FG), 0);
    lv_obj_center(s_record_list.read_right_glyph);

    /* Reading card body */
    s_record_list.read_cont = lv_obj_create(s_record_list.play_cont);
    lv_obj_remove_style_all(s_record_list.read_cont);
    lv_obj_set_size(s_record_list.read_cont,
                    RECORD_LIST_READ_CARD_W, RECORD_LIST_READ_CARD_H);
    lv_obj_align(s_record_list.read_cont, LV_ALIGN_TOP_MID, 0,
                 RECORD_LIST_READ_CARD_Y);
    lv_obj_set_style_radius(s_record_list.read_cont,
                            RECORD_LIST_READ_CARD_RADIUS, 0);
    lv_obj_set_style_bg_color(s_record_list.read_cont,
                              lv_color_hex(RECORD_LIST_READ_CARD_BG), 0);
    lv_obj_set_style_bg_opa(s_record_list.read_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_record_list.read_cont,
                             RECORD_LIST_READ_CARD_PAD, 0);
    lv_obj_set_style_border_width(s_record_list.read_cont, 0, 0);
    lv_obj_set_scrollbar_mode(s_record_list.read_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_record_list.read_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_record_list.read_cont, LV_OBJ_FLAG_HIDDEN);

    /* Top-left "返回" button — same style as play_collapse_btn */
    s_record_list.read_back_btn = lv_btn_create(s_record_list.read_cont);
    lv_obj_remove_style_all(s_record_list.read_back_btn);
    lv_obj_set_size(s_record_list.read_back_btn,
                    RECORD_LIST_DELETE_BTN_W, RECORD_LIST_DELETE_BTN_H);
    lv_obj_align(s_record_list.read_back_btn, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_radius(s_record_list.read_back_btn,
                            RECORD_LIST_DELETE_BTN_RADIUS, 0);
    lv_obj_set_style_bg_opa(s_record_list.read_back_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_record_list.read_back_btn,
                              lv_color_hex(RECORD_LIST_DELETE_BTN_BG), 0);
    lv_obj_set_style_border_width(s_record_list.read_back_btn, 0, 0);
    lv_obj_add_event_cb(s_record_list.read_back_btn, __record_list_read_back_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_record_list.read_back_btn, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *back_lbl = lv_label_create(s_record_list.read_back_btn);
    lv_obj_remove_style_all(back_lbl);
    lv_label_set_text(back_lbl, "返回");
    lv_obj_center(back_lbl);
    lv_obj_set_style_text_font(back_lbl,
                               &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(back_lbl,
                                lv_color_hex(RECORD_LIST_DELETE_BTN_FG), 0);

    /* Top-right page number label */
    s_record_list.read_page_lbl = lv_label_create(s_record_list.read_cont);
    lv_label_set_text(s_record_list.read_page_lbl, "");
    lv_obj_set_style_text_font(s_record_list.read_page_lbl,
                               &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(s_record_list.read_page_lbl, lv_color_white(), 0);
    lv_obj_align(s_record_list.read_page_lbl, LV_ALIGN_TOP_RIGHT, 0, 4);
    lv_obj_add_flag(s_record_list.read_page_lbl, LV_OBJ_FLAG_HIDDEN);

    /* "暂无文件" hint, centered when status != 1 */
    s_record_list.read_no_file_lbl = lv_label_create(s_record_list.read_cont);
    lv_label_set_text(s_record_list.read_no_file_lbl, "暂无文件");
    lv_obj_set_style_text_font(s_record_list.read_no_file_lbl,
                               &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(s_record_list.read_no_file_lbl,
                                lv_color_hex(RECORD_LIST_READ_NO_FILE_FG), 0);
    lv_obj_align(s_record_list.read_no_file_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_record_list.read_no_file_lbl, LV_OBJ_FLAG_HIDDEN);

    /* Choice container (1-2 buttons, vertically stacked) */
    s_record_list.read_choice_cont = lv_obj_create(s_record_list.read_cont);
    lv_obj_remove_style_all(s_record_list.read_choice_cont);
    lv_obj_set_size(s_record_list.read_choice_cont,
                    RECORD_LIST_READ_TEXT_W, RECORD_LIST_READ_TEXT_AREA_H);
    lv_obj_align(s_record_list.read_choice_cont, LV_ALIGN_TOP_LEFT, 0,
                 RECORD_LIST_READ_TEXT_AREA_Y);
    lv_obj_set_style_bg_opa(s_record_list.read_choice_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_record_list.read_choice_cont, 0, 0);
    lv_obj_set_style_border_width(s_record_list.read_choice_cont, 0, 0);
    lv_obj_set_flex_flow(s_record_list.read_choice_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_record_list.read_choice_cont,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_record_list.read_choice_cont,
                             RECORD_LIST_READ_CHOICE_GAP, 0);
    lv_obj_set_scrollbar_mode(s_record_list.read_choice_cont,
                              LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_record_list.read_choice_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_record_list.read_choice_cont, LV_OBJ_FLAG_HIDDEN);

    for (i = 0; i < 2; i++) {
        s_record_list.read_choice_btn[i] =
            lv_btn_create(s_record_list.read_choice_cont);
        lv_obj_remove_style_all(s_record_list.read_choice_btn[i]);
        lv_obj_set_size(s_record_list.read_choice_btn[i],
                        RECORD_LIST_READ_CHOICE_BTN_W,
                        RECORD_LIST_READ_CHOICE_BTN_H);
        lv_obj_set_style_radius(s_record_list.read_choice_btn[i],
                                RECORD_LIST_DELETE_BTN_RADIUS, 0);
        lv_obj_set_style_bg_opa(s_record_list.read_choice_btn[i],
                                LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(s_record_list.read_choice_btn[i],
                                  lv_color_hex(RECORD_LIST_DELETE_BTN_BG), 0);
        lv_obj_set_style_border_width(s_record_list.read_choice_btn[i], 0, 0);
        lv_obj_add_event_cb(s_record_list.read_choice_btn[i],
                            __record_list_read_choice_cb,
                            LV_EVENT_CLICKED,
                            (VOID_T *)(intptr_t)i);

        s_record_list.read_choice_lbl[i] =
            lv_label_create(s_record_list.read_choice_btn[i]);
        lv_obj_remove_style_all(s_record_list.read_choice_lbl[i]);
        lv_label_set_text(s_record_list.read_choice_lbl[i],
                          (i == 0) ? "转写结果" : "总结结果");
        lv_obj_center(s_record_list.read_choice_lbl[i]);
        lv_obj_set_style_text_font(s_record_list.read_choice_lbl[i],
                                   &AlibabaPuHuiTi3_Regular18_Static, 0);
        lv_obj_set_style_text_color(s_record_list.read_choice_lbl[i],
                                    lv_color_hex(RECORD_LIST_DELETE_BTN_FG), 0);
    }

    /* Streaming text label */
    s_record_list.read_text_lbl = lv_label_create(s_record_list.read_cont);
    lv_label_set_text(s_record_list.read_text_lbl, "");
    lv_obj_set_size(s_record_list.read_text_lbl,
                    RECORD_LIST_READ_TEXT_W, RECORD_LIST_READ_PAGE_PIXEL_H);
    lv_obj_align(s_record_list.read_text_lbl, LV_ALIGN_TOP_LEFT, 0,
                 RECORD_LIST_READ_TEXT_AREA_Y);
    lv_obj_set_style_text_font(s_record_list.read_text_lbl,
                               &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(s_record_list.read_text_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_line_space(s_record_list.read_text_lbl, 0, 0);
    lv_label_set_long_mode(s_record_list.read_text_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(s_record_list.read_text_lbl, LV_OBJ_FLAG_HIDDEN);
}

/* ===========================================================================
 * Reading-card state machine + streaming paginator
 * =========================================================================== */

/**
 * @brief Close the active reading file handle (no-op if none)
 * @return none
 */
STATIC VOID_T __record_list_reading_close_file(VOID_T)
{
    if (s_reading.fp != NULL) {
        ui_record_runtime_close_file(s_reading.fp);
        s_reading.fp = NULL;
    }
    s_reading.file_size = 0;
    s_reading.cur_offset = 0;
    s_reading.cur_page_index = 0;
    s_reading.back_top = 0;
    s_reading.at_last_page = FALSE;
    s_reading.cur_page_bytes = 0;
}

/**
 * @brief Re-evaluate enabled state of the left/right side arrows
 * @return none
 * @note  Only TEXT_VIEWING enables arrows; even there, left is disabled at
 *        the first page and right is disabled once the paginator has
 *        rendered the EOF chunk.
 */
STATIC VOID_T __record_list_reading_apply_side_btn_state(VOID_T)
{
    BOOL_T left_en = FALSE;
    BOOL_T right_en = FALSE;

    if (s_reading.view == READING_VIEW_TEXT_VIEWING) {
        left_en  = (s_reading.back_top > 0) ? TRUE : FALSE;
        right_en = (s_reading.at_last_page == FALSE) ? TRUE : FALSE;
    }
    __record_list_apply_btn_enabled(s_record_list.read_left_btn,
                                    s_record_list.read_left_glyph,
                                    RECORD_LIST_READ_SIDE_BTN_FG,
                                    RECORD_LIST_READ_SIDE_BTN_DISABLED_FG,
                                    left_en);
    __record_list_apply_btn_enabled(s_record_list.read_right_btn,
                                    s_record_list.read_right_glyph,
                                    RECORD_LIST_READ_SIDE_BTN_FG,
                                    RECORD_LIST_READ_SIDE_BTN_DISABLED_FG,
                                    right_en);
}

/**
 * @brief Show the "暂无文件" view (status != 1)
 * @return none
 */
STATIC VOID_T __record_list_reading_show_no_file(VOID_T)
{
    __record_list_reading_close_file();
    s_reading.view = READING_VIEW_NO_FILE;

    if (s_record_list.read_no_file_lbl != NULL) {
        lv_obj_clear_flag(s_record_list.read_no_file_lbl, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_record_list.read_choice_cont != NULL) {
        lv_obj_add_flag(s_record_list.read_choice_cont, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_record_list.read_back_btn != NULL) {
        lv_obj_add_flag(s_record_list.read_back_btn, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_record_list.read_page_lbl != NULL) {
        lv_obj_add_flag(s_record_list.read_page_lbl, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_record_list.read_text_lbl != NULL) {
        lv_obj_add_flag(s_record_list.read_text_lbl, LV_OBJ_FLAG_HIDDEN);
    }
    __record_list_reading_apply_side_btn_state();
}

/**
 * @brief Show the 1- or 2-button selection view (status == 1)
 * @return none
 * @note  Per ADR-0003 the runtime always produces a transcribe file before a
 *        summary file, so the layout is: btn[0] = transcribe (if present),
 *        btn[1] = summary (if present). When only one of them exists the
 *        other slot is hidden and flex-centering keeps the layout balanced.
 */
STATIC VOID_T __record_list_reading_show_button_select(VOID_T)
{
    BOOL_T has_t = FALSE;
    BOOL_T has_s = FALSE;

    __record_list_reading_close_file();
    s_reading.view = READING_VIEW_BUTTON_SELECT;

    if (s_record_list_play_item_valid == TRUE) {
        has_t = ui_record_runtime_has_file(s_record_list_play_item.id,
                                           UI_REC_FILE_TRANSCRIBE);
        has_s = ui_record_runtime_has_file(s_record_list_play_item.id,
                                           UI_REC_FILE_SUMMARY);
    }

    if (has_t == FALSE && has_s == FALSE) {
        __record_list_reading_show_no_file();
        return;
    }

    if (s_record_list.read_choice_btn[0] != NULL) {
        if (has_t == TRUE) {
            lv_label_set_text(s_record_list.read_choice_lbl[0], "转写结果");
            lv_obj_clear_flag(s_record_list.read_choice_btn[0],
                              LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_record_list.read_choice_btn[0], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_record_list.read_choice_btn[1] != NULL) {
        if (has_s == TRUE) {
            lv_label_set_text(s_record_list.read_choice_lbl[1], "总结结果");
            lv_obj_clear_flag(s_record_list.read_choice_btn[1],
                              LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_record_list.read_choice_btn[1], LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_record_list.read_choice_cont != NULL) {
        lv_obj_clear_flag(s_record_list.read_choice_cont, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_record_list.read_no_file_lbl != NULL) {
        lv_obj_add_flag(s_record_list.read_no_file_lbl, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_record_list.read_back_btn != NULL) {
        lv_obj_add_flag(s_record_list.read_back_btn, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_record_list.read_page_lbl != NULL) {
        lv_obj_add_flag(s_record_list.read_page_lbl, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_record_list.read_text_lbl != NULL) {
        lv_obj_add_flag(s_record_list.read_text_lbl, LV_OBJ_FLAG_HIDDEN);
    }
    __record_list_reading_apply_side_btn_state();
}

/**
 * @brief Render the page that starts at s_reading.cur_offset
 * @return none
 *
 * Streaming paginator. Reads up to RECORD_LIST_READ_CHUNK_BYTES into
 * s_reading.chunk_buf, then binary-searches the largest UTF-8-aligned
 * prefix that fits a page (RECORD_LIST_READ_PAGE_PIXEL_H tall) measured
 * with lv_text_get_size against the actual font + label width. The
 * resulting byte count goes into cur_page_bytes so the right-arrow handler
 * can advance cur_offset by exactly that amount.
 *
 * "At last page" is true only when the chunk read came back short (=> EOF
 * reached on the kernel side) AND the entire chunk fit on this page.
 * Otherwise the user can still page forward into more bytes.
 */
STATIC VOID_T __record_list_reading_render_page(VOID_T)
{
    INT_T n;
    UINT32_T lo, hi, fit;
    CHAR_T saved;
    lv_point_t sz;
    CHAR_T page_buf[16];

    if (s_record_list.read_text_lbl == NULL || s_reading.fp == NULL) {
        return;
    }

    n = ui_record_runtime_read_at(s_reading.fp,
                                  s_reading.cur_offset,
                                  s_reading.chunk_buf,
                                  RECORD_LIST_READ_CHUNK_BYTES);
    if (n <= 0) {
        s_reading.at_last_page = TRUE;
        s_reading.cur_page_bytes = 0;
        lv_label_set_text(s_record_list.read_text_lbl, "");
    } else {
        s_reading.chunk_buf[n] = '\0';

        lo = 1;
        hi = (UINT32_T)n;
        fit = 0;
        while (lo <= hi) {
            UINT32_T orig_mid = (lo + hi) / 2;
            UINT32_T mid = orig_mid;
            /* Snap mid back to a UTF-8 character boundary so we never cut
             * a multi-byte code point in half. */
            while (mid > 0 &&
                   ((UINT8_T)s_reading.chunk_buf[mid] & 0xC0) == 0x80) {
                mid--;
            }
            if (mid == 0) {
                break;
            }
            saved = s_reading.chunk_buf[mid];
            s_reading.chunk_buf[mid] = '\0';

            lv_txt_get_size(&sz, s_reading.chunk_buf,
                            &AlibabaPuHuiTi3_Regular18_Static,
                            0, 0,
                            RECORD_LIST_READ_TEXT_W,
                            LV_TEXT_FLAG_NONE);
            s_reading.chunk_buf[mid] = saved;

            if (sz.y <= RECORD_LIST_READ_PAGE_PIXEL_H) {
                fit = mid;
                /* Skip past the entire continuation-byte run that was
                 * absorbed by the alignment, otherwise lo can stall when
                 * mid+1 still falls inside the same multi-byte code point
                 * and the next iteration aligns back to the same fit. */
                lo = orig_mid + 1;
            } else {
                hi = mid - 1;
            }
        }

        if (fit == 0) {
            /* Pathological: a single character is taller than the page.
             * Force-feed one full UTF-8 code point so we always make
             * forward progress and do not deadlock the right arrow. */
            fit = 1;
            while (fit < (UINT32_T)n &&
                   ((UINT8_T)s_reading.chunk_buf[fit] & 0xC0) == 0x80) {
                fit++;
            }
        }

        s_reading.at_last_page =
            ((UINT32_T)n < RECORD_LIST_READ_CHUNK_BYTES &&
             fit >= (UINT32_T)n) ? TRUE : FALSE;

        s_reading.chunk_buf[fit] = '\0';
        lv_label_set_text(s_record_list.read_text_lbl, s_reading.chunk_buf);
        s_reading.cur_page_bytes = fit;
    }

    if (s_record_list.read_page_lbl != NULL) {
        snprintf(page_buf, sizeof(page_buf), "第 %u 页",
                 (unsigned)(s_reading.cur_page_index + 1));
        lv_label_set_text(s_record_list.read_page_lbl, page_buf);
    }
    __record_list_reading_apply_side_btn_state();
}

/**
 * @brief Switch to the streaming text view for the chosen file kind
 * @param[in] kind UI_REC_FILE_TRANSCRIBE or UI_REC_FILE_SUMMARY
 * @return none
 */
STATIC VOID_T __record_list_reading_show_text_view(UI_REC_FILE_KIND_T kind)
{
    TUYA_FILE fp = NULL;
    UINT32_T size = 0;
    OPERATE_RET rt = OPRT_OK;

    if (s_record_list_play_item_valid == FALSE) {
        __record_list_reading_show_no_file();
        return;
    }

    __record_list_reading_close_file();

    rt = ui_record_runtime_open_file(s_record_list_play_item.id,
                                     kind, &fp, &size);
    if (rt != OPRT_OK || fp == NULL) {
        PR_ERR("reading: open_file failed kind=%d rt=%d", (int)kind, rt);
        __record_list_reading_show_no_file();
        return;
    }

    s_reading.kind = kind;
    s_reading.fp = fp;
    s_reading.file_size = size;
    s_reading.cur_offset = 0;
    s_reading.cur_page_index = 0;
    s_reading.back_top = 0;
    s_reading.at_last_page = FALSE;
    s_reading.cur_page_bytes = 0;
    s_reading.view = READING_VIEW_TEXT_VIEWING;

    if (s_record_list.read_choice_cont != NULL) {
        lv_obj_add_flag(s_record_list.read_choice_cont, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_record_list.read_no_file_lbl != NULL) {
        lv_obj_add_flag(s_record_list.read_no_file_lbl, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_record_list.read_back_btn != NULL) {
        lv_obj_clear_flag(s_record_list.read_back_btn, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_record_list.read_page_lbl != NULL) {
        lv_obj_clear_flag(s_record_list.read_page_lbl, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_record_list.read_text_lbl != NULL) {
        lv_obj_clear_flag(s_record_list.read_text_lbl, LV_OBJ_FLAG_HIDDEN);
    }

    __record_list_reading_render_page();
}

/**
 * @brief Show the reading card + side arrows (visibility wrapper)
 * @return none
 */
STATIC VOID_T __record_list_reading_show_card(VOID_T)
{
    if (s_record_list.read_cont != NULL) {
        lv_obj_clear_flag(s_record_list.read_cont, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_record_list.read_left_btn != NULL) {
        lv_obj_clear_flag(s_record_list.read_left_btn, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_record_list.read_right_btn != NULL) {
        lv_obj_clear_flag(s_record_list.read_right_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief Hide the reading card + side arrows and release the file handle
 * @return none
 */
STATIC VOID_T __record_list_reading_hide_card(VOID_T)
{
    __record_list_reading_close_file();
    if (s_record_list.read_cont != NULL) {
        lv_obj_add_flag(s_record_list.read_cont, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_record_list.read_left_btn != NULL) {
        lv_obj_add_flag(s_record_list.read_left_btn, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_record_list.read_right_btn != NULL) {
        lv_obj_add_flag(s_record_list.read_right_btn, LV_OBJ_FLAG_HIDDEN);
    }
    s_reading.view = READING_VIEW_NO_FILE;
}

/**
 * @brief Pick the right sub-view for the current transcribe_status
 * @param[in] transcribe_status -1/0/1/2 (only 1 unlocks the button view)
 * @return none
 */
STATIC VOID_T __record_list_reading_show_for_status(INT_T transcribe_status)
{
    if (transcribe_status == 1) {
        __record_list_reading_show_button_select();
    } else {
        __record_list_reading_show_no_file();
    }
}

/* ---------------------------------------------------------------------------
 * Reading card event callbacks
 * --------------------------------------------------------------------------- */

/**
 * @brief "返回" button: leave text view, back to button-select view
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __record_list_read_back_cb(lv_event_t *e)
{
    (VOID_T)e;
    if (s_reading.view != READING_VIEW_TEXT_VIEWING) {
        return;
    }
    __record_list_reading_show_button_select();
}

/**
 * @brief Choice button click: open the matching file and switch to text view
 * @param[in] e LVGL event (user_data is 0 for transcribe, 1 for summary)
 * @return none
 */
STATIC VOID_T __record_list_read_choice_cb(lv_event_t *e)
{
    INT_T idx = (INT_T)(intptr_t)lv_event_get_user_data(e);
    UI_REC_FILE_KIND_T kind = (idx == 1) ? UI_REC_FILE_SUMMARY
                                         : UI_REC_FILE_TRANSCRIBE;
    __record_list_reading_show_text_view(kind);
}

/**
 * @brief Left arrow: pop the back-stack and re-render the previous page
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __record_list_read_left_cb(lv_event_t *e)
{
    (VOID_T)e;
    if (s_reading.view != READING_VIEW_TEXT_VIEWING || s_reading.back_top == 0) {
        return;
    }

    s_reading.back_top--;
    s_reading.cur_offset = s_reading.back_stack[s_reading.back_top];
    if (s_reading.cur_page_index > 0) {
        s_reading.cur_page_index--;
    }
    s_reading.at_last_page = FALSE;
    __record_list_reading_render_page();
}

/**
 * @brief Right arrow: push the current offset and advance one page
 * @param[in] e LVGL event
 * @return none
 * @note Stack-overflow drops the oldest entry (single 256-element shift).
 *       The user keeps the most recent 256 prev-pages; older ones are no
 *       longer reachable, which matches the "infinite forward, bounded
 *       backward" intent.
 */
STATIC VOID_T __record_list_read_right_cb(lv_event_t *e)
{
    (VOID_T)e;
    if (s_reading.view != READING_VIEW_TEXT_VIEWING ||
        s_reading.at_last_page == TRUE ||
        s_reading.cur_page_bytes == 0) {
        return;
    }

    if (s_reading.back_top >= RECORD_LIST_READ_BACK_STACK_DEPTH) {
        memmove(&s_reading.back_stack[0], &s_reading.back_stack[1],
                (RECORD_LIST_READ_BACK_STACK_DEPTH - 1) * sizeof(UINT32_T));
        s_reading.back_top = RECORD_LIST_READ_BACK_STACK_DEPTH - 1;
    }
    s_reading.back_stack[s_reading.back_top] = s_reading.cur_offset;
    s_reading.back_top++;

    s_reading.cur_offset += s_reading.cur_page_bytes;
    s_reading.cur_page_index++;
    __record_list_reading_render_page();
}

/**
 * @brief Create one row inside the list container for the given item
 * @param[in] it pointer to the view item to render (must not be NULL)
 * @return none
 */
STATIC VOID_T __record_list_build_row(CONST UI_RECORD_ITEM_T *it)
{
    lv_obj_t *row;
    lv_obj_t *name_lbl;
    lv_obj_t *date_lbl;
    lv_obj_t *del_btn;
    lv_obj_t *del_lbl;
    lv_obj_t *status_badge;
    lv_obj_t *status_lbl;

    if (it == NULL) {
        return;
    }

    row = lv_obj_create(s_record_list.list_cont);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, RECORD_LIST_ITEM_W, RECORD_LIST_ITEM_H);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(row, RECORD_LIST_ITEM_RADIUS, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(RECORD_LIST_ITEM_BG), 0);
    lv_obj_set_style_pad_left(row, 16, 0);
    lv_obj_set_style_pad_right(row, 16, 0);
    lv_obj_set_style_pad_top(row, 12, 0);
    lv_obj_set_style_pad_bottom(row, 12, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(row, __record_list_item_clicked_cb, LV_EVENT_CLICKED,
                        (VOID_T *)(intptr_t)it->id);

    name_lbl = lv_label_create(row);
    lv_obj_remove_style_all(name_lbl);
    lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);
    lv_label_set_text(name_lbl, it->name[0] ? it->name : "录音");
    lv_obj_set_pos(name_lbl, 0, 0);
    lv_obj_set_style_text_font(name_lbl,
                               &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(name_lbl, lv_color_white(), 0);

    /* Transcribe-status badge — right of "录音", auto-fits text. */
    status_badge = __record_list_make_status_badge(row, &status_lbl);
    __record_list_apply_status_badge(status_lbl, it->transcribe_status);
    lv_obj_align_to(status_badge, name_lbl, LV_ALIGN_OUT_RIGHT_MID,
                    RECORD_LIST_STATUS_BADGE_GAP, 0);

    date_lbl = lv_label_create(row);
    lv_obj_remove_style_all(date_lbl);
    lv_label_set_long_mode(date_lbl, LV_LABEL_LONG_DOT);
    lv_label_set_text(date_lbl, it->datetime_str);
    lv_obj_set_pos(date_lbl, 0, 28);
    lv_obj_set_style_text_font(date_lbl,
                               &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(date_lbl,
                                lv_color_hex(RECORD_LIST_ITEM_LABEL_HINT), 0);

    del_btn = lv_btn_create(row);
    lv_obj_remove_style_all(del_btn);
    lv_obj_set_size(del_btn, RECORD_LIST_DELETE_BTN_W, RECORD_LIST_DELETE_BTN_H);
    lv_obj_align(del_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(del_btn, RECORD_LIST_DELETE_BTN_RADIUS, 0);
    lv_obj_set_style_bg_opa(del_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(del_btn, lv_color_hex(RECORD_LIST_DELETE_BTN_BG), 0);
    lv_obj_set_style_border_width(del_btn, 0, 0);
    lv_obj_add_event_cb(del_btn, __record_list_item_delete_cb, LV_EVENT_CLICKED,
                        (VOID_T *)(intptr_t)it->id);

    del_lbl = lv_label_create(del_btn);
    lv_obj_remove_style_all(del_lbl);
    lv_label_set_text(del_lbl, "删除");
    lv_obj_center(del_lbl);
    lv_obj_set_style_text_font(del_lbl,
                               &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(del_lbl, lv_color_hex(RECORD_LIST_DELETE_BTN_FG), 0);
}

/**
 * @brief Rebuild the list view based on the current internal linked list
 * @return none
 */
STATIC VOID_T __record_list_rebuild_list(VOID_T)
{
    LIST_HEAD *pos = NULL;

    if (s_record_list.list_cont == NULL) {
        return;
    }

    __ui_list_ensure_inited();

    lv_obj_clean(s_record_list.list_cont);
    s_record_list.empty_lbl = NULL;

    if (s_ui_list_count == 0) {
        s_record_list.empty_lbl = lv_label_create(s_record_list.list_cont);
        lv_obj_remove_style_all(s_record_list.empty_lbl);
        lv_label_set_text(s_record_list.empty_lbl, "暂无录音");
        lv_obj_set_style_text_font(s_record_list.empty_lbl,
                                   &AlibabaPuHuiTi3_Regular18_Static, 0);
        lv_obj_set_style_text_color(s_record_list.empty_lbl,
                                    lv_color_hex(RECORD_LIST_ITEM_LABEL_HINT), 0);
        return;
    }

    tuya_list_for_each(pos, &s_ui_list_head) {
        UI_RECORD_LIST_NODE_T *n = tuya_list_entry(pos, UI_RECORD_LIST_NODE_T, list_node);
        if (n == NULL) {
            continue;
        }
        __record_list_build_row(&n->item);
    }
}

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------- */

/**
 * @brief Create the record list screen (lazy)
 * @return none
 */
VOID_T setup_scr_record_list(VOID_T)
{
    if (s_record_list.scr) {
        return;
    }

    memset(&s_record_list, 0, sizeof(s_record_list));

    s_record_list.scr = lv_obj_create(NULL);
    lv_obj_set_size(s_record_list.scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_record_list.scr, lv_color_hex(UI_BG_COLOR_DARK), 0);
    lv_obj_set_style_pad_all(s_record_list.scr, 0, 0);
    lv_obj_set_scrollbar_mode(s_record_list.scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_record_list.scr, LV_OBJ_FLAG_SCROLLABLE);

    __record_list_build_title_bar();
    __record_list_build_list_container();
    __record_list_build_play_container();
    __record_list_rebuild_list();

    ui_control_center_register_gesture(s_record_list.scr);

    lv_obj_update_layout(s_record_list.scr);
}

/**
 * @brief Show the record list screen (creates if needed)
 * @return none
 */
VOID_T ui_record_list_show(VOID_T)
{
    if (s_record_list.scr == NULL) {
        setup_scr_record_list();
    } else {
        /* Refresh items in case they were pushed while the screen was
         * cached but not visible. */
        __record_list_rebuild_list();
        __record_list_show_list_view();
    }

    if (lv_scr_act() != s_record_list.scr) {
        lv_scr_load(s_record_list.scr);
    }
}

/**
 * @brief Hide the record list screen and release per-show resources
 * @return none
 */
VOID_T ui_record_list_hide(VOID_T)
{
    tuya_ai_display_action_post(NULL, 0, TY_DISP_ACT_RECORD_PLAY_STOP);
    __record_list_play_stop_mock();
    s_record_list_play_item_valid = FALSE;
}

/**
 * @brief Get the record list screen object
 * @return record list screen pointer, NULL if not created
 */
lv_obj_t *ui_record_list_get_scr(VOID_T)
{
    return s_record_list.scr;
}

/**
 * @brief Begin replacing the displayed list items
 * @return none
 */
VOID_T ui_record_list_replace_begin(VOID_T)
{
    __ui_list_clear();
}

/**
 * @brief Append one item to the in-progress replacement
 * @param[in] item view fields to copy (must not be NULL)
 * @return OPRT_OK on success or when item is dropped due to internal cap,
 *         OPRT_INVALID_PARM when item is NULL,
 *         OPRT_MALLOC_FAILED when out of memory
 */
OPERATE_RET ui_record_list_replace_push(CONST UI_RECORD_ITEM_T *item)
{
    UI_RECORD_LIST_NODE_T *node = NULL;

    if (item == NULL) {
        return OPRT_INVALID_PARM;
    }

    __ui_list_ensure_inited();

    if (s_ui_list_count >= RECORD_LIST_MAX_ITEMS) {
        return OPRT_OK;
    }

    node = (UI_RECORD_LIST_NODE_T *)tal_malloc(sizeof(*node));
    if (node == NULL) {
        return OPRT_MALLOC_FAILED;
    }
    memset(node, 0, sizeof(*node));
    memcpy(&node->item, item, sizeof(*item));

    /* 同一 entry 的 play card 当前可见时，列表 push 顺带把 play card 的
     * status badge 同步刷新——避免 caller（upload 完成、poll 线程 apply）
     * 各自记得调 set_play_status。 */
    if (s_record_list_play_item_valid &&
        item->id == s_record_list_play_item.id) {
        INT_T old_status = s_record_list_play_item.transcribe_status;
        s_record_list_play_item.transcribe_status = item->transcribe_status;
        if (s_record_list.play_status_lbl != NULL) {
            __record_list_apply_status_badge(s_record_list.play_status_lbl,
                                             item->transcribe_status);
        }
        /* Reading card lives in lockstep with the play card. Any status
         * transition (and even 1→1, in case the file changed under us)
         * resets the reading view so we never keep streaming from a stale
         * fp. The reset is skipped when the reading card is already
         * hidden (e.g. mid-upload). */
        if (s_record_list.read_cont != NULL &&
            !lv_obj_has_flag(s_record_list.read_cont, LV_OBJ_FLAG_HIDDEN)) {
            if (old_status != item->transcribe_status ||
                item->transcribe_status == 1) {
                __record_list_reading_show_for_status(item->transcribe_status);
            }
        }
    }

    tuya_list_add_tail(&node->list_node, &s_ui_list_head);
    s_ui_list_count++;
    return OPRT_OK;
}

/**
 * @brief Commit the in-progress replacement and rebuild the screen
 * @return none
 */
VOID_T ui_record_list_replace_commit(VOID_T)
{
    if (s_record_list.scr != NULL) {
        __record_list_rebuild_list();
    }
}

/**
 * @brief Update the upload progress bar on the playback overlay
 * @param[in] percent progress in [0, 100]
 * @return none
 */
VOID_T ui_record_list_set_upload_progress(UINT8_T percent)
{
    CHAR_T buf[8];
    UINT8_T pct = (percent > 100) ? 100 : percent;

    if (s_record_list.upload_cont == NULL || s_record_list.upload_bar == NULL ||
        s_record_list.upload_pct_lbl == NULL) {
        return;
    }

    if (pct == 0) {
        lv_obj_add_flag(s_record_list.upload_cont, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_record_list.upload_cont, LV_OBJ_FLAG_HIDDEN);
    }

    lv_bar_set_value(s_record_list.upload_bar, pct, LV_ANIM_OFF);
    snprintf(buf, sizeof(buf), "%u%%", (unsigned)pct);
    lv_label_set_text(s_record_list.upload_pct_lbl, buf);

    if (pct >= 100) {
        /* Auto-hide after reaching 100% so the bar does not linger */
        lv_obj_add_flag(s_record_list.upload_cont, LV_OBJ_FLAG_HIDDEN);
        /* Restore reading card. status was just flipped to 0 ("处理中") so
         * the card lands on the "暂无文件" sub-view; once the poll thread
         * sees status=1, replace_push will switch it to BUTTON_SELECT. */
        if (s_record_list_play_item_valid == TRUE) {
            __record_list_reading_show_card();
            __record_list_reading_show_for_status(
                s_record_list_play_item.transcribe_status);
        }
    }
}

/**
 * @brief Refresh the transcribe-status badge on the visible playback card
 * @param[in] transcribe_status new value to render
 * @return none
 * @note Updates the cached play item snapshot so a later list-card rebuild
 *       (driven by ui_record_runtime_refresh_ui_list) sees consistent data,
 *       and immediately re-applies the badge text/color so the user sees
 *       the change while still on the play view. No-op when the play card
 *       isn't valid (e.g. user already collapsed back to list).
 */
VOID_T ui_record_list_set_play_status(INT_T transcribe_status)
{
    if (s_record_list_play_item_valid == FALSE) {
        return;
    }
    s_record_list_play_item.transcribe_status = transcribe_status;

    if (s_record_list.play_status_lbl != NULL) {
        __record_list_apply_status_badge(s_record_list.play_status_lbl,
                                         transcribe_status);
    }
    if (s_record_list.play_status_badge != NULL &&
        s_record_list.play_title_lbl != NULL) {
        lv_obj_align_to(s_record_list.play_status_badge,
                        s_record_list.play_title_lbl,
                        LV_ALIGN_OUT_RIGHT_MID,
                        RECORD_LIST_STATUS_BADGE_GAP, 0);
    }
}
