#include "desk_event_handle.h"
#include "desk_handle_ui.h"
#include "desk_func_camera.h"
#include "desk_chat.h"
#include "wukong_picture.h"
#include "tuya_iot_com_api.h"
#include "base_event.h"
#include "base_event_info.h"
#include "tuya_error_code.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static desk_ui_lv_t *s_lv_ui_handle = {0};
lv_font_t *AlibabaPuHuiTi3_55_18 = NULL;
static bool is_music_play = false;
static AI_DEVICE_MODE_E s_ui_last_device_mode = AI_DEVICE_MODE_CHAT;

static GIF_EMOJ_T s_gif_emoj_table[] = 
{
    {GIF_DEFAULT,      GIF_DEFAULT_EMOJ,        GIF_DEFAULT_ORI_EMOJ,       "neutral"},
    {GIF_HAPPY,        GIF_HAPPY_EMOJ,          GIF_HAPPY_ORI_EMOJ,         "happy"},
    {GIF_CONFUSED,     GIF_CONFUSED_EMOJ,       GIF_CONFUSED_ORI_EMOJ,      "confused"},
    {GIF_SHY,          GIF_SHY_EMOJ,            GIF_SHY_ORI_EMOJ,           "sleepy"},
    {GIF_CRY,          GIF_CRY_EMOJ,            GIF_CRY_ORI_EMOJ,           "crying"},
    {GIF_ANGRY,        GIF_ANGRY_EMOJ,          GIF_ANGRY_ORI_EMOJ,         "angry"},
    {GIF_SCARED,       GIF_SCARED_EMOJ,         GIF_SCARED_ORI_EMOJ,        "shocked"},
    {GIF_SURPRISED,    GIF_SURPRISED_EMOJ,      GIF_SURPRISED_ORI_EMOJ,     "surprise"},
    {GIF_DISAPPOINTED, GIF_DISAPPOINTED_EMOJ,   GIF_DISAPPOINTED_ORI_EMOJ,  "sad"}, 
    {GIF_ANNOYED,      GIF_ANNOYED_EMOJ,        GIF_ANNOYED_ORI_EMOJ,       "silly"},
    {GIF_THINKING,     GIF_THINKING_EMOJ,       GIF_THINKING_ORI_EMOJ,      "thinking"},
    {GIF_LAUGH,        GIF_LAUGH_EMOJ,          GIF_LAUGH_ORI_EMOJ,         "laughing"},
    {GIF_FUNNY,        GIF_FUNNY_EMOJ,          GIF_FUNNY_ORI_EMOJ,         "funny"},
    {GIF_LOVE,         GIF_LOVE_EMOJ,           GIF_LOVE_ORI_EMOJ,          "loving"},
    {GIF_EMBARRASSED,  GIF_EMBARRASSED_EMOJ,    GIF_EMBARRASSED_ORI_EMOJ,   "embarrassed"},
    {GIF_BLINK,        GIF_BLINK_EMOJ,          GIF_BLINK_ORI_EMOJ,         "winking"},       
    {GIF_COOL,         GIF_COOL_EMOJ,           GIF_COOL_ORI_EMOJ,          "cool"},
    {GIF_RELAXED,      GIF_RELAXED_EMOJ,        GIF_RELAXED_ORI_EMOJ,       "relaxed"},
    {GIF_DELICIOUS,    GIF_DELICIOUS_EMOJ,      GIF_DELICIOUS_ORI_EMOJ,     "delicious"},
    {GIF_UNHAPPY,      GIF_UNHAPPY_EMOJ,        GIF_UNHAPPY_ORI_EMOJ,       "sad"},  
};

desk_ui_lv_t *getContent()
{
    return s_lv_ui_handle;
}

