#include "desk_event_handle.h"
#include "desk_handle_ui.h"
#include "desk_personal.h"
#include <stdint.h>
#include "tuya_device_camera.h"
#include "tuya_port_dma2d.h"

#if defined(ENABLE_TUYA_CAMERA) && (ENABLE_TUYA_CAMERA == 1)

static camera_scr_res_t s_camera_res = {0};

typedef struct
{
    lv_obj_t *canvas_obj;
    BOOL_T ai_camera_on;

    UINT8_T *display_buf;
    UINT8_T *display_rotate_buf;
    MUTEX_HANDLE mutex;
    BOOL_T camera_exit;
    UINT32_T refresh_frame_count;
    BOOL_T refresh_async_pending;
    BOOL_T dma2d_converting;
    UINT32_T lifecycle_generation;

    BOOL_T jpeg_start;
    BOOL_T jpeg_processing;
    BOOL_T jpeg_async_pending;
    UINT8_T *jpeg_data;
    UINT32_T jpeg_data_len;
    BOOL_T jpeg_save_to_album;

    lv_obj_t *ai_icon_img;
    BOOL_T ai_icon_switching;

    lv_obj_t *thumbnail_btn;
}CAMERA_DISPLAY_T;

STATIC BOOL_T s_ai_camera_on = FALSE;

static void camera_dma2d_convert_done_cb(TY_DMA2D_Task_T *task);
static int camera_in_canvas_start(void);

static CAMERA_DISPLAY_T s_camera_disp = {
    .canvas_obj = NULL,
    .ai_camera_on = FALSE,

    .display_buf = NULL,
    .display_rotate_buf = NULL,
    .mutex = NULL,
    .camera_exit = TRUE,
    .refresh_frame_count = 0,
    .refresh_async_pending = FALSE,
    .dma2d_converting = FALSE,
    .lifecycle_generation = 0,

    .jpeg_start = FALSE,
    .jpeg_processing = FALSE,
    .jpeg_async_pending = FALSE,
    .jpeg_data = NULL,
    .jpeg_data_len = 0,
    .jpeg_save_to_album = FALSE,

    .ai_icon_img = NULL,
    .ai_icon_switching = FALSE,

    .thumbnail_btn = NULL,
};

static void camera_lcd_rotate90_rgb565(uint16_t * src, uint16_t * dst, uint32_t src_width, uint32_t src_height, bool is_swap)
{
    #define ROTATE_TILE_SIZE 16
    uint32_t dst_stride = src_height;

    for (uint32_t tx = 0; tx < src_width; tx += ROTATE_TILE_SIZE) {
        uint32_t x_end = tx + ROTATE_TILE_SIZE;
        if (x_end > src_width) x_end = src_width;
        for (uint32_t ty = 0; ty < src_height; ty += ROTATE_TILE_SIZE) {
            uint32_t y_end = ty + ROTATE_TILE_SIZE;
            if (y_end > src_height) y_end = src_height;
            for (uint32_t x = tx; x < x_end; ++x) {
                uint32_t dst_row = src_width - x - 1;
                uint32_t dst_base = dst_row * dst_stride;
                for (uint32_t y = ty; y < y_end; ++y) {
                    if (is_swap) {
                        dst[dst_base + y] = WORD_SWAP(src[y * src_width + x]);
                    } else {
                        dst[dst_base + y] = src[y * src_width + x];
                    }
                }
            }
        }
    }
}

static void camera_canvas_refresh_async_cb(void *arg)
{
    lv_obj_t *canvas_obj = NULL;
    UINT32_T generation = (UINT32_T)(uintptr_t)arg;

    tal_mutex_lock(s_camera_disp.mutex);
    if (s_camera_disp.camera_exit == TRUE) {
        s_camera_disp.refresh_async_pending = FALSE;
        tal_mutex_unlock(s_camera_disp.mutex);
        return;
    }

    if (generation != s_camera_disp.lifecycle_generation) {
        TAL_PR_INFO("[%s][%d] drop stale refresh async, gen=%u current=%u", __func__, __LINE__,
            (unsigned)generation, (unsigned)s_camera_disp.lifecycle_generation);
        s_camera_disp.refresh_async_pending = FALSE;
        tal_mutex_unlock(s_camera_disp.mutex);
        return;
    }

    canvas_obj = s_camera_disp.canvas_obj;
    s_camera_disp.refresh_async_pending = FALSE;

    tal_mutex_unlock(s_camera_disp.mutex);

    if (canvas_obj != NULL) {
        lv_obj_invalidate(canvas_obj);
    }
}

