/**
 * @file tuya_app_gui_gw_core1.h
 * @brief tuya_gw_core module is used to 
 * @version 0.1
 * @date 2024-07-29
 */

#ifndef __TUYA_APP_GUI_GW_CORE1_H__
#define __TUYA_APP_GUI_GW_CORE1_H__

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
************************macro define************************
***********************************************************/

/***********************************************************
********************function declaration********************
***********************************************************/
typedef void (*gui_dp_update_cb_t)(unsigned char is_raw, void *pdata);
typedef void (*gui_disk_format_end_cb_t)(bool mkfs_ok);

typedef void (*gui_cloud_recipe_rsp_cb_t)(void *rsp_param);
typedef void (*gui_cloud_recipe_rsp_clear_cb_t)(void *rsp_param);

typedef void (*gui_audio_play_status_update_cb_t)(uint32_t status);

typedef struct {
    uint32_t rsp_type;
    gui_cloud_recipe_rsp_cb_t           cr_rsp_hdl_cb;
    gui_cloud_recipe_rsp_clear_cb_t     cr_rsp_clear_cb;
    void **menu_list;
} CLOUD_RECIPE_RSP_HDL_T;

typedef void (*gui_resource_event_cb_t)(unsigned int evt_type, void *evt_data);

//注册dp(obj及raw类型)改变时回调函数
void tuya_app_gui_dp_update_notify_callback_register (gui_dp_update_cb_t cb);

//获取所有dpid的相关信息(仅obj类型,不包含raw类型,设备激活状态下有效),成功返回DEV_CNTL_N_S类型指针, 失败返回NULL!
void *tuya_app_gui_get_dp_n_cntl(void);

//获取指定dpid的相关信息(仅obj类型,不包含raw类型,设备激活状态下有效),成功返回DP_CNTL_S类型指针, 失败返回NULL!
void *tuya_app_gui_get_dp_cntl(unsigned char dpid);

//查询wifi是否连接上云端(仅当连接上返回true时,输出的rssi及ssid为有效),返回true:连接成功; 返回false:连接失败
bool tuya_app_gui_is_wifi_connected(signed char *out_rssi, char *ssid_buff, int ssid_buff_size);

//查询设备是否激活,返回true:已经激活; 返回false:未激活
bool tuya_app_gui_is_dev_activeted(void);

//请求解绑设备
void tuya_app_gui_request_dev_unbind(void);

//查询当前时间信息,返回true:查询成功; 返回false:查询失败
bool tuya_app_gui_request_local_time(char *time_buff, int time_buff_size);

//查询当前天气信息(天气预报数据格式为:LKTLV), 返回true:查询成功; 返回false:查询失败
bool tuya_app_gui_request_local_weather(char **local_weather, uint32_t *weather_len);

//开始文件系统格式化:仅测试阶段使用,慎用!!!!!
bool tuya_app_gui_disk_format_start(gui_disk_format_end_cb_t disk_mkfs_result_cb);

//查询平台资源更新信息
void tuya_app_gui_query_resource_update(void);

//查询平台资源更新信息事件回调注册
void tuya_app_gui_query_resource_event_cb_register(gui_resource_event_cb_t cb);

//云菜谱响应回调注册
void tuya_app_gui_cloud_recipe_rsp_hdl_register(gui_cloud_recipe_rsp_cb_t cr_rsp_hdl_cb);

//云菜谱请求
bool tuya_app_gui_cloud_recipe_request(uint32_t req_type, char *req_json_param);

/**
 * @brief kv写操作
 *
 * @param[in] key key of the entry you want to write
 * @param[in] value value buffer you want to write
 * @param[in] len the numbers of byte you want to write
 * @return true on success. Others on error.
 */
bool tuya_app_gui_kv_common_write(char *key, unsigned char *value, uint32_t len);

/**
 * @brief kv读操作
 *
 * @param[in] key  key of the entry you want to read
 * @param[out] value buffer of the value
 * @param[out] p_len length of the buffer
 * @return true on success. Others on error.
 *
 * @note must free the value buffer with wd_common_free_data when you nolonger need the buffer
 */
bool tuya_app_gui_kv_common_read(char *key, unsigned char **value, uint32_t *p_len);

/**
 * @brief kv文件写操作
 *
 * @param[in] key key of the entry you want to write
 * @param[in] value value buffer you want to write
 * @param[in] len the numbers of byte you want to write
 * @return true on success. Others on error.
 */
bool tuya_app_gui_fs_kv_common_write(char *key, unsigned char *value, uint32_t len);

/**
 * @brief kv文件读操作
 *
 * @param[in] key  key of the entry you want to read
 * @param[out] value buffer of the value
 * @param[out] p_len length of the buffer
 * @return true on success. Others on error.
 *
 * @note must free the value buffer with wd_common_free_data when you nolonger need the buffer
 */
bool tuya_app_gui_fs_kv_common_read(char *key, unsigned char **value, uint32_t *p_len);

//kv文件配置删除操作
void tuya_app_gui_fs_kv_common_delete_config(bool config_file/*配置或备份文件*/);

//cp1请求从cp0申请psram内存
void *cp1_req_cp0_malloc_psram(uint32_t buf_len);

//cp1请求从cp0释放psram内存
int cp1_req_cp0_free_psram(void *ptr);

//cp1请求播放本地音频文件, return true on success. Others on error.
bool tuya_app_gui_audio_file_play(char *audio_file);

//cp1请求播放暂停, return true on success. Others on error.
bool tuya_app_gui_audio_play_pause(void);

//cp1请求播放继续, return true on success. Others on error.
bool tuya_app_gui_audio_play_resume(void);

//cp1请求设置本地音频音量, return true on success. Others on error.
bool tuya_app_gui_audio_volume_set(uint32_t volume);

//cp1请求获取本地音频音量, return true on success. Others on error.
bool tuya_app_gui_audio_volume_get(uint32_t *volume);

//cp1获取本地音频文件播放状态回调注册
void tuya_app_gui_audio_play_status_callback_register(gui_audio_play_status_update_cb_t cb);

//设置屏幕背光亮度百分比
bool tuya_app_gui_set_lcd_backlight_percent(unsigned char light_percent);

/**
 * @brief 
 * @param[in] : data
 * @return NULL
 */
void tuya_app_gui_common_process (void *data);

int tuya_app_gui_gw_DpInfoCreate(void *_dev_cntl);

/*msgbox create*/
lv_obj_t * tuya_msgbox_create(lv_obj_t * parent, const char * title, const char * txt, const char * btn_txts[],
                            bool add_close_btn, lv_event_cb_t event_cb);

/*msgbox delete*/
void tuya_msgbox_del(lv_obj_t * msgbox);

/*msgbox adjust auto size*/
void tuya_msgbox_adjust_auto_size(lv_obj_t *msgbox);

#ifdef __cplusplus
}
#endif

#endif /* __TUYA_APP_GUI_GW_CORE1_H__ */