void initContent()
{    
    OPERATE_RET rt;

    TAL_PR_ERR("[%s] enter", __func__);
#if defined(AI_CHAT_FONT_USED_STATIC) && (AI_CHAT_FONT_USED_STATIC == 0)
    AlibabaPuHuiTi3_55_18 = lv_font_load(FONT_PUHUITI3_55_TTF);
    if(AlibabaPuHuiTi3_55_18)
    {
        TAL_PR_INFO("[%s] lv_font_load success", __func__);
    }
#endif

    s_lv_ui_handle = tal_psram_malloc(sizeof(desk_ui_lv_t));
    if(s_lv_ui_handle == NULL)
    {
        TAL_PR_ERR("[%s] malloc fail", __func__);
        return;
    }

    memset(s_lv_ui_handle, 0, sizeof(desk_ui_lv_t));  

    s_lv_ui_handle->st_ai_message.asr_txt = (char *)tal_psram_malloc(AI_ASR_MESSAGE_LEN);
    if(s_lv_ui_handle->st_ai_message.asr_txt == NULL)
    {
        TAL_PR_ERR("[%s] asr txt malloc fail", __func__);
    }

    s_lv_ui_handle->st_ai_message.tts_txt = (char *)tal_psram_malloc(AI_TTS_MESSAGE_LEN);
    if(s_lv_ui_handle->st_ai_message.tts_txt == NULL)
    {
        TAL_PR_ERR("[%s] tts txt malloc fail", __func__);
    }

    s_lv_ui_handle->active_stat = get_gw_active();

    initDeskLanguage();

    desk_handle_ui_init();
    desk_handle_ui_register_all();

    TUYA_CALL_ERR_LOG(desk_record_handle_register());

    TUYA_CALL_ERR_LOG(ty_subscribe_event(TUYA_IPC_CALL, "desktop_call", call_status_event, SUBSCRIBE_TYPE_NORMAL));

    TUYA_CALL_ERR_LOG(ty_subscribe_event(EVENT_MUSIC_PLAYER, "music_player", music_player_event, SUBSCRIBE_TYPE_NORMAL));
    TUYA_CALL_ERR_LOG(ty_subscribe_event(EVENT_MUSIC_BREAK, "music_player", music_player_interrupt, SUBSCRIBE_TYPE_NORMAL));

    TUYA_CALL_ERR_LOG(ty_subscribe_event(EVENT_AI_CLIENT_RUN, "desktop_netcfg", event_netcfg_success, SUBSCRIBE_TYPE_ONETIME));
    TUYA_CALL_ERR_LOG(ty_subscribe_event(EVENT_NETCFG_DATA, "desktop_netcfg", event_netcfg_start, SUBSCRIBE_TYPE_ONETIME));
}

int event_netcfg_start(void *data)
{
    TAL_PR_INFO("[%s] enter", __func__);
    uint32_t cur_screen = desk_handle_ui_get_current_screen_id();

    if (cur_screen == DHUI_SCREEN_ID_LANGUAGE || cur_screen == DHUI_SCREEN_ID_QRCODE) 
    {
        desk_handle_ui_switch_to(DHUI_SCREEN_ID_NETWORK_CFG, LV_SCR_LOAD_ANIM_NONE, DHUI_SWITCH_PERMANENT);
    }

    return OPRT_OK;
}

int event_netcfg_success(void *data)
{
    TAL_PR_INFO("[%s] enter", __func__);
    uint32_t cur_screen = desk_handle_ui_get_current_screen_id();
    if (cur_screen == DHUI_SCREEN_ID_NETWORK_CFG) {
        cfg_network_success();
        return OPRT_OK;
    }
    return OPRT_OK;
}

char *getGifEmojNameByIndex(GIF_EMOJ_E index, bool ori)
{
    for(size_t i = 0; i < sizeof(s_gif_emoj_table)/sizeof(GIF_EMOJ_T); i++)
    {
        if(s_gif_emoj_table[i].index == index)
        {
            if(ori)
            {
                return s_gif_emoj_table[i].ori_gif_name;
            }
            else
            {
                return s_gif_emoj_table[i].gif_name;
            }
        }
    }

    return NULL;
}

int initDeskLanguage()
{
    OPERATE_RET rt = OPRT_OK;
    BYTE_T *value = NULL;
    UINT_T len = 0;

    s_lv_ui_handle->language_stat = DESK_ENGLISH;

    // read volume from kv
    TUYA_CALL_ERR_RETURN(wd_common_read(AI_DESKTOP_LANGUAGE, &value, &len));
    TAL_PR_DEBUG("read desk language config: %s", value);
    ty_cJSON *root = ty_cJSON_Parse((CONST CHAR_T *)value);
    wd_common_free_data(value);
    TUYA_CHECK_NULL_RETURN(root, OPRT_FILE_READ_FAILED);

    // read volum
    ty_cJSON *pdata = ty_cJSON_GetObjectItem(root, "desk_language");
    if (pdata) {
        if (pdata->valueint == DESK_CHINESE || pdata->valueint == DESK_ENGLISH) {
            s_lv_ui_handle->language_stat = pdata->valueint;
        }
    }

    ty_cJSON_Delete(root);
}

