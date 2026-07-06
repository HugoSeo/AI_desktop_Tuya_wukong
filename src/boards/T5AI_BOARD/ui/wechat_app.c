/**
 * @file wechat_app.c
 * @brief WeChat-style chat UI for T5AI_BOARD.
 *        Provides message bubbles, streaming text display,
 *        and clickable image links with fullscreen preview.
 * @version 1.0
 * @copyright Copyright (c) Tuya Inc.
 */
#include "lvgl/lvgl.h"
#include "gui_common.h"
#include "tuya_ai_display.h"
#include "tal_log.h"
#include "tal_memory.h"
#include "tal_system.h"
#if defined(ENABLE_TUYA_PICTURE) && (ENABLE_TUYA_PICTURE == 1)
#include "tal_image_jpeg_codec.h"
#include "wukong_picture.h"
#endif

#define SCREEN_WIDTH  LV_HOR_RES
#define SCREEN_HEIGHT LV_VER_RES

/***********************************************************
***********************variable define**********************
***********************************************************/
static lv_style_t style_avatar;
static lv_style_t style_ai_bubble;
static lv_style_t style_user_bubble;
static lv_style_t style_time;

lv_obj_t* msg_container;
lv_obj_t* title;

static  lv_obj_t    *status_bar_ ;
static  lv_obj_t    *network_label_;
static  lv_obj_t    *status_label_;
static  lv_obj_t    *mode_label_;
static int __s_mode = 0;
static int __s_status = 0;

#if defined(ENABLE_TUYA_PICTURE) && (ENABLE_TUYA_PICTURE == 1)
/** Fullscreen image overlay objects */
static lv_obj_t  *s_image_overlay = NULL;
static lv_obj_t  *s_image_canvas  = NULL;
static uint8_t   *s_image_buf     = NULL;
#endif

/***********************************************************
********************function declaration********************
***********************************************************/
static void SetStatus(uint8_t stat);

/***********************************************************
***********************function define**********************
***********************************************************/

/**
 * @brief Calculate the bubble width based on screen size
 * @return bubble width in pixels
 */
static inline uint32_t calc_bubble_width(void) {
    return SCREEN_WIDTH - 85;
}

LV_FONT_DECLARE(puhui_3bp_18);
LV_IMG_DECLARE(ai);
LV_IMG_DECLARE(user);
LV_FONT_DECLARE(font_awesome_20_4);

#define AI_MESSAGE_FONT    &puhui_3bp_18

/**
 * @brief Create a message row with avatar and bubble
 * @param[out] lable pointer to receive the label widget
 * @param[in] is_ai TRUE for AI side, FALSE for user side
 * @return message container object
 */
