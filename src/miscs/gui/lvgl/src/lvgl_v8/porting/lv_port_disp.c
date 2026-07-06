/**
 * @file lv_port_disp_templ.c
 *
 */

 /*Copy this file as "lv_port_disp.c" and set this value to "1" to enable content*/
#if 1

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_disp.h"
#include "lv_vendor.h"
#include "tuya_port_disp.h"
#include "tkl_memory.h"
#include "tkl_semaphore.h"
#include "tkl_system.h"
#include "tal_display_service.h"

#define LOGI LV_LOG_INFO
#define LOGW LV_LOG_WARN
#define LOGE LV_LOG_ERROR
#define LOGD LV_LOG_TRACE

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

static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p);

//static void gpu_fill(lv_disp_drv_t * disp_drv, lv_color_t * dest_buf, lv_coord_t dest_width,
//        const lv_area_t * fill_area, lv_color_t color);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
static void *rotate_buffer = NULL;
//static beken_semaphore_t lv_dma2d_sem = NULL;
//static TKL_SEM_HANDLE lv_dma2d_sem = NULL;
//static uint8_t lv_dma2d_use_flag = 0;
// frame_buffer_t *lvgl_frame_buffer = NULL;
extern lv_vnd_config_t vendor_config;
// extern media_debug_t *media_debug;
static TKL_DISP_ROTATION_E disp_rotation = TKL_DISP_ROTATION_0;

static TKL_SEM_HANDLE lv_async_wait_sem = NULL;

// Modified by TUYA Start
#define LV_DRAW_BUF_STRIDE_ALIGN 1
static void *rotate_buffer_unalign = NULL;

static void rotate270_argb8888(const uint32_t * src, uint32_t * dst, int32_t srcWidth, int32_t srcHeight, int32_t srcStride, int32_t dstStride)
{
    srcStride /= sizeof(uint32_t);
    dstStride /= sizeof(uint32_t);

    for(int32_t x = 0; x < srcWidth; ++x) {
        int32_t dstIndex = x * dstStride;
        int32_t srcIndex = x;
        for(int32_t y = 0; y < srcHeight; ++y) {
            dst[dstIndex + (srcHeight - y - 1)] = src[srcIndex];
            srcIndex += srcStride;
        }
    }
}

static void rotate180_argb8888(const uint32_t * src, uint32_t * dst, int32_t width, int32_t height, int32_t src_stride, int32_t dest_stride)
{
    LV_UNUSED(dest_stride);
    src_stride /= sizeof(uint32_t);

    for(int32_t y = 0; y < height; ++y) {
        int32_t dstIndex = (height - y - 1) * src_stride;
        int32_t srcIndex = y * src_stride;
            for(int32_t x = 0; x < width; ++x) {
            dst[dstIndex + width - x - 1] = src[srcIndex + x];
        }
    }
}

static void rotate90_argb8888(const uint32_t * src, uint32_t * dst, int32_t srcWidth, int32_t srcHeight, int32_t srcStride, int32_t dstStride)
{
    srcStride /= sizeof(uint32_t);
    dstStride /= sizeof(uint32_t);

    for(int32_t x = 0; x < srcWidth; ++x) {
        int32_t dstIndex = (srcWidth - x - 1);
        int32_t srcIndex = x;
        for(int32_t y = 0; y < srcHeight; ++y) {
            dst[dstIndex * dstStride + y] = src[srcIndex];
            srcIndex += srcStride;
        }
    }
}

static void rotate270_rgb565(const uint16_t * src, uint16_t * dst, int32_t srcWidth, int32_t srcHeight, int32_t srcStride, int32_t dstStride)
{
    srcStride /= sizeof(uint16_t);
    dstStride /= sizeof(uint16_t);

    for(int32_t x = 0; x < srcWidth; ++x) {
        int32_t dstIndex = x * dstStride;
        int32_t srcIndex = x;
        for(int32_t y = 0; y < srcHeight; ++y) {
            dst[dstIndex + (srcHeight - y - 1)] = src[srcIndex];
            srcIndex += srcStride;
        }
    }
}

static void rotate180_rgb565(const uint16_t * src, uint16_t * dst, int32_t width, int32_t height, int32_t src_stride, int32_t dest_stride)
{
    src_stride /= sizeof(uint16_t);
    dest_stride /= sizeof(uint16_t);

    for(int32_t y = 0; y < height; ++y) {
        int32_t dstIndex = (height - y - 1) * dest_stride;
        int32_t srcIndex = y * src_stride;
            for(int32_t x = 0; x < width; ++x) {
            dst[dstIndex + width - x - 1] = src[srcIndex + x];
        }
    }
}