int setDeskLanguage(int value)
{
    int rt = OPRT_OK;
    char buf[64] = {0};
    snprintf(buf, sizeof(buf), "{\"desk_language\": %d}", value);
    TUYA_CALL_ERR_RETURN(wd_common_write(AI_DESKTOP_LANGUAGE, (CONST BYTE_T *)buf, strlen(buf)));
    TAL_PR_DEBUG("save desk language config: %s", buf);
    s_lv_ui_handle->language_stat = value;
    return rt;
}

int getDeskLanguage()
{
    return s_lv_ui_handle->language_stat;
}

void setDeskUIIndex(DESKUI_INDEX index)
{
    s_lv_ui_handle->deskui_index = index;
}

INT_T getDeskUIIndex()
{
    return s_lv_ui_handle->deskui_index;
}

static BOOL_T __desk_chat_screen_ready(lv_chat_ui_t **chat_ui)
{
    lv_chat_ui_t *ui = NULL;

    if (s_lv_ui_handle == NULL) {
        return FALSE;
    }

    if (desk_handle_ui_get_current_screen_id() != DHUI_SCREEN_ID_CHAT) {
        return FALSE;
    }

    ui = &s_lv_ui_handle->st_chat;
    if (ui->msg_container == NULL || lv_obj_is_valid(ui->msg_container) == false) {
        return FALSE;
    }

    if (chat_ui != NULL) {
        *chat_ui = ui;
    }

    return TRUE;
}

static void __desk_chat_session_reset_all(void)
{
    if (s_lv_ui_handle == NULL) {
        return;
    }

    s_lv_ui_handle->st_chat_session.active_ai_label = NULL;
    s_lv_ui_handle->st_chat_session.active_ai_parent = NULL;
    s_lv_ui_handle->st_chat_session.ai_message_visible = FALSE;
    s_lv_ui_handle->st_chat_session.ai_stream_active = FALSE;
    s_lv_ui_handle->st_chat_session.pending_ai_stop = FALSE;
}

void desk_chat_session_reset(void)
{
    if (s_lv_ui_handle == NULL) {
        return;
    }

    s_lv_ui_handle->st_chat_session.active_ai_label = NULL;
    s_lv_ui_handle->st_chat_session.active_ai_parent = NULL;
    s_lv_ui_handle->st_chat_session.ai_message_visible = FALSE;
}

static void __desk_chat_reset_text_buffer(char *buf, int *len, size_t buf_size)
{
    if (len != NULL) {
        *len = 0;
    }

    if (buf != NULL && buf_size > 0) {
        memset(buf, 0, buf_size);
    }
}

static int __desk_chat_copy_chunk(char *dst, size_t dst_size, const char *src, int len)
{
    size_t copy_len = 0;

    if (dst == NULL || dst_size == 0 || src == NULL || len <= 0) {
        return 0;
    }

    copy_len = (size_t)len;
    if (copy_len > dst_size - 1) {
        copy_len = dst_size - 1;
    }

    memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';
    return (int)copy_len;
}

static int __desk_chat_append_chunk(char *dst, size_t dst_size, int cur_len, const char *src, int len)
{
    size_t copy_len = 0;

    if (dst == NULL || dst_size == 0 || src == NULL || len <= 0) {
        return cur_len;
    }

    if (cur_len < 0) {
        cur_len = 0;
    }

    if ((size_t)cur_len >= dst_size - 1) {
        dst[dst_size - 1] = '\0';
        return (int)(dst_size - 1);
    }

    copy_len = dst_size - 1 - (size_t)cur_len;
    if (copy_len > (size_t)len) {
        copy_len = (size_t)len;
    }

    memcpy(dst + cur_len, src, copy_len);
    cur_len += (int)copy_len;
    dst[cur_len] = '\0';
    return cur_len;
}

static void __desk_chat_log_chunk(const char *prefix, const char *data, int len)
{
    if (data == NULL || len <= 0) {
        TAL_PR_INFO("%s len:%d", prefix, len);
        return;
    }

    TAL_PR_INFO("%s len:%d text:%.*s", prefix, len, len, data);
}

static int __desk_chat_effective_chunk_len(const char *data, int len)
{
    if (data == NULL || len <= 0) {
        return 0;
    }

    while (len > 0 && data[len - 1] == '\0') {
        len--;
    }

    return len;
}

static BOOL_T __desk_chat_session_valid(void)
{
    desk_chat_session_t *session = NULL;

    if (s_lv_ui_handle == NULL) {
        return FALSE;
    }

    session = &s_lv_ui_handle->st_chat_session;
    if (session->active_ai_label == NULL || session->active_ai_parent == NULL) {
        return FALSE;
    }

    if (lv_obj_is_valid(session->active_ai_label) == false ||
        lv_obj_is_valid(session->active_ai_parent) == false) {
        return FALSE;
    }

    return TRUE;
}

