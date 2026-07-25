#include <stdio.h>
#include <string.h>
#include "img_direct_flush.h"

#include "app_ipc_command.h"
#ifdef TUYA_GUI_RESOURCE_DOWNLAOD
#include "ty_gfw_gui_file.h"
#include "ty_gfw_gui_kepler_file_download_cloud.h"
#endif
#include "tuya_app_gui_gw_core1.h"
#include "tuya_app_gui_gw_core0.h"
#include "tuya_port_disp.h"
#include "tal_display_service.h"
#include "tal_workq_service.h"
#include "tal_semaphore.h"
#include "tal_memory.h"
#include "tkl_mutex.h"
#include "tkl_memory.h"
#include "tal_system.h"

#ifdef ENABLE_IMG_DIRECT_FLUSH_RES_DL_UI
#define __ARM_2D_IMPL__
#include "arm_2d.h"
#include "__arm_2d_impl.h"
#include "arm_2d_helper.h"
#endif

STATIC BOOL_T PwrOnPageQuickStart = FALSE;                  //上电页面快速启动是否打开!
STATIC UCHAR_T *PwrOnPageBuf = NULL;                        //上电页面图片缓存!(从代码空间的图片静态数组指针直刷速度慢,会卡顿!)
STATIC UINT32_T PwrOnPageBufRows = 0;                       //上电页面图片缓存的行数!
STATIC UCHAR_T *PwrOnPageFrame = NULL;                      //上电页面当前图片帧!
STATIC UINT32_T PwrOnPageFrameRowOff = 0;                   //上电页面当前图片帧行偏移!
STATIC UCHAR_T *PwrOnPageBufUnaligned = NULL;
STATIC UCHAR_T PwrOnPageFlushStop = 0;                      //上电页面刷新停止计数!
STATIC size_t PwrOnPageBufSize = 0;
STATIC SEM_HANDLE PwrOnPageFrameSem = NULL;                 //一帧等待信号量


STATIC DELAYED_WORK_HANDLE d_img_flush_work = NULL;
STATIC DELAYED_WORK_HANDLE d_img_resource_dl_work = NULL;
STATIC TKL_MUTEX_HANDLE d_img_flush_mutex = NULL;
STATIC UCHAR_T **d_img_flush_data = NULL;
STATIC UCHAR_T *d_img_flush_frame = NULL;
STATIC UCHAR_T *d_img_flush_tmpbuff = NULL;
STATIC UINT32_T d_img_flush_num = 0;
STATIC UINT32_T d_img_flush_offset = 0;
STATIC UINT32_T d_img_flush_interval = 0;                   //flush interval:ms
STATIC volatile INT32_T screen_width = 0;
STATIC volatile INT32_T screen_height = 0;
STATIC volatile UCHAR_T screen_bytes_per_pixel = 0;
STATIC BOOL_T img_flush_stopped = TRUE;                    //flush状态:是否停止的!
STATIC IMG_FLUSH_RES_DL_STAGE_E img_flush_res_dl_stage = IMG_FLUSH_RES_DL_NOTHING;
STATIC LV_RESOURCE_DL_EVENT_SS *img_flush_res_dl_progress = NULL;
STATIC TKL_DISP_ROTATION_E dl_progerss_rotate_angle = TKL_DISP_ROTATION_0;
STATIC SEM_HANDLE img_flush_dl_sem = NULL;
#define IMG_FLUSH_DL_SEM_CNT            8
#define IMG_FLUSH_RES_DL_DELAY_MS       3000

#ifdef ENABLE_IMG_DIRECT_FLUSH_RES_DL_UI
#define ANGLE_PI 3.14159265359f

