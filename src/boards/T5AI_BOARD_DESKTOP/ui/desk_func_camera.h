#ifndef __DESK_FUNC_CAMERA_H__
#define __DESK_FUNC_CAMERA_H__

#include "desk_ui_res.h"

#define TAKE_PHOTO_JPEG_PATH "/t5_fs/tmp/take_photo.jpeg"

typedef struct
{
    lv_obj_t *camera_scr;
}lv_camera_ui_t;

typedef struct 
{
    lv_img_dsc_t back_icon;
    lv_img_dsc_t ai_chat_icon;
    lv_img_dsc_t thumbnail_icon;
}camera_scr_res_t;

void setup_camera_scr(void);

void camera_scr_leave(void);

void camera_scr_res_clear(void);

OPERATE_RET desk_camera_take_photo(void);

BOOL_T desk_camera_is_canvas_ready(VOID);

static int camera_in_canvas_start();

static void camera_foreground_obj_create();

#endif // __DESK_FUNC_CAMERA_H__