static BOOL_T __desk_chat_ensure_ai_message(lv_chat_ui_t *chat_ui)
{
    desk_chat_session_t *session = NULL;
    lv_obj_t *label = NULL;
    lv_obj_t *parent = NULL;

    if (chat_ui == NULL || chat_ui->msg_container == NULL || lv_obj_is_valid(chat_ui->msg_container) == false) {
        return FALSE;
    }

    if (s_lv_ui_handle == NULL) {
        return FALSE;
    }

    session = &s_lv_ui_handle->st_chat_session;
    if (__desk_chat_session_valid()) {
        session->ai_message_visible = TRUE;
        return TRUE;
    }

    parent = create_chat_message(&label, true);
    if (parent == NULL || label == NULL) {
        desk_chat_session_reset();
        return FALSE;
    }

    session->active_ai_parent = parent;
    session->active_ai_label = label;
    session->ai_message_visible = TRUE;
    return TRUE;
}

static void __desk_chat_render_ai_message(lv_chat_ui_t *chat_ui)
{
    desk_chat_session_t *session = NULL;

    if (chat_ui == NULL || s_lv_ui_handle == NULL) {
        return;
    }

    session = &s_lv_ui_handle->st_chat_session;
    if (__desk_chat_session_valid() == false || session->ai_message_visible == FALSE) {
        return;
    }

    lv_label_set_text(session->active_ai_label, s_lv_ui_handle->st_ai_message.tts_txt);
    lv_obj_scroll_to_view(session->active_ai_parent, LV_ANIM_ON);
    lv_obj_update_layout(chat_ui->msg_container);
}

void desk_chat_session_resume_on_chat_ready(void)
{
    lv_chat_ui_t *chat_ui = NULL;
    desk_chat_session_t *session = NULL;
    lv_chat_ui_t *ui = NULL;

    if (s_lv_ui_handle == NULL) {
        return;
    }

    ui = &s_lv_ui_handle->st_chat;
    if (s_lv_ui_handle->st_ai_message.asr_len > 0 &&
        ui->msg_container != NULL && lv_obj_is_valid(ui->msg_container)) {
        set_chat_message((uint8_t *)s_lv_ui_handle->st_ai_message.asr_txt, false);
        s_lv_ui_handle->st_ai_message.asr_len = 0;
    }

    if (__desk_chat_screen_ready(&chat_ui) == FALSE) {
        return;
    }

    session = &s_lv_ui_handle->st_chat_session;
    if (s_lv_ui_handle->st_ai_message.tts_len > 0 &&
        (session->ai_stream_active == TRUE || session->pending_ai_stop == TRUE)) {
        if (__desk_chat_ensure_ai_message(chat_ui) == TRUE) {
            __desk_chat_render_ai_message(chat_ui);
        }
    }

    if (session->pending_ai_stop == TRUE) {
        session->pending_ai_stop = FALSE;
    }

    disp_picture_message();
}

void switch_ui_scr_animation(lv_obj_t ** new_scr, ui_setup_scr_cb setup_scr, lv_scr_load_anim_t anim_type, SWITCH_SCREEN_TYPE_E del_type)
{
    static bool s_legacy_switch_warned = false;
    lv_obj_t * act_scr = lv_scr_act();

    if (!s_legacy_switch_warned)
    {
        s_legacy_switch_warned = true;
        TAL_PR_INFO("[desk_event_handle] switch_ui_scr_animation is legacy, use desk_handle_ui instead");
    }
    
    switch (del_type)
    {
        case SWITCH_SCREEN_PERMANENT:
        {
            lv_obj_clean(act_scr);

            if(setup_scr)
            {
                setup_scr();
            }

            lv_scr_load_anim(*new_scr, anim_type, SWITCH_UI_DURATION, SWITCH_UI_DELAY, true);
        }
        break;

        case SWITCH_SCREEN_TEMPORARY:
        {
            setup_scr();
            lv_scr_load_anim(*new_scr, anim_type, SWITCH_UI_DURATION, SWITCH_UI_DELAY, false);
        }
        break;

        case SWITCH_SCREEN_DYNAMIC:
        {
            lv_obj_clean(act_scr);

            if(setup_scr)
            {
                setup_scr();
            }

            lv_scr_load_anim(*new_scr, anim_type, SWITCH_UI_DURATION, SWITCH_UI_DELAY, false);
        }
        break;
        
        default:
        break;
    }
}

