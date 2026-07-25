
#ifndef IMG_DIRECT_FLUSH_H
#define IMG_DIRECT_FLUSH_H

#include "tuya_cloud_types.h"
#include "tuya_app_gui_core_config.h"
#include "tkl_display.h"

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

/*********************
 *      DEFINES
 *********************/
#if defined(TUYA_MODULE_T5) && (TUYA_MODULE_T5 == 1)
#if defined(TUYA_LIBARM2D) && (TUYA_LIBARM2D == 1)
#define ENABLE_IMG_DIRECT_FLUSH_RES_DL_UI
#endif
#endif

typedef enum{
    IMG_FLUSH_RES_DL_NOTHING = 0,           //未进行资源相关操作
    IMG_FLUSH_RES_DL_QUERYING = 1,          //资源查询中
    IMG_FLUSH_RES_DL_QUERIED_NEW = 2,       //查询到新资源
    IMG_FLUSH_RES_DL_QUERIED_NO_NEW = 3,    //未查询到新资源
    IMG_FLUSH_RES_DL_START = 4,             //资源下载中
    IMG_FLUSH_RES_DL_FAIL = 5,              //资源下载失败
    IMG_FLUSH_RES_DL_SUCCESS = 6            //资源下载完成
}IMG_FLUSH_RES_DL_STAGE_E;

OPERATE_RET tuya_img_direct_flush_init(BOOL_T all_init);
OPERATE_RET tuya_img_direct_flush(UCHAR_T **img_bin, UINT32_T img_num, UINT32_T interval_ms);
OPERATE_RET tuya_img_direct_flush_screen_info_set(INT32_T width, INT32_T height, TKL_DISP_ROTATION_E info_rotate);
OPERATE_RET tuya_img_direct_flush_screen_info_get(INT32_T *width, INT32_T *height, UCHAR_T *bytes_per_pixel);
VOID tuya_img_direct_flush_rgb565_swap(UCHAR_T *img_bin);

/**
 * @brief  查询平台资源更新信息:由平台下发dp或AI指令触发!
 *
 * @param[in]   notification_rotate: 更新过程文本提示信息角度
 * 
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tuya_img_direct_flush_query_resource_update(TKL_DISP_ROTATION_E notification_rotate);

//上电快速页面启动是否开启!
BOOL_T tuya_img_direct_flush_poweronpage_started(VOID);
//上电快速页面启动开始!
OPERATE_RET tuya_img_direct_flush_poweronpage_start(UCHAR_T **img_bin, UINT32_T img_num, UINT32_T interval_ms);
//上电快速页面启动退出!
OPERATE_RET tuya_img_direct_flush_poweronpage_exit(VOID);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*IMG_DIRECT_FLUSH_H*/