static void rotate90_rgb565(const uint16_t * src, uint16_t * dst, int32_t srcWidth, int32_t srcHeight, int32_t srcStride, int32_t dstStride)
{
    srcStride /= sizeof(uint16_t);
    dstStride /= sizeof(uint16_t);

    for(int32_t x = 0; x < srcWidth; ++x) {
        int32_t dstIndex = (srcWidth - x - 1);
        int32_t srcIndex = x;
        for(int32_t y = 0; y < srcHeight; ++y) {
            dst[dstIndex * dstStride + y] = src[srcIndex];
            srcIndex += srcStride;
        }
    }
}

static void lv_draw_sw_rotate(const void * src, void * dest, int32_t src_width, int32_t src_height, int32_t src_sride, int32_t dest_stride, lv_disp_drv_t * disp_drv, uint32_t px_bpp)
{
    if(disp_drv->rotated == LV_DISP_ROT_90) {
        if(px_bpp == 16) rotate90_rgb565(src, dest, src_width, src_height, src_sride, dest_stride);
        if(px_bpp == 32) rotate90_argb8888(src, dest, src_width, src_height, src_sride, dest_stride);
    }
    else if(disp_drv->rotated == LV_DISP_ROT_180) {
        if(px_bpp == 16) rotate180_rgb565(src, dest, src_width, src_height, src_sride, dest_stride);
        if(px_bpp == 32) rotate180_argb8888(src, dest, src_width, src_height, src_sride, dest_stride);
    }
    else if(disp_drv->rotated == LV_DISP_ROT_270) {
        if(px_bpp == 16) rotate270_rgb565(src, dest, src_width, src_height, src_sride, dest_stride);
        if(px_bpp == 32) rotate270_argb8888(src, dest, src_width, src_height, src_sride, dest_stride);
    }
}

static void lv_display_rotate_area(lv_disp_drv_t * disp_drv, lv_area_t * area)
{
    int32_t w = lv_area_get_width(area);
    int32_t h = lv_area_get_height(area);

    switch(disp_drv->rotated) {
        case LV_DISP_ROT_NONE:
            return;
        case LV_DISP_ROT_90:
            area->y2 = disp_drv->ver_res - area->x1 - 1;
            area->x1 = area->y1;
            area->x2 = area->x1 + h - 1;
            area->y1 = area->y2 - w + 1;
            break;
        case LV_DISP_ROT_180:
            area->y2 = disp_drv->ver_res - area->y1 - 1;
            area->y1 = area->y2 - h + 1;
            area->x2 = disp_drv->hor_res - area->x1 - 1;
            area->x1 = area->x2 - w + 1;
            break;
        case LV_DISP_ROT_270:
            area->x1 = disp_drv->hor_res - area->y2 - 1;
            area->y2 = area->x2;
            area->x2 = area->x1 + h - 1;
            area->y1 = area->y2 - w + 1;
            break;
    }
}

static uint32_t lv_draw_buf_width_to_stride(uint32_t w, uint32_t color_bpp)
{
    uint32_t width_byte;
    width_byte = w * color_bpp;
    width_byte = (width_byte + 7) >> 3; /*Round up*/
    return (width_byte + LV_DRAW_BUF_STRIDE_ALIGN - 1) & ~(LV_DRAW_BUF_STRIDE_ALIGN - 1);
}

static void * rotate_buf_align_alloc(size_t size_bytes, bool use_sram)
{
    uint8_t *buf_u8= NULL;
    /*Allocate larger memory to be sure it can be aligned as needed*/
    size_bytes += 4 - 1;
    if (use_sram)
        buf_u8 = tkl_system_sram_malloc(size_bytes);
    else
        buf_u8 = tkl_system_psram_malloc(size_bytes);
    rotate_buffer_unalign = buf_u8;
    if (buf_u8) {
        buf_u8 += 4 - 1;
        buf_u8 = (uint8_t *)((uint32_t) buf_u8 & ~(4 - 1));
        //bk_printf("%s-%d: original buf ptr '%p', aligned buf ptr '%p'\r\n", __func__, __LINE__, rotate_buffer_unalign, (void *)buf_u8);
    }
    return buf_u8;
}

