#include <stdio.h>
#include <string.h>
#include "tuya_port_disp.h"
#include "tkl_memory.h"
#include "tal_mutex.h"

#ifdef TUYA_APP_DRIVERS_TP
#include "tal_tp_service.h"
#else
#include "driver/drv_tp.h"
#endif

#ifdef TUYA_MULTI_TYPES_LCD
#include "lvgl.h"
#include "tal_display_service.h"
static TY_DISPLAY_HANDLE disp_hdl = NULL;
static TY_DISPLAY_HANDLE disp_hdl_slave = NULL;
static TUYA_SCREEN_EXPANSION_TYPE  disp_exp_type = TUYA_SCREEN_EXPANSION_NONE;
static UINT8_T *disp_exp_buf = NULL;
static UINT8_T *disp_exp_slave_buf = NULL;                      //扩展屏幕的次屏内存不可和主屏共用,因为tal_display为异步处理!
static UINT8_T *disp_exp_buf_unalign = NULL;
static UINT8_T *disp_exp_slave_buf_unalign = NULL;              //扩展屏幕的次屏内存不可和主屏共用,因为tal_display为异步处理!
static ty_frame_buffer_t *lvgl_frame_buffer = NULL;
static ty_frame_buffer_t *lvgl_frame_slave_buffer = NULL;       //扩展屏幕的次屏内存不可和主屏共用,因为tal_display为异步处理!
static UINT32_T disp_x_offset = 0;      //屏显x坐标偏移
static UINT32_T disp_y_offset = 0;      //屏显y坐标偏移
#define DISP_EXP_BUF_ALIGN                       4
#else
#include "driver/media_types.h"
#include "yuv_encode.h"
#include <frame_buffer.h>
static frame_buffer_t *lvgl_frame_buffer = NULL;
#endif
#include "tal_time_service.h"
#include "base_event.h"

#ifdef TUYA_LVGL_DMA2D
#ifdef TUYA_DMA2D_SHARE
#include "tuya_port_dma2d.h"
#else
#include "tkl_dma2d.h"
#include "tkl_semaphore.h"
static TKL_SEM_HANDLE lv_dma2d_sem = NULL;
static unsigned char lv_dma2d_use_flag = 0;
#endif
#endif

static unsigned char ui_paused = 0;             //ui pause !
static MUTEX_HANDLE  ui_pause_mutex = NULL;     //ui pause mutex

static tkl_lvgl_disp_flush_async_cb_t tkl_lvgl_disp_flush_async_cb = NULL;
static void *tkl_lvgl_disp_flush_async_cb_data = NULL;
static volatile unsigned char lcd_flush_count = 0;
static ty_pixel_format_t curr_fmt = FMT_UNKNOW;
static uint16_t screen_width = 0;               //屏幕物理宽像素
static uint16_t screen_height = 0;              //屏幕物理高像素
//poweron page flush异步回调独立出来,避免与lvgl刷屏异步回调冲突!
static tkl_lvgl_disp_flush_async_cb_t tkl_lvgl_disp_flush_PwrOnPage_async_cb = NULL;
static void *tkl_lvgl_disp_flush_PwrOnPage_async_cb_data = NULL;

STATIC VOID tkl_lvgl_wait_for_te_signal(VOID);

#ifndef TUYA_MULTI_TYPES_LCD
static void lvgl_frame_buffer_free(frame_buffer_t *frame_buffer)
{
    //To do
}

const frame_buffer_callback_t lvgl_frame_buffer_cb =
{
    .free = lvgl_frame_buffer_free,
};
#endif

#ifdef TUYA_LVGL_DMA2D
#ifndef TUYA_DMA2D_SHARE
static void ty_dma2d_cb(TUYA_DMA2D_IRQ_E type, VOID_T *args)
{
    if (type == TUYA_DMA2D_TRANS_COMPLETE_ISR) {
        tkl_semaphore_post(lv_dma2d_sem);
    }
    else if (type == TUYA_DMA2D_TRANS_ERROR_ISR) {
        TY_GUI_LOG_PRINT("%s: dma2d trans error\n", __func__);
    }
}

static CONST TUYA_DMA2D_BASE_CFG_T ty_dma2d_cfg = {
    .cb = ty_dma2d_cb,
    .arg = NULL
};
#endif
#endif

static void tkl_lvgl_frame_flush_completed_cb(UINT8_T *frame_buff)
{
    tkl_lvgl_disp_flush_async_cb_t async_cb = NULL;
    void *data = NULL;

    if (tkl_lvgl_disp_flush_PwrOnPage_async_cb != NULL) {               //优先执行启动页面回调!
        async_cb = tkl_lvgl_disp_flush_PwrOnPage_async_cb;
        data = tkl_lvgl_disp_flush_PwrOnPage_async_cb_data;
    }
    else if (tkl_lvgl_disp_flush_async_cb != NULL){
        async_cb = tkl_lvgl_disp_flush_async_cb;
        data = tkl_lvgl_disp_flush_async_cb_data;
    }

    if (async_cb) {
        if (disp_exp_type != TUYA_SCREEN_EXPANSION_NONE) {          //双屏幕!
            static unsigned char cb_cnt = 0;
            if (++cb_cnt == lcd_flush_count) {                      //所有屏幕刷屏完成!
                async_cb(data);
                cb_cnt = 0;             //重置!
            }
        }
        else {      //单屏幕!
            async_cb(data);
        }
    }
}

VOID tkl_lvgl_dma2d_init(VOID)
{
#ifdef TUYA_LVGL_DMA2D
    #ifdef TUYA_DMA2D_SHARE
        tuya_dma2d_init();
    #else
        OPERATE_RET ret = OPRT_OK;

        ret = tkl_semaphore_create_init(&lv_dma2d_sem, 0, 1);
        if (OPRT_OK != ret) {
            TY_GUI_LOG_PRINT("%s %d ty_dma2d_sem init failed ???\n", __func__, __LINE__);
            return;
        }
        if (tkl_dma2d_init(&ty_dma2d_cfg) != OPRT_OK) {
            TY_GUI_LOG_PRINT("%s: tuya TKL DMA2D enable fail ???\n", __func__);
            return;
        }
    #endif
    TY_GUI_LOG_PRINT("%s: tuya TKL DMA2D enabled!\n", __func__);
#else
    TY_GUI_LOG_PRINT("%s: DMA2D disabled!\n", __func__);
#endif
}

VOID tkl_lvgl_dma2d_deinit(VOID)
{
#ifdef TUYA_LVGL_DMA2D
    #ifdef TUYA_DMA2D_SHARE
        //tuya_dma2d_deinit();          //share dma2d, never stop!
    #else
        tkl_dma2d_deinit();
        if (lv_dma2d_sem != NULL)
            tkl_semaphore_release(lv_dma2d_sem);
        lv_dma2d_sem = NULL;
    #endif
#endif
}

