#ifndef __DESK_FUNC_RECORD_H__
#define __DESK_FUNC_RECORD_H__

#include "desk_ui_res.h"
#include "tal_time_service.h"
#include "tuya_list.h"
#include "tal_mutex.h"

#define RECORD_TIME_DEFAULT "00:00.00"
#define RECORD_STORE_DIR       "/t5_fs/tmp/record"
#define RECORD_INFO_SAVE_PATH "/t5_fs/tmp/record/record_list.json"
#define RECORD_INFO_ITEM_NUM  100
/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef struct
{
    int id;
    char name[64];
    UINT64_T len;  
    UINT32_T duration;
    POSIX_TM_S create_time;
    LIST_HEAD list_node;
}RECORD_AUDIO_LIST_T;

typedef struct 
{
    int num;
    LIST_HEAD list_head;
    MUTEX_HANDLE mutex;
    BOOL_T inited;
}RECORD_AUDIO_LIST_HEAD_T;

typedef enum {
    RECORD_STATE_DEFAULT = 0,
    RECORD_STATE_RECORDING,
    RECORD_STATE_PAUSED,
} RECORD_STATE_E;

typedef struct
{
    lv_obj_t *record_scr;
    lv_obj_t *title;
    lv_obj_t *content;
    lv_obj_t *record_time;
    lv_obj_t *record_icon;
    lv_obj_t *list_btn;
    lv_obj_t *list_icon_obj;
    lv_obj_t *list_label;

    lv_obj_t *record_list_scr;
    lv_obj_t *record_list_title;
    lv_obj_t *record_list_cont;

    lv_obj_t *record_play_cont;
    lv_obj_t *play_date_label;
    lv_obj_t *play_slider;
    lv_obj_t *play_cur_time;
    lv_obj_t *play_total_time;
    lv_obj_t *play_btn_icon;

    lv_obj_t *upload_bar_cont;
    lv_obj_t *upload_bar;
    lv_obj_t *upload_pct_label;
} lv_record_ui_t;

typedef struct
{
    lv_img_dsc_t back_icon;
    lv_img_dsc_t list_icon;
    lv_img_dsc_t record_default_icon;
    lv_img_dsc_t record_recording_icon;
    lv_img_dsc_t record_pause_icon;
    lv_img_dsc_t play_playing_icon;
    lv_img_dsc_t play_pause_icon;
    lv_img_dsc_t fast_forward_icon;
    lv_img_dsc_t fast_back_icon;
    lv_img_dsc_t delete_icon;
    lv_img_dsc_t expand_icon;
    lv_img_dsc_t ai_camera_icon;
} record_scr_res_t;

/* ---------------------------------------------------------------------------
 * Function declarations
 * --------------------------------------------------------------------------- */
/**
 * @brief Create and initialize the record screen
 * @return none
 */
void setup_record_scr(void);

/**
 * @brief Release all resources held by the record screen
 * @return none
 */
void record_scr_res_clear(void);

/**
 * @brief Create and initialize the record list screen
 * @return none
 */
void setup_record_list_scr(void);

/**
 * @brief Release all resources held by the record list screen
 * @return none
 */
void record_list_scr_res_clear(void);

/**
 * @brief Register record audio input callback to AI mode record handle
 * @return OPRT_OK on success
 */
OPERATE_RET desk_record_handle_register(VOID);

#endif // __DESK_FUNC_RECORD_H__