void handle_home1_event(lv_event_t *e)
{
    static unsigned int first_time = 0;
    static unsigned int second_time = 0;
    static int gif_index = 0;
    lv_event_code_t code = lv_event_get_code(e);
    char gif_path[32] = {0};

    if(code == LV_EVENT_GESTURE)
    {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());

        if(dir == LV_DIR_LEFT)
        {
            TAL_PR_DEBUG("[%s] left", __func__);

            lv_indev_wait_release(lv_indev_get_act());

            gif_index = 0;

            desk_handle_ui_switch_to(DHUI_SCREEN_ID_HOME2, LV_SCR_LOAD_ANIM_MOVE_LEFT, DHUI_SWITCH_PERMANENT);
        }
        else if(dir == LV_DIR_RIGHT)
        {
            TAL_PR_DEBUG("[%s] right", __func__);
            lv_indev_wait_release(lv_indev_get_act());

            gif_index = 0;

            desk_handle_ui_switch_to(DHUI_SCREEN_ID_CHAT, LV_SCR_LOAD_ANIM_MOVE_RIGHT, DHUI_SWITCH_PERMANENT);

        }
        else if(dir == LV_DIR_BOTTOM)
        {
            TAL_PR_DEBUG("[%s] bottom", __func__);

            lv_indev_wait_release(lv_indev_get_act());

            gif_index = 0;

            desk_handle_ui_switch_to(DHUI_SCREEN_ID_HOME3, LV_SCR_LOAD_ANIM_MOVE_BOTTOM, DHUI_SWITCH_PERMANENT);
        }
    }
    else if(code == LV_EVENT_CLICKED)
    {
        if(first_time == 0) //first_time=0, 代表第一次单击
        {
            first_time = tal_system_get_millisecond();
        }
        else
        {
            second_time = tal_system_get_millisecond();
            if(second_time - first_time <= CLICKED_EVENT_TIME)
            {
                gif_index = gif_index + 1 % GIF_MAX;
                strncpy(gif_path, getGifEmojNameByIndex(gif_index, false), sizeof(gif_path));
                home1_gif_switch(gif_path, false);

            }
            first_time = 0;
            second_time = 0;        
        }
    }    
}

void handle_home2_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_GESTURE)
    {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());

        if(dir == LV_DIR_LEFT)
        {
            lv_indev_wait_release(lv_indev_get_act());
            desk_handle_ui_switch_to(DHUI_SCREEN_ID_PERSONAL_CENTER, LV_SCR_LOAD_ANIM_MOVE_LEFT, DHUI_SWITCH_PERMANENT);
        }
        else if(dir == LV_DIR_RIGHT)
        {
            lv_indev_wait_release(lv_indev_get_act());
            desk_handle_ui_switch_to(DHUI_SCREEN_ID_HOME1, LV_SCR_LOAD_ANIM_MOVE_RIGHT, DHUI_SWITCH_PERMANENT);
        }
        else if(dir == LV_DIR_BOTTOM)
        {
            lv_indev_wait_release(lv_indev_get_act());
            desk_handle_ui_switch_to(DHUI_SCREEN_ID_HOME3, LV_SCR_LOAD_ANIM_MOVE_BOTTOM, DHUI_SWITCH_PERMANENT);
        }
    }    
}

void handle_home3_event(lv_event_t *e)
{
    lv_home_ui_t *ui = &getContent()->st_home;
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *target = lv_event_get_target(e);

    if((code == LV_EVENT_GESTURE) && (target == ui->home3_lv.home_scr3))
    {
        TAL_PR_INFO("[%s] gesture", __func__);
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());

        if(dir == LV_DIR_TOP)
        {
            lv_indev_wait_release(lv_indev_get_act());
            desk_handle_ui_back(LV_SCR_LOAD_ANIM_MOVE_TOP, DHUI_SWITCH_PERMANENT);
        }
    }
}