static void rotate_buf_align_free(void *ptr)
{
    //((void *)ptr);
    if (rotate_buffer_unalign) {
        if (vendor_config.draw_buf_use_sram)
            tkl_system_sram_free(rotate_buffer_unalign);
        else
            tkl_system_psram_free(rotate_buffer_unalign);
    }
    rotate_buffer_unalign = NULL;
}
// Modified by TUYA End

static void lv_disp_flush_async_ready_notify(void *drv)
{
    lv_disp_flush_ready((lv_disp_drv_t *)drv);
    tkl_semaphore_post(lv_async_wait_sem);
}

static void lv_disp_wait_async_flush(lv_disp_drv_t * disp_drv)
{
    if (disp_drv->draw_buf->flushing) {
        tkl_semaphore_wait(lv_async_wait_sem, TKL_SEM_WAIT_FOREVER);    // 睡眠等待异步刷新结束,让出CPU
    }
}

int lv_port_disp_init(void)
{
    int ret = OPRT_OK;

    /*-------------------------
     * Initialize your display
     * -----------------------*/
    disp_init();

    /*-----------------------------
     * Create a buffer for drawing
     *----------------------------*/

    /**
     * LVGL requires a buffer where it internally draws the widgets.
     * Later this buffer will passed to your display driver's `flush_cb` to copy its content to your display.
     * The buffer has to be greater than 1 display row
     *
     * There are 3 buffering configurations:
     * 1. Create ONE buffer:
     *      LVGL will draw the display's content here and writes it to your display
     *
     * 2. Create TWO buffer:
     *      LVGL will draw the display's content to a buffer and writes it your display.
     *      You should use DMA to write the buffer's content to the display.
     *      It will enable LVGL to draw the next part of the screen to the other buffer while
     *      the data is being sent form the first buffer. It makes rendering and flushing parallel.
     *
     * 3. Double buffering
     *      Set 2 screens sized buffers and set disp_drv.full_refresh = 1.
     *      This way LVGL will always provide the whole rendered screen in `flush_cb`
     *      and you only need to change the frame buffer's address.
     */

    /* Example for 1) */
    //static lv_disp_draw_buf_t draw_buf_dsc_1;
    //static lv_color_t buf_1[MY_DISP_HOR_RES * 10];                          /*A buffer for 10 rows*/
    //lv_disp_draw_buf_init(&draw_buf_dsc_1, buf_1, NULL, MY_DISP_HOR_RES * 10);   /*Initialize the display buffer*/

    /* Example for 2) */
    static lv_disp_draw_buf_t draw_buf_dsc_2;

    LOGI("LVGL addr1:%x, addr2:%x, pixel size:%d, fb1:%x, fb2:%x\r\n", vendor_config.draw_buf_2_1, vendor_config.draw_buf_2_2,
                                        vendor_config.draw_pixel_size, vendor_config.frame_buf_1, vendor_config.frame_buf_2);

    lv_disp_draw_buf_init(&draw_buf_dsc_2, vendor_config.draw_buf_2_1, vendor_config.draw_buf_2_2, vendor_config.draw_pixel_size);   /*Initialize the display buffer*/

    /* Example for 3) also set disp_drv.full_refresh = 1 below*/
    //static lv_disp_draw_buf_t draw_buf_dsc_3;
    //static lv_color_t buf_3_1[MY_DISP_HOR_RES * MY_DISP_VER_RES];            /*A screen sized buffer*/
    //static lv_color_t buf_3_1[MY_DISP_HOR_RES * MY_DISP_VER_RES];            /*An other screen sized buffer*/
    //lv_disp_draw_buf_init(&draw_buf_dsc_3, buf_3_1, buf_3_2, MY_DISP_VER_RES * LV_VER_RES_MAX);   /*Initialize the display buffer*/

    /*-----------------------------------
     * Register the display in LVGL
     *----------------------------------*/

    static lv_disp_drv_t disp_drv;                         /*Descriptor of a display driver*/
    lv_disp_drv_init(&disp_drv);                    /*Basic initialization*/

    /*Set up the functions to access to your display*/

    /*Set the resolution of the display*/
// Modified by TUYA Start
    // if ((vendor_config.rotation == ROTATE_90) || (vendor_config.rotation == ROTATE_270)) {
    //     disp_drv.hor_res = vendor_config.lcd_ver_res;
    //     disp_drv.ver_res = vendor_config.lcd_hor_res;
    // } else {
        disp_drv.hor_res = vendor_config.lcd_hor_res;
        disp_drv.ver_res = vendor_config.lcd_ver_res;
    // }
// Modified by TUYA End

#if LVGL_USE_PSRAM
#if LVGL_USE_DIRECT_MODE
    disp_drv.full_refresh = 0;
    disp_drv.direct_mode = 1;
#else
    disp_drv.full_refresh = 1;
    disp_drv.direct_mode = 0;
#endif
#endif

    /*Used to copy the buffer's content to the display*/
    disp_drv.flush_cb = disp_flush;

    //disp_drv.render_start_cb = disp_render_start;

    /*Set a display buffer*/
    disp_drv.draw_buf = &draw_buf_dsc_2;

// Modified by TUYA Start
    disp_rotation = tkl_lvgl_display_rotation_get(vendor_config.rotation);
    if (disp_rotation != TKL_DISP_ROTATION_0) {
        if (disp_rotation == TKL_DISP_ROTATION_180) {
            //disp_drv.sw_rotate = 1;
            disp_drv.rotated = LV_DISP_ROT_180;
        }else if(disp_rotation == TKL_DISP_ROTATION_90){
            disp_drv.rotated = LV_DISP_ROT_90;
        }else if(disp_rotation == TKL_DISP_ROTATION_270){
            disp_drv.rotated = LV_DISP_ROT_270;
        }

        #if (LV_COLOR_DEPTH == 16)
            rotate_buffer = rotate_buf_align_alloc(vendor_config.draw_pixel_size * sizeof(lv_color_t), vendor_config.draw_buf_use_sram);
        #elif (LV_COLOR_DEPTH == 32)
            rotate_buffer = rotate_buf_align_alloc(vendor_config.draw_pixel_size * sizeof(lv_color32_t), vendor_config.draw_buf_use_sram);
        #endif
        if (rotate_buffer == NULL) {
            LOGE("lvgl rotate buffer malloc fail!\n");
            return OPRT_MALLOC_FAILED;
        }
    }
// Modified by TUYA End

    /*Required for Example 3)*/
    //disp_drv.full_refresh = 1

    /* Fill a memory array with a color if you have GPU.
     * Note that, in lv_conf.h you can enable GPUs that has built-in support in LVGL.
     * But if you have a different GPU you can use with this callback.*/
    //disp_drv.gpu_fill_cb = gpu_fill;

    /*Finally register the driver*/
    lv_disp_drv_register(&disp_drv);

    if (lv_vendor_display_frame_cnt() == 0) {   //没有帧缓存,如T2AI(仅支持spi单屏幕,内存不足情况下)渲染后直接flush,开启异步刷屏完成通知!
        if (OPRT_OK != tkl_semaphore_create_init(&lv_async_wait_sem, 0, 1)) {
            LOGE("%s semaphore init failed\n", __func__);
            return OPRT_COM_ERROR;
        }
        tkl_lvgl_disp_flush_async_register(lv_disp_flush_async_ready_notify, (void *)&disp_drv);
        disp_drv.wait_cb = lv_disp_wait_async_flush;            //lvgl异步刷新等待!
    }

    return ret;
}

