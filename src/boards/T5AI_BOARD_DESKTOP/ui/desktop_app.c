#include "lvgl/lvgl.h"
#include "gui_common.h"
#include "tuya_ai_display.h"
#include "tal_log.h"
#include "desk_event_handle.h"
static bool is_first_start = true;

extern void desktop_ui_startup(void);

#if 1

VOID app_ui_init(VOID)
{
#if defined(TUYA_FILE_SYSTEM) && (TUYA_FILE_SYSTEM == 1)
    TAL_PR_INFO("[%s] enter", __FUNCTION__);
	desktop_ui_startup();
#endif
}

void app_ui_msg_handler(TY_DISPLAY_MSG_T *msg)
{
#if defined(TUYA_FILE_SYSTEM) && (TUYA_FILE_SYSTEM == 1)

    TAL_PR_DEBUG("[%s] type: %d", __func__, msg->type);

    switch(msg->type)
    {
        case TY_DISPLAY_TP_HUMAN_CHAT:
        case TY_DISPLAY_TP_AI_CHAT_START: 
        case TY_DISPLAY_TP_AI_CHAT_DATA: 
        case TY_DISPLAY_TP_AI_CHAT_STOP:
        {
            receive_ai_message_data(msg->type, msg->data, msg->len);
        }
        break;

        case TY_DISPLAY_TP_EMOJI:
        {
            TAL_PR_INFO("[%s] emotion: %s", __func__, msg->data);
            receive_emotional_feedback(msg->data, msg->len);
        }
        break;

        case TY_DISPLAY_TP_CHAT_MODE: 
        {
            if(is_first_start)
            {
                is_first_start = false;
                break;
            }
            receive_ai_chat_mode_data(msg->data, msg->len);
        }
        break;

        case TY_DISPLAY_TP_STAT_NET:
        {
            receive_network_status_data(msg->data, msg->len);
        }
        break;

        case TY_DISPLAY_TP_AI_IMAGE:
        {
            receive_ai_picture_data((char *)msg->data, msg->len);
        }
        break;

        case TY_DISPLAY_TP_CLEAR_ATTACHMENT:
        {
            /* Attachment send/receive finished in picture/detection mode,
             * mode state has been reset to AI_CHAT_IDLE (see wukong_ai_mode_picture.c).
             * Sync the top-right status label; desk_chat_refresh_stat_label() caches
             * the value and is null-safe when chat screen is not active. */
            desk_chat_refresh_stat_label(GUI_STAT_IDLE);
        }
        break;

        case TY_DISPLAY_TP_CHAT_STAT:
        {
            if (msg->data != NULL && msg->len >= 1) {
                desk_chat_refresh_stat_label(msg->data[0]);
            }
        }
        break;

        case TY_DISPLAY_TP_STAT_ONLINE:
        case TY_DISPLAY_TP_STAT_SLEEP:
        {
            desk_chat_refresh_stat_label(GUI_STAT_IDLE);
        }
        break;

        case TY_DISPLAY_TP_STAT_NETCFG:
        {
            desk_chat_refresh_stat_label(GUI_STAT_PROV);
        }
        break;

        case TY_DISPLAY_TP_LANGUAGE:
        {
            if (msg->data != NULL && msg->len >= 1) {
                gui_lang_set(msg->data[0]);
                desk_chat_refresh_stat_label_current();
                desk_chat_refresh_mode_label();
            }
        }
        break;

        default:
        break;
    }
    
#endif
}

#else

#include "lvgl/lvgl.h"

#define TINY_BTN_MAX_NUM 300

typedef enum {
    TINY_BTN_COLOR_BLUE = 0,
    TINY_BTN_COLOR_RED,
    TINY_BTN_COLOR_YELLOW,
} TINY_BTN_COLOR_E;

static uint8_t s_tiny_btn_color_state[TINY_BTN_MAX_NUM] = {0};

static void tiny_btn_apply_color(lv_obj_t *btn, TINY_BTN_COLOR_E color_state)
{
    lv_color_t color = lv_color_make(0x00, 0x00, 0xFF); // 默认蓝色

    if (color_state == TINY_BTN_COLOR_RED) {
        color = lv_color_make(0xFF, 0x00, 0x00);
    } else if (color_state == TINY_BTN_COLOR_YELLOW) {
        color = lv_color_make(0xFF, 0xFF, 0x00);
    }

    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, color, LV_STATE_DEFAULT);
}

static void btn_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        lv_obj_t *btn = lv_event_get_target(e);
        uint32_t id = (uint32_t)(uintptr_t)lv_event_get_user_data(e);

        if (id < TINY_BTN_MAX_NUM) {
            s_tiny_btn_color_state[id] = (s_tiny_btn_color_state[id] + 1) % 3;
            tiny_btn_apply_color(btn, (TINY_BTN_COLOR_E)s_tiny_btn_color_state[id]);
            LV_LOG_USER("Tiny button %d clicked, color_state=%d", (int)id, (int)s_tiny_btn_color_state[id]);
        }
    }
}

void create_tiny_6x6_buttons_fill_screen(void)
{
    const int BTN_W = 6;
    const int BTN_H = 6;
    const int GAP = 10;
    const int SCREEN_W = 320;
    const int SCREEN_H = 240;

    // 计算最大行列数
    int cols = (SCREEN_W + GAP) / (BTN_W + GAP); // 等价于 floor((W + G) / (B + G))
    int rows = (SCREEN_H + GAP) / (BTN_H + GAP);

    // 安全边界检查（防止除零，但这里不会）
    if (cols <= 0) cols = 1;
    if (rows <= 0) rows = 1;

    // 可选：居中显示（当前从 (0,0) 开始，左上对齐）
    // int start_x = (SCREEN_W - (cols * BTN_W + (cols - 1) * GAP)) / 2;
    // int start_y = (SCREEN_H - (rows * BTN_H + (rows - 1) * GAP)) / 2;
    int start_x = 0;
    int start_y = 0;

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int x = start_x + c * (BTN_W + GAP);
            int y = start_y + r * (BTN_H + GAP);

            lv_obj_t * btn = lv_btn_create(lv_scr_act());
            lv_obj_set_pos(btn, x, y);
            lv_obj_set_size(btn, BTN_W, BTN_H);

            // 可选：隐藏文字（6x6 太小，文字无法显示）
            // 如果需要标识，可用不同颜色或后续逻辑处理

            // 添加点击事件，传入唯一 ID
            uint32_t id = r * cols + c;
            lv_obj_set_tag(btn, NULL);
            lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)id);

            if (id < TINY_BTN_MAX_NUM) {
                s_tiny_btn_color_state[id] = TINY_BTN_COLOR_BLUE;
            }
            tiny_btn_apply_color(btn, TINY_BTN_COLOR_BLUE);

            // 优化：关闭滚动、边框等
            lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_border_width(btn, 0, LV_STATE_DEFAULT);
            lv_obj_set_style_shadow_width(btn, 0, LV_STATE_DEFAULT);
        }
    }

    LV_LOG_USER("Created %d x %d = %d tiny buttons (6x6, gap=%d)", cols, rows, cols * rows, GAP);
}
    
VOID app_ui_init(VOID)
{
    create_tiny_6x6_buttons_fill_screen();
}

void app_ui_msg_handler(TY_DISPLAY_MSG_T *msg)
{
    TAL_PR_DEBUG("[%s] type: %d", __func__, msg->type);

}

#endif

