/**
 * @file tuya_app_gui_gw_core0.h
 * @brief tuya_gw_core module is used to 
 * @version 0.1
 * @date 2024-07-29
 */

#ifndef __TUYA_APP_GUI_GW_CORE0_H__
#define __TUYA_APP_GUI_GW_CORE0_H__

#include "tuya_iot_com_api.h"
#include "tkl_display.h"
#include "lv_vendor_event.h"
#include "tuya_app_gui_core_config.h"
#ifdef TUYA_MULTI_TYPES_LCD
#include "tal_display_service.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
************************macro define************************
***********************************************************/
#define EVENT_GUI_READY_NOTIFY                  "gui.page.ready"            // gui准备好
#if defined(TUYA_FILE_SYSTEM) && (TUYA_FILE_SYSTEM == 1)
#define USER_CONFIG_KV_FILE_ASYNC_WRITE                                     //文件kv写操作异步处理
#endif
#define USER_CONFIG_FILE_BACKUP_ENABLE                                      //文件kv备份功能开启:防止数据丢失
#define USER_CONFIG_KV_FILE_NAME                "file_kv.conf"              //文件kv配置名
#ifdef USER_CONFIG_FILE_BACKUP_ENABLE
#define USER_CONFIG_KV_FILE_BACKUP_NAME         "file_kv.conf.bak"          //备份文件kv配置名
#endif
#ifdef USER_CONFIG_KV_FILE_ASYNC_WRITE
#include "tuya_list.h"
#include "tal_mutex.h"

typedef UCHAR_T KV_WR_TYPE;
#define TY_KVWR_KV          0
#define TY_KVWR_FS          1

typedef struct {
    LIST_HEAD node;
    CHAR_T *key;
    BYTE_T *value;
    UINT_T len;
    KV_WR_TYPE w_type;
} KV_FILE_WR_ELEMENT_S;

typedef struct {
    MUTEX_HANDLE mutex;
    LIST_HEAD list_hd;
} KV_FILE_ASYNC_LIST_S;
#endif

#ifdef LVGL_ENABLE_AUDIO_FILE_PLAY
typedef BYTE_T GUI_AUDIO_EVENT_TYPE_E;
#define T_GUI_AUDIO_EVT_VOLUME  0           // 音频音量设置
#define T_GUI_AUDIO_EVT_PLAY    1           // 音频文件播放
#define T_GUI_AUDIO_EVT_PAUSE   2           // 音频播放暂停
#define T_GUI_AUDIO_EVT_RESUME  3           // 音频播放继续

typedef struct {
    GUI_AUDIO_EVENT_TYPE_E  audio_evt_type;
    CHAR_T*                 gui_audio_file;
    INT32_T                 volume;
} GUI_AUDIO_EVENT_S;

OPERATE_RET gui_audio_play_event_enqueue(GUI_AUDIO_EVENT_TYPE_E evt_type, CHAR_T* audio_file, INT32_T volume);
INT32_T gui_audio_play_volume_get(VOID);
#endif

typedef struct
{
    INT32_T     spk_gpio;                    // spk amplifier pin number, <0, no amplifier
    INT32_T     spk_gpio_polarity;           // pin polarity, 0 high enable, 1 low enable
    INT32_T     spk_default_vol;             // default volume :0~100
} GUI_AUDIO_INFO_S;

typedef UINT_T GUI_BOOTON_TYPE;
#define GUI_BOOTON_NONE             0
#define GUI_BOOTON_WITHOUT_PAGE     1
#define GUI_BOOTON_WITH_PAGE        2

#ifdef TUYA_MULTI_TYPES_LCD
typedef struct
{
    ty_display_device_s *display_device;
    ty_display_cfg      *display_cfg;
} GUI_LCD_INFO_S;

typedef struct
{
    UCHAR_T                     lcd_num;        //屏幕数量
    TUYA_SCREEN_EXPANSION_TYPE  exp_type;       //屏幕扩展类型
    GUI_LCD_INFO_S              **lcd_info;     //屏幕信息
} GUI_LCDS_INFO_S;
#endif

/**
 * @brief event subscribe callback function type
 *
 */
typedef OPERATE_RET(*GUI_SYS_EVENT_CB)(LV_DISP_EVENT_S *evt);
typedef OPERATE_RET(*GUI_OBJ_EVENT_CB)(LV_DISP_EVENT_S *evt);
typedef OPERATE_RET(*GUI_DP2OBJ_PRE_INIT_EVENT_CB)(VOID);
typedef DP_REPT_CB_DATA*(*GUI_OBJ2DP_CONVERT_CB)(LV_DISP_EVENT_S *evt);

typedef OPERATE_RET(*BACKLIGHT_OPEN_USER_CB)(VOID_T);
typedef OPERATE_RET(*BACKLIGHT_CLOSE_USER_CB)(VOID_T);
typedef OPERATE_RET(*BACKLIGHT_LIGHT_PERCENT_USER_CB)(UCHAR_T light_percent);

/***********************************************************
********************function declaration********************
***********************************************************/
OPERATE_RET tuya_gui_system_event_hdl_register(GUI_SYS_EVENT_CB cb);
OPERATE_RET tuya_gui_obj_event_hdl_register(GUI_OBJ_EVENT_CB cb);
OPERATE_RET tuya_gui_dp2obj_pre_init_hdl_register(GUI_DP2OBJ_PRE_INIT_EVENT_CB cb);
OPERATE_RET tuya_gui_obj2dp_convert_hdl_register(GUI_OBJ2DP_CONVERT_CB cb);

OPERATE_RET tuya_disp_lcd_backlight_open(VOID_T);
OPERATE_RET tuya_disp_lcd_backlight_close(VOID_T);
OPERATE_RET tuya_disp_lcd_backlight_set(UCHAR_T light_percent);
OPERATE_RET tuya_disp_lcd_backlight_set_custom_register(BACKLIGHT_OPEN_USER_CB bl_open, BACKLIGHT_CLOSE_USER_CB bl_close, BACKLIGHT_LIGHT_PERCENT_USER_CB bl_percent);

OPERATE_RET tuya_gui_lcd_open(VOID_T *info);

/**
 * @brief tuya key-value file system database write entry
 *
 * @param[in] key key of the entry you want to write
 * @param[in] value value buffer you want to write
 * @param[in] len the numbers of byte you want to write
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET fs_kv_common_write(IN CONST CHAR_T *key, IN CONST CHAR_T *value, IN CONST UINT_T len);

/**
 * @brief tuya key-value file system database read entry
 *
 * @param[in] key  key of the entry you want to read
 * @param[out] value buffer of the value
 * @param[out] p_len length of the buffer
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 *
 * @note must free the value buffer with wd_common_free_data when you nolonger need the buffer
 */
OPERATE_RET fs_kv_common_read(IN CONST CHAR_T *key, OUT CHAR_T **value, OUT UINT_T *p_len);

//cp0请求从cp1申请内存
VOID *cp0_req_cp1_malloc_psram(IN UINT_T buf_len);

//cp0请求从cp1释放内存
OPERATE_RET cp0_req_cp1_free_psram(IN VOID *ptr);

VOID tuya_gui_init(BOOL_T is_mf_test, VOID_T *lcd_info, GUI_AUDIO_INFO_S *audio_info, CHAR_T *weather_code);

BOOL_T tuya_gui_screen_is_loaded(VOID);

VOID tuya_gui_dp_source_release(DP_REPT_CB_DATA* dp_data);

#ifdef __cplusplus
}
#endif

#endif /* __TUYA_APP_GUI_GW_CORE0_H__ */
