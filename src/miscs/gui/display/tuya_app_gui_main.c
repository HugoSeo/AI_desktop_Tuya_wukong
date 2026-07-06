/**
 * @file tuya_app_gui_main.c
 * @author www.tuya.com
 * @brief tuya_app_gui_main module is used to 
 * @version 0.1
 * @date 2024-8-26
 *
 * @copyright Copyright (c) tuya.inc 2022
 *
 */
#include "tuya_app_gui_config.h"
#include "tuya_app_gui_fs_path.h"
#include "tuya_app_gui_booton_page.h"
#include <stdbool.h>
#include "tkl_display.h"

#if defined(NXP_GUI_GUIDER)
#include "gui_guider.h"
#include "custom.h"
lv_ui guider_ui;
#elif defined(EEZ_STUDIO)
#include "ui.h"
#else
#ifdef TUYA_PAGE_TILEVIEW
#include "tuya_gui_tileview.h"
#endif
#endif

#ifdef EEZ_STUDIO
// 定义一个定时器回调，用于周期性调用 ui_tick()
static void eez_tick_cb(struct _lv_timer_t * timer) {
    ui_tick();
}
#endif

/**********************************>>>>>>>>>>gui侧配置代码,不要修改!!!!!!>>>>>>>>>>*****************************************/
const char *tuya_app_gui_get_lcd_name(void)
{
    return TUYA_LCD_IC_NAME;
}

int tuya_app_gui_get_lcd_width_size(void)
{
    return TUYA_LCD_WIDTH;
}

int tuya_app_gui_get_lcd_rotation(void)
{
    switch (TUYA_LCD_ROTATION) {
        case TUYA_SCREEN_ROTATION_0:
            return TKL_DISP_ROTATION_0;
        case TUYA_SCREEN_ROTATION_90:
            return TKL_DISP_ROTATION_90;
        case TUYA_SCREEN_ROTATION_180:
            return TKL_DISP_ROTATION_180;
        case TUYA_SCREEN_ROTATION_270:
            return TKL_DISP_ROTATION_270;
        default:
            return TKL_DISP_ROTATION_0;
    }
}

int tuya_app_gui_get_lcd_height_size(void)
{
    return TUYA_LCD_HEIGHT;
}

unsigned int tuya_app_gui_get_heartbeat_interval_time_ms(void)
{
    return GUI_HEART_BEAT_INTERVAL_MS;
}

unsigned int tuya_app_gui_get_screen_saver_maximum_time_ms(void)
{
    return GUI_IDEL_Time_MS;
}

unsigned int tuya_app_gui_bootOnPage2mainPage_transition_time_ms(void)
{
    if (BootPage2mainPageTime_MS > 0)
        return BootPage2mainPageTime_MS;
    else
        return 10;
}

unsigned int tuya_app_gui_bootOnPage_minmum_elpase_time_ms(void)
{
    return BootPageMinElapseTime_MS;
}

bool tuya_app_gui_get_screen_saver_enabled(void)
{
#ifdef GUI_SCREEN_SAVER
    return true;
#else
    return false;
#endif
}

bool tuya_app_gui_language_live_update_enabled(void)
{
#ifdef LANGUAGE_LIVE_UPDATE
    return true;
#else
    return false;
#endif
}

bool tuya_app_gui_memory_stack_dbg_enabled(void)
{
#ifdef GUI_MEM_USED_INFO_DBG
    return true;
#else
    return false;
#endif
}

unsigned int tuya_app_gui_lvgl_draw_buffer_rows(void)
{
    return LVGL_DRAW_BUFFER_ROWS;
}

bool tuya_app_gui_lvgl_draw_buffer_use_sram(void)
{
    if (LVGL_DRAW_BUFFER_USE_SRAM == 0)
        return false;
    return true;
}

unsigned char tuya_app_gui_lvgl_draw_buffer_count(void)
{
    if (LVGL_DRAW_BUFFER_CNT > 2)
        return 2;
    return LVGL_DRAW_BUFFER_CNT;
}

unsigned char tuya_app_gui_lvgl_frame_buffer_count(void)
{
    if (LVGL_FRAME_BUFFER_CNT > 2)
        return 2;
    return LVGL_FRAME_BUFFER_CNT;
}

/**********************************<<<<<<<<gui侧配置代码,不要修改!!!!!!<<<<<<<<*****************************************/

static void tuya_app_gui_main_start(void)
{
    /**************************************************!!!!!!!!**************************************************/
    /************************此处需直接运行图形初始化,不建议再封装task去调用图形初始化!***************************/
    /****否则需在task中lv_vendor_disp_lock/lv_vendor_disp_unlock调用图形初始化(避免和主lvgl任务异步而造成crash)****/
    /**************************************************!!!!!!!!**************************************************/
#if defined(TUYA_IMG_DIRECT_FLUSH) && (TUYA_IMG_DIRECT_FLUSH == 1)
    extern void img_direct_flush_demo_start(void);
    img_direct_flush_demo_start();
#else
    #if defined(ENABLE_TUYA_AI_DEMO)
        extern void tuya_ai_display_demo(void);
        tuya_ai_display_demo();
    #elif defined(TUYA_PAGE_TILEVIEW)
        tuya_gui_tileview_init();
    #elif defined(ENABLE_TUYA_BASIC_DEMO)
        extern void lv_example_switch_1(void);
        lv_example_switch_1();
    #elif defined(ENABLE_USER_GUI_APPLICATION)
        #if defined(NXP_GUI_GUIDER)
            custom_init(&guider_ui);
            setup_ui(&guider_ui);
        #elif defined(EEZ_STUDIO) 
            ui_init();
            lv_timer_create(eez_tick_cb, 10, NULL);     //定时刷新!
        #else
            //TODO...user gui app entry here!
        #endif
    #else
        #error "can not find any gui application entry"
    #endif
#endif
}

void tuya_app_gui_main_init(bool is_mf_test)
{
    if (is_mf_test) {
        extern void screen_tp_mf_test(void);
        screen_tp_mf_test();
    }
    else {
        #ifdef ENABLE_TUYA_AI_DEMO
        tuya_user_app_gui_startup_register(NULL, PAGE_IMG_SRC_UNKNOWN, tuya_app_gui_main_start, NULL, NULL);
        #else
/*
        ***************************使用背景图启动注意!!!!!*******************************
        背景图切换到用户屏幕应用时会删除当前屏幕导致lv_scr_act()指向的系统默认屏幕失效.
        用户应用避免马上直接操作系统默认屏幕,如通过lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0),否则系统异常.
        用户需先显式创建并激活一个屏幕后方可对其操作,如:
        lv_obj_t* screen = lv_obj_create(NULL);  // 创建新屏幕
        lv_scr_load(screen);                    // 激活该屏幕
        lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
        ***************************使用背景图启动注意!!!!!*******************************
*/
        uint32_t img_id = 4;
        extern void tuya_app_gui_img_pre_decode(void);
        extern void tuya_app_gui_img_pre_decode2(void);
        //tuya_user_app_gui_startup_register((const void *)tuya_app_gui_get_picture_full_path("3.png"), tuya_app_gui_main_start, tuya_app_gui_img_pre_decode, NULL);
        tuya_user_app_gui_startup_register((const void *)&img_id, tuya_app_gui_main_start, tuya_app_gui_img_pre_decode, tuya_app_gui_img_pre_decode2);
        #endif
    }
}