VOID tkl_lvgl_dma2d_memcpy(tkl_dma2d_memcpy_pfc_t *pixel_info)
{
    tal_mutex_lock(ui_pause_mutex);

    if (ui_paused) {
        TY_GUI_LOG_PRINT("%s : ui paused !\r\n", __func__);
        tal_mutex_unlock(ui_pause_mutex);
        return;
    }
#ifdef TUYA_LVGL_DMA2D
    TKL_DMA2D_FRAME_INFO_T src = { 0 };
    TKL_DMA2D_FRAME_INFO_T dst = { 0 };

    src.pbuf = pixel_info->input_addr;
    dst.pbuf = pixel_info->output_addr;
    //input_color_mode
    if (pixel_info->input_color_mode == COLOR_INPUT_RGB565)
        src.type = TUYA_FRAME_FMT_RGB565;
    else if (pixel_info->input_color_mode == COLOR_INPUT_RGB888)
        src.type = TUYA_FRAME_FMT_RGB888;
    else if (pixel_info->input_color_mode == COLOR_INPUT_ARGB888) {
        //src.type = TUYA_FRAME_FMT_RGB888;
        TY_GUI_LOG_PRINT("%s not support input type(%d) frame cpy\r\n", __func__, pixel_info->input_color_mode);
        tal_mutex_unlock(ui_pause_mutex);
        return;
    }
    //output_color_mode
    if (pixel_info->output_color_mode == COLOR_OUTPUT_RGB565)
        dst.type = TUYA_FRAME_FMT_RGB565;
    else if (pixel_info->output_color_mode == COLOR_OUTPUT_RGB888)
        dst.type = TUYA_FRAME_FMT_RGB888;
    else if (pixel_info->output_color_mode == COLOR_OUTPUT_ARGB888) {
        //dst.type = TUYA_FRAME_FMT_RGB888;
        TY_GUI_LOG_PRINT("%s not support output type(%d) frame cpy\r\n", __func__, pixel_info->output_color_mode);
        tal_mutex_unlock(ui_pause_mutex);
        return;
    }
    src.width       = pixel_info->src_frame_width;
    src.height      = pixel_info->src_frame_height;
    src.axis.x_axis = pixel_info->src_frame_xpos;
    src.axis.y_axis = pixel_info->src_frame_ypos;
    src.width_cp    = 0;
    src.height_cp   = 0;

    dst.width       = pixel_info->dst_frame_width;
    dst.height      = pixel_info->dst_frame_height;
    dst.axis.x_axis = pixel_info->dst_frame_xpos;
    dst.axis.y_axis = pixel_info->dst_frame_ypos;

    #ifdef TUYA_DMA2D_SHARE
    if (tuya_dma2d_request(TASK_TYPE_LVGL_FLUSH, &src, &dst) != OPRT_OK) {
        TY_GUI_LOG_PRINT("%s: share dma2d memcpy error ???\n", __func__);
    }
    #else
    if (tkl_dma2d_memcpy(&src, &dst) != OPRT_OK) {
        TY_GUI_LOG_PRINT("%s: tkl dma2d memcpy error ???\n", __func__);
    }
    lv_dma2d_use_flag = 1;
    #endif
#else
    UINT32_T offset = 0;
    UINT32_T bytes_per_pixel = pixel_info->src_pixel_byte;
    UCHAR_T *output_addr = (UCHAR_T *)pixel_info->output_addr;
    UCHAR_T *input_addr = (UCHAR_T *)pixel_info->input_addr;

    offset = (pixel_info->dst_frame_ypos * pixel_info->dst_frame_width + pixel_info->dst_frame_xpos) * bytes_per_pixel;
    for(UINT32_T i = pixel_info->dst_frame_ypos; i < (pixel_info->dst_frame_ypos + pixel_info->src_frame_height); i++) {
        memcpy(output_addr + offset, input_addr, pixel_info->src_frame_width * bytes_per_pixel);
        offset += (pixel_info->dst_frame_width * bytes_per_pixel);
        input_addr += (pixel_info->src_frame_width * bytes_per_pixel);
    }
#endif

    tal_mutex_unlock(ui_pause_mutex);
}

BOOL_T tkl_lvgl_dma2d_is_busy(void)
{
#ifdef TUYA_LVGL_DMA2D
    #ifdef TUYA_DMA2D_SHARE
    return tuya_dma2d_is_busy();
    #else
    return FALSE;
    #endif
#else
    return FALSE;
#endif
}

VOID tkl_lvgl_dma2d_wait_transfer_finish(void)
{
    tal_mutex_lock(ui_pause_mutex);
    if (ui_paused) {
        TY_GUI_LOG_PRINT("%s : ui paused !\r\n", __func__);
        tal_mutex_unlock(ui_pause_mutex);
        return;
    }

#ifdef TUYA_LVGL_DMA2D
    #ifdef TUYA_DMA2D_SHARE
    tuya_dma2d_wait_finish(TASK_TYPE_LVGL_FLUSH);
    #else
    OPERATE_RET ret = OPRT_OK;

    if (lv_dma2d_sem && lv_dma2d_use_flag) {
        ret = tkl_semaphore_wait(lv_dma2d_sem, 1000);
        if (ret != OPRT_OK) {
            TY_GUI_LOG_PRINT("%s lv_dma2d_sem get fail! ret = %d\r\n", __func__, ret);
        }
        lv_dma2d_use_flag = 0;
    }
    #endif
#endif

    tal_mutex_unlock(ui_pause_mutex);
}

VOID tkl_lvgl_pause(void)
{
    tal_mutex_lock(ui_pause_mutex);
    tkl_lvgl_dma2d_deinit();
    ui_paused = 1;
    tal_mutex_unlock(ui_pause_mutex);
    //restore ui snapshot
}

VOID tkl_lvgl_resume(void)
{
    tal_mutex_lock(ui_pause_mutex);
    tkl_lvgl_dma2d_init();
    ui_paused = 0;
#ifdef TUYA_LVGL_DMA2D
    #ifndef TUYA_DMA2D_SHARE
    lv_dma2d_use_flag = 0;
    #endif
#endif
    tal_mutex_unlock(ui_pause_mutex);
    //recovery snapshot!
    if (disp_hdl != NULL) {
        tal_display_flush(disp_hdl, lvgl_frame_buffer);
        if (disp_hdl_slave != NULL) {       //双屏幕暂不支持!
            if (lvgl_frame_slave_buffer != NULL)
                tal_display_flush(disp_hdl_slave, lvgl_frame_slave_buffer);
        }
    }
}

#if defined(TUYA_AI_TOY_DEMO) && TUYA_AI_TOY_DEMO == 1
STATIC OPERATE_RET tuya_display_flush(TY_DISPLAY_HANDLE handle, ty_frame_buffer_t *frame_buff);
VOID tuya_ai_display_pause(VOID)
{
    tkl_lvgl_pause();
}

VOID tuya_ai_display_resume(VOID)
{
    tkl_lvgl_resume();
}

VOID tuya_ai_display_flush(VOID *data)
{
    if (disp_hdl != NULL)
        tuya_display_flush(disp_hdl, (ty_frame_buffer_t *)data);
    else {
        TY_GUI_LOG_PRINT("%s %d : disp hal not found ?\r\n", __func__, __LINE__);
    }
}
#endif

static UINT64_T _current_timestamp_(void)
{
    TIME_S sectime = 0;
    TIME_MS mstime = 0;
    UINT64_T cur_time_ms = 0;

    tal_time_get_system_time(&sectime, &mstime);
    cur_time_ms = (UINT64_T)sectime * 1000 + mstime;

    return cur_time_ms;
}

#ifdef TUYA_APP_DRIVERS_TP
static TY_TP_HANDLE gui_tp_hdl = NULL;
static ty_tp_device_cfg_t *gui_tp_driver = NULL;
static ty_tp_usr_cfg_t *gui_tp_cfg = NULL;
OPERATE_RET tkl_lvgl_tp_handle_register(VOID *tp_driver, VOID *tp_cfg)
{
    STATIC BOOL_T tp_inited = FALSE;

    if (tp_inited) {
        TY_GUI_LOG_PRINT("%s %d : tp re-inited !\r\n", __func__, __LINE__);
        return OPRT_OK;
    }
    if (gui_tp_driver)
        tkl_system_free(gui_tp_driver);
    if (gui_tp_cfg)
        tkl_system_free(gui_tp_cfg);
    gui_tp_driver = (ty_tp_device_cfg_t *)tkl_system_malloc(sizeof(ty_tp_device_cfg_t));
    gui_tp_cfg = (ty_tp_usr_cfg_t *)tkl_system_malloc(sizeof(ty_tp_usr_cfg_t));
    if (gui_tp_driver == NULL || gui_tp_cfg == NULL) {
        TY_GUI_LOG_PRINT("%s %d : malloc fail ?\r\n", __func__, __LINE__);
        return OPRT_MALLOC_FAILED;
    }
    memcpy((VOID *)gui_tp_driver, tp_driver, sizeof(ty_tp_device_cfg_t));
    memcpy((VOID *)gui_tp_cfg, tp_cfg, sizeof(ty_tp_usr_cfg_t));
    tp_inited = TRUE;
    return OPRT_OK;
}
#endif

VOID tkl_lvgl_tp_close(VOID)
{
#ifdef TUYA_APP_DRIVERS_TP
    if (gui_tp_hdl)
        tal_tp_close(gui_tp_hdl);
    else {
        TY_GUI_LOG_PRINT("%s %d : tp not initialized ?\r\n", __func__, __LINE__);
    }
#else
    drv_tp_close();
#endif
}

OPERATE_RET tkl_lvgl_tp_open(UINT32_T hor_size, UINT32_T ver_size)
{
#ifdef TUYA_APP_DRIVERS_TP
    if (gui_tp_driver != NULL && gui_tp_cfg != NULL) {
        TY_GUI_LOG_PRINT("%s %d : tp '%s' open start!\r\n", __func__, __LINE__, gui_tp_driver->name);
        TY_GUI_LOG_PRINT("%s %d : tp idx '%d', clk '%d', sda '%d', intr '%d', rst '%d', pwr_ctrl '%d'\r\n", __func__, __LINE__, 
            gui_tp_cfg->tp_i2c_idx,
            gui_tp_cfg->tp_i2c_clk.pin, gui_tp_cfg->tp_i2c_sda.pin, gui_tp_cfg->tp_intr.pin, gui_tp_cfg->tp_rst.pin,
            gui_tp_cfg->tp_pwr_ctrl.pin);
        gui_tp_driver->width = hor_size;
        gui_tp_driver->height = ver_size;
        gui_tp_driver->mirror_type = TP_MIRROR_NONE;
        gui_tp_hdl = tal_tp_open(gui_tp_driver, gui_tp_cfg);
        tkl_system_free(gui_tp_driver);
        tkl_system_free(gui_tp_cfg);
        gui_tp_driver = NULL;
        gui_tp_cfg = NULL;
    }
    else {
        TY_GUI_LOG_PRINT("%s %d : tp not initialized ?\r\n", __func__, __LINE__);
    }
    return (gui_tp_hdl != NULL)?OPRT_OK:OPRT_COM_ERROR;
#else
    int ret = kNoErr;
    ret = drv_tp_open(hor_size, ver_size, TP_MIRROR_NONE);
    return (ret == kNoErr)?OPRT_OK:OPRT_COM_ERROR;
#endif
}

