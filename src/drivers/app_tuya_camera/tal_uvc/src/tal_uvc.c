/**
 * @file tal_uvc.c
 * @brief Tuya Abstraction Layer for UVC camera driver implementation
 *
 * Delivers raw frames (JPEG / H264 / YUV422) to the registered callback.
 * Format conversion (MJPEG → RGB565) is handled by the lcd_service layer,
 * not here.
 *
 * @author linch
 * @date 2025-02-09
 * @version 1.0.0
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All rights reserved.
 */

#include "tuya_cloud_types.h"
#include "driver/media_types.h"
#include "tal_uvc.h"
#include "tal_memory.h"
#include "tal_log.h"
#include "tal_system.h"
#include "tuya_device_cfg.h"

// For LCD display support
#if defined(ENABLE_TUYA_UI) && ENABLE_TUYA_UI == 1
#include "tuya_lcd_pbuf.h"
#include "ty_frame_buff.h"
#include "tal_semaphore.h"
#include "tuya_port_disp.h"
#include "tkl_dma2d.h"
#endif

// Default UVC port
#ifndef TAL_UVC_DEFAULT_PORT
#define TAL_UVC_DEFAULT_PORT    (1)
#endif


// UVC callback debug log switch (0: disable, 1: enable)
#ifndef TAL_UVC_LOG_ENABLE
#define TAL_UVC_LOG_ENABLE 0
#endif

#if TAL_UVC_LOG_ENABLE
#define TAL_UVC_LOG(fmt, ...) TAL_PR_DEBUG(fmt, ##__VA_ARGS__)
#else
#define TAL_UVC_LOG(fmt, ...) do {} while(0)
#endif

// Forward declarations
bk_err_t media_app_camera_open(camera_handle_t *handle, media_camera_device_t *device);
bk_err_t media_app_camera_close(camera_handle_t *handle);
bk_err_t media_app_camera_start(camera_handle_t *handle, media_camera_device_t *device);
bk_err_t media_app_camera_stop(camera_handle_t *handle);
bk_err_t media_app_register_read_frame_callback(image_format_t fmt, frame_cb_t cb);
bk_err_t media_app_unregister_read_frame_callback(void);
bk_err_t media_app_camera_set_frame_drop_mode(uint8_t mode);
bk_err_t media_app_camera_set_power_pin(uint32_t gpio_id);

typedef struct {    
    camera_handle_t handle;         // Camera handle from media layer
    media_camera_device_t device;   // Camera device configuration
    tal_uvc_frame_cb_t frame_cb;    // User frame callback
    void *args;                      // User arguments for callback
} TAL_UVC_T;

// Internal frame callback wrapper (converts frame_cb_t to tal_uvc_frame_cb_t)
static void __tal_uvc_frame_callback_wrapper(frame_buffer_t *frame);

// Global pointer to current UVC instance (for callback wrapper and context management)
static TAL_UVC_T *s_current_uvc = NULL;

// Format mapping direction
#define TAL_FMT_FROM_PIXEL    1  // 映射到涂鸦: pixel_format_t -> TUYA_FRAME_FMT_E

/**
 * @brief Map TUYA_CAMERA_OUTPUT_MODE to media layer image_format_t.
 *        Used during tal_uvc_init() to configure the media device.
 */
static image_format_t __output_mode_to_image_fmt(TUYA_CAMERA_OUTPUT_MODE mode)
{
    switch (mode) {
        case TUYA_CAMERA_OUTPUT_JPEG:            return IMAGE_MJPEG;
        case TUYA_CAMERA_OUTPUT_H264:            return IMAGE_H264;
        case TUYA_CAMERA_OUTPUT_YUV422:          return IMAGE_YUV;
        default:
            TAL_PR_WARN("UVC: unrecognized output_mode %d, defaulting to MJPEG", mode);
            return IMAGE_MJPEG;
    }
}

/**
 * @brief Map media layer pixel_format_t to TUYA_FRAME_FMT_E.
 *        Used in the frame callback wrapper.
 */
static TUYA_FRAME_FMT_E __pixel_fmt_to_tuya(pixel_format_t fmt)
{
    switch (fmt) {
        case PIXEL_FMT_JPEG:   return TUYA_FRAME_FMT_JPEG;
        case PIXEL_FMT_YUV422: return TUYA_FRAME_FMT_YUV422;
        case PIXEL_FMT_H264:   return TUYA_FRAME_FMT_H264;
        default:               return TUYA_FRAME_FMT_JPEG;
    }
}

/**
 * @brief Initialize UVC camera
 * 
 * @param[out] handle UVC handle pointer (will be allocated and initialized)
 * @param[in]  cfg UVC configuration (width, height, format, fps, etc.)
 * 
 * @return OPERATE_RET
 *   - OPRT_OK: Success (including already initialized case)
 *   - OPRT_INVALID_PARM: Invalid parameters
 *   - OPRT_MALLOC_FAILED: Memory allocation failed
 *   - OPRT_COM_ERROR: Camera open failed
 * 
 * @note This function allocates TAL_UVC_T structure, converts TAL_UVC_CFG_T to 
 *       media_camera_device_t and opens the camera. If already initialized, returns OPRT_OK.
 */
