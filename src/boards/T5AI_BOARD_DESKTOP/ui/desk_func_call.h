/**
 * @file desk_func_call.h
 * @brief Call screen UI
 * @version 1.0
 * @date 2025-04-16
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __DESK_FUNC_CALL_H__
#define __DESK_FUNC_CALL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "desk_ui_res.h"

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef enum {
    CALL_STATUS_IDLE = 0,
    CALL_STATUS_CALLING,
    CALL_STATUS_IN_CALL,
} CALL_STATUS_E;

typedef struct {
    lv_obj_t *call_scr;
    lv_obj_t *title_bar;
    lv_obj_t *content_cont;
    lv_obj_t *hangup_btn;
    lv_obj_t *answer_btn;
    lv_obj_t *status_label;
} lv_call_ui_t;

typedef struct {
    lv_img_dsc_t back_icon;
    lv_img_dsc_t hangup_icon;
    lv_img_dsc_t answer_icon;
} call_scr_res_t;

/* ---------------------------------------------------------------------------
 * Function declarations
 * --------------------------------------------------------------------------- */
/**
 * @brief Create and initialize the call screen
 * @return none
 */
void setup_call_scr(void);

/**
 * @brief Release all resources held by the call screen
 * @return none
 */
void call_scr_res_clear(void);

int call_status_event(void *data);

#ifdef __cplusplus
}
#endif
#endif /* __DESK_FUNC_CALL_H__ */
