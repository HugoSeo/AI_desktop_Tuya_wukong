/**
 * @file desk_func_detection.h
 * @brief Detection message list screen UI
 * @version 1.0
 * @date 2025-04-14
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __DESK_FUNC_DETECTION_H__
#define __DESK_FUNC_DETECTION_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "desk_ui_res.h"

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef struct {
    lv_obj_t *detection_scr;
    lv_obj_t *title_bar;
    lv_obj_t *content_cont;
    lv_obj_t *page_dropdown;
    lv_obj_t *ai_detection_btn;
    lv_obj_t *loading_spinner;
} lv_detection_ui_t;

typedef struct {
    lv_img_dsc_t back_icon;
    lv_img_dsc_t list_icon;
    lv_img_dsc_t ai_camera_icon;
} detection_scr_res_t;

typedef struct {
    CHAR_T msgTitle[64];
    CHAR_T dateTime[32];
    CHAR_T attachPics[1024];
} detection_msg_item_t;

/* ---------------------------------------------------------------------------
 * Function declarations
 * --------------------------------------------------------------------------- */
/**
 * @brief Create and initialize the detection message list screen
 * @return none
 */
void setup_detection_scr(void);

/**
 * @brief Release all resources held by the detection screen
 * @return none
 */
void detection_scr_res_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* __DESK_FUNC_DETECTION_H__ */
