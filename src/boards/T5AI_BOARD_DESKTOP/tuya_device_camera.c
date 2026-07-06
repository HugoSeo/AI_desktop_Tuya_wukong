#include "tuya_device_camera.h"
#include "tal_camera.h"
#include "tuya_device_cfg.h"
#include "tal_log.h"
#include "tal_semaphore.h"
#include "tal_mutex.h"
#include "tal_memory.h"
#include "tuya_device_board.h"
#include "lvgl/lvgl.h"
#if defined(ENABLE_TUYA_CAMERA) && (ENABLE_TUYA_CAMERA == 1)
#if defined(T5AI_BOARD_DESKTOP) && (T5AI_BOARD_DESKTOP == 1)
#include "ui/desk_func_camera.h"
#include "ui/desk_handle_ui.h"
#include "tal_system.h"
#include "tkl_fs.h"
#endif
#if defined(ENABLE_MQTT_P2P) && (ENABLE_MQTT_P2P == 1)
#include "tkl_audio.h"
#include "tuya_ipc_media.h"
#include "tuya_ipc_media_stream.h"
#include "tuya_p2p_app.h"
#endif

#if defined(ENABLE_TUYA_UI) && ENABLE_TUYA_UI == 1       
#include "tkl_dma2d.h"
#include "tuya_port_dma2d.h"
#include "lv_conf.h"
#include "ty_frame_buff.h"
#include "tuya_ai_display.h"
#include "tuya_lcd_pbuf.h"
#endif

extern OPERATE_RET tuya_board_get_camera_cfg(TAL_CAMERA_CFG_T *cfg);
extern void camera_to_rgb565(UINT8_T *data, UINT16_T width, UINT16_T height);
extern void camera_to_jpg(UINT8_T *data, UINT32_T data_len);
STATIC TAL_CAMERA_HANDLE_T  s_dvp_handle;

/* ---------------------------------------------------------------------------
 * YUV stream reference counting
 * --------------------------------------------------------------------------- */
STATIC UINT8_T      s_yuv_user_mask  = 0;
STATIC MUTEX_HANDLE s_yuv_user_mutex = NULL;

STATIC TUYA_YUV_FRAME_CB s_yuv_md_cb = NULL;
STATIC TUYA_MJPEG_FRAME_CB s_mjpeg_md_cb = NULL;

STATIC INT_T camera_to_p2p(UINT8_T is_i_frame, UINT8_T *data, UINT32_T data_len)
{
    if (tuya_ipc_get_client_online_num() <= 0) {
        return OPRT_COM_ERROR;
    }
    MEDIA_FRAME_T mf = {
        .type      = is_i_frame ? E_VIDEO_I_FRAME : E_VIDEO_PB_FRAME,
        .p_buf     = data,
        .size      = data_len,
        .pts       = tuya_p2p_misc_get_current_time_ms(),
        .timestamp = tuya_p2p_misc_get_current_time_ms(),
    };
    int rt = tuya_p2p_sdk_put_video_frame(&mf);
    TAL_PR_INFO("H264 frame size=%u rt=%d", data_len, rt);
    return rt;
}

static void on_mjpeg_frame(TAL_CAMERA_HANDLE_T handle, TAL_CAMERA_FRAME_T *frame, void *args)
{
    if (s_mjpeg_md_cb) {
        s_mjpeg_md_cb(frame->data, frame->length);
        return;
    }
    camera_to_jpg(frame->data, frame->length);
}

static void on_yuv_frame(TAL_CAMERA_HANDLE_T handle, TAL_CAMERA_FRAME_T *frame, void *args)
{
    if (s_yuv_user_mask & YUV_USER_PREVIEW) {
        camera_to_rgb565(frame->data, frame->width, frame->height);
        // return;
    }

    if ((s_yuv_user_mask & YUV_USER_MD) && s_yuv_md_cb) {
        s_yuv_md_cb(frame->data, frame->width, frame->height);
    }
}

static void on_h264_frame(TAL_CAMERA_HANDLE_T handle, TAL_CAMERA_FRAME_T *frame, void *args)
{
    TAL_PR_DEBUG("H264 frame size=%u", frame->length);
    camera_to_p2p(frame->is_i_frame, frame->data, frame->length);
}

