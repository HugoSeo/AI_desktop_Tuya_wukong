/**
 * @file lv_port_disp.c
 *
 */

/*Copy this file as "lv_port_disp.c" and set this value to "1" to enable content*/
#if 1

/*********************
 *      INCLUDES
 *********************/
#include "tuya_port_disp.h"
#include "tkl_display.h"
#include "tkl_memory.h"
#include "tkl_semaphore.h"
#include "tkl_system.h"
#include "tal_display_service.h"
#include "lv_port_disp.h"
#include <stdbool.h>
#include "lv_vendor.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void disp_init(void);

static void disp_deinit(void);

static void disp_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map);

/**********************
 *  STATIC VARIABLES
 **********************/
extern lv_vnd_config_t vendor_config;

static void *rotate_buffer = NULL;
static void *rotate_buffer_unalign = NULL;
static TKL_DISP_ROTATION_E disp_rotation = TKL_DISP_ROTATION_0;
static TKL_SEM_HANDLE lv_async_wait_sem = NULL;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

static void * rotate_buf_align_alloc(size_t size_bytes, bool use_sram)
{
    uint8_t *buf_u8= NULL;
    /*Allocate larger memory to be sure it can be aligned as needed*/
    size_bytes += LV_DRAW_BUF_ALIGN - 1;
    if (use_sram)
        buf_u8 = tkl_system_sram_malloc(size_bytes);
    else
        buf_u8 = tkl_system_psram_malloc(size_bytes);
    rotate_buffer_unalign = buf_u8;
    if (buf_u8) {
        buf_u8 += LV_DRAW_BUF_ALIGN - 1;
        buf_u8 = (uint8_t *)((uint32_t) buf_u8 & ~(LV_DRAW_BUF_ALIGN - 1));
        //bk_printf("%s-%d: original buf ptr '%p', aligned buf ptr '%p'\r\n", __func__, __LINE__, rotate_buffer_unalign, (void *)buf_u8);
    }
    return buf_u8;
}

static void rotate_buf_align_free(void *ptr)
{
    ((void *)ptr);
    if (rotate_buffer_unalign) {
        if (vendor_config.draw_buf_use_sram)
            tkl_system_sram_free(rotate_buffer_unalign);
        else
            tkl_system_psram_free(rotate_buffer_unalign);
    }
    rotate_buffer_unalign = NULL;
}

static void lv_disp_flush_async_ready_notify(void *drv)
{
    lv_display_flush_ready((lv_display_t *)drv);
    tkl_semaphore_post(lv_async_wait_sem);
}

static void lv_disp_wait_async_flush(lv_display_t * disp)
{
    if (disp->flushing) {
        tkl_semaphore_wait(lv_async_wait_sem, TKL_SEM_WAIT_FOREVER);    // 睡眠等待异步刷新结束,让出CPU
    }
}