static lv_obj_t* create_message(lv_obj_t **lable, bool is_ai)
{
    lv_obj_t* msg_cont = lv_obj_create(msg_container);
    lv_obj_remove_style_all(msg_cont);
    lv_obj_set_size(msg_cont, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(msg_cont, 6, 0);
    lv_obj_set_flex_flow(msg_cont, is_ai ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_ROW_REVERSE);
    lv_obj_set_style_pad_column(msg_cont, 10, 0);

    lv_obj_t* icon = lv_img_create(msg_cont);
    lv_obj_set_size(icon, 40, 40);
    lv_img_set_src(icon, is_ai ? &ai : &user);
    lv_obj_center(icon);

    lv_obj_t* bubble = lv_obj_create(msg_cont);
    lv_obj_set_width(bubble, calc_bubble_width());
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);
    lv_obj_add_style(bubble, is_ai ? &style_ai_bubble : &style_user_bubble, 0);

    lv_obj_set_scrollbar_mode(bubble, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(bubble, LV_DIR_NONE);

    lv_obj_t* text_cont = lv_obj_create(bubble);
    lv_obj_remove_style_all(text_cont);
    lv_obj_set_size(text_cont, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(text_cont, LV_FLEX_FLOW_COLUMN);

    *lable = lv_label_create(text_cont);
    lv_obj_set_width(*lable, calc_bubble_width() - 24);
    lv_label_set_long_mode(*lable, LV_LABEL_LONG_WRAP);

    return msg_cont;
}

#define MAX_TEXT_LEN_IN_ONE_LABEL 1023

#define WECHAT_AI_TEXT_THROTTLE_MS    (50)
static SYS_TIME_T s_last_ai_render_ms = 0;

/**
 * @brief Handle streaming AI text display (start / data / stop)
 * @param[in] type display message type
 * @param[in] data text chunk data
 * @param[in] len text chunk length
 * @return none
 */
static void SetDynamicMessage(TY_DISPLAY_TYPE_E type, uint8_t *data, int len)
{
    static lv_obj_t *lable = NULL;
    static lv_obj_t *parent = NULL;
    static uint8_t buf[MAX_TEXT_LEN_IN_ONE_LABEL + 1] = {0};
    static uint16_t buf_len = 0;
    uint8_t *tmp_data = data;
    int tmp_len = len;
    BOOL_T need_new_label = FALSE;
    SYS_TIME_T now_ms = 0;

    switch (type)
    {
    case TY_DISPLAY_TP_AI_CHAT_START:
        parent = create_message(&lable, true);
        memset(buf, 0, MAX_TEXT_LEN_IN_ONE_LABEL);
        buf_len = 0;
        s_last_ai_render_ms = 0;
        // fall through
    case TY_DISPLAY_TP_AI_CHAT_DATA:
        while (tmp_len > 0)
        {
            if (need_new_label) {
                parent = create_message(&lable, true);
                memset(buf, 0, MAX_TEXT_LEN_IN_ONE_LABEL);
                buf_len = 0;
                need_new_label = FALSE;
                s_last_ai_render_ms = 0;
            }

            if (buf_len < MAX_TEXT_LEN_IN_ONE_LABEL && tmp_len < MAX_TEXT_LEN_IN_ONE_LABEL - buf_len) {
                strncat(buf, tmp_data, strlen(tmp_data));
                buf_len += strlen(tmp_data);

                TAL_PR_DEBUG("1-buf %s, buf len %d, data %s len %d, strlen %d", buf, buf_len, tmp_data, tmp_len, strlen(buf));
                now_ms = tal_system_get_millisecond();
                if ((now_ms - s_last_ai_render_ms) >= WECHAT_AI_TEXT_THROTTLE_MS) {
                    lv_label_set_text(lable, buf);
                    lv_obj_scroll_to_view(parent, LV_ANIM_OFF);
                    s_last_ai_render_ms = now_ms;
                }
                break;
            } else if (buf_len < MAX_TEXT_LEN_IN_ONE_LABEL && len > MAX_TEXT_LEN_IN_ONE_LABEL - buf_len) {
                need_new_label = TRUE;
            } else {
                TAL_PR_DEBUG("buf %s, buf len %d, data %s len %d", buf, buf_len, data, len);
                break;
            }
        }
        break;
    case TY_DISPLAY_TP_AI_CHAT_STOP:
        if (lable != NULL && parent != NULL && buf_len > 0) {
            lv_label_set_text(lable, buf);
            lv_obj_scroll_to_view(parent, LV_ANIM_ON);
            s_last_ai_render_ms = tal_system_get_millisecond();
        }
        break;
    default:
        break;
    }
}

/**
 * @brief Display a static (non-streaming) text message
 * @param[in] data text string to display
 * @param[in] is_ai TRUE for AI side, FALSE for user side
 * @return none
 */
void SetStaticMessage(uint8_t *data, bool is_ai)
{
    lv_obj_t *lable;
    lv_obj_t *parent = create_message(&lable, is_ai);
    lv_label_set_text(lable, data);
    lv_obj_scroll_to_view(parent, LV_ANIM_ON);
}

#if defined(ENABLE_TUYA_PICTURE) && (ENABLE_TUYA_PICTURE == 1)
/* ---------------------------------------------------------------------------
 * Image overlay
 * --------------------------------------------------------------------------- */

/**
 * @brief Click handler for fullscreen image overlay: dismiss and free buffer
 * @param[in] e LVGL event
 * @return none
 */
static void __image_overlay_click_cb(lv_event_t *e)
{
    (void)e;
    if (s_image_overlay) {
        lv_obj_add_flag(s_image_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_image_buf) {
        tal_free(s_image_buf);
        s_image_buf = NULL;
    }
}

/**
 * @brief Display a JPEG image fullscreen in an overlay, click to dismiss
 * @param[in] jpeg_data JPEG image data
 * @param[in] jpeg_len JPEG data length in bytes
 * @return none
 */
static void __wechat_disp_image(const uint8_t *jpeg_data, uint32_t jpeg_len)
{
    if (jpeg_data == NULL || jpeg_len == 0) {
        return;
    }

    TAL_IMAGE_JPEG_INFO_T info = {0};
    if (tal_image_jpeg_get_info(jpeg_data, jpeg_len, &info) != OPRT_OK) {
        TAL_PR_ERR("wechat: jpeg get info failed");
        return;
    }

    uint32_t rgb565_size = info.width * info.height * 2;
    uint8_t *rgb565_buf = tal_malloc(rgb565_size);
    if (rgb565_buf == NULL) {
        TAL_PR_ERR("wechat: malloc rgb565 buf failed, size=%u", rgb565_size);
        return;
    }

    TAL_IMAGE_JPEG_OUTPUT_T out = {0};
    out.out_buf      = rgb565_buf;
    out.out_buf_size = rgb565_size;
    out.out_width    = info.width;
    out.out_height   = info.height;

    if (tal_image_jpeg_decode_rgb565(jpeg_data, jpeg_len, &out) != OPRT_OK) {
        TAL_PR_ERR("wechat: jpeg decode rgb565 failed");
        tal_free(rgb565_buf);
        return;
    }

    if (s_image_overlay == NULL) {
        /* Create overlay on first use */
        s_image_overlay = lv_obj_create(lv_scr_act());
        lv_obj_remove_style_all(s_image_overlay);
        lv_obj_set_size(s_image_overlay, SCREEN_WIDTH, SCREEN_HEIGHT);
        lv_obj_set_style_bg_color(s_image_overlay, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(s_image_overlay, LV_OPA_COVER, 0);
        lv_obj_set_scrollbar_mode(s_image_overlay, LV_SCROLLBAR_MODE_OFF);
        lv_obj_add_flag(s_image_overlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s_image_overlay, __image_overlay_click_cb, LV_EVENT_CLICKED, NULL);

        s_image_canvas = lv_canvas_create(s_image_overlay);
        lv_obj_center(s_image_canvas);
    }

    if (s_image_buf) {
        tal_free(s_image_buf);
    }
    s_image_buf = rgb565_buf;

    lv_canvas_set_buffer(s_image_canvas, rgb565_buf, info.width, info.height, LV_IMG_CF_TRUE_COLOR);
    lv_obj_center(s_image_canvas);
    lv_obj_clear_flag(s_image_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_image_overlay);
}

/**
 * @brief Context for image link click callback
 */
typedef struct {
    char pic_name[WUKONG_PICTURE_NAME_MAX_LEN + 1];
} WECHAT_IMG_LINK_CTX_T;

/**
 * @brief Click handler for "View Image" link: load picture by name and display fullscreen
 * @param[in] e LVGL event
 * @return none
 */
static void __image_link_click_cb(lv_event_t *e)
{
    WECHAT_IMG_LINK_CTX_T *ctx = (WECHAT_IMG_LINK_CTX_T *)lv_event_get_user_data(e);
    if (ctx == NULL) {
        return;
    }

    WUKONG_PICTURE_INFO_T pic = {0};
    if (wukong_picture_get_by_name(ctx->pic_name, &pic) != OPRT_OK) {
        TAL_PR_ERR("wechat: get picture by name failed: %s", ctx->pic_name);
        return;
    }

    if (pic.data && pic.len) {
        __wechat_disp_image(pic.data, pic.len);
    }

    wukong_picture_free_pic_info(&pic);
}

/**
 * @brief Delete handler for link label: free the attached context
 * @param[in] e LVGL event
 * @return none
 */
static void __image_link_delete_cb(lv_event_t *e)
{
    WECHAT_IMG_LINK_CTX_T *ctx = (WECHAT_IMG_LINK_CTX_T *)lv_event_get_user_data(e);
    if (ctx) {
        tal_free(ctx);
    }
}

/**
 * @brief Add a clickable "View Image" link in a chat bubble
 * @param[in] pic_name album picture filename
 * @param[in] is_ai TRUE for AI side bubble
 * @return none
 */
static void __wechat_add_image_link(const char *pic_name, bool is_ai)
{
    if (pic_name == NULL) {
        return;
    }

    WECHAT_IMG_LINK_CTX_T *ctx = tal_malloc(sizeof(WECHAT_IMG_LINK_CTX_T));
    if (ctx == NULL) {
        return;
    }
    strncpy(ctx->pic_name, pic_name, WUKONG_PICTURE_NAME_MAX_LEN);
    ctx->pic_name[WUKONG_PICTURE_NAME_MAX_LEN] = '\0';

    lv_obj_t *label;
    lv_obj_t *parent = create_message(&label, is_ai);
    lv_label_set_text(label, "#0000FF [View Image]#");
    lv_label_set_recolor(label, true);
    lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(label, 8);
    lv_obj_add_event_cb(label, __image_link_click_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_add_event_cb(label, __image_link_delete_cb, LV_EVENT_DELETE, ctx);

    lv_obj_scroll_to_view(parent, LV_ANIM_ON);
    lv_obj_update_layout(msg_container);
}
#endif /* ENABLE_TUYA_PICTURE */

/* ---------------------------------------------------------------------------
 * Styles and UI setup
 * --------------------------------------------------------------------------- */

/**
 * @brief Initialize bubble and avatar styles
 * @return none
 */
static void init_styles(void)
{
    lv_style_init(&style_avatar);
    lv_style_set_radius(&style_avatar, LV_RADIUS_CIRCLE);
    lv_style_set_bg_color(&style_avatar, lv_palette_main(LV_PALETTE_GREY));
    lv_style_set_border_width(&style_avatar, 1);
    lv_style_set_border_color(&style_avatar, lv_palette_darken(LV_PALETTE_GREY, 2));

    lv_style_init(&style_ai_bubble);
    lv_style_set_bg_color(&style_ai_bubble, lv_color_white());
    lv_style_set_radius(&style_ai_bubble, 15);
    lv_style_set_pad_all(&style_ai_bubble, 12);
    lv_style_set_shadow_width(&style_ai_bubble, 12);
    lv_style_set_shadow_color(&style_ai_bubble, lv_color_hex(0xCCCCCC));

    lv_style_init(&style_user_bubble);
    lv_style_set_bg_color(&style_user_bubble, lv_palette_main(LV_PALETTE_GREEN));
    lv_style_set_text_color(&style_user_bubble, lv_color_white());
    lv_style_set_radius(&style_user_bubble, 15);
    lv_style_set_pad_all(&style_user_bubble, 12);
    lv_style_set_shadow_width(&style_user_bubble, 12);
    lv_style_set_shadow_color(&style_user_bubble, lv_palette_darken(LV_PALETTE_GREEN, 2));
}

/**
 * @brief Create the WeChat-style chat UI
 * @return none
 */
static void create_ai_chat_ui(void)
{
    init_styles();

    lv_obj_t* main_cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(main_cont, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(main_cont, lv_color_hex(0xF0F0F0), 0);
    lv_obj_set_style_pad_all(main_cont, 0, 0);
    lv_obj_set_style_text_font(main_cont, AI_MESSAGE_FONT, 0);
    lv_obj_set_style_text_color(main_cont, lv_color_black(), 0);
    lv_obj_set_scrollbar_mode(main_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(main_cont, LV_DIR_NONE);

    status_bar_ = lv_obj_create(main_cont);
    lv_obj_set_size(status_bar_, LV_HOR_RES, 40);
    lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_column(status_bar_, 0, 0);
    lv_obj_set_style_pad_left(status_bar_, 5, 0);
    lv_obj_set_style_pad_right(status_bar_, 5, 0);
    lv_obj_set_flex_align( status_bar_, LV_FLEX_ALIGN_CENTER,  LV_FLEX_ALIGN_CENTER,  LV_FLEX_ALIGN_CENTER);

    mode_label_ = lv_label_create(status_bar_);
    lv_obj_set_style_text_align(mode_label_, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(mode_label_, gui_mode_desc_get(__s_mode));

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(status_label_, 1);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    SetStatus(__s_status);

    network_label_ = lv_label_create(status_bar_);
    lv_obj_set_style_text_font(network_label_, &font_awesome_20_4, 0);
    lv_label_set_text(network_label_, FONT_AWESOME_WIFI_OFF);

    msg_container = lv_obj_create(main_cont);
    lv_obj_set_size(msg_container, SCREEN_WIDTH, SCREEN_HEIGHT - 40);
    lv_obj_set_flex_flow(msg_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_border_width(msg_container, 0, 0);
    lv_obj_set_style_pad_ver(msg_container, 8, 0);
    lv_obj_set_style_pad_hor(msg_container, 10, 0);
    lv_obj_set_y(msg_container, 40);

    lv_obj_move_background(msg_container);

    lv_obj_set_scroll_dir(msg_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(msg_container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(msg_container, LV_OPA_TRANSP, 0);
}

/**
 * @brief Initialize the WeChat chat UI
 * @return none
 */
void app_ui_init(void)
{
    create_ai_chat_ui();
    SetStaticMessage("hi, I'm Tuya AI assistant", true);
}

/**
 * @brief Update the status label text
 * @param[in] stat status code
 * @return none
 */
static void SetStatus(uint8_t stat)
{
    if (status_label_ == NULL) {
        return;
    }

    char *text;

    if (OPRT_OK != gui_status_desc_get(stat, &text, NULL)) {
        return;
    }

    TAL_PR_DEBUG("status is %s", text);
    lv_label_set_text(status_label_, text);
}

/**
 * @brief Text display callback for gui_text_disp module
 * @param[in] obj label widget
 * @param[in] text text string to insert/set
 * @param[in] pos insert position (0 = set text)
 * @param[in] priv_data parent container for scroll
 * @return none
 */
void wechat_text_disp_cb(void *obj, char *text, int pos, void *priv_data)
{
    if (!obj || !priv_data) {
        return;
    }

    if (pos) {
        lv_label_ins_text(obj, pos, text);
    } else {
        lv_label_set_text(obj, text);
    }

    lv_obj_scroll_to_view(priv_data, LV_ANIM_ON);
    lv_obj_update_layout(msg_container);
}

static const char *power_txet  = "Hello, let's play together";
static const char *netok_txet  = "Network connected, let's start chatting";
static const char *netcfg_txet = "Entering network config mode, please use Tuya Smart app to configure";

#define MAX_LEN_DISPLAY_ONE_TIME 4096

/**
 * @brief Handle display messages from the platform (WeChat UI)
 * @param[in] msg display message
 * @return none
 */
void app_ui_msg_handler(TY_DISPLAY_MSG_T *msg)
{
    switch (msg->type) {

    case TY_DISPLAY_TP_LANGUAGE:
        gui_lang_set(msg->data[0]);
        SetStatus(__s_status);
        lv_label_set_text(mode_label_, gui_mode_desc_get(__s_mode));
        break;

    case TY_DISPLAY_TP_HUMAN_CHAT:
        SetStaticMessage(msg->data, FALSE);
        break;

    case TY_DISPLAY_TP_AI_CHAT_START:
    case TY_DISPLAY_TP_AI_CHAT_DATA:
    case TY_DISPLAY_TP_AI_CHAT_STOP:
        SetDynamicMessage(msg->type, msg->data, msg->len);
        break;

    case TY_DISPLAY_TP_AI_IMAGE:
#if defined(ENABLE_TUYA_PICTURE) && (ENABLE_TUYA_PICTURE == 1)
        __wechat_add_image_link((const char *)msg->data, true);
#else
        TAL_PR_DEBUG("wechat: TY_DISPLAY_TP_AI_IMAGE ignored (picture module disabled)");
#endif
        break;

    case TY_DISPLAY_TP_STAT_POWERON:
        SetStaticMessage(power_txet, TRUE);
        break;

    case TY_DISPLAY_TP_STAT_ONLINE:
        SetStatus(GUI_STAT_IDLE);
        SetStaticMessage(netok_txet, TRUE);
        break;

    case TY_DISPLAY_TP_CHAT_STAT:
        __s_status = msg->data[0];
        SetStatus(msg->data[0]);
        break;

    case TY_DISPLAY_TP_STAT_SLEEP:
        SetStatus(GUI_STAT_IDLE);
        break;

    case TY_DISPLAY_TP_STAT_NET:
        lv_label_set_text(network_label_, gui_wifi_level_get(msg->data[0]));
        break;

    case TY_DISPLAY_TP_CHAT_MODE:
        __s_mode = msg->data[0];
        lv_label_set_text(mode_label_, gui_mode_desc_get(__s_mode));
        SetStatus(GUI_STAT_IDLE);
        break;

    case TY_DISPLAY_TP_STAT_NETCFG:
        SetStatus(GUI_STAT_PROV);
        SetStaticMessage(netcfg_txet, TRUE);
        break;

    default:
        break;
    }
}
