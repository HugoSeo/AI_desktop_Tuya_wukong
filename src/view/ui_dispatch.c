#include "ui_dispatch.h"

#include "lv_vendor.h"
#ifdef CONFIG_ENABLE_TUYA_CAMERA
#include "tal_camera.h"
#include "tuya_device_camera.h"
#endif
#include "ui_common.h"
#include "wukong_picture.h"
#include "wukong_picture_input.h"

#define ALBUM_GRID_THUMB_SIZE  96
#define ALBUM_GRID_MAX_SELECT  30

static WUKONG_PICTURE_THUMB_LIST_T s_thumb_list = {0};

typedef VOID_T (*UI_MSG_HANDLER)(TY_DISPLAY_MSG_T *msg);
typedef OPERATE_RET (*UI_ACTION_HANDLER)(UINT8_T *msg, INT_T len);

typedef struct {
    TY_DISPLAY_TYPE_E type;
    UI_MSG_HANDLER handler;
} UI_MSG_DISPATCH_T;

typedef struct {
    TY_DISPLAY_ACTION_E action;
    UI_ACTION_HANDLER handler;
} UI_ACTION_DISPATCH_T;

#ifdef CONFIG_ENABLE_TUYA_CAMERA
STATIC VOID_T __display_camera_yuv_fram(TAL_CAMERA_FRAME_T *frame)
{
    if (NULL == frame) {
        return;
    }

    lv_vendor_disp_lock();
    ui_camera_set_preview_yuv_format(frame->width, frame->height, frame->data, frame->length);
    lv_vendor_disp_unlock();
}
#endif

STATIC VOID_T __view_image_from_album(VOID_T *arg)
{
    char *pic_name = (char *)arg;

    WUKONG_PICTURE_INFO_T pic = {0};
    if (wukong_picture_get_by_name(pic_name, &pic) != OPRT_OK) {
        PR_ERR("get picture by name failed: %s", pic_name);
        return;
    }

    if (pic.data && pic.len) {
        ui_nav_to(UI_SCR_ALBUM);
        ui_chat_disp_image(pic.data, pic.len);
    }

    wukong_picture_free_pic_info(&pic);
}

STATIC VOID_T __chat_image(CHAT_MSG_ROLE_TP_E type, char *pic_name)
{
    ui_chat_add_link(type, "查看图片", __view_image_from_album, pic_name, strlen(pic_name) + 1);
}

STATIC VOID_T __handle_human_chat(TY_DISPLAY_MSG_T *msg)
{
    ui_chat_add_text(CHAT_MSG_ROLE_USER, (CONST CHAR_T *)msg->data);
}

STATIC VOID_T __handle_ai_chat(TY_DISPLAY_MSG_T *msg)
{
    ui_chat_add_text(CHAT_MSG_ROLE_AI, (CONST CHAR_T *)msg->data);
}

STATIC VOID_T __handle_ai_chat_start(TY_DISPLAY_MSG_T *msg)
{
    ui_chat_stream_begin();
    if (NULL != msg->data) {
        ui_chat_stream_append((CONST CHAR_T *)msg->data);
    }
}

STATIC VOID_T __handle_ai_chat_data(TY_DISPLAY_MSG_T *msg)
{
    ui_chat_stream_append((CONST CHAR_T *)msg->data);
}

STATIC VOID_T __handle_ai_chat_stop(TY_DISPLAY_MSG_T *msg)
{
    (VOID_T)msg;
    ui_chat_stream_end();
}

STATIC VOID_T __handle_ai_image(TY_DISPLAY_MSG_T *msg)
{
    __chat_image(CHAT_MSG_ROLE_AI, (char *)msg->data);
}

STATIC VOID_T __handle_clear_attachment(TY_DISPLAY_MSG_T *msg)
{
    (VOID_T)msg;
    ui_chat_clear_attachment();
}

#ifdef CONFIG_ENABLE_TUYA_CAMERA
STATIC OPERATE_RET __action_open_camera(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    PR_DEBUG("camera open");
    tuya_device_camera_set_yuv_frame_cb(__display_camera_yuv_fram);
    tuya_device_camera_start();
    ui_nav_to(UI_SCR_CAMERA);
    return OPRT_OK;
}

STATIC OPERATE_RET __action_close_camera(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    PR_DEBUG("camera close");
    tuya_device_camera_stop();
    tuya_device_camera_set_yuv_frame_cb(NULL);
    ui_nav_back();
    return OPRT_OK;
}
#endif