static void camera_jpeg_done_async_cb(void *arg)
{
    OPERATE_RET rt = OPRT_OK;
    UINT8_T *jpeg_data = NULL;
    UINT32_T jpeg_data_len = 0;
    BOOL_T ai_camera_on = FALSE;
    BOOL_T save_to_album = FALSE;
    UINT32_T generation = (UINT32_T)(uintptr_t)arg;

    tal_mutex_lock(s_camera_disp.mutex);
    if (generation != s_camera_disp.lifecycle_generation) {
        TAL_PR_INFO("[%s][%d] drop stale jpeg async, gen=%u current=%u", __func__, __LINE__,
            (unsigned)generation, (unsigned)s_camera_disp.lifecycle_generation);
        tal_mutex_unlock(s_camera_disp.mutex);
        return;
    }

    jpeg_data = s_camera_disp.jpeg_data;
    jpeg_data_len = s_camera_disp.jpeg_data_len;
    ai_camera_on = s_camera_disp.ai_camera_on;
    save_to_album = s_camera_disp.jpeg_save_to_album;

    s_camera_disp.jpeg_data = NULL;
    s_camera_disp.jpeg_data_len = 0;
    s_camera_disp.jpeg_processing = FALSE;
    s_camera_disp.jpeg_start = FALSE;
    s_camera_disp.jpeg_save_to_album = FALSE;
    tuya_device_camera_stop_stream(CAM_STREAM_MJPEG);
    s_camera_disp.jpeg_async_pending = FALSE;
    tal_mutex_unlock(s_camera_disp.mutex);

    TAL_PR_INFO("[%s][%d] take photo jpeg len: %d, save_to_album: %d", __func__, __LINE__,
                jpeg_data_len, save_to_album);
    if(jpeg_data != NULL){
        if (save_to_album) {
            OPERATE_RET rt = desk_photo_add(jpeg_data, jpeg_data_len);
            if (rt != OPRT_OK) {
                TAL_PR_ERR("[%s][%d] desk_photo_add failed, rt=%d", __func__, __LINE__, rt);
            } else if (s_camera_disp.thumbnail_btn != NULL) {
                jpg_img_unload(&s_camera_res.thumbnail_icon);
                if (raw_jpg_img_load_with_scale(jpeg_data, jpeg_data_len,
                        &s_camera_res.thumbnail_icon, 40, 40) == OPRT_OK) {
                    lv_obj_t *thumb_img = lv_obj_get_child(s_camera_disp.thumbnail_btn, 0);
                    if (thumb_img == NULL) {
                        thumb_img = lv_img_create(s_camera_disp.thumbnail_btn);
                        lv_obj_align(thumb_img, LV_ALIGN_CENTER, 0, 0);
                        lv_obj_set_size(thumb_img, 40, 40);
                    }
                    lv_img_set_src(thumb_img, &s_camera_res.thumbnail_icon);
                    lv_obj_clear_flag(s_camera_disp.thumbnail_btn, LV_OBJ_FLAG_HIDDEN);
                }
            }
        } else {
            TUYA_FILE f = tkl_fopen(TAKE_PHOTO_JPEG_PATH, "w+");
            if (f == NULL) {
                TAL_PR_ERR("[%s][%d] open failed", __func__, __LINE__);
                tal_free(jpeg_data);
                return;
            }
            tkl_fwrite(jpeg_data, jpeg_data_len, f);
            tkl_fclose(f);

            if (ai_camera_on) {
                set_picture_message_file(TAKE_PHOTO_JPEG_PATH);
            }
        }

        if(ai_camera_on == TRUE) {
            if (tuya_ai_toy_device_mode_get() != AI_DEVICE_MODE_CHAT) {
                TAL_PR_INFO("[%s][%d] ai upload: switch device mode to CHAT", __func__, __LINE__);
                wukong_ai_device_mode_switch(AI_DEVICE_MODE_CHAT);
                desk_event_update_device_mode(AI_DEVICE_MODE_CHAT);
            }
            wukong_ai_agent_set_scene(AI_AGENT_SCODE_CHAT);
            wukong_ai_agent_input_start(TRUE);
            TUYA_CALL_ERR_LOG(wukong_ai_agent_send_image(jpeg_data, jpeg_data_len));
            TUYA_CALL_ERR_LOG(wukong_ai_agent_send_text("请解释刚刚上传的图片内容，请勿触发 MCP 技能。"));
            wukong_ai_agent_input_stop();
            desk_handle_ui_switch_to(DHUI_SCREEN_ID_CHAT, LV_SCR_LOAD_ANIM_FADE_ON, DHUI_SWITCH_PERMANENT);
        }

        tal_free(jpeg_data);
    }
}

