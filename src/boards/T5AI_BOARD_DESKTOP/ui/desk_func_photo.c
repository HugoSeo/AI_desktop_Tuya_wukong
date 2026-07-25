#include "desk_event_handle.h"
#include "desk_handle_ui.h"

static photo_scr_res_t s_photo_res;

typedef struct {
    UINT32_T indices[PHOTO_LIST_MAX];
    UINT32_T count;
    INT32_T  cur_pos;
} photo_browser_t;

static photo_browser_t s_browser = {0};
static BOOL_T s_show_latest = FALSE;
static lv_obj_t *s_photo_img_obj   = NULL;
static lv_obj_t *s_no_photo_label  = NULL;
static lv_obj_t *s_ai_upload_btn   = NULL;
static lv_obj_t *s_delete_btn      = NULL;

/* ------------------------------------------------------------------ */
/*                      Photo file name parsing                       */
/* ------------------------------------------------------------------ */

static BOOL_T __parse_photo_index(CONST CHAR_T *name, UINT32_T *out_num)
{
    CONST UINT32_T prefix_len = strlen(PHOTO_NAME_PREFIX);
    CONST UINT32_T suffix_len = strlen(PHOTO_NAME_SUFFIX);
    UINT32_T name_len = strlen(name);

    if (name_len < prefix_len + 4 + suffix_len) {
        return FALSE;
    }
    if (memcmp(name, PHOTO_NAME_PREFIX, prefix_len) != 0) {
        return FALSE;
    }
    if (memcmp(name + name_len - suffix_len, PHOTO_NAME_SUFFIX, suffix_len) != 0) {
        return FALSE;
    }

    UINT32_T num = 0;
    UINT32_T digit_start = prefix_len;
    UINT32_T digit_end   = name_len - suffix_len;

    if (digit_end <= digit_start) {
        return FALSE;
    }

    for (UINT32_T i = digit_start; i < digit_end; i++) {
        if (name[i] < '0' || name[i] > '9') {
            return FALSE;
        }
        if (num > (UINT32_MAX - (name[i] - '0')) / 10) {
            return FALSE;
        }
        num = num * 10 + (name[i] - '0');
    }

    *out_num = num;
    return TRUE;
}

/* ------------------------------------------------------------------ */
/*                     Photo list scan / sort                         */
/* ------------------------------------------------------------------ */

static OPERATE_RET photo_list_scan(void)
{
    s_browser.count   = 0;
    s_browser.cur_pos = 0;

    TUYA_DIR dir = NULL;
    INT_T rt = tkl_dir_open(PHOTO_STORE_DIR, &dir);
    if (rt != 0 || dir == NULL) {
        return OPRT_OK;
    }

    TUYA_FILEINFO info = NULL;
    CONST CHAR_T *name = NULL;

    while (tkl_dir_read(dir, &info) == 0 && s_browser.count < PHOTO_LIST_MAX) {
        BOOL_T is_dir = FALSE;
        tkl_dir_is_directory(info, &is_dir);
        if (is_dir) {
            continue;
        }
        if (tkl_dir_name(info, &name) != 0 || name == NULL) {
            continue;
        }

        UINT32_T num = 0;
        if (__parse_photo_index(name, &num)) {
            s_browser.indices[s_browser.count++] = num;
        }
    }

    tkl_dir_close(dir);

    for (UINT32_T i = 0; i < s_browser.count; i++) {
        for (UINT32_T j = i + 1; j < s_browser.count; j++) {
            if (s_browser.indices[j] < s_browser.indices[i]) {
                UINT32_T tmp = s_browser.indices[i];
                s_browser.indices[i] = s_browser.indices[j];
                s_browser.indices[j] = tmp;
            }
        }
    }

    return OPRT_OK;
}

/* ------------------------------------------------------------------ */
/*                     Photo path / add / delete                      */
/* ------------------------------------------------------------------ */

