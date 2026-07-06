#include "gui_common.h"
#include "skill_emotion.h"
#include "tal_log.h"
#include "tuya_ai_display.h"

LV_FONT_DECLARE(FONT_SY_20);
LV_FONT_DECLARE(puhui_3bp_18);
LV_FONT_DECLARE(font_awesome_16_4);
typedef struct  {
    CONST lv_font_t *text_font;
    CONST lv_font_t *icon_font;
    CONST lv_font_t *emoji_font;
} DisplayFonts;

STATIC DisplayFonts  fonts_;
STATIC  lv_obj_t    *status_bar_ ;
STATIC  lv_obj_t    *content_;
STATIC  lv_obj_t    *container_;
STATIC  lv_obj_t    *emotion_label_;
STATIC  lv_obj_t    *battery_label_;
STATIC  lv_obj_t    *chat_message_label_;
STATIC  lv_obj_t    *network_label_;
STATIC  lv_obj_t    *mode_label_;
STATIC  lv_obj_t    *status_label_;
STATIC  lv_obj_t    *vol_label_;
STATIC  INT_T       __s_mode = 0;
STATIC  INT_T       __s_status = 0;

typedef struct  {
    CONST lv_font_t *text_font;
    CONST lv_font_t *icon_font;
    CONST lv_font_t *emoji_font;
} DisplayFonts_t;

CONST lv_font_t* font_emoji_64_init(VOID);
VOID SetEmotion(CONST CHAR_T* emotion);
VOID SetStatus(UINT8_T stat);

VOID tuya_xiaozhi_init(VOID)
{
    fonts_.text_font = &puhui_3bp_18; 
    fonts_.icon_font = &font_awesome_16_4; 
    fonts_.emoji_font = font_emoji_64_init(); 

    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_text_font(screen, fonts_.text_font, 0);
    lv_obj_set_style_text_color(screen, lv_color_black(), 0);

    /* Container */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_row(container_, 0, 0);

    /* Status bar */
    status_bar_ = lv_obj_create(container_);
    lv_obj_set_size(status_bar_, LV_HOR_RES, fonts_.text_font->line_height);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    
    /* Content */
    content_ = lv_obj_create(container_);
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_radius(content_, 0, 0);
    lv_obj_set_width(content_, LV_HOR_RES);
    lv_obj_set_flex_grow(content_, 1);


    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN); // 垂直布局（从上到下）
    lv_obj_set_flex_align(content_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_EVENLY); // 子对象居中对齐，等距分布

    emotion_label_ = lv_label_create(content_);
    lv_obj_set_style_text_font(emotion_label_, fonts_.emoji_font, 0);
    lv_label_set_text(emotion_label_, "😶");

    chat_message_label_ = lv_label_create(content_);
    lv_label_set_text(chat_message_label_, "");
    lv_obj_set_width(chat_message_label_, LV_HOR_RES * 0.9); // 限制宽度为屏幕宽度的 90%
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_WRAP); // 设置为自动换行模式
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0); // 设置文本居中对齐

    /* Status bar */
    lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_column(status_bar_, 0, 0);
    lv_obj_set_style_pad_left(status_bar_, 2, 0);
    lv_obj_set_style_pad_right(status_bar_, 2, 0);

    mode_label_ = lv_label_create(status_bar_);
    lv_obj_set_style_text_align(mode_label_, LV_TEXT_ALIGN_LEFT, 0);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(status_label_, 1);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);

    network_label_ = lv_label_create(status_bar_);
    lv_obj_set_style_text_font(network_label_, fonts_.icon_font, 0);
    lv_label_set_text(network_label_, FONT_AWESOME_WIFI_OFF);
    
    battery_label_ = lv_label_create(status_bar_);
    lv_label_set_text(battery_label_, FONT_AWESOME_BATTERY_FULL);
    lv_obj_set_style_text_font(battery_label_, fonts_.icon_font, 0);

    vol_label_ = lv_label_create(status_bar_);
    lv_obj_set_style_text_font(vol_label_, fonts_.icon_font, 0);
    lv_label_set_text(vol_label_, FONT_AWESOME_VOLUME_MEDIUM);

    SetStatus(__s_status);
    SetEmotion("neutral");
    lv_label_set_text(mode_label_, gui_mode_desc_get(__s_mode));
}


VOID SetEmotion(CONST CHAR_T* emotion) 
{
    TAL_PR_NOTICE("SetEmotion unicode: %s", emotion);

    // lv_label_set_text(emotion_label_, "😶");
    CHAR_T* emoji = wukong_emoji_get_by_name(emotion);
    TAL_PR_NOTICE("SetEmotion name: %s", emoji);

    UCHAR_T utf8_buf[5] = {0};
    wukong_emoji_unicode_to_utf8(emoji, utf8_buf, sizeof(utf8_buf));
    //! utf8_buf:是hex，打印一行hex
    TAL_PR_NOTICE("unicode: 0x%02x%02x%02x%02x", utf8_buf[0], utf8_buf[1], utf8_buf[2], utf8_buf[3]);

    lv_label_set_text(emotion_label_, utf8_buf);
}

VOID SetChatMessage(CONST UINT8_T* role, CONST CHAR_T* content) {
    if (chat_message_label_ == NULL) {
        return;
    }
    lv_label_set_text(chat_message_label_, content);
}

VOID SetStatus(UINT8_T stat) 
{
    if (status_label_ == NULL) {
        return;
    }

    CHAR_T *text;

    if (OPRT_OK != gui_status_desc_get(stat, (VOID**)&text, NULL)) {
        return;
    }

    lv_label_set_text(status_label_, text);
}

