#include "desk_event_handle.h"
#include <stdbool.h>

static chat_scr_res_t s_ai_res = {0}; 
static bool is_need_show = false;
static lv_img_dsc_t s_jpeg_img = {0};
static char s_picture_msg_path[128] = {0};
static int16_t s_picture_rotation = -900;
static int s_picture_scale_w = 240;
static int s_picture_scale_h = 240;
static int s_picture_disp_w = 240;
static int s_picture_disp_h = 240;

static CHAR_T s_pending_notify[128] = {0};

static uint8_t s_last_stat = GUI_STAT_IDLE;

static lv_style_t style_ai_message;
static lv_style_t style_user_message;
static lv_style_t style_link;

static lv_timer_t *chat_mode_timer = NULL;
static lv_obj_t *chat_mode_overlay = NULL; //全局覆盖层
static bool s_chat_mode_suppressed = false;
static const char *chat_mode_en[] = {"LONG PRESS MODE", "ONESHOT PRESS MODE", "WAKEUP MODE", "FREE MODE", "TRANSLATE MODE", "P2P MODE"};
static const char *chat_mode_cn[] = {"长按模式", "按键模式", "唤醒模式", "自由模式", "翻译模式", "P2P模式"};

/**
 * @brief Get current mode display text for chat title
 * @return mode text string (static storage, do not free)
 */
static CONST char *__get_chat_mode_text(void)
{
    AI_DEVICE_MODE_E device_mode = tuya_ai_toy_device_mode_get();
    if (device_mode == AI_DEVICE_MODE_CHAT) {
        AI_CHAT_SUB_MODE_E trigger = tuya_ai_toy_trigger_mode_get();
        switch (trigger) {
        case AI_CHAT_SUB_HOLD:     return "闲聊模式: 长按";
        case AI_CHAT_SUB_ONESHOT: return "闲聊模式: 按键";
        case AI_CHAT_SUB_WAKEUP:   return "闲聊模式: 唤醒";
        case AI_CHAT_SUB_FREE:     return "闲聊模式: 自由";
        default:                       return "闲聊模式";
        }
    }

    STATIC CONST char *device_mode_labels[] = {
        "闲聊模式", "翻译模式", "P2P模式", "录音模式", "生图模式", "侦测模式"
    };
    int idx = (int)device_mode;
    if (idx >= 0 && idx < 6) {
        return device_mode_labels[idx];
    }
    return "未知模式";
}

static void chat_styles_init(void) 
{
    // AI气泡样式
    lv_style_init(&style_ai_message);
    lv_style_set_bg_color(&style_ai_message, lv_color_white());
    lv_style_set_bg_opa(&style_ai_message, 0); //透明度
    lv_style_set_text_color(&style_ai_message, lv_color_white());
    lv_style_set_radius(&style_ai_message, 15);
    lv_style_set_pad_all(&style_ai_message, 12);
    lv_style_set_shadow_width(&style_ai_message, 0);
    lv_style_set_border_width(&style_ai_message, 0);

    // 用户气泡样式
    lv_style_init(&style_user_message);
    lv_style_set_bg_color(&style_user_message, lv_color_hex(0xB8BDDE));
    lv_style_set_bg_opa(&style_user_message, 28); //透明度
    lv_style_set_text_color(&style_user_message, lv_color_white());
    lv_style_set_radius(&style_user_message, 15);
    lv_style_set_pad_all(&style_user_message, 12);
    lv_style_set_shadow_width(&style_user_message, 0);
    lv_style_set_border_width(&style_user_message, 0);

    // 超链接样式
    lv_style_init(&style_link);
    lv_style_set_text_color(&style_link, lv_color_hex(0x0066CC));
    lv_style_set_text_decor(&style_link, LV_TEXT_DECOR_UNDERLINE);
}

