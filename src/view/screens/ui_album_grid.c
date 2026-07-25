/**
 * @file ui_album_grid.c
 * @brief Album grid (all photos) screen for T5AI_BOARD (320x480)
 *
 * Shows thumbnails in a 3-column grid. Top-right "choose" icon enters
 * selection mode where the user can pick photos and delete them.
 *
 * This file does NOT directly operate album/picture APIs. Thumbnail data is
 * passed in via ui_album_grid_set_thumbs(); delete and close operations are
 * posted as display actions and handled in the action callback layer.
 *
 * @version 1.1
 * @date 2025-04-02
 * @copyright Copyright (c) Tuya Inc.
 */
#include <stdio.h>
#include <string.h>
#include "ui_common.h"

/* ---------------------------------------------------------------------------
 * Font / icon declarations
 * --------------------------------------------------------------------------- */
LV_IMG_DECLARE(icon_choose);

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define ALBUM_GRID_BG_COLOR       0x1A1A1A
#define ALBUM_GRID_CELL_COLOR     0x333333
#define ALBUM_GRID_TOP_Y          12
#define ALBUM_GRID_BTN_SIZE       36
#define ALBUM_GRID_TOP_BAR_H      48
#define ALBUM_GRID_BOT_BAR_H      48
#define ALBUM_GRID_COLS           3
#define ALBUM_GRID_CELL_SIZE      96
#define ALBUM_GRID_GAP            8
#define ALBUM_GRID_PAD            8
#define ALBUM_GRID_CELL_RADIUS    8
#define ALBUM_GRID_CHK_SIZE       22
#define ALBUM_GRID_CHK_OFFSET     6
#define ALBUM_GRID_MAX_PHOTOS     30
#define ALBUM_GRID_MIN_ROWS       2

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef struct {
    lv_obj_t *cell;
    lv_obj_t *canvas;
    lv_obj_t *chk;
    lv_obj_t *chk_label;
    char      filename[WUKONG_PICTURE_NAME_MAX_LEN + 1];
    BOOL_T    selected;
} ALBUM_GRID_ITEM_T;

typedef struct {
    lv_obj_t    *scr;
    /* Top bar */
    lv_obj_t    *back_btn;
    lv_obj_t    *title_lbl;
    lv_obj_t    *choose_btn;
    lv_obj_t    *cancel_btn;
    /* Grid */
    lv_obj_t    *grid_cont;
    ALBUM_GRID_ITEM_T  items[ALBUM_GRID_MAX_PHOTOS];
    uint32_t     item_count;
    /* Bottom bar */
    lv_obj_t    *bottom_bar;
    lv_obj_t    *select_lbl;
    lv_obj_t    *del_btn;
    /* State */
    BOOL_T       select_mode;
    uint32_t     select_count;
} ALBUM_GRID_UI_T;

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC ALBUM_GRID_UI_T s_album_grid = {0};

/* Pending-delete buffer: filled by UI before posting action */
STATIC CHAR_T s_album_grid_pending_delete[ALBUM_GRID_MAX_PHOTOS][WUKONG_PICTURE_NAME_MAX_LEN + 1];
STATIC UINT32_T s_album_grid_pending_count = 0;

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC VOID_T __album_grid_back_cb(lv_event_t *e);
STATIC VOID_T __album_grid_choose_cb(lv_event_t *e);
STATIC VOID_T __album_grid_cancel_cb(lv_event_t *e);
STATIC VOID_T __album_grid_cell_cb(lv_event_t *e);
STATIC VOID_T __album_grid_delete_cb(lv_event_t *e);
STATIC VOID_T __album_grid_enter_select_mode(VOID_T);
STATIC VOID_T __album_grid_exit_select_mode(VOID_T);
STATIC VOID_T __album_grid_update_select_label(VOID_T);
STATIC VOID_T __album_grid_update_checkbox(ALBUM_GRID_ITEM_T *item);
STATIC VOID_T __album_grid_clear_cells(VOID_T);

/* ---------------------------------------------------------------------------
 * Callback implementations
 * --------------------------------------------------------------------------- */

STATIC VOID_T __album_grid_back_cb(lv_event_t *e)
{
    (VOID_T)e;
    tuya_ai_display_action_post(NULL, 0, TY_DISP_ACT_CLOSE_ALBUM_GRID);
}