void handle_home3_slider_event(lv_event_t *e)
{
    lv_home_ui_t *ui = &getContent()->st_home;
    lv_event_code_t code = lv_event_get_code(e);

    typedef enum
    {
        IDLE_SLIDER = 0,
        VOLUME_SLIDER,
        BRIGHTNESS_SLIDER,
        CLOCK_VOLUME_SLIDER
    }slider_type_e;

    typedef struct
    {
        slider_type_e slider_type;
        unsigned int volume_v;
        unsigned int brightness_v;
        unsigned int clock_volume_v;
    }device_control_t;

    static device_control_t device_control = {0}; 

    if (code == LV_EVENT_PRESSED) 
    {
        lv_obj_remove_event_cb(ui->home3_lv.home_scr3, handle_home3_event);
    }
    else if (code == LV_EVENT_RELEASED) 
    {
        lv_obj_set_tag(ui->home3_lv.home_scr3, NULL);
        lv_obj_add_event_cb(ui->home3_lv.home_scr3, handle_home3_event, LV_EVENT_GESTURE, NULL);
        
        if (device_control.slider_type == VOLUME_SLIDER) {
            TAL_PR_DEBUG("[%s] set volume: %d", __func__, device_control.volume_v);
            device_control.slider_type = IDLE_SLIDER;
            tuya_ai_toy_volume_set(device_control.volume_v);

        } else if(device_control.slider_type == BRIGHTNESS_SLIDER) {
            TAL_PR_DEBUG("[%s] set brightness: %d", __func__, device_control.brightness_v);
            device_control.slider_type = IDLE_SLIDER;

        } else if(device_control.slider_type == CLOCK_VOLUME_SLIDER) {
            TAL_PR_DEBUG("[%s] set clock volume: %d", __func__, device_control.clock_volume_v);
            device_control.slider_type = IDLE_SLIDER;

        }
    }

    if (code == LV_EVENT_VALUE_CHANGED) 
    {
        lv_obj_t *target = lv_event_get_target(e);
        int32_t value = lv_slider_get_value(target);

        if(target == ui->home3_lv.home_scr3_volume_sli)
        {
            device_control.slider_type = VOLUME_SLIDER;
            device_control.volume_v = value;
        }
        else if(target == ui->home3_lv.home_scr3_brightness_sli)
        {
            device_control.slider_type = BRIGHTNESS_SLIDER;
            device_control.brightness_v = value;
        }
        else if(target == ui->home3_lv.home_scr3_clock_vol_sli)
        {
            device_control.slider_type = CLOCK_VOLUME_SLIDER;
            device_control.clock_volume_v = value;
        }
    }
}

void handle_home3_clicked_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_CLICKED) 
    {
        TAL_PR_DEBUG("[%s] clicked !!!!!!", __func__);

        lv_indev_wait_release(lv_indev_get_act());
        desk_handle_ui_back(LV_SCR_LOAD_ANIM_MOVE_TOP, DHUI_SWITCH_PERMANENT);
    }       
}


void handle_chat_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_GESTURE)
    {
        TAL_PR_INFO("[%s] gesture", __func__);
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());

        if(dir == LV_DIR_LEFT)
        {
            lv_indev_wait_release(lv_indev_get_act());
            desk_handle_ui_back_to(DHUI_SCREEN_ID_HOME1,LV_SCR_LOAD_ANIM_MOVE_LEFT, DHUI_SWITCH_PERMANENT);
        }
        else if(dir == LV_DIR_RIGHT)
        {
            lv_indev_wait_release(lv_indev_get_act());
            desk_handle_ui_back_to(DHUI_SCREEN_ID_PERSONAL_CENTER, LV_SCR_LOAD_ANIM_MOVE_RIGHT, DHUI_SWITCH_PERMANENT);
        }        
    }
}

void handle_personal_center_back_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) 
    {
        TAL_PR_DEBUG("[handle_personal_center_back_event] clicked !!!!!!");

        lv_indev_wait_release(lv_indev_get_act());
        desk_handle_ui_back_to(DHUI_SCREEN_ID_HOME2, LV_SCR_LOAD_ANIM_MOVE_RIGHT, DHUI_SWITCH_PERMANENT);
    }       
}