void lv_port_disp_init(void)
{
    /*-------------------------
     * Initialize your display
     * -----------------------*/
    disp_init();

    /*------------------------------------
     * Create a display and set a flush_cb
     * -----------------------------------*/
    lv_display_t * disp = lv_display_create(vendor_config.lcd_hor_res, vendor_config.lcd_ver_res);
    lv_display_set_flush_cb(disp, disp_flush);

    #if 0
    /* Example 1
     * One buffer for partial rendering*/
    static lv_color_t buf_1_1[MY_DISP_HOR_RES * 10];                          /*A buffer for 10 rows*/
    lv_display_set_buffers(disp, buf_1_1, NULL, sizeof(buf_1_1), LV_DISPLAY_RENDER_MODE_PARTIAL);

    /* Example 2
     * Two buffers for partial rendering
     * In flush_cb DMA or similar hardware should be used to update the display in the background.*/
    static lv_color_t buf_2_1[MY_DISP_HOR_RES * 10];
    static lv_color_t buf_2_2[MY_DISP_HOR_RES * 10];
    lv_display_set_buffers(disp, buf_2_1, buf_2_2, sizeof(buf_2_1), LV_DISPLAY_RENDER_MODE_PARTIAL);

    /* Example 3
     * Two buffers screen sized buffer for double buffering.
     * Both LV_DISPLAY_RENDER_MODE_DIRECT and LV_DISPLAY_RENDER_MODE_FULL works, see their comments*/
    static lv_color_t buf_3_1[MY_DISP_HOR_RES * MY_DISP_VER_RES];
    static lv_color_t buf_3_2[MY_DISP_HOR_RES * MY_DISP_VER_RES];
    lv_display_set_buffers(disp, buf_3_1, buf_3_2, sizeof(buf_3_1), LV_DISPLAY_RENDER_MODE_DIRECT);
    #else
    #if LVGL_USE_PSRAM
    lv_display_set_buffers(disp, vendor_config.draw_buf_2_1, vendor_config.draw_buf_2_2, vendor_config.draw_pixel_size * (LV_COLOR_DEPTH/8), LV_DISPLAY_RENDER_MODE_DIRECT);
    #else
    lv_display_set_buffers(disp, vendor_config.draw_buf_2_1, vendor_config.draw_buf_2_2, vendor_config.draw_pixel_size * (LV_COLOR_DEPTH/8), LV_DISPLAY_RENDER_MODE_PARTIAL);
    #endif
    #endif

    LV_LOG_INFO("LVGL addr1:%x, addr2:%x, pixel size:%d, fb1:%x, fb2:%x\r\n", vendor_config.draw_buf_2_1, vendor_config.draw_buf_2_2,
                                            vendor_config.draw_pixel_size, vendor_config.frame_buf_1, vendor_config.frame_buf_2);

    disp_rotation = tkl_lvgl_display_rotation_get(vendor_config.rotation);
    if (disp_rotation != TKL_DISP_ROTATION_0) {
        if (disp_rotation == TKL_DISP_ROTATION_90) {
            lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);
        }else if (disp_rotation == TKL_DISP_ROTATION_180){
            lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_180);
        }else if(disp_rotation == TKL_DISP_ROTATION_270){
            lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270);
        }
        #if (LV_COLOR_DEPTH == 16)
            rotate_buffer = rotate_buf_align_alloc(vendor_config.draw_pixel_size * sizeof(lv_color16_t), vendor_config.draw_buf_use_sram);
        #elif (LV_COLOR_DEPTH == 24)
            rotate_buffer = rotate_buf_align_alloc(vendor_config.draw_pixel_size * sizeof(lv_color_t), vendor_config.draw_buf_use_sram);
        #elif (LV_COLOR_DEPTH == 32)
            rotate_buffer = rotate_buf_align_alloc(vendor_config.draw_pixel_size * sizeof(lv_color32_t), vendor_config.draw_buf_use_sram);
        #endif
        if (rotate_buffer == NULL) {
            LV_LOG_ERROR("lvgl rotate buffer malloc fail!\n");
        }
    }

    if (lv_vendor_display_frame_cnt() == 0) {   //没有帧缓存,如T2AI(仅支持spi单屏幕,内存不足情况下)渲染后直接flush,开启异步刷屏完成通知!
        if (OPRT_OK != tkl_semaphore_create_init(&lv_async_wait_sem, 0, 1)) {
            LV_LOG_ERROR("%s semaphore init failed\n", __func__);
        }
        tkl_lvgl_disp_flush_async_register(lv_disp_flush_async_ready_notify, (void *)disp);
        lv_display_set_flush_wait_cb(disp, lv_disp_wait_async_flush);            //lvgl异步刷新等待!
    }
}

