
#include "tuya_app_gui.h"

#if 1
/* 这个函数会在屏幕上任何地方被点击时调用 */
static lv_obj_t * rgb_test_screen = NULL;
static lv_obj_t * tp_test_screen = NULL;

static void screen_mf_event_cb(lv_event_t *e);
static void tp_mf_event_cb(lv_event_t * e);

static void tp_mf_test(void)
{
    LV_LOG_USER("start touch test......");
    lv_scr_load(tp_test_screen);

    /*创建一个触控屏的按钮测试对象*/
    lv_obj_t * screen_test_tp_obj = NULL;
    screen_test_tp_obj = lv_btn_create(lv_scr_act());
    lv_obj_center(screen_test_tp_obj);
    lv_obj_add_event_cb(screen_test_tp_obj, tp_mf_event_cb, LV_EVENT_ALL, NULL);
    
    lv_obj_t * label = lv_label_create(screen_test_tp_obj);
    lv_label_set_text(label, "Press me");
    lv_obj_center(label);
}

static void screen_mf_test(void)
{
    LV_LOG_USER("start screen test......");
    lv_scr_load(rgb_test_screen);

    /*创建一个全屏的颜色测试对象*/
    lv_obj_t * screen_test_rgb_obj = NULL;
    screen_test_rgb_obj = lv_obj_create(lv_scr_act());
    lv_obj_set_size(screen_test_rgb_obj, LV_HOR_RES, LV_VER_RES);
    lv_obj_add_flag(screen_test_rgb_obj, LV_OBJ_FLAG_CLICKABLE); /*确保该对象可以被点击*/
    lv_obj_set_style_radius(screen_test_rgb_obj, 0, 0);     //设置边框无圆角
    lv_obj_set_style_bg_color(screen_test_rgb_obj, lv_color_black(), 0);  //设置默认黑色
    lv_obj_set_style_border_color(screen_test_rgb_obj, lv_color_black(), 0);   //设置边框黑色
    lv_obj_add_event_cb(screen_test_rgb_obj, screen_mf_event_cb, LV_EVENT_CLICKED, NULL);

    /*创建一个标签对象*/
    static lv_style_t style;
    lv_obj_t * label = lv_label_create(screen_test_rgb_obj);
    lv_label_set_text(label, "Click anywhere to Start Test!");
    lv_style_init(&style);
    lv_style_set_text_color(&style, lv_color_white());
    lv_obj_add_style(label, &style, 0);
    lv_obj_set_user_data(screen_test_rgb_obj, label);
    lv_obj_center(label);
}

static void tp_mf_event_cb(lv_event_t * e)
{
    static int tp_test_cycle_count = 0;
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * btn = lv_event_get_target(e);
    int tp_pos[] = {LV_ALIGN_TOP_LEFT, LV_ALIGN_TOP_MID, LV_ALIGN_TOP_RIGHT,   \
                    LV_ALIGN_LEFT_MID, LV_ALIGN_RIGHT_MID,    \
                    LV_ALIGN_BOTTOM_LEFT, LV_ALIGN_BOTTOM_MID, LV_ALIGN_BOTTOM_RIGHT};

    if(code == LV_EVENT_PRESSED) {
        lv_obj_t * label = lv_obj_get_child(btn, 0);
        lv_label_set_text(label, "Pressed");
    }
    else if(code == LV_EVENT_RELEASED) {
        lv_obj_t * label = lv_obj_get_child(btn, 0);
        lv_label_set_text(label, "Released");
        lv_obj_align(btn, tp_pos[tp_test_cycle_count++], 0, 0);
        if (tp_test_cycle_count > sizeof(tp_pos)/sizeof(tp_pos[0])) {
            tp_test_cycle_count = 0;
            /* 清除当前屏幕的所有子对象 */
            lv_obj_t * current_screen = lv_scr_act();
            lv_obj_clean(current_screen);
            screen_mf_test();
        }
    }
}

static void screen_mf_event_cb(lv_event_t *e)
{
    static int rgb_test_cycle_count = 0;
    lv_obj_t * obj = lv_event_get_target(e); /* 获取被点击的对象 */
    lv_event_code_t code = lv_event_get_code(e);
    lv_color_t current_bg_color = lv_obj_get_style_bg_color(obj, 0);

    if(code == LV_EVENT_CLICKED) {
    #if LVGL_VERSION_MAJOR < 9
        if (current_bg_color.full == lv_color_make(0xFF, 0, 0).full) 
    #else
        if (lv_color_eq(current_bg_color, lv_color_make(0xFF, 0, 0)))
    #endif
        {      //红色
            lv_obj_set_style_bg_color(obj, lv_color_make(0, 0xFF, 0), 0);  //设置屏幕绿色
            lv_obj_set_style_border_color(obj, lv_color_make(0, 0xFF, 0), 0);   //设置边框绿色
            rgb_test_cycle_count++;
        }
    #if LVGL_VERSION_MAJOR < 9
        else if (current_bg_color.full == lv_color_make(0, 0xFF, 0).full) 
    #else
        else if (lv_color_eq(current_bg_color, lv_color_make(0, 0xFF, 0)))
    #endif
        {    //绿色
            lv_obj_set_style_bg_color(obj, lv_color_make(0, 0, 0xFF), 0);  //设置屏幕蓝色
            lv_obj_set_style_border_color(obj, lv_color_make(0, 0, 0xFF), 0);   //设置边框蓝色
            rgb_test_cycle_count++;
        }
        else {      //蓝色或其他色
            lv_obj_t * label = lv_obj_get_user_data(obj);
            lv_label_set_text(label, "");
            lv_obj_set_style_bg_color(obj, lv_color_make(0xFF, 0, 0), 0);  //设置屏幕红色
            lv_obj_set_style_border_color(obj, lv_color_make(0xFF, 0, 0), 0);   //设置边框红色
            rgb_test_cycle_count++;
        }

        if (rgb_test_cycle_count > 3) {
            rgb_test_cycle_count = 0;
            /* 清除当前屏幕的所有子对象 */
            lv_obj_t * current_screen = lv_scr_act();
            lv_obj_clean(current_screen);
            tp_mf_test();
        }
    }
}

void screen_tp_mf_test(void)
{
    rgb_test_screen = lv_obj_create(NULL);
    tp_test_screen = lv_obj_create(NULL);
    /* 设置当前屏幕为RGB测试屏幕 */
    screen_mf_test();
}

void __attribute__((weak)) img_direct_flush_demo_start(void)
{
}

void __attribute__((weak)) tuya_app_gui_img_pre_decode(void)
{
}

void __attribute__((weak)) tuya_app_gui_img_pre_decode2(void)
{
}
#endif
