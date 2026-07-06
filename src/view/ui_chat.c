/**
 * @file ui_chat.c
 * @brief Chat screen UI for T5AI_BOARD (320x480)
 * @version 1.0
 * @date 2025-04-02
 * @copyright Copyright (c) Tuya Inc.
 */
#include "ui_common.h"
#include "uni_log.h"
#include "tal_image_jpeg_codec.h"
#include "tal_image_scale.h"
#include "tal_memory.h"
#include <string.h>

/* ---------------------------------------------------------------------------
 * Font / icon declarations
 * --------------------------------------------------------------------------- */
LV_FONT_DECLARE(AlibabaPuHuiTi3_Regular18_Static);
LV_IMG_DECLARE(icon_ai_icon);

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define CHAT_BG_COLOR        0x25262A
#define CHAT_AI_BUBBLE_OPA   0
#define CHAT_USER_BUBBLE_BG  0xB8BDDE
#define CHAT_USER_BUBBLE_OPA 28
#define CHAT_BUBBLE_RADIUS   15
#define CHAT_BUBBLE_PAD      12
#define CHAT_ICON_SIZE       48
#define CHAT_TOP_H           58
#define CHAT_MSG_PAD_H       10
#define CHAT_MSG_PAD_V       6
#define CHAT_STREAM_BUF_SIZE 2048
#define CHAT_LINK_COLOR      0x5B9BD5

#define CHAT_ATTACH_BAR_H      60
#define CHAT_ATTACH_THUMB_SIZE 44
#define CHAT_ATTACH_BAR_COLOR  0x2F3036
#define CHAT_ATTACH_BORDER_CLR 0x3E3F44

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef struct {
    UI_CHAT_LINK_CB cb;
    VOID_T         *cb_arg;
    UINT32_T        arg_len;
} CHAT_LINK_CTX_T;

typedef struct {
    lv_obj_t *chat_scr;
    lv_obj_t *ai_icon;
    lv_obj_t *msg_container;
    lv_obj_t *stream_label;
    lv_obj_t *stream_row;
    CHAR_T    stream_buf[CHAT_STREAM_BUF_SIZE];
    UINT32_T  stream_len;
    lv_obj_t *attach_bar;
    lv_obj_t *attach_canvas;
    uint8_t  *attach_buf;
} CHAT_UI_T;

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC CHAT_UI_T s_chat_ui = {0};
STATIC lv_style_t s_style_ai_bubble;
STATIC lv_style_t s_style_user_bubble;
STATIC BOOL_T s_styles_inited = FALSE;
STATIC lv_obj_t *s_image_overlay = NULL;
STATIC lv_obj_t *s_image_canvas  = NULL;
STATIC uint8_t  *s_image_buf     = NULL;

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC VOID_T __chat_ensure_visible(VOID_T);
STATIC VOID_T __chat_gesture_cb(lv_event_t *e);
STATIC VOID_T __chat_attach_close_cb(lv_event_t *e);

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */

/**
 * @brief Initialize chat bubble styles (once)
 * @return none
 */
STATIC VOID_T __chat_styles_init(VOID_T)
{
    if (s_styles_inited) {
        return;
    }

    lv_style_init(&s_style_ai_bubble);
    lv_style_set_bg_opa(&s_style_ai_bubble, CHAT_AI_BUBBLE_OPA);
    lv_style_set_text_color(&s_style_ai_bubble, lv_color_white());
    lv_style_set_radius(&s_style_ai_bubble, CHAT_BUBBLE_RADIUS);
    lv_style_set_pad_all(&s_style_ai_bubble, CHAT_BUBBLE_PAD);
    lv_style_set_shadow_width(&s_style_ai_bubble, 0);
    lv_style_set_border_width(&s_style_ai_bubble, 0);

    lv_style_init(&s_style_user_bubble);
    lv_style_set_bg_color(&s_style_user_bubble, lv_color_hex(CHAT_USER_BUBBLE_BG));
    lv_style_set_bg_opa(&s_style_user_bubble, CHAT_USER_BUBBLE_OPA);
    lv_style_set_text_color(&s_style_user_bubble, lv_color_white());
    lv_style_set_radius(&s_style_user_bubble, CHAT_BUBBLE_RADIUS);
    lv_style_set_pad_all(&s_style_user_bubble, CHAT_BUBBLE_PAD);
    lv_style_set_shadow_width(&s_style_user_bubble, 0);
    lv_style_set_border_width(&s_style_user_bubble, 0);

    s_styles_inited = TRUE;
}

