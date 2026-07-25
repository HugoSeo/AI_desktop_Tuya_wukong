#ifndef __GUI_BASE_H__
#define __GUI_BASE_H__

#include <stdio.h>
#include "tuya_gui_api.h"
#include "lvgl.h"

#ifndef TY_IMG_PATH
#define TY_IMG_PATH     ""
#endif

#define GUI_PAGE_ID_MAX          32

#if 1
#define LV_COLOR_WHITE lv_color_hex(0xFFFFFF)
#define LV_COLOR_SILVER lv_color_hex(0xC0C0C0)
#define LV_COLOR_GRAY lv_color_hex(0x808080)
#define LV_COLOR_BLACK lv_color_hex(0x000000)
#define LV_COLOR_RED lv_color_hex(0xFF0000)
#define LV_COLOR_MAROON lv_color_hex(0x800000)
#define LV_COLOR_YELLOW lv_color_hex(0xFFFF00)
#define LV_COLOR_OLIVE lv_color_hex(0x808000)
#define LV_COLOR_LIME lv_color_hex(0x00FF00)
#define LV_COLOR_GREEN lv_color_hex(0x008000)
#define LV_COLOR_CYAN lv_color_hex(0x00FFFF)
#define LV_COLOR_AQUA LV_COLOR_CYAN
#define LV_COLOR_TEAL lv_color_hex(0x008080)
#define LV_COLOR_BLUE lv_color_hex(0x0000FF)
#define LV_COLOR_NAVY lv_color_hex(0x000080)
#define LV_COLOR_MAGENTA lv_color_hex(0xFF00FF)
#define LV_COLOR_PURPLE lv_color_hex(0x800080)
#define LV_COLOR_ORANGE lv_color_hex(0xFFA500)
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*TY_SWIPE_BACK_FUNCTION)(void);

typedef enum {
    TY_PAGE_TP_MAIN = 0x1,     //main page
    TY_PAGE_TP_SUB = 0x2,      //subpages of the main page
    TY_PAGE_TP_OTHER = 0x4,    //other page: only goto last for the main/sub page
    TY_PAGE_TP_ALL = 0xffff,
} TY_PAGE_TP_E;

/**
 * @brief typr of page switching animation
 *
 */
typedef enum {
    TY_PAGE_ANIM_TP_NONE = 0,   //no switching animation
    TY_PAGE_ANIM_TP_ZOOM,       //page switching with zoom animation
    TY_PAGE_ANIM_TP_IN,         //page switching with in animation
    TY_PAGE_ANIM_TP_OUT,        //page switching with out animation
    TY_PAGE_ANIM_TP_IN_SIMPLE,  //简化的页面进入动画，无old page动画
    TY_PAGE_ANIM_TP_OUT_SIMPLE, //简化的页面跳出动画，无old page动画

} TY_PAGE_ANIM_TP_E;

typedef enum {
    TY_PAGE_LAYER_DEF = 0,      //default page
    TY_PAGE_LAYER_TOP = 1,      //layer_top page
    TY_PAGE_LAYER_SYS = 2,      //layer_sys page, layer_sys is on top of layer_top

} TY_PAGE_LAYER_E;

typedef OPERATE_RET (*TY_PAGE_CREATE_CB)(lv_obj_t *obj);
typedef OPERATE_RET (*TY_PAGE_START_CB)(lv_obj_t *obj, VOID *start_param, UINT_T param_len);
typedef OPERATE_RET (*TY_PAGE_STOP_CB)(lv_obj_t *obj);

typedef struct {
    CHAR_T              id[GUI_PAGE_ID_MAX];
    TY_PAGE_TP_E        type;               //page type

    TY_PAGE_CREATE_CB   page_create_cb;     //draw page base ui
    TY_PAGE_START_CB    page_start_cb;      //draw page service ui; start anim
    TY_PAGE_STOP_CB     page_stop_cb;       //stop anim

} TUYA_GUI_PAGE_S;

void tuya_gui_mutex_init(void);
void tuya_gui_mutex_lock(void);
void tuya_gui_mutex_unlock(void);

#define tuya_gui_call_atom(...)     \
    tuya_gui_mutex_lock();          \
    __VA_ARGS__                     \
    tuya_gui_mutex_unlock()


#ifdef __cplusplus
} // extern "C"
#endif

#endif // __GUI_BASE_H__