#if defined(T5AI_BOARD_DESKTOP) && (T5AI_BOARD_DESKTOP == 1)
STATIC OPERATE_RET __wait_screen_ready(UINT_T screen_id, UINT32_T timeout_ms)
{
    UINT32_T waited_ms = 0;

    while (waited_ms < timeout_ms) {
        if (desk_handle_ui_get_current_screen_id() == screen_id) {
            return OPRT_OK;
        }
        tal_system_sleep(50);
        waited_ms += 50;
    }

    TAL_PR_ERR("wait screen ready timeout, screen_id=%u current=%u", screen_id,
               desk_handle_ui_get_current_screen_id());
    return OPRT_TIMEOUT;
}

STATIC OPERATE_RET __read_photo_file(BYTE_T **image_data, UINT_T *image_size)
{
    UINT32_T file_size = 0;
    TUYA_FILE file = NULL;
    BYTE_T *buf = NULL;
    UINT32_T read_size = 0;

    if (image_data == NULL || image_size == NULL) {
        return OPRT_INVALID_PARM;
    }

    *image_data = NULL;
    *image_size = 0;

    file_size = tkl_fgetsize(TAKE_PHOTO_JPEG_PATH);
    if (file_size == 0) {
        return OPRT_RESOURCE_NOT_READY;
    }

    file = tkl_fopen(TAKE_PHOTO_JPEG_PATH, "r+");
    if (file == NULL) {
        TAL_PR_ERR("open photo file failed");
        return OPRT_COM_ERROR;
    }

    buf = tal_malloc(file_size);
    if (buf == NULL) {
        tkl_fclose(file);
        return OPRT_MALLOC_FAILED;
    }

    read_size = tkl_fread(buf, file_size, file);
    tkl_fclose(file);
    if (read_size != file_size) {
        TAL_PR_ERR("read photo file failed, size=%u read=%u", file_size, read_size);
        tal_free(buf);
        return OPRT_COM_ERROR;
    }

    *image_data = buf;
    *image_size = file_size;
    return OPRT_OK;
}

/**
 * @brief Wait for camera canvas to be fully initialized (camera_exit == FALSE)
 * @param[in] timeout_ms maximum wait time in milliseconds
 * @return OPRT_OK on success, OPRT_TIMEOUT if canvas not ready within timeout
 */
STATIC OPERATE_RET __wait_camera_canvas_ready(UINT32_T timeout_ms)
{
    UINT32_T waited_ms = 0;

    while (waited_ms < timeout_ms) {
        if (desk_camera_is_canvas_ready()) {
            return OPRT_OK;
        }
        tal_system_sleep(50);
        waited_ms += 50;
    }

    TAL_PR_ERR("wait camera canvas ready timeout");
    return OPRT_TIMEOUT;
}

STATIC OPERATE_RET __wait_photo_ready(BYTE_T **image_data, UINT_T *image_size, UINT32_T timeout_ms)
{
    OPERATE_RET rt = OPRT_OK;
    UINT32_T waited_ms = 0;

    while (waited_ms < timeout_ms) {
        rt = __read_photo_file(image_data, image_size);
        if (rt == OPRT_OK) {
            return OPRT_OK;
        }
        tal_system_sleep(100);
        waited_ms += 100;
    }

    TAL_PR_ERR("wait photo ready timeout");
    return OPRT_TIMEOUT;
}
#endif

OPERATE_RET tuya_device_camera_init()
{
    OPERATE_RET rt = OPRT_OK;
    TAL_CAMERA_CFG_T cfg = {0};
    rt = tuya_board_get_camera_cfg(&cfg);
    if (rt != OPRT_OK) {
        TAL_PR_WARN("No camera on this board, skip init");
        return rt;
    }

    s_dvp_handle = tal_camera_init(&cfg);
    if (!s_dvp_handle) {
        TAL_PR_ERR("tal_camera_init failed");
        return OPRT_COM_ERROR;
    }

    tal_camera_register_cb(s_dvp_handle, TAL_STREAM_MJPEG,  on_mjpeg_frame, NULL);
    tal_camera_register_cb(s_dvp_handle, TAL_STREAM_YUV422, on_yuv_frame,   NULL);
    tal_camera_register_cb(s_dvp_handle, TAL_STREAM_H264,   on_h264_frame,  NULL);

    if (s_yuv_user_mutex == NULL) {
        tal_mutex_create_init(&s_yuv_user_mutex);
    }
    s_yuv_user_mask = 0;

    tuya_dma2d_init();

    TAL_PR_DEBUG("desktop camera init success!");
    return OPRT_OK;
}

