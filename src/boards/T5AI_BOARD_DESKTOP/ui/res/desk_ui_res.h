#ifndef __DESK_UI_RES_H__
#define __DESK_UI_RES_H__

#include "tuya_cloud_types.h"
#include "lvgl/lvgl.h"
#include "gui_common.h"
#include "tuya_ai_display.h"
#include "tal_log.h"

/*****************************************************/
/*********************Font****************************/
/***********AlibabaPuHuiTi3 55(Regular)***************/
/*****************************************************/
LV_FONT_DECLARE(AlibabaPuHuiTi3_Regular16);
LV_FONT_DECLARE(AlibabaPuHuiTi3_Regular18_Static);
LV_FONT_DECLARE(AlibabaPuHuiTi3_Regular20);
LV_FONT_DECLARE(AlibabaPuHuiTi3_Regular30);
LV_FONT_DECLARE(AlibabaPuHuiTi3_Regular40);
LV_FONT_DECLARE(AlibabaPuHuiTi3_Regular65);
LV_FONT_DECLARE(AlibabaPuHuiTi3_Regular120);



/*****************************************************/
/*********************Font TTF************************/
/***********AlibabaPuHuiTi3 55(Regular)***************/
/*****************************************************/
#define FONT_PUHUITI3_55_TTF "/t5_fs/font/AlibabaPuHuiTi3_18_Regular.bin"    



/*****************************************************/
/**********************Drawable***********************/
/**********************Gif Emoj***********************/
/*****************************************************/
#define GIF_DEFAULT_EMOJ            "00_Default.gif"
#define GIF_HAPPY_EMOJ              "01_Happy.gif"
#define GIF_CONFUSED_EMOJ           "02_Confused.gif"
#define GIF_SHY_EMOJ                "03_Shy.gif"
#define GIF_CRY_EMOJ                "04_Cry.gif"
#define GIF_ANGRY_EMOJ              "05_Angry.gif"
#define GIF_SCARED_EMOJ             "06_Scared.gif"
#define GIF_SURPRISED_EMOJ          "07_Surprised.gif"
#define GIF_DISAPPOINTED_EMOJ       "08_Disappointed.gif"
#define GIF_ANNOYED_EMOJ            "09_Annoyed.gif"
#define GIF_THINKING_EMOJ           "10_Thinking.gif"
#define GIF_LAUGH_EMOJ              "11_Laugh.gif"
#define GIF_FUNNY_EMOJ              "12_Funny.gif"
#define GIF_LOVE_EMOJ               "13_Love.gif"
#define GIF_EMBARRASSED_EMOJ        "14_Embarrassed.gif"
#define GIF_BLINK_EMOJ              "15_Blink.gif"
#define GIF_COOL_EMOJ               "16_Cool.gif"
#define GIF_RELAXED_EMOJ            "17_Relaxed.gif"
#define GIF_DELICIOUS_EMOJ          "18_Delicious.gif"
#define GIF_UNHAPPY_EMOJ            "19_Unhappy.gif"



/*****************************************************/
/*****************Drawable Original*******************/
/**********************Gif Emoj***********************/
/*****************************************************/
#define GIF_DEFAULT_ORI_EMOJ            "00_Default_original.gif"
#define GIF_HAPPY_ORI_EMOJ              "01_Happy_original.gif"
#define GIF_CONFUSED_ORI_EMOJ           "02_Confused_original.gif"
#define GIF_SHY_ORI_EMOJ                "03_Shy_original.gif"
#define GIF_CRY_ORI_EMOJ                "04_Cry_original.gif"
#define GIF_ANGRY_ORI_EMOJ              "05_Angry_original.gif"
#define GIF_SCARED_ORI_EMOJ             "06_Scared_original.gif"
#define GIF_SURPRISED_ORI_EMOJ          "07_Surprised_original.gif"
#define GIF_DISAPPOINTED_ORI_EMOJ       "08_Disappointed_original.gif"
#define GIF_ANNOYED_ORI_EMOJ            "09_Annoyed_original.gif"
#define GIF_THINKING_ORI_EMOJ           "10_Thinking_original.gif"
#define GIF_LAUGH_ORI_EMOJ              "11_Laugh_original.gif"
#define GIF_FUNNY_ORI_EMOJ              "12_Funny_original.gif"
#define GIF_LOVE_ORI_EMOJ               "13_Love_original.gif"
#define GIF_EMBARRASSED_ORI_EMOJ        "14_Embarrassed_original.gif"
#define GIF_BLINK_ORI_EMOJ              "15_Blink_original.gif"
#define GIF_COOL_ORI_EMOJ               "16_Cool_original.gif"
#define GIF_RELAXED_ORI_EMOJ            "17_Relaxed_original.gif"
#define GIF_DELICIOUS_ORI_EMOJ          "18_Delicious_original.gif"
#define GIF_UNHAPPY_ORI_EMOJ            "19_Unhappy_original.gif"



/*****************************************************/
/*******************Drawable UI Res*******************/
/**********************APP Icon***********************/
/*****************************************************/
#define APP_ICON_ALARM                  "alarm_app.png"
#define APP_ICON_CALENDAR               "calendar_app.png"
#define APP_ICON_CAMERA                 "camera_app.png"
#define APP_ICON_CLOCK                  "clock_app.png"
#define APP_ICON_FILE                   "file_app.png"
#define APP_ICON_MUSIC                  "music_app.png"
#define APP_ICON_PHOTO                  "photo_app.png"
#define APP_ICON_RECORD                 "record_app.png"
#define APP_ICON_SETTINGS               "settings_app.png"
#define APP_ICON_WEATHER                "weather_app.png"
#define APP_ICON_DETECTION              "detection_app.png"
#define APP_ICON_CALL                   "call_app.png"



/*****************************************************/
/*******************Drawable UI Res*******************/
/**********************Other Icon*********************/
/*****************************************************/
#define ICON_AI_CHAT                    "ai_icon.png"
#define ICON_BRIGHTNESS                 "brightness.png"
#define ICON_CLOCK_VOL                  "clock_vol.png"
#define ICON_VOLUMD                     "volume.png"
#define ICON_CALENDAR                   "calendar.png"
#define ICON_CLOCK                      "clock.png"
#define ICON_WEATHER                    "weather.png"
#define ICON_WIFI_24_24                 "wifi_24_24.png"
#define ICON_WIFI_30_30                 "wifi_30_30.png"
#define ICON_UP                         "up.png"
#define ICON_POINT_1                    "point_1.png"
#define ICON_POINT_2                    "point_2.png"
#define ICON_TUYA_APP_QR                "tuya_app_qr.png"
#define ICON_ICON_DOG                   "icon_dog.png"
#define ICON_BACK_LEFT_24_24            "back_left_24_24.png"
#define ICON_BACK_TOP_24_24             "back_top_24_24.png"
#define ICON_ICON_ARROW_YELLOW          "arrow_yellow.png"
#define ICON_ICON_ARROW_BLACK           "arrow_black.png"
#define ICON_ICON_ARROW_WHITE           "arrow_white.png"
#define ICON_ICON_NETWORK_30_30         "network_30_30.png"

#define ICON_MUSIC_BACK_GROUND          "music_background.png"
#define ICON_MUSIC_BACK_TOP             "music_back_top.png"
#define ICON_MUSIC_BACK_BOTTOM          "music_back_bottom.png"
#define ICON_MUSIC_DISK                 "music_disk.png"
#define ICON_MUSIC_STYLI                "music_styli.png"
#define ICON_PLAYMODE_RANDOM            "play_mode_random.png"
#define ICON_PLAYMODE_LOOP              "play_mode_loop.png"
#define ICON_PLAYMODE_SLOOP             "play_mode_sloop.png"
#define ICON_PLAY_PREVIOUS              "play_previous.png"
#define ICON_PLAY_NEXT                  "play_next.png"
#define ICON_PLAY_LIST                  "play_list.png"
#define ICON_PLAY_MUSIC                 "play_music.png"
#define ICON_PAUSE_PLAY                 "pause_music.png"

#define ICON_AI_CAMERA_OFF              "ai_camera_off.png"
#define ICON_AI_CAMERA_ON               "ai_camera_on.png"

#define RECORD_DEFAULT_ICON            "record_default.png"
#define RECORD_RECORDING_ICON          "record_ing.png"
#define RECORD_PAUSE_ICON              "record_pause.png"
#define RECORD_LIST_ICON               "record_list.png"
#define RECORD_PLAY_PLAYING            "record_play_playing.png"
#define RECORD_PLAY_PAUSE              "record_play_pause.png"
#define RECORD_FAST_FORWARD            "fast_forward.png"
#define RECORD_FAST_BACK               "fast_back.png"
#define RECORD_DELETE_24_24            "delete_24_24.png"

#define DEVICE_MODE_SWITCHER_ENTRY     "ModeSwitcherEntry.png"    
#define DEVICE_MODE_PICTURE            "PictureMode.png"       
#define DEVICE_MODE_RECORDING          "RecordingMode.png"       
#define DEVICE_MODE_DETECTION          "DetectionMode.png"       
#define DEVICE_MODE_P2P                "P2PMode.png"       
#define DEVICE_MODE_TRANSLATION        "TranslationMode.png"       
#define DEVICE_MODE_CHAT               "ChatMode.png"       

#define DETECTION_MSG                  "det_msg.png" 
#define DETECTION_MSG_LIST             "det_msg_list.png" 

#define CALL_ANSWER_ICON               "call_answer.png"
#define CALL_HANGUP_ICON               "call_hangup.png"

#endif  // __DESK_UI_RES_H__