#define MAX_TEXT_LEN_IN_ONE_LABEL 128
VOID SetDynamicMessage(TY_DISPLAY_TYPE_E type, UINT8_T *data, INT_T len) 
{
    if (chat_message_label_ == NULL) {
        return;
    }

    STATIC CHAR_T buf[MAX_TEXT_LEN_IN_ONE_LABEL + 1] = {0};
    STATIC INT_T buf_len = 0;
    UINT8_T *tmp_data = data;
    INT_T tmp_len = len;
    BOOL_T need_new_label = FALSE;

    switch (type)
    {
    case TY_DISPLAY_TP_HUMAN_CHAT:
    case TY_DISPLAY_TP_AI_CHAT_START:
        need_new_label = TRUE;
        // break;
    case TY_DISPLAY_TP_AI_CHAT_DATA:
        while (tmp_len > 0)
        {   
            if (need_new_label) {
                memset(buf, 0, MAX_TEXT_LEN_IN_ONE_LABEL+1);
                buf_len = 0;
                need_new_label = FALSE;
            }
            
            if (buf_len < MAX_TEXT_LEN_IN_ONE_LABEL) {
                if (tmp_len < MAX_TEXT_LEN_IN_ONE_LABEL - buf_len) {
                    // have enough space for new data
                    strncat(buf, tmp_data, strlen(tmp_data));
                    buf_len += strlen(tmp_data);
                    TAL_PR_DEBUG("1-buf %s, buf len %d", buf, buf_len);                    
                    SetChatMessage(NULL, buf);
                    break;
                } else if (tmp_len > MAX_TEXT_LEN_IN_ONE_LABEL - buf_len) {
                    // no enough space for new data and buf not full
                    strncat(buf, tmp_data, (MAX_TEXT_LEN_IN_ONE_LABEL - buf_len));
                    tmp_len -= (MAX_TEXT_LEN_IN_ONE_LABEL - buf_len);
                    tmp_data += (MAX_TEXT_LEN_IN_ONE_LABEL - buf_len);
                    TAL_PR_DEBUG("2-buf %s, buf len %d", buf, buf_len);                    
                    SetChatMessage(NULL, buf);

                    // create new label for left
                    need_new_label = TRUE;
                } else {
                    
                    TAL_PR_DEBUG("Invalid status, buf %s, buf len %d, tmp_data %s, tmp_len %d", buf, buf_len, tmp_data, tmp_len);
                    break;
                }
            } else {
                // no enough space for new data and buf is full
                TAL_PR_DEBUG("buf not enough, create new");
                need_new_label = TRUE;
            }
        }
        break;    
    default:
        break;
    }    
}

VOID tuya_xiaozhi_app(TY_DISPLAY_MSG_T *msg)
{   
    switch (msg->type) {

    case TY_DISPLAY_TP_LANGUAGE:
        gui_lang_set(msg->data[0]);
        SetStatus(__s_status);
        lv_label_set_text(mode_label_, gui_mode_desc_get(__s_mode));
        gui_txet_disp_init(chat_message_label_, NULL, LV_HOR_RES, (LV_VER_RES / 2));
        break;

    case TY_DISPLAY_TP_STAT_CHARGING:
        lv_label_set_text(battery_label_, FONT_AWESOME_BATTERY_CHARGING); 
        break;

    case TY_DISPLAY_TP_STAT_BATTERY:
        lv_label_set_text(battery_label_, gui_battery_level_get(msg->data[0]));
        break;

    case TY_DISPLAY_TP_STAT_NET:
        lv_label_set_text(network_label_, gui_wifi_level_get(msg->data[0]));
        break;

    case TY_DISPLAY_TP_CHAT_MODE: 
        __s_mode = msg->data[0];
        lv_label_set_text(mode_label_, gui_mode_desc_get(__s_mode)); 
        SetStatus(GUI_STAT_IDLE);
        break;

    case TY_DISPLAY_TP_AI_CHAT: 
    case TY_DISPLAY_TP_AI_CHAT_START: 
    case TY_DISPLAY_TP_AI_CHAT_DATA: 
    case TY_DISPLAY_TP_AI_CHAT_STOP: 
        SetDynamicMessage(msg->type, msg->data, msg->len);
        break;

    case TY_DISPLAY_TP_HUMAN_CHAT:
        SetDynamicMessage(msg->type, msg->data, msg->len);
        // gui_text_disp_free();
        break;

    case TY_DISPLAY_TP_EMOJI:
    case TY_DISPLAY_TP_ASR_EMOJI:
        SetEmotion((CONST CHAR_T*)msg->data);
        break;

    case TY_DISPLAY_TP_CHAT_STAT: {
        if (GUI_STAT_IDLE == msg->data[0]) {
            // gui_text_disp_stop();
            SetEmotion("neutral");
            SetChatMessage(NULL, "");
        } else if (GUI_STAT_LISTEN == msg->data[0]) {
            // gui_text_disp_stop();
            SetEmotion("neutral");
            SetChatMessage(NULL, "");
        } else if (GUI_STAT_SPEAK == msg->data[0]) {
            // gui_text_disp_start();
        }
        __s_status = msg->data[0];
        SetStatus(msg->data[0]);
    } break;

    case TY_DISPLAY_TP_STAT_POWERON:
    case TY_DISPLAY_TP_STAT_ONLINE:
    case TY_DISPLAY_TP_STAT_SLEEP:
        SetStatus(GUI_STAT_IDLE);
        SetEmotion("neutral");
        SetChatMessage(NULL, "");
        break;

    case TY_DISPLAY_TP_STAT_NETCFG:
        SetStatus(GUI_STAT_PROV);
        break;

    case TY_DISPLAY_TP_VOLUME: 
        lv_label_set_text(vol_label_, gui_volum_level_get(msg->data[0]));
        break;
    default:
        break;
    }
}
