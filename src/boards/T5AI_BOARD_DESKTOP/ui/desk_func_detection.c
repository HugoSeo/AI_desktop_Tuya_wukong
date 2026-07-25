/**
 * @file desk_func_detection.c
 * @brief Detection message list screen UI implementation
 * @version 1.0
 * @date 2025-04-14
 * @copyright Copyright (c) Tuya Inc.
 */
#include "desk_event_handle.h"
#include "desk_handle_ui.h"
#include "ty_cJSON.h"
#include "tal_time_service.h"
#include "tuya_iot_internal_api.h"

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define DETECTION_TITLE_BAR_W       320
#define DETECTION_TITLE_BAR_H       50
#define DETECTION_CONTENT_W         320
#define DETECTION_CONTENT_H         190
#define DETECTION_BACK_BTN_SIZE     50
#define DETECTION_LIST_BTN_SIZE     50
#define DETECTION_MAX_PAGE          20
#define MD_MSG_LIST_API             "thing.ipc.ai.robot.msg.list"
#define MD_MSG_LIST_VER             "1.0"
#define MD_MSG_QUERY_RANGE          3600*24 //一天
#define MD_MSG_PAGE_SIZE            10

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC detection_scr_res_t s_detection_res = {0};
STATIC INT_T s_current_page = 1;
STATIC INT_T s_total_count = 0;
STATIC INT_T s_total_pages = 0;
STATIC INT_T s_page_item_count = 0;
STATIC detection_msg_item_t s_page_items[MD_MSG_PAGE_SIZE];

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC VOID_T __detection_title_bar_create(VOID_T);
STATIC VOID_T __detection_content_create(VOID_T);
STATIC VOID_T __detection_list_btn_clicked_cb(lv_event_t *e);
STATIC VOID_T __detection_page_dropdown_changed_cb(lv_event_t *e);
STATIC VOID_T __detection_back_btn_clicked_cb(lv_event_t *e);
STATIC VOID_T __detection_ai_btn_clicked_cb(lv_event_t *e);
STATIC VOID_T __detection_dropdown_style_apply(lv_obj_t *dropdown);
STATIC OPERATE_RET __detection_query_msg_list(INT_T page_num);
STATIC VOID_T __detection_refresh_content(VOID_T);
STATIC VOID_T __detection_update_dropdown_pages(INT_T total_pages);
STATIC VOID_T __detection_msg_item_clicked_cb(lv_event_t *e);

extern VOID_T __on_get_detection_msg();

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */
/**
 * @brief Apply black background and white text style to the opened dropdown list
 * @param[in] dropdown the dropdown object
 * @return none
 * @note Must be called after lv_dropdown_open() so that the list object exists
 */