STATIC VOID_T img_direct_flush_resource_dl_progress_display(CONST CHAR_T *text)
{
    static BOOL_T arm_2d_initialized = FALSE;
    arm_2d_tile_t ptTile;
    arm_2d_tile_t text_ptTile;
    arm_2d_region_t text_tRegion;
    UINT32_T text_width = 0, text_height = 0;
    UCHAR_T *text_rotate_buf = NULL;
    float fAngle = 0.0f;

    if (!arm_2d_initialized) {
        arm_2d_initialized = TRUE;
        arm_2d_init();
    }

    if (d_img_flush_tmpbuff == NULL) {
        d_img_flush_tmpbuff = tkl_system_psram_malloc(screen_width * screen_height * screen_bytes_per_pixel);
        if (d_img_flush_tmpbuff == NULL) {
            TY_GUI_LOG_PRINT("[%s:%d]:------resource dl alloc fail !\r\n", __func__, __LINE__);
            return;
        }
    }
    memset(d_img_flush_tmpbuff, 0, screen_width * screen_height * screen_bytes_per_pixel);
    memcpy(d_img_flush_tmpbuff, d_img_flush_frame, screen_width * screen_height * screen_bytes_per_pixel);

    ptTile = (arm_2d_tile_t) {
        .tRegion = {
            .tSize = {
                .iWidth = screen_width,
                .iHeight = screen_height,
            },
        },
        .tInfo = {
            .bIsRoot = true,
        },
        .pchBuffer = (uint8_t *)d_img_flush_tmpbuff,
    };

    if (screen_width >= 240 && screen_height >= 160) {
        arm_lcd_text_set_font(&ARM_2D_FONT_16x24.use_as__arm_2d_font_t);
        text_width = strlen(text) * 16;
        text_height = 24;
    }
    else {
        arm_lcd_text_set_font(&ARM_2D_FONT_6x8.use_as__arm_2d_font_t);
        text_width = strlen(text) * 6;
        text_height = 8;
    }

    if (dl_progerss_rotate_angle == TKL_DISP_ROTATION_0 || dl_progerss_rotate_angle == TKL_DISP_ROTATION_180) { //0度,180度
        text_tRegion = (arm_2d_region_t) {
            .tLocation = {
                .iX = (screen_width - text_width) / 2,
                .iY = (screen_height - text_height) / 2,
            },
            .tSize = {
                .iWidth = text_width,
                .iHeight = text_height
            }
        };
    }
    else {      //90度,270度
        text_tRegion = (arm_2d_region_t) {
            .tLocation = {
                .iX = (screen_width - text_height) / 2,
                .iY = (screen_height - text_width) / 2,
            },
            .tSize = {
                .iWidth = text_height,
                .iHeight = text_width
            }
        };
    }

    arm_lcd_text_set_colour(GLCD_COLOR_RED, GLCD_COLOR_BLACK);
    if (0/*dl_progerss_rotate_angle == TKL_DISP_ROTATION_0*/) {
        arm_lcd_text_set_target_framebuffer(&ptTile);
        arm_lcd_text_set_draw_region(&text_tRegion);
        arm_lcd_text_location(0, 0);
        arm_lcd_puts(text);
    }
    else {
        if (dl_progerss_rotate_angle == TKL_DISP_ROTATION_90)
            fAngle = ANGLE_PI/2;
        else if (dl_progerss_rotate_angle == TKL_DISP_ROTATION_180)
            fAngle = ANGLE_PI;
        else if (dl_progerss_rotate_angle == TKL_DISP_ROTATION_270)
            fAngle = ANGLE_PI*3/2;

        text_rotate_buf = tkl_system_psram_malloc(text_width * text_height * screen_bytes_per_pixel);
        if (text_rotate_buf == NULL) {
            TY_GUI_LOG_PRINT("[%s:%d]:------resource dl alloc text rotate buf fail !\r\n", __func__, __LINE__);
            return;
        }
        memset(text_rotate_buf, 0, text_width * text_height * screen_bytes_per_pixel);
        text_ptTile = (arm_2d_tile_t) {
            .tRegion = {
                .tLocation = {
                    .iX = 0,
                    .iY = 0
                },
                .tSize = {
                    .iWidth = text_width,
                    .iHeight = text_height,
                },
            },
            .tInfo = {
                .bIsRoot = true,
                .bHasEnforcedColour = true,             // 强制颜色格式
                .tColourInfo = {
                    .chScheme = (screen_bytes_per_pixel == 2)?ARM_2D_COLOUR_RGB565:(screen_bytes_per_pixel == 3)?ARM_2D_COLOUR_RGB888:ARM_2D_COLOUR_BGRA8888,
                },
            },
            .pchBuffer = (uint8_t *)text_rotate_buf,
        };
        arm_lcd_text_set_target_framebuffer(&text_ptTile);
        arm_lcd_text_set_draw_region(NULL);
        arm_lcd_text_location(0, 0);
        arm_lcd_puts(text);

        arm_2d_location_t targetCentre = {          // 设置旋转中心
            .iX = text_width / 2,
            .iY = text_height / 2,
        };

        if (screen_bytes_per_pixel == 2) {
            arm_2d_rgb565_tile_rotation_only(
                &text_ptTile,               // 源Tile
                &ptTile,                    // 目标Tile
                &text_tRegion,              // 目标区域
                targetCentre,               // 目标中心点
                fAngle
            );
        }
        else if (screen_bytes_per_pixel == 3){
            arm_2d_cccn888_tile_rotation_only(
                &text_ptTile,               // 源Tile
                &ptTile,                    // 目标Tile
                &text_tRegion,              // 目标区域
                targetCentre,               // 目标中心点
                fAngle
            );
        }
        else {
            TY_GUI_LOG_PRINT("[%s:%d]:------resource dl unsupported pixel '%d' !\r\n", __func__, __LINE__, screen_bytes_per_pixel);
        }
    }
    tkl_mutex_lock(d_img_flush_mutex);
    tkl_lvgl_display_frame_request(0, 0, 0, 0, d_img_flush_tmpbuff, FRAME_REQ_TYPE_IMG_DIR_FLUSH);
    tkl_mutex_unlock(d_img_flush_mutex);
    if (text_rotate_buf)
        tkl_system_psram_free(text_rotate_buf);
}
#endif

STATIC VOID_T img_direct_flush_resource_dl_display(IMG_FLUSH_RES_DL_STAGE_E stage, VOID *data)
{
    #ifdef ENABLE_IMG_DIRECT_FLUSH_RES_DL_UI
    switch (stage) {
        case IMG_FLUSH_RES_DL_QUERYING:
            img_direct_flush_resource_dl_progress_display("In Resource Query...");
            break;
    
        case IMG_FLUSH_RES_DL_QUERIED_NEW:
            img_direct_flush_resource_dl_progress_display("Has Newer Resource");
            break;
    
        case IMG_FLUSH_RES_DL_QUERIED_NO_NEW:
            img_direct_flush_resource_dl_progress_display("No Newer Resource");
            break;
    
        case IMG_FLUSH_RES_DL_START: {
                LV_RESOURCE_DL_EVENT_SS *dl_progress = (LV_RESOURCE_DL_EVENT_SS *)data;
                static CHAR_T progress_str[64] = { 0 };
                memset(progress_str, 0, sizeof(progress_str));
                snprintf(progress_str, sizeof(progress_str), "Total:%02d/%02d,Progress:%03d%%", 
                    dl_progress->files_dl_count, dl_progress->files_num, dl_progress->dl_prog);
                img_direct_flush_resource_dl_progress_display((CONST CHAR_T *)progress_str);
            }
            break;
    
        case IMG_FLUSH_RES_DL_FAIL:
            img_direct_flush_resource_dl_progress_display("Resource Download Fail !");
            break;
    
        case IMG_FLUSH_RES_DL_SUCCESS:
            img_direct_flush_resource_dl_progress_display("Resource Download OK !");
            break;
    
        default:
            break;
    }
    #endif
}

STATIC VOID_T img_direct_flush_resource_dl_event_callback(UINT32_T evt_type, VOID_T *evt_data)
{
    LV_TUYAOS_EVENT_TYPE_E evt = (LV_TUYAOS_EVENT_TYPE_E)evt_type;
    BOOL_T is_known = TRUE;

    TY_GUI_LOG_PRINT("[%s:%d]:------resource dl evt '%d' !\r\n", __func__, __LINE__, evt);
    if (evt == LV_TUYAOS_EVENT_HAS_NEW_RESOURCE) {
        img_flush_res_dl_stage = IMG_FLUSH_RES_DL_QUERIED_NEW;
    }
    else if (evt == LV_TUYAOS_EVENT_NO_NEW_RESOURCE) {
        img_flush_res_dl_stage = IMG_FLUSH_RES_DL_QUERIED_NO_NEW;
    }
    else if (evt == LV_TUYAOS_EVENT_RESOURCE_UPDATE_PROGRESS) {
        memcpy((VOID *)img_flush_res_dl_progress, evt_data, sizeof(LV_RESOURCE_DL_EVENT_SS));
        if (img_flush_res_dl_stage != IMG_FLUSH_RES_DL_START)
            img_flush_res_dl_stage = IMG_FLUSH_RES_DL_START;
    }
    else if (evt == LV_TUYAOS_EVENT_RESOURCE_UPDATE_END) {
        if (*(UINT_T *)evt_data == OPRT_OK)
            img_flush_res_dl_stage = IMG_FLUSH_RES_DL_SUCCESS;
        else
            img_flush_res_dl_stage = IMG_FLUSH_RES_DL_FAIL;
    }
    else
        is_known = FALSE;

    if (is_known)
        tal_semaphore_post(img_flush_dl_sem);
}

STATIC VOID_T img_direct_flush_resource_dl_work(VOID_T *data)
{
    if (img_flush_res_dl_stage == IMG_FLUSH_RES_DL_QUERYING) {
        if (img_flush_stopped) {         //flush work stop!
            TY_GUI_LOG_PRINT("[%s:%d]:------resource dl query start !\r\n", __func__, __LINE__);
        #ifdef TUYA_GUI_RESOURCE_DOWNLAOD
            ty_gfw_gui_file_init();
        #else
            TY_GUI_LOG_PRINT("[%s:%d]:------resource dl query start fail, unsupport resource dl ?\r\n", __func__, __LINE__);
        #endif
        }
        else {
            tal_workq_start_delayed(d_img_resource_dl_work, 100, LOOP_ONCE);
        }
    }
    else if (img_flush_res_dl_stage == IMG_FLUSH_RES_DL_QUERIED_NO_NEW || img_flush_res_dl_stage == IMG_FLUSH_RES_DL_FAIL){
        TY_GUI_LOG_PRINT("[%s:%d]:------resource dl query end by fail !\r\n", __func__, __LINE__);
        img_flush_res_dl_stage = IMG_FLUSH_RES_DL_NOTHING;
        if (d_img_flush_tmpbuff)
            tkl_system_psram_free(d_img_flush_tmpbuff);
        d_img_flush_tmpbuff = NULL;
    }
    else if (img_flush_res_dl_stage == IMG_FLUSH_RES_DL_SUCCESS) {
        LV_DISP_EVENT_S lv_msg = {.tag = NULL, .event = LLV_EVENT_VENDOR_REBOOT, .param = 0};
        aic_lvgl_msg_event_change((void *)&lv_msg);
    }
}

STATIC VOID_T img_direct_flush_resource_dl_task(VOID_T *args)
{
    for(;;) {
        if (tal_semaphore_wait(img_flush_dl_sem, SEM_WAIT_FOREVER) == OPRT_OK) {
            switch (img_flush_res_dl_stage) {
                case IMG_FLUSH_RES_DL_QUERYING: {
                        TY_GUI_LOG_PRINT("[%s:%d]:------resource dl query start !\r\n", __func__, __LINE__);
                        img_direct_flush_resource_dl_display(img_flush_res_dl_stage, NULL);
                        tal_workq_stop_delayed(d_img_resource_dl_work);
                        tal_workq_start_delayed(d_img_resource_dl_work, 100, LOOP_ONCE);
                    }
                    break;

                case IMG_FLUSH_RES_DL_QUERIED_NEW: {
                        TY_GUI_LOG_PRINT("[%s:%d]:------resource dl queried new !\r\n", __func__, __LINE__);
                        img_direct_flush_resource_dl_display(img_flush_res_dl_stage, NULL);
                    #ifdef TUYA_GUI_RESOURCE_DOWNLAOD
                        tfm_kepler_gui_files_download_action(TRUE);
                    #else
                        TY_GUI_LOG_PRINT("[%s:%d]:------resource dl query new fail, unsupport resource dl ?\r\n", __func__, __LINE__);
                    #endif
                    }
                    break;

                case IMG_FLUSH_RES_DL_QUERIED_NO_NEW: {
                        TY_GUI_LOG_PRINT("[%s:%d]:------resource dl queried no new !\r\n", __func__, __LINE__);
                        img_direct_flush_resource_dl_display(img_flush_res_dl_stage, NULL);
                        tal_workq_stop_delayed(d_img_resource_dl_work);
                        tal_workq_start_delayed(d_img_resource_dl_work, IMG_FLUSH_RES_DL_DELAY_MS, LOOP_ONCE);
                    }
                    break;

                case IMG_FLUSH_RES_DL_START: {
                        TY_GUI_LOG_PRINT("[%s:%d]:Progress ------ Total: %02d/%02d, Progress: %03d%%. \r\n", __func__, __LINE__,
                                    img_flush_res_dl_progress->files_dl_count, img_flush_res_dl_progress->files_num, img_flush_res_dl_progress->dl_prog);
                        img_direct_flush_resource_dl_display(img_flush_res_dl_stage, (VOID *)img_flush_res_dl_progress);
                    }
                    break;

                case IMG_FLUSH_RES_DL_FAIL: {
                        TY_GUI_LOG_PRINT("[%s:%d]:------resource dl evt 'resource dl end fail' !\r\n", __func__, __LINE__);
                        img_direct_flush_resource_dl_display(img_flush_res_dl_stage, NULL);
                        tal_workq_stop_delayed(d_img_resource_dl_work);
                        tal_workq_start_delayed(d_img_resource_dl_work, IMG_FLUSH_RES_DL_DELAY_MS, LOOP_ONCE);
                    }
                    break;

                case IMG_FLUSH_RES_DL_SUCCESS: {
                        TY_GUI_LOG_PRINT("[%s:%d]:------resource dl evt 'resource dl end successful' !\r\n", __func__, __LINE__);
                        img_direct_flush_resource_dl_display(img_flush_res_dl_stage, NULL);
                        tal_workq_stop_delayed(d_img_resource_dl_work);
                        tal_workq_start_delayed(d_img_resource_dl_work, IMG_FLUSH_RES_DL_DELAY_MS, LOOP_ONCE);
                    }
                    break;

                default:
                    break;
            }
        }
    }
}

STATIC OPERATE_RET img_direct_flush_resource_dl_task_create(VOID)
{
    OPERATE_RET rt = OPRT_COM_ERROR;
    THREAD_CFG_T thrd_cfg;
    THREAD_HANDLE task_handle = NULL;

    rt = tal_semaphore_create_init(&img_flush_dl_sem, 0, IMG_FLUSH_DL_SEM_CNT);
    if (rt != OPRT_OK) {
        TY_GUI_LOG_PRINT("[%s:%d]:------tal_semaphore_create_init err:%d\r\n", __func__, __LINE__, rt);
        return rt;
    }

    thrd_cfg.priority = THREAD_PRIO_1;
    thrd_cfg.stackDepth = 6*1024;
    thrd_cfg.thrdname = "flush_dl_task";
    TUYA_CALL_ERR_RETURN(tal_thread_create_and_start(&task_handle, NULL, NULL, img_direct_flush_resource_dl_task, NULL, &thrd_cfg));
    return OPRT_OK;
}

STATIC VOID_T img_direct_flush_PwrOnPage_backlight_on(VOID)
{
    static BOOL_T once_bl_on = FALSE;
    if (PwrOnPageQuickStart && !once_bl_on) {
        TY_GUI_LOG_PRINT("[%s:%d]:------power on page backlight on start...\r\n", __func__, __LINE__);
        tuya_disp_lcd_backlight_open();
        once_bl_on = TRUE;
    }
}

STATIC VOID_T img_direct_partial_flush_async_ready_notify(VOID *data)
{
    tkl_mutex_lock(d_img_flush_mutex);
    if (PwrOnPageFrameRowOff < screen_height) {
        if ((screen_height-PwrOnPageFrameRowOff) >= PwrOnPageBufRows) {
            if (PwrOnPageFlushStop > 1)     //黑屏处理
                memset(PwrOnPageBuf, 0, PwrOnPageBufSize);
            else
                memcpy(PwrOnPageBuf, PwrOnPageFrame+(PwrOnPageFrameRowOff*screen_width*screen_bytes_per_pixel), PwrOnPageBufSize);
            d_img_flush_frame = PwrOnPageBuf;
            tkl_lvgl_display_frame_request(0, PwrOnPageFrameRowOff, screen_width, PwrOnPageBufRows, d_img_flush_frame, FRAME_REQ_TYPE_BOOTON_PAGE);
            PwrOnPageFrameRowOff += PwrOnPageBufRows;
        }
        else {
            if (PwrOnPageFlushStop > 1)     //黑屏处理
                memset(PwrOnPageBuf, 0, (screen_height-PwrOnPageFrameRowOff)*screen_width*screen_bytes_per_pixel);
            else
                memcpy(PwrOnPageBuf, PwrOnPageFrame+(PwrOnPageFrameRowOff*screen_width*screen_bytes_per_pixel), (screen_height-PwrOnPageFrameRowOff)*screen_width*screen_bytes_per_pixel);
            d_img_flush_frame = PwrOnPageBuf;
            tkl_lvgl_display_frame_request(0, PwrOnPageFrameRowOff, screen_width, screen_height-PwrOnPageFrameRowOff, d_img_flush_frame, FRAME_REQ_TYPE_BOOTON_PAGE);
            PwrOnPageFrameRowOff = screen_height;
        }
    }
    else {
        img_direct_flush_PwrOnPage_backlight_on();  //启动页面一帧刷新完成,背光点亮!
        if (PwrOnPageFlushStop > 0) {               //最后一帧刷新完成,主页面启动完成,停止刷新!
            if (PwrOnPageFlushStop == 1) {          //防止最后一帧启动页面切换到主页闪屏,第一次等待做黑屏处理
                PwrOnPageFrameRowOff = 0;
                memset(PwrOnPageBuf, 0, PwrOnPageBufSize);
                d_img_flush_frame = PwrOnPageBuf;
                tkl_lvgl_display_frame_request(0, 0, screen_width, PwrOnPageBufRows, d_img_flush_frame, FRAME_REQ_TYPE_BOOTON_PAGE);
                PwrOnPageFrameRowOff += PwrOnPageBufRows;
                PwrOnPageFlushStop++;
            }
            else
                tal_semaphore_post(PwrOnPageFrameSem);
        }
        else                                //整帧刷屏完成后继续启动下一帧!
            tal_workq_start_delayed(d_img_flush_work, d_img_flush_interval, LOOP_ONCE);
    }
    tkl_mutex_unlock(d_img_flush_mutex);
}

STATIC VOID_T img_direct_full_flush_async_ready_notify(VOID *data)
{
    if (PwrOnPageQuickStart && PwrOnPageFlushStop > 1) {
        TY_GUI_LOG_PRINT("[%s:%d]:------last full frame tx completed...\r\n", __func__, __LINE__);
        tal_semaphore_post(PwrOnPageFrameSem);
    }
}

STATIC VOID_T img_direct_partial_flush_start(UCHAR_T *frame_data)
{
    PwrOnPageFrame = frame_data;
    PwrOnPageFrameRowOff = 0;
    memcpy(PwrOnPageBuf, PwrOnPageFrame, PwrOnPageBufSize);
    d_img_flush_frame = PwrOnPageBuf;
    tkl_lvgl_display_frame_request(0, 0, screen_width, PwrOnPageBufRows, d_img_flush_frame, FRAME_REQ_TYPE_BOOTON_PAGE);
    PwrOnPageFrameRowOff += PwrOnPageBufRows;
}

STATIC VOID_T img_direct_flush_work(VOID_T *data)
{
    BOOL_T partial_flush = FALSE;

    if (img_flush_res_dl_stage == IMG_FLUSH_RES_DL_NOTHING) {
        tkl_mutex_lock(d_img_flush_mutex);
        if (PwrOnPageQuickStart) {
            if (PwrOnPageBuf) {
                if (PwrOnPageBufRows == screen_height) {        //full frame flush !
                    if (PwrOnPageFlushStop > 0) {               //stopping!
                        if (PwrOnPageFlushStop > 1) {
                            tkl_mutex_unlock(d_img_flush_mutex);
                            return;
                        }
                        memset(PwrOnPageBuf, 0, PwrOnPageBufSize);  // black screen at last time!
                        PwrOnPageFlushStop++;
                    }
                    else
                        memcpy(PwrOnPageBuf, d_img_flush_data[d_img_flush_offset++], PwrOnPageBufSize);
                    d_img_flush_frame = PwrOnPageBuf;
                }
                else {                                          //partial frame flush !
                    partial_flush = TRUE;
                }
            }
            else {
                tkl_mutex_unlock(d_img_flush_mutex);
                return;
            }
        }
        else
            d_img_flush_frame = d_img_flush_data[d_img_flush_offset++];

        if (partial_flush)
            img_direct_partial_flush_start(d_img_flush_data[d_img_flush_offset++]);
        else {
            tkl_lvgl_display_frame_request(0, 0, 0, 0, d_img_flush_frame, (PwrOnPageQuickStart==TRUE)?FRAME_REQ_TYPE_BOOTON_PAGE:FRAME_REQ_TYPE_IMG_DIR_FLUSH);
            img_direct_flush_PwrOnPage_backlight_on();  //启动页面一帧刷新完成,背光点亮!
        }
        d_img_flush_offset = (d_img_flush_offset % d_img_flush_num);
        tkl_mutex_unlock(d_img_flush_mutex);
        if (img_flush_stopped)
            img_flush_stopped = FALSE;
    }
    else {
        if (!img_flush_stopped)
            img_flush_stopped = TRUE;
    }
}