void lv_port_disp_deinit(void)
{
    if(disp_rotation != TKL_DISP_ROTATION_0){
        if (rotate_buffer) {
            rotate_buf_align_free(rotate_buffer);
            rotate_buffer = NULL;
        }
    }
    lv_display_delete(lv_disp_get_default());
    disp_deinit();
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
static void lv_memcpy_one_line(void *dest_buf, const void *src_buf, uint32_t point_num)
{
    #if (LV_COLOR_DEPTH == 16)
        memcpy(dest_buf, src_buf, point_num * sizeof(lv_color16_t));
    #elif (LV_COLOR_DEPTH == 24)
        memcpy(dest_buf, src_buf, point_num * sizeof(lv_color_t));
    #elif (LV_COLOR_DEPTH == 32)
        memcpy(dest_buf, src_buf, point_num * sizeof(lv_color32_t));
    #endif
}

static void disp_memcpy(void *Psrc, uint32_t src_xsize, uint32_t src_ysize,
                                    void *Pdst, uint32_t dst_xsize, uint32_t dst_ysize,
                                    uint32_t dst_xpos, uint32_t dst_ypos)
{
    tkl_dma2d_memcpy_pfc_t dma2d_memcpy_pfc = {0};

    dma2d_memcpy_pfc.input_addr = (char *)Psrc;
    dma2d_memcpy_pfc.output_addr = (char *)Pdst;

    #if (LV_COLOR_DEPTH == 16)
    dma2d_memcpy_pfc.input_color_mode   = COLOR_INPUT_RGB565;
    dma2d_memcpy_pfc.src_pixel_byte     = PIXEL_TWO_BYTES;
    dma2d_memcpy_pfc.output_color_mode  = COLOR_OUTPUT_RGB565;
    dma2d_memcpy_pfc.dst_pixel_byte     = PIXEL_TWO_BYTES;
    #elif (LV_COLOR_DEPTH == 24)
    dma2d_memcpy_pfc.input_color_mode   = COLOR_INPUT_RGB888;
    dma2d_memcpy_pfc.src_pixel_byte     = PIXEL_THREE_BYTES;
    dma2d_memcpy_pfc.output_color_mode  = COLOR_OUTPUT_RGB888;
    dma2d_memcpy_pfc.dst_pixel_byte     = PIXEL_THREE_BYTES;
    #elif (LV_COLOR_DEPTH == 32)
    dma2d_memcpy_pfc.input_color_mode   = COLOR_INPUT_ARGB888;
    dma2d_memcpy_pfc.src_pixel_byte     = PIXEL_FOUR_BYTES;
    dma2d_memcpy_pfc.output_color_mode  = COLOR_OUTPUT_ARGB888;
    dma2d_memcpy_pfc.dst_pixel_byte     = PIXEL_FOUR_BYTES;
    #endif

    dma2d_memcpy_pfc.dma2d_width = src_xsize;
    dma2d_memcpy_pfc.dma2d_height = src_ysize;
    dma2d_memcpy_pfc.src_frame_width = src_xsize;
    dma2d_memcpy_pfc.src_frame_height = src_ysize;
    dma2d_memcpy_pfc.dst_frame_width = dst_xsize;
    dma2d_memcpy_pfc.dst_frame_height = dst_ysize;
    dma2d_memcpy_pfc.src_frame_xpos = 0;
    dma2d_memcpy_pfc.src_frame_ypos = 0;
    dma2d_memcpy_pfc.dst_frame_xpos = dst_xpos;
    dma2d_memcpy_pfc.dst_frame_ypos = dst_ypos;

    tkl_lvgl_dma2d_memcpy(&dma2d_memcpy_pfc);
}

void lv_dma2d_memcpy_buffer(void *Psrc, uint32_t src_xsize, uint32_t src_ysize, void *Pdst, uint32_t dst_xsize, uint32_t dst_ysize, uint32_t dst_xpos, uint32_t dst_ypos)
{
    tkl_lvgl_dma2d_wait_transfer_finish();
    disp_memcpy(Psrc, src_xsize, src_ysize, Pdst, dst_xsize, dst_ysize, dst_xpos, dst_ypos);
}

/*Initialize your display and the required peripherals.*/
static void disp_init(void)
{
    /*You code here*/
    if (lv_vendor_display_frame_cnt() == 2 || lv_vendor_draw_buffer_cnt() == 2) {
        tkl_lvgl_dma2d_init();
    }
    tkl_lvgl_display_frame_init(vendor_config.lcd_hor_res, vendor_config.lcd_ver_res);
}

static void disp_deinit(void)
{
    if (lv_vendor_display_frame_cnt() == 2 || lv_vendor_draw_buffer_cnt() == 2) {
        tkl_lvgl_dma2d_deinit();
    }
    tkl_lvgl_display_frame_deinit();
}

volatile bool disp_flush_enabled = true;

/* Enable updating the screen (the flushing process) when disp_flush() is called by LVGL
 */
void disp_enable_update(void)
{
    disp_flush_enabled = true;
}

/* Disable updating the screen (the flushing process) when disp_flush() is called by LVGL
 */
void disp_disable_update(void)
{
    disp_flush_enabled = false;
}

/*Flush the content of the internal buffer the specific area on the display.
 *`px_map` contains the rendered image as raw pixel map and it should be copied to `area` on the display.
 *You can use DMA or any hardware acceleration to do this operation in the background but
 *'lv_display_flush_ready()' has to be called when it's finished.*/
static void disp_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
    //RGB convert!
    tkl_lvgl_display_pixel_rgb_convert((UCHAR_T *)px_map, lv_area_get_width(area), lv_area_get_height(area));

    if (lv_vendor_display_frame_cnt() == 0) {                   //没有帧缓存,如T2AI(仅支持spi单屏幕,内存不足情况下)渲染后直接flush!
        TY_DISPLAY_HANDLE _disp_hdl = NULL, _disp_hdl_slave = NULL;
        TUYA_SCREEN_EXPANSION_TYPE exp_type = TUYA_SCREEN_EXPANSION_NONE;

        if (tkl_lvgl_display_handle((VOID **)&_disp_hdl, (VOID **)&_disp_hdl_slave, &exp_type) == TRUE) {
            if (_disp_hdl != NULL && _disp_hdl->type == DISPLAY_SPI) {
                if (disp_flush_enabled) {
                    tkl_lvgl_display_frame_request(area->x1, area->y1, lv_area_get_width(area), lv_area_get_height(area), (UCHAR_T *)px_map, FRAME_REQ_TYPE_LVGL);
                }
                /*没有帧缓存,屏驱又是将color_p做异步刷新的,不能立刻调用lv_disp_flush_ready(告诉lvgl刷新完成),
                  否则lvgl会马上继续使用draw buffer渲染,导致数据被篡改至花屏,只能等屏幕刷新完成后调用ty_frame_buffer_t->free_cb,
                  在其回调中执行lv_disp_flush_ready告诉lvgl刷新完成后才能继续渲染下一帧数据*/
                //lv_display_flush_ready(disp);
                return;
            }
            else {
                LV_LOG_ERROR("unsupport lcd type '%d', expand type '%d' ???\n", _disp_hdl->type, exp_type);
                return;
            }
        }
        else {
            LV_LOG_ERROR("disp handler invalid ???\n");
            return;
        }
    }

    if (disp_flush_enabled) {
    #if (!LVGL_USE_PSRAM)
        static uint8_t disp_buff_index = DISPLAY_BUFFER_DEF;
        #if (LV_COLOR_DEPTH == 16)
            static lv_color16_t *disp_buf = NULL;
            lv_color16_t *color_ptr = NULL;
            lv_color16_t *disp_buf1 = vendor_config.frame_buf_1;
            lv_color16_t *disp_buf2 = vendor_config.frame_buf_2;
        #elif (LV_COLOR_DEPTH == 24)
            static lv_color_t *disp_buf = NULL;
            lv_color_t *color_ptr = NULL;
            lv_color_t *disp_buf1 = vendor_config.frame_buf_1;
            lv_color_t *disp_buf2 = vendor_config.frame_buf_2;
        #elif (LV_COLOR_DEPTH == 32)
            static lv_color32_t *disp_buf = NULL;
            lv_color32_t *color_ptr = NULL;
            lv_color32_t *disp_buf1 = vendor_config.frame_buf_1;
            lv_color32_t *disp_buf2 = vendor_config.frame_buf_2;
        #endif
        lv_coord_t lv_hor = LV_HOR_RES;
        lv_coord_t lv_ver = LV_VER_RES;

        int y = 0;
        int offset = 0;
        if (disp_buf2 != NULL) {
            if(DISPLAY_BUFFER_1 == disp_buff_index) {
                tkl_lvgl_dma2d_wait_transfer_finish();
                disp_buf = disp_buf2;
            } else if (DISPLAY_BUFFER_2 == disp_buff_index) {
                tkl_lvgl_dma2d_wait_transfer_finish();
                disp_buf = disp_buf1;
            }
            else //first display
            {
                tkl_lvgl_dma2d_wait_transfer_finish();
                disp_buf = disp_buf1;
            }
        } else {
            disp_buf = disp_buf1;
        }

        lv_display_rotation_t rotation = lv_display_get_rotation(disp);
        lv_area_t rotated_area;
        rotated_area.x1 = area->x1;
        rotated_area.x2 = area->x2;
        rotated_area.y1 = area->y1;
        rotated_area.y2 = area->y2;
        if(rotation != LV_DISPLAY_ROTATION_0) {
            lv_color_format_t cf = lv_display_get_color_format(disp);
            /*Calculate the position of the rotated area*/
            lv_display_rotate_area(disp, &rotated_area);
            /*Calculate the source stride (bytes in a line) from the width of the area*/
            uint32_t src_stride = lv_draw_buf_width_to_stride(lv_area_get_width(area), cf);
            /*Calculate the stride of the destination (rotated) area too*/
            uint32_t dest_stride = lv_draw_buf_width_to_stride(lv_area_get_width(&rotated_area), cf);
            /*Have a buffer to store the rotated area and perform the rotation*/
            
            int32_t src_w = lv_area_get_width(area);
            int32_t src_h = lv_area_get_height(area);
            lv_draw_sw_rotate(px_map, rotate_buffer, src_w, src_h, src_stride, dest_stride, rotation, cf);
            /*Use the rotated area and rotated buffer from now on*/

            #if (LV_COLOR_DEPTH == 16)
                color_ptr = (lv_color16_t *)rotate_buffer;
            #elif (LV_COLOR_DEPTH == 24)
                color_ptr = (lv_color_t *)rotate_buffer;
            #elif (LV_COLOR_DEPTH == 32)
                color_ptr = (lv_color32_t *)rotate_buffer;
            #endif
            
        }else{
            #if (LV_COLOR_DEPTH == 16)
                color_ptr = (lv_color16_t *)px_map;
            #elif (LV_COLOR_DEPTH == 24)
                color_ptr = (lv_color_t *)px_map;
            #elif (LV_COLOR_DEPTH == 32)
                color_ptr = (lv_color32_t *)px_map;
            #endif
        }

        lv_coord_t width = lv_area_get_width(&rotated_area);
        lv_coord_t height = lv_area_get_height(&rotated_area);

        if (lv_vendor_draw_buffer_cnt() == 2) {
            lv_dma2d_memcpy_buffer(color_ptr, width, height, disp_buf, vendor_config.lcd_hor_res, vendor_config.lcd_ver_res, rotated_area.x1, rotated_area.y1);
        } else {
            offset = rotated_area.y1 * lv_hor + rotated_area.x1;
            for (y = rotated_area.y1; y <= rotated_area.y2 && y < lv_ver; y++) {
                lv_memcpy_one_line(disp_buf + offset, color_ptr, width);
                offset += lv_hor;
                color_ptr += width;
            }
        }

        if (lv_display_flush_is_last(disp)) {
            if (lv_vendor_draw_buffer_cnt() == 2) {
                tkl_lvgl_dma2d_wait_transfer_finish();
            }

            tkl_lvgl_display_frame_request(0, 0, 0, 0, (UCHAR_T *)disp_buf, FRAME_REQ_TYPE_LVGL);

            if (disp_buf2) {
                if (DISPLAY_BUFFER_1 == disp_buff_index) {
                    lv_dma2d_memcpy_buffer(disp_buf, lv_hor, lv_ver, disp_buf1, lv_hor, lv_ver, 0, 0);
                    disp_buff_index = 2;
                } else if (DISPLAY_BUFFER_2 == disp_buff_index) {
                    lv_dma2d_memcpy_buffer(disp_buf, lv_hor, lv_ver, disp_buf2, lv_hor, lv_ver, 0, 0);
                    disp_buff_index = 1;
                }
                else //first display
                {
                    lv_dma2d_memcpy_buffer(disp_buf, lv_hor, lv_ver, disp_buf2, lv_hor, lv_ver, 0, 0);
                    disp_buff_index = 1;
                }
            }
        }
    #else
        tkl_lvgl_display_frame_request(0, 0, 0, 0, (UCHAR_T *)px_map, FRAME_REQ_TYPE_LVGL);
    #endif
    }

    /*IMPORTANT!!!
     *Inform the graphics library that you are ready with the flushing*/
    lv_display_flush_ready(disp);
}

#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif
