/**
 * @file tal_uvc.h
 * @brief TAL (Tuya Abstraction Layer) UVC Camera Driver Interface
 *
 * Provides a unified abstraction for UVC (USB Video Class) camera operations:
 * - Initialization and configuration
 * - Frame capture via callback mechanism
 * - Format / output-mode selection aligned with TAL_CAMERA layer
 *
 * Note: Hardware JPEG decoding (MJPEG → RGB565) is handled by the
 * lcd_service layer, NOT inside tal_uvc.  The UVC driver delivers raw
 * frames (JPEG / H264 / YUV422) directly to the registered callback.
 *
 * @author linch
 * @date 2025-02-09
 * @version 1.0.0
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All rights reserved.
 */

#ifndef __TAL_UVC_H__
#define __TAL_UVC_H__

#include "tuya_cloud_types.h"  /* TUYA_CAMERA_OUTPUT_MODE, TUYA_FRAME_FMT_E */

#ifdef __cplusplus
extern "C" {
#endif

// UVC test function switch (0: disable, 1: enable)
#ifndef TAL_UVC_TEST
#define TAL_UVC_TEST 0
#endif

typedef void * TAL_UVC_HANDLE_T;

typedef struct {
    UINT8_T            *frame;
    UINT32_T            length;
    TUYA_FRAME_FMT_E    fmt;
    UINT32_T            timestamp;
    UINT16_T            width;
    UINT16_T            height;
    UINT32_T            sequence;
} TAL_UVC_FRAME_T;

typedef void (*tal_uvc_frame_cb_t)(TAL_UVC_HANDLE_T handle, TAL_UVC_FRAME_T *frame, void *args);

typedef struct {
    UINT16_T                width;
    UINT16_T                height;
    TUYA_CAMERA_OUTPUT_MODE output_mode; /**< Camera output mode, aligned with TAL_CAMERA layer */
    UINT16_T                fps;
    TUYA_GPIO_NUM_E         power_pin;
    TUYA_GPIO_LEVEL_E       active_level;
    tal_uvc_frame_cb_t      frame_cb;    /**< Frame callback, overwritten by tal_camera_init() */
    void                   *args;
} TAL_UVC_CFG_T;


/**
 * @brief Initialize UVC camera
 * 
 * @param[out] handle UVC handle pointer (will be allocated and initialized)
 * @param[in]  cfg UVC configuration (width, height, format, fps, etc.)
 * 
 * @return OPERATE_RET
 *   - OPRT_OK: Success
 *   - OPRT_INVALID_PARM: Invalid parameters
 *   - OPRT_MALLOC_FAILED: Memory allocation failed
 *   - OPRT_COM_ERROR: Camera open failed
 */
OPERATE_RET tal_uvc_init(TAL_UVC_HANDLE_T *handle, TAL_UVC_CFG_T *cfg);
OPERATE_RET tal_uvc_start(TAL_UVC_HANDLE_T handle);
OPERATE_RET tal_uvc_stop(TAL_UVC_HANDLE_T handle);

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
OPERATE_RET tal_uvc_deinit(TAL_UVC_HANDLE_T handle);

/**
 * @brief Start reading UVC frames
 * 
 * @param[in] handle UVC handle
 * 
 * @return OPERATE_RET
 *   - OPRT_OK: Success
 *   - OPRT_INVALID_PARM: Invalid parameter
 *   - OPRT_COM_ERROR: Register callback failed
 * 
 * @note Frame callback should be set in TAL_UVC_CFG_T during initialization
 */
OPERATE_RET tal_uvc_read_start(TAL_UVC_HANDLE_T handle);

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
OPERATE_RET tal_uvc_read_stop(TAL_UVC_HANDLE_T handle);

#if defined(TAL_UVC_TEST) && TAL_UVC_TEST == 1
/**
 * @brief UVC CLI command for testing
 * 
 * @param[out] pcWriteBuffer Response buffer
 * @param[in] xWriteBufferLen Buffer length
 * @param[in] argc Argument count
 * @param[in] argv Argument array
 */
void tal_uvc_cli_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv);

/**
 * @brief Programmatic UVC test wrapper
 * 
 * @param[in] width MJPEG width
 * @param[in] height MJPEG height
 * @param[in] auto_start Auto start after init
 * @return OPERATE_RET
 */
OPERATE_RET uvc_test(uint16_t width, uint16_t height, BOOL_T auto_start);

/**
 * @brief Stop UVC test
 * @return OPERATE_RET
 */
OPERATE_RET uvc_test_stop(void);

/**
 * @brief Deinitialize UVC test
 * @return OPERATE_RET
 */
OPERATE_RET uvc_test_deinit(void);


/**
 * @brief CLI command for hardware decode and DMA2D test operations
 * 
 * @param[out] pcWriteBuffer Response buffer
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
 *   // From CLI:
 *   hw_test mjpeg           // Decode to default LCD size
 *   hw_test rgb
 *   hw_test red
 */
void tal_hw_decode_dma2d_test_cli_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv);
#endif  // TAL_UVC_TEST

#ifdef __cplusplus
}
#endif

#endif
