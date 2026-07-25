#ifndef __DESK_FUNC_PHOTO_H__
#define __DESK_FUNC_PHOTO_H__

#include "desk_ui_res.h"

#define PHOTO_STORE_DIR       "/t5_fs/tmp/picture"
#define PHOTO_NAME_PREFIX     "TUYA"
#define PHOTO_NAME_SUFFIX     ".jpeg"
#define PHOTO_PATH_MAX_LEN    64
#define PHOTO_LIST_MAX        100

typedef struct
{
    lv_obj_t *photo_scr;
}lv_photo_ui_t;

typedef struct
{
    lv_img_dsc_t back_icon;
    lv_img_dsc_t photo_icon;
    lv_img_dsc_t ai_upload_icon;
}photo_scr_res_t;

void setup_photo_scr(void);

void photo_scr_res_clear(void);

OPERATE_RET desk_photo_add(CONST UINT8_T *data, UINT32_T data_len);

OPERATE_RET desk_photo_delete(UINT32_T index);

void desk_photo_set_show_latest(void);

OPERATE_RET desk_photo_get_next_index(UINT32_T *next_index);

OPERATE_RET desk_photo_build_path(UINT32_T index, CHAR_T *path, UINT32_T path_sz);

#endif // __DESK_FUNC_PHOTO_H__