STATIC VOID_T __album_grid_choose_cb(lv_event_t *e)
{
    (VOID_T)e;
    if (s_album_grid.item_count > 0) {
        __album_grid_enter_select_mode();
    }
}

STATIC VOID_T __album_grid_cancel_cb(lv_event_t *e)
{
    (VOID_T)e;
    __album_grid_exit_select_mode();
}

STATIC VOID_T __album_grid_cell_cb(lv_event_t *e)
{
    if (!s_album_grid.select_mode) {
        return;
    }

    lv_obj_t *cell = lv_event_get_current_target(e);
    uint32_t idx = (uint32_t)(uintptr_t)lv_obj_get_user_data(cell);

    if (idx >= s_album_grid.item_count) {
        return;
    }

    ALBUM_GRID_ITEM_T *item = &s_album_grid.items[idx];
    item->selected = !item->selected;

    if (item->selected) {
        s_album_grid.select_count++;
    } else {
        if (s_album_grid.select_count > 0) {
            s_album_grid.select_count--;
        }
    }

    __album_grid_update_checkbox(item);
    __album_grid_update_select_label();
}

STATIC VOID_T __album_grid_delete_cb(lv_event_t *e)
{
    (VOID_T)e;
    uint32_t i;

    if (s_album_grid.select_count == 0) {
        return;
    }

    /* 1. Save selected filenames to pending buffer */
    s_album_grid_pending_count = 0;
    for (i = 0; i < s_album_grid.item_count; i++) {
        if (s_album_grid.items[i].selected && s_album_grid.items[i].filename[0]) {
            if (s_album_grid_pending_count < ALBUM_GRID_MAX_PHOTOS) {
                strncpy(s_album_grid_pending_delete[s_album_grid_pending_count], s_album_grid.items[i].filename,
                        WUKONG_PICTURE_NAME_MAX_LEN);
                s_album_grid_pending_delete[s_album_grid_pending_count][WUKONG_PICTURE_NAME_MAX_LEN] = '\0';
                s_album_grid_pending_count++;
            }
        }
    }

    /* 2. Immediately remove selected cells from LVGL (visual feedback) */
    for (i = 0; i < s_album_grid.item_count; i++) {
        if (s_album_grid.items[i].selected && s_album_grid.items[i].cell) {
            lv_obj_del(s_album_grid.items[i].cell);
            s_album_grid.items[i].cell = NULL;
            s_album_grid.items[i].canvas = NULL;
            s_album_grid.items[i].chk = NULL;
            s_album_grid.items[i].chk_label = NULL;
        }
    }

    /* 3. Compact items array */
    uint32_t write = 0;
    for (i = 0; i < s_album_grid.item_count; i++) {
        if (s_album_grid.items[i].cell != NULL) {
            if (write != i) {
                s_album_grid.items[write] = s_album_grid.items[i];
                lv_obj_set_user_data(s_album_grid.items[write].cell, (void *)(uintptr_t)write);
            }
            write++;
        }
    }
    for (i = write; i < s_album_grid.item_count; i++) {
        memset(&s_album_grid.items[i], 0, sizeof(ALBUM_GRID_ITEM_T));
    }
    s_album_grid.item_count = write;

    /* 4. Exit select mode */
    __album_grid_exit_select_mode();

    /* 5. Post action for actual album deletion */
    tuya_ai_display_action_post(NULL, 0, TY_DISP_ACT_ALBUM_BATCH_DELETE);
}

/* ---------------------------------------------------------------------------
 * Selection mode helpers
 * --------------------------------------------------------------------------- */

