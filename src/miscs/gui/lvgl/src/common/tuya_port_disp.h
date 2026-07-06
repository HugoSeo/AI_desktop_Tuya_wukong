
#ifndef TKL_LVGL_ADAPTER_H
#define TKL_LVGL_ADAPTER_H

#include "tuya_cloud_types.h"
#include "tkl_display.h"
#include "tuya_app_gui_core_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

/*********************
 *      DEFINES
 *********************/
 #define EVENT_TAKE_PHOTO_TRIGGER        "take.photo"            // 照相事件触发准备好
 
/** DMA2D_Output_Color_Mode , used for fillfmt / memcpy dst fmt/ blend output fmt*/
typedef enum {
	COLOR_OUTPUT_RGB565 = 0,
	COLOR_OUTPUT_RGB888,
	COLOR_OUTPUT_ARGB888,
} ty_out_color_mode_t;

/** DMA2D_Input_Color_Mode, the members in order */
typedef enum {
	COLOR_INPUT_RGB565 = 0,
	COLOR_INPUT_RGB888,
	COLOR_INPUT_ARGB888,
} ty_input_color_mode_t;

typedef enum {
	PIXEL_TWO_BYTES   = 2,
	PIXEL_THREE_BYTES = 3,
	PIXEL_FOUR_BYTES  = 4,
}ty_color_bytes_t;

typedef enum {
	RB_REGULAR = 0x0,    /**< Select regular mode (RGB or ARGB) */
	RB_SWAP,             /**< Select swap mode (BGR or ABGR) * = 0*/
}ty_red_blue_swap_t;

typedef enum {
	FMT_UNKNOW,         /**< unknow image format */
	FMT_RGB565,
	FMT_RGB666,
	FMT_RGB888,
	FMT_ARGB888,
} ty_pixel_format_t;

typedef enum {
	FRAME_REQ_TYPE_UNKNOW,
	FRAME_REQ_TYPE_BOOTON_PAGE,             //启动页面请求刷新帧类型
	FRAME_REQ_TYPE_IMG_DIR_FLUSH,           //直刷图模式请求刷新帧类型
	FRAME_REQ_TYPE_LVGL                     //lvgl请求刷新帧类型
} ty_frame_req_type_t;

/**********************
 *      TYPEDEFS
 **********************/
typedef struct{
    uint16_t m_x;
    uint16_t m_y;
    uint16_t m_state;              //0 release 1 press
    uint16_t m_need_continue;
}tkl_tp_point_infor_t;

typedef struct
{
	//ty_dma2d_mode_t    mode; 
	void * input_addr;               /**< The image memcpy or pixel convert src addr */
	void * output_addr;              /**< The mage memcpy or pixel convert dst addr */
	uint16_t src_frame_width;        /**< memcpy or pfc src image width */
	uint16_t src_frame_height;       /**< imemcpy or pfc src image height  */
	uint16_t src_frame_xpos;         /**< src img start copy/pfc x pos*/
	uint16_t src_frame_ypos;         /**< src img start copy/pfc y pos*/

	uint16_t dst_frame_width;         /**< memcpy to dst image, the dst image width */
	uint16_t dst_frame_height;        /**< memcpy to dst image, the dst image height   */
	uint16_t dst_frame_xpos;          /**< dma2d fill x pos based on frame_xsize */
	uint16_t dst_frame_ypos;          /**< dma2d fill y pos based on frame_ysize */
	uint16_t dma2d_width;              /**< dma2d memcpy or pfc width */
	uint16_t dma2d_height;               /**< dma2d memcpy or pfc height */

	ty_input_color_mode_t input_color_mode;  /**< The pixel convert src color mode */
	ty_out_color_mode_t output_color_mode;   /**< The pixel convert dst color mode */
	ty_color_bytes_t src_pixel_byte;
	ty_color_bytes_t dst_pixel_byte;
	uint8_t input_alpha;                /**< src data alpha, depend on alpha_mode */
	uint8_t output_alpha;                /**< dst data alpha,depend on alpha_mode */
	ty_red_blue_swap_t input_red_blue_swap;        /**< src img red blue swap, select DMA2D_RB_SWAP or  DMA2D_RB_REGULAR */
	ty_red_blue_swap_t output_red_blue_swap;        /**< src img red blue swap, select DMA2D_RB_SWAP or  DMA2D_RB_REGULAR */
}tkl_dma2d_memcpy_pfc_t;

typedef void (*tkl_lvgl_disp_flush_async_cb_t)(void *drv);

/**********************
 * GLOBAL PROTOTYPES
 **********************/
VOID tkl_lvgl_dma2d_init(VOID);

VOID tkl_lvgl_dma2d_deinit(VOID);

