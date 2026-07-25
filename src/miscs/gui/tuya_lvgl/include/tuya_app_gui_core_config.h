#ifndef __TUYA_APP_GUI_CORE_CONFIG_H
#define __TUYA_APP_GUI_CORE_CONFIG_H

#include "tuya_app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(TUYA_MODULE_T5) && (TUYA_MODULE_T5 == 1)
#if defined(TUYA_FILE_SYSTEM) && (TUYA_FILE_SYSTEM == 1)
#define TUYA_GUI_IMG_PRE_DECODE                     //图形预解码
#endif
#endif
#ifdef TUYA_GUI_IMG_PRE_DECODE
#define LVGL_ENABLE_CLOUD_RECIPE                    //支持云食谱
#endif

//音频文件播放支持配置
//#define LVGL_ENABLE_AUDIO_FILE_PLAY               //支持音频文件播放
#ifdef LVGL_ENABLE_AUDIO_FILE_PLAY
//#define AUDIO_HW_INIT_BY_OTHERS                   //有其他应用初始化音频设备(避免存在多个应用初始化音频设备,如AI demo...)
#endif

//#define TUYA_APP_USE_INTERNAL_FLASH_FS

#if defined(TUYA_FILE_SYSTEM) && (TUYA_FILE_SYSTEM == 1)
#define TUYA_GUI_RESOURCE_DOWNLAOD
#endif

#if defined(TUYA_CPU_ARCH_SMP) && (TUYA_CPU_ARCH_SMP == 1)
//大核SMP
#else
//多核核间通讯
#define TUYA_APP_MULTI_CORE_IPC
#endif

#ifdef TUYA_APP_MULTI_CORE_IPC
//#define TUYA_APP_USE_TAL_IPC                      //tal层定义IPC核间通讯
#else
#define TUYA_TKL_THREAD                             //***使用涂鸦tkl层thread接口***
                                                    //1.tkl有些接口在ai产物中tkl_thread_create创建被强制从psram分配,会导致tuya_gui_main异常
                                                    //2.lvgl-v9, lv_freertos.c需要直接调用tkl层任务创建以达到smp的效果
//#define TUYA_MULTI_TYPES_LCD                        //支持多种类型LCD:8080/RGB/SPI/QSPI,etc.(目前仅大核框架支持):移至tuya_app_config.h中配置@2025/8/15
//#define TUYA_APP_DRIVERS_TP                         //支持触控屏应用驱动:移至tuya_app_config.h中配置@2025/8/15
#endif

#if defined(TUYA_LVGL_VERSION) && (TUYA_LVGL_VERSION > 7)   //启用LVGL应用组件
#define LVGL_AS_APP_COMPONENT                               //lvgl上移作为应用组件
#endif

/*Swap the 2 bytes of RGB565 color(BGR565->RGB565). Useful if the display has an 8-bit interface (e.g. SPI)*/
#if defined(RGB565_COLOR_SWAP_LVGL) && (RGB565_COLOR_SWAP_LVGL == 1)
#define LVGL_COLOR_16_SWAP
#endif

/*Swap the 3 bytes of RGB666 color(BGR888->RGB666). Useful if the display has an 8-bit interface (e.g. SPI)*/
#if defined(RGB888_COLOR_SWAP_LVGL) && (RGB888_COLOR_SWAP_LVGL == 1)
#define LVGL_COLOR_24_SWAP
#endif

/*多显示屏幕扩展配置*/
typedef unsigned int TUYA_SCREEN_EXPANSION_TYPE;
#define TUYA_SCREEN_EXPANSION_NONE      0           //无扩展
#define TUYA_SCREEN_EXPANSION_H_EXP     1           //横向扩展
#define TUYA_SCREEN_EXPANSION_H_DUP     2           //横向复制
#define TUYA_SCREEN_EXPANSION_V_EXP     3           //纵向扩展
#define TUYA_SCREEN_EXPANSION_V_DUP     4           //纵向复制

/*原厂JPG解码*/
#if !(defined(TUYA_LIBJPEG_TURBO) && (TUYA_LIBJPEG_TURBO == 1))
#define ENABLE_TUYA_TKL_JPEG_DECODE         //使用tkl层解码api
#endif

/*大核SMP日志重定义*/
#if defined(TUYA_CPU_ARCH_SMP) && (TUYA_CPU_ARCH_SMP == 1)
#include "uni_log.h"
#define TY_GUI_LOG_PRINT PR_DEBUG
#else
#define TY_GUI_LOG_PRINT bk_printf
#endif

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif

