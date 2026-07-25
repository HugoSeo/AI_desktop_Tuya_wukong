/**
 * @file ui_detection.c
 * @brief Detection list screen for T5AI_BOARD (320x480)
 *
 * Read-only browser for recent AI robot detection alerts pushed by cloud.
 * Each show pulls page 1 (last 24h, 10 items/page, up to 20 pages) via
 * `iot_httpc_common_post_simple` against the
 * `thing.ipc.ai.robot.msg.list` API. The user can:
 *  - browse the list (each item logs its attachPics URL on click,
 *    matching desk_func_detection.c — drill-down preview is not
 *    implemented in either tree),
 *  - tap the AI button to trigger `__on_get_detection_msg()` so the
 *    device manually performs one detection and the cloud pushes a new
 *    alert (visible after the next page query),
 *  - tap the page button to toggle a hidden lvgl dropdown for paging.
 *
 * Lifecycle follows view's "lazy create + persistent screen" convention.
 * The HTTP fetch is dispatched onto the system workq (WORKQ_SYSTEM allows
 * blocking) and the result is bounced back to the LVGL thread via
 * lv_async_call so the screen-switch animation stays responsive while the
 * 1-3s request is in flight; a "加载中..." overlay covers the wait.
 *
 * @version 1.0
 * @date 2026-05-18
 * @copyright Copyright (c) Tuya Inc.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "ui_common.h"
#include "ui_detection.h"
#include "ty_cJSON.h"
#include "tal_time_service.h"
#include "tal_workq_service.h"
#include "tuya_iot_internal_api.h"

extern VOID_T __on_get_detection_msg(VOID_T);

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define DETECTION_AI_BTN_W          50
#define DETECTION_PAGE_BTN_W        50
#define DETECTION_PAGE_BTN_GAP      20

#define DETECTION_DROPDOWN_W        50
#define DETECTION_DROPDOWN_H        30

#define DETECTION_MAX_PAGE          20
#define DETECTION_PAGE_SIZE         10
#define DETECTION_QUERY_RANGE_S     (3600 * 24)            /* last 24 hours */
#define DETECTION_MSG_LIST_API      "thing.ipc.ai.robot.msg.list"
#define DETECTION_MSG_LIST_VER      "1.0"

#define DETECTION_ITEM_W            290
#define DETECTION_ITEM_H            50
#define DETECTION_ITEM_BG           0x353740
#define DETECTION_ITEM_RADIUS       16

#define DETECTION_MUTED_TEXT_COLOR  0xB8BDDE

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef struct {
    CHAR_T msgTitle[64];
    CHAR_T dateTime[32];
    CHAR_T attachPics[1024];
} DETECTION_MSG_ITEM_T;

typedef struct {
    lv_obj_t *scr;
    lv_obj_t *title_bar;
    lv_obj_t *title_lbl;
    lv_obj_t *back_btn;
    lv_obj_t *ai_btn;
    lv_obj_t *page_btn;
    lv_obj_t *page_dropdown;
    lv_obj_t *content;
    lv_obj_t *loading_lbl;   /* "加载中..." centered overlay, hidden when idle */
} DETECTION_UI_T;

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC DETECTION_UI_T s_detection = {0};
STATIC INT_T s_current_page = 1;
STATIC INT_T s_total_count = 0;
STATIC INT_T s_total_pages = 0;
STATIC INT_T s_page_item_count = 0;
STATIC DETECTION_MSG_ITEM_T s_page_items[DETECTION_PAGE_SIZE];
STATIC BOOL_T s_query_in_flight = FALSE;
STATIC INT_T s_pending_page = 1;

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC VOID_T setup_scr_detection(VOID_T);
STATIC VOID_T __detection_build_title_bar(VOID_T);
STATIC VOID_T __detection_build_content(VOID_T);
STATIC VOID_T __detection_build_dropdown(VOID_T);

STATIC VOID_T __detection_back_cb(lv_event_t *e);
STATIC VOID_T __detection_ai_cb(lv_event_t *e);
STATIC VOID_T __detection_page_btn_cb(lv_event_t *e);
STATIC VOID_T __detection_dropdown_changed_cb(lv_event_t *e);
STATIC VOID_T __detection_msg_item_cb(lv_event_t *e);

STATIC OPERATE_RET __detection_query_msg_list(INT_T page_num);
STATIC VOID_T __detection_refresh_content(VOID_T);
STATIC VOID_T __detection_update_dropdown_pages(INT_T total_pages);
STATIC VOID_T __detection_set_loading(BOOL_T loading);
STATIC VOID_T __detection_query_async(INT_T page_num);
STATIC VOID_T __detection_query_work_cb(VOID_T *data);
STATIC VOID_T __detection_query_done_async(VOID_T *arg);

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */

/**
 * @brief Query detection message list from cloud (synchronous HTTP)
 * @param[in] page_num 1-based page number to fetch
 * @return OPRT_OK on success (or empty), HTTP/parse error code on failure
 * @note Updates only the file-scope page buffers. The caller is responsible
 *       for refreshing the dropdown / list widgets on the LVGL thread, since
 *       this function may be invoked from a workq thread where touching LVGL
 *       widgets is not safe.
 */
STATIC OPERATE_RET __detection_query_msg_list(INT_T page_num)
{
    OPERATE_RET rt = OPRT_OK;
    ty_cJSON *result = NULL;
    CHAR_T post_content[128] = {0};
    TIME_T now = tal_time_get_posix();
    TIME_T start = now - DETECTION_QUERY_RANGE_S;

    snprintf(post_content, sizeof(post_content),
             "{\"startTime\":%ld,\"endTime\":%ld,\"pageNum\":%d,\"pageSize\":%d}",
             (long)start, (long)now, page_num, DETECTION_PAGE_SIZE);

    rt = iot_httpc_common_post_simple(DETECTION_MSG_LIST_API, DETECTION_MSG_LIST_VER,
                                      post_content, NULL, &result);
    if (rt != OPRT_OK) {
        PR_ERR("detection: msg list request failed, rt=%d", rt);
        return rt;
    }

    if (result == NULL) {
        return OPRT_OK;
    }

    memset(s_page_items, 0, sizeof(s_page_items));
    s_page_item_count = 0;

    ty_cJSON *total_count_json = ty_cJSON_GetObjectItem(result, "totalCount");
    if (total_count_json && ty_cJSON_GetStringValue(total_count_json)) {
        s_total_count = atoi(ty_cJSON_GetStringValue(total_count_json));
    } else if (total_count_json && ty_cJSON_IsNumber(total_count_json)) {
        s_total_count = total_count_json->valueint;
    } else {
        s_total_count = 0;
    }

    s_total_pages = (s_total_count + DETECTION_PAGE_SIZE - 1) / DETECTION_PAGE_SIZE;
    if (s_total_pages < 1) {
        s_total_pages = 1;
    }
    if (s_total_pages > DETECTION_MAX_PAGE) {
        s_total_pages = DETECTION_MAX_PAGE;
    }

    ty_cJSON *datas = ty_cJSON_GetObjectItem(result, "datas");
    if (datas && ty_cJSON_IsArray(datas)) {
        INT_T count = ty_cJSON_GetArraySize(datas);
        if (count > DETECTION_PAGE_SIZE) {
            count = DETECTION_PAGE_SIZE;
        }
        for (INT_T i = 0; i < count; i++) {
            ty_cJSON *item = ty_cJSON_GetArrayItem(datas, i);
            if (item == NULL) {
                continue;
            }
            ty_cJSON *title_j = ty_cJSON_GetObjectItem(item, "msgTitle");
            ty_cJSON *date_j  = ty_cJSON_GetObjectItem(item, "dateTime");
            ty_cJSON *pics_j  = ty_cJSON_GetObjectItem(item, "attachPics");

            if (title_j && ty_cJSON_GetStringValue(title_j)) {
                snprintf(s_page_items[i].msgTitle, sizeof(s_page_items[i].msgTitle),
                         "%s", ty_cJSON_GetStringValue(title_j));
            }
            if (date_j && ty_cJSON_GetStringValue(date_j)) {
                snprintf(s_page_items[i].dateTime, sizeof(s_page_items[i].dateTime),
                         "%s", ty_cJSON_GetStringValue(date_j));
            }
            if (pics_j && ty_cJSON_GetStringValue(pics_j)) {
                snprintf(s_page_items[i].attachPics, sizeof(s_page_items[i].attachPics),
                         "%s", ty_cJSON_GetStringValue(pics_j));
            }
            s_page_item_count++;
        }
    }

    PR_INFO("detection: page %d totalCount=%d items=%d",
            page_num, s_total_count, s_page_item_count);
    ty_cJSON_Delete(result);

    return OPRT_OK;
}