OPERATE_RET desk_photo_build_path(UINT32_T index, CHAR_T *path, UINT32_T path_sz)
{
    if (path == NULL || path_sz == 0) {
        return OPRT_INVALID_PARM;
    }

    INT_T n = snprintf(path, path_sz, "%s/%s%04u%s",
                       PHOTO_STORE_DIR, PHOTO_NAME_PREFIX,
                       (unsigned int)index, PHOTO_NAME_SUFFIX);
    if (n < 0 || (UINT32_T)n >= path_sz) {
        TAL_PR_ERR("photo path truncated, index=%u", (unsigned int)index);
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

OPERATE_RET desk_photo_get_next_index(UINT32_T *next_index)
{
    if (next_index == NULL) {
        return OPRT_INVALID_PARM;
    }

    TUYA_DIR dir = NULL;
    INT_T rt = tkl_dir_open(PHOTO_STORE_DIR, &dir);
    if (rt != 0 || dir == NULL) {
        *next_index = 0;
        return OPRT_OK;
    }

    UINT32_T max_idx = 0;
    BOOL_T found = FALSE;
    TUYA_FILEINFO info = NULL;
    CONST CHAR_T *name = NULL;

    while (tkl_dir_read(dir, &info) == 0) {
        BOOL_T is_dir = FALSE;
        tkl_dir_is_directory(info, &is_dir);
        if (is_dir) {
            continue;
        }
        if (tkl_dir_name(info, &name) != 0 || name == NULL) {
            continue;
        }

        UINT32_T num = 0;
        if (__parse_photo_index(name, &num)) {
            if (!found || num > max_idx) {
                max_idx = num;
                found = TRUE;
            }
        }
    }

    tkl_dir_close(dir);

    if (found) {
        if (max_idx == UINT32_MAX) {
            TAL_PR_ERR("photo index overflow");
            return OPRT_COM_ERROR;
        }
        *next_index = max_idx + 1;
    } else {
        *next_index = 0;
    }

    return OPRT_OK;
}

static OPERATE_RET __photo_ensure_dir(void)
{
    BOOL_T is_exist = FALSE;

    /* /t5_fs/tmp */
    tkl_fs_is_exist("/t5_fs/tmp", &is_exist);
    if (!is_exist) {
        if (tkl_fs_mkdir("/t5_fs/tmp") != 0) {
            TAL_PR_ERR("mkdir /t5_fs/tmp failed");
            return OPRT_COM_ERROR;
        }
    }

    /* /t5_fs/tmp/picture */
    is_exist = FALSE;
    tkl_fs_is_exist(PHOTO_STORE_DIR, &is_exist);
    if (!is_exist) {
        if (tkl_fs_mkdir(PHOTO_STORE_DIR) != 0) {
            TAL_PR_ERR("mkdir %s failed", PHOTO_STORE_DIR);
            return OPRT_COM_ERROR;
        }
        TAL_PR_INFO("mkdir %s ok", PHOTO_STORE_DIR);
    }

    return OPRT_OK;
}

OPERATE_RET desk_photo_add(CONST UINT8_T *data, UINT32_T data_len)
{
    if (data == NULL || data_len == 0) {
        return OPRT_INVALID_PARM;
    }

    OPERATE_RET rt = __photo_ensure_dir();
    if (rt != OPRT_OK) {
        return rt;
    }

    UINT32_T idx = 0;
    rt = desk_photo_get_next_index(&idx);
    if (rt != OPRT_OK) {
        return rt;
    }

    CHAR_T path[PHOTO_PATH_MAX_LEN] = {0};
    rt = desk_photo_build_path(idx, path, sizeof(path));
    if (rt != OPRT_OK) {
        return rt;
    }

    TUYA_FILE f = tkl_fopen(path, "w+");
    if (f == NULL) {
        TAL_PR_ERR("open %s for write failed", path);
        return OPRT_COM_ERROR;
    }

    INT_T written = tkl_fwrite((VOID_T *)data, data_len, f);
    tkl_fclose(f);

    if (written != (INT_T)data_len) {
        TAL_PR_ERR("write %s failed, expect %u got %d", path,
                   (unsigned int)data_len, written);
        tkl_fs_remove(path);
        return OPRT_COM_ERROR;
    }

    TAL_PR_INFO("photo saved: %s (%u bytes)", path, (unsigned int)data_len);
    return OPRT_OK;
}

OPERATE_RET desk_photo_delete(UINT32_T index)
{
    CHAR_T path[PHOTO_PATH_MAX_LEN] = {0};
    OPERATE_RET rt = desk_photo_build_path(index, path, sizeof(path));
    if (rt != OPRT_OK) {
        return rt;
    }

    BOOL_T is_exist = FALSE;
    if (tkl_fs_is_exist(path, &is_exist) != 0 || !is_exist) {
        TAL_PR_ERR("photo %s not exist", path);
        return OPRT_COM_ERROR;
    }

    if (tkl_fs_remove(path) != 0) {
        TAL_PR_ERR("remove %s failed", path);
        return OPRT_COM_ERROR;
    }

    TAL_PR_INFO("photo deleted: %s", path);
    return OPRT_OK;
}

void desk_photo_set_show_latest(void)
{
    s_show_latest = TRUE;
}

/* ------------------------------------------------------------------ */
/*                     Photo display helpers                          */
/* ------------------------------------------------------------------ */

static OPERATE_RET photo_load_at_pos(void)
{
    if (s_browser.count == 0 || s_browser.cur_pos < 0 ||
        (UINT32_T)s_browser.cur_pos >= s_browser.count) {
        return OPRT_COM_ERROR;
    }

    CHAR_T path[PHOTO_PATH_MAX_LEN] = {0};
    OPERATE_RET rt = desk_photo_build_path(
        s_browser.indices[s_browser.cur_pos], path, sizeof(path));
    if (rt != OPRT_OK) {
        return rt;
    }

    INT_T file_size = tkl_fgetsize(path);
    if (file_size <= 0) {
        TAL_PR_ERR("photo file size invalid: %s", path);
        return OPRT_COM_ERROR;
    }

    TUYA_FILE f = tkl_fopen(path, "r+");
    if (f == NULL) {
        TAL_PR_ERR("open %s failed", path);
        return OPRT_COM_ERROR;
    }

    UINT8_T *data = (UINT8_T *)tal_malloc(file_size);
    if (data == NULL) {
        tkl_fclose(f);
        return OPRT_MALLOC_FAILED;
    }

    tkl_fread(data, file_size, f);
    tkl_fclose(f);

    jpg_img_unload(&s_photo_res.photo_icon);

    rt = raw_jpg_img_load_with_scale(data, file_size,
                                     &s_photo_res.photo_icon, 240, 240);
    tal_free(data);

    if (rt != OPRT_OK) {
        TAL_PR_ERR("decode %s failed", path);
    }
    return rt;
}

static void photo_show_empty_state(void)
{
    if (s_photo_img_obj != NULL) {
        lv_obj_add_flag(s_photo_img_obj, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ai_upload_btn != NULL) {
        lv_obj_add_flag(s_ai_upload_btn, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_delete_btn != NULL) {
        lv_obj_add_flag(s_delete_btn, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_no_photo_label != NULL) {
        lv_obj_clear_flag(s_no_photo_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void photo_show_image_state(void)
{
    if (s_photo_img_obj != NULL) {
        lv_obj_clear_flag(s_photo_img_obj, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ai_upload_btn != NULL) {
        lv_obj_clear_flag(s_ai_upload_btn, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_delete_btn != NULL) {
        lv_obj_clear_flag(s_delete_btn, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_no_photo_label != NULL) {
        lv_obj_add_flag(s_no_photo_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void photo_refresh_display(void)
{
    if (s_browser.count == 0) {
        photo_show_empty_state();
        return;
    }

    if (photo_load_at_pos() == OPRT_OK && s_photo_img_obj != NULL) {
        lv_img_set_src(s_photo_img_obj, &s_photo_res.photo_icon);
        photo_show_image_state();
    } else {
        photo_show_empty_state();
    }
}

/* ------------------------------------------------------------------ */
/*                      Event callbacks                               */
/* ------------------------------------------------------------------ */

static void photo_scr_back_btn_clicked_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        lv_indev_wait_release(lv_indev_get_act());
        desk_handle_ui_back(LV_SCR_LOAD_ANIM_FADE_ON, DHUI_SWITCH_PERMANENT);
    }
}

static void photo_scr_gesture_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_GESTURE) {
        return;
    }

    if (s_browser.count == 0) {
        return;
    }

    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());

    if (dir == LV_DIR_LEFT) {
        if ((UINT32_T)(s_browser.cur_pos + 1) >= s_browser.count) {
            return;
        }
        s_browser.cur_pos++;
        photo_refresh_display();
    } else if (dir == LV_DIR_RIGHT) {
        if (s_browser.cur_pos <= 0) {
            return;
        }
        s_browser.cur_pos--;
        photo_refresh_display();
    }
}

static void photo_scr_ai_upload_btn_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) {
        return;
    }

    if (s_browser.count == 0 || s_browser.cur_pos < 0 ||
        (UINT32_T)s_browser.cur_pos >= s_browser.count) {
        return;
    }

    CHAR_T path[PHOTO_PATH_MAX_LEN] = {0};
    OPERATE_RET rt = desk_photo_build_path(
        s_browser.indices[s_browser.cur_pos], path, sizeof(path));
    if (rt != OPRT_OK) {
        return;
    }

    INT_T file_size = tkl_fgetsize(path);
    if (file_size <= 0) {
        TAL_PR_ERR("ai upload: file size invalid %s", path);
        return;
    }

    TUYA_FILE f = tkl_fopen(path, "r");
    if (f == NULL) {
        TAL_PR_ERR("ai upload: open %s failed", path);
        return;
    }

    UINT8_T *data = (UINT8_T *)tal_malloc(file_size);
    if (data == NULL) {
        tkl_fclose(f);
        TAL_PR_ERR("ai upload: malloc %d failed", file_size);
        return;
    }

    tkl_fread(data, file_size, f);
    tkl_fclose(f);

    TAL_PR_INFO("ai upload: sending %s (%d bytes)", path, file_size);

    if (tuya_ai_toy_device_mode_get() != AI_DEVICE_MODE_CHAT) {
        TAL_PR_INFO("ai upload: switch device mode to CHAT");
        wukong_ai_device_mode_switch(AI_DEVICE_MODE_CHAT);
        desk_event_update_device_mode(AI_DEVICE_MODE_CHAT);
    }

    wukong_ai_agent_set_scene(AI_AGENT_SCODE_CHAT);
    wukong_ai_agent_input_start(TRUE);
    TUYA_CALL_ERR_LOG(wukong_ai_agent_send_image(data, file_size));
    TUYA_CALL_ERR_LOG(wukong_ai_agent_send_text("请解释刚刚上传的图片内容，请勿触发 MCP 技能。"));
    wukong_ai_agent_input_stop();

    tal_free(data);

    desk_handle_ui_switch_to(DHUI_SCREEN_ID_CHAT,
                             LV_SCR_LOAD_ANIM_FADE_ON,
                             DHUI_SWITCH_PERMANENT);
}

static void photo_scr_delete_btn_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) {
        return;
    }

    if (s_browser.count == 0) {
        return;
    }

    UINT32_T del_index = s_browser.indices[s_browser.cur_pos];
    desk_photo_delete(del_index);

    for (UINT32_T i = (UINT32_T)s_browser.cur_pos; i + 1 < s_browser.count; i++) {
        s_browser.indices[i] = s_browser.indices[i + 1];
    }
    s_browser.count--;

    if (s_browser.count == 0) {
        jpg_img_unload(&s_photo_res.photo_icon);
        photo_show_empty_state();
        return;
    }

    if ((UINT32_T)s_browser.cur_pos >= s_browser.count) {
        s_browser.cur_pos = (INT32_T)(s_browser.count - 1);
    }

    photo_refresh_display();
}

/* ------------------------------------------------------------------ */
/*                     Screen setup / cleanup                         */
/* ------------------------------------------------------------------ */

void setup_photo_scr(void)
{
    TAL_PR_INFO("[%s] enter ", __func__);
    lv_photo_ui_t *ui = &getContent()->st_func_photo;
    ui->photo_scr = lv_obj_create(NULL);
    lv_obj_set_size(ui->photo_scr, DESK_LCD_WIDTH, DESK_LCD_HEIGHT);
    lv_obj_set_scrollbar_mode(ui->photo_scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(ui->photo_scr, lv_color_hex(0x25262A), 0);
    lv_obj_set_style_pad_all(ui->photo_scr, 0, 0);

    photo_list_scan();

    /* photo image object */
    s_photo_img_obj = lv_img_create(ui->photo_scr);
    lv_img_set_angle(s_photo_img_obj, -900);
    lv_obj_align(s_photo_img_obj, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_size(s_photo_img_obj, 240, 240);
    lv_obj_add_flag(s_photo_img_obj, LV_OBJ_FLAG_HIDDEN);

    /* "no photos" label */
    s_no_photo_label = lv_label_create(ui->photo_scr);
    lv_label_set_text(s_no_photo_label, "暂时没有照片");
    lv_obj_set_style_text_color(s_no_photo_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_no_photo_label, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_align(s_no_photo_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_no_photo_label, LV_OBJ_FLAG_HIDDEN);

    if (s_browser.count > 0) {
        s_browser.cur_pos = (INT32_T)(s_browser.count - 1);
        s_show_latest = FALSE;
        if (photo_load_at_pos() == OPRT_OK) {
            lv_img_set_src(s_photo_img_obj, &s_photo_res.photo_icon);
            photo_show_image_state();
        } else {
            photo_show_empty_state();
        }
    } else {
        photo_show_empty_state();
    }

    /* gesture: swipe left / right */
    lv_obj_add_event_cb(ui->photo_scr, photo_scr_gesture_cb,
                        LV_EVENT_GESTURE, NULL);
    lv_obj_clear_flag(ui->photo_scr, LV_OBJ_FLAG_GESTURE_BUBBLE);

    /* back button: top-left */
    lv_obj_t *back_btn = lv_btn_create(ui->photo_scr);
    lv_obj_remove_style_all(back_btn);
    lv_obj_set_pos(back_btn, 10, 10);
    lv_obj_set_size(back_btn, 40, 40);
    lv_obj_set_style_radius(back_btn, 20, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_50, 0);
    lv_obj_set_tag(back_btn, NULL);
    lv_obj_add_event_cb(back_btn, photo_scr_back_btn_clicked_cb,
                        LV_EVENT_CLICKED, NULL);
    if (png_img_load(tuya_app_gui_get_picture_full_path(ICON_BACK_LEFT_24_24),
                     &s_photo_res.back_icon) == 0) {
        lv_obj_t *back_icon = lv_img_create(back_btn);
        lv_img_set_src(back_icon, &s_photo_res.back_icon);
        lv_obj_align(back_icon, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_size(back_icon, 24, 24);
    }

    /* AI upload button: left of delete button */
    s_ai_upload_btn = lv_btn_create(ui->photo_scr);
    lv_obj_remove_style_all(s_ai_upload_btn);
    lv_obj_set_pos(s_ai_upload_btn, DESK_LCD_WIDTH - 100, 10);
    lv_obj_set_size(s_ai_upload_btn, 40, 40);
    lv_obj_set_style_radius(s_ai_upload_btn, 20, 0);
    lv_obj_set_style_bg_color(s_ai_upload_btn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_ai_upload_btn, LV_OPA_50, 0);
    lv_obj_set_tag(s_ai_upload_btn, NULL);
    lv_obj_add_event_cb(s_ai_upload_btn, photo_scr_ai_upload_btn_cb,
                        LV_EVENT_CLICKED, NULL);
    if (png_img_load(tuya_app_gui_get_picture_full_path(ICON_AI_CAMERA_ON),
                     &s_photo_res.ai_upload_icon) == 0) {
        lv_obj_t *ai_icon = lv_img_create(s_ai_upload_btn);
        lv_img_set_src(ai_icon, &s_photo_res.ai_upload_icon);
        lv_obj_align(ai_icon, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_size(ai_icon, 24, 24);
    }

    /* delete button: top-right */
    s_delete_btn = lv_btn_create(ui->photo_scr);
    lv_obj_remove_style_all(s_delete_btn);
    lv_obj_set_pos(s_delete_btn, DESK_LCD_WIDTH - 50, 10);
    lv_obj_set_size(s_delete_btn, 40, 40);
    lv_obj_set_style_radius(s_delete_btn, 20, 0);
    lv_obj_set_style_bg_color(s_delete_btn, lv_color_hex(0xCC0000), 0);
    lv_obj_set_style_bg_opa(s_delete_btn, LV_OPA_70, 0);
    lv_obj_set_tag(s_delete_btn, NULL);
    lv_obj_add_event_cb(s_delete_btn, photo_scr_delete_btn_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *del_label = lv_label_create(s_delete_btn);
    lv_label_set_text(del_label, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(del_label, lv_color_white(), 0);
    lv_obj_align(del_label, LV_ALIGN_CENTER, 0, 0);

    if (s_browser.count == 0) {
        lv_obj_add_flag(s_ai_upload_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_delete_btn, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_update_layout(ui->photo_scr);
}

void photo_scr_res_clear(void)
{
    TAL_PR_INFO("[%s] enter ", __func__);
    png_img_unload(&s_photo_res.back_icon);
    png_img_unload(&s_photo_res.ai_upload_icon);
    jpg_img_unload(&s_photo_res.photo_icon);
    memset(&s_photo_res, 0, sizeof(photo_scr_res_t));

    s_photo_img_obj  = NULL;
    s_no_photo_label = NULL;
    s_ai_upload_btn  = NULL;
    s_delete_btn     = NULL;
    s_show_latest    = FALSE;
    memset(&s_browser, 0, sizeof(s_browser));
}
