#include <stdio.h>
#include <string.h>
#include "tuya_port_dma2d.h"
#include "tal_thread.h"
#include "tal_mutex.h"
#include "tal_queue.h"
#include "tal_semaphore.h"

#ifdef TUYA_DMA2D_SHARE

#define QUEUE_WAIT_FROEVER 0xFFFFFFFF
#define DMA2D_QUEUE_NUM 8

STATIC QUEUE_HANDLE ty_dma2d_queue = NULL;
STATIC SEM_HANDLE ty_dma2d_sem = NULL;
STATIC SEM_HANDLE ty_dma2d_lvgl_sem = NULL;                             //lvgl flush dma2d completed notity sem
STATIC SEM_HANDLE ty_dma2d_yuv_sem = NULL;                              //camera yuv422 convert dma2d completed notity sem
STATIC MUTEX_HANDLE ty_dma2d_mutex = NULL;
STATIC THREAD_HANDLE ty_dma2d_thrd_hdl = NULL;
STATIC volatile UINT_T ty_dma2d_busy = 0;
STATIC volatile BOOL_T ty_dma2d_lvgl_flush_doing = FALSE;               //lvgl-flush doing
STATIC volatile BOOL_T ty_dma2d_yuv_convert_doing = FALSE;              //camera convert donig
STATIC volatile BOOL_T ty_dma2d_exit = FALSE;

STATIC tuya_dma2d_complete_cb_t ty_dma2d_lvgl_flush_completed = NULL;
STATIC tuya_dma2d_complete_cb_t ty_dma2d_yuv_convert_completed = NULL;

STATIC void tuya_dma2d_completed_cb(TUYA_DMA2D_IRQ_E type, VOID_T *args)
{
    if (type == TUYA_DMA2D_TRANS_ERROR_ISR) {
        PR_ERR("%s: dma2d trans error\n", __func__);
    }
    tal_semaphore_post(ty_dma2d_sem);
}

static CONST TUYA_DMA2D_BASE_CFG_T ty_dma2d_cfg = {
    .cb = tuya_dma2d_completed_cb,
    .arg = NULL
};

STATIC VOID_T tuya_dma2d_thread(VOID_T *arg) 
{
    TY_DMA2D_Task_T dma2d_task;
    OPERATE_RET rt = OPRT_OK;
    
    for (;;) {
        rt  = tal_queue_fetch(ty_dma2d_queue, &dma2d_task, QUEUE_WAIT_FROEVER);
        if (rt == OPRT_OK && ty_dma2d_exit != TRUE) {
            tal_mutex_lock(ty_dma2d_mutex);
            //PR_NOTICE("[%s][%d]---------task type = %d\r\n",  __func__, __LINE__, dma2d_task.type);
            switch (dma2d_task.type){
                case TASK_TYPE_LVGL_FLUSH:
                    ty_dma2d_busy |= TY_DMA2D_LVGL_FLUSH_BUSY;
                    tkl_dma2d_memcpy(&(dma2d_task.in_frame), &(dma2d_task.out_frame));
                    break;

                case TASK_TYPE_CAMERA_CONVERT:
                    ty_dma2d_busy |= TY_DMA2D_CAM_CONVERT_BUSY;
                    tkl_dma2d_convert(&(dma2d_task.in_frame), &(dma2d_task.out_frame));
                    break;

                default:
                    PR_ERR("%s: unknown dma2d task type '%d'\n", __func__, dma2d_task.type);
                    break;
            }
            tal_semaphore_wait_forever(ty_dma2d_sem);
            if (dma2d_task.type == TASK_TYPE_LVGL_FLUSH) {
                ty_dma2d_lvgl_flush_doing = FALSE;
                tal_semaphore_post(ty_dma2d_lvgl_sem);
                if (ty_dma2d_lvgl_flush_completed)
                    ty_dma2d_lvgl_flush_completed(&dma2d_task);
            }
            else if (dma2d_task.type == TASK_TYPE_CAMERA_CONVERT) {
                ty_dma2d_yuv_convert_doing = FALSE;
                tal_semaphore_post(ty_dma2d_yuv_sem);
                if (ty_dma2d_yuv_convert_completed)
                    ty_dma2d_yuv_convert_completed(&dma2d_task);
            }
            ty_dma2d_busy = 0;
            tal_mutex_unlock(ty_dma2d_mutex);
        }
    }
}

OPERATE_RET tuya_dma2d_init(VOID)
{
    STATIC BOOL_T dma2d_inited = FALSE;
    OPERATE_RET ret = OPRT_COM_ERROR;

    if (dma2d_inited)
        return OPRT_OK;

    ret = tkl_dma2d_init(&ty_dma2d_cfg);
    if (ret != OPRT_OK) {
        PR_ERR("[%s:%d]:------tuya TKL DMA2D init fail ???\n", __func__);
        return ret;
    }

    ret = tal_semaphore_create_init(&ty_dma2d_sem, 0, 1);
    if (OPRT_OK != ret) {
        PR_ERR("[%s:%d]:------sem create init failed ???\n", __func__, __LINE__);
        return ret;
    }

    ret = tal_semaphore_create_init(&ty_dma2d_lvgl_sem, 0, 1);
    if (OPRT_OK != ret) {
        PR_ERR("[%s:%d]:------sem create init failed ???\n", __func__, __LINE__);
        return ret;
    }

    ret = tal_semaphore_create_init(&ty_dma2d_yuv_sem, 0, 1);
    if (OPRT_OK != ret) {
        PR_ERR("[%s:%d]:------sem create init failed ???\n", __func__, __LINE__);
        return ret;
    }

    ret = tal_mutex_create_init(&ty_dma2d_mutex);
    if (ret != OPRT_OK) {
        PR_ERR("[%s:%d]:------mutex create init fail ???\r\n", __func__, __LINE__);
        return ret;
    }

    ret = tal_queue_create_init(&ty_dma2d_queue, SIZEOF(TY_DMA2D_Task_T), DMA2D_QUEUE_NUM);
    if (ret != OPRT_OK) {
        PR_ERR("[%s:%d]:------queue create fail ???\r\n", __func__, __LINE__);
        return ret;
    }

    THREAD_CFG_T thrd_cfg = {
        .priority = THREAD_PRIO_0,
        .thrdname = "ty_dma2d",
        .stackDepth = 4 * 1024,
    #if defined(ENABLE_EXT_RAM) && ENABLE_EXT_RAM == 1
        .psram_mode = 1,
    #endif
    };

    if (ty_dma2d_thrd_hdl == NULL) {
        ret = tal_thread_create_and_start(&ty_dma2d_thrd_hdl, NULL, NULL, tuya_dma2d_thread, NULL, &thrd_cfg);
        if (ret != OPRT_OK) {
            PR_ERR("[%s:%d]:------thread create fail ???\r\n", __func__, __LINE__);
            return ret;
        }
    }
    ty_dma2d_exit = FALSE;
    dma2d_inited = TRUE;
    PR_NOTICE("%s: tuya share DMA2D enabled!\n", __func__);
    return ret;
}

OPERATE_RET tuya_dma2d_request(DMA2D_TaskType type, TKL_DMA2D_FRAME_INFO_T *in_frame, TKL_DMA2D_FRAME_INFO_T *out_frame)
{
    OPERATE_RET op_ret = OPRT_COM_ERROR;
    TY_DMA2D_Task_T dma2d_task =  { 0 };

    if ((type != TASK_TYPE_LVGL_FLUSH) && (type != TASK_TYPE_CAMERA_CONVERT)) {
        PR_ERR("[%s:%d]:------unknown dma2d req type '%d' !\r\n", __func__, __LINE__, type);
        return op_ret;
    }
    else if ((type == TASK_TYPE_LVGL_FLUSH && ty_dma2d_lvgl_flush_doing == TRUE)/* || 
        (type == TASK_TYPE_CAMERA_CONVERT && ty_dma2d_yuv_convert_doing == TRUE)*/) {
        op_ret = tuya_dma2d_wait_finish(type);
    }

    dma2d_task.type = type;
    memcpy((VOID *)(&dma2d_task.in_frame), (VOID *)in_frame, SIZEOF(TKL_DMA2D_FRAME_INFO_T));
    memcpy((VOID *)(&dma2d_task.out_frame), (VOID *)out_frame, SIZEOF(TKL_DMA2D_FRAME_INFO_T));
    op_ret = tal_queue_post(ty_dma2d_queue, &dma2d_task, 0);
    if (op_ret != OPRT_OK) {
        PR_ERR("[%s:%d]:------queue fail, ret '%d' !\r\n", __func__, __LINE__, op_ret);
    }
    else {
        if (type == TASK_TYPE_LVGL_FLUSH)
            ty_dma2d_lvgl_flush_doing = TRUE;
        else if (type == TASK_TYPE_CAMERA_CONVERT)
            ty_dma2d_yuv_convert_doing = TRUE;
    }
    return op_ret;
}

