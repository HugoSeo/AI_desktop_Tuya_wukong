
#ifndef TUYA_PORT_DMA2D_H
#define TUYA_PORT_DMA2D_H

#include "tuya_cloud_types.h"
#include "tuya_port_disp.h"
#ifdef TUYA_DMA2D_SHARE
#include "tkl_dma2d.h"

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

/*********************
 *      DEFINES
 *********************/
#define TY_DMA2D_LVGL_FLUSH_BUSY    0x00000001     //dma2d进行lvgl-flush中
#define TY_DMA2D_CAM_CONVERT_BUSY   0x00000002     //dma2d进行摄像头转换中

typedef enum {
    TASK_TYPE_LVGL_FLUSH,           // LVGL刷新复制帧缓存
    TASK_TYPE_CAMERA_CONVERT        // camera YUV422转RGB
} DMA2D_TaskType;

typedef struct {
    DMA2D_TaskType type;
    TKL_DMA2D_FRAME_INFO_T in_frame;
    TKL_DMA2D_FRAME_INFO_T out_frame;
}TY_DMA2D_Task_T;

typedef void (*tuya_dma2d_complete_cb_t)(TY_DMA2D_Task_T *dma2d_task);

OPERATE_RET tuya_dma2d_init(VOID);

OPERATE_RET tuya_dma2d_request(DMA2D_TaskType type, TKL_DMA2D_FRAME_INFO_T *in_frame, TKL_DMA2D_FRAME_INFO_T *out_frame);

BOOL_T tuya_dma2d_is_busy(VOID);

OPERATE_RET tuya_dma2d_wait_finish(DMA2D_TaskType type);

VOID tuya_dma2d_deinit(VOID);

OPERATE_RET tuya_dma2d_complete_register_callback(DMA2D_TaskType type, tuya_dma2d_complete_cb_t cb);

#ifdef __cplusplus
} /*extern "C"*/
#endif
#endif
#endif /*TUYA_PORT_DMA2D_H*/