lv_obj_t* create_chat_message(lv_obj_t **lable, bool is_ai) 
{
    lv_chat_ui_t *ui = &getContent()->st_chat;
    // 主消息容器
    lv_obj_t* msg_cont = lv_obj_create(ui->msg_container);
    lv_obj_remove_style_all(msg_cont);
    lv_obj_set_size(msg_cont, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(msg_cont, 6, 0);
    lv_obj_set_flex_flow(msg_cont, is_ai ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_ROW_REVERSE);
    lv_obj_set_style_pad_column(msg_cont, 10, 0);

    /*---- 消息气泡 ----*/
    lv_obj_t* bubble = lv_obj_create(msg_cont);
    lv_obj_set_width(bubble, is_ai ? (LV_HOR_RES-40) : (LV_HOR_RES-85));
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);
    lv_obj_add_style(bubble, is_ai ? &style_ai_message : &style_user_message, 0);
    
    // 禁用所有滚动条和滑动
    lv_obj_set_scrollbar_mode(bubble, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(bubble, LV_DIR_NONE);
    lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);

    /*---- 消息内容 ----*/
    lv_obj_t* text_cont = lv_obj_create(bubble);
    lv_obj_remove_style_all(text_cont);
    lv_obj_set_size(text_cont, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(text_cont, LV_FLEX_FLOW_COLUMN);
    // 禁用文本容器的滚动
    lv_obj_set_scrollbar_mode(text_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(text_cont, LV_DIR_NONE);

    // 消息文本
    *lable = lv_label_create(text_cont);
    // lv_obj_set_width(*lable, is_ai ? (LV_HOR_RES-40-10) : (LV_HOR_RES-85-10));
    lv_obj_set_width(*lable, is_ai ? (LV_HOR_RES-40-24) : (LV_HOR_RES-85-24));  // 减去padding
    lv_label_set_long_mode(*lable, LV_LABEL_LONG_WRAP);

    return msg_cont;    
}

void set_chat_message(uint8_t *data, bool is_ai) 
{
    lv_chat_ui_t *ui = &getContent()->st_chat;
    lv_obj_t *lable;
    lv_obj_t *parent = create_chat_message(&lable, is_ai);
    lv_label_set_text(lable, data);
    lv_obj_scroll_to_view(parent, LV_ANIM_ON);
    lv_obj_update_layout(ui->msg_container);    
}

void desk_chat_flush_pending_notify(void)
{
    if (s_pending_notify[0] != '\0') {
        set_chat_message((uint8_t *)s_pending_notify, false);
        s_pending_notify[0] = '\0';
    }
}

void desk_chat_set_pending_notify(CONST CHAR_T *msg)
{
    if (msg == NULL) {
        s_pending_notify[0] = '\0';
        return;
    }
    snprintf(s_pending_notify, sizeof(s_pending_notify), "%s", msg);
}

void setup_scr_chat_scr(void)
{
    TAL_PR_INFO("[%s] enter.", __func__);
    desk_chat_session_reset();
    chat_styles_init();
    lv_chat_ui_t *ui = &getContent()->st_chat;

    ui->main_cont = lv_obj_create(NULL);
    lv_obj_set_size(ui->main_cont, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(ui->main_cont, lv_color_hex(0x25262A), 0);
    lv_obj_set_style_pad_all(ui->main_cont, 0, 0);

#if defined(AI_CHAT_FONT_USED_STATIC) && (AI_CHAT_FONT_USED_STATIC == 1)
    lv_obj_set_style_text_font(ui->main_cont, &AlibabaPuHuiTi3_Regular18_Static, 0);
#else        
    lv_obj_set_style_text_font(ui->main_cont, AlibabaPuHuiTi3_55_18, 0);
#endif

    lv_obj_set_style_text_color(ui->main_cont, lv_color_white(), 0);
    lv_obj_set_scrollbar_mode(ui->main_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(ui->main_cont, LV_DIR_NONE);

    lv_obj_t *title = lv_obj_create(ui->main_cont);
    lv_obj_remove_style_all(title);
    lv_obj_set_size(title, LV_HOR_RES, 58);
    lv_obj_set_pos(title, 0, 0);
    lv_obj_set_style_bg_opa(title, LV_OPA_TRANSP, 0);

    if (png_img_load(tuya_app_gui_get_picture_full_path(ICON_AI_CHAT), &s_ai_res.ai_icon) == 0) 
    {
        ui->ai_icon = lv_img_create(title);
        lv_img_set_src(ui->ai_icon, &s_ai_res.ai_icon);
        lv_obj_set_size(ui->ai_icon, 48, 48);
        lv_obj_align(ui->ai_icon, LV_ALIGN_CENTER, 0, 0);
    }

    ui->mode_label = lv_label_create(title);
    lv_label_set_text(ui->mode_label, __get_chat_mode_text());
    lv_obj_set_style_text_font(ui->mode_label, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(ui->mode_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(ui->mode_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_size(ui->mode_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(ui->mode_label, LV_ALIGN_LEFT_MID, 10, 0);

    char *init_stat_text = NULL;
    (void)gui_status_desc_get(s_last_stat, (void **)&init_stat_text, NULL);
    ui->stat_label = lv_label_create(title);
    lv_label_set_text(ui->stat_label, init_stat_text ? init_stat_text : "");
    lv_obj_set_style_text_font(ui->stat_label, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(ui->stat_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(ui->stat_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_size(ui->stat_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(ui->stat_label, LV_ALIGN_RIGHT_MID, -10, 0);

    // 消息容器
    ui->msg_container = lv_obj_create(ui->main_cont);
    lv_obj_set_size(ui->msg_container, LV_HOR_RES, LV_VER_RES - 58); 
    lv_obj_set_flex_flow(ui->msg_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_border_width(ui->msg_container, 0, 0);
    lv_obj_set_style_pad_ver(ui->msg_container, 8, 0);
    lv_obj_set_style_pad_hor(ui->msg_container, 10, 0);
    lv_obj_set_y(ui->msg_container, 58);

    lv_obj_move_background(ui->msg_container);

    // 禁用横向滚动
    lv_obj_set_scroll_dir(ui->msg_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ui->msg_container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(ui->msg_container, LV_OPA_TRANSP, 0);

    // 画布容器
    ui->picture_cont = lv_obj_create(ui->main_cont);
    lv_obj_set_size(ui->picture_cont, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(ui->picture_cont, 0, 0);
    lv_obj_align(ui->picture_cont, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_scrollbar_mode(ui->picture_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(ui->picture_cont, LV_DIR_NONE);
    lv_obj_add_flag(ui->picture_cont, LV_OBJ_FLAG_HIDDEN);

    ui->picture_spinner = lv_spinner_create(ui->main_cont, 1000, 60);
    lv_obj_set_size(ui->picture_spinner, 64, 64);
    lv_obj_align(ui->picture_spinner, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_arc_width(ui->picture_spinner, 4, 0);
    lv_obj_set_style_arc_color(ui->picture_spinner, lv_color_hex(0x3D3D3D), 0);
    lv_obj_set_style_arc_width(ui->picture_spinner, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ui->picture_spinner, lv_color_hex(0x5B8CFF), LV_PART_INDICATOR);
    lv_obj_add_flag(ui->picture_spinner, LV_OBJ_FLAG_HIDDEN);

    lv_obj_update_layout(ui->main_cont);

    lv_obj_set_tag(ui->main_cont, NULL);
    lv_obj_add_event_cb(ui->main_cont, handle_chat_event, LV_EVENT_GESTURE, NULL);

    setDeskUIIndex(DESKUI_INDEX_CHAT);
    desk_chat_session_resume_on_chat_ready();
    disp_picture_message();

    desk_chat_flush_pending_notify();

    TAL_PR_INFO("[%s] quit.", __func__);
}

/**
 * @brief Refresh the mode label text in chat title bar
 * @return none
 */
void desk_chat_refresh_mode_label(void)
{
    lv_chat_ui_t *ui = &getContent()->st_chat;
    if (ui->mode_label == NULL || lv_obj_is_valid(ui->mode_label) == false) {
        return;
    }
    lv_label_set_text(ui->mode_label, __get_chat_mode_text());
}

/**
 * @brief Apply current cached stat to stat_label if it exists
 * @return none
 */
static void __apply_stat_label(void)
{
    lv_chat_ui_t *ui = &getContent()->st_chat;
    if (ui->stat_label == NULL || lv_obj_is_valid(ui->stat_label) == false) {
        return;
    }

    char *text = NULL;
    if (OPRT_OK != gui_status_desc_get(s_last_stat, (void **)&text, NULL)) {
        return;
    }
    if (text == NULL) {
        return;
    }
    lv_label_set_text(ui->stat_label, text);
}

/**
 * @brief Set and refresh the status label (gui_stat_desc) in chat title bar
 * @param[in] stat GUI_STAT_* status value
 * @return none
 */
void desk_chat_refresh_stat_label(uint8_t stat)
{
    s_last_stat = stat;
    __apply_stat_label();
}

/**
 * @brief Re-render the status label using the cached stat (used on language switch)
 * @return none
 */
void desk_chat_refresh_stat_label_current(void)
{
    __apply_stat_label();
}

void chat_scr_res_clear(void)
{
    TAL_PR_INFO("[%s] enter.", __func__);
    desk_chat_session_reset();

    //清除图片资源
    png_img_unload(&s_ai_res.ai_icon);
    jpg_img_unload(&s_jpeg_img);

    //样式释放
    lv_style_reset(&style_ai_message);
    lv_style_reset(&style_user_message);
    lv_style_reset(&style_link);

    //清除动态对象，set_chat_message创建的对象
    lv_chat_ui_t *ui = &getContent()->st_chat;
    if(ui->msg_container != NULL)
    {
        lv_obj_t *msg = lv_obj_get_child(ui->msg_container, NULL);
        while (msg != NULL) {
            lv_obj_t *next_msg = lv_obj_get_child(ui->msg_container, msg);
            lv_obj_del(msg); // 递归删除单个消息（包含气泡、文本等子对象）
            msg = next_msg;
            TAL_PR_INFO("[%s] set_chat_message obj clear.", __func__);
        }
        // lv_obj_del(ui->msg_container); // 删除消息容器本身
        // ui->msg_container = NULL;
        TAL_PR_INFO("[%s] set_chat_message obj clear finsh.", __func__);
    }

    if(ui->picture_canvas)
    {
        lv_obj_del(ui->picture_canvas);
        ui->picture_canvas = NULL;
    }

    ui->mode_label = NULL;
    ui->stat_label = NULL;
    ui->picture_spinner = NULL;

    memset(&s_ai_res, 0, sizeof(chat_scr_res_t));
}

void timer_ai_chat_mode_show(lv_timer_t * timer)
{
    TAL_PR_INFO("[%s] enter", __func__);

    if (chat_mode_overlay) 
    {
        lv_obj_del(chat_mode_overlay);
        chat_mode_overlay = NULL;
    }

    lv_obj_t *ui = lv_scr_act();
    if(ui)
    {
        TAL_PR_INFO("[%s] update scr act", __func__);
        lv_obj_update_layout(ui);
    }

    if(timer)
    {
        if (chat_mode_timer == timer) 
        {
            TAL_PR_INFO("[%s][%d] del chat mode timer", __func__, __LINE__);
            lv_timer_del(timer);
            chat_mode_timer = NULL;
        } 
        else 
        {
            TAL_PR_INFO("[%s][%d] del chat mode timer", __func__, __LINE__);
            lv_timer_del(timer);
        }
    }
} 

// 空回调，仅用来“消费”事件，阻止向下传递
static void overlay_event_cb(lv_event_t *e)
{
    (void)e;
}

/**
 * @brief Suppress the next chat mode overlay display
 * @return none
 */
void setup_scr_chat_mode_suppress(void)
{
    s_chat_mode_suppressed = true;
}

void setup_scr_chat_mode(int mode)
{
    if (s_chat_mode_suppressed) {
        s_chat_mode_suppressed = false;
        TAL_PR_INFO("[%s] suppressed, skip mode overlay", __func__);
        return;
    }

    if (chat_mode_overlay) 
    {
        lv_obj_del(chat_mode_overlay);
        chat_mode_overlay = NULL;
    }

    if(chat_mode_timer)
    {
        lv_timer_del(chat_mode_timer);
        chat_mode_timer = NULL;
    }

    chat_mode_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(chat_mode_overlay);
    lv_obj_set_size(chat_mode_overlay, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_opa(chat_mode_overlay, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(chat_mode_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(chat_mode_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(chat_mode_overlay, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_tag(chat_mode_overlay, NULL);
    lv_obj_add_event_cb(chat_mode_overlay, overlay_event_cb, LV_EVENT_ALL, NULL);   //空事件绑定，防止透传事件

    
    lv_obj_t *chat_mode = lv_obj_create(chat_mode_overlay);
    lv_obj_remove_style_all(chat_mode);
    lv_obj_set_size(chat_mode, 320, 240);
    lv_obj_set_pos(chat_mode, 0, 0);
    lv_obj_set_style_bg_color(chat_mode, lv_color_hex(0x25262A), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(chat_mode, LV_OPA_80, 0);
    lv_obj_set_style_text_font(chat_mode, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_scrollbar_mode(chat_mode, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(chat_mode, LV_DIR_NONE);
    lv_obj_set_flex_flow(chat_mode, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(chat_mode, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); 
    

    lv_obj_t *mode_label = lv_label_create(chat_mode);
    lv_obj_set_size(mode_label, LV_SIZE_CONTENT, 32);
    lv_label_set_long_mode(mode_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_border_width(mode_label, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(mode_label, 16, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(mode_label, lv_color_hex(0xFFF37B), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(mode_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(mode_label, LV_OPA_30, 0);
    lv_obj_set_style_bg_color(mode_label, lv_color_hex(0xB8BDDE), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_hor(mode_label, 10, 0);
    lv_obj_set_style_pad_ver(mode_label, 7, 0);

    DESKTOP_LANGUAGE language_choice = getDeskLanguage();
    if(language_choice == DESK_CHINESE)
    {
        lv_label_set_text(mode_label, chat_mode_cn[mode]);
    }
    else if(language_choice == DESK_ENGLISH)
    {
        lv_label_set_text(mode_label, chat_mode_en[mode]);
    }

    chat_mode_timer = lv_timer_create(timer_ai_chat_mode_show, 1200, NULL);

    lv_obj_update_layout(chat_mode_overlay);
}

int set_picture_message(BYTE_T *data, UINT_T len)
{
    TUYA_CHECK_NULL_RETURN(data, OPRT_INVALID_PARM);

    TUYA_FILE f = tkl_fopen(AI_CHAT_JPEG_MSG_PATH, "w+");
    if (f == NULL) {
        bk_printf("open failed\r\n");
        return OPRT_COM_ERROR;
    }

    tkl_fwrite(data, len, f);

    tkl_fclose(f);

    strncpy(s_picture_msg_path, AI_CHAT_JPEG_MSG_PATH, sizeof(s_picture_msg_path) - 1);
    s_picture_msg_path[sizeof(s_picture_msg_path) - 1] = '\0';
    s_picture_rotation = 0;
    s_picture_scale_w = 0;
    s_picture_scale_h = 0;
    s_picture_disp_w = LV_HOR_RES;
    s_picture_disp_h = LV_VER_RES;
    is_need_show = true;
    return OPRT_OK;
}

/**
 * @brief Set picture message display path (file already saved by caller)
 * @param[in] path JPEG file path to display
 * @return none
 */
void set_picture_message_file(CONST CHAR_T *path)
{
    if (path == NULL) {
        return;
    }

    strncpy(s_picture_msg_path, path, sizeof(s_picture_msg_path) - 1);
    s_picture_msg_path[sizeof(s_picture_msg_path) - 1] = '\0';
    s_picture_rotation = -900;
    s_picture_scale_w = 240;
    s_picture_scale_h = 240;
    s_picture_disp_w = 240;
    s_picture_disp_h = 240;
    is_need_show = true;
}


static void view_photo_event_cb(lv_event_t *e) 
{
    lv_chat_ui_t *ui = &getContent()->st_chat;
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) 
    {
        TAL_PR_INFO("[%s] clicked!!!!!!!!!!", __func__);
        lv_obj_add_flag(ui->msg_container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui->picture_cont, LV_OBJ_FLAG_HIDDEN);
    }    
}

static void return_chat_content_event_cb(lv_event_t *e) 
{
    lv_chat_ui_t *ui = &getContent()->st_chat;
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) 
    {
        TAL_PR_INFO("[%s] clicked!!!!!!!!!!", __func__);
        lv_obj_add_flag(ui->picture_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui->msg_container, LV_OBJ_FLAG_HIDDEN);
    } 
}

void disp_picture_message() 
{
    if(!is_need_show)
    {
        TAL_PR_INFO("[%s] dont disp picture message ", __func__);
        return;
    }

    CONST CHAR_T *path = (s_picture_msg_path[0] != '\0') ? s_picture_msg_path : AI_CHAT_JPEG_MSG_PATH;

    lv_chat_ui_t *ui = &getContent()->st_chat;

    if(ui->picture_canvas)
    {
        lv_obj_del(ui->picture_canvas);
        ui->picture_canvas = NULL;
    }


    jpg_img_unload(&s_jpeg_img);
    if (jpg_img_load_with_scale((CHAR_T *)path, &s_jpeg_img, s_picture_scale_w, s_picture_scale_h) == 0) 
    {
        ui->picture_canvas = lv_img_create(ui->picture_cont);
        lv_img_set_src(ui->picture_canvas, &s_jpeg_img);
        lv_img_set_angle(ui->picture_canvas, s_picture_rotation); 
        lv_obj_align(ui->picture_canvas, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_size(ui->picture_canvas, s_picture_disp_w, s_picture_disp_h);
        lv_obj_add_flag(ui->picture_canvas, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_tag(ui->picture_canvas, NULL);
        lv_obj_add_event_cb(ui->picture_canvas, return_chat_content_event_cb, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_add_flag(ui->picture_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui->msg_container, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *label;
    lv_obj_t *parent = create_chat_message(&label, false);
    lv_label_set_text(label, "查看图片");
    lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_style(label, &style_link, LV_STATE_DEFAULT);
    lv_obj_set_tag(label, NULL);
    lv_obj_add_event_cb(label, view_photo_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_scroll_to_view(parent, LV_ANIM_ON);

    lv_obj_update_layout(ui->msg_container);   
    
    s_picture_msg_path[0] = '\0';
    s_picture_rotation = -900;
    s_picture_scale_w = 240;
    s_picture_scale_h = 240;
    s_picture_disp_w = 240;
    s_picture_disp_h = 240;
    is_need_show = false;
}

/**
 * @brief Show the picture loading spinner on chat screen
 * @return none
 */
void desk_chat_picture_spinner_show(void)
{
    lv_chat_ui_t *ui = &getContent()->st_chat;
    if (ui->picture_spinner == NULL || lv_obj_is_valid(ui->picture_spinner) == false) {
        return;
    }
    lv_obj_clear_flag(ui->picture_spinner, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Hide the picture loading spinner on chat screen
 * @return none
 */
void desk_chat_picture_spinner_hide(void)
{
    lv_chat_ui_t *ui = &getContent()->st_chat;
    if (ui->picture_spinner == NULL || lv_obj_is_valid(ui->picture_spinner) == false) {
        return;
    }
    lv_obj_add_flag(ui->picture_spinner, LV_OBJ_FLAG_HIDDEN);
}