OPERATE_RET tal_uvc_init(TAL_UVC_HANDLE_T *handle, TAL_UVC_CFG_T *cfg)
{
    if (!handle || !cfg) {
        return OPRT_INVALID_PARM;
    }

    // Check if already initialized (use global context)
    if (s_current_uvc != NULL) {
        // Already initialized, return the global context handle
        *handle = (TAL_UVC_HANDLE_T)s_current_uvc;
        return OPRT_OK;
    }

    // Ensure the handle pointer is initialized to NULL before allocation
    *handle = NULL;

    // Allocate UVC structure
    TAL_UVC_T *uvc = (TAL_UVC_T *)tal_malloc(sizeof(TAL_UVC_T));
    if (!uvc) {
        return OPRT_MALLOC_FAILED;
    }
    memset(uvc, 0, sizeof(TAL_UVC_T));

    // Initialize device structure
    uvc->device.type = UVC_CAMERA;
    uvc->device.port = TAL_UVC_DEFAULT_PORT;
    
    // Map TUYA_CAMERA_OUTPUT_MODE to image_format_t
    uvc->device.format = __output_mode_to_image_fmt(cfg->output_mode);
    
    // Copy configuration parameters
    uvc->device.width = cfg->width;
    uvc->device.height = cfg->height;
    uvc->device.fps = cfg->fps;
    uvc->device.rotate = ROTATE_NONE;

    // Save user callback and arguments
    uvc->frame_cb = cfg->frame_cb;
    uvc->args = cfg->args;

    /* @param mode 丢帧模式
    *   0: 不丢帧 (30fps → 30fps)
    *   1: 每2帧保留1帧 (30fps → 15fps)
    *   2: 每3帧保留1帧 (30fps → 10fps)
    *   3: 每4帧保留1帧 (30fps → 7.5fps)
    *   N: 每(N+1)帧保留1帧
    */
    media_app_camera_set_frame_drop_mode(1);
    media_app_camera_set_power_pin(cfg->power_pin);

    // Open camera
    camera_handle_t uvc_handle = NULL;
    bk_err_t ret = media_app_camera_open(&uvc_handle, &uvc->device);
    if (ret != BK_OK) {
        tal_free(uvc);
        return OPRT_COM_ERROR;
    }

    // Store camera handle
    uvc->handle = uvc_handle;

    // Set global context
    s_current_uvc = uvc;

    // Return UVC handle to caller
    *handle = (TAL_UVC_HANDLE_T)uvc;

    TAL_PR_DEBUG("tal_uvc_init %p: handle %p, format %d", uvc, uvc->handle, uvc->device.format);

    return OPRT_OK;
}

/**
 * @brief Internal frame callback wrapper
 * 
 * Converts frame_cb_t callback (from media layer) to tal_uvc_frame_cb_t callback (user layer)
 * 
 * @param[in] frame Frame buffer from media layer
 */
static void __tal_uvc_frame_callback_wrapper(frame_buffer_t *frame)
{
    if (!s_current_uvc || !s_current_uvc->frame_cb || !frame) {
        return;
    }

    TAL_UVC_FRAME_T tal_frame;
    tal_frame.frame     = frame->frame;
    tal_frame.length    = frame->length;
    tal_frame.width     = frame->width;
    tal_frame.height    = frame->height;
    tal_frame.sequence  = frame->sequence;
    tal_frame.timestamp = frame->timestamp;
    tal_frame.fmt       = __pixel_fmt_to_tuya(frame->fmt);

    TAL_UVC_LOG("uvc_frame_cb: pixel_fmt=%d -> tuya_fmt=%d len=%d",
                frame->fmt, tal_frame.fmt, frame->length);

    s_current_uvc->frame_cb((TAL_UVC_HANDLE_T)s_current_uvc,
                            &tal_frame, s_current_uvc->args);
}

OPERATE_RET tal_uvc_start(TAL_UVC_HANDLE_T handle)
{
    TAL_UVC_T *uvc = (TAL_UVC_T *)handle;
    //! FIXME:
    if (!uvc) {
        uvc = (TAL_UVC_T *)s_current_uvc;
        if (!uvc) {
            return OPRT_INVALID_PARM;
        }
    }

    // Check if user callback is configured
    if (!uvc->frame_cb) {
        return OPRT_INVALID_PARM;
    }
    TAL_PR_DEBUG("tal_uvc_start %p: handle %p, format %d", uvc, uvc->handle, uvc->device.format);

    // Register internal wrapper callback with media layer
    bk_err_t ret = media_app_register_read_frame_callback(uvc->device.format, __tal_uvc_frame_callback_wrapper);
    if (ret != BK_OK) {
        return OPRT_COM_ERROR;
    }

    return media_app_camera_start(&uvc->handle, &uvc->device);
    // return OPRT_OK;
}

OPERATE_RET tal_uvc_stop(TAL_UVC_HANDLE_T handle)
{
    TAL_UVC_T *uvc = (TAL_UVC_T *)handle;

    //! FIXME:
    if (!uvc) {
        uvc = (TAL_UVC_T *)s_current_uvc;
        if (!uvc) {
            return OPRT_INVALID_PARM;
        }
    }

    // Unregister frame callback
    bk_err_t ret = media_app_unregister_read_frame_callback();
    if (ret != BK_OK) {
        return OPRT_COM_ERROR;
    }

    return media_app_camera_stop(&uvc->handle);
}