/**
 * @brief Rebuild the dropdown options string ("1\n2\n...") for the given page count
 * @param[in] total_pages clamped page total
 * @return none
 */
STATIC VOID_T __detection_update_dropdown_pages(INT_T total_pages)
{
    STATIC CHAR_T page_options[DETECTION_MAX_PAGE * 4];

    if (s_detection.page_dropdown == NULL) {
        return;
    }
    if (total_pages < 1) {
        total_pages = 1;
    }
    if (total_pages > DETECTION_MAX_PAGE) {
        total_pages = DETECTION_MAX_PAGE;
    }

    INT_T offset = 0;
    for (INT_T i = 1; i <= total_pages; i++) {
        if (i > 1) {
            page_options[offset++] = '\n';
        }
        offset += snprintf(page_options + offset, sizeof(page_options) - offset, "%d", i);
    }
    page_options[offset] = '\0';

    lv_dropdown_set_options(s_detection.page_dropdown, page_options);

    if (s_current_page > total_pages) {
        s_current_page = total_pages;
    }
    lv_dropdown_set_selected(s_detection.page_dropdown, s_current_page - 1);
}

/**
 * @brief Rebuild the list content area with the current page's items
 * @return none
 * @note Cleans the content container then either renders an empty-state label
 *       or re-creates one item card per record.
 */
STATIC VOID_T __detection_refresh_content(VOID_T)
{
    if (s_detection.content == NULL) {
        return;
    }

    lv_obj_clean(s_detection.content);

    lv_obj_set_flex_flow(s_detection.content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_detection.content,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(s_detection.content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_detection.content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_ver(s_detection.content, 8, 0);
    lv_obj_set_style_pad_hor(s_detection.content, 15, 0);
    lv_obj_set_style_pad_row(s_detection.content, 5, 0);

    if (s_page_item_count == 0) {
        lv_obj_t *empty = lv_label_create(s_detection.content);
        lv_obj_remove_style_all(empty);
        lv_label_set_text(empty, "暂无侦测记录");
        lv_obj_set_style_text_font(empty, &AlibabaPuHuiTi3_Regular18_Static, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(DETECTION_MUTED_TEXT_COLOR), 0);
        return;
    }

    for (INT_T i = 0; i < s_page_item_count; i++) {
        lv_obj_t *item = lv_obj_create(s_detection.content);
        lv_obj_remove_style_all(item);
        lv_obj_set_size(item, DETECTION_ITEM_W, DETECTION_ITEM_H);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(item, DETECTION_ITEM_RADIUS, 0);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(item, lv_color_hex(DETECTION_ITEM_BG), 0);
        lv_obj_set_style_pad_left(item, 12, 0);
        lv_obj_set_style_pad_right(item, 12, 0);
        lv_obj_set_style_pad_top(item, 6, 0);
        lv_obj_set_style_pad_bottom(item, 6, 0);
        lv_obj_set_scrollbar_mode(item, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_scroll_dir(item, LV_DIR_NONE);

        lv_obj_add_event_cb(item, __detection_msg_item_cb,
                            LV_EVENT_CLICKED, (VOID_T *)(uintptr_t)i);

        lv_obj_t *title = lv_label_create(item);
        lv_obj_remove_style_all(title);
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        lv_label_set_text(title, s_page_items[i].msgTitle);
        lv_obj_set_size(title, 260, LV_SIZE_CONTENT);
        lv_obj_set_pos(title, 0, 0);
        lv_obj_set_style_text_font(title, &AlibabaPuHuiTi3_Regular18_Static, 0);
        lv_obj_set_style_text_color(title, lv_color_white(), 0);

        lv_obj_t *date = lv_label_create(item);
        lv_obj_remove_style_all(date);
        lv_label_set_long_mode(date, LV_LABEL_LONG_DOT);
        lv_label_set_text(date, s_page_items[i].dateTime);
        lv_obj_set_size(date, 260, LV_SIZE_CONTENT);
        lv_obj_set_pos(date, 0, 20);
        lv_obj_set_style_text_font(date, &AlibabaPuHuiTi3_Regular16, 0);
        lv_obj_set_style_text_color(date, lv_color_hex(DETECTION_MUTED_TEXT_COLOR), 0);
    }
}

/* ---------------------------------------------------------------------------
 * Async query (workq thread + lv_async_call back to LVGL thread)
 * --------------------------------------------------------------------------- */

/**
 * @brief Toggle the loading-overlay label visibility (must run on LVGL thread)
 * @param[in] loading TRUE to show, FALSE to hide
 * @return none
 */
STATIC VOID_T __detection_set_loading(BOOL_T loading)
{
    if (s_detection.loading_lbl == NULL) {
        return;
    }
    if (loading) {
        lv_obj_clear_flag(s_detection.loading_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_detection.loading_lbl);
    } else {
        lv_obj_add_flag(s_detection.loading_lbl, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief LVGL-thread completion callback: refresh widgets from page buffers
 * @param[in] arg unused
 * @return none
 */
STATIC VOID_T __detection_query_done_async(VOID_T *arg)
{
    (VOID_T)arg;

    s_query_in_flight = FALSE;
    __detection_set_loading(FALSE);

    if (s_detection.scr == NULL) {
        return;
    }
    __detection_update_dropdown_pages(s_total_pages);
    __detection_refresh_content();
}

/**
 * @brief Workq-thread worker: run the blocking HTTP query, then bounce back
 * @param[in] data unused
 * @return none
 */
STATIC VOID_T __detection_query_work_cb(VOID_T *data)
{
    (VOID_T)data;
    (VOID_T)__detection_query_msg_list(s_pending_page);
    lv_async_call(__detection_query_done_async, NULL);
}

/**
 * @brief Public entry: kick off an async page fetch with loading indicator
 * @param[in] page_num 1-based page number to fetch
 * @return none
 * @note Re-entrancy-guarded by s_query_in_flight; concurrent requests are
 *       dropped silently rather than queued.
 */
STATIC VOID_T __detection_query_async(INT_T page_num)
{
    OPERATE_RET rt;

    if (s_query_in_flight) {
        PR_DEBUG("detection: query already in flight, ignoring page=%d", page_num);
        return;
    }

    s_pending_page = page_num;
    s_query_in_flight = TRUE;
    __detection_set_loading(TRUE);

    rt = tal_workq_schedule(WORKQ_SYSTEM, __detection_query_work_cb, NULL);
    if (rt != OPRT_OK) {
        PR_ERR("detection: workq schedule failed, rt=%d", rt);
        s_query_in_flight = FALSE;
        __detection_set_loading(FALSE);
    }
}

/* ---------------------------------------------------------------------------
 * Event handlers
 * --------------------------------------------------------------------------- */

/**
 * @brief Back button: route through dispatch so ui_nav_back drives ui_detection_hide
 * @param[in] e LVGL event (unused)
 * @return none
 */
STATIC VOID_T __detection_back_cb(lv_event_t *e)
{
    (VOID_T)e;
    PR_DEBUG("detection: back");
    tuya_ai_display_action_post(NULL, 0, TY_DISP_ACT_CLOSE_DETECTION);
}

/**
 * @brief AI button: ask the device to perform one manual detection now
 * @param[in] e LVGL event (unused)
 * @return none
 * @note Cloud will push a new record asynchronously; the user must re-enter
 *       the screen (or change page) to see it. We do not auto-refresh here
 *       because the new record is not yet available when this returns.
 */
STATIC VOID_T __detection_ai_cb(lv_event_t *e)
{
    (VOID_T)e;
    PR_INFO("detection: AI trigger pressed");
    __on_get_detection_msg();
}

/**
 * @brief Page button: toggle the hidden dropdown's visibility/open state
 * @param[in] e LVGL event (unused)
 * @return none
 */
STATIC VOID_T __detection_page_btn_cb(lv_event_t *e)
{
    (VOID_T)e;
    if (s_detection.page_dropdown == NULL) {
        return;
    }
    if (lv_obj_has_flag(s_detection.page_dropdown, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(s_detection.page_dropdown, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_detection.page_dropdown);
        lv_dropdown_open(s_detection.page_dropdown);
    } else {
        lv_dropdown_close(s_detection.page_dropdown);
        lv_obj_add_flag(s_detection.page_dropdown, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief Dropdown value changed: update current page and refetch
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __detection_dropdown_changed_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    s_current_page = lv_dropdown_get_selected(dd) + 1;
    PR_DEBUG("detection: page changed to %d", s_current_page);
    lv_dropdown_close(dd);
    lv_obj_add_flag(dd, LV_OBJ_FLAG_HIDDEN);
    __detection_query_async(s_current_page);
}

/**
 * @brief List item click: log attachPics URL (drill-down preview not implemented)
 * @param[in] e LVGL event carrying the item index in user_data
 * @return none
 */
STATIC VOID_T __detection_msg_item_cb(lv_event_t *e)
{
    INT_T index = (INT_T)(uintptr_t)lv_event_get_user_data(e);
    if (index >= 0 && index < s_page_item_count) {
        PR_INFO("detection: item clicked, index=%d, attachPics=%s",
                index, s_page_items[index].attachPics);
    }
}

/* ---------------------------------------------------------------------------
 * Layout builders
 * --------------------------------------------------------------------------- */

/**
 * @brief Build the title bar: back btn + centered "侦测记录" + AI btn + page btn
 * @return none
 * @note AI btn lives at (LV_HOR_RES - 50 - 20 - 50, 0); page btn at the right edge.
 */
STATIC VOID_T __detection_build_title_bar(VOID_T)
{
    s_detection.title_bar = lv_obj_create(s_detection.scr);
    lv_obj_remove_style_all(s_detection.title_bar);
    lv_obj_set_pos(s_detection.title_bar, 0, 0);
    lv_obj_set_size(s_detection.title_bar, LV_HOR_RES, UI_TITLE_BAR_H);
    lv_obj_set_style_bg_opa(s_detection.title_bar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_detection.title_bar, LV_OBJ_FLAG_SCROLLABLE);

    /* Back button: top-left */
    s_detection.back_btn = lv_btn_create(s_detection.title_bar);
    lv_obj_remove_style_all(s_detection.back_btn);
    lv_obj_set_size(s_detection.back_btn, UI_TITLE_BTN_W, UI_TITLE_BAR_H);
    lv_obj_set_pos(s_detection.back_btn, 0, 0);
    lv_obj_set_style_bg_opa(s_detection.back_btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(s_detection.back_btn, __detection_back_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_icon = lv_img_create(s_detection.back_btn);
    lv_img_set_src(back_icon, &icon_back_24_24);
    lv_obj_set_size(back_icon, UI_TITLE_ICON_SIZE, UI_TITLE_ICON_SIZE);
    lv_obj_center(back_icon);

    /* Title label: centered */
    s_detection.title_lbl = lv_label_create(s_detection.title_bar);
    lv_label_set_text(s_detection.title_lbl, "侦测记录");
    lv_obj_set_style_text_font(s_detection.title_lbl,
                               &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(s_detection.title_lbl, lv_color_white(), 0);
    lv_obj_center(s_detection.title_lbl);

    /* AI button: 50x50, sits 20px to the left of the page button */
    s_detection.ai_btn = lv_btn_create(s_detection.title_bar);
    lv_obj_remove_style_all(s_detection.ai_btn);
    lv_obj_set_size(s_detection.ai_btn, DETECTION_AI_BTN_W, UI_TITLE_BAR_H);
    lv_obj_set_pos(s_detection.ai_btn,
                   LV_HOR_RES - DETECTION_PAGE_BTN_W - DETECTION_PAGE_BTN_GAP - DETECTION_AI_BTN_W,
                   0);
    lv_obj_set_style_bg_opa(s_detection.ai_btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(s_detection.ai_btn, __detection_ai_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *ai_icon = lv_img_create(s_detection.ai_btn);
    lv_img_set_src(ai_icon, &icon_ai_on);
    lv_obj_set_size(ai_icon, UI_TITLE_ICON_SIZE, UI_TITLE_ICON_SIZE);
    lv_obj_center(ai_icon);

    /* Page button: top-right, uses lvgl built-in LV_SYMBOL_LIST glyph */
    s_detection.page_btn = lv_btn_create(s_detection.title_bar);
    lv_obj_remove_style_all(s_detection.page_btn);
    lv_obj_set_size(s_detection.page_btn, DETECTION_PAGE_BTN_W, UI_TITLE_BAR_H);
    lv_obj_align(s_detection.page_btn, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_opa(s_detection.page_btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(s_detection.page_btn, __detection_page_btn_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *page_lbl = lv_label_create(s_detection.page_btn);
    lv_label_set_text(page_lbl, LV_SYMBOL_LIST);
    lv_obj_set_style_text_color(page_lbl, lv_color_white(), 0);
    lv_obj_center(page_lbl);
}

/**
 * @brief Build the hidden dropdown anchored to the page button's position
 * @return none
 * @note Default lvgl dropdown styling (Q3 decision: no custom style apply).
 *       Hidden by default; toggled by the page button.
 */
STATIC VOID_T __detection_build_dropdown(VOID_T)
{
    s_detection.page_dropdown = lv_dropdown_create(s_detection.scr);
    lv_obj_set_size(s_detection.page_dropdown,
                    DETECTION_DROPDOWN_W, DETECTION_DROPDOWN_H);
    lv_obj_set_pos(s_detection.page_dropdown,
                   LV_HOR_RES - DETECTION_DROPDOWN_W, UI_TITLE_BAR_H);
    lv_dropdown_set_dir(s_detection.page_dropdown, LV_DIR_BOTTOM);
    lv_dropdown_set_symbol(s_detection.page_dropdown, NULL);

    /* Initial options 1..MAX so the widget has something to show before the
     * first query updates them. */
    STATIC CHAR_T initial_options[DETECTION_MAX_PAGE * 4];
    INT_T offset = 0;
    for (INT_T i = 1; i <= DETECTION_MAX_PAGE; i++) {
        if (i > 1) {
            initial_options[offset++] = '\n';
        }
        offset += snprintf(initial_options + offset, sizeof(initial_options) - offset, "%d", i);
    }
    initial_options[offset] = '\0';
    lv_dropdown_set_options(s_detection.page_dropdown, initial_options);

    lv_dropdown_set_selected(s_detection.page_dropdown, 0);
    lv_obj_add_flag(s_detection.page_dropdown, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_detection.page_dropdown, __detection_dropdown_changed_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
}

/**
 * @brief Build the content container that holds list items below the title bar
 * @return none
 */
STATIC VOID_T __detection_build_content(VOID_T)
{
    s_detection.content = lv_obj_create(s_detection.scr);
    lv_obj_remove_style_all(s_detection.content);
    lv_obj_set_pos(s_detection.content, 0, UI_TITLE_BAR_H);
    lv_obj_set_size(s_detection.content, LV_HOR_RES, LV_VER_RES - UI_TITLE_BAR_H);
    lv_obj_set_style_bg_opa(s_detection.content, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(s_detection.content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(s_detection.content, LV_DIR_VER);

    /* Loading overlay: centered "加载中..." label, sibling of content so it
     * stays put while content rebuilds during async refresh. */
    s_detection.loading_lbl = lv_label_create(s_detection.scr);
    lv_label_set_text(s_detection.loading_lbl, "加载中...");
    lv_obj_set_style_text_font(s_detection.loading_lbl,
                               &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(s_detection.loading_lbl,
                                lv_color_hex(DETECTION_MUTED_TEXT_COLOR), 0);
    lv_obj_align(s_detection.loading_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_detection.loading_lbl, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Create the detection screen lazily (idempotent)
 * @return none
 */
STATIC VOID_T setup_scr_detection(VOID_T)
{
    if (s_detection.scr != NULL) {
        return;
    }

    memset(&s_detection, 0, sizeof(s_detection));

    s_detection.scr = lv_obj_create(NULL);
    lv_obj_set_size(s_detection.scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_detection.scr, lv_color_hex(UI_BG_COLOR_DARK), 0);
    lv_obj_set_style_bg_opa(s_detection.scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_detection.scr, 0, 0);
    lv_obj_set_scrollbar_mode(s_detection.scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_detection.scr, LV_OBJ_FLAG_SCROLLABLE);

    __detection_build_title_bar();
    __detection_build_content();
    __detection_build_dropdown();

    ui_control_center_register_gesture(s_detection.scr);

    lv_obj_update_layout(s_detection.scr);
}

/**
 * @brief Show the detection screen, refetching page 1 every visit (Q2 decision)
 * @return none
 * @note Switches the screen first, then dispatches the HTTP fetch onto the
 *       system workq. UI shows a "加载中..." overlay until the fetch lands
 *       back on the LVGL thread via lv_async_call. This keeps the LVGL main
 *       thread responsive — sync iot_httpc_common_post_simple can block
 *       1-3s and used to freeze the screen-switch animation.
 */
VOID_T ui_detection_show(VOID_T)
{
    if (s_detection.scr == NULL) {
        setup_scr_detection();
    }

    /* Reset paging and pull fresh records every time the user enters. */
    s_current_page = 1;
    if (s_detection.page_dropdown != NULL) {
        lv_dropdown_close(s_detection.page_dropdown);
        lv_obj_add_flag(s_detection.page_dropdown, LV_OBJ_FLAG_HIDDEN);
    }

    /* Clear stale list immediately so we don't show last-visit's items
     * while loading. */
    s_page_item_count = 0;
    if (s_detection.content != NULL) {
        lv_obj_clean(s_detection.content);
    }

    if (lv_scr_act() != s_detection.scr) {
        lv_scr_load(s_detection.scr);
    }

    __detection_query_async(1);
}

/**
 * @brief Hide the detection screen
 * @return none
 * @note No-op: the lv_obj tree and last-fetched data persist; the next show
 *       overwrites both.
 */
VOID_T ui_detection_hide(VOID_T)
{
    return;
}

/**
 * @brief Get the detection screen object
 * @return detection screen pointer, NULL if not created yet
 */
lv_obj_t *ui_detection_get_scr(VOID_T)
{
    return s_detection.scr;
}
