#include "tuya_gui_gesture.h"
#include "tuya_gui_gesture_internal.h"
#include "tuya_gui_base.h"
#include "tuya_gui_navigate.h"
#include "tuya_gui_nav_internal.h"
#include <math.h>

STATIC TUYA_GEST_MGR_S gest_mgr = {0x0};

VOID tuya_nav_hor_gesture(lv_event_t* e);
OPERATE_RET tuya_nav_ver_gesture(lv_dir_t dir, TY_GUI_GEST_START_TP_E type);
VOID tuya_nav_hor_gesture_swipe_back(lv_event_t *e);

TY_GUI_GEST_START_TP_E gui_gesture_get_start_type(lv_coord_t pos)
{
    TY_GUI_GEST_START_TP_E type = TY_GUI_GESTURE_TP_TOP;
    if (pos <= TY_GESTURE_EDGE_MAX) {
        type = TY_GUI_GESTURE_TP_TOP;
    } else if (pos > LV_HOR_RES_MAX - TY_GESTURE_EDGE_MAX) {
        type = TY_GUI_GESTURE_TP_BOTTOM;
    } else {
        type = TY_GUI_GESTURE_TP_MID;
    }

    return type;
}

VOID_T gui_gesture_Round_screen_mode(BOOL_T en)
{
    gest_mgr.round_screen_mode = en;
}

STATIC BOOL_T gui_Round_screen_gesture_start_judge(lv_coord_t pos_x, lv_coord_t pos_y)
{
    double x = pos_x;
    double y = pos_y;
    double distance;

    if(pos_x > LV_HOR_RES_MAX/2){
        return false;
    }

    distance = (x - LV_HOR_RES_MAX/2 - TY_GESTURE_EDGE_MAX*2)*(x- LV_HOR_RES_MAX/2 - TY_GESTURE_EDGE_MAX*2) + \
        (y - LV_VER_RES_MAX/2)*(y - LV_VER_RES_MAX/2);

    distance = sqrt(distance);

    if(distance > LV_HOR_RES_MAX/2 && distance > LV_VER_RES_MAX/2){
        return TRUE;
    }

    return FALSE;
}

VOID gui_gesture_screen_event(lv_event_t *e)
{
    lv_event_code_t event = lv_event_get_code(e);

    if (event == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        lv_coord_t x = lv_indev_get_start_point_x(lv_indev_get_act());
        lv_coord_t y = lv_indev_get_start_point_y(lv_indev_get_act());

        if (dir == gest_mgr.last_dir && 
            uni_time_get_posix_ms() - gest_mgr.last_time < GUI_GESTURE_INTR_MIN) {
            PR_TRACE("repeat gesture");
            return;
        }

        PR_TRACE("dir: %d, x: %d, y: %d", dir, x, y);
        OPERATE_RET rt = OPRT_NOT_FOUND;
        if (LV_DIR_RIGHT == dir) {
            if(gest_mgr.round_screen_mode == TRUE){
                if(TRUE == gui_Round_screen_gesture_start_judge(x, y)){
                    tuya_nav_hor_gesture(e);
                }
            } else {
                if(TY_GUI_GESTURE_TP_TOP == gui_gesture_get_start_type(x)){
                    tuya_nav_hor_gesture(e);
                }
            }
        } else {
            rt = tuya_nav_ver_gesture(dir, gui_gesture_get_start_type(y));
        }

        if (OPRT_OK == rt) {
            gest_mgr.last_time = uni_time_get_posix_ms();
            gest_mgr.last_dir = dir;
        }
    } else if (event == LV_EVENT_GESTURE_RELEASED) {
        tuya_nav_hor_gesture_swipe_back(e);
    }
}

OPERATE_RET gui_gesture_start()
{
    return OPRT_OK;
}

OPERATE_RET gui_gesture_get_dir(lv_dir_t *p_dir)
{
    TUYA_CHECK_NULL_RETURN(p_dir, OPRT_INVALID_PARM);
    if (uni_time_get_posix_ms() - gest_mgr.last_time < 500) {
        *p_dir = gest_mgr.last_dir;
        return OPRT_OK;
    }

    return OPRT_NOT_FOUND;
}

VOID tuya_nav_hor_gesture_swipe_back(lv_event_t *e)
{
    CHAR_T current_page_id[GUI_PAGE_ID_MAX] = {0};
    gui_nav_get_current_page_id(current_page_id, GUI_PAGE_ID_MAX);

    if(FALSE == tuya_page_is_gesture_triggered(current_page_id)){
        return;
    }

    tuya_page_set_gesture_triggered(current_page_id, FALSE);

    lv_coord_t x = lv_indev_get_point_x(lv_indev_get_act());
    TY_SWIPE_BACK_FUNCTION fn = NULL;

    TY_GUI_GEST_START_TP_E type = gui_gesture_get_start_type(x);
    fn = tuya_page_get_swipe_back_fn(current_page_id);

    if (TY_GUI_GESTURE_TP_TOP != type && NULL != fn){
        fn();
        lv_indev_reset(lv_indev_get_act(), NULL);
    }
}

VOID tuya_nav_hor_gesture(lv_event_t *e)
{
    CHAR_T current_page_id[GUI_PAGE_ID_MAX] = {0};
    gui_nav_get_current_page_id(current_page_id, GUI_PAGE_ID_MAX);

    BOOL_T is_hor = tuya_page_is_hor(current_page_id);

    if (is_hor){
        return;
    }

    if (false == tuya_page_is_swipe_back(current_page_id)){
        return;
    }
    
    tuya_page_set_gesture_triggered(current_page_id, TRUE);
}

OPERATE_RET tuya_nav_ver_gesture(lv_dir_t dir, TY_GUI_GEST_START_TP_E type)
{
    CHAR_T top_gesture_id[GUI_PAGE_ID_MAX] = {0};
    CHAR_T current_page_id[GUI_PAGE_ID_MAX] = {0};

    gui_nav_get_top_gesture(top_gesture_id, GUI_PAGE_ID_MAX);
    gui_nav_get_current_page_id(current_page_id, GUI_PAGE_ID_MAX);

    PR_TRACE("current: %s dir: %d type:%d", current_page_id, dir, type);
    if (LV_DIR_BOTTOM == dir) {
        if (TY_GUI_GESTURE_TP_TOP == type && strcmp(current_page_id, top_gesture_id)) {
            BOOL_T forbid_cover = tuya_page_get_forbid_cover(current_page_id);
            if (forbid_cover) {
                PR_DEBUG("current: %s forbid cover", current_page_id);
                return OPRT_NOT_SUPPORTED;
            }

            tuya_gui_nav_goto_page(top_gesture_id);
            return OPRT_OK;
        }
    }

    return OPRT_INVALID_PARM;
}