OPERATE_RET tuya_device_camera_deinit(VOID)
{
    TAL_PR_INFO("tuya device camera deinit");
    if (s_dvp_handle) {
        tal_camera_deinit(s_dvp_handle);
        s_dvp_handle = NULL;
    }

    tuya_dma2d_deinit();

    return OPRT_OK;
}

OPERATE_RET tuya_device_camera_start(VOID)
{
    if (!s_dvp_handle) {
        return OPRT_COM_ERROR;
    }
    tal_camera_start_stream(s_dvp_handle, TAL_STREAM_YUV422);
    tal_camera_start_stream(s_dvp_handle, TAL_STREAM_H264);
    tal_camera_start_stream(s_dvp_handle, TAL_STREAM_MJPEG);
    return OPRT_OK;
}

OPERATE_RET tuya_device_camera_stop(VOID)
{
    if (!s_dvp_handle) {    
        return OPRT_COM_ERROR;
    }
    tal_camera_stop_stream(s_dvp_handle, TAL_STREAM_YUV422);
    tal_camera_stop_stream(s_dvp_handle, TAL_STREAM_MJPEG);
    tal_camera_stop_stream(s_dvp_handle, TAL_STREAM_H264);
    return OPRT_OK;
}

OPERATE_RET tuya_device_camera_h264_start()
{
    return tal_camera_start_stream(s_dvp_handle, TAL_STREAM_H264);
}

OPERATE_RET tuya_device_camera_h264_stop()
{
    return tal_camera_stop_stream(s_dvp_handle, TAL_STREAM_H264);
}

/**
 * @brief Acquire YUV stream for a specific user (reference counted)
 * @param[in] user caller identity bitmask
 * @return OPRT_OK on success
 */
OPERATE_RET tuya_device_camera_yuv_acquire(YUV_USER_E user)
{
    if (!s_dvp_handle) {
        return OPRT_COM_ERROR;
    }

    tal_mutex_lock(s_yuv_user_mutex);
    BOOL_T was_off = (s_yuv_user_mask == 0);
    s_yuv_user_mask |= (UINT8_T)user;
    tal_mutex_unlock(s_yuv_user_mutex);

    if (was_off) {
        TAL_PR_INFO("YUV stream start, user=0x%02x", (UINT8_T)user);
        return tal_camera_start_stream(s_dvp_handle, TAL_STREAM_YUV422);
    }
    TAL_PR_INFO("YUV stream already running, add user=0x%02x mask=0x%02x",
                (UINT8_T)user, s_yuv_user_mask);
    return OPRT_OK;
}

/**
 * @brief Release YUV stream for a specific user (reference counted)
 * @param[in] user caller identity bitmask
 * @return OPRT_OK on success
 */
OPERATE_RET tuya_device_camera_yuv_release(YUV_USER_E user)
{
    if (!s_dvp_handle) {
        return OPRT_COM_ERROR;
    }

    tal_mutex_lock(s_yuv_user_mutex);
    s_yuv_user_mask &= ~(UINT8_T)user;
    BOOL_T all_off = (s_yuv_user_mask == 0);
    tal_mutex_unlock(s_yuv_user_mutex);

    if (all_off) {
        TAL_PR_INFO("YUV stream stop, last user=0x%02x", (UINT8_T)user);
        return tal_camera_stop_stream(s_dvp_handle, TAL_STREAM_YUV422);
    }
    TAL_PR_INFO("YUV stream keep running, remove user=0x%02x mask=0x%02x",
                (UINT8_T)user, s_yuv_user_mask);
    return OPRT_OK;
}

/**
 * @brief Start YUV stream for preview (backward compatible wrapper)
 * @return OPRT_OK on success
 */
OPERATE_RET tuya_device_camera_yuv_start(VOID)
{
    return tuya_device_camera_yuv_acquire(YUV_USER_PREVIEW);
}

/**
 * @brief Stop YUV stream for preview (backward compatible wrapper)
 * @return OPRT_OK on success
 */
OPERATE_RET tuya_device_camera_yuv_stop(VOID)
{
    return tuya_device_camera_yuv_release(YUV_USER_PREVIEW);
}