STATIC VOID_T __detection_dropdown_style_apply(lv_obj_t *dropdown)
{
    lv_obj_t *dd_list = lv_dropdown_get_list(dropdown);
    if (dd_list == NULL) {
        return;
    }
    lv_obj_set_style_bg_color(dd_list, lv_color_hex(0xB8BDDE), 0);
    lv_obj_set_style_bg_opa(dd_list, LV_OPA_10, 0);
    lv_obj_set_style_border_width(dd_list, 1, 0);
    lv_obj_set_style_border_color(dd_list, lv_color_white(), 0);
    lv_obj_set_style_border_opa(dd_list, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(dd_list, lv_color_white(), 0);
    lv_obj_set_style_text_color(dd_list, lv_color_white(), LV_PART_SELECTED);
    lv_obj_set_style_bg_color(dd_list, lv_color_hex(0x333333), LV_PART_SELECTED);

    /* White border between items */
    lv_obj_set_style_border_width(dd_list, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(dd_list, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_border_opa(dd_list, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_side(dd_list, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS);
}

/**
 * @brief Query detection message list from cloud
 *
 * Sends an HTTP POST to retrieve recent AI robot detection messages
 * within the last MD_MSG_QUERY_RANGE seconds.
 *
 * @param[in] page_num page number to query (1-based)
 * @return OPRT_OK on success, error code on failure
 */
STATIC OPERATE_RET __detection_query_msg_list(INT_T page_num)
{
    OPERATE_RET rt = OPRT_OK;
    ty_cJSON *result = NULL;
    CHAR_T post_content[128] = {0};
    TIME_T now = tal_time_get_posix();
    TIME_T start = now - MD_MSG_QUERY_RANGE;

    snprintf(post_content, sizeof(post_content),
             "{\"startTime\":%ld,\"endTime\":%ld,\"pageNum\":%d,\"pageSize\":%d}",
             (long)start, (long)now, page_num, MD_MSG_PAGE_SIZE);

    rt = iot_httpc_common_post_simple(MD_MSG_LIST_API, MD_MSG_LIST_VER,
                                      post_content, NULL, &result);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("[detection_ui] msg list request failed, rt=%d", rt);
        return rt;
    }

    if (result) {
        memset(s_page_items, 0, sizeof(s_page_items));
        s_page_item_count = 0;

        /* Parse totalCount */
        ty_cJSON *total_count_json = ty_cJSON_GetObjectItem(result, "totalCount");
        if (total_count_json && ty_cJSON_GetStringValue(total_count_json)) {
            s_total_count = atoi(ty_cJSON_GetStringValue(total_count_json));
        } else if (total_count_json && ty_cJSON_IsNumber(total_count_json)) {
            s_total_count = total_count_json->valueint;
        } else {
            s_total_count = 0;
        }

        /* Calculate total pages */
        s_total_pages = (s_total_count + MD_MSG_PAGE_SIZE - 1) / MD_MSG_PAGE_SIZE;
        if (s_total_pages < 1) {
            s_total_pages = 1;
        }
        if (s_total_pages > DETECTION_MAX_PAGE) {
            s_total_pages = DETECTION_MAX_PAGE;
        }

        /* Parse datas array */
        ty_cJSON *datas = ty_cJSON_GetObjectItem(result, "datas");
        if (datas && ty_cJSON_IsArray(datas)) {
            INT_T count = ty_cJSON_GetArraySize(datas);
            if (count > MD_MSG_PAGE_SIZE) {
                count = MD_MSG_PAGE_SIZE;
            }
            for (INT_T i = 0; i < count; i++) {
                ty_cJSON *item = ty_cJSON_GetArrayItem(datas, i);
                if (item == NULL) continue;

                ty_cJSON *title_json = ty_cJSON_GetObjectItem(item, "msgTitle");
                ty_cJSON *datetime_json = ty_cJSON_GetObjectItem(item, "dateTime");
                ty_cJSON *pics_json = ty_cJSON_GetObjectItem(item, "attachPics");

                if (title_json && ty_cJSON_GetStringValue(title_json)) {
                    snprintf(s_page_items[i].msgTitle, sizeof(s_page_items[i].msgTitle),
                             "%s", ty_cJSON_GetStringValue(title_json));
                }
                if (datetime_json && ty_cJSON_GetStringValue(datetime_json)) {
                    snprintf(s_page_items[i].dateTime, sizeof(s_page_items[i].dateTime),
                             "%s", ty_cJSON_GetStringValue(datetime_json));
                }
                if (pics_json && ty_cJSON_GetStringValue(pics_json)) {
                    snprintf(s_page_items[i].attachPics, sizeof(s_page_items[i].attachPics),
                             "%s", ty_cJSON_GetStringValue(pics_json));
                }
                s_page_item_count++;
            }
        }

        TAL_PR_INFO("[detection_ui] page %d: totalCount=%d, items=%d", page_num, s_total_count, s_page_item_count);
        ty_cJSON_Delete(result);

        __detection_update_dropdown_pages(s_total_pages);
        __detection_refresh_content();
    }

    return OPRT_OK;
}

/**
 * @brief Back button click callback
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __detection_back_btn_clicked_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        desk_handle_ui_back(LV_SCR_LOAD_ANIM_FADE_ON, DHUI_SWITCH_PERMANENT);
    }
}

/**
 * @brief AI detection button click callback
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __detection_ai_btn_clicked_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        TAL_PR_INFO("ai detection button clicked");
        __on_get_detection_msg();
    }
}

/**
 * @brief Page dropdown value changed callback
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __detection_page_dropdown_changed_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t *dropdown = lv_event_get_target(e);
        s_current_page = lv_dropdown_get_selected(dropdown) + 1;
        TAL_PR_DEBUG("detection page changed to %d", s_current_page);
        lv_dropdown_close(dropdown);
        lv_obj_add_flag(dropdown, LV_OBJ_FLAG_HIDDEN);
        __detection_query_msg_list(s_current_page);
    }
}

/**
 * @brief Create title bar with back button, title label and page dropdown
 * @return none
 */
STATIC VOID_T __detection_title_bar_create(VOID_T)
{
    lv_detection_ui_t *ui = &getContent()->st_func_detection;

    ui->title_bar = lv_obj_create(ui->detection_scr);
    lv_obj_remove_style_all(ui->title_bar);
    lv_obj_set_size(ui->title_bar, DETECTION_TITLE_BAR_W, DETECTION_TITLE_BAR_H);
    lv_obj_set_pos(ui->title_bar, 0, 0);
    lv_obj_set_style_bg_opa(ui->title_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui->title_bar, 0, 0);
    lv_obj_set_style_pad_all(ui->title_bar, 0, 0);

    /* Back button: top-left 50x50 */
    lv_obj_t *back_btn = lv_btn_create(ui->title_bar);
    lv_obj_remove_style_all(back_btn);
    lv_obj_set_size(back_btn, DETECTION_BACK_BTN_SIZE, DETECTION_BACK_BTN_SIZE);
    lv_obj_set_pos(back_btn, 0, 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(back_btn, __detection_back_btn_clicked_cb, LV_EVENT_CLICKED, NULL);
    if (png_img_load(tuya_app_gui_get_picture_full_path(ICON_BACK_LEFT_24_24),
                     &s_detection_res.back_icon) == 0) {
        lv_obj_t *back_icon = lv_img_create(back_btn);
        lv_img_set_src(back_icon, &s_detection_res.back_icon);
        lv_obj_align(back_icon, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_size(back_icon, 24, 24);
    }

    /* Title label: centered in title bar */
    lv_obj_t *title_label = lv_label_create(ui->title_bar);
    lv_label_set_text(title_label, "侦测记录");
    lv_obj_set_style_text_font(title_label, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
    lv_obj_set_size(title_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(title_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);

    /* Page dropdown button: top-right 50x50 */
    lv_obj_t *list_btn = lv_btn_create(ui->title_bar);
    lv_obj_remove_style_all(list_btn);
    lv_obj_set_size(list_btn, DETECTION_LIST_BTN_SIZE, DETECTION_LIST_BTN_SIZE);
    lv_obj_align(list_btn, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_opa(list_btn, LV_OPA_TRANSP, 0);
    if (png_img_load(tuya_app_gui_get_picture_full_path(DETECTION_MSG_LIST),
                     &s_detection_res.list_icon) == 0) {
        lv_obj_t *list_icon = lv_img_create(list_btn);
        lv_img_set_src(list_icon, &s_detection_res.list_icon);
        lv_obj_align(list_icon, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_size(list_icon, 23, 18);
    }

    /* AI detection button: 50x50, 20px left of page_dropdown */
    ui->ai_detection_btn = lv_btn_create(ui->title_bar);
    lv_obj_remove_style_all(ui->ai_detection_btn);
    lv_obj_set_size(ui->ai_detection_btn, DETECTION_LIST_BTN_SIZE, DETECTION_LIST_BTN_SIZE);
    lv_obj_set_pos(ui->ai_detection_btn, DETECTION_TITLE_BAR_W - 50 - 20 - 50, 0);
    lv_obj_set_style_bg_opa(ui->ai_detection_btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(ui->ai_detection_btn, __detection_ai_btn_clicked_cb, LV_EVENT_CLICKED, NULL);
    if (png_img_load(tuya_app_gui_get_picture_full_path(ICON_AI_CAMERA_ON),
                     &s_detection_res.ai_camera_icon) == 0) {
        lv_obj_t *ai_icon = lv_img_create(ui->ai_detection_btn);
        lv_img_set_src(ai_icon, &s_detection_res.ai_camera_icon);
        lv_obj_align(ai_icon, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_size(ai_icon, 24, 24);
    }

    /* Page dropdown: right-aligned, top at content area */
    ui->page_dropdown = lv_dropdown_create(ui->detection_scr);
    lv_obj_set_size(ui->page_dropdown, 50, 30);
    lv_obj_set_pos(ui->page_dropdown, DETECTION_TITLE_BAR_W - 50, DETECTION_TITLE_BAR_H);
    lv_obj_set_style_bg_color(ui->page_dropdown, lv_color_hex(0xB8BDDE), 0);
    lv_obj_set_style_bg_opa(ui->page_dropdown, LV_OPA_10, 0);
    lv_obj_set_style_border_width(ui->page_dropdown, 1, 0);
    lv_obj_set_style_border_color(ui->page_dropdown, lv_color_white(), 0);
    lv_obj_set_style_border_opa(ui->page_dropdown, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(ui->page_dropdown, lv_color_white(), 0);
    lv_obj_set_style_text_font(ui->page_dropdown, &AlibabaPuHuiTi3_Regular16, 0);
    lv_obj_set_style_pad_all(ui->page_dropdown, 4, 0);

    lv_dropdown_set_dir(ui->page_dropdown, LV_DIR_BOTTOM);
    lv_dropdown_set_symbol(ui->page_dropdown, NULL);

    /* Build page number options: "1\n2\n3\n..." */
    STATIC CHAR_T page_options[DETECTION_MAX_PAGE * 4];
    INT_T offset = 0;
    for (INT_T i = 1; i <= DETECTION_MAX_PAGE; i++) {
        if (i > 1) {
            page_options[offset++] = '\n';
        }
        offset += snprintf(page_options + offset, sizeof(page_options) - offset, "%d", i);
    }
    page_options[offset] = '\0';
    lv_dropdown_set_options(ui->page_dropdown, page_options);

    /* Dropdown list style is applied dynamically when opened (see __detection_dropdown_style_apply) */

    lv_dropdown_set_selected(ui->page_dropdown, 0);
    lv_obj_add_flag(ui->page_dropdown, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(ui->page_dropdown, __detection_page_dropdown_changed_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* Make list_btn toggle dropdown visibility */
    lv_obj_add_event_cb(list_btn, __detection_list_btn_clicked_cb, LV_EVENT_CLICKED, NULL);
}

/**
 * @brief List button click callback to toggle dropdown visibility
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __detection_list_btn_clicked_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        lv_detection_ui_t *ui = &getContent()->st_func_detection;
        if (ui->page_dropdown == NULL) {
            return;
        }
        if (lv_obj_has_flag(ui->page_dropdown, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_clear_flag(ui->page_dropdown, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(ui->page_dropdown);
            lv_dropdown_open(ui->page_dropdown);
            __detection_dropdown_style_apply(ui->page_dropdown);
        } else {
            lv_dropdown_close(ui->page_dropdown);
            lv_obj_add_flag(ui->page_dropdown, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/**
 * @brief Create content area for detection message list
 * @return none
 */
STATIC VOID_T __detection_content_create(VOID_T)
{
    lv_detection_ui_t *ui = &getContent()->st_func_detection;

    ui->content_cont = lv_obj_create(ui->detection_scr);
    lv_obj_remove_style_all(ui->content_cont);
    lv_obj_set_size(ui->content_cont, DETECTION_CONTENT_W, DETECTION_CONTENT_H);
    lv_obj_set_pos(ui->content_cont, 0, DETECTION_TITLE_BAR_H);
    lv_obj_set_style_bg_opa(ui->content_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui->content_cont, 0, 0);
    lv_obj_set_style_pad_all(ui->content_cont, 0, 0);
    lv_obj_set_scrollbar_mode(ui->content_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(ui->content_cont, LV_DIR_VER);

    /* Loading spinner: 64x64, centered in content area */
    // ui->loading_spinner = lv_spinner_create(ui->content_cont, 1000, 60);
    // lv_obj_set_size(ui->loading_spinner, 64, 64);
    // lv_obj_align(ui->loading_spinner, LV_ALIGN_CENTER, 0, 0);
    // lv_obj_set_style_arc_width(ui->loading_spinner, 4, 0);
    // lv_obj_set_style_arc_color(ui->loading_spinner, lv_color_hex(0x3D3D3D), 0);
    // lv_obj_set_style_arc_width(ui->loading_spinner, 4, LV_PART_INDICATOR);
    // lv_obj_set_style_arc_color(ui->loading_spinner, lv_color_hex(0x5B8CFF), LV_PART_INDICATOR);
}

/**
 * @brief Update dropdown page options based on actual total pages
 * @param[in] total_pages calculated total page count
 * @return none
 */
STATIC VOID_T __detection_update_dropdown_pages(INT_T total_pages)
{
    lv_detection_ui_t *ui = &getContent()->st_func_detection;
    STATIC CHAR_T page_options[DETECTION_MAX_PAGE * 4];
    INT_T offset = 0;

    if (ui->page_dropdown == NULL) {
        return;
    }
    if (total_pages < 1) {
        total_pages = 1;
    }
    if (total_pages > DETECTION_MAX_PAGE) {
        total_pages = DETECTION_MAX_PAGE;
    }

    offset = 0;
    for (INT_T i = 1; i <= total_pages; i++) {
        if (i > 1) {
            page_options[offset++] = '\n';
        }
        offset += snprintf(page_options + offset, sizeof(page_options) - offset, "%d", i);
    }
    page_options[offset] = '\0';

    lv_dropdown_set_options(ui->page_dropdown, page_options);

    if (s_current_page > total_pages) {
        s_current_page = total_pages;
    }
    lv_dropdown_set_selected(ui->page_dropdown, s_current_page - 1);
}

/**
 * @brief Detection message item click callback - prints attachPics URL
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __detection_msg_item_clicked_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        INT_T index = (INT_T)(uintptr_t)lv_event_get_user_data(e);
        if (index >= 0 && index < s_page_item_count) {
            TAL_PR_INFO("[detection_ui] item clicked, index=%d, attachPics=%s",
                        index, s_page_items[index].attachPics);
        }
    }
}

/**
 * @brief Refresh content area with current page message items
 * @return none
 */
STATIC VOID_T __detection_refresh_content(VOID_T)
{
    lv_detection_ui_t *ui = &getContent()->st_func_detection;

    if (ui->content_cont == NULL) {
        return;
    }

    lv_obj_clean(ui->content_cont);

    lv_obj_set_flex_flow(ui->content_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui->content_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(ui->content_cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ui->content_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_ver(ui->content_cont, 8, 0);
    lv_obj_set_style_pad_hor(ui->content_cont, 15, 0);
    lv_obj_set_style_pad_row(ui->content_cont, 5, 0);
    lv_obj_set_style_pad_column(ui->content_cont, 0, 0);

    if (s_page_item_count == 0) {
        lv_obj_t *empty_label = lv_label_create(ui->content_cont);
        lv_obj_remove_style_all(empty_label);
        lv_label_set_text(empty_label, "暂无侦测记录");
        lv_obj_set_size(empty_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_text_font(empty_label, &AlibabaPuHuiTi3_Regular18_Static, 0);
        lv_obj_set_style_text_color(empty_label, lv_color_hex(0xB8BDDE), 0);
        return;
    }

    for (INT_T i = 0; i < s_page_item_count; i++) {
        lv_obj_t *item_cont = lv_obj_create(ui->content_cont);
        lv_obj_remove_style_all(item_cont);
        lv_obj_set_size(item_cont, 290, 50);
        lv_obj_add_flag(item_cont, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(item_cont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(item_cont, 16, 0);
        lv_obj_set_style_bg_opa(item_cont, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(item_cont, lv_color_hex(0x353740), 0);
        lv_obj_set_style_pad_left(item_cont, 12, 0);
        lv_obj_set_style_pad_right(item_cont, 12, 0);
        lv_obj_set_style_pad_top(item_cont, 6, 0);
        lv_obj_set_style_pad_bottom(item_cont, 6, 0);
        lv_obj_set_style_border_width(item_cont, 0, 0);
        lv_obj_set_scrollbar_mode(item_cont, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_scroll_dir(item_cont, LV_DIR_NONE);

        lv_obj_add_event_cb(item_cont, __detection_msg_item_clicked_cb,
                            LV_EVENT_CLICKED, (void *)(uintptr_t)i);

        lv_obj_t *title_label = lv_label_create(item_cont);
        lv_obj_remove_style_all(title_label);
        lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
        lv_label_set_text(title_label, s_page_items[i].msgTitle);
        lv_obj_set_size(title_label, 260, LV_SIZE_CONTENT);
        lv_obj_set_pos(title_label, 0, 0);
        lv_obj_set_style_text_font(title_label, &AlibabaPuHuiTi3_Regular18_Static, 0);
        lv_obj_set_style_text_color(title_label, lv_color_white(), 0);

        lv_obj_t *date_label = lv_label_create(item_cont);
        lv_obj_remove_style_all(date_label);
        lv_label_set_long_mode(date_label, LV_LABEL_LONG_DOT);
        lv_label_set_text(date_label, s_page_items[i].dateTime);
        lv_obj_set_size(date_label, 260, LV_SIZE_CONTENT);
        lv_obj_set_pos(date_label, 0, 20);
        lv_obj_set_style_text_font(date_label, &AlibabaPuHuiTi3_Regular16, 0);
        lv_obj_set_style_text_color(date_label, lv_color_hex(0xB8BDDE), 0);
    }
}

/**
 * @brief Create and initialize the detection message list screen
 * @return none
 */
void setup_detection_scr(void)
{
    TAL_PR_INFO("[%s] enter", __func__);
    lv_detection_ui_t *ui = &getContent()->st_func_detection;

    ui->detection_scr = lv_obj_create(NULL);
    lv_obj_set_size(ui->detection_scr, DESK_LCD_WIDTH, DESK_LCD_HEIGHT);
    lv_obj_set_style_bg_color(ui->detection_scr, lv_color_hex(0x252624), 0);
    lv_obj_set_style_bg_opa(ui->detection_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(ui->detection_scr, 0, 0);
    lv_obj_set_scrollbar_mode(ui->detection_scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(ui->detection_scr, LV_DIR_NONE);

    __detection_title_bar_create();
    __detection_content_create();

    lv_obj_update_layout(ui->detection_scr);
    s_current_page = 1;

    __detection_query_msg_list(1);
}

/**
 * @brief Release all resources held by the detection screen
 * @return none
 */
void detection_scr_res_clear(void)
{
    TAL_PR_INFO("[%s] enter", __func__);
    lv_detection_ui_t *ui = &getContent()->st_func_detection;

    png_img_unload(&s_detection_res.back_icon);
    png_img_unload(&s_detection_res.list_icon);
    png_img_unload(&s_detection_res.ai_camera_icon);
    memset(&s_detection_res, 0, sizeof(detection_scr_res_t));

    ui->detection_scr = NULL;
    ui->title_bar = NULL;
    ui->content_cont = NULL;
    ui->page_dropdown = NULL;
    ui->ai_detection_btn = NULL;
    ui->loading_spinner = NULL;

    s_current_page = 1;
    s_total_count = 0;
    s_total_pages = 0;
    s_page_item_count = 0;
    memset(s_page_items, 0, sizeof(s_page_items));
}
