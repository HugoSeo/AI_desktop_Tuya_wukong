#include "tuya_gui_page_internal.h"
#include "tuya_gui_page.h"

STATIC lv_style_t page_bg, page_scrollable;

OPERATE_RET gui_base_page_init(lv_obj_t *p_page)
{
    if (NULL == p_page) {
        PR_ERR("invalld param");
        return OPRT_INVALID_PARM;
    }

    lv_obj_add_style(p_page, &page_bg, LV_PART_MAIN);
    lv_obj_add_style(p_page, &page_scrollable, LV_PART_SCROLLBAR);

    lv_disp_t *d = lv_obj_get_disp(p_page);
    lv_obj_set_size(p_page, lv_disp_get_hor_res(d), lv_disp_get_ver_res(d));
    lv_obj_set_scrollbar_mode(p_page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(p_page, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_add_flag(p_page, LV_OBJ_FLAG_PRESS_LOCK);        //保持press，防止从该obj到子obj的滑动时，触发子obj的click或滑动

    return OPRT_OK;
}

OPERATE_RET gui_com_page_style_init()
{
    lv_obj_t* active_screen = lv_scr_act();

    if (active_screen != NULL && lv_obj_is_valid(active_screen))
        lv_obj_set_style_bg_color(active_screen, LV_COLOR_BLACK, 0);
    else {
/*
    如果使用启动背景图,由于切换到当前应用后,lv_scr_act()指向的默认屏幕已经失效,继续操作会导致系统crash!
    解决方法:用户后续需先显式创建并激活一个屏幕后方可对其操作,如:
    lv_obj_t* screen = lv_obj_create(NULL);  // 创建新屏幕
    lv_scr_load(screen);                    // 激活该屏幕
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0); // 安全操作
*/
    }

    lv_style_init(&page_bg);
    lv_style_set_pad_top(&page_bg, 0);
    lv_style_set_pad_left(&page_bg, 0);
    lv_style_set_pad_bottom(&page_bg, 0);
    lv_style_set_pad_right(&page_bg, 0);
    lv_style_set_bg_color(&page_bg, LV_COLOR_BLACK);
    lv_style_set_border_width(&page_bg, 0);

    lv_style_init(&page_scrollable);
    lv_style_set_pad_top(&page_scrollable, 0);
    lv_style_set_pad_left(&page_scrollable, 0);
    lv_style_set_pad_bottom(&page_scrollable, 0);
    lv_style_set_pad_right(&page_scrollable, 0);

    return OPRT_OK;
}