STATIC VOID_T __album_grid_enter_select_mode(VOID_T)
{
    uint32_t i;

    s_album_grid.select_mode = TRUE;
    s_album_grid.select_count = 0;

    /* Toggle top-right buttons */
    lv_obj_add_flag(s_album_grid.choose_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_album_grid.cancel_btn, LV_OBJ_FLAG_HIDDEN);

    /* Show bottom bar and shrink grid */
    lv_obj_clear_flag(s_album_grid.bottom_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(s_album_grid.grid_cont, LV_VER_RES - ALBUM_GRID_TOP_BAR_H - ALBUM_GRID_BOT_BAR_H);

    /* Add checkbox circles to photo cells */
    for (i = 0; i < s_album_grid.item_count; i++) {
        ALBUM_GRID_ITEM_T *item = &s_album_grid.items[i];
        item->selected = FALSE;

        if (item->chk == NULL) {
            item->chk = lv_obj_create(item->cell);
            lv_obj_remove_style_all(item->chk);
            lv_obj_set_size(item->chk, ALBUM_GRID_CHK_SIZE, ALBUM_GRID_CHK_SIZE);
            lv_obj_set_pos(item->chk, ALBUM_GRID_CHK_OFFSET, ALBUM_GRID_CHK_OFFSET);
            lv_obj_set_style_radius(item->chk, ALBUM_GRID_CHK_SIZE / 2, 0);
            lv_obj_set_style_border_width(item->chk, 2, 0);
            lv_obj_set_style_border_color(item->chk, lv_color_white(), 0);
            lv_obj_set_style_bg_opa(item->chk, LV_OPA_30, 0);
            lv_obj_set_style_bg_color(item->chk, lv_color_hex(0x808080), 0);
            lv_obj_clear_flag(item->chk, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_clear_flag(item->chk, LV_OBJ_FLAG_SCROLLABLE);

            item->chk_label = lv_label_create(item->chk);
            lv_label_set_text(item->chk_label, LV_SYMBOL_OK);
            lv_obj_set_style_text_color(item->chk_label, lv_color_white(), 0);
            lv_obj_center(item->chk_label);
            lv_obj_add_flag(item->chk_label, LV_OBJ_FLAG_HIDDEN);
        }
    }

    __album_grid_update_select_label();
}

STATIC VOID_T __album_grid_exit_select_mode(VOID_T)
{
    uint32_t i;

    s_album_grid.select_mode = FALSE;
    s_album_grid.select_count = 0;

    /* Toggle top-right buttons */
    lv_obj_clear_flag(s_album_grid.choose_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_album_grid.cancel_btn, LV_OBJ_FLAG_HIDDEN);

    /* Hide bottom bar and restore grid */
    lv_obj_add_flag(s_album_grid.bottom_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(s_album_grid.grid_cont, LV_VER_RES - ALBUM_GRID_TOP_BAR_H);

    /* Remove checkbox circles */
    for (i = 0; i < s_album_grid.item_count; i++) {
        ALBUM_GRID_ITEM_T *item = &s_album_grid.items[i];
        item->selected = FALSE;
        if (item->chk) {
            lv_obj_del(item->chk);
            item->chk = NULL;
            item->chk_label = NULL;
        }
    }
}

STATIC VOID_T __album_grid_update_select_label(VOID_T)
{
    if (s_album_grid.select_lbl == NULL) {
        return;
    }

    if (s_album_grid.select_count == 0) {
        lv_label_set_text(s_album_grid.select_lbl, "\xe9\x80\x89\xe6\x8b\xa9\xe5\x9b\xbe\xe7\x89\x87");  /* 选择图片 */
    } else {
        char buf[32];
        snprintf(buf, sizeof(buf), "%u\xe4\xb8\xaa\xe8\xa2\xab\xe9\x80\x89\xe4\xb8\xad", s_album_grid.select_count);  /* X个被选中 */
        lv_label_set_text(s_album_grid.select_lbl, buf);
    }
}

STATIC VOID_T __album_grid_update_checkbox(ALBUM_GRID_ITEM_T *item)
{
    if (item->chk == NULL) {
        return;
    }

    if (item->selected) {
        lv_obj_set_style_bg_color(item->chk, lv_color_hex(0xFFD700), 0);
        lv_obj_set_style_bg_opa(item->chk, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(item->chk, lv_color_hex(0xFFD700), 0);
        if (item->chk_label) {
            lv_obj_clear_flag(item->chk_label, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        lv_obj_set_style_bg_color(item->chk, lv_color_hex(0x808080), 0);
        lv_obj_set_style_bg_opa(item->chk, LV_OPA_30, 0);
        lv_obj_set_style_border_color(item->chk, lv_color_white(), 0);
        if (item->chk_label) {
            lv_obj_add_flag(item->chk_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* ---------------------------------------------------------------------------
 * Cell management
 * --------------------------------------------------------------------------- */

STATIC VOID_T __album_grid_clear_cells(VOID_T)
{
    if (s_album_grid.grid_cont) {
        lv_obj_clean(s_album_grid.grid_cont);
    }

    memset(s_album_grid.items, 0, sizeof(s_album_grid.items));
    s_album_grid.item_count = 0;
    s_album_grid.select_count = 0;
}

/* ---------------------------------------------------------------------------
 * Screen setup
 * --------------------------------------------------------------------------- */

VOID_T setup_scr_album_grid(VOID_T)
{
    lv_obj_t *icon;

    if (s_album_grid.scr) {
        return;
    }

    memset(&s_album_grid, 0, sizeof(s_album_grid));

    /* ---- Full-screen base ---- */
    s_album_grid.scr = lv_obj_create(NULL);
    lv_obj_set_size(s_album_grid.scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_album_grid.scr, lv_color_hex(ALBUM_GRID_BG_COLOR), 0);
    lv_obj_set_style_pad_all(s_album_grid.scr, 0, 0);
    lv_obj_set_scrollbar_mode(s_album_grid.scr, LV_SCROLLBAR_MODE_OFF);

    /* ---- Top-left: back button ---- */
    s_album_grid.back_btn = lv_btn_create(s_album_grid.scr);
    lv_obj_remove_style_all(s_album_grid.back_btn);
    lv_obj_set_size(s_album_grid.back_btn, ALBUM_GRID_BTN_SIZE, ALBUM_GRID_BTN_SIZE);
    lv_obj_set_pos(s_album_grid.back_btn, 12, ALBUM_GRID_TOP_Y);
    lv_obj_set_style_bg_opa(s_album_grid.back_btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(s_album_grid.back_btn, __album_grid_back_cb, LV_EVENT_CLICKED, NULL);

    icon = lv_img_create(s_album_grid.back_btn);
    lv_img_set_src(icon, &icon_back_24_24);
    lv_obj_center(icon);

    /* ---- Top-center: title ---- */
    s_album_grid.title_lbl = lv_label_create(s_album_grid.scr);
    lv_label_set_text(s_album_grid.title_lbl, "\xe7\x9b\xb8\xe5\x86\x8c");  /* 相册 */
    lv_obj_set_style_text_font(s_album_grid.title_lbl, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(s_album_grid.title_lbl, lv_color_white(), 0);
    lv_obj_align(s_album_grid.title_lbl, LV_ALIGN_TOP_MID, 0, ALBUM_GRID_TOP_Y + 6);

    /* ---- Top-right: choose button (normal mode) ---- */
    s_album_grid.choose_btn = lv_btn_create(s_album_grid.scr);
    lv_obj_remove_style_all(s_album_grid.choose_btn);
    lv_obj_set_size(s_album_grid.choose_btn, ALBUM_GRID_BTN_SIZE, ALBUM_GRID_BTN_SIZE);
    lv_obj_set_pos(s_album_grid.choose_btn, LV_HOR_RES - ALBUM_GRID_BTN_SIZE - 12, ALBUM_GRID_TOP_Y);
    lv_obj_set_style_bg_opa(s_album_grid.choose_btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(s_album_grid.choose_btn, __album_grid_choose_cb, LV_EVENT_CLICKED, NULL);

    icon = lv_img_create(s_album_grid.choose_btn);
    lv_img_set_src(icon, &icon_choose);
    lv_obj_center(icon);

    /* ---- Top-right: cancel button (selection mode, hidden) ---- */
    s_album_grid.cancel_btn = lv_btn_create(s_album_grid.scr);
    lv_obj_remove_style_all(s_album_grid.cancel_btn);
    lv_obj_set_size(s_album_grid.cancel_btn, LV_SIZE_CONTENT, ALBUM_GRID_BTN_SIZE);
    lv_obj_set_pos(s_album_grid.cancel_btn, LV_HOR_RES - 60, ALBUM_GRID_TOP_Y);
    lv_obj_set_style_bg_opa(s_album_grid.cancel_btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(s_album_grid.cancel_btn, __album_grid_cancel_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *cancel_lbl = lv_label_create(s_album_grid.cancel_btn);
    lv_label_set_text(cancel_lbl, "\xe5\x8f\x96\xe6\xb6\x88");  /* 取消 */
    lv_obj_set_style_text_font(cancel_lbl, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(cancel_lbl, lv_color_white(), 0);
    lv_obj_center(cancel_lbl);
    lv_obj_add_flag(s_album_grid.cancel_btn, LV_OBJ_FLAG_HIDDEN);

    /* ---- Grid container (scrollable, flex-wrap) ---- */
    s_album_grid.grid_cont = lv_obj_create(s_album_grid.scr);
    lv_obj_remove_style_all(s_album_grid.grid_cont);
    lv_obj_set_pos(s_album_grid.grid_cont, 0, ALBUM_GRID_TOP_BAR_H);
    lv_obj_set_size(s_album_grid.grid_cont, LV_HOR_RES, LV_VER_RES - ALBUM_GRID_TOP_BAR_H);
    lv_obj_set_style_bg_opa(s_album_grid.grid_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_album_grid.grid_cont, ALBUM_GRID_PAD, 0);
    lv_obj_set_style_pad_row(s_album_grid.grid_cont, ALBUM_GRID_GAP, 0);
    lv_obj_set_style_pad_column(s_album_grid.grid_cont, ALBUM_GRID_GAP, 0);
    lv_obj_set_flex_flow(s_album_grid.grid_cont, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_scrollbar_mode(s_album_grid.grid_cont, LV_SCROLLBAR_MODE_OFF);

    /* ---- Bottom bar (selection mode, hidden) ---- */
    s_album_grid.bottom_bar = lv_obj_create(s_album_grid.scr);
    lv_obj_remove_style_all(s_album_grid.bottom_bar);
    lv_obj_set_size(s_album_grid.bottom_bar, LV_HOR_RES, ALBUM_GRID_BOT_BAR_H);
    lv_obj_align(s_album_grid.bottom_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_album_grid.bottom_bar, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_bg_opa(s_album_grid.bottom_bar, LV_OPA_COVER, 0);

    s_album_grid.select_lbl = lv_label_create(s_album_grid.bottom_bar);
    lv_label_set_text(s_album_grid.select_lbl, "\xe9\x80\x89\xe6\x8b\xa9\xe5\x9b\xbe\xe7\x89\x87");  /* 选择图片 */
    lv_obj_set_style_text_font(s_album_grid.select_lbl, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(s_album_grid.select_lbl, lv_color_white(), 0);
    lv_obj_align(s_album_grid.select_lbl, LV_ALIGN_CENTER, 0, 0);

    s_album_grid.del_btn = lv_btn_create(s_album_grid.bottom_bar);
    lv_obj_remove_style_all(s_album_grid.del_btn);
    lv_obj_set_size(s_album_grid.del_btn, 80, ALBUM_GRID_BOT_BAR_H);
    lv_obj_align(s_album_grid.del_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_opa(s_album_grid.del_btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(s_album_grid.del_btn, __album_grid_delete_cb, LV_EVENT_CLICKED, NULL);

    icon = lv_img_create(s_album_grid.del_btn);
    lv_img_set_src(icon, &icon_delete);
    lv_img_set_zoom(icon, 192);
    lv_obj_center(icon);

    lv_obj_add_flag(s_album_grid.bottom_bar, LV_OBJ_FLAG_HIDDEN);

    /* Register gesture for control center */
    ui_control_center_register_gesture(s_album_grid.scr);

    lv_obj_update_layout(s_album_grid.scr);
}

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------- */

VOID_T ui_album_grid_show(VOID_T)
{
    if (s_album_grid.scr == NULL) {
        setup_scr_album_grid();
    }

    if (lv_scr_act() != s_album_grid.scr) {
        lv_scr_load(s_album_grid.scr);
    }
}

VOID_T ui_album_grid_set_thumbs(CONST WUKONG_PICTURE_THUMB_LIST_T *list)
{
    uint32_t i;
    uint32_t count;

    __album_grid_clear_cells();

    if (list == NULL) {
        count = 0;
    } else {
        count = (list->count > ALBUM_GRID_MAX_PHOTOS) ? ALBUM_GRID_MAX_PHOTOS : list->count;
    }

    for (i = 0; i < count; i++) {
        CONST WUKONG_PICTURE_THUMB_T *ti = &list->items[i];
        ALBUM_GRID_ITEM_T *gi = &s_album_grid.items[i];

        /* Cell container */
        gi->cell = lv_obj_create(s_album_grid.grid_cont);
        lv_obj_remove_style_all(gi->cell);
        lv_obj_set_size(gi->cell, ALBUM_GRID_CELL_SIZE, ALBUM_GRID_CELL_SIZE);
        lv_obj_set_style_radius(gi->cell, ALBUM_GRID_CELL_RADIUS, 0);
        lv_obj_set_style_bg_color(gi->cell, lv_color_hex(ALBUM_GRID_CELL_COLOR), 0);
        lv_obj_set_style_bg_opa(gi->cell, LV_OPA_COVER, 0);
        lv_obj_set_style_clip_corner(gi->cell, true, 0);
        lv_obj_clear_flag(gi->cell, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_user_data(gi->cell, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(gi->cell, __album_grid_cell_cb, LV_EVENT_CLICKED, NULL);

        /* Thumbnail canvas */
        if (ti->data && ti->width > 0 && ti->height > 0) {
            gi->canvas = lv_canvas_create(gi->cell);
            lv_canvas_set_buffer(gi->canvas, ti->data,
                                 ti->width, ti->height,
                                 LV_IMG_CF_TRUE_COLOR);
            lv_obj_center(gi->canvas);
            lv_obj_clear_flag(gi->canvas, LV_OBJ_FLAG_CLICKABLE);
        }

        strncpy(gi->filename, ti->name, WUKONG_PICTURE_NAME_MAX_LEN);
        gi->filename[WUKONG_PICTURE_NAME_MAX_LEN] = '\0';
        gi->selected = FALSE;
        gi->chk = NULL;
        gi->chk_label = NULL;
    }

    s_album_grid.item_count = count;

    /* Add empty placeholder cells to fill the row */
    uint32_t grid_rows = (count + ALBUM_GRID_COLS - 1) / ALBUM_GRID_COLS;
    if (grid_rows < ALBUM_GRID_MIN_ROWS) {
        grid_rows = ALBUM_GRID_MIN_ROWS;
    }
    uint32_t total_cells = grid_rows * ALBUM_GRID_COLS;

    for (; i < total_cells; i++) {
        lv_obj_t *placeholder = lv_obj_create(s_album_grid.grid_cont);
        lv_obj_remove_style_all(placeholder);
        lv_obj_set_size(placeholder, ALBUM_GRID_CELL_SIZE, ALBUM_GRID_CELL_SIZE);
        lv_obj_set_style_radius(placeholder, ALBUM_GRID_CELL_RADIUS, 0);
        lv_obj_set_style_bg_color(placeholder, lv_color_hex(ALBUM_GRID_CELL_COLOR), 0);
        lv_obj_set_style_bg_opa(placeholder, LV_OPA_COVER, 0);
        lv_obj_clear_flag(placeholder, LV_OBJ_FLAG_SCROLLABLE);
    }
}

UINT32_T ui_album_grid_get_selected_names(CONST CHAR_T *names[], UINT32_T max_count)
{
    UINT32_T count = 0;
    UINT32_T i;

    if (names == NULL || max_count == 0) {
        return 0;
    }

    for (i = 0; i < s_album_grid.item_count && count < max_count; i++) {
        if (s_album_grid.items[i].selected && s_album_grid.items[i].filename[0]) {
            names[count++] = s_album_grid.items[i].filename;
        }
    }

    return count;
}

UINT32_T ui_album_grid_get_pending_delete_names(CONST CHAR_T *names[], UINT32_T max_count)
{
    UINT32_T count = 0;
    UINT32_T i;

    if (names == NULL || max_count == 0) {
        return 0;
    }

    for (i = 0; i < s_album_grid_pending_count && count < max_count; i++) {
        names[count++] = s_album_grid_pending_delete[i];
    }

    s_album_grid_pending_count = 0;
    return count;
}

VOID_T ui_album_grid_hide(VOID_T)
{
    if (s_album_grid.select_mode) {
        __album_grid_exit_select_mode();
    }

    __album_grid_clear_cells();
}

lv_obj_t *ui_album_grid_get_scr(VOID_T)
{
    return s_album_grid.scr;
}