OPERATE_RET tkl_lvgl_tp_read(tkl_tp_point_infor_t *tp_point)
{
    static UINT64_T last_release_ms = 0;
    UINT64_T click_interval_ms = 0;
    OPERATE_RET ret = OPRT_OK;

#ifdef TUYA_APP_DRIVERS_TP
    ty_tp_point_info_t point;
    if (gui_tp_hdl == NULL) {
        //TY_GUI_LOG_PRINT("%s %d : tp not initialized ?\r\n", __func__, __LINE__);
        return OPRT_COM_ERROR;
    }
    if (OPRT_OK == tal_tp_read(&point)) {
#else
    tp_point_infor_t point;
    if (kNoErr == drv_tp_read(&point)) {
#endif
        if (ui_paused) {
            //TY_GUI_LOG_PRINT("%s : ui paused, detect tp state '%d' !\r\n", __func__, point.m_state);
            if (point.m_state == 0) {
                if (last_release_ms == 0)
                    last_release_ms = _current_timestamp_();
                else {
                    click_interval_ms = _current_timestamp_() - last_release_ms;
                    //TY_GUI_LOG_PRINT("%s : ui paused, click interval '%llu' ms !\r\n", __func__, click_interval_ms);
                    if (click_interval_ms <= 500) {
                        //TY_GUI_LOG_PRINT("%s-do something!\r\n", __func__);
                        ty_publish_event(EVENT_TAKE_PHOTO_TRIGGER, NULL);
                        click_interval_ms = 0;
                    }
                    else
                        last_release_ms = _current_timestamp_();
                }
            }
            ret = OPRT_COM_ERROR;
        }
        else {
            tp_point->m_x = point.m_x;
            tp_point->m_y = point.m_y;
            tp_point->m_state = point.m_state;
            tp_point->m_need_continue = point.m_need_continue;
        }
    }
    else
        ret = OPRT_COM_ERROR;
    return ret;
}

OPERATE_RET tkl_lvgl_display_frame_init(UINT16_T lcd_width, UINT16_T lcd_height)
{
    OPERATE_RET ret = OPRT_COM_ERROR;
    ty_pixel_format_t fmt = FMT_RGB565;
    STATIC BOOL_T disp_frame_inited = FALSE;

    if (disp_frame_inited) {
        TY_GUI_LOG_PRINT("%s %d : display frame *re-inited* !\r\n", __func__, __LINE__);
        return OPRT_OK;
    }

    ret = tal_mutex_create_init(&ui_pause_mutex);
    if (ret != OPRT_OK) {
        TY_GUI_LOG_PRINT("[%s:%d]:------ui pause mutex init fail ???\r\n", __func__, __LINE__);
        return ret;
    }

#if (LV_COLOR_DEPTH == 16)
    fmt = FMT_RGB565;
#elif (LV_COLOR_DEPTH == 24)
    fmt = FMT_RGB888;
#elif (LV_COLOR_DEPTH == 32)
    fmt = FMT_ARGB888;
#endif

#ifdef TUYA_MULTI_TYPES_LCD
    DISPLAY_PIXEL_FORMAT_E last_fmt = TY_PIXEL_FMT_RGB565;
    UCHAR_T bytes_per_pixel = 2;

    if (fmt == FMT_RGB565) {
        last_fmt = TY_PIXEL_FMT_RGB565;
        bytes_per_pixel = 2;
    }
    else if (fmt == FMT_RGB888) {
        last_fmt = TY_PIXEL_FMT_RGB888;
        bytes_per_pixel = 3;
    }
    //else if (fmt == FMT_ARGB888)
    //    last_fmt = PIXEL_FMT_ARGB8888;
    else {
        TY_GUI_LOG_PRINT("%s %d :unknown fmt '%d'\r\n", __func__, __LINE__, fmt);
        return ret;
    }
    lvgl_frame_buffer = tkl_system_psram_malloc(sizeof(ty_frame_buffer_t));
    if (!lvgl_frame_buffer) {
        TY_GUI_LOG_PRINT("%s %d lvgl_frame_buffer malloc fail\r\n", __func__, __LINE__);
        return ret;
    }

    memset(lvgl_frame_buffer, 0, sizeof(ty_frame_buffer_t));
    lvgl_frame_buffer->type = TYPE_PSRAM;
    lvgl_frame_buffer->fmt = last_fmt;
    lvgl_frame_buffer->x_start = 0;
    lvgl_frame_buffer->y_start = 0;
    lvgl_frame_buffer->width = lcd_width;
    lvgl_frame_buffer->height = lcd_height;
    lvgl_frame_buffer->free_cb = tkl_lvgl_frame_flush_completed_cb;
    lvgl_frame_buffer->frame = NULL;
    if (disp_hdl_slave != NULL) {
        if (disp_exp_type == TUYA_SCREEN_EXPANSION_H_EXP || disp_exp_type == TUYA_SCREEN_EXPANSION_V_EXP) {
            //slave frame buffer:
            lvgl_frame_slave_buffer = tkl_system_psram_malloc(sizeof(ty_frame_buffer_t));
            if (!lvgl_frame_slave_buffer) {
                TY_GUI_LOG_PRINT("%s %d lvgl_frame_slave_buffer malloc fail\r\n", __func__, __LINE__);
                return ret;
            }
            memset(lvgl_frame_slave_buffer, 0, sizeof(ty_frame_buffer_t));

            UINT8_T *buf_u8 = NULL, *slave_buf_u8 = NULL;
            size_t size_bytes = (lcd_width * lcd_height * bytes_per_pixel)/2;

            size_bytes += DISP_EXP_BUF_ALIGN - 1;
            //master buffer:
            buf_u8 = tkl_system_psram_malloc(size_bytes);
            if (buf_u8) {
                disp_exp_buf_unalign = buf_u8;
                memset(buf_u8, 0x00, size_bytes);
                buf_u8 += DISP_EXP_BUF_ALIGN - 1;
                buf_u8 = (UINT8_T *)((UINT32_T) buf_u8 & ~(DISP_EXP_BUF_ALIGN - 1));
                disp_exp_buf = buf_u8;
            }
            else {
                TY_GUI_LOG_PRINT("%s %d :malloc fail ?\r\n", __func__, __LINE__);
                return ret;
            }
            //slave buffer:
            slave_buf_u8 = tkl_system_psram_malloc(size_bytes);
            if (slave_buf_u8) {
                disp_exp_slave_buf_unalign = slave_buf_u8;
                memset(slave_buf_u8, 0x00, size_bytes);
                slave_buf_u8 += DISP_EXP_BUF_ALIGN - 1;
                slave_buf_u8 = (UINT8_T *)((UINT32_T) slave_buf_u8 & ~(DISP_EXP_BUF_ALIGN - 1));
                disp_exp_slave_buf = slave_buf_u8;
            }
            else {
                TY_GUI_LOG_PRINT("%s %d :malloc fail ?\r\n", __func__, __LINE__);
                return ret;
            }
        }

        //重新调整屏幕宽高为单一屏幕参数
        if (disp_hdl->type == DISPLAY_SPI) {
            lvgl_frame_buffer->width = disp_hdl->spi.width;
            lvgl_frame_buffer->height = disp_hdl->spi.height;
        }
        else if (disp_hdl->type == DISPLAY_QSPI) {
            lvgl_frame_buffer->width = disp_hdl->qspi.width;
            lvgl_frame_buffer->height = disp_hdl->qspi.height;
        }
        else if (disp_hdl->type == DISPLAY_8080) {
            lvgl_frame_buffer->width = disp_hdl->mcu8080.cfg.width;
            lvgl_frame_buffer->height = disp_hdl->mcu8080.cfg.height;
        }
        else if (disp_hdl->type == DISPLAY_RGB) {
            lvgl_frame_buffer->width = disp_hdl->rgb.cfg.width;
            lvgl_frame_buffer->height = disp_hdl->rgb.cfg.height;
        }
    }
    lvgl_frame_buffer->len = lvgl_frame_buffer->width * lvgl_frame_buffer->height * bytes_per_pixel;
#else
    pixel_format_t last_fmt = PIXEL_FMT_UNKNOW;

    if (fmt == FMT_RGB565)
        last_fmt = PIXEL_FMT_RGB565;
    else if (fmt == FMT_RGB888)
        last_fmt = PIXEL_FMT_RGB888;
    else if (fmt == FMT_ARGB888)
        last_fmt = PIXEL_FMT_ARGB8888;
    else {
        TY_GUI_LOG_PRINT("%s %d :unknown fmt '%d'\r\n", __func__, __LINE__, fmt);
        return ret;
    }
    lvgl_frame_buffer = tkl_system_psram_malloc(sizeof(frame_buffer_t));
    if (!lvgl_frame_buffer) {
        TY_GUI_LOG_PRINT("%s %d lvgl_frame_buffer malloc fail\r\n", __func__, __LINE__);
        return ret;
    }

    memset(lvgl_frame_buffer, 0, sizeof(frame_buffer_t));
    lvgl_frame_buffer->width = lcd_width;
    lvgl_frame_buffer->height = lcd_height;
    lvgl_frame_buffer->fmt = last_fmt;
    lvgl_frame_buffer->cb = &lvgl_frame_buffer_cb;
#endif
    curr_fmt = fmt;
    screen_width = lvgl_frame_buffer->width;
    screen_height = lvgl_frame_buffer->height;
    if (lvgl_frame_slave_buffer != NULL) {
        memcpy((VOID *)lvgl_frame_slave_buffer, (VOID *)lvgl_frame_buffer, sizeof(ty_frame_buffer_t));
    }

    disp_frame_inited = TRUE;
    return OPRT_OK;
}

VOID tkl_lvgl_display_frame_deinit(VOID)
{
    if (lvgl_frame_buffer) {
        tkl_system_psram_free(lvgl_frame_buffer);
        lvgl_frame_buffer = NULL;
    }
    if (lvgl_frame_slave_buffer) {
        tkl_system_psram_free(lvgl_frame_slave_buffer);
        lvgl_frame_slave_buffer = NULL;
    }
#ifdef TUYA_MULTI_TYPES_LCD
    if (disp_exp_buf) {
        tkl_system_psram_free(disp_exp_buf_unalign);
        disp_exp_buf = NULL;
        disp_exp_buf_unalign = NULL;
    }
    if (disp_exp_slave_buf) {
        tkl_system_psram_free(disp_exp_slave_buf_unalign);
        disp_exp_slave_buf = NULL;
        disp_exp_slave_buf_unalign = NULL;
    }
#endif
}

#ifdef TUYA_MULTI_TYPES_LCD
STATIC OPERATE_RET tuya_display_flush(TY_DISPLAY_HANDLE handle, ty_frame_buffer_t *frame_buff)
{
    if (disp_x_offset > 0 || disp_y_offset > 0) {
        frame_buff->x_start += disp_x_offset;
        frame_buff->y_start += disp_y_offset;
    }
    return tal_display_flush(handle, frame_buff);
}
#endif

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
OPERATE_RET tkl_lvgl_display_frame_request(UINT32_T x, UINT32_T y, UINT32_T width, UINT32_T height, UCHAR_T *disp_buf, ty_frame_req_type_t req_type)
{
    OPERATE_RET ret = OPRT_COM_ERROR;
    UCHAR_T bytes_per_pixel = 2;

    if (x == 0 && y == 0) {     //a frame start position!
        tkl_lvgl_wait_for_te_signal();
    }

    tal_mutex_lock(ui_pause_mutex);
    if (ui_paused) {
        TY_GUI_LOG_PRINT("%s : ui paused !\r\n", __func__);
        tal_mutex_unlock(ui_pause_mutex);
        return ret;
    }

    if (curr_fmt == FMT_RGB565)
        bytes_per_pixel = 2;
    else if (curr_fmt == FMT_RGB888)
        bytes_per_pixel = 3;
    else if (curr_fmt == FMT_ARGB888)
        bytes_per_pixel = 4;

#ifdef TUYA_MULTI_TYPES_LCD
    if (disp_hdl != NULL) {
        if (disp_hdl_slave == NULL) {       //单屏
            if (lvgl_frame_buffer) {
                if (width != 0 && height != 0) {            //都不为0,表示使用局部刷新;否则默认为帧缓存刷新使用默认屏幕像素大小!
                    lvgl_frame_buffer->x_start  = x;
                    lvgl_frame_buffer->y_start  = y;
                    lvgl_frame_buffer->width    = width;
                    lvgl_frame_buffer->height   = height;
                    lvgl_frame_buffer->len      = width * height * bytes_per_pixel;
                }
                else {
                    lvgl_frame_buffer->x_start  = x;        //初始化为起始x坐标
                    lvgl_frame_buffer->y_start  = y;        //初始化为起始y坐标
                }
                lvgl_frame_buffer->frame    = (uint8_t *)disp_buf;
                lcd_flush_count = 1;
                ret = tuya_display_flush(disp_hdl, lvgl_frame_buffer);
            }
        }
        else {          //双屏扩展,复制
            UINT32_T i = 0;
            UINT32_T desIndex = 0;
            UINT32_T half_len = 0, full_len = 0;

            if (disp_exp_type == TUYA_SCREEN_EXPANSION_H_EXP) {
                UINT32_T screen_hor_res = screen_width * 2;                         //启动页面宽默认值
                UINT32_T screen_ver_res = screen_height;                            //启动页面高默认值
                if (req_type != FRAME_REQ_TYPE_BOOTON_PAGE) {
                    #if defined(TUYA_IMG_DIRECT_FLUSH) && (TUYA_IMG_DIRECT_FLUSH == 1)
                        screen_hor_res = screen_width * 2;
                        screen_ver_res = screen_height;
                    #else
                        screen_hor_res = LV_HOR_RES;
                        screen_ver_res = LV_VER_RES;
                    #endif
                }
                if (width != 0 && height != 0) {            //都不为0,表示使用局部刷新;否则默认为帧缓存刷新使用默认屏幕像素大小!
                    if (x < screen_width) {
                        UINT32_T end_x = x + width -1;
                        if (end_x < screen_width) {
                            lvgl_frame_buffer->x_start  = x;
                            lvgl_frame_buffer->y_start  = y;
                            lvgl_frame_buffer->width    = width;
                            lvgl_frame_buffer->height   = height;
                            lvgl_frame_buffer->len      = width * height * bytes_per_pixel;
                            lvgl_frame_buffer->frame    = (uint8_t *)disp_buf;
                            lcd_flush_count = 1;
                            ret = tuya_display_flush(disp_hdl, lvgl_frame_buffer);
                        }
                        else {      //x轴横跨两个屏幕
                            UINT32_T first_width_len    = screen_width - x;
                            UINT32_T last_width_len     = width - first_width_len;
                            UINT32_T first_data_len     = first_width_len * bytes_per_pixel;
                            UINT32_T last_data_len      = last_width_len * bytes_per_pixel;
                            UINT32_T full_data_len      = width * bytes_per_pixel;

                            lcd_flush_count = 2;
                            //screen 1:
                            desIndex = 0;
                            for (i = 0; i < height; i++) {
                                memcpy(disp_exp_buf+desIndex, disp_buf+full_data_len*i, first_data_len);
                                desIndex += first_data_len;
                            }
                            lvgl_frame_buffer->x_start  = x;
                            lvgl_frame_buffer->y_start  = y;
                            lvgl_frame_buffer->width    = first_width_len;
                            lvgl_frame_buffer->height   = height;
                            lvgl_frame_buffer->len      = first_width_len * height * bytes_per_pixel;
                            lvgl_frame_buffer->frame    = (uint8_t *)disp_exp_buf;
                            ret = tuya_display_flush(disp_hdl, lvgl_frame_buffer);

                            //screen 2:
                            desIndex = 0;
                            for (i = 0; i < height; i++) {
                                memcpy(disp_exp_slave_buf+desIndex, disp_buf+first_data_len*(i+1)+last_data_len*i, last_data_len);
                                desIndex += last_data_len;
                            }
                            lvgl_frame_slave_buffer->x_start  = 0;
                            lvgl_frame_slave_buffer->y_start  = y;
                            lvgl_frame_slave_buffer->width    = last_width_len;
                            lvgl_frame_slave_buffer->height   = height;
                            lvgl_frame_slave_buffer->len      = last_width_len * height * bytes_per_pixel;
                            lvgl_frame_slave_buffer->frame    = (uint8_t *)disp_exp_slave_buf;
                            ret = tuya_display_flush(disp_hdl_slave, lvgl_frame_slave_buffer);
                        }
                    }
                    else {
                        lvgl_frame_slave_buffer->x_start  = x - screen_width;
                        lvgl_frame_slave_buffer->y_start  = y;
                        lvgl_frame_slave_buffer->width    = width;
                        lvgl_frame_slave_buffer->height   = height;
                        lvgl_frame_slave_buffer->len      = width * height * bytes_per_pixel;
                        lvgl_frame_slave_buffer->frame    = (uint8_t *)disp_buf;
                        lcd_flush_count = 1;
                        ret = tuya_display_flush(disp_hdl_slave, lvgl_frame_slave_buffer);
                    }
                }
                else {
                    lcd_flush_count = 2;
                    half_len = (screen_hor_res/2) * bytes_per_pixel;
                    full_len = screen_hor_res * bytes_per_pixel;
                    //screen 1:
                    desIndex = 0;
                    for (i = 0; i < screen_ver_res; i++) {
                        memcpy(disp_exp_buf+desIndex, disp_buf+full_len*i, half_len);
                        desIndex += half_len;
                    }

                    lvgl_frame_buffer->x_start  = x;
                    lvgl_frame_buffer->y_start  = y;
                    //lvgl_frame_buffer->len      = lvgl_frame_buffer->width * lvgl_frame_buffer->height * bytes_per_pixel;
                    lvgl_frame_buffer->frame = (uint8_t *)disp_exp_buf;
                    ret = tuya_display_flush(disp_hdl, lvgl_frame_buffer);

                    //screen 2:
                    desIndex = 0;
                    for (i = 0; i < screen_ver_res; i++) {
                        memcpy(disp_exp_slave_buf+desIndex, disp_buf+half_len*(2*i+1), half_len);
                        desIndex += half_len;
                    }
                    lvgl_frame_slave_buffer->x_start  = x;
                    lvgl_frame_slave_buffer->y_start  = y;
                    lvgl_frame_slave_buffer->frame = (uint8_t *)disp_exp_slave_buf;
                    ret = tuya_display_flush(disp_hdl_slave, lvgl_frame_slave_buffer);
                }
            }
            else if (disp_exp_type == TUYA_SCREEN_EXPANSION_V_EXP) {
                if (width != 0 && height != 0) {            //都不为0,表示使用局部刷新;否则默认为帧缓存刷新使用默认屏幕像素大小!
                    if (y < screen_height) {
                        UINT32_T end_y = y + height -1;
                        if (end_y < screen_height) {
                            lcd_flush_count = 1;
                            lvgl_frame_buffer->x_start  = x;
                            lvgl_frame_buffer->y_start  = y;
                            lvgl_frame_buffer->width    = width;
                            lvgl_frame_buffer->height   = height;
                            lvgl_frame_buffer->len      = width * height * bytes_per_pixel;
                            lvgl_frame_buffer->frame    = (uint8_t *)disp_buf;
                            ret = tuya_display_flush(disp_hdl, lvgl_frame_buffer);
                        }
                        else {          //y轴横跨两个屏幕
                            UINT32_T first_height_len   = screen_height - y;
                            UINT32_T last_height_len    = height - first_height_len;

                            lcd_flush_count = 2;
                            //screen 1:
                            memcpy(disp_exp_buf, disp_buf, width*first_height_len*bytes_per_pixel);
                            lvgl_frame_buffer->x_start  = x;
                            lvgl_frame_buffer->y_start  = y;
                            lvgl_frame_buffer->width    = width;
                            lvgl_frame_buffer->height   = first_height_len;
                            lvgl_frame_buffer->len      = width * first_height_len * bytes_per_pixel;
                            lvgl_frame_buffer->frame    = (uint8_t *)disp_exp_buf;
                            ret = tuya_display_flush(disp_hdl, lvgl_frame_buffer);

                            //screen 2:
                            memcpy(disp_exp_slave_buf, disp_buf+width*first_height_len*bytes_per_pixel, width*last_height_len*bytes_per_pixel);
                            lvgl_frame_slave_buffer->x_start  = x;
                            lvgl_frame_slave_buffer->y_start  = 0;
                            lvgl_frame_slave_buffer->width    = width;
                            lvgl_frame_slave_buffer->height   = last_height_len;
                            lvgl_frame_slave_buffer->len      = width * last_height_len * bytes_per_pixel;
                            lvgl_frame_slave_buffer->frame    = (uint8_t *)disp_exp_slave_buf;
                            ret = tuya_display_flush(disp_hdl_slave, lvgl_frame_slave_buffer);
                        }
                    }
                    else {
                        lcd_flush_count = 1;
                        lvgl_frame_slave_buffer->x_start  = x;
                        lvgl_frame_slave_buffer->y_start  = y - screen_height;
                        lvgl_frame_slave_buffer->width    = width;
                        lvgl_frame_slave_buffer->height   = height;
                        lvgl_frame_slave_buffer->len      = width * height * bytes_per_pixel;
                        lvgl_frame_slave_buffer->frame    = (uint8_t *)disp_buf;
                        ret = tuya_display_flush(disp_hdl_slave, lvgl_frame_slave_buffer);
                    }
                }
                else {
                    lcd_flush_count = 2;
                    half_len = screen_width * screen_height * bytes_per_pixel;
                    //screen 1:
                    memcpy(disp_exp_buf, disp_buf, half_len);
                    lvgl_frame_buffer->x_start  = x;
                    lvgl_frame_buffer->y_start  = y;
                    //lvgl_frame_buffer->len      = lvgl_frame_buffer->width * lvgl_frame_buffer->height * bytes_per_pixel;
                    lvgl_frame_buffer->frame = (uint8_t *)disp_exp_buf;
                    ret = tuya_display_flush(disp_hdl, lvgl_frame_buffer);

                    //screen 2:
                    memcpy(disp_exp_slave_buf, disp_buf+half_len, half_len);
                    lvgl_frame_slave_buffer->x_start  = x;
                    lvgl_frame_slave_buffer->y_start  = y;
                    lvgl_frame_slave_buffer->frame = (uint8_t *)disp_exp_slave_buf;
                    ret = tuya_display_flush(disp_hdl_slave, lvgl_frame_slave_buffer);
                }
            }
            else if (disp_exp_type == TUYA_SCREEN_EXPANSION_H_DUP || disp_exp_type == TUYA_SCREEN_EXPANSION_V_DUP){
                lcd_flush_count = 2;
                //screen 1:
                if (width != 0 && height != 0) {            //都不为0,表示使用局部刷新;否则默认为帧缓存刷新使用默认屏幕像素大小!
                    lvgl_frame_buffer->width    = width;
                    lvgl_frame_buffer->height   = height;
                    lvgl_frame_buffer->len      = width * height * bytes_per_pixel;
                }
                lvgl_frame_buffer->x_start  = x;
                lvgl_frame_buffer->y_start  = y;
                lvgl_frame_buffer->frame = (uint8_t *)disp_buf;
                ret = tuya_display_flush(disp_hdl, lvgl_frame_buffer);
                //screen 2:
                lvgl_frame_buffer->x_start  = x;        //初始化为起始x坐标
                lvgl_frame_buffer->y_start  = y;        //初始化为起始y坐标
                ret = tuya_display_flush(disp_hdl_slave, lvgl_frame_buffer);
            }
            else {
                TY_GUI_LOG_PRINT("%s %d: unknown expanded direction '%d' ?\r\n", __func__, __LINE__, disp_exp_type);
            }
        }
    }
    else {
        TY_GUI_LOG_PRINT("%s %d: disp_hdl is null ?\r\n", __func__, __LINE__);
    }
#else
    if (lvgl_frame_buffer) {
        lvgl_frame_buffer->frame = (uint8_t *)disp_buf;
        ret = (lcd_display_frame_request(lvgl_frame_buffer) == BK_OK)?OPRT_OK:OPRT_COM_ERROR;
    }
#endif

    tal_mutex_unlock(ui_pause_mutex);
    return ret;
}

TKL_DISP_ROTATION_E tkl_lvgl_display_rotation_get(UCHAR_T rotation)
{
#ifdef LVGL_AS_APP_COMPONENT
    return (TKL_DISP_ROTATION_E)rotation;
#else
    TKL_DISP_ROTATION_E last_rotation = TKL_DISP_ROTATION_0;

    if (rotation == ROTATE_NONE)
        last_rotation = TKL_DISP_ROTATION_0;
    else if (rotation == ROTATE_90)
        last_rotation = TKL_DISP_ROTATION_90;
    else if (rotation == ROTATE_180)
        last_rotation = TKL_DISP_ROTATION_180;
    else if (rotation == ROTATE_270)
        last_rotation = TKL_DISP_ROTATION_270;
    return last_rotation;
#endif
}

VOID tkl_lvgl_disp_flush_async_register(tkl_lvgl_disp_flush_async_cb_t cb, void *drv)
{
    tkl_lvgl_disp_flush_async_cb = cb;
    tkl_lvgl_disp_flush_async_cb_data = drv;
}

VOID tkl_PwrOnPage_disp_flush_async_register(tkl_lvgl_disp_flush_async_cb_t cb, void *drv)
{
    tkl_lvgl_disp_flush_PwrOnPage_async_cb = cb;
    tkl_lvgl_disp_flush_PwrOnPage_async_cb_data = drv;
}

#ifdef TUYA_LVGL_TE
#include "tal_semaphore.h"
STATIC SEM_HANDLE te_signal_sem = NULL;                 //Tearing Effect coming sem
STATIC volatile bool te_flush_start  = false; 

VOID tuya_lvgl_te_enable(VOID)
{
    if (tal_semaphore_create_init(&te_signal_sem, 0, 1) == OPRT_OK) {
        TY_GUI_LOG_PRINT("TE func enabled successful !\r\n");
    }
    else {
        TY_GUI_LOG_PRINT("TE func enabled fail !\r\n");
    }
}

VOID tuya_lvgl_te_irq_handle(VOID)
{
    if (te_signal_sem && te_flush_start)
        tal_semaphore_post(te_signal_sem);
}

/*
                 no Read                             no Read
                 _____                              _____
                 |    |         Reading data        |    |         Reading data...
TE:______________|    |_____________________________|    |__________________________
参考:st77916 spi屏幕, 调整刷新频率(0xC2/0xC5同步改),保持不旋转(0x36), 指定TE触发的结束行数(0x44)
*/
STATIC VOID tkl_lvgl_wait_for_te_signal(VOID)
{
    OPERATE_RET ret = OPRT_OK;

    if (te_signal_sem) {
        te_flush_start = true;
        ret = tal_semaphore_wait(te_signal_sem, 1000);      //wait te !
        te_flush_start = false;
        if (ret != OPRT_OK) {
            TY_GUI_LOG_PRINT("wait TE timeout !\r\n");
        }
    }
    else {
        TY_GUI_LOG_PRINT("TE semphore no found !!!\r\n");
    }
}
#else
VOID tuya_lvgl_te_enable(VOID)
{
    TY_GUI_LOG_PRINT("TE func disabled !\r\n");
}

VOID tuya_lvgl_te_irq_handle(VOID)
{
    return;
}

STATIC VOID tkl_lvgl_wait_for_te_signal(VOID)
{
    return;
}
#endif

#ifdef TUYA_MULTI_TYPES_LCD
OPERATE_RET tkl_lvgl_display_handle_register(VOID *hdl, VOID *hdl_slave, TUYA_SCREEN_EXPANSION_TYPE  exp_type)
{
    OPERATE_RET ret = OPRT_OK;
    disp_hdl = (TY_DISPLAY_HANDLE)hdl;
    disp_hdl_slave = (TY_DISPLAY_HANDLE)hdl_slave;
    if (disp_hdl_slave == NULL)
        disp_exp_type = TUYA_SCREEN_EXPANSION_NONE;
    else
        disp_exp_type = exp_type;
    if (disp_hdl_slave != NULL && disp_exp_type == TUYA_SCREEN_EXPANSION_NONE)
        disp_exp_type = TUYA_SCREEN_EXPANSION_H_EXP;
    return ret;
}

BOOL_T tkl_lvgl_display_handle(VOID **hdl, VOID **hdl_slave, TUYA_SCREEN_EXPANSION_TYPE *out_exp_type)
{
    *hdl = (VOID *)disp_hdl;
    *hdl_slave = (VOID *)disp_hdl_slave;
    *out_exp_type = disp_exp_type;
    if (disp_hdl != NULL || disp_hdl_slave != NULL)
        return TRUE;
    return FALSE;
}

OPERATE_RET tkl_lvgl_display_resolution(UINT16_T *lcd_hor_res, UINT16_T *lcd_ver_res)
{
    OPERATE_RET ret = OPRT_COM_ERROR;

    if (disp_hdl == NULL && disp_hdl_slave == NULL) {
        TY_GUI_LOG_PRINT("%s:%d------------------------------------disp hdl un-initialized ????\r\n", __func__, __LINE__);
        return ret;
    }

    switch(disp_hdl->type) {
        case DISPLAY_RGB:
            *lcd_hor_res = disp_hdl->rgb.cfg.width;
            *lcd_ver_res = disp_hdl->rgb.cfg.height;
            ret = OPRT_OK;
            break;

        case DISPLAY_8080:
            *lcd_hor_res = disp_hdl->mcu8080.cfg.width;
            *lcd_ver_res = disp_hdl->mcu8080.cfg.height;
            ret = OPRT_OK;
            break;

        case DISPLAY_SPI:
            *lcd_hor_res = disp_hdl->spi.width;
            *lcd_ver_res = disp_hdl->spi.height;
            if (disp_hdl_slave != NULL) {
                if (disp_exp_type == TUYA_SCREEN_EXPANSION_H_EXP/* || exp_type == TUYA_SCREEN_EXPANSION_H_DUP*/) {
                    *lcd_hor_res += disp_hdl_slave->spi.width;
                }
                else if (disp_exp_type == TUYA_SCREEN_EXPANSION_V_EXP/* || exp_type == TUYA_SCREEN_EXPANSION_V_DUP*/) {
                    *lcd_ver_res += disp_hdl_slave->spi.height;
                }
            }
            ret = OPRT_OK;
            break;

        case DISPLAY_QSPI:
            *lcd_hor_res = disp_hdl->qspi.width;
            *lcd_ver_res = disp_hdl->qspi.height;
            if (disp_hdl_slave != NULL) {
                if (disp_exp_type == TUYA_SCREEN_EXPANSION_H_EXP/* || exp_type == TUYA_SCREEN_EXPANSION_H_DUP*/) {
                    *lcd_hor_res += disp_hdl_slave->qspi.width;
                }
                else if (disp_exp_type == TUYA_SCREEN_EXPANSION_V_EXP/* || exp_type == TUYA_SCREEN_EXPANSION_V_DUP*/) {
                    *lcd_ver_res += disp_hdl_slave->qspi.height;
                }
            }
            ret = OPRT_OK;
            break;

        default:
            TY_GUI_LOG_PRINT("%s:%d------------------------------------unknown disp type '%d' ????\r\n", __func__, __LINE__, disp_hdl->type);
            break;
    }

    return ret;
}

VOID tkl_lvgl_display_offset_set(UINT32_T x_off, UINT32_T y_off)
{
    disp_x_offset = x_off;
    disp_y_offset = y_off;
}

VOID tkl_lvgl_display_pixel_rgb_convert(UCHAR_T * src, INT32_T src_w, INT32_T src_h)
{
    if (disp_hdl != NULL) {
        if (disp_hdl->type == DISPLAY_8080) {   //BK7258底层的8080屏幕控制器仅支持16线输出的RGB666!!!
            #if 0
            if (curr_fmt == FMT_RGB888) {                 //仅支持RGB888->RGB666的转换!
                if ((disp_hdl->type == DISPLAY_RGB && disp_hdl->rgb.cfg.pixel_fmt == TY_PIXEL_FMT_RGB666) ||    \
                    (disp_hdl->type == DISPLAY_8080 && disp_hdl->mcu8080.cfg.pixel_fmt == TY_PIXEL_FMT_RGB666) ||   \
                    (disp_hdl->type == DISPLAY_SPI && disp_hdl->spi.pixel_fmt == TY_PIXEL_FMT_RGB666) ||    \
                    (disp_hdl->type == DISPLAY_QSPI && disp_hdl->qspi.pixel_fmt == TY_PIXEL_FMT_RGB666)) {
                    INT32_T srcIndex = 0;
                    for(INT32_T y = 0; y < src_h; ++y) {
                        for(INT32_T x = 0; x < src_w; ++x) {
                            srcIndex = y * src_w * 3 + x * 3;
                            src[srcIndex]       = src[srcIndex] & 0xFC;      /*Red*/
                            src[srcIndex + 1]   = src[srcIndex + 1] & 0xFC;  /*Green*/
                            src[srcIndex + 2]   = src[srcIndex + 2] & 0xFC;  /*Blue*/
                        }
                    }
                }
            }
            #endif
        }
        else if (disp_hdl->type == DISPLAY_SPI || disp_hdl->type == DISPLAY_QSPI) {
            if (curr_fmt == FMT_RGB565 && disp_hdl->spi.pixel_fmt == TY_PIXEL_FMT_RGB565) {
            #ifdef LVGL_COLOR_16_SWAP
                INT32_T srcIndex = 0;
                UCHAR_T tmp_data = 0;
                for(INT32_T y = 0; y < src_h; ++y) {
                    for(INT32_T x = 0; x < src_w; ++x) {
                        srcIndex = y * src_w * 2 + x * 2;
                        tmp_data = src[srcIndex];
                        src[srcIndex]       = src[srcIndex + 1];
                        src[srcIndex + 1]   = tmp_data;
                    }
                }
            #endif
            }
            else if (curr_fmt == FMT_RGB888 && disp_hdl->spi.pixel_fmt == TY_PIXEL_FMT_RGB666) {
            #ifdef LVGL_COLOR_24_SWAP
            INT32_T srcIndex = 0;
            UCHAR_T tmp_data = 0;
            for(INT32_T y = 0; y < src_h; ++y) {
                for(INT32_T x = 0; x < src_w; ++x) {
                    srcIndex = y * src_w * 3 + x * 3;
                    tmp_data = src[srcIndex];
                    src[srcIndex]       = src[srcIndex + 2];
                    src[srcIndex + 2]   = tmp_data;
                }
            }
            #endif
            }
        }
    }
}
#else
VOID tkl_lvgl_display_pixel_rgb_convert(UCHAR_T * src, INT32_T src_w, INT32_T src_h)
{

}
#endif

VOID* tkl_system_sram_malloc(CONST SIZE_T size)
{
    #if defined(TUYA_CPU_ARCH_SMP) && (TUYA_CPU_ARCH_SMP == 1)
    VOID* ptr = tkl_system_malloc(size);
    #else
    VOID* ptr = os_malloc(size);
    #endif
    if(NULL == ptr) {
        TY_GUI_LOG_PRINT("tkl_system sram_malloc failed, size(%d)!\r\n", size);
    }

    if (size > 4096) {
        TY_GUI_LOG_PRINT("tkl_system sram_malloc big memory, size(%d)\r\n", size);
    }
    return ptr;
}

VOID *tkl_system_sram_calloc(size_t nitems, size_t size)
{
    void *ptr = tkl_system_sram_malloc(nitems * size);
    if (ptr == NULL) {
        TY_GUI_LOG_PRINT("calloc failed, total_size(%d)! nitems = %d size = %d\r\n", nitems * size,nitems,size);
    }
    else
        memset(ptr, 0, nitems * size);
    return ptr;
}

VOID tkl_system_sram_free(VOID* ptr)
{
    #if defined(TUYA_CPU_ARCH_SMP) && (TUYA_CPU_ARCH_SMP == 1)
    tkl_system_free(ptr);
    #else
    os_free(ptr);
    #endif
}

/*Notice:
  以下弱定义兼容不支持psram的硬件平台!!!!!
*/
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    //TODO...
#else
#ifndef TUYA_WEAK_ATTRIBUTE
#define TUYA_WEAK_ATTRIBUTE __attribute__ ((weak))
#endif

TUYA_WEAK_ATTRIBUTE VOID_T tkl_system_psram_malloc_force_set(BOOL_T enable)
{

}

TUYA_WEAK_ATTRIBUTE VOID_T* tkl_system_psram_malloc(CONST SIZE_T size)
{
    return tkl_system_malloc(size);
}

TUYA_WEAK_ATTRIBUTE VOID_T tkl_system_psram_free(VOID_T* ptr)
{
    tkl_system_free(ptr);
}

TUYA_WEAK_ATTRIBUTE VOID_T *tkl_system_psram_calloc(size_t nitems, size_t size)
{
    return tkl_system_calloc(nitems, size);
}

TUYA_WEAK_ATTRIBUTE VOID_T *tkl_system_psram_realloc(VOID_T* ptr, size_t size)
{
    return tkl_system_realloc(ptr, size);
}

TUYA_WEAK_ATTRIBUTE INT_T tkl_system_psram_get_free_heap_size(VOID_T)
{
    return 0;
}

TUYA_WEAK_ATTRIBUTE size_t rtos_get_psram_total_heap_size(void)
{
    return 0;
}

TUYA_WEAK_ATTRIBUTE size_t rtos_get_psram_free_heap_size(void)
{
    return tkl_system_get_free_heap_size();
}

TUYA_WEAK_ATTRIBUTE size_t rtos_get_psram_minimum_free_heap_size(void)
{
    return 0;
}

TUYA_WEAK_ATTRIBUTE size_t rtos_get_total_heap_size(void)
{
    return 0;
}

TUYA_WEAK_ATTRIBUTE size_t rtos_get_free_heap_size(void)
{
    return tkl_system_get_free_heap_size();
}

TUYA_WEAK_ATTRIBUTE size_t rtos_get_minimum_free_heap_size(void)
{
    return 0;
}
#endif
