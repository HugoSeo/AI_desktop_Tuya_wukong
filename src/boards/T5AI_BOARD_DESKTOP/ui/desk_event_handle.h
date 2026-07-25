#ifndef __DESK_EVENT_HANDLE_H__
#define __DESK_EVENT_HANDLE_H__

#include "desk_home.h"
#include "desk_startup.h"
#include "desk_chat.h"
#include "desk_personal.h"
#include "desk_ui_res.h"
#include "desk_func_settings.h"
#include "desk_func_music.h"
#include "desk_func_camera.h"
#include "desk_func_photo.h"
#include "desk_func_record.h"
#include "desk_func_detection.h"
#include "desk_func_call.h"

#include "tuya_device_board.h"
#include "tuya_devos_utils.h"
#include "tuya_ai_client.h"
#include "tal_system.h"
#include <stdio.h>
#include "tuya_ai_display.h"
#include "uni_random.h"
#include "wukong_audio_player.h"
#include "wukong_ai_skills.h"
#include "wukong_ai_mode.h"
#include "tkl_jpeg_codec.h"
#include "tkl_fs.h"
#include "lv_vendor.h"
#include "tal_mutex.h"
#include "tuya_list.h"
#include "tuya_ipc_media_stream_event.h"
#include "tuya_sdk_call.h"
#include "tuya_app_config.h"

#if defined(TUYA_AI_TOY_BATTERY_ENABLE) && (TUYA_AI_TOY_BATTERY_ENABLE == 1)
#include "tuya_ai_battery.h"
#endif

#if defined(ENABLE_WIFI_SERVICE) && (ENABLE_WIFI_SERVICE == 1)
#include "tuya_cloud_wifi_defs.h"
#endif

#if defined(ENABLE_TUYA_CAMERA) && (ENABLE_TUYA_CAMERA == 1)
#include "tuya_device_camera.h"
#include "lv_vendor_event.h"
#include "tuya_port_dma2d.h"
#endif

#if defined(ENABLE_TUYA_CODEC_OPUS) && (ENABLE_TUYA_CODEC_OPUS == 1)
#include "tuya_ai_encoder_opus.h"
#include "tuya_ai_encoder.h"
#endif

extern lv_font_t *AlibabaPuHuiTi3_55_18;

#define DESK_LCD_WIDTH        (320)
#define DESK_LCD_HEIGHT       (240)

#define AI_DESKTOP_GUI_DEBUG 0
#define AI_CHAT_FONT_USED_STATIC 1  //1:使用静态字体 0:使用动态加载字体（加载sdka中的bin字库，但会拖慢开机速度）

#define AI_DESKTOP_LANGUAGE "ai_desk_language"

#define CLICKED_EVENT_TIME (200)
#define CLICKED_RESET_TIME (300)
#define CLICKED_RESET_NUM (5)

#define AI_ASR_MESSAGE_LEN (1024)
#define AI_TTS_MESSAGE_LEN (2048)

#define SWITCH_UI_DURATION 50	//谨慎设置该时间，时间越长，cpu占用率越高（0~500）,会导致切换UI时卡死
#define SWITCH_UI_DELAY    0	//切换延时，单位ms
typedef void (*ui_setup_scr_cb)(void);

typedef enum
{
    DESKUI_INDEX_IDLE = 0,
    DESKUI_INDEX_STARTUP,
    DESKUI_INDEX_HOME1,
    DESKUI_INDEX_HOME2,
    DESKUI_INDEX_HOME3,
    DESKUI_INDEX_CHAT,
    DESKUI_INDEX_PERSONAL_CENTER,
    DESKUI_INDEX_SETTINGS,
    DESKUI_INDEX_SETTINGS_NETWORK,
    DESKUI_INDEX_SETTINGS_LANGUAGE,
    DESKUI_INDEX_SETTINGS_DEVICE_MODE,
    DESKUI_INDEX_MUSIC,
    DESKUI_INDEX_MUSIC_PLAYLIST,
    DESKUI_INDEX_RECORD_LIST,
    DESKUI_INDEX_MAX
}DESKUI_INDEX;

typedef BYTE_T DESKTOP_LANGUAGE;
#define DESK_CHINESE 0
#define DESK_ENGLISH 1 

typedef struct
{
    char *asr_txt;
    int   asr_len;
    char *tts_txt;
    int   tts_len;
}ai_message_t;

typedef struct
{
    lv_obj_t *active_ai_label;
    lv_obj_t *active_ai_parent;
    BOOL_T    ai_message_visible;
    BOOL_T    ai_stream_active;
    BOOL_T    pending_ai_stop;
}desk_chat_session_t;

typedef struct
{
    DESKUI_INDEX     deskui_index;
    GW_WORK_STAT_T   active_stat;
    DESKTOP_LANGUAGE language_stat;
    UINT8_T          network_status;
    ai_message_t     st_ai_message;
    desk_chat_session_t st_chat_session;
    lv_startup_ui_t  st_startup;
    lv_home_ui_t     st_home;
    lv_chat_ui_t     st_chat;
    lv_personal_ui_t st_personal;
    lv_device_mode_ui_t st_device_mode;
    lv_settings_ui_t st_func_settings;
    lv_music_ui_t    st_func_music;
    lv_camera_ui_t   st_func_camera;
    lv_photo_ui_t    st_func_photo;
    lv_record_ui_t   st_func_record;
    lv_detection_ui_t st_func_detection;
    lv_call_ui_t     st_func_call;
}desk_ui_lv_t;

typedef enum
{
	SWITCH_SCREEN_PERMANENT = 0,	//永久切换屏幕（旧屏幕不再复用）
	SWITCH_SCREEN_TEMPORARY = 1,	//临时切换屏幕（旧屏幕需复用，不释放旧屏幕资源）
	SWITCH_SCREEN_DYNAMIC = 2,		//旧屏幕复用但需刷新内容（旧屏幕需复用，释放旧屏幕资源，防止内存占用过高）
}SWITCH_SCREEN_TYPE_E;

/**
 * 兼容旧切屏接口。
 * 新代码禁止继续使用该接口，统一改为调用 `desk_handle_ui_switch_to()` /
 * `desk_handle_ui_back()` / `desk_handle_ui_back_to()`。
 */
void switch_ui_scr_animation(lv_obj_t ** new_scr, ui_setup_scr_cb setup_scr, lv_scr_load_anim_t anim_type, SWITCH_SCREEN_TYPE_E del_type);

typedef enum
{
    GIF_DEFAULT = 0,
    GIF_HAPPY,
    GIF_CONFUSED,
    GIF_SHY,
    GIF_CRY,
    GIF_ANGRY,
    GIF_SCARED,
    GIF_SURPRISED,
    GIF_DISAPPOINTED,
    GIF_ANNOYED,
    GIF_THINKING,
    GIF_LAUGH,
    GIF_FUNNY,
    GIF_LOVE,
    GIF_EMBARRASSED,
    GIF_BLINK,       
    GIF_COOL,
    GIF_RELAXED,
    GIF_DELICIOUS,
    GIF_UNHAPPY,  
    GIF_MAX 
}GIF_EMOJ_E;

typedef struct
{
    GIF_EMOJ_E index;
    char *gif_name;
    char *ori_gif_name;
    char *emo_name;
}GIF_EMOJ_T; 

desk_ui_lv_t *getContent();

void initContent();

int initDeskLanguage();

int setDeskLanguage(int value);

int getDeskLanguage();

void setDeskUIIndex(DESKUI_INDEX index);

INT_T getDeskUIIndex();

int event_netcfg_start(void *data);

int event_netcfg_success(void *data);

void handle_home1_event(lv_event_t *e);

void handle_home2_event(lv_event_t *e);

void handle_home3_event(lv_event_t *e);

void handle_home3_slider_event(lv_event_t *e);

void handle_chat_event(lv_event_t *e);

void handle_personal_center_back_event(lv_event_t *e);

void desk_chat_session_reset(void);

void desk_chat_session_resume_on_chat_ready(void);

void receive_ai_message_data(TY_DISPLAY_TYPE_E type, char *data, int len);

void receive_ai_chat_mode_data(char *data, int len);

void receive_emotional_feedback(char *data, int len);

void receive_network_status_data(char *data, int len);

void receive_ai_picture_data(char *pic_name, int len);

extern OPERATE_RET tuya_ai_toy_volume_set(UINT8_T value);

extern UINT8_T tuya_ai_toy_volume_get();

extern AI_CHAT_SUB_MODE_E tuya_ai_toy_trigger_mode_get(VOID);

extern AI_DEVICE_MODE_E tuya_ai_toy_device_mode_get(VOID);

extern VOID tuya_ai_toy_device_mode_set(AI_DEVICE_MODE_E mode);

extern OPERATE_RET wukong_ai_agent_translate_list_language(ty_cJSON **result);

extern OPERATE_RET wukong_ai_agent_translate_update_language(CHAR_T *lang, CHAR_T *tts_lang);

void desk_event_update_device_mode(AI_DEVICE_MODE_E mode);

#endif  // __DESK_EVENT_HANDLE_H__