void receive_ai_message_data(TY_DISPLAY_TYPE_E type, char *data, int len)
{
    lv_chat_ui_t *chat_ui = NULL;
    BOOL_T chat_screen_ready = FALSE;
    int copied_len = 0;
    int text_len = 0;

    if (s_lv_ui_handle == NULL) {
        TAL_PR_ERR("[%s] ui handle is null", __func__);
        return;
    }

    if (s_lv_ui_handle->st_ai_message.asr_txt == NULL || s_lv_ui_handle->st_ai_message.tts_txt == NULL) {
        TAL_PR_ERR("[%s] ai chat buffer is null", __func__);
        desk_chat_session_reset();
        return;
    }

    chat_screen_ready = __desk_chat_screen_ready(&chat_ui);
    text_len = __desk_chat_effective_chunk_len(data, len);
    switch(type) {
        case TY_DISPLAY_TP_HUMAN_CHAT:
            TAL_PR_INFO("[receive_ai_message_data] TY_DISPLAY_TP_HUMAN_CHAT.");
            if (data == NULL || text_len <= 0) {
                TAL_PR_ERR("[receive_ai_message_data] invalid human chat data");
                break;
            }

            desk_chat_picture_spinner_hide();

            __desk_chat_reset_text_buffer(s_lv_ui_handle->st_ai_message.asr_txt,
                                          &s_lv_ui_handle->st_ai_message.asr_len,
                                          AI_ASR_MESSAGE_LEN);
            copied_len = __desk_chat_copy_chunk(s_lv_ui_handle->st_ai_message.asr_txt,
                                                AI_ASR_MESSAGE_LEN, data, text_len);
            s_lv_ui_handle->st_ai_message.asr_len = copied_len;

            if (copied_len > 0) {
                if (chat_screen_ready == TRUE) {
                    set_chat_message((uint8_t *)s_lv_ui_handle->st_ai_message.asr_txt, false);
                    s_lv_ui_handle->st_ai_message.asr_len = 0;
                } else {
                    desk_handle_ui_switch_to(DHUI_SCREEN_ID_CHAT,
                                             LV_SCR_LOAD_ANIM_NONE,
                                             DHUI_SWITCH_PERMANENT);
                }
                if (tuya_ai_toy_device_mode_get() == AI_DEVICE_MODE_PICTURE) {
                    desk_chat_picture_spinner_show();
                }
            }
            break;

        case TY_DISPLAY_TP_AI_CHAT_START:
            TAL_PR_INFO("[receive_ai_message_data] TY_DISPLAY_TP_AI_CHAT_START.");
            __desk_chat_log_chunk("[receive_ai_message_data] tts start", data, text_len);

            desk_chat_picture_spinner_hide();

            __desk_chat_session_reset_all();
            s_lv_ui_handle->st_chat_session.ai_stream_active = TRUE;
            __desk_chat_reset_text_buffer(s_lv_ui_handle->st_ai_message.tts_txt,
                                          &s_lv_ui_handle->st_ai_message.tts_len,
                                          AI_TTS_MESSAGE_LEN);

            if (data == NULL || text_len <= 0) {
                break;
            }

            copied_len = __desk_chat_copy_chunk(s_lv_ui_handle->st_ai_message.tts_txt,
                                                AI_TTS_MESSAGE_LEN, data, text_len);
            s_lv_ui_handle->st_ai_message.tts_len = copied_len;

            if (chat_screen_ready == TRUE && __desk_chat_ensure_ai_message(chat_ui) == TRUE) {
                __desk_chat_render_ai_message(chat_ui);
            }
            break;

        case TY_DISPLAY_TP_AI_CHAT_DATA:
            TAL_PR_INFO("[receive_ai_message_data] TY_DISPLAY_TP_AI_CHAT_DATA.");
            __desk_chat_log_chunk("[receive_ai_message_data] tts data", data, text_len);

            if (data == NULL || text_len <= 0) {
                TAL_PR_ERR("[receive_ai_message_data] invalid ai chat data");
                break;
            }

            if (tuya_ai_toy_device_mode_get() == AI_DEVICE_MODE_TRANSLATE) {
                copied_len = __desk_chat_copy_chunk(s_lv_ui_handle->st_ai_message.tts_txt,
                                                    AI_TTS_MESSAGE_LEN, data, text_len);
            } else {
                copied_len = __desk_chat_append_chunk(s_lv_ui_handle->st_ai_message.tts_txt,
                                                      AI_TTS_MESSAGE_LEN,
                                                      s_lv_ui_handle->st_ai_message.tts_len,
                                                      data, text_len);
                if (copied_len == s_lv_ui_handle->st_ai_message.tts_len) {
                    TAL_PR_ERR("[receive_ai_message_data] tts chat buffer is full, skip append");
                }
            }
            s_lv_ui_handle->st_ai_message.tts_len = copied_len;

            if (chat_screen_ready == FALSE) {
                desk_chat_session_reset();
                break;
            }

            if (__desk_chat_ensure_ai_message(chat_ui) == FALSE) {
                TAL_PR_ERR("[receive_ai_message_data] create ai message failed");
                break;
            }

            __desk_chat_render_ai_message(chat_ui);
            break;

        case TY_DISPLAY_TP_AI_CHAT_STOP:
            TAL_PR_INFO("[receive_ai_message_data] TY_DISPLAY_TP_AI_CHAT_STOP.");
            s_lv_ui_handle->st_chat_session.ai_stream_active = FALSE;
            if (chat_screen_ready == TRUE) {
                if (s_lv_ui_handle->st_ai_message.tts_len > 0 &&
                    __desk_chat_ensure_ai_message(chat_ui) == TRUE) {
                    __desk_chat_render_ai_message(chat_ui);
                }
                disp_picture_message();
                s_lv_ui_handle->st_chat_session.pending_ai_stop = FALSE;
            } else if (s_lv_ui_handle->st_ai_message.tts_len > 0) {
                s_lv_ui_handle->st_chat_session.pending_ai_stop = TRUE;
            }
            desk_chat_session_reset();
            break;
        
        default:
            break;
    }
}