/**
 * @brief Register YUV frame callback and acquire YUV stream for MD user
 * @param[in] cb YUV frame callback to receive raw YUV422 data
 * @return OPRT_OK on success
 */
OPERATE_RET tuya_device_camera_md_start(TUYA_YUV_FRAME_CB cb)
{
    s_yuv_md_cb = cb;
    return tuya_device_camera_yuv_acquire(YUV_USER_MD);
}

/**
 * @brief Unregister YUV frame callback and release YUV stream for MD user
 * @return OPRT_OK on success
 */
OPERATE_RET tuya_device_camera_md_stop(VOID)
{
    s_yuv_md_cb = NULL;
    return tuya_device_camera_yuv_release(YUV_USER_MD);
}

OPERATE_RET tuya_device_camera_jpeg_start()
{
    return tal_camera_start_stream(s_dvp_handle, TAL_STREAM_MJPEG);
}

OPERATE_RET tuya_device_camera_jpeg_stop()
{
    return tal_camera_stop_stream(s_dvp_handle, TAL_STREAM_MJPEG);
}

OPERATE_RET tuya_device_camera_switch_to_h264_mode(VOID)
{
    TAL_CAMERA_CFG_T cfg = {0};
    if (tuya_board_get_camera_cfg(&cfg) != OPRT_OK) {
        return OPRT_COM_ERROR;
    }
    return tal_camera_switch_output_mode(s_dvp_handle, &cfg,
                                         TUYA_CAMERA_OUTPUT_H264_YUV422_BOTH);
}

OPERATE_RET tuya_device_camera_switch_to_jpeg_mode(VOID)
{
    TAL_CAMERA_CFG_T cfg = {0};
    if (tuya_board_get_camera_cfg(&cfg) != OPRT_OK) {
        return OPRT_COM_ERROR;
    }
    return tal_camera_switch_output_mode(s_dvp_handle, &cfg,
                                         TUYA_CAMERA_OUTPUT_JPEG_YUV422_BOTH);
}

OPERATE_RET tuya_device_camera_get_jpeg_frame(BYTE_T **data, UINT_T *len, VOID *user_data)
{
    return tuya_device_camera_take_photo(data, len);
}

OPERATE_RET tuya_device_camera_take_photo(BYTE_T **image_data, UINT_T *image_size)
{
    OPERATE_RET rt = OPRT_OK;

    if (image_data == NULL || image_size == NULL) {
        return OPRT_INVALID_PARM;
    }

    *image_data = NULL;
    *image_size = 0;

    tkl_fs_remove(TAKE_PHOTO_JPEG_PATH);

    desk_handle_ui_switch_to(DHUI_SCREEN_ID_CAMERA, LV_SCR_LOAD_ANIM_FADE_ON, DHUI_SWITCH_PERMANENT);
    TUYA_CALL_ERR_RETURN(__wait_screen_ready(DHUI_SCREEN_ID_CAMERA, 2000));
    TUYA_CALL_ERR_RETURN(__wait_camera_canvas_ready(2000));
    TUYA_CALL_ERR_RETURN(desk_camera_take_photo());
    TUYA_CALL_ERR_RETURN(__wait_photo_ready(image_data, image_size, 5000));

    desk_handle_ui_switch_to(DHUI_SCREEN_ID_CHAT, LV_SCR_LOAD_ANIM_FADE_ON, DHUI_SWITCH_PERMANENT);
    return OPRT_OK;
}

/**
 * @brief Register MJPEG frame callback for MD and start MJPEG stream
 *
 * When registered, on_mjpeg_frame dispatches to this callback instead of
 * the normal camera_to_jpg path, keeping detection-mode capture separate
 * from the UI camera / MCP photo interface.
 */
OPERATE_RET tuya_device_camera_md_jpeg_start(TUYA_MJPEG_FRAME_CB cb)
{
    s_mjpeg_md_cb = cb;
    return tal_camera_start_stream(s_dvp_handle, TAL_STREAM_MJPEG);
}

/**
 * @brief Unregister MJPEG frame callback for MD and stop MJPEG stream
 */
OPERATE_RET tuya_device_camera_md_jpeg_stop(VOID)
{
    s_mjpeg_md_cb = NULL;
    return tal_camera_stop_stream(s_dvp_handle, TAL_STREAM_MJPEG);
}


#endif