/*
 * camera_request_refresh_locked / camera_request_jpeg_done_locked 已内联到
 * 各调用方，将 lv_async_call 移到 s_camera_disp.mutex 外部并用
 * lv_vendor_disp_lock 保护，避免跨线程修改 LVGL 定时器链表导致崩溃，
 * 同时规避 s_camera_disp.mutex 与 g_disp_mutex 的 ABBA 死锁。
 */

OPERATE_RET desk_camera_take_photo(void)
{
    OPERATE_RET rt = OPRT_OK;

    if (s_camera_disp.mutex == NULL) {
        TAL_PR_ERR("[%s][%d] camera mutex is NULL", __func__, __LINE__);
        return OPRT_RESOURCE_NOT_READY;
    }

    tal_mutex_lock(s_camera_disp.mutex);

    if (s_camera_disp.camera_exit == TRUE) {
        TAL_PR_ERR("[%s][%d] camera screen is not active", __func__, __LINE__);
        rt = OPRT_RESOURCE_NOT_READY;
        goto __exit;
    }

    if (s_camera_disp.jpeg_start || s_camera_disp.jpeg_processing || s_camera_disp.jpeg_async_pending) {
        TAL_PR_ERR("[%s][%d] camera jpeg is busy", __func__, __LINE__);
        rt = OPRT_COM_ERROR;
        goto __exit;
    }

    s_camera_disp.jpeg_start = TRUE;
    rt = tuya_device_camera_start_stream(CAM_STREAM_MJPEG);
    if (rt != OPRT_OK) {
        s_camera_disp.jpeg_start = FALSE;
        TAL_PR_ERR("[%s][%d] start jpeg stream failed, rt=%d", __func__, __LINE__, rt);
    }

__exit:
    tal_mutex_unlock(s_camera_disp.mutex);
    return rt;
}

/**
 * @brief Check whether camera canvas is initialized and active
 * @return TRUE if canvas is ready, FALSE otherwise
 */
BOOL_T desk_camera_is_canvas_ready(VOID)
{
    return (s_camera_disp.mutex != NULL && s_camera_disp.camera_exit == FALSE);
}

/** 异步执行 canvas 创建，避免切屏与图像处理复用同一调用栈 */
static void camera_in_canvas_start_async_cb(void *arg)
{
    (void)arg;
    if (s_camera_disp.camera_exit == FALSE)
        return;
    camera_in_canvas_start();
}

static int camera_in_canvas_start()
{
    if(s_camera_disp.camera_exit == FALSE)
    {
        TAL_PR_ERR("[%s][%d] camera is not exit", __func__, __LINE__);
        return OPRT_COM_ERROR;
    }

    if(s_camera_disp.mutex == NULL)
    {
        tal_mutex_create_init(&s_camera_disp.mutex);
    }

    s_camera_disp.display_buf = tal_malloc(LV_CANVAS_BUF_SIZE_TRUE_COLOR(DESK_LCD_WIDTH, DESK_LCD_HEIGHT));
    TUYA_CHECK_NULL_RETURN(s_camera_disp.display_buf, OPRT_MALLOC_FAILED);
    memset(s_camera_disp.display_buf, 0, LV_CANVAS_BUF_SIZE_TRUE_COLOR(DESK_LCD_WIDTH, DESK_LCD_HEIGHT));

    s_camera_disp.display_rotate_buf = tal_malloc(LV_CANVAS_BUF_SIZE_TRUE_COLOR(DESK_LCD_WIDTH, DESK_LCD_HEIGHT));
    TUYA_CHECK_NULL_RETURN(s_camera_disp.display_rotate_buf, OPRT_MALLOC_FAILED);
    memset(s_camera_disp.display_rotate_buf, 0, LV_CANVAS_BUF_SIZE_TRUE_COLOR(DESK_LCD_WIDTH, DESK_LCD_HEIGHT));

    lv_camera_ui_t *ui = &getContent()->st_func_camera;
    s_camera_disp.canvas_obj = lv_canvas_create(ui->camera_scr);
    lv_obj_set_pos(s_camera_disp.canvas_obj, 0, 0);
    lv_obj_set_size(s_camera_disp.canvas_obj, DESK_LCD_WIDTH, DESK_LCD_HEIGHT);
    lv_obj_set_style_border_width(s_camera_disp.canvas_obj, 0, 0);
    lv_canvas_set_buffer(s_camera_disp.canvas_obj, s_camera_disp.display_rotate_buf, DESK_LCD_WIDTH, DESK_LCD_HEIGHT, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(s_camera_disp.canvas_obj, lv_color_black(), LV_OPA_COVER);

    //在画布上层创建前景组件
    camera_foreground_obj_create();

    s_camera_disp.lifecycle_generation++;
    s_camera_disp.camera_exit = FALSE;
    s_camera_disp.dma2d_converting = FALSE;
    tuya_dma2d_complete_register_callback(TASK_TYPE_CAMERA_CONVERT, camera_dma2d_convert_done_cb);
    tuya_device_camera_start_stream(CAM_STREAM_YUV422);

    s_camera_disp.refresh_frame_count = 0;
    s_camera_disp.refresh_async_pending = FALSE;
    s_camera_disp.jpeg_async_pending = FALSE;
    TAL_PR_INFO("[%s][%d] camera session start, gen=%u canvas=%p", __func__, __LINE__,
        (unsigned)s_camera_disp.lifecycle_generation, s_camera_disp.canvas_obj);

    return OPRT_OK;
}

static int camera_in_canvas_stop()
{
    tal_mutex_lock(s_camera_disp.mutex);

    s_camera_disp.lifecycle_generation++;
    s_camera_disp.camera_exit = TRUE;
    tuya_device_camera_stop_stream(CAM_STREAM_YUV422);

    BOOL_T need_wait_dma2d = s_camera_disp.dma2d_converting;
    tal_mutex_unlock(s_camera_disp.mutex);

    /* 等待进行中的 DMA2D 转换完成，确保 buffer 安全释放 */
    if (need_wait_dma2d) {
        tuya_dma2d_wait_finish(TASK_TYPE_CAMERA_CONVERT);
    }
    tuya_dma2d_complete_register_callback(TASK_TYPE_CAMERA_CONVERT, NULL);

    tal_mutex_lock(s_camera_disp.mutex);

    s_camera_disp.refresh_frame_count = 0;
    s_camera_disp.refresh_async_pending = FALSE;
    s_camera_disp.dma2d_converting = FALSE;
    s_camera_disp.jpeg_async_pending = FALSE;
    s_camera_disp.jpeg_processing = FALSE;
    s_camera_disp.jpeg_start = FALSE;
    s_camera_disp.jpeg_save_to_album = FALSE;
    tuya_device_camera_stop_stream(CAM_STREAM_MJPEG);
    s_camera_disp.ai_icon_img = NULL;
    s_camera_disp.ai_icon_switching = FALSE;
    s_camera_disp.thumbnail_btn = NULL;

    if(s_camera_disp.canvas_obj != NULL)
    {
        lv_obj_del(s_camera_disp.canvas_obj);
        s_camera_disp.canvas_obj = NULL;
    }

    if(s_camera_disp.display_buf != NULL)
    {
        tal_free(s_camera_disp.display_buf);
        s_camera_disp.display_buf = NULL;
    }

    if(s_camera_disp.display_rotate_buf != NULL)
    {
        tal_free(s_camera_disp.display_rotate_buf);
        s_camera_disp.display_rotate_buf = NULL;
    }

    if(s_camera_disp.jpeg_data != NULL)
    {
        tal_free(s_camera_disp.jpeg_data);
        s_camera_disp.jpeg_data = NULL;
    }
    s_camera_disp.jpeg_data_len = 0;

    TAL_PR_INFO("[%s][%d] camera session stop, gen=%u", __func__, __LINE__,
        (unsigned)s_camera_disp.lifecycle_generation);

    tal_mutex_unlock(s_camera_disp.mutex);
    return OPRT_OK;
}

static void camera_dma2d_convert_done_cb(TY_DMA2D_Task_T *task)
{
    BOOL_T need_refresh = FALSE;
    UINT32_T generation = 0;

    tal_mutex_lock(s_camera_disp.mutex);

    if (s_camera_disp.camera_exit == TRUE) {
        s_camera_disp.dma2d_converting = FALSE;
        tal_mutex_unlock(s_camera_disp.mutex);
        return;
    }

    camera_lcd_rotate90_rgb565((uint16_t *)s_camera_disp.display_buf,
                               (uint16_t *)s_camera_disp.display_rotate_buf,
                               DESK_LCD_HEIGHT, DESK_LCD_WIDTH, false);

    if (s_camera_disp.refresh_async_pending == FALSE) {
        s_camera_disp.refresh_async_pending = TRUE;
        need_refresh = TRUE;
        generation = s_camera_disp.lifecycle_generation;
    }
    s_camera_disp.dma2d_converting = FALSE;

    tal_mutex_unlock(s_camera_disp.mutex);

    if (need_refresh) {
        lv_vendor_disp_lock();
        if (lv_async_call(camera_canvas_refresh_async_cb, (void *)(uintptr_t)generation) != LV_RES_OK) {
            tal_mutex_lock(s_camera_disp.mutex);
            s_camera_disp.refresh_async_pending = FALSE;
            tal_mutex_unlock(s_camera_disp.mutex);
        }
        lv_vendor_disp_unlock();
    }
}

void camera_to_rgb565(UINT8_T *data, UINT16_T width, UINT16_T height)
{
    TKL_DMA2D_FRAME_INFO_T in_frame = {0};
    TKL_DMA2D_FRAME_INFO_T out_frame = {0};

    tal_mutex_lock(s_camera_disp.mutex);

    if (s_camera_disp.camera_exit == TRUE) {
        tal_mutex_unlock(s_camera_disp.mutex);
        return;
    }

    if (s_camera_disp.dma2d_converting || s_camera_disp.refresh_async_pending) {
        tal_mutex_unlock(s_camera_disp.mutex);
        return;
    }

    in_frame.type = TUYA_FRAME_FMT_YUV422;
    in_frame.width = width;
    in_frame.height = height;
    in_frame.axis.x_axis = 0;
    in_frame.axis.y_axis = 0;
    in_frame.width_cp = 240;
    in_frame.height_cp = 320;
    in_frame.pbuf = data;

    out_frame.type = TUYA_FRAME_FMT_RGB565;
    out_frame.width = 240;
    out_frame.height = 320;
    out_frame.axis.x_axis = 0;
    out_frame.axis.y_axis = 0;
    out_frame.width_cp = 240;
    out_frame.height_cp = 320;
    out_frame.pbuf = s_camera_disp.display_buf;

    s_camera_disp.dma2d_converting = TRUE;
    tuya_dma2d_request(TASK_TYPE_CAMERA_CONVERT, &in_frame, &out_frame);

    tal_mutex_unlock(s_camera_disp.mutex);
}

void camera_to_jpg(UINT8_T *data, UINT32_T data_len)
{
    BOOL_T need_jpeg_async = FALSE;
    UINT32_T generation = 0;

    tal_mutex_lock(s_camera_disp.mutex);
    if(s_camera_disp.camera_exit == TRUE)
    {
        tal_mutex_unlock(s_camera_disp.mutex);
        return;
    }

    if (!s_camera_disp.jpeg_processing && s_camera_disp.jpeg_start)
    {
        s_camera_disp.jpeg_data = tal_malloc(data_len);
        if (s_camera_disp.jpeg_data != NULL) {
            s_camera_disp.jpeg_processing = TRUE;
            memcpy(s_camera_disp.jpeg_data, data, data_len);
            s_camera_disp.jpeg_data_len = data_len;
            if (s_camera_disp.jpeg_async_pending == FALSE) {
                s_camera_disp.jpeg_async_pending = TRUE;
                need_jpeg_async = TRUE;
                generation = s_camera_disp.lifecycle_generation;
            }
        }
        else {
            TAL_PR_ERR("[%s][%d] photo jpeg malloc failed", __func__, __LINE__);
            s_camera_disp.jpeg_start = FALSE;
            tuya_device_camera_stop_stream(CAM_STREAM_MJPEG);
        }
    }

    tal_mutex_unlock(s_camera_disp.mutex);

    if (need_jpeg_async) {
        lv_vendor_disp_lock();
        if (lv_async_call(camera_jpeg_done_async_cb, (void *)(uintptr_t)generation) != LV_RES_OK) {
            tal_mutex_lock(s_camera_disp.mutex);
            s_camera_disp.jpeg_async_pending = FALSE;
            tal_mutex_unlock(s_camera_disp.mutex);
        }
        lv_vendor_disp_unlock();
    }
}

static void camera_scr_back_btn_clicked_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        desk_handle_ui_back_to(DHUI_SCREEN_ID_PERSONAL_CENTER, LV_SCR_LOAD_ANIM_FADE_ON, DHUI_SWITCH_PERMANENT);
    }
}