OPERATE_RET tuya_img_direct_flush_init(BOOL_T all_init)
{
    OPERATE_RET op_ret = OPRT_COM_ERROR;
    STATIC BOOL_T partial_inited = FALSE;
    STATIC BOOL_T dir_all_inited = FALSE;

    if (dir_all_inited) {
        TY_GUI_LOG_PRINT("%s %d : ignore img direct flush *re-inited* !\r\n", __func__, __LINE__);
        return OPRT_OK;
    }

    //刷图资源分配!
    if (partial_inited) {
        TY_GUI_LOG_PRINT("%s %d : ignore partial img direct flush *re-inited* !\r\n", __func__, __LINE__);
    }
    else {
        op_ret = tkl_mutex_create_init(&d_img_flush_mutex);
        if (op_ret != OPRT_OK) {
            TY_GUI_LOG_PRINT("[%s:%d]:------img flush mutex init fail ???\r\n", __func__, __LINE__);
            return op_ret;
        }

        op_ret = tal_workq_init_delayed(WORKQ_HIGHTPRI, img_direct_flush_work, NULL, &d_img_flush_work);
        if (OPRT_OK != op_ret) {
            TY_GUI_LOG_PRINT("[%s:%d]:------img flush work q create fail '%d'!", __func__, __LINE__, op_ret);
            return op_ret;
        }
    }
    partial_inited = TRUE;          //局部刷图资源初始化完成!
    if (!all_init) {
        TY_GUI_LOG_PRINT("%s %d : partial img direct flush *inited* !\r\n", __func__, __LINE__);
        return OPRT_OK;
    }

    TY_GUI_LOG_PRINT("%s %d : all img direct flush init start!\r\n", __func__, __LINE__);

    //刷图过程升级资源分配!
    img_flush_res_dl_progress = (LV_RESOURCE_DL_EVENT_SS *)tal_malloc(sizeof(LV_RESOURCE_DL_EVENT_SS));
    if (img_flush_res_dl_progress == NULL) {
        TY_GUI_LOG_PRINT("[%s:%d]:------img flush malloc fail ???\r\n", __func__, __LINE__);
        return op_ret;
    }

    op_ret = tal_workq_init_delayed(WORKQ_HIGHTPRI, img_direct_flush_resource_dl_work, NULL, &d_img_resource_dl_work);
    if (OPRT_OK != op_ret) {
        TY_GUI_LOG_PRINT("[%s:%d]:------img resource dl work q create fail '%d'!", __func__, __LINE__, op_ret);
        return op_ret;
    }

    op_ret = img_direct_flush_resource_dl_task_create();
    if (OPRT_OK != op_ret) {
        TY_GUI_LOG_PRINT("[%s:%d]:------img flush dl task create fail '%d'!", __func__, __LINE__, op_ret);
        return op_ret;
    }

    tuya_app_gui_query_resource_event_cb_register(img_direct_flush_resource_dl_event_callback);
    dir_all_inited = TRUE;

    return op_ret;
}

OPERATE_RET tuya_img_direct_flush_screen_info_set(INT32_T width, INT32_T height, TKL_DISP_ROTATION_E info_rotate)
{
    UCHAR_T bytes_per_pixel = 2;

    #if (LV_COLOR_DEPTH == 16)
        bytes_per_pixel = 2;
    #elif (LV_COLOR_DEPTH == 24)
        bytes_per_pixel = 3;
    #elif (LV_COLOR_DEPTH == 32)
        bytes_per_pixel = 4;
    #endif

    screen_width = width;
    screen_height = height;
    screen_bytes_per_pixel = bytes_per_pixel;
    dl_progerss_rotate_angle = info_rotate;
    if (dl_progerss_rotate_angle != TKL_DISP_ROTATION_90 && dl_progerss_rotate_angle != TKL_DISP_ROTATION_180   \
        && dl_progerss_rotate_angle != TKL_DISP_ROTATION_270)
        dl_progerss_rotate_angle = TKL_DISP_ROTATION_0;
    return OPRT_OK;
}

OPERATE_RET tuya_img_direct_flush_screen_info_get(INT32_T *width, INT32_T *height, UCHAR_T *bytes_per_pixel)
{
    *width = screen_width;
    *height = screen_height;
    *bytes_per_pixel = screen_bytes_per_pixel;
    return OPRT_OK;
}

VOID tuya_img_direct_flush_rgb565_swap(UCHAR_T *img_bin)
{
    tkl_lvgl_display_pixel_rgb_convert(img_bin, screen_width, screen_height);
}

OPERATE_RET tuya_img_direct_flush(UCHAR_T **img_bin, UINT32_T img_num, UINT32_T interval_ms)
{
    OPERATE_RET ret = OPRT_OK;

    if (d_img_flush_work == NULL) {
        TY_GUI_LOG_PRINT("[%s:%d]:------img flush init fail ???\r\n", __func__, __LINE__);
        return OPRT_COM_ERROR;
    }

    if (img_bin == NULL) {
        TY_GUI_LOG_PRINT("[%s:%d]:------input flush img bin error ???\r\n", __func__, __LINE__);
        return OPRT_COM_ERROR;
    }
    if (img_num == 0) {
        TY_GUI_LOG_PRINT("[%s:%d]:------input flush img num error ???\r\n", __func__, __LINE__);
        return OPRT_COM_ERROR;
    }
    if (interval_ms == 0) {
        TY_GUI_LOG_PRINT("[%s:%d]:------input flush interval error ???\r\n", __func__, __LINE__);
        return OPRT_COM_ERROR;
    }

    if (d_img_flush_data != img_bin || d_img_flush_num != img_num || d_img_flush_interval != interval_ms) {      //new img flush start!
        TY_GUI_LOG_PRINT("[%s:%d]:------new img flush start, num '%d', interval '%d'ms\r\n", __func__, __LINE__, img_num, interval_ms);
        tkl_mutex_lock(d_img_flush_mutex);
        d_img_flush_data = img_bin;
        d_img_flush_num = img_num;
        d_img_flush_interval = (interval_ms >= 10)?interval_ms:10;      //不要少于10ms!
        d_img_flush_offset = 0;
        tal_workq_stop_delayed(d_img_flush_work);
        if (PwrOnPageQuickStart && (PwrOnPageBufRows < screen_height)) {        //启动页面局部刷先仅运行一次!
            tkl_PwrOnPage_disp_flush_async_register(img_direct_partial_flush_async_ready_notify, NULL);
            tal_workq_start_delayed(d_img_flush_work, d_img_flush_interval, LOOP_ONCE);
        }
        else {
            if (PwrOnPageQuickStart)
                tkl_PwrOnPage_disp_flush_async_register(img_direct_full_flush_async_ready_notify, NULL);
            tal_workq_start_delayed(d_img_flush_work, d_img_flush_interval, (img_num > 1)?LOOP_CYCLE:LOOP_ONCE);
        }
        tkl_mutex_unlock(d_img_flush_mutex);
    }
    return ret;
}

//查询平台资源更新信息:由平台下发dp或AI指令触发!
OPERATE_RET tuya_img_direct_flush_query_resource_update(TKL_DISP_ROTATION_E notification_rotate)
{
    if (d_img_resource_dl_work == NULL) {
        TY_GUI_LOG_PRINT("[%s:%d]:------img flush resoure dl init fail ???\r\n", __func__, __LINE__);
        return OPRT_COM_ERROR;
    }

    dl_progerss_rotate_angle = notification_rotate;
    if (dl_progerss_rotate_angle != TKL_DISP_ROTATION_90 && dl_progerss_rotate_angle != TKL_DISP_ROTATION_180   \
        && dl_progerss_rotate_angle != TKL_DISP_ROTATION_270)
        dl_progerss_rotate_angle = TKL_DISP_ROTATION_0;

    if (img_flush_res_dl_stage == IMG_FLUSH_RES_DL_NOTHING) {
        img_flush_res_dl_stage = IMG_FLUSH_RES_DL_QUERYING;
        tal_semaphore_post(img_flush_dl_sem);
    }
    else {
        TY_GUI_LOG_PRINT("[%s:%d]:------ignore repeatedly img flush resoure dl req !!!\r\n", __func__, __LINE__);
    }
    return OPRT_OK;
}

#ifndef LV_DRAW_BUF_ALIGN
#define LV_DRAW_BUF_ALIGN                       4
#endif
static UCHAR_T * tuya_img_direct_flush_poweronpage_buf_align_alloc(size_t size_bytes, UCHAR_T **unaligned_hd)
{
    UCHAR_T *buf_u8= NULL;
    /*Allocate larger memory to be sure it can be aligned as needed*/
    size_bytes += LV_DRAW_BUF_ALIGN - 1;
    buf_u8 = tkl_system_psram_malloc(size_bytes);
    if (buf_u8) {
        *unaligned_hd = buf_u8;
        memset(buf_u8, 0x00, size_bytes);
        //TY_GUI_LOG_PRINT("%s-%d: original buf ptr '%p'\r\n", __func__, __LINE__, (void *)buf_u8);
        buf_u8 += LV_DRAW_BUF_ALIGN - 1;
        buf_u8 = (UCHAR_T *)((UINT_T) buf_u8 & ~(LV_DRAW_BUF_ALIGN - 1));
        //TY_GUI_LOG_PRINT("%s-%d: aligned buf ptr '%p'\r\n", __func__, __LINE__, (void *)buf_u8);
    }
    else {
        TY_GUI_LOG_PRINT("%s-%d: malloc frame buf fail ???\r\n", __func__, __LINE__);
    }
    return buf_u8;
}

//上电快速页面启动是否开启!
BOOL_T tuya_img_direct_flush_poweronpage_started(VOID)
{
    return PwrOnPageQuickStart;
}

