#ifndef __TUYA_APP_GUI_CONFIG_H
#define __TUYA_APP_GUI_CONFIG_H

#include "tuya_app_config.h"
#include "tuya_device_cfg.h"
#include "tuya_app_gui_core_config.h"
#include "tkl_display.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(ENABLE_TUYA_UI) && (ENABLE_TUYA_UI == 1)
#define ENABLE_TUYA_GUI_DEMO            //启动涂鸦gui演示代码
#endif

//屏幕旋转定义
typedef unsigned int TUYA_SCREEN_ROTATION_TYPE;
#define TUYA_SCREEN_ROTATION_0      0
#define TUYA_SCREEN_ROTATION_90     1
#define TUYA_SCREEN_ROTATION_180    2
#define TUYA_SCREEN_ROTATION_270    3

/*******************************************************
********GUI应用选择: 通过make app_menuconfig配置********
********************************************************/
#if defined(TUYA_AI_TOY_DEMO) && TUYA_AI_TOY_DEMO == 1
#define ENABLE_TUYA_AI_DEMO             //启动涂鸦AI演示代码
//#pragma message("***************** ENABLE_TUYA_AI_DEMO *****************")
#elif  defined(TUYA_BASIC_DEMO) && TUYA_BASIC_DEMO == 1
#define ENABLE_TUYA_BASIC_DEMO          //启动涂鸦基础演示代码
//#pragma message("***************** ENABLE_TUYA_BASIC_DEMO *****************")
#elif  defined(TUYA_TILEVIEW_DEMO) && TUYA_TILEVIEW_DEMO == 1
#define TUYA_PAGE_TILEVIEW                //开启多页面滑动效果演示
//#pragma message("***************** TUYA_PAGE_TILEVIEW *****************")
#elif  defined(USER_GUI_APPLICATION) && USER_GUI_APPLICATION == 1
//#define NXP_GUI_GUIDER
//#define EEZ_STUDIO
#define ENABLE_USER_GUI_APPLICATION
//#pragma message("***************** ENABLE_USER_GUI_APPLICATION *****************")
#endif


#define GUI_IDEL_Time_MS                (10000*6)   //ms:判断屏幕进入IDEL(屏幕保护省电模式)的默认时间间隔
#define GUI_HEART_BEAT_INTERVAL_MS      (3000)      //ms:gui发送心跳时间间隔(间隔时间太短可能会影响刷屏效果!)

//#define GUI_MEM_USED_INFO_DBG
// #define GUI_SCREEN_SAVER                    //开启屏幕保护省电模式

//#define TUYA_TFTP_SERVER                  //模组作为tftp文件服务器:安全考虑,建议仅调试使用,量产固件关闭!!!!!!!!!!

#define LANGUAGE_LIVE_UPDATE                //语言实时更新,否则语言切换后重启更新

#define BootPageMinElapseTime_MS  (1500)    //ms:启动页面最小播放时间
#define BootPage2mainPageTime_MS  (1000)    //ms:启动页面播放结束时至用户主页面的时间间隔

//lvgl draw buffer/frame buffer配置
#define LVGL_DRAW_BUFFER_ROWS       (0)     //lvgl绘图缓存使用的行数(最大不超过当前屏幕的物理高度,0:默认为屏幕物理高度的1/10)
#define LVGL_DRAW_BUFFER_USE_SRAM   (0)     //lvgl绘图缓存是否使用SRAM,否则使用PSRAM
#define LVGL_DRAW_BUFFER_CNT        (2)     //lvgl绘图缓存数目,         1:单绘制缓存, 2:双绘制缓存
#define LVGL_FRAME_BUFFER_CNT       (2)     //lvgl帧缓存数目   ,       0:不使用帧缓存 1:单帧缓存,       2:双帧缓存

#ifdef ENABLE_TUYA_AI_DEMO

typedef enum {
    LV_USER_PRIVATE_EVENT_AI_TEXT = 0,      //文本信息
    LV_USER_PRIVATE_EVENT_AI_EXIT = 1,      //AI GUI退出通知
    //last line, don't change it !!!
    LV_USER_PRIVATE_EVENT_UNKNOWN = 0xff,
} LV_USER_PRIVATE_EVENT_TYPE_E;

typedef struct {
    LV_USER_PRIVATE_EVENT_TYPE_E  event_typ;
    unsigned int param1;                    //数据1
    unsigned int param2;                    //返回状态!(1:失败, 0:成功)
} LV_USER_PRIVATE_EVENT_S;
#endif
#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif

