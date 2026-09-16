#include "ui_port_indev.h"
#include "lvgl.h"

#if defined(UI_INDEV_TOUCH) && UI_INDEV_TOUCH
#include "tuya_display_hw.h"

static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    ty_tp_point_info_t point;

    if (tuya_display_hw_tp_read(&point) == OPRT_OK) {
        data->point.x = point.m_x;
        data->point.y = point.m_y;
        data->state = point.m_state ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        data->continue_reading = point.m_need_continue ? true : false;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void touch_init(void)
{
    static lv_indev_drv_t drv;
    lv_indev_drv_init(&drv);
    drv.type = LV_INDEV_TYPE_POINTER;
    drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&drv);
}

#endif

#if defined(UI_INDEV_KEYPAD) && UI_INDEV_KEYPAD

static void keypad_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    data->state = LV_INDEV_STATE_RELEASED;
}

static void keypad_init(void)
{
    static lv_indev_drv_t drv;
    lv_indev_drv_init(&drv);
    drv.type = LV_INDEV_TYPE_KEYPAD;
    drv.read_cb = keypad_read_cb;
    lv_indev_drv_register(&drv);
}

#endif

#if defined(UI_INDEV_ENCODER) && UI_INDEV_ENCODER

static void encoder_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    data->enc_diff = 0;
    data->state = LV_INDEV_STATE_RELEASED;
}

static void encoder_init(void)
{
    static lv_indev_drv_t drv;
    lv_indev_drv_init(&drv);
    drv.type = LV_INDEV_TYPE_ENCODER;
    drv.read_cb = encoder_read_cb;
    lv_indev_drv_register(&drv);
}

#endif

void ui_port_indev_init(const ui_port_indev_cfg_t *cfg)
{
    (void)cfg;
#if defined(UI_INDEV_TOUCH) && UI_INDEV_TOUCH
    touch_init();
#endif
#if defined(UI_INDEV_KEYPAD) && UI_INDEV_KEYPAD
    keypad_init();
#endif
#if defined(UI_INDEV_ENCODER) && UI_INDEV_ENCODER
    encoder_init();
#endif
}