//上电快速页面启动开始!
OPERATE_RET tuya_img_direct_flush_poweronpage_start(UCHAR_T **img_bin, UINT32_T img_num, UINT32_T interval_ms)
{
    extern unsigned int tuya_app_gui_lvgl_draw_buffer_rows(void);
    extern unsigned char tuya_app_gui_lvgl_frame_buffer_count(void);
    OPERATE_RET rt = OPRT_COM_ERROR;
    UINT16_T lcd_hor_res = 0;         /**< Horizontal resolution.*/
    UINT16_T lcd_ver_res = 0;         /**< Vertical resolution.*/
    UCHAR_T bytes_per_pixel = 2;

    #if (LV_COLOR_DEPTH == 16)
        bytes_per_pixel = 2;
    #elif (LV_COLOR_DEPTH == 24)
        bytes_per_pixel = 3;
    #elif (LV_COLOR_DEPTH == 32)
        bytes_per_pixel = 4;
    #endif

    if (img_bin == NULL || img_num == 0) {
        TY_GUI_LOG_PRINT("[%s:%d]:------empty poweronpage input ???\r\n", __func__, __LINE__);
        return rt;
    }
    rt = tkl_lvgl_display_resolution(&lcd_hor_res, &lcd_ver_res);
    if (rt != OPRT_OK) {
        return rt;
    }

    rt = tkl_lvgl_display_frame_init(lcd_hor_res, lcd_ver_res);
    if (rt != OPRT_OK) {
        TY_GUI_LOG_PRINT("%s:%d- tkl_lvgl_display_frame init fail ????\r\n", __func__, __LINE__);
        return rt;
    }
    tuya_img_direct_flush_screen_info_set(lcd_hor_res, lcd_ver_res, TKL_DISP_ROTATION_0);
    rt = tuya_img_direct_flush_init(FALSE);
    if (rt != OPRT_OK) {
        TY_GUI_LOG_PRINT("[%s:%d]:------img direct flush init fail ???\r\n", __func__, __LINE__);
        return rt;
    }
    //for (i = 0; i<img_num; i++) {
        //tuya_img_direct_flush_rgb565_swap((UCHAR_T *)img_bin[i]); //静态图片保存在flash代码空间中,无法写操作进行RGB交换!!!!!
    //}

    if (tuya_app_gui_lvgl_frame_buffer_count() == 0) {          //无帧缓存,仅支持spi/qspi等屏幕类型的局部刷新!
        PwrOnPageBufRows = tuya_app_gui_lvgl_draw_buffer_rows();
        if (PwrOnPageBufRows == 0) {
            PwrOnPageBufRows = (screen_height / 10);     //1/10 屏幕高度区域
        }
        else if (PwrOnPageBufRows >= screen_height)
            PwrOnPageBufRows = screen_height;
    }
    else
        PwrOnPageBufRows = lcd_ver_res;

    rt = tal_semaphore_create_init(&PwrOnPageFrameSem, 0, 1);
    if (rt != OPRT_OK) {
        TY_GUI_LOG_PRINT("[%s:%d]:------img direct flush sem create fail ???\r\n", __func__, __LINE__);
        return rt;
    }

    TY_GUI_LOG_PRINT("[%s:%d]:------poweron page buff rows: '%d' \r\n", __func__, __LINE__, PwrOnPageBufRows);
    PwrOnPageBufSize = lcd_hor_res * PwrOnPageBufRows * bytes_per_pixel;
    PwrOnPageBuf = tuya_img_direct_flush_poweronpage_buf_align_alloc(PwrOnPageBufSize, &PwrOnPageBufUnaligned);
    if (PwrOnPageBuf == NULL) {
        TY_GUI_LOG_PRINT("[%s:%d]:------img direct flush malloc fail ???\r\n", __func__, __LINE__);
        return OPRT_MALLOC_FAILED;
    }

    PwrOnPageQuickStart = TRUE;
    rt = tuya_img_direct_flush(img_bin, img_num, interval_ms);
    if (rt != OPRT_OK) {
        TY_GUI_LOG_PRINT("[%s:%d]:------img direct flush fail ???\r\n", __func__, __LINE__);
        PwrOnPageQuickStart = FALSE;
        return rt;
    }
    return rt;
}

//上电快速页面启动退出!
OPERATE_RET tuya_img_direct_flush_poweronpage_exit(VOID)
{
    OPERATE_RET rt = OPRT_COM_ERROR;

    TY_GUI_LOG_PRINT("[%s:%d]:------exit poweronpage !\r\n", __func__, __LINE__);
    if (PwrOnPageQuickStart) {
        PwrOnPageFlushStop = 1;
        if (PwrOnPageBufRows < screen_height) {     //wait a frame flush stop!
            if (PwrOnPageFrameSem) {
                rt = tal_semaphore_wait(PwrOnPageFrameSem, 3000);
                TY_GUI_LOG_PRINT("[%s:%d]:------partial black screen %s !\r\n", __func__, __LINE__, (rt == OPRT_OK)?"completed":"fail");
            }
        }
        else {      //整帧黑屏处理!
            if (d_img_flush_num > 1) {          //多启动页面!
                if (PwrOnPageFrameSem) {
                    rt = tal_semaphore_wait(PwrOnPageFrameSem, 3000);
                }
            }
            else {          //单启动页面!
                PwrOnPageFlushStop++;
                memset(PwrOnPageBuf, 0, PwrOnPageBufSize);
                d_img_flush_frame = PwrOnPageBuf;
                tkl_lvgl_display_frame_request(0, 0, 0, 0, d_img_flush_frame, FRAME_REQ_TYPE_BOOTON_PAGE);
                rt = tal_semaphore_wait(PwrOnPageFrameSem, 3000);
            }
            TY_GUI_LOG_PRINT("[%s:%d]:------full black screen %s !\r\n", __func__, __LINE__, (rt == OPRT_OK)?"completed":"fail");
        }
        tkl_mutex_lock(d_img_flush_mutex);
        if (PwrOnPageFrameSem) {
            tal_semaphore_release(PwrOnPageFrameSem);
            PwrOnPageFrameSem = NULL;
        }
        if (PwrOnPageBufUnaligned)
            tkl_system_psram_free(PwrOnPageBufUnaligned);
        tkl_PwrOnPage_disp_flush_async_register(NULL, NULL);
        PwrOnPageBufUnaligned = NULL;
        PwrOnPageBuf = NULL;
        PwrOnPageBufSize = 0;
        PwrOnPageBufRows = 0;
        PwrOnPageFrameRowOff = 0;
        tkl_mutex_unlock(d_img_flush_mutex);

    #if defined(TUYA_IMG_DIRECT_FLUSH) && (TUYA_IMG_DIRECT_FLUSH == 1)
        //停止当前上电开机页面播放!
        if (d_img_flush_num > 1) {

        }
    #else
        //停止当前上电开机页面播放,同时清除刷图资源!
        TY_GUI_LOG_PRINT("[%s:%d]:------start img direct flush deinit !\r\n", __func__, __LINE__);
        if (d_img_flush_work) {
            tal_workq_cancel_delayed(d_img_flush_work);
        }
        d_img_flush_work = NULL;
        if (d_img_flush_mutex)
            tkl_mutex_release(d_img_flush_mutex);
        d_img_flush_mutex = NULL;
    #endif
        PwrOnPageQuickStart = FALSE;
    }
    return OPRT_OK;
}