/* 异步执行 AI 图标切换，避免在输入/事件回调栈中执行 png_img_load/unload 导致崩溃 */
static void camera_scr_ai_chat_icon_switch_async_cb(void *arg)
{
    UINT32_T generation = (UINT32_T)(uintptr_t)arg;
    lv_obj_t *icon_img = NULL;
    BOOL_T target_on = FALSE;
    const char *icon_path = NULL;

    if (s_camera_disp.camera_exit == TRUE) {
        s_camera_disp.ai_icon_switching = FALSE;
        return;
    }

    if (generation != s_camera_disp.lifecycle_generation) {
        TAL_PR_INFO("[%s][%d] drop stale ai-icon async, gen=%u current=%u", __func__, __LINE__,
            (unsigned)generation, (unsigned)s_camera_disp.lifecycle_generation);
        s_camera_disp.ai_icon_switching = FALSE;
        return;
    }

    icon_img = s_camera_disp.ai_icon_img;
    if (icon_img == NULL) {
        TAL_PR_ERR("[%s][%d] ai_icon_img is NULL", __func__, __LINE__);
        s_camera_disp.ai_icon_switching = FALSE;
        return;
    }

    target_on = (s_camera_disp.ai_camera_on == FALSE);
    icon_path = tuya_app_gui_get_picture_full_path(target_on ? ICON_AI_CAMERA_ON : ICON_AI_CAMERA_OFF);

    png_img_unload(&s_camera_res.ai_chat_icon);
    if (png_img_load(icon_path, &s_camera_res.ai_chat_icon) == 0) {
        lv_img_set_src(icon_img, &s_camera_res.ai_chat_icon);
        s_camera_disp.ai_camera_on = target_on;
        s_ai_camera_on = target_on;
    } else {
        TAL_PR_ERR("[%s][%d] png_img_load failed, rollback icon", __func__, __LINE__);
        icon_path = tuya_app_gui_get_picture_full_path(
            s_camera_disp.ai_camera_on ? ICON_AI_CAMERA_ON : ICON_AI_CAMERA_OFF);
        if (png_img_load(icon_path, &s_camera_res.ai_chat_icon) == 0) {
            lv_img_set_src(icon_img, &s_camera_res.ai_chat_icon);
        }
    }

    s_camera_disp.ai_icon_switching = FALSE;
}

static void camera_scr_ai_chat_btn_clicked_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        if (s_camera_disp.camera_exit == TRUE || s_camera_disp.ai_icon_switching == TRUE)
            return;
        s_camera_disp.ai_icon_switching = TRUE;
        lv_async_call(camera_scr_ai_chat_icon_switch_async_cb,
                      (void *)(uintptr_t)s_camera_disp.lifecycle_generation);
    }
}

static void camera_scr_thumbnail_clicked_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        desk_photo_set_show_latest();
        desk_handle_ui_switch_to(DHUI_SCREEN_ID_PHOTO, LV_SCR_LOAD_ANIM_FADE_ON, DHUI_SWITCH_PERMANENT);
    }
}

static void camera_scr_take_photo_clicked_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *btn = lv_event_get_target(e);

    if (code == LV_EVENT_PRESSED) {
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        return;
    }
    if (code == LV_EVENT_RELEASED) {
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        s_camera_disp.jpeg_save_to_album = TRUE;
        if (desk_camera_take_photo() != OPRT_OK) {
            s_camera_disp.jpeg_save_to_album = FALSE;
            TAL_PR_INFO("camera take photo start fail... dvp not exist !\n");
        } else {
            TAL_PR_INFO("camera take photo start...\n");
        }
        return;
    }
}

static void camera_foreground_obj_create()
{
    lv_camera_ui_t *ui = &getContent()->st_func_camera;

    /* 返回键：坐标(10,10)，40*40 圆形，置于最上层不受后续组件影响，用于退出 camera_scr */
    lv_obj_t *back_btn = lv_btn_create(ui->camera_scr);
    lv_obj_remove_style_all(back_btn);
    lv_obj_set_pos(back_btn, 10, 10);
    lv_obj_set_size(back_btn, 40, 40);
    lv_obj_set_style_radius(back_btn, 20, 0);   /* 半径 20 为圆形 */
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_50, 0);
    lv_obj_set_tag(back_btn, NULL);
    lv_obj_add_event_cb(back_btn, camera_scr_back_btn_clicked_cb, LV_EVENT_CLICKED, NULL);
    if(png_img_load(tuya_app_gui_get_picture_full_path(ICON_BACK_LEFT_24_24), &s_camera_res.back_icon) == 0) 
    {
        lv_obj_t *back_icon = lv_img_create(back_btn);
        lv_img_set_src(back_icon, &s_camera_res.back_icon);
        lv_obj_align(back_icon, LV_ALIGN_CENTER, 0, 0);    //相对于父对象居中
        lv_obj_set_size(back_icon, 24, 24);
    }

    lv_obj_t *ai_chat_btn = lv_btn_create(ui->camera_scr);
    lv_obj_remove_style_all(ai_chat_btn);
    lv_obj_set_pos(ai_chat_btn, 269, 10);
    lv_obj_set_size(ai_chat_btn, 40, 40);
    lv_obj_set_style_radius(ai_chat_btn, 20, 0);   /* 半径 20 为圆形 */
    lv_obj_set_style_bg_color(ai_chat_btn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ai_chat_btn, LV_OPA_50, 0);
    lv_obj_set_tag(ai_chat_btn, NULL);
    lv_obj_add_event_cb(ai_chat_btn, camera_scr_ai_chat_btn_clicked_cb, LV_EVENT_CLICKED, NULL);
    s_camera_disp.ai_camera_on = s_ai_camera_on;
    if(png_img_load(tuya_app_gui_get_picture_full_path(s_ai_camera_on ? ICON_AI_CAMERA_ON : ICON_AI_CAMERA_OFF), &s_camera_res.ai_chat_icon) == 0) 
    {
        s_camera_disp.ai_icon_img = lv_img_create(ai_chat_btn);
        lv_img_set_src(s_camera_disp.ai_icon_img, &s_camera_res.ai_chat_icon);
        lv_obj_align(s_camera_disp.ai_icon_img, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_size(s_camera_disp.ai_icon_img, 24, 24);
    }

    lv_obj_t *camera_cont = lv_obj_create(ui->camera_scr);
    lv_obj_remove_style_all(camera_cont);
    lv_obj_set_size(camera_cont, 50, 50);
    lv_obj_align(camera_cont, LV_ALIGN_BOTTOM_MID, 0, -15);    //相对于父对象居中
    lv_obj_set_style_radius(camera_cont, 25, 0);
    lv_obj_set_style_bg_opa(camera_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(camera_cont, 2, 0);
    lv_obj_set_style_border_color(camera_cont, lv_color_white(), 0);
    lv_obj_set_style_border_opa(camera_cont, LV_OPA_COVER, 0);

    lv_obj_t *camera_btn = lv_btn_create(camera_cont);
    lv_obj_remove_style_all(camera_btn);
    lv_obj_align(camera_btn, LV_ALIGN_CENTER, 0, 0);    //相对于父对象居中
    lv_obj_set_size(camera_btn, 40, 40);
    lv_obj_set_style_radius(camera_btn, 20, 0);   /* 半径 20 为圆形 */
    lv_obj_set_style_bg_color(camera_btn, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(camera_btn, LV_OPA_COVER, 0);
    lv_obj_set_tag(camera_btn, NULL);
    lv_obj_add_event_cb(camera_btn, camera_scr_take_photo_clicked_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(camera_btn, camera_scr_take_photo_clicked_cb, LV_EVENT_RELEASED, NULL);

    /* 缩略图按钮：camera_cont 左侧间隔 20px，40x40 圆角矩形 */
    lv_obj_t *thumb_btn = lv_btn_create(ui->camera_scr);
    lv_obj_remove_style_all(thumb_btn);
    lv_obj_set_pos(thumb_btn, 70, 180);
    lv_obj_set_size(thumb_btn, 40, 40);
    lv_obj_set_style_radius(thumb_btn, 8, 0);
    lv_obj_set_style_clip_corner(thumb_btn, true, 0);
    lv_obj_set_style_border_width(thumb_btn, 2, 0);
    lv_obj_set_style_border_color(thumb_btn, lv_color_white(), 0);
    lv_obj_set_style_border_opa(thumb_btn, LV_OPA_70, 0);
    lv_obj_set_tag(thumb_btn, NULL);
    lv_obj_add_event_cb(thumb_btn, camera_scr_thumbnail_clicked_cb, LV_EVENT_CLICKED, NULL);
    s_camera_disp.thumbnail_btn = thumb_btn;

    UINT32_T next_idx = 0;
    BOOL_T thumb_loaded = FALSE;
    if (desk_photo_get_next_index(&next_idx) == OPRT_OK && next_idx > 0) {
        UINT32_T latest_idx = next_idx - 1;
        CHAR_T path[PHOTO_PATH_MAX_LEN] = {0};
        if (desk_photo_build_path(latest_idx, path, sizeof(path)) == OPRT_OK) {
            INT_T file_size = tkl_fgetsize(path);
            if (file_size > 0) {
                TUYA_FILE f = tkl_fopen(path, "r");
                if (f != NULL) {
                    UINT8_T *data = (UINT8_T *)tal_malloc(file_size);
                    if (data != NULL) {
                        tkl_fread(data, file_size, f);
                        tkl_fclose(f);
                        f = NULL;
                        jpg_img_unload(&s_camera_res.thumbnail_icon);
                        if (raw_jpg_img_load_with_scale(data, file_size,
                                &s_camera_res.thumbnail_icon, 40, 40) == OPRT_OK) {
                            lv_obj_t *thumb_img = lv_img_create(thumb_btn);
                            lv_obj_align(thumb_img, LV_ALIGN_CENTER, 0, 0);
                            lv_obj_set_size(thumb_img, 40, 40);
                            lv_img_set_src(thumb_img, &s_camera_res.thumbnail_icon);
                            thumb_loaded = TRUE;
                        }
                        tal_free(data);
                    } else {
                        tkl_fclose(f);
                    }
                }
            }
        }
    }

    if (thumb_loaded) {
        lv_obj_set_style_bg_color(thumb_btn, lv_color_hex(0x333333), 0);
        lv_obj_set_style_bg_opa(thumb_btn, LV_OPA_70, 0);
    } else {
        lv_obj_set_style_bg_color(thumb_btn, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(thumb_btn, LV_OPA_COVER, 0);
    }
}

void setup_camera_scr(void)
{
    TAL_PR_INFO("[%s] enter ", __func__);
    lv_camera_ui_t *ui = &getContent()->st_func_camera;
    ui->camera_scr = lv_obj_create(NULL);
    lv_obj_set_size(ui->camera_scr, DESK_LCD_WIDTH, DESK_LCD_HEIGHT);
    lv_obj_set_scrollbar_mode(ui->camera_scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(ui->camera_scr, lv_color_hex(0x25262A), 0);
    lv_obj_set_style_pad_all(ui->camera_scr, 0, 0);

    lv_obj_update_layout(ui->camera_scr);

    /* 延迟到下一 LVGL tick 再创建 canvas，避开切屏同一调用栈的对象创建时序 */
    lv_async_call(camera_in_canvas_start_async_cb, NULL);
}

void camera_scr_leave(void)
{
    camera_in_canvas_stop();
    camera_scr_res_clear();
}

void camera_scr_res_clear(void)
{
    TAL_PR_INFO("[%s] enter ", __func__);
    png_img_unload(&s_camera_res.back_icon);
    png_img_unload(&s_camera_res.ai_chat_icon);
    jpg_img_unload(&s_camera_res.thumbnail_icon);
    memset(&s_camera_res, 0, sizeof(camera_scr_res_t));
}
#endif