BOOL_T tuya_dma2d_is_busy(VOID)
{
    return (ty_dma2d_busy > 0)?TRUE:FALSE;
}

OPERATE_RET tuya_dma2d_wait_finish(DMA2D_TaskType type)
{
    OPERATE_RET op_ret = OPRT_OK;

    if (type == TASK_TYPE_LVGL_FLUSH) {
        if ((ty_dma2d_busy & TY_DMA2D_LVGL_FLUSH_BUSY) != 0 || ty_dma2d_lvgl_flush_doing == TRUE) {
            op_ret = tal_semaphore_wait(ty_dma2d_lvgl_sem, 1000);
            if (op_ret != OPRT_OK) {
                PR_ERR("[%s:%d]:------sem wait fail, ret '%d' !\r\n", __func__, __LINE__, op_ret);
            }
        }
    }
    else if (type == TASK_TYPE_CAMERA_CONVERT) {
        if ((ty_dma2d_busy & TY_DMA2D_CAM_CONVERT_BUSY) != 0 || ty_dma2d_yuv_convert_doing == TRUE) {
            op_ret = tal_semaphore_wait(ty_dma2d_yuv_sem, 1000);
            if (op_ret != OPRT_OK) {
                PR_ERR("[%s:%d]:------sem wait fail, ret '%d' !\r\n", __func__, __LINE__, op_ret);
            }
        }
    }
    return op_ret;
}

VOID tuya_dma2d_deinit(VOID)
{
    PR_NOTICE("%s: start !\n", __func__);
    ty_dma2d_exit = TRUE;
    while (tuya_dma2d_is_busy()) {
        tal_system_sleep(10);
    }
    tkl_dma2d_deinit();
    if (ty_dma2d_sem != NULL)
        tal_semaphore_release(ty_dma2d_sem);
    ty_dma2d_sem = NULL;

    if (ty_dma2d_lvgl_sem != NULL)
        tal_semaphore_release(ty_dma2d_lvgl_sem);
    ty_dma2d_lvgl_sem = NULL;

    if (ty_dma2d_yuv_sem != NULL)
        tal_semaphore_release(ty_dma2d_yuv_sem);
    ty_dma2d_yuv_sem = NULL;

    if (ty_dma2d_mutex != NULL)
        tal_mutex_release(ty_dma2d_mutex);
    ty_dma2d_mutex = NULL;

    if (ty_dma2d_queue != NULL)
        tal_queue_free(ty_dma2d_queue);
    ty_dma2d_queue = NULL;
    PR_NOTICE("%s: end !\n", __func__);
}

OPERATE_RET tuya_dma2d_complete_register_callback(DMA2D_TaskType type, tuya_dma2d_complete_cb_t cb)
{
    OPERATE_RET op_ret = OPRT_OK;

    if (type == TASK_TYPE_LVGL_FLUSH)
        ty_dma2d_lvgl_flush_completed = cb;
    else if (type == TASK_TYPE_CAMERA_CONVERT)
        ty_dma2d_yuv_convert_completed = cb;
    else {
        op_ret = OPRT_INVALID_PARM;
        PR_ERR("%s: unknown type '%d' !\n", __func__, type);
    }
    return op_ret;
}
#endif