void receive_ai_chat_mode_data(char *data, int len)
{
    if(NULL == data || len <= 0)
    {
        TAL_PR_ERR("[%s] input error", __func__);
        return;
    }

    if (s_ui_last_device_mode != AI_DEVICE_MODE_CHAT) {
        s_ui_last_device_mode = AI_DEVICE_MODE_CHAT;
        desk_chat_set_pending_notify("已成功切换到闲聊模式");
        if (__desk_chat_screen_ready(NULL) == TRUE) {
            desk_chat_flush_pending_notify();
        }
    }

    int trigger_mode = data[0];
    TAL_PR_INFO("[receive_ai_message_data] ai chat %d ", trigger_mode);
    setup_scr_chat_mode(trigger_mode);
    if (__desk_chat_screen_ready(NULL) == TRUE) {
        desk_chat_refresh_mode_label();
    }
    if (desk_handle_ui_get_current_screen_id() == DHUI_SCREEN_ID_PERSONAL_CENTER) {
        desk_personal_refresh_role_name();
    }
}

void receive_emotional_feedback(char *data, int len)
{
    if(NULL == data || len <= 0)
    {
        TAL_PR_ERR("[%s] input error", __func__);
        return;
    }

    DESKUI_INDEX deskui_index = getDeskUIIndex();
    if(deskui_index != DESKUI_INDEX_HOME1)
    {
        TAL_PR_INFO("[%s] current desk ui index: %d", __func__, deskui_index);
        return;
    }

    for(size_t i = 0; i < sizeof(s_gif_emoj_table)/sizeof(GIF_EMOJ_T); i++)
    {
        if(strcmp(s_gif_emoj_table[i].emo_name, data) == 0)
        {
            TAL_PR_INFO("[%s] current ui emo name: %s", __func__, data);
            home1_gif_switch(s_gif_emoj_table[i].gif_name, true);
        }
    }

}

void receive_network_status_data(char *data, int len)
{
    if(NULL == data || len <= 0)
    {
        TAL_PR_ERR("[%s] input error", __func__);
        return;
    }
    s_lv_ui_handle->network_status = data[0];
    TAL_PR_INFO("[%s] network status: %d", __func__, s_lv_ui_handle->network_status);

    uint32_t cur_screen = desk_handle_ui_get_current_screen_id();

    if (cur_screen == DHUI_SCREEN_ID_HOME2) {
        home2_scr_t *h2 = &s_lv_ui_handle->st_home.home2_lv;
        if (h2->home_scr2_wifi_icon != NULL) {
            if (s_lv_ui_handle->network_status == 1) {
                lv_obj_clear_flag(h2->home_scr2_wifi_icon, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(h2->home_scr2_wifi_icon, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    if (cur_screen == DHUI_SCREEN_ID_SETTINGS) {
        desk_settings_refresh_network_status_label();
    }
}

/**
 * @brief Handle AI-generated picture result
 * @param[in] pic_name picture name in album
 * @param[in] len length of pic_name string
 * @return none
 */
void receive_ai_picture_data(char *pic_name, int len)
{
    if (pic_name == NULL || len <= 0) {
        TAL_PR_ERR("[%s] invalid param", __func__);
        return;
    }

    desk_chat_picture_spinner_hide();

    WUKONG_PICTURE_INFO_T pic = {0};
    if (wukong_picture_get_by_name(pic_name, &pic) != OPRT_OK) {
        TAL_PR_ERR("[%s] get picture by name failed: %s", __func__, pic_name);
        return;
    }

    if (pic.data != NULL && pic.len > 0) {
        set_picture_message(pic.data, pic.len);
    }

    wukong_picture_free_pic_info(&pic);

    uint32_t cur_screen = desk_handle_ui_get_current_screen_id();
    if (cur_screen != DHUI_SCREEN_ID_CHAT) {
        desk_handle_ui_switch_to(DHUI_SCREEN_ID_CHAT,
                                 LV_SCR_LOAD_ANIM_NONE,
                                 DHUI_SWITCH_PERMANENT);
        return;
    }

    disp_picture_message();
}

void desk_event_update_device_mode(AI_DEVICE_MODE_E mode)
{
    s_ui_last_device_mode = mode;
}