/**
 * @brief Create a message row container with bubble
 * @param[in] is_ai TRUE for AI message (left-aligned), FALSE for user (right-aligned)
 * @return bubble inner container for adding content
 */
STATIC lv_obj_t *__chat_create_bubble(BOOL_T is_ai)
{
    lv_obj_t *msg_row = lv_obj_create(s_chat_ui.msg_container);
    lv_obj_remove_style_all(msg_row);
    lv_obj_set_size(msg_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(msg_row, CHAT_MSG_PAD_V, 0);
    lv_obj_set_flex_flow(msg_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(msg_row,
                          is_ai ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(msg_row, CHAT_MSG_PAD_H, 0);

    lv_coord_t max_w = is_ai ? (LV_HOR_RES - 40) : (LV_HOR_RES - 60);

    lv_obj_t *bubble = lv_obj_create(msg_row);
    lv_obj_set_width(bubble, LV_SIZE_CONTENT);
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(bubble, max_w, 0);
    lv_obj_add_style(bubble, is_ai ? &s_style_ai_bubble : &s_style_user_bubble, 0);
    lv_obj_set_scrollbar_mode(bubble, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(bubble, LV_DIR_NONE);
    lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *content = lv_obj_create(bubble);
    lv_obj_remove_style_all(content);
    lv_obj_set_width(content, LV_SIZE_CONTENT);
    lv_obj_set_height(content, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(content, max_w - CHAT_BUBBLE_PAD * 2, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(content, LV_DIR_NONE);

    return content;
}

/**
 * @brief Free link context when the label object is deleted
 */
STATIC VOID_T __chat_link_delete_cb(lv_event_t *e)
{
    CHAT_LINK_CTX_T *ctx = (CHAT_LINK_CTX_T *)lv_event_get_user_data(e);
    if (ctx) {
        if (ctx->cb_arg) {
            lv_mem_free(ctx->cb_arg);
        }
        lv_mem_free(ctx);
    }
}

/**
 * @brief Event handler for hyperlink click
 */
STATIC VOID_T __chat_link_click_cb(lv_event_t *e)
{
    CHAT_LINK_CTX_T *ctx = (CHAT_LINK_CTX_T *)lv_event_get_user_data(e);
    if (ctx && ctx->cb) {
        ctx->cb(ctx->cb_arg);
    }
}

/**
 * @brief Add a clickable hyperlink to the chat (AI side)
 * @param[in] text display text for the link
 * @param[in] cb callback invoked when the link is clicked
 * @param[in] cb_arg argument data to copy (can be NULL if arg_len is 0)
 * @param[in] arg_len size in bytes of cb_arg data to copy
 * @return none
 */
VOID_T ui_chat_add_link(CHAT_MSG_ROLE_TP_E type, CONST CHAR_T *text, UI_CHAT_LINK_CB cb,\
                        CONST VOID_T *cb_arg, UINT32_T arg_len)
{
    if (text == NULL || cb == NULL) {
        return;
    }

    __chat_ensure_visible();

    BOOL_T is_ai = (type == CHAT_MSG_ROLE_AI);
    lv_obj_t *content = __chat_create_bubble(is_ai);

    lv_coord_t max_label_w = is_ai ? (LV_HOR_RES - 40 - CHAT_BUBBLE_PAD * 2)
                                    : (LV_HOR_RES - 60 - CHAT_BUBBLE_PAD * 2);
    lv_obj_t *label = lv_label_create(content);
    lv_label_set_text(label, text);
    lv_obj_update_layout(label);
    if (lv_obj_get_width(label) > max_label_w) {
        lv_obj_set_width(label, max_label_w);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    }
    lv_obj_set_style_text_color(label, lv_color_hex(CHAT_LINK_COLOR), 0);
    lv_obj_set_style_text_decor(label, LV_TEXT_DECOR_UNDERLINE, 0);

    CHAT_LINK_CTX_T *ctx = lv_mem_alloc(sizeof(CHAT_LINK_CTX_T));
    if (ctx == NULL) {
        return;
    }
    ctx->cb      = cb;
    ctx->cb_arg  = NULL;
    ctx->arg_len = arg_len;

    if (cb_arg && arg_len > 0) {
        ctx->cb_arg = lv_mem_alloc(arg_len);
        if (ctx->cb_arg == NULL) {
            lv_mem_free(ctx);
            return;
        }
        memcpy(ctx->cb_arg, cb_arg, arg_len);
    }

    lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(label, __chat_link_click_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_add_event_cb(label, __chat_link_delete_cb, LV_EVENT_DELETE, ctx);

    lv_obj_t *msg_row = lv_obj_get_parent(lv_obj_get_parent(content));
    lv_obj_scroll_to_view(msg_row, LV_ANIM_ON);
    lv_obj_update_layout(s_chat_ui.msg_container);
}

/**
 * @brief Click handler for fullscreen image overlay — hide overlay
 */
STATIC VOID_T __chat_image_click_cb(lv_event_t *e)
{
    (VOID_T)e;
    if (s_image_overlay) {
        lv_obj_add_flag(s_image_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_image_buf) {
        tal_free(s_image_buf);
        s_image_buf = NULL;
    }
}

/**
 * @brief Display a JPEG image fullscreen over the chat screen, click to dismiss
 */
VOID_T ui_chat_disp_image(CONST UINT8_T *jpeg_data, UINT32_T jpeg_len)
{
    if (jpeg_data == NULL || jpeg_len == 0) {
        return;
    }

    TAL_IMAGE_JPEG_INFO_T info = {0};
    if (tal_image_jpeg_get_info(jpeg_data, jpeg_len, &info) != OPRT_OK) {
        PR_ERR("chat: jpeg get info failed");
        return;
    }

    /* Decode JPEG to RGB565 */
    uint32_t rgb565_size = info.width * info.height * 2;
    uint8_t *rgb565_buf = tal_malloc(rgb565_size);
    if (rgb565_buf == NULL) {
        PR_ERR("chat: malloc rgb565 buf failed, size=%u", rgb565_size);
        return;
    }

    TAL_IMAGE_JPEG_OUTPUT_T out = {0};
    out.out_buf      = rgb565_buf;
    out.out_buf_size = rgb565_size;
    out.out_width    = info.width;
    out.out_height   = info.height;

    if (tal_image_jpeg_decode_rgb565(jpeg_data, jpeg_len, &out) != OPRT_OK) {
        PR_ERR("chat: jpeg decode rgb565 failed");
        tal_free(rgb565_buf);
        return;
    }

    __chat_ensure_visible();

    if (s_image_overlay == NULL || s_image_canvas == NULL) {
        PR_ERR("chat: image overlay not initialized");
        tal_free(rgb565_buf);
        return;
    }

    /* Free previous buffer */
    if (s_image_buf) {
        tal_free(s_image_buf);
    }
    s_image_buf = rgb565_buf;

    /* Update canvas and show overlay */
    lv_canvas_set_buffer(s_image_canvas, rgb565_buf, info.width, info.height, LV_IMG_CF_TRUE_COLOR);
    lv_obj_center(s_image_canvas);
    lv_obj_clear_flag(s_image_overlay, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Close button callback for attachment bar
 */
STATIC VOID_T __chat_attach_close_cb(lv_event_t *e)
{
    (VOID_T)e;
    ui_chat_clear_attachment();
}

/**
 * @brief Set a JPEG image as pending attachment thumbnail at chat bottom
 */
VOID_T ui_chat_set_attachment_jpeg(CONST UINT8_T *jpeg_data, UINT32_T jpeg_len)
{
    if (jpeg_data == NULL || jpeg_len == 0) {
        return;
    }

    __chat_ensure_visible();

    if (s_chat_ui.attach_bar == NULL || s_chat_ui.attach_canvas == NULL) {
        return;
    }

    TAL_IMAGE_JPEG_SCALE_IN_T in = {0};
    in.method     = TAL_IMAGE_SCALE_MTH_BILINEAR;
    in.mode       = TAL_IMAGE_SCALE_MODE_SIZE;
    in.data       = (uint8_t *)jpeg_data;
    in.size       = jpeg_len;
    in.out_width  = CHAT_ATTACH_THUMB_SIZE;
    in.out_height = CHAT_ATTACH_THUMB_SIZE;

    TAL_IMAGE_SCALE_OUT_T out = {0};
    if (tal_image_jpeg_scale_rgb565(&in, &out) != OPRT_OK) {
        PR_ERR("chat: attach thumbnail scale failed");
        return;
    }

    if (s_chat_ui.attach_buf) {
        tal_image_scale_buf_free(&(TAL_IMAGE_SCALE_OUT_T){.buf = s_chat_ui.attach_buf});
    }
    s_chat_ui.attach_buf = out.buf;

    lv_canvas_set_buffer(s_chat_ui.attach_canvas, s_chat_ui.attach_buf,
                         out.width, out.height, LV_IMG_CF_TRUE_COLOR);
    lv_obj_invalidate(s_chat_ui.attach_canvas);

    lv_obj_clear_flag(s_chat_ui.attach_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(s_chat_ui.msg_container, LV_VER_RES - CHAT_TOP_H - CHAT_ATTACH_BAR_H);
    lv_obj_update_layout(s_chat_ui.msg_container);
}

/**
 * @brief Clear the pending attachment and restore chat layout
 */
VOID_T ui_chat_clear_attachment(VOID_T)
{
    if (s_chat_ui.attach_buf) {
        tal_image_scale_buf_free(&(TAL_IMAGE_SCALE_OUT_T){.buf = s_chat_ui.attach_buf});
        s_chat_ui.attach_buf = NULL;
    }
    if (s_chat_ui.attach_bar) {
        lv_obj_add_flag(s_chat_ui.attach_bar, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_chat_ui.msg_container) {
        lv_obj_set_height(s_chat_ui.msg_container, LV_VER_RES - CHAT_TOP_H);
    }
}

/**
 * @brief Start a new AI streaming text message (creates bubble, ready for append)
 * @return none
 */
VOID_T ui_chat_stream_begin(VOID_T)
{
    __chat_ensure_visible();

    s_chat_ui.stream_buf[0] = '\0';
    s_chat_ui.stream_len = 0;

    lv_obj_t *content = __chat_create_bubble(TRUE);

    s_chat_ui.stream_label = lv_label_create(content);
    lv_obj_set_width(s_chat_ui.stream_label, LV_HOR_RES - 40 - CHAT_BUBBLE_PAD * 2);
    lv_label_set_long_mode(s_chat_ui.stream_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_chat_ui.stream_label, "");

    s_chat_ui.stream_row = lv_obj_get_parent(lv_obj_get_parent(content));
}

/**
 * @brief Append text chunk to the current AI streaming message
 * @param[in] chunk text fragment to append
 * @return none
 */
VOID_T ui_chat_stream_append(CONST CHAR_T *chunk)
{
    if (s_chat_ui.stream_label == NULL || chunk == NULL) {
        return;
    }

    UINT32_T chunk_len = strlen(chunk);
    if (s_chat_ui.stream_len + chunk_len >= CHAT_STREAM_BUF_SIZE - 1) {
        chunk_len = CHAT_STREAM_BUF_SIZE - 1 - s_chat_ui.stream_len;
    }
    if (chunk_len == 0) {
        return;
    }

    memcpy(s_chat_ui.stream_buf + s_chat_ui.stream_len, chunk, chunk_len);
    s_chat_ui.stream_len += chunk_len;
    s_chat_ui.stream_buf[s_chat_ui.stream_len] = '\0';

    lv_label_set_text(s_chat_ui.stream_label, s_chat_ui.stream_buf);

    if (s_chat_ui.stream_row) {
        lv_obj_scroll_to_view(s_chat_ui.stream_row, LV_ANIM_ON);
    }
    lv_obj_update_layout(s_chat_ui.msg_container);
}

/**
 * @brief End the current AI streaming message
 * @return none
 */
VOID_T ui_chat_stream_end(VOID_T)
{
    s_chat_ui.stream_label = NULL;
    s_chat_ui.stream_row = NULL;
    s_chat_ui.stream_len = 0;
}

/**
 * @brief Add a text message to the chat
 * @param[in] role
 * @param[in] text message text string
 * @return none
 */
VOID_T ui_chat_add_text(CHAT_MSG_ROLE_TP_E role, CONST CHAR_T *text)
{
    if (text == NULL) {
        return;
    }

    __chat_ensure_visible();

    BOOL_T is_ai = (role == CHAT_MSG_ROLE_AI);
    lv_obj_t *content = __chat_create_bubble(is_ai);

    lv_coord_t max_label_w = is_ai ? (LV_HOR_RES - 40 - CHAT_BUBBLE_PAD * 2)
                                    : (LV_HOR_RES - 60 - CHAT_BUBBLE_PAD * 2);
    lv_obj_t *label = lv_label_create(content);
    lv_label_set_text(label, text);
    lv_obj_update_layout(label);
    if (lv_obj_get_width(label) > max_label_w) {
        lv_obj_set_width(label, max_label_w);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    }

    lv_obj_t *msg_row = lv_obj_get_parent(lv_obj_get_parent(content));
    lv_obj_scroll_to_view(msg_row, LV_ANIM_ON);
    lv_obj_update_layout(s_chat_ui.msg_container);
}

/**
 * @brief Clear all messages in the chat
 * @return none
 */
VOID_T ui_chat_clear(VOID_T)
{
    if (s_chat_ui.msg_container) {
        lv_obj_clean(s_chat_ui.msg_container);
    }
    s_chat_ui.stream_label = NULL;
    s_chat_ui.stream_row = NULL;
    s_chat_ui.stream_len = 0;
}

/**
 * @brief Create chat screen objects (does NOT load/show it)
 * @return none
 */
VOID_T setup_scr_chat(VOID_T)
{
    if (s_chat_ui.chat_scr) {
        return;
    }

    __chat_styles_init();

    s_chat_ui.chat_scr = lv_obj_create(NULL);
    lv_obj_set_size(s_chat_ui.chat_scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_chat_ui.chat_scr, lv_color_hex(CHAT_BG_COLOR), 0);
    lv_obj_set_style_pad_all(s_chat_ui.chat_scr, 0, 0);
    lv_obj_set_style_text_font(s_chat_ui.chat_scr, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(s_chat_ui.chat_scr, lv_color_white(), 0);
    lv_obj_set_scrollbar_mode(s_chat_ui.chat_scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(s_chat_ui.chat_scr, LV_DIR_NONE);

    s_chat_ui.ai_icon = lv_img_create(s_chat_ui.chat_scr);
    lv_img_set_src(s_chat_ui.ai_icon, &icon_ai_icon);
    lv_obj_align(s_chat_ui.ai_icon, LV_ALIGN_TOP_MID, 0, 6);

    s_chat_ui.msg_container = lv_obj_create(s_chat_ui.chat_scr);
    lv_obj_set_size(s_chat_ui.msg_container, LV_HOR_RES, LV_VER_RES - CHAT_TOP_H);
    lv_obj_set_pos(s_chat_ui.msg_container, 0, CHAT_TOP_H);
    lv_obj_set_flex_flow(s_chat_ui.msg_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_border_width(s_chat_ui.msg_container, 0, 0);
    lv_obj_set_style_bg_opa(s_chat_ui.msg_container, 0, 0);
    lv_obj_set_style_pad_ver(s_chat_ui.msg_container, 8, 0);
    lv_obj_set_style_pad_hor(s_chat_ui.msg_container, CHAT_MSG_PAD_H, 0);
    lv_obj_set_scroll_dir(s_chat_ui.msg_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_chat_ui.msg_container, LV_SCROLLBAR_MODE_OFF);

    s_chat_ui.stream_label = NULL;
    s_chat_ui.stream_row = NULL;
    s_chat_ui.stream_len = 0;

    /* ---- Bottom attachment bar (hidden by default) ---- */
    s_chat_ui.attach_bar = lv_obj_create(s_chat_ui.chat_scr);
    lv_obj_remove_style_all(s_chat_ui.attach_bar);
    lv_obj_set_size(s_chat_ui.attach_bar, LV_HOR_RES, CHAT_ATTACH_BAR_H);
    lv_obj_align(s_chat_ui.attach_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_chat_ui.attach_bar, lv_color_hex(CHAT_ATTACH_BAR_COLOR), 0);
    lv_obj_set_style_bg_opa(s_chat_ui.attach_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(s_chat_ui.attach_bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(s_chat_ui.attach_bar, 1, 0);
    lv_obj_set_style_border_color(s_chat_ui.attach_bar, lv_color_hex(CHAT_ATTACH_BORDER_CLR), 0);
    lv_obj_set_scrollbar_mode(s_chat_ui.attach_bar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(s_chat_ui.attach_bar, LV_OBJ_FLAG_HIDDEN);

    /* Thumbnail container (rounded clip) */
    lv_obj_t *thumb_cont = lv_obj_create(s_chat_ui.attach_bar);
    lv_obj_remove_style_all(thumb_cont);
    lv_obj_set_size(thumb_cont, CHAT_ATTACH_THUMB_SIZE, CHAT_ATTACH_THUMB_SIZE);
    lv_obj_align(thumb_cont, LV_ALIGN_LEFT_MID, CHAT_MSG_PAD_H, 0);
    lv_obj_set_style_radius(thumb_cont, 8, 0);
    lv_obj_set_style_clip_corner(thumb_cont, true, 0);
    lv_obj_set_style_bg_color(thumb_cont, lv_color_hex(0x444444), 0);
    lv_obj_set_style_bg_opa(thumb_cont, LV_OPA_COVER, 0);

    s_chat_ui.attach_canvas = lv_canvas_create(thumb_cont);
    lv_obj_set_pos(s_chat_ui.attach_canvas, 0, 0);
    lv_obj_set_size(s_chat_ui.attach_canvas, CHAT_ATTACH_THUMB_SIZE, CHAT_ATTACH_THUMB_SIZE);
    lv_obj_set_style_border_width(s_chat_ui.attach_canvas, 0, 0);

    /* "x" close badge on top-right of thumbnail */
    lv_obj_t *close_btn = lv_btn_create(s_chat_ui.attach_bar);
    lv_obj_remove_style_all(close_btn);
    lv_obj_set_size(close_btn, 18, 18);
    lv_obj_set_pos(close_btn,
                   CHAT_MSG_PAD_H + CHAT_ATTACH_THUMB_SIZE - 12,
                   (CHAT_ATTACH_BAR_H - CHAT_ATTACH_THUMB_SIZE) / 2 - 6);
    lv_obj_set_style_radius(close_btn, 9, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x666666), 0);
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_COVER, 0);
    lv_obj_set_ext_click_area(close_btn, 8);
    lv_obj_add_event_cb(close_btn, __chat_attach_close_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, "\xC3\x97");  /* × */
    lv_obj_set_style_text_color(close_lbl, lv_color_white(), 0);
    lv_obj_center(close_lbl);

    s_chat_ui.attach_buf = NULL;

    /* Fullscreen image overlay (hidden by default) */
    s_image_overlay = lv_obj_create(s_chat_ui.chat_scr);
    lv_obj_remove_style_all(s_image_overlay);
    lv_obj_set_size(s_image_overlay, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_image_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_image_overlay, LV_OPA_COVER, 0);
    lv_obj_center(s_image_overlay);
    lv_obj_add_flag(s_image_overlay, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_image_overlay, __chat_image_click_cb, LV_EVENT_CLICKED, NULL);

    s_image_canvas = lv_canvas_create(s_image_overlay);
    lv_obj_center(s_image_canvas);

    ui_control_register_gesture(s_chat_ui.chat_scr);
    lv_obj_add_event_cb(s_chat_ui.chat_scr, __chat_gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_update_layout(s_chat_ui.chat_scr);
}

/**
 * @brief Chat screen gesture callback, swipe-right to return to home page
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __chat_gesture_cb(lv_event_t *e)
{
    (VOID_T)e;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_RIGHT) {
        ui_nav_to(UI_SCR_HOME);
    }
}

/**
 * @brief Ensure chat screen is created and currently displayed
 * @return none
 */
/**
 * @brief Ensure chat screen is created and visible. Uses nav stack when called
 *        from message handlers; ui_chat_show bypasses nav (called BY nav).
 * @return none
 */
STATIC VOID_T __chat_ensure_visible(VOID_T)
{
    if (s_chat_ui.chat_scr == NULL) {
        setup_scr_chat();
    }
    if (lv_scr_act() != s_chat_ui.chat_scr) {
        ui_nav_to(UI_SCR_CHAT);
    }
}

/**
 * @brief Show the chat screen (called by ui_nav, does NOT push to nav stack)
 * @return none
 */
VOID_T ui_chat_show(VOID_T)
{
    if (s_chat_ui.chat_scr == NULL) {
        setup_scr_chat();
    }
    if (lv_scr_act() != s_chat_ui.chat_scr) {
        lv_scr_load(s_chat_ui.chat_scr);
    }
}

/**
 * @brief Hide chat screen and return to previous screen
 * @param[in] target_scr screen to switch to (NULL to stay)
 * @return none
 */
VOID_T ui_chat_hide(lv_obj_t *target_scr)
{
    if (target_scr && s_chat_ui.chat_scr && lv_scr_act() == s_chat_ui.chat_scr) {
        lv_scr_load(target_scr);
    }
}

/**
 * @brief Get the chat screen object
 * @return chat screen pointer, NULL if not created
 */
lv_obj_t *ui_chat_get_scr(VOID_T)
{
    return s_chat_ui.chat_scr;
}