STATIC OPERATE_RET __action_open_album(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    ui_nav_to(UI_SCR_ALBUM);
    wukong_picture_open_album();
    uint32_t count = wukong_picture_get_count();
    PR_DEBUG("album open: count=%d, seek to last", count);
    if (count > 0) {
        ui_album_set_empty_state(FALSE);
        wukong_picture_seek_to_photo(count);
        WUKONG_PICTURE_INFO_T pic = {0};
        if (wukong_picture_get_next(&pic) == OPRT_OK && pic.data && pic.len) {
            ui_album_set_jpeg_photo(pic.width, pic.height, pic.data, pic.len);
        }
        wukong_picture_free_pic_info(&pic);
    } else {
        ui_album_set_empty_state(TRUE);
    }
    return OPRT_OK;
}

STATIC OPERATE_RET __action_close_album(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    PR_DEBUG("album close");
    wukong_picture_close_album();
    ui_nav_back();
    return OPRT_OK;
}

STATIC OPERATE_RET __action_album_view_next(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    WUKONG_PICTURE_INFO_T pic = {0};
    if (wukong_picture_get_next(&pic) == OPRT_OK && pic.data && pic.len) {
        ui_album_set_jpeg_photo(pic.width, pic.height, pic.data, pic.len);
    }
    wukong_picture_free_pic_info(&pic);
    return OPRT_OK;
}

STATIC OPERATE_RET __action_album_view_prev(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    WUKONG_PICTURE_INFO_T pic = {0};
    if (wukong_picture_get_prev(&pic) == OPRT_OK && pic.data && pic.len) {
        ui_album_set_jpeg_photo(pic.width, pic.height, pic.data, pic.len);
    }
    wukong_picture_free_pic_info(&pic);
    return OPRT_OK;
}

#ifdef CONFIG_ENABLE_TUYA_CAMERA
STATIC OPERATE_RET __action_take_photo(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    uint8_t *jpeg = NULL;
    uint32_t jpeg_len = 0;
    tuya_device_camera_get_jpeg_frame(&jpeg, &jpeg_len, NULL);

    if (jpeg && jpeg_len) {
        char name[WUKONG_PICTURE_NAME_MAX_LEN + 1] = {0};
        wukong_picture_save_to_album(jpeg, jpeg_len, name);
        ui_camera_set_thumbnail_jpeg(jpeg, jpeg_len);
    }
    return OPRT_OK;
}
#endif

STATIC OPERATE_RET __action_album_delete(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    if (wukong_picture_delete_current() != OPRT_OK) {
        return OPRT_OK;
    }
    if (wukong_picture_get_count() == 0) {
        ui_album_set_empty_state(TRUE);
        ui_camera_clear_thumbnail();
        return OPRT_OK;
    }

    WUKONG_PICTURE_INFO_T pic = {0};
    if (wukong_picture_get_next(&pic) == OPRT_OK && pic.data && pic.len) {
        ui_album_set_jpeg_photo(pic.width, pic.height, pic.data, pic.len);
        ui_camera_set_thumbnail_jpeg(pic.data, pic.len);
    }
    wukong_picture_free_pic_info(&pic);
    return OPRT_OK;
}

STATIC OPERATE_RET __action_open_album_grid(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    PR_DEBUG("album: all photos grid");
    ui_nav_to(UI_SCR_ALBUM_GRID);
    wukong_picture_free_thumb_list(&s_thumb_list);
    wukong_picture_get_thumb_list(ALBUM_GRID_THUMB_SIZE, ALBUM_GRID_THUMB_SIZE, &s_thumb_list);
    ui_album_grid_set_thumbs(&s_thumb_list);
    return OPRT_OK;
}

STATIC OPERATE_RET __action_close_album_grid(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    PR_DEBUG("album grid: close");
    ui_nav_back();
    wukong_picture_free_thumb_list(&s_thumb_list);
    return OPRT_OK;
}

STATIC OPERATE_RET __action_album_batch_delete(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    CONST CHAR_T *names[ALBUM_GRID_MAX_SELECT];
    UINT32_T del_count = ui_album_grid_get_pending_delete_names(names, ALBUM_GRID_MAX_SELECT);
    PR_DEBUG("album grid: batch delete %u photos", del_count);
    if (del_count > 0) {
        wukong_picture_delete_batch(names, del_count);
    }
    if (del_count >= s_thumb_list.count) {
        ui_camera_clear_thumbnail();
    }
    return OPRT_OK;
}

STATIC OPERATE_RET __action_open_device_mode(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    PR_DEBUG("device mode open");
    ui_nav_to(UI_SCR_DEVICE_MODE);
    return OPRT_OK;
}

STATIC OPERATE_RET __action_close_device_mode(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    PR_DEBUG("device mode close");
    ui_nav_back();
    return OPRT_OK;
}