void lv_port_disp_deinit(void)
{
// Modified by TUYA Start
    if(disp_rotation != TKL_DISP_ROTATION_0){
        if (rotate_buffer) {
            rotate_buf_align_free(rotate_buffer);
            rotate_buffer = NULL;
        }
    }
// Modified by TUYA End
    lv_disp_remove(lv_disp_get_default());

    disp_deinit();
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
static void lv_memcpy_one_line(void *dest_buf, const void *src_buf, uint32_t point_num)
{
    memcpy(dest_buf, src_buf, point_num * sizeof(lv_color_t));
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

#if 0
static lv_color_t *update_dual_buffer_with_direct_mode(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *colour_p)
{
    lv_disp_t* disp = _lv_refr_get_disp_refreshing();
    lv_coord_t y, hres = lv_disp_get_hor_res(disp);
    uint16_t i;
    lv_color_t *buf_cpy;
    lv_coord_t w;
    lv_area_t *inv_area = NULL;
    int offset = 0;

    if (colour_p == disp_drv->draw_buf->buf1) {
        buf_cpy = disp_drv->draw_buf->buf2;
    } else {
        buf_cpy = disp_drv->draw_buf->buf1;
    }

    LOGD("inv_p:%d, x1:%d, y1:%d, x2:%d, y2:%d, buf_cpy:%x\r\n", disp->inv_p, disp->inv_areas[0].x1, disp->inv_areas[0].y1, disp->inv_areas[0].x2, disp->inv_areas[0].y2, buf_cpy);
    for(i = 0; i < disp->inv_p; i++) {
        if(disp->inv_area_joined[i])
            continue;  /* Only copy areas which aren't part of another area */

        inv_area = &disp->inv_areas[i];
        w = lv_area_get_width(inv_area);

        offset = inv_area->y1 * hres + inv_area->x1;

        for(y = inv_area->y1; y <= inv_area->y2 && y < disp_drv->ver_res; y++) {
            lv_memcpy_one_line(buf_cpy + offset, colour_p + offset, w);
            offset += hres;
        }
    }

    return buf_cpy;
}
#endif

/*Flush the content of the internal buffer the specific area on the display
 *You can use DMA or any hardware acceleration to do this operation in the background but
 *'lv_disp_flush_ready()' has to be called when finished.*/
static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p)
{
    //RGB convert!
    tkl_lvgl_display_pixel_rgb_convert((UCHAR_T *)color_p, lv_area_get_width(area), lv_area_get_height(area));

    if (lv_vendor_display_frame_cnt() == 0) {                   //没有帧缓存,如T2AI(仅支持spi单屏幕,内存不足情况下)渲染后直接flush!
        TY_DISPLAY_HANDLE _disp_hdl = NULL, _disp_hdl_slave = NULL;
        TUYA_SCREEN_EXPANSION_TYPE exp_type = TUYA_SCREEN_EXPANSION_NONE;

        if (tkl_lvgl_display_handle((VOID **)&_disp_hdl, (VOID **)&_disp_hdl_slave, &exp_type) == TRUE) {
            if (_disp_hdl != NULL && _disp_hdl->type == DISPLAY_SPI) {
                if (disp_flush_enabled) {
                    tkl_lvgl_display_frame_request(area->x1, area->y1, lv_area_get_width(area), lv_area_get_height(area), (UCHAR_T *)color_p, FRAME_REQ_TYPE_LVGL);
                }
                /*没有帧缓存,屏驱又是将color_p做异步刷新的,不能立刻调用lv_disp_flush_ready(告诉lvgl刷新完成),
                  否则lvgl会马上继续使用draw buffer渲染,导致数据被篡改至花屏,只能等屏幕刷新完成后调用ty_frame_buffer_t->free_cb,
                  在其回调中执行lv_disp_flush_ready告诉lvgl刷新完成后才能继续渲染下一帧数据*/
                //lv_disp_flush_ready(disp_drv);
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

#if (!LVGL_USE_PSRAM)
    if (disp_flush_enabled) {
// Modified by TUYA Start
        lv_color_t *color_ptr = NULL;
// Modified by TUYA End
        static lv_color_t *disp_buf = NULL;
        static uint8_t disp_buff_index = DISPLAY_BUFFER_DEF;
        lv_color_t *disp_buf1 = vendor_config.frame_buf_1;
        lv_color_t *disp_buf2 = vendor_config.frame_buf_2;

        lv_coord_t lv_hor = LV_HOR_RES;
        lv_coord_t lv_ver = LV_VER_RES;

        int y = 0;
        int offset = 0;
// Modified by TUYA Start
        // lv_coord_t width = lv_area_get_width(area);
        // lv_coord_t height = lv_area_get_height(area);
// Modified by TUYA End

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

// Modified by TUYA Start
        lv_area_t rotated_area;
        rotated_area.x1 = area->x1;
        rotated_area.x2 = area->x2;
        rotated_area.y1 = area->y1;
        rotated_area.y2 = area->y2;
        if(disp_drv->rotated != LV_DISP_ROT_NONE) {
            #if (LV_COLOR_DEPTH == 16)
            uint32_t color_bpp = 16;
            #elif (LV_COLOR_DEPTH == 32)
            uint32_t color_bpp = 32;
            #endif
            /*Calculate the position of the rotated area*/
            lv_display_rotate_area(disp_drv, &rotated_area);
            /*Calculate the source stride (bytes in a line) from the width of the area*/
            uint32_t src_stride = lv_draw_buf_width_to_stride(lv_area_get_width(area), color_bpp);
            /*Calculate the stride of the destination (rotated) area too*/
            uint32_t dest_stride = lv_draw_buf_width_to_stride(lv_area_get_width(&rotated_area), color_bpp);
            /*Have a buffer to store the rotated area and perform the rotation*/
            
            int32_t src_w = lv_area_get_width(area);
            int32_t src_h = lv_area_get_height(area);
            lv_draw_sw_rotate(color_p, rotate_buffer, src_w, src_h, src_stride, dest_stride, disp_drv, color_bpp);
            /*Use the rotated area and rotated buffer from now on*/

            color_ptr = (lv_color_t *)rotate_buffer;

        }else{
            color_ptr = color_p;
        }

        lv_coord_t width = lv_area_get_width(&rotated_area);
        lv_coord_t height = lv_area_get_height(&rotated_area);
// Modified by TUYA End
            if (lv_vendor_draw_buffer_cnt() == 2) {
// Modified by TUYA Start
                //lv_dma2d_memcpy_draw_buffer(color_ptr, width, height, disp_buf, area->x1, area->y1);
                lv_dma2d_memcpy_buffer(color_ptr, width, height, disp_buf, vendor_config.lcd_hor_res, vendor_config.lcd_ver_res, rotated_area.x1, rotated_area.y1);
// Modified by TUYA End
            } else {
// Modified by TUYA Start
                offset = rotated_area.y1 * lv_hor + rotated_area.x1;
                for (y = rotated_area.y1; y <= rotated_area.y2 && y < disp_drv->ver_res; y++) {
// Modified by TUYA End
                    lv_memcpy_one_line(disp_buf + offset, color_ptr, width);
                    offset += lv_hor;
                    color_ptr += width;
                }
            }

        if (lv_disp_flush_is_last(disp_drv)) {
 
            if (lv_vendor_draw_buffer_cnt() == 2) {
                tkl_lvgl_dma2d_wait_transfer_finish();
            }
 
            tkl_lvgl_display_frame_request(0, 0, 0, 0, (UCHAR_T *)disp_buf, FRAME_REQ_TYPE_LVGL);


            if(disp_buf2) {
                if (DISPLAY_BUFFER_1 == disp_buff_index) {
// Modified by TUYA Start
                    //lv_dma2d_memcpy_last_frame(disp_buf, disp_buf1, lv_hor, lv_ver, 0, 0);
                    lv_dma2d_memcpy_buffer(disp_buf, lv_hor, lv_ver, disp_buf1, lv_hor, lv_ver, 0, 0);
// Modified by TUYA End
                    disp_buff_index = 2;
                } else if (DISPLAY_BUFFER_2 == disp_buff_index) {
// Modified by TUYA Start
                    //lv_dma2d_memcpy_last_frame(disp_buf, disp_buf2, lv_hor, lv_ver, 0, 0);
                    lv_dma2d_memcpy_buffer(disp_buf, lv_hor, lv_ver, disp_buf2, lv_hor, lv_ver, 0, 0);
// Modified by TUYA End
                    disp_buff_index = 1;
                }
                else //first display
                {
// Modified by TUYA Start
                    //lv_dma2d_memcpy_last_frame(disp_buf, disp_buf2, lv_hor, lv_ver, 0, 0);
                    lv_dma2d_memcpy_buffer(disp_buf, lv_hor, lv_ver, disp_buf2, lv_hor, lv_ver, 0, 0);
                    disp_buff_index = 1;
// Modified by TUYA End
                }
            }
        }
    }

    lv_disp_flush_ready(disp_drv);
#else
    if (disp_flush_enabled) {
        tkl_lvgl_display_frame_request(0, 0, 0, 0, (UCHAR_T *)color_p, FRAME_REQ_TYPE_LVGL);
    }

    lv_disp_flush_ready(disp_drv);
#endif
}


/*OPTIONAL: GPU INTERFACE*/

/*If your MCU has hardware accelerator (GPU) then you can use it to fill a memory with a color*/
//static void gpu_fill(lv_disp_drv_t * disp_drv, lv_color_t * dest_buf, lv_coord_t dest_width,
//                    const lv_area_t * fill_area, lv_color_t color)
//{
//    /*It's an example code which should be done by your GPU*/
//    int32_t x, y;
//    dest_buf += dest_width * fill_area->y1; /*Go to the first line*/
//
//    for(y = fill_area->y1; y <= fill_area->y2; y++) {
//        for(x = fill_area->x1; x <= fill_area->x2; x++) {
//            dest_buf[x] = color;
//        }
//        dest_buf+=dest_width;    /*Go to the next line*/
//    }
//}


#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif
