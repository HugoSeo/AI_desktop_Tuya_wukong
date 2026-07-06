#include "tuya_ai_display.h"
#include "gui_common.h"
#include "lvgl.h"

LV_IMG_DECLARE(Nature128);
LV_IMG_DECLARE(Fearful128);
LV_IMG_DECLARE(Delicious128);
LV_IMG_DECLARE(Think128);
LV_IMG_DECLARE(Loving128);
LV_IMG_DECLARE(Confused128);
LV_IMG_DECLARE(Disappointed128);
LV_IMG_DECLARE(Angry);

LV_IMG_DECLARE(Sad);
LV_IMG_DECLARE(Cool);
LV_IMG_DECLARE(Crying);
LV_IMG_DECLARE(Embarrassed);
LV_IMG_DECLARE(Laughing);
LV_IMG_DECLARE(Relaxing);
LV_IMG_DECLARE(Shocked);
LV_IMG_DECLARE(Silly);
LV_IMG_DECLARE(Sleepy);
LV_IMG_DECLARE(Winking);
LV_IMG_DECLARE(Suprising);

static lv_obj_t *dual_eyes;
static int current_gif_index = -1;

static const gui_emotion_t gif_emotion[] = {
    {&Nature128,        "neutral"       },
    {&Loving128,        "loving"        },
    {&Think128,         "thinking"      },
    // {&Confused128,      "confused"    },
    // {&Delicious128,     "delicious"   },
    // {&Fearful128,       "fearful"     },
    // {&Disappointed128,  "disappointed"},
    /*----------------------------------- */
    //! dual-eyes
    {&Sad,               "sad"         },
    {&Crying,            "crying"      },
    {&Laughing,          "laughing"    },
    {&Relaxing,          "relaxed"     },
    {&Winking,           "winking"     },
    {&Suprising,         "suprising"   },
    // {&Cool,              "cool"        },
    // {&Embarrassed,       "embarrassed" },
    // {&Shocked,           "shocked"     },
    // {&Silly,             "silly"       },
    // {&Sleepy,            "sleepy"      },
};

#include "tal_log.h"

void eyes_emotion_flush(char  *emotion)
{
    uint8_t index = 0;

    TAL_PR_DEBUG("eyes_emotion_flush: %s\r\n", emotion);

    index = gui_emotion_find(gif_emotion, CNTSOF(gif_emotion), emotion);

    if (current_gif_index == index) {
        return;
    }

    current_gif_index = index;

    lv_gif_set_src(dual_eyes, gif_emotion[index].source); 
}

//! letf red, right blue
void eyes_test_separate_display(void)
{
    lv_obj_t *left_eye;
    lv_obj_t *right_eye;

    lv_obj_t * obj = NULL;
    obj = lv_obj_create(lv_scr_act());
    lv_obj_set_size(obj, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);

    left_eye = lv_obj_create(obj);
    lv_obj_set_size(left_eye, LV_HOR_RES, LV_VER_RES / 2);
    lv_obj_set_pos(left_eye, 0, 0);
    lv_obj_set_style_pad_all(left_eye, 0, 0);
    lv_obj_set_style_border_width(left_eye, 0, 0);
    lv_obj_set_style_bg_color(left_eye, lv_color_hex(0xFF0000), 0);

    right_eye = lv_obj_create(obj);
    lv_obj_set_size(right_eye, LV_HOR_RES, LV_VER_RES / 2);
    lv_obj_set_pos(right_eye, 0, 128);
    lv_obj_set_style_pad_all(right_eye, 0, 0);
    lv_obj_set_style_border_width(right_eye, 0, 0);
    lv_obj_set_style_bg_color(right_eye, lv_color_hex(0x0000FF), 0);
}

void app_ui_init(void)
{
    // eyes_test_separate_display();
    lv_obj_t * obj = NULL;
    obj = lv_obj_create(lv_scr_act());
    lv_obj_set_size(obj, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);

    dual_eyes = lv_gif_create(obj);
    lv_obj_set_size(dual_eyes, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(dual_eyes, 0, 0);

    eyes_emotion_flush("neutral");
}


void app_ui_msg_handler(TY_DISPLAY_MSG_T *msg)
{
    uint8_t emoji;

    switch (msg->type)
    {
        
    case TY_DISPLAY_TP_EMOJI:
        eyes_emotion_flush(msg->data);
        break;    

    case TY_DISPLAY_TP_CHAT_STAT: {
        if (0 == msg->data[0] || 1 == msg->data[0]) {
            eyes_emotion_flush("neutral");
        }
    } break;

    default:
        break;
    }
}