STATIC OPERATE_RET __action_album_ai_recognize(UINT8_T *msg, INT_T len)
{
    (VOID_T)msg;
    (VOID_T)len;

    char cur_name[WUKONG_PICTURE_NAME_MAX_LEN + 1] = {0};
    if (wukong_picture_get_current_name(cur_name) != OPRT_OK) {
        PR_ERR("album AI: no current picture");
        return OPRT_OK;
    }

    WUKONG_PICTURE_INFO_T pic = {0};
    if (wukong_picture_get_by_name(cur_name, &pic) != OPRT_OK || pic.data == NULL) {
        PR_ERR("album AI: get picture failed: %s", cur_name);
        wukong_picture_free_pic_info(&pic);
        return OPRT_OK;
    }

    PR_DEBUG("album AI: attach %s to chat", cur_name);
    wukong_picture_input_add_from_album(cur_name, NULL);
    wukong_picture_close_album();
    ui_nav_back();
#ifdef CONFIG_ENABLE_TUYA_CAMERA
    if (ui_nav_current() == UI_SCR_CAMERA) {
        tuya_device_camera_stop();
        tuya_device_camera_set_yuv_frame_cb(NULL);
        ui_nav_back();
    }
#endif
    ui_chat_set_attachment_jpeg(pic.data, pic.len);
    wukong_picture_free_pic_info(&pic);
    return OPRT_OK;
}

STATIC CONST UI_MSG_DISPATCH_T s_msg_dispatch_table[] = {
    { TY_DISPLAY_TP_HUMAN_CHAT,       __handle_human_chat },
    { TY_DISPLAY_TP_AI_CHAT,          __handle_ai_chat },
    { TY_DISPLAY_TP_AI_CHAT_START,    __handle_ai_chat_start },
    { TY_DISPLAY_TP_AI_CHAT_DATA,     __handle_ai_chat_data },
    { TY_DISPLAY_TP_AI_CHAT_STOP,     __handle_ai_chat_stop },
    { TY_DISPLAY_TP_AI_IMAGE,         __handle_ai_image },
    { TY_DISPLAY_TP_CLEAR_ATTACHMENT, __handle_clear_attachment },
};

STATIC CONST UI_ACTION_DISPATCH_T s_action_dispatch_table[] = {
#ifdef CONFIG_ENABLE_TUYA_CAMERA
    { TY_DISP_ACT_OPEN_CAMERA,        __action_open_camera },
    { TY_DISP_ACT_CLOSE_CAMERA,       __action_close_camera },
#endif
    { TY_DISP_ACT_OPEN_ALBUM,         __action_open_album },
    { TY_DISP_ACT_CLOSE_ALBUM,        __action_close_album },
    { TY_DISP_ACT_ALBUM_VIEW_NEXT_PIC, __action_album_view_next },
    { TY_DISP_ACT_ALBUM_VIEW_PREV_PIC, __action_album_view_prev },
#ifdef CONFIG_ENABLE_TUYA_CAMERA
    { TY_DISP_ACT_TAKE_PHOTO,         __action_take_photo },
#endif
    { TY_DISP_ACT_ALBUM_DELETE_PIC,   __action_album_delete },
    { TY_DISP_ACT_OPEN_ALBUM_GRID,    __action_open_album_grid },
    { TY_DISP_ACT_CLOSE_ALBUM_GRID,   __action_close_album_grid },
    { TY_DISP_ACT_ALBUM_BATCH_DELETE, __action_album_batch_delete },
    { TY_DISP_ACT_OPEN_DEVICE_MODE,   __action_open_device_mode },
    { TY_DISP_ACT_CLOSE_DEVICE_MODE,  __action_close_device_mode },
    { TY_DISP_ACT_ALBUM_AI_RECOGNIZE, __action_album_ai_recognize },
};

VOID_T ui_dispatch_msg(TY_DISPLAY_MSG_T *msg)
{
    UINT32_T i;

    if (NULL == msg) {
        return;
    }

    for (i = 0; i < sizeof(s_msg_dispatch_table) / sizeof(s_msg_dispatch_table[0]); i++) {
        if (s_msg_dispatch_table[i].type == msg->type) {
            s_msg_dispatch_table[i].handler(msg);
            return;
        }
    }
}

OPERATE_RET ui_dispatch_action(TY_DISPLAY_ACTION_E action, UINT8_T *msg, INT_T len)
{
    UINT32_T i;

    lv_vendor_disp_lock();

    for (i = 0; i < sizeof(s_action_dispatch_table) / sizeof(s_action_dispatch_table[0]); i++) {
        if (s_action_dispatch_table[i].action == action) {
            OPERATE_RET rt = s_action_dispatch_table[i].handler(msg, len);
            lv_vendor_disp_unlock();
            return rt;
        }
    }

    lv_vendor_disp_unlock();
    return OPRT_OK;
}