VOID tkl_lvgl_dma2d_memcpy(tkl_dma2d_memcpy_pfc_t *pixel_info);

BOOL_T tkl_lvgl_dma2d_is_busy(void);

VOID tkl_lvgl_dma2d_wait_transfer_finish(void);

OPERATE_RET tkl_lvgl_display_frame_init(UINT16_T lcd_width, UINT16_T lcd_height);

VOID tkl_lvgl_display_frame_deinit(VOID);

/**
* @brief 屏幕数据刷新
*
* @param[in] x:         数据在屏幕中的横向坐标(仅做局部刷新有效,整屏数据输入时为0)
* @param[in] y:         数据在屏幕中的纵向坐标(仅做局部刷新有效,整屏数据输入时为0)
* @param[in] width:     数据在屏幕中的宽度(仅做局部刷新有效,整屏数据输入时为0)
* @param[in] height:    数据在屏幕中的高度(仅做局部刷新有效,整屏数据输入时为0)
* @param[in] disp_buf:  数据
* @param[in] req_type:  应用类型
*
* @note This API is used for display flush.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET tkl_lvgl_display_frame_request(UINT32_T x, UINT32_T y, UINT32_T width, UINT32_T height, UCHAR_T *disp_buf, ty_frame_req_type_t req_type);

TKL_DISP_ROTATION_E tkl_lvgl_display_rotation_get(UCHAR_T rotation);

#ifdef TUYA_APP_DRIVERS_TP
OPERATE_RET tkl_lvgl_tp_handle_register(VOID *tp_driver, VOID *tp_cfg);
#endif

VOID tkl_lvgl_tp_close(VOID);

OPERATE_RET tkl_lvgl_tp_open(UINT32_T hor_size, UINT32_T ver_size);

OPERATE_RET tkl_lvgl_tp_read(tkl_tp_point_infor_t *tp_point);

#ifdef TUYA_MULTI_TYPES_LCD
OPERATE_RET tkl_lvgl_display_handle_register(VOID *hdl, VOID *hdl_slave, TUYA_SCREEN_EXPANSION_TYPE  exp_type);
BOOL_T tkl_lvgl_display_handle(VOID **hdl, VOID **hdl_slave, TUYA_SCREEN_EXPANSION_TYPE *out_exp_type);
OPERATE_RET tkl_lvgl_display_resolution(UINT16_T *lcd_hor_res, UINT16_T *lcd_ver_res);
VOID tkl_lvgl_display_offset_set(UINT32_T x_off, UINT32_T y_off);
#endif

VOID tuya_lvgl_te_enable(VOID);
VOID tuya_lvgl_te_irq_handle(VOID);

//lvgl刷屏完成异步回调注册
VOID tkl_lvgl_disp_flush_async_register(tkl_lvgl_disp_flush_async_cb_t cb, void *drv);

//启动页面刷屏完成异步回调注册
VOID tkl_PwrOnPage_disp_flush_async_register(tkl_lvgl_disp_flush_async_cb_t cb, void *drv);

VOID tkl_lvgl_display_pixel_rgb_convert(UCHAR_T * src, INT32_T src_w, INT32_T src_h);

VOID* tkl_system_sram_malloc(CONST SIZE_T size);
VOID *tkl_system_sram_calloc(size_t nitems, size_t size);
VOID tkl_system_sram_free(VOID* ptr);

#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
//#pragma message("######################## system has psram ########################")
#else
#pragma message("######################## system no psram ########################")

VOID_T tkl_system_psram_malloc_force_set(BOOL_T enable);

VOID_T* tkl_system_psram_malloc(CONST SIZE_T size);

VOID_T tkl_system_psram_free(VOID_T* ptr);

VOID_T *tkl_system_psram_calloc(size_t nitems, size_t size);

VOID_T *tkl_system_psram_realloc(VOID_T* ptr, size_t size);

INT_T tkl_system_psram_get_free_heap_size(VOID_T);

size_t rtos_get_psram_total_heap_size(void);

size_t rtos_get_psram_free_heap_size(void);

size_t rtos_get_psram_minimum_free_heap_size(void);

size_t rtos_get_total_heap_size(void);

size_t rtos_get_free_heap_size(void);

size_t rtos_get_minimum_free_heap_size(void);

#endif

VOID tkl_lvgl_pause(void);
VOID tkl_lvgl_resume(void);

#if defined(TUYA_AI_TOY_DEMO) && TUYA_AI_TOY_DEMO == 1
VOID tuya_ai_display_pause(VOID);
VOID tuya_ai_display_resume(VOID);
VOID tuya_ai_display_flush(VOID *data);
#endif

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*TKL_LVGL_ADAPTER_H*/