/**
 * @brief Start reading UVC frames
 * 
 * @param[in] handle UVC handle
 * 
 * @return OPERATE_RET
 *   - OPRT_OK: Success
 *   - OPRT_INVALID_PARM: Invalid parameter or no callback configured
 *   - OPRT_COM_ERROR: Register callback failed
 * 
 * @note Frame callback should be set in TAL_UVC_CFG_T during initialization
 */
OPERATE_RET tal_uvc_read_start(TAL_UVC_HANDLE_T handle)
{
    TAL_UVC_T *uvc = (TAL_UVC_T *)handle;

    //! FIXME:
    if (!uvc) {
        uvc = (TAL_UVC_T *)s_current_uvc;
        if (!uvc) {
            return OPRT_INVALID_PARM;
        }
    }

    // Check if user callback is configured
    if (!uvc->frame_cb) {
        return OPRT_INVALID_PARM;
    }

    // Register internal wrapper callback with media layer
    bk_err_t ret = media_app_register_read_frame_callback(uvc->device.format, __tal_uvc_frame_callback_wrapper);
    if (ret != BK_OK) {
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

/**
 * @brief Stop reading UVC frames
 * 
 * @param[in] handle UVC handle
 * 
 * @return OPERATE_RET
 *   - OPRT_OK: Success
 *   - OPRT_INVALID_PARM: Invalid parameter
 *   - OPRT_COM_ERROR: Unregister callback failed
 */
OPERATE_RET tal_uvc_read_stop(TAL_UVC_HANDLE_T handle)
{
    TAL_UVC_T *uvc = (TAL_UVC_T *)handle;

    //! FIXME:
    if (!uvc) {
        uvc = (TAL_UVC_T *)s_current_uvc;
        if (!uvc) {
            return OPRT_INVALID_PARM;
        }
    }

    // Unregister frame callback
    bk_err_t ret = media_app_unregister_read_frame_callback();
    if (ret != BK_OK) {
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

/**
 * @brief Close UVC camera and free resources
 * 
 * @param[in] handle UVC handle (will be freed)
 * 
 * @return OPERATE_RET
 *   - OPRT_OK: Success
 *   - OPRT_INVALID_PARM: Invalid parameter
 *   - OPRT_COM_ERROR: Camera close failed
 */
OPERATE_RET tal_uvc_deinit(TAL_UVC_HANDLE_T handle)
{
    if (!handle) {
        return OPRT_INVALID_PARM;
    }

    TAL_UVC_T *uvc = (TAL_UVC_T *)handle;

    // Unregister frame callback
    media_app_unregister_read_frame_callback();

    // Close camera if handle exists
    if (uvc->handle) {
        bk_err_t ret = media_app_camera_close(&uvc->handle);
        if (ret != BK_OK) {
            TAL_PR_ERR("Failed to close camera: %d", ret);
        }
    }

    // Clear global context if it matches this handle
    if (s_current_uvc == uvc) {
        s_current_uvc = NULL;
    }

    // Free UVC structure
    tal_free(uvc);

    TAL_PR_DEBUG("UVC deinitialized");
    return OPRT_OK;
}

#if defined(ENABLE_TUYA_UI) && ENABLE_TUYA_UI == 1 && TAL_UVC_TEST == 1
/**
 * @brief LCD frame flush callback for test - releases LCD buffer back to pool
 */
static void __tal_uvc_test_lcd_flush_callback(ty_frame_buffer_t *frame)
{
    // SPI transmission complete, release buffer to pool
    // The pbuf node pointer is stored in frame->pdata
    if (frame && frame->pdata) {
        tuya_lcd_pbuf_node_t *pbuf_node = (tuya_lcd_pbuf_node_t *)frame->pdata;
        tuya_lcd_pbuf_release(pbuf_node);
        TAL_UVC_LOG("Test: LCD frame flush complete, buffer released");
    }
}

/**
 * @brief Test frame callback for CLI test with LCD display support
 */
static void __tal_uvc_test_frame_callback(TAL_UVC_HANDLE_T handle, TAL_UVC_FRAME_T *frame, void *args)
{
    SYS_TICK_T start, end;
    tuya_lcd_pbuf_node_t *lcd_buffer = NULL;

    if (!frame || !frame->frame || frame->length == 0) {
        TAL_PR_ERR("Test: Invalid frame");
        return;
    }
    TAL_PR_DEBUG("Test: frame callback, fmt=%d", frame->fmt);

    if (frame->fmt != TUYA_FRAME_FMT_JPEG) {
        TAL_UVC_LOG("Test: Unsupported frame format: %d (expected JPEG)", frame->fmt);
        return;
    }

    /* Static decode buffer — allocated once on first frame */
    static UINT8_T *s_decode_buf = NULL;
    if (!s_decode_buf) {
        s_decode_buf = (UINT8_T *)tal_psram_malloc((UINT32_T)frame->width * frame->height * 2);
        if (!s_decode_buf) {
            TAL_PR_ERR("Test: decode_buf alloc failed");
            return;
        }
    }

    /* Step 1: JPEG hw decode → RGB565 */
    start = tal_system_get_tick_count();
    int decode_ret = bk_jpeg_hw_decode_to_mem(
        (uint8_t *)frame->frame,
        (uint8_t *)s_decode_buf,
        frame->length,
        frame->width,
        frame->height
    );
    end = tal_system_get_tick_count();
    if (decode_ret != 0) {
        TAL_PR_ERR("Test: JPEG hw decode failed: %d", decode_ret);
        return;
    }
    TAL_UVC_LOG("Test: JPEG hw decode: %u ticks", (uint32_t)(end - start));

    /* Step 2: Acquire LCD buffer */
    lcd_buffer = tuya_lcd_pbuf_acquire(300);
    if (!lcd_buffer) {
        TAL_PR_WARN("Test: Failed to acquire LCD buffer, frame dropped");
        return;
    }

    /* Step 3: Copy & byte swap (CPU little-endian → SPI display big-endian) */
    start = tal_system_get_tick_count();
    uint16_t *src_buf = (uint16_t *)s_decode_buf;
    uint16_t *dst_buf = (uint16_t *)lcd_buffer->frame.frame;
    uint32_t pixel_count = TUYA_LCD_WIDTH * TUYA_LCD_HEIGHT;
    for (uint32_t k = 0; k < pixel_count; k++) {
        dst_buf[k] = ((src_buf[k] & 0xff00) >> 8) | ((src_buf[k] & 0x00ff) << 8);
    }
    end = tal_system_get_tick_count();
    TAL_UVC_LOG("Test: Copy & byte swap: %u ticks", (uint32_t)(end - start));

    /* Step 4: Flush to LCD */ // move to lcd pbuf process
    // lcd_buffer->frame.x_start = 0;
    // lcd_buffer->frame.y_start = 0;
    // lcd_buffer->frame.width = TUYA_LCD_WIDTH;
    // lcd_buffer->frame.height = TUYA_LCD_HEIGHT;
    // lcd_buffer->frame.len = TUYA_LCD_WIDTH * TUYA_LCD_HEIGHT * 2;  // RGB565
    // lcd_buffer->frame.pdata = (uint8_t *)lcd_buffer;
    // lcd_buffer->frame.free_cb = __tal_uvc_test_lcd_flush_callback;

    TAL_UVC_LOG("Test: Flushing to LCD (async, %dx%d)...", TUYA_LCD_WIDTH, TUYA_LCD_HEIGHT);
    tuya_ai_display_flush(&lcd_buffer->frame);
}

/**
 * @brief UVC test function for programmatic testing (without CLI)
 * 
 * @param[in] width MJPEG width (e.g., 320)
 * @param[in] height MJPEG height (e.g., 240)
 * @param[in] auto_start If TRUE, automatically start after init
 * 
 * @return OPERATE_RET
 *   - OPRT_OK: Success
 *   - Others: Failed
 * 
 * @note This function wraps tal_uvc_cli_cmd for direct code invocation
 * 
 * @example
 *   // Initialize and start UVC test
 *   uvc_test(320, 240, TRUE);
 *   
 *   // Later, to stop:
 *   uvc_test_stop();
 */
OPERATE_RET uvc_test(uint16_t width, uint16_t height, BOOL_T auto_start)
{
    char buffer[128] = {0};
    char width_str[16] = {0};
    char height_str[16] = {0};
    
    // Convert width and height to strings
    snprintf(width_str, sizeof(width_str), "%u", width);
    snprintf(height_str, sizeof(height_str), "%u", height);
    
    // Step 1: Initialize UVC
    char *init_argv[] = {"uvc", "init", width_str, height_str};
    tal_uvc_cli_cmd(buffer, sizeof(buffer), 4, init_argv);
    
    // Check if init was successful
    if (strstr(buffer, "failed") != NULL || strstr(buffer, "Invalid") != NULL) {
        TAL_PR_ERR("UVC test init failed: %s", buffer);
        return OPRT_COM_ERROR;
    }
    
    TAL_PR_NOTICE("UVC test init OK: %s", buffer);
    
    // Step 2: Start UVC if auto_start is enabled
    if (auto_start) {
        tal_system_sleep(1000);  // Small delay between init and start
        char *start_argv[] = {"uvc", "start"};
        memset(buffer, 0, sizeof(buffer));
        tal_uvc_cli_cmd(buffer, sizeof(buffer), 2, start_argv);
        
        if (strstr(buffer, "failed") != NULL) {
            TAL_PR_ERR("UVC test start failed: %s", buffer);
            return OPRT_COM_ERROR;
        }
        
        TAL_PR_NOTICE("UVC test start OK: %s", buffer);
    }
    
    return OPRT_OK;
}

/**
 * @brief Stop UVC test
 * 
 * @return OPERATE_RET
 */
OPERATE_RET uvc_test_stop(void)
{
    char buffer[128] = {0};
    char *stop_argv[] = {"uvc", "stop"};
    
    tal_uvc_cli_cmd(buffer, sizeof(buffer), 2, stop_argv);
    
    if (strstr(buffer, "failed") != NULL) {
        TAL_PR_ERR("UVC test stop failed: %s", buffer);
        return OPRT_COM_ERROR;
    }
    
    TAL_PR_NOTICE("UVC test stop OK: %s", buffer);
    return OPRT_OK;
}

/**
 * @brief Deinitialize UVC test
 * 
 * @return OPERATE_RET
 */
OPERATE_RET uvc_test_deinit(void)
{
    char buffer[128] = {0};
    char *deinit_argv[] = {"uvc", "deinit"};
    
    tal_uvc_cli_cmd(buffer, sizeof(buffer), 2, deinit_argv);
    
    if (strstr(buffer, "failed") != NULL) {
        TAL_PR_ERR("UVC test deinit failed: %s", buffer);
        return OPRT_COM_ERROR;
    }
    
    TAL_PR_NOTICE("UVC test deinit OK: %s", buffer);
    return OPRT_OK;
}

/**
 * @brief DMA2D IRQ callback for hardware decode and DMA2D test - signals completion
 */
static VOID_T __tal_hw_decode_dma2d_test_irq_cb(TUYA_DMA2D_IRQ_E type, VOID_T *args)
{
    SEM_HANDLE sem = (SEM_HANDLE)args;
    if (sem && type == TUYA_DMA2D_TRANS_COMPLETE_ISR) {
        tal_semaphore_post(sem);
    }
}

/**
 * @brief CLI command for hardware decode and DMA2D test operations
 * 
 * @param[out] pcWriteBuffer Buffer for response message
 * @param[in] xWriteBufferLen Buffer length
 * @param[in] argc Argument count
 * @param[in] argv Argument array
 * 
 * @note Usage:
 *   hw_test mjpeg [width] [height] - Test JPEG hardware decode (optional dimensions)
 *   hw_test rgb                    - Test DMA2D RGB2RGB convert with embedded RGB565 data
 *   hw_test red                    - Fill LCD with red color
 *   hw_test green                  - Fill LCD with green color
 *   hw_test blue                   - Fill LCD with blue color
 * 
 * @example
 *   hw_test mjpeg           - Decode to default LCD size (320x172)
 *   hw_test mjpeg 640 480   - Decode to 640x480
 *   hw_test rgb             - DMA2D RGB565 convert
 */
void tal_hw_decode_dma2d_test_cli_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    char *msg = NULL;
    SYS_TICK_T start, end;
    
    // External test data arrays (MJPEG and RGB565)
    extern const unsigned char uvc_mjpg_jpg[];
    extern unsigned int uvc_mjpg_jpg_len;
    extern const uint16_t uvc_rgb565_raw[];
    extern const unsigned int uvc_rgb565_raw_len;
    extern const unsigned int uvc_rgb565_raw_width;
    extern const unsigned int uvc_rgb565_raw_height;
    
    // Allocate LCD buffer if not already allocated
    static UINT8_T *s_test_lcd_buf = NULL;
    static SEM_HANDLE s_dma2d_sem = NULL;
    OPERATE_RET ret = OPRT_OK;
    
    if (argc < 2) {
        msg = "Usage: hw_test [mjpeg|rgb|red|green|blue]\r\n"
              "  mjpeg                - JPEG hardware decode (320x240)\r\n"
              "  rgb                  - DMA2D RGB2RGB convert\r\n"
              "  red/green/blue       - Color fill test\r\n";
        goto exit;
    }

        
    // Parse optional width and height from command line
    // Usage: hw_test mjpeg [width] [height]
    uint16_t decode_width = 320;
    uint16_t decode_height = 240;
    
    if (!s_test_lcd_buf) {
        s_test_lcd_buf = tal_psram_malloc(decode_width * decode_height * 2);
        if (!s_test_lcd_buf) {
            TAL_PR_ERR("Failed to allocate LCD buffer");
            msg = "Failed to allocate LCD buffer\r\n";
            goto exit;
        }
        TAL_PR_DEBUG("HW Test LCD buffer allocated: %p (%dx%d)", 
                     s_test_lcd_buf, decode_width, decode_height);

        tuya_ai_display_pause();
        TAL_PR_DEBUG("HW Test: Display paused");
    }
    
    // MJPEG hardware decode test
    if (strcmp(argv[1], "mjpeg") == 0) {
        TAL_PR_NOTICE("=== HW Test: MJPEG Hardware Decode ===");

        bk_jpeg_hw_decode_to_mem_init();
        
        TAL_PR_DEBUG("MJPEG data: %p, size: %u bytes", uvc_mjpg_jpg, uvc_mjpg_jpg_len);
        
        // Critical issue: JPEG decoder DMA cannot directly access Flash (CODE segment)
        // Reason:
        // 1. uvc_mjpg_jpg uses const, stored in Flash .rodata segment (saves RAM)
        // 2. Flash address range usually at 0x02xxxxxx, DMA controller can only access RAM/PSRAM addresses
        // 3. CPU can read Flash through cache, but DMA direct access will fail
        // Solution: Copy JPEG data to aligned buffer in PSRAM
        uint8_t *jpeg_buf_aligned = tal_psram_malloc(uvc_mjpg_jpg_len + 4);
        if (!jpeg_buf_aligned) {
            TAL_PR_ERR("Failed to allocate aligned JPEG buffer");
            msg = "Failed to allocate aligned JPEG buffer\r\n";
            goto exit;
        }
        
        // Ensure 4-byte alignment (DMA usually requires aligned access)
        uint8_t *jpeg_buf = (uint8_t *)(((uintptr_t)jpeg_buf_aligned + 3) & ~3);
        
        SYS_TICK_T copy_start = tal_system_get_tick_count();
        memcpy(jpeg_buf, uvc_mjpg_jpg, uvc_mjpg_jpg_len);
        SYS_TICK_T copy_end = tal_system_get_tick_count();
        TAL_PR_NOTICE("JPEG copy: %d bytes in %d ticks", uvc_mjpg_jpg_len, copy_end - copy_start);
        
        // Perform JPEG hardware decode
        start = tal_system_get_tick_count();
        int ret = bk_jpeg_hw_decode_to_mem(
            jpeg_buf,  // JPEG source data
            s_test_lcd_buf,            // RGB565 destination buffer
            uvc_mjpg_jpg_len,          // JPEG data size
            decode_width,              // Target width (from command line or default)
            decode_height              // Target height (from command line or default)
        );
        end = tal_system_get_tick_count();
        
        if (ret != 0) {
            TAL_PR_ERR("JPEG hardware decode failed: %d", ret);
            msg = "MJPEG decode failed\r\n";
            goto exit;
        }
        
        TAL_PR_NOTICE("JPEG decode SUCCESS: %u ticks (%dx%d)", 
                      (uint32_t)(end - start), decode_width, decode_height);
        
        // Byte swap (CPU little-endian → SPI display big-endian)
        start = tal_system_get_tick_count();
        uint16_t *buf16 = (uint16_t *)s_test_lcd_buf;
        uint32_t pixel_count = decode_width * decode_height;
        for (uint32_t k = 0; k < pixel_count; k++) {
            buf16[k] = ((buf16[k] & 0xff00) >> 8) | ((buf16[k] & 0x00ff) << 8);
        }
        end = tal_system_get_tick_count();
        
        TAL_PR_NOTICE("Byte swap complete: %u ticks, %u pixels", 
                      (uint32_t)(end - start), pixel_count);
        
        msg = "MJPEG hardware decode test completed\r\n";

        tal_psram_free(jpeg_buf_aligned);
        bk_jpeg_hw_decode_to_mem_deinit();
    }
    // RGB565 DMA2D convert test (RGB2RGB)
    else if (strcmp(argv[1], "rgb") == 0) {
        TAL_PR_NOTICE("=== HW Test: RGB565 DMA2D Convert (RGB2RGB) ===");
        
        // Initialize DMA2D (only once)
        if (!s_dma2d_sem) {
            OPERATE_RET ret = tal_semaphore_create_init(&s_dma2d_sem, 0, 1);
            if (ret != OPRT_OK) {
                TAL_PR_ERR("Failed to create DMA2D semaphore: %d", ret);
                msg = "Failed to create DMA2D semaphore\r\n";
                goto exit;
            }
        }
            
        TUYA_DMA2D_BASE_CFG_T dma2d_cfg = {
            .cb = __tal_hw_decode_dma2d_test_irq_cb,
            .arg = s_dma2d_sem,
        };
        
        ret = tkl_dma2d_init(&dma2d_cfg);
        if (ret != OPRT_OK) {
            TAL_PR_ERR("DMA2D init failed: %d", ret);
            msg = "DMA2D init failed\r\n";
            goto exit;
        }

        TAL_PR_DEBUG("RGB565 data: %p, size: %u pixels (%ux%u)", 
                     uvc_rgb565_raw, uvc_rgb565_raw_len, 
                     uvc_rgb565_raw_width, uvc_rgb565_raw_height);
        
        // Setup input frame (RGB565 source)
        TKL_DMA2D_FRAME_INFO_T in_frame = {0};
        in_frame.type = TUYA_FRAME_FMT_RGB565;
        in_frame.width = uvc_rgb565_raw_width;
        in_frame.height = uvc_rgb565_raw_height;
        in_frame.axis.x_axis = 0;
        in_frame.axis.y_axis = 0;
        in_frame.width_cp = 0;
        in_frame.height_cp = 0;
        in_frame.pbuf = (CHAR_T *)uvc_rgb565_raw;
        
        // Setup output frame (RGB565 destination)
        TKL_DMA2D_FRAME_INFO_T out_frame = {0};
        out_frame.type = TUYA_FRAME_FMT_RGB565;
        out_frame.width = TUYA_LCD_WIDTH;
        out_frame.height = TUYA_LCD_HEIGHT;
        out_frame.axis.x_axis = 0;
        out_frame.axis.y_axis = 0;
        out_frame.width_cp = 0;
        out_frame.height_cp = 0;
        out_frame.pbuf = (CHAR_T *)s_test_lcd_buf;
        
        // DMA2D convert (RGB565 -> RGB565)
        start = tal_system_get_tick_count();
        ret = tkl_dma2d_convert(&in_frame, &out_frame);
        if (ret != OPRT_OK) {
            TAL_PR_ERR("DMA2D convert failed: %d", ret);
            msg = "DMA2D convert failed\r\n";
            goto exit;
        }
        
        // Wait for DMA2D completion (semaphore released in callback)
        tal_semaphore_wait_forever(s_dma2d_sem);
        end = tal_system_get_tick_count();
        
        TAL_PR_NOTICE("DMA2D convert complete: %u ticks (%ux%u -> %ux%u)", 
                      (uint32_t)(end - start),
                      uvc_rgb565_raw_width, uvc_rgb565_raw_height,
                      TUYA_LCD_WIDTH, TUYA_LCD_HEIGHT);
        
        msg = "RGB565 DMA2D test completed\r\n";
        tkl_dma2d_deinit();
    }
    // Color fill tests
    else if (strcmp(argv[1], "red") == 0 || 
             strcmp(argv[1], "green") == 0 || 
             strcmp(argv[1], "blue") == 0) {
        
        // RGB565 color definition (adapted for CPU little-endian + SPI + screen big-endian)
        uint16_t color = 0x00F8;  // Default red
        const char *color_name = "RED";
        
        if (strcmp(argv[1], "red") == 0) {
            color = 0x00F8;
            color_name = "RED";
        } else if (strcmp(argv[1], "green") == 0) {
            color = 0xE007;
            color_name = "GREEN";
        } else if (strcmp(argv[1], "blue") == 0) {
            color = 0x1F00;
            color_name = "BLUE";
        }
        
        TAL_PR_NOTICE("=== HW Test: Color Fill (%s) ===", color_name);
        TAL_PR_DEBUG("Fill color: %s (color=0x%04X)", color_name, color);
        
        // Fill buffer with color
        uint16_t *buf16 = (uint16_t *)s_test_lcd_buf;
        for (int k = 0; k < TUYA_LCD_WIDTH * TUYA_LCD_HEIGHT; k++) {
            buf16[k] = color;
        }
        
        snprintf(pcWriteBuffer, xWriteBufferLen, "Color fill test (%s) completed\r\n", color_name);
        msg = NULL;  // Already set in pcWriteBuffer
    }
    else {
        msg = "Unknown test. Usage: hw_test [mjpeg|rgb|red|green|blue]\r\n";
        goto exit;
    }
    
    // Display to LCD (common for all tests)
    static ty_frame_buffer_t test_frame = {0};
    test_frame.type = TYPE_PSRAM;
    test_frame.fmt = TY_PIXEL_FMT_RGB565;
    test_frame.frame = s_test_lcd_buf;
    test_frame.x_start = 0;
    test_frame.y_start = 0;
    test_frame.width = TUYA_LCD_WIDTH;
    test_frame.height = TUYA_LCD_HEIGHT;
    test_frame.len = TUYA_LCD_WIDTH * TUYA_LCD_HEIGHT * 2;
    test_frame.free_cb = NULL;  // Static buffer, no callback needed
    test_frame.pdata = NULL;
    
    TAL_PR_DEBUG("Flushing to LCD (%dx%d)...", TUYA_LCD_WIDTH, TUYA_LCD_HEIGHT);
    tuya_ai_display_flush(&test_frame);
    TAL_PR_NOTICE("Hardware test completed successfully");
    
exit:
    if (msg && pcWriteBuffer) {
        snprintf(pcWriteBuffer, xWriteBufferLen, "%s", msg);
    }
}

/**
 * @brief CLI command for UVC test operations
 * 
 * @param[out] pcWriteBuffer Buffer for response message
 * @param[in] xWriteBufferLen Buffer length
 * @param[in] argc Argument count
 * @param[in] argv Argument array
 * 
 * @note Usage:
 *   uvc init <width> <height> - Initialize UVC (e.g., uvc init 320 240)
 *   uvc start                 - Start reading frames and display to LCD
 *   uvc stop                  - Stop reading frames
 *   uvc deinit                - Deinit UVC and cleanup resources
 */
void tal_uvc_cli_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    static TAL_UVC_HANDLE_T s_test_handle = NULL;
    OPERATE_RET ret = OPRT_OK;
    char *msg = NULL;
    
    // Forward declaration for display control
    extern void tuya_ai_display_pause(void);
    extern void tuya_ai_display_resume(void);
    
    if (argc < 2) {
        msg = "Usage: uvc [init <width> <height>|start|stop|deinit]\r\n";
        goto exit;
    }
    
    if (strcmp(argv[1], "init") == 0) {
        TAL_PR_NOTICE("=== UVC Test: Init ===");
        
        if (s_test_handle) {
            TAL_PR_WARN("UVC already initialized, skipping");
            msg = "UVC already initialized\r\n";
            goto exit;
        }
        
        // Parse MJPEG width and height from command line
        // Default: 320x240
        uint16_t mjpeg_width = 320;
        uint16_t mjpeg_height = 240;

        uint16_t lcd_width = TUYA_LCD_WIDTH;
        uint16_t lcd_height = TUYA_LCD_HEIGHT;
        
        if (argc >= 4) {
            mjpeg_width = (uint16_t)atoi(argv[2]);
            mjpeg_height = (uint16_t)atoi(argv[3]);
            
            // Validate dimensions
            if (mjpeg_width == 0 || mjpeg_height == 0 || 
                mjpeg_width > 1920 || mjpeg_height > 1080) {
                TAL_PR_ERR("Invalid dimensions: %dx%d", mjpeg_width, mjpeg_height);
                msg = "Invalid dimensions (valid range: 1-1920 x 1-1080)\r\n";
                goto exit;
            }
        }
        
        TAL_UVC_CFG_T cfg = {
            .width = mjpeg_width,      // MJPEG width from command line
            .height = mjpeg_height,    // MJPEG height from command line
            .output_mode = TUYA_CAMERA_OUTPUT_JPEG,  /* JPEG — decoded in test callback */
            .fps = FPS15,
            .power_pin = TUYA_GPIO_NUM_6,
            .active_level = TUYA_GPIO_LEVEL_LOW,
            .frame_cb = __tal_uvc_test_frame_callback,
            .args = NULL,
        };
        
        TAL_PR_DEBUG("Test: MJPEG size=%dx%d, LCD size=%dx%d", 
                     mjpeg_width, mjpeg_height, TUYA_LCD_WIDTH, TUYA_LCD_HEIGHT);
        TAL_PR_DEBUG("Test: Initializing LCD pbuf pool (%dx%d, 3 buffers)...", 
                     mjpeg_width, mjpeg_height);
        
        // Initialize LCD buffer pool (3 buffers) with MJPEG size
        ret = tuya_lcd_pbuf_init(lcd_width, lcd_height, 3);
        if (ret != OPRT_OK) {
            TAL_PR_ERR("Failed to init LCD pbuf pool: %d", ret);
            msg = "Failed to init LCD buffer pool\r\n";
            goto exit;
        }
        TAL_PR_DEBUG("LCD pbuf pool initialized");
        
        ret = tal_uvc_init(&s_test_handle, &cfg);
        if (ret != OPRT_OK) {
            TAL_PR_ERR("UVC init failed: %d", ret);
            tuya_lcd_pbuf_deinit();  // Cleanup on error
            msg = "UVC init failed\r\n";
            goto exit;
        }
        
        TAL_PR_NOTICE("UVC Test: Initialized (MJPEG %dx%d@15fps, LCD %dx%d, RGB565)", 
                      mjpeg_width, mjpeg_height, TUYA_LCD_WIDTH, TUYA_LCD_HEIGHT);
        msg = "UVC initialized successfully\r\n";
    }
    else if (strcmp(argv[1], "start") == 0) {
        TAL_PR_NOTICE("=== UVC Test: Start ===");
        
        if (!s_test_handle) {
            TAL_PR_ERR("UVC not initialized, please run init first");
            msg = "UVC not initialized, please run 'uvc init <width> <height>' first\r\n";
            goto exit;
        }
        
        // Pause display before starting camera
        tuya_ai_display_pause();
        
        // Initialize hardware JPEG decoder before starting
        bk_jpeg_hw_decode_to_mem_init();

        ret = tal_uvc_start(s_test_handle);
        if (ret != OPRT_OK) {
            TAL_PR_ERR("UVC start failed: %d", ret);
            tuya_ai_display_resume();  // Resume on error
            msg = "UVC start failed\r\n";
            goto exit;
        }
        
        TAL_PR_NOTICE("UVC Test: Started, receiving frames and displaying to LCD...");
        msg = "UVC started successfully\r\n";
    }
    else if (strcmp(argv[1], "stop") == 0) {
        TAL_PR_NOTICE("=== UVC Test: Stop ===");
        
        if (!s_test_handle) {
            TAL_PR_WARN("UVC not started");
            msg = "UVC not started\r\n";
            goto exit;
        }
        
        ret = tal_uvc_stop(s_test_handle);
        if (ret != OPRT_OK) {
            TAL_PR_ERR("UVC stop failed: %d", ret);
            msg = "UVC stop failed\r\n";
            goto exit;
        }

        bk_jpeg_hw_decode_to_mem_deinit();
        
        // Resume display after stopping camera
        tuya_ai_display_resume();
        
        TAL_PR_NOTICE("UVC Test: Stopped");
        msg = "UVC stopped successfully\r\n";
    }
    else if (strcmp(argv[1], "deinit") == 0) {
        TAL_PR_NOTICE("=== UVC Test: Deinit ===");
        
        if (!s_test_handle) {
            TAL_PR_WARN("UVC not initialized");
            msg = "UVC not initialized\r\n";
            goto exit;
        }
        
        ret = tal_uvc_deinit(s_test_handle);
        if (ret != OPRT_OK) {
            TAL_PR_ERR("UVC deinit failed: %d", ret);
            msg = "UVC deinit failed\r\n";
            goto exit;
        }
        s_test_handle = NULL;
        TAL_PR_DEBUG("UVC deinitialized");
        
        // Cleanup LCD buffer pool
        tuya_lcd_pbuf_deinit();
        TAL_PR_DEBUG("LCD pbuf pool deinitialized");
        
        TAL_PR_NOTICE("UVC Test: Deinitialized and cleaned up");
        msg = "UVC deinitialized successfully\r\n";
    }
    else {
        msg = "Unknown command. Usage: uvc [init <width> <height>|start|stop|deinit]\r\n";
    }
    
exit:
    if (msg && pcWriteBuffer) {
        snprintf(pcWriteBuffer, xWriteBufferLen, "%s", msg);
    }
}

#endif  // TAL_UVC_TEST && ENABLE_TUYA_UI


