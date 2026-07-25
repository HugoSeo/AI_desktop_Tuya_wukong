/**
 * @file tuya_app_gui_gw_core0.c
 * @author www.tuya.com
 * @brief tuya_app_gui_gw_core0 module is used to 
 * @version 0.1
 * @date 2022-10-28
 *
 * @copyright Copyright (c) tuya.inc 2022
 *
 */

#include <stdlib.h>
#include <stdio.h>

#include "tal_system.h"
#include "tal_log.h"
#include "mqc_app.h"
#include "tal_workq_service.h"
#include "ty_cJSON.h"

#include "tuya_iot_wifi_api.h"
#include "tuya_wifi_status.h"

#include "tuya_app_gui_gw_core0.h"
#include "tuya_app_gui_fs_path.h"
#include "tuya_app_gui_booton_page.h"

#include "tkl_lvgl.h"
#include "tal_system.h"
#include "tal_sw_timer.h"
#include "tuya_list.h"
#include "app_ipc_command.h"
#include "tuya_app_gui_fs_path.h"

#ifdef TUYA_GUI_RESOURCE_DOWNLAOD
#include "ty_gfw_gui_file.h"
#include "ty_gfw_gui_kepler_file_download.h"
#include "ty_gfw_gui_kepler_file_download_cloud.h"
#endif

#include "ty_gfw_gui_weather.h"

#include "tal_queue.h"
#include "tal_semaphore.h"
#include "tal_thread.h"
#include "smart_frame.h"
#include "uni_log.h"
#include "tuya_ws_db.h"
#include "gw_intf.h"
#include "tal_mutex.h"
#include "ty_gui_fs.h"
#ifdef TUYA_APP_MULTI_CORE_IPC
#include "tkl_ipc.h"
#endif
#ifdef LVGL_ENABLE_CLOUD_RECIPE
#include "ty_gfw_gui_cloud_recipe.h"
#endif

#define GUI_DP_QUEUE_NUM 64
#define QUEUE_WAIT_FROEVER 0xFFFFFFFF

typedef struct  {
    THREAD_HANDLE dp_recv_thread;
    THREAD_HANDLE dp_send_thread;
    QUEUE_HANDLE dp_rx_queue;
    QUEUE_HANDLE dp_tx_queue;
    DP_REPT_HADLE_CB_S dp_rept_hd_s;
}TUYA_GUI_DP_CORE_T;

extern VOID_T tkl_system_psram_free(VOID_T* ptr);
extern VOID_T* tkl_system_psram_malloc(CONST SIZE_T size);

STATIC MUTEX_HANDLE fs_kvs_mutex = NULL;             //文件kv互斥锁
STATIC GUI_SYS_EVENT_CB gui_system_event_hdl = NULL;
STATIC GUI_OBJ_EVENT_CB gui_obj_event_hdl = NULL;
STATIC GUI_DP2OBJ_PRE_INIT_EVENT_CB gui_dp2obj_pre_init_hdl = NULL;
STATIC GUI_OBJ2DP_CONVERT_CB gui_obj2dp_convert_hdl = NULL;
STATIC TUYA_GUI_DP_CORE_T gui_dp_process_hdl;
STATIC BOOL_T gui_initialized = false;
STATIC BOOL_T gui_mf_test = false;
STATIC BOOL_T gui_wifi_completed = false;
STATIC DELAYED_WORK_HANDLE gui_disk_format_work = NULL;
STATIC DELAYED_WORK_HANDLE gui_unbind_dev_work = NULL;

#ifdef USER_CONFIG_KV_FILE_ASYNC_WRITE
STATIC KV_FILE_ASYNC_LIST_S *kv_file_async_list = NULL;
#endif

#ifdef TUYA_MULTI_TYPES_LCD
#include "tuya_port_disp.h"

STATIC TY_DISPLAY_HANDLE disp_handle = NULL;
STATIC TY_DISPLAY_HANDLE disp_handle_slave = NULL;
#endif

STATIC GUI_BOOTON_TYPE gui_screen_loaded = GUI_BOOTON_NONE;         //主屏幕是否已经加载

STATIC OPERATE_RET  __app_gui_activated_event(VOID *data);
STATIC OPERATE_RET  __app_gui_dev_initialized_event(VOID *data);

int __attribute__((weak)) ___cli_lfs_mkfs(const char *mount_path, unsigned char lfs_p_type)
{
    int ret = -1;
    PR_NOTICE("[%s:%d]>>>>>>>>>>>>>>>>>>>>entry weak mkfs.....\r\n", __func__, __LINE__);
    return ret;
}

STATIC VOID_T __app_gui_disk_format_msg_cb(VOID_T *data)
{
    extern int ___cli_lfs_mkfs(const char *mount_path, unsigned char lfs_p_type);
    OPERATE_RET rt = OPRT_OK;
    LV_DISP_EVENT_S Sevt = {0};
    LV_TUYAOS_EVENT_S g_dp_evt_s = {0};
    TUYA_GUI_LFS_PARTITION_TYPE_E lfs_p_type = tuya_app_gui_get_lfs_partiton_type();
    GUI_FS_DEV_TYPE_T p_type = GUI_DEV_INNER_FLASH;

    if (lfs_p_type == TUYA_GUI_LFS_QSPI_FLASH)          //仅支持片外qspi-flash格式化!
        p_type = GUI_DEV_EXT_FLASH;
    else {
        PR_ERR("[%s][%d]---------------unsupport partition format type '%d'???", __func__, __LINE__, lfs_p_type);
        rt = OPRT_INVALID_PARM;
    }

    if (rt == OPRT_OK) {
        PR_NOTICE("[%s][%d]---------------[disk type '%s', mount path '%s']!", __func__, __LINE__,
            (lfs_p_type==TUYA_GUI_LFS_SPI_FLASH)?"internal-flash":(lfs_p_type==TUYA_GUI_LFS_QSPI_FLASH)?"exteranl qspi-flash":"sd-card", 
            tuya_app_gui_get_disk_mount_path());

        ty_gui_fs_unmount(tuya_app_gui_get_disk_mount_path());
        rt = ___cli_lfs_mkfs(tuya_app_gui_get_disk_mount_path(), p_type);
        if (rt == OPRT_OK)
            rt = ty_gui_fs_mount(tuya_app_gui_get_disk_mount_path(), p_type);
    }

    PR_NOTICE("[%s][%d]---------------disk format result '%s'!", __func__, __LINE__, (rt == OPRT_OK)?"ok":"fail");
    g_dp_evt_s.event_typ = LV_TUYAOS_EVENT_FS_FORMAT_REPORT;    //cp0:返回格式化文件系统结果!
    if (rt == OPRT_OK) {
        g_dp_evt_s.param2 = OPRT_OK;
    }
    else {
        g_dp_evt_s.param2 = 1;
    }
    Sevt.param = (UINT32_T) &g_dp_evt_s;
    Sevt.event = LLV_EVENT_VENDOR_TUYAOS;
    tuya_disp_gui_event_send(&Sevt);

    tal_workq_stop_delayed(gui_disk_format_work);
    tal_workq_cancel_delayed(gui_disk_format_work);
    gui_disk_format_work = NULL;
}

STATIC OPERATE_RET tuya_gui_widget_to_dp_evt_send(LV_DISP_EVENT_S *evt)
{
    OPERATE_RET rt = OPRT_COM_ERROR;
    DP_REPT_CB_DATA* dp_data = NULL;

    if (gui_obj2dp_convert_hdl != NULL) {
        dp_data = gui_obj2dp_convert_hdl(evt);
    }
    if (dp_data != NULL) {
        rt = tal_queue_post(gui_dp_process_hdl.dp_tx_queue, &dp_data, 0);
        if (OPRT_OK != rt) {
            PR_ERR("tx queue post err");
        }
    }
    return rt;
}

#ifdef USER_CONFIG_FILE_BACKUP_ENABLE
#include "crc32i.h"

STATIC DELAYED_WORK_HANDLE _s_fskv_bk_tm_work = NULL;

STATIC OPERATE_RET __fs_kv_common_config_bk_execute(BOOL_T init_stage)
{
    OPERATE_RET op_ret = OPRT_COM_ERROR;
    BOOL_T is_exist = FALSE;
    CHAR_T config_file[UI_FILE_PATH_MAX_LEN] = {0};
    TUYA_FILE file_hdl = NULL;
    CHAR_T *file_name = USER_CONFIG_KV_FILE_NAME;           //先读配置文件
    //配置文件信息
    UCHAR_T *file_content = NULL;
    UCHAR_T *file_content_normal = NULL;
    UCHAR_T *file_content_bk = NULL;
    INT_T rd_len = 0;
    UINT32_T rd_len_normal = 0, rd_len_bk = 0;
    //备份配置文件信息
    BOOL_T bk_file_exist = TRUE;

    if (fs_kvs_mutex == NULL) {
        PR_ERR("[%s][%d] fs kv not initiliazed !\r\n", __FUNCTION__, __LINE__);
        return op_ret;
    }

    //tal_mutex_lock(fs_kvs_mutex);
    //1.获取文件内容!
    for (INT_T i = 0; i<2; i++) {
        op_ret = OPRT_COM_ERROR;
        file_content = NULL;
        rd_len = 0;
        memset(config_file, 0, UI_FILE_PATH_MAX_LEN);
        snprintf(config_file, UI_FILE_PATH_MAX_LEN, "%s%s", tuya_app_gui_get_disk_mount_path(), file_name);
        ty_gui_fs_is_exist(config_file, &is_exist);
        if (is_exist == TRUE) {
            INT_T file_len = 0;
            file_len = ty_gui_fgetsize(config_file);
            if(file_len <= 0) {
                PR_ERR("[%s][%d] get file size fail: '%s'\r\n", __FUNCTION__, __LINE__, config_file);
            }
            else {
                //PR_NOTICE("[%s][%d]--------------- file '%s', size '%d' !", __func__, __LINE__, config_file, file_len);
                file_content = tal_malloc(file_len+1);
                if (file_content == NULL) {
                    PR_ERR("[%s][%d] malloc sizeof of '%u' fail ?\r\n", __FUNCTION__, __LINE__, file_len);
                }
                else {
                    memset(file_content, 0, file_len+1);
                    file_hdl = ty_gui_fopen(config_file, "r");        //以只读方式打开!
                    if (file_hdl != NULL) {
                        rd_len = ty_gui_fread(file_content, file_len, file_hdl);
                        if (rd_len < 0) {
                            PR_ERR("[%s][%d] read file fail '%d'?\r\n", __FUNCTION__, __LINE__, rd_len);
                        }
                        else
                            op_ret = OPRT_OK;
                        ty_gui_fclose(file_hdl);
                    }
                    else {
                        PR_ERR("[%s][%d]--------------- config file '%s' open fail !", __func__, __LINE__, config_file);
                    }
                }
            }
        }
        else {
            if (strcmp(file_name, USER_CONFIG_KV_FILE_NAME) == 0) {                 //配置文件不存在!
                if (init_stage) {               //继续读备份配置文件
                    file_name = USER_CONFIG_KV_FILE_BACKUP_NAME;
                    file_content_normal = NULL;
                    rd_len_normal = 0;
                    continue;
                }
                else {
                    PR_ERR("[%s][%d]--------------- config file '%s' not exist !", __func__, __LINE__, config_file);
                }
            }
            else if (strcmp(file_name, USER_CONFIG_KV_FILE_BACKUP_NAME) == 0) {     //备份配置文件不存在,第一次备份开始!
                bk_file_exist = FALSE;
                if (init_stage) {               //初始化阶段,备份配置文件必须存在!
                    //必须存在!
                    PR_ERR("[%s][%d]--------------- config file '%s' not exist !", __func__, __LINE__, config_file);
                }
                else {                          //备份配置文件不存在,第一次备份开始!
                    op_ret = OPRT_OK;
                    break;
                }
            }
        }

        if (op_ret != OPRT_OK) {
            break;
        }
        else {
            if (strcmp(file_name, USER_CONFIG_KV_FILE_NAME) == 0) {
                file_name = USER_CONFIG_KV_FILE_BACKUP_NAME;
                file_content_normal = file_content;
                rd_len_normal = rd_len;
            }
            else {
                file_content_bk = file_content;
                rd_len_bk = rd_len;
                break;
            }
        }
    }

    //2.开始备份!
    if (op_ret == OPRT_OK) {
        UINT32_T kv_crc32 = 0, bk_kv_crc32 = 0;

        if (init_stage != TRUE)
            kv_crc32 = hash_crc32i_total(file_content_normal, rd_len_normal);
        if (bk_file_exist == TRUE)
            bk_kv_crc32 = hash_crc32i_total(file_content_bk, rd_len_bk);
        if (kv_crc32 != bk_kv_crc32) {
            INT_T ret_num = 0;

            op_ret = OPRT_COM_ERROR;            //重新初始化状态!
            //PR_NOTICE("[%s][%d]--------------- config file crc32 '%u' -> '%u', backup start !", __func__, __LINE__, bk_kv_crc32, kv_crc32);
            memset(config_file, 0, UI_FILE_PATH_MAX_LEN);
            if (init_stage == TRUE) {       //备份配置文件--->配置文件
                snprintf(config_file, UI_FILE_PATH_MAX_LEN, "%s%s", tuya_app_gui_get_disk_mount_path(), USER_CONFIG_KV_FILE_NAME);
            }
            else                            //配置文件--->备份配置文件
                snprintf(config_file, UI_FILE_PATH_MAX_LEN, "%s%s", tuya_app_gui_get_disk_mount_path(), USER_CONFIG_KV_FILE_BACKUP_NAME);
            file_hdl = ty_gui_fopen(config_file, "w+");        //以清零方式重新创建可读写文件!
            if (file_hdl != NULL) {
                UINT32_T w_len = 0;
                if (init_stage == TRUE) {
                    w_len = rd_len_bk;
                    ret_num = ty_gui_fwrite(file_content_bk, w_len, file_hdl);
                }
                else {
                    w_len = rd_len_normal;
                    ret_num = ty_gui_fwrite(file_content_normal, w_len, file_hdl);
                }
                if (w_len != ret_num) {
                    PR_ERR("[%s][%d] backup file len mismatch '%d' - '%d' ???\r\n", __FUNCTION__, __LINE__, w_len, ret_num);
                }
                else
                    op_ret = OPRT_OK;
                ty_gui_fclose(file_hdl);
            }
            else {
                PR_ERR("[%s][%d]--------------- config file '%s' creat fail !", __func__, __LINE__, config_file);
            }
        }
        else {
            PR_NOTICE("[%s][%d]--------------- config file no change !", __func__, __LINE__);
        }
    }

    //3.释放资源
    if (file_content_normal != NULL && file_content_bk != NULL) {
        tal_free(file_content_normal);
        tal_free(file_content_bk);
    }
    else if (file_content_normal != NULL) {
        if (file_content != NULL && file_content != file_content_normal)
            tal_free(file_content);
        tal_free(file_content_normal);
    }
    else if (file_content_bk != NULL) {
        if (file_content != NULL && file_content != file_content_bk)
            tal_free(file_content);
        tal_free(file_content_bk);
    }
    else if (file_content != NULL) {
        tal_free(file_content);
    }
    file_content = NULL;
    file_content_normal = NULL;
    file_content_bk = NULL;
    //tal_mutex_unlock(fs_kvs_mutex);

    if (op_ret == OPRT_OK) {
        PR_NOTICE("[%s][%d]--------------- config file backup loop successful !", __func__, __LINE__);
    }
    return op_ret;
}

STATIC VOID_T __fs_kv_common_config_bk_tm_cb(VOID_T *data)
{
    tal_mutex_lock(fs_kvs_mutex);
    __fs_kv_common_config_bk_execute(FALSE);
    tal_mutex_unlock(fs_kvs_mutex);
}

STATIC OPERATE_RET fs_kv_common_config_backup_start(VOID)
{
    OPERATE_RET rt = OPRT_OK;
    BOOL_T is_exist = FALSE;
    BOOL_T is_bk_exist = FALSE;
    CHAR_T config_file[UI_FILE_PATH_MAX_LEN] = {0};

    rt = tal_workq_init_delayed(WORKQ_HIGHTPRI, __fs_kv_common_config_bk_tm_cb, NULL, &_s_fskv_bk_tm_work);
    if (OPRT_OK != rt) {
        PR_ERR("tal_workq_init delayed __fs_kv_common_config_bk_tm_cb failed:%d!", rt);
    }
    else {
        //开始恢复配置文件
        if (fs_kvs_mutex == NULL) {
            PR_ERR("[%s][%d] fs kv not initiliazed !\r\n", __FUNCTION__, __LINE__);
        }
        else {
            tal_mutex_lock(fs_kvs_mutex);
            memset(config_file, 0, UI_FILE_PATH_MAX_LEN);
            snprintf(config_file, UI_FILE_PATH_MAX_LEN, "%s%s", tuya_app_gui_get_disk_mount_path(), USER_CONFIG_KV_FILE_NAME);
            ty_gui_fs_is_exist(config_file, &is_exist);
            memset(config_file, 0, UI_FILE_PATH_MAX_LEN);
            snprintf(config_file, UI_FILE_PATH_MAX_LEN, "%s%s", tuya_app_gui_get_disk_mount_path(), USER_CONFIG_KV_FILE_BACKUP_NAME);
            ty_gui_fs_is_exist(config_file, &is_bk_exist);
            if (is_exist == FALSE) {
                if (is_bk_exist == TRUE) {                                    //恢复配置文件
                    rt = __fs_kv_common_config_bk_execute(TRUE);
                    PR_NOTICE("[%s][%d]---------------recovery from backup file to config '%s' !", __func__, __LINE__, (rt == OPRT_OK)?"ok":"fail");
                }
            }
            else {
                rt = __fs_kv_common_config_bk_execute(FALSE);               //恢复备份配置文件
                PR_NOTICE("[%s][%d]---------------recovery from config to backup file '%s' !", __func__, __LINE__, (rt == OPRT_OK)?"ok":"fail");
            }
            tal_mutex_unlock(fs_kvs_mutex);
        }
    }

    return rt;
}

STATIC OPERATE_RET fs_kv_common_config_backup_notify(VOID)
{
    OPERATE_RET rt = OPRT_COM_ERROR;

    rt = tal_workq_stop_delayed(_s_fskv_bk_tm_work);
    if (OPRT_OK == rt) {
        rt = tal_workq_start_delayed(_s_fskv_bk_tm_work, 5000, LOOP_ONCE);
    }
    if (OPRT_OK != rt) {
        PR_ERR("tal_workq_start delayed fs_kv_common_config_backup notify failed:%d!", rt);
    }
    return rt;
}
#endif

/*kv文件保存格式
[
    {
        "key": "a",
        "value": "11223344"
    },
    {
        "key": "b",
        "value": "5566778899"
    }
]
*/
STATIC OPERATE_RET tuya_gui_fs_kv_element_new(CHAR_T *config_file, IN CONST CHAR_T *key, IN CONST CHAR_T *value, IN CONST UINT_T len)
{
    OPERATE_RET op_ret = OPRT_COM_ERROR;
    CHAR_T *json_string = NULL;
    ty_cJSON *array = NULL, *item = NULL;
    INT_T w_num = 0, ret_num = 0;
    TUYA_FILE file_hdl = NULL;

    file_hdl = ty_gui_fopen(config_file, "w+");        //以清零方式重新创建可读写文件!
    if (file_hdl != NULL) {
        array = ty_cJSON_CreateArray();
        item = ty_cJSON_CreateObject();
        ty_cJSON_AddStringToObject(item, "key", key);
        ty_cJSON_AddStringToObject(item, "value", value);
        ty_cJSON_AddItemToArray(array, item);
        json_string = ty_cJSON_PrintUnformatted(array);
        ty_cJSON_Delete(array);
        if (json_string == NULL) {
            PR_ERR("[%s][%d] json printformat fail ?\r\n", __FUNCTION__, __LINE__);
        }
        else {
            w_num = strlen(json_string);
            ret_num = ty_gui_fwrite(json_string, w_num, file_hdl);
            if (w_num != ret_num) {
                PR_ERR("[%s][%d] write key '%s' len mismatch '%d' - '%d' ???\r\n", __FUNCTION__, __LINE__,key, w_num, ret_num);
            }
            else
                op_ret = OPRT_OK;
        }
        if (json_string != NULL)
            Free(json_string);
        ty_gui_fclose(file_hdl);
    }
    else {
        PR_ERR("[%s][%d]--------------- config file '%s' creat fail !", __func__, __LINE__, config_file);
    }
    return op_ret;
}

STATIC OPERATE_RET tuya_gui_fs_kv_element_set(CHAR_T *config_file, INT_T file_len, IN CONST CHAR_T *key, IN CONST CHAR_T *value, IN CONST UINT_T len)
{
    OPERATE_RET op_ret = OPRT_COM_ERROR;
    CHAR_T *json_string = NULL;
    UCHAR_T *file_content = NULL;
    ty_cJSON *root = NULL, *item = NULL;
    INT_T w_num = 0, ret_num = 0;
    TUYA_FILE file_hdl = NULL;

    file_hdl = ty_gui_fopen(config_file, "r");        //先以只读方式打开文件!
    if (file_hdl != NULL) {
        file_content = tal_malloc(file_len+1);
        if (file_content == NULL) {
            PR_ERR("[%s][%d] malloc sizeof of '%u' fail ?\r\n", __FUNCTION__, __LINE__, file_len);
        }
        else {
            INT_T rd_len = 0;
            memset(file_content, 0, file_len+1);
            rd_len = ty_gui_fread(file_content, file_len, file_hdl);
            if (rd_len < 0) {
                PR_ERR("[%s][%d] read file fail '%d'?\r\n", __FUNCTION__, __LINE__, rd_len);
            }
            else {
                root = ty_cJSON_Parse((CHAR_T *)file_content);
                if (NULL == root) {
                    PR_ERR("[%s][%d] cjson parse fail", __FUNCTION__, __LINE__);
                }
                else {
                    BOOL_T key_exist = FALSE;
                    ty_cJSON_ArrayForEach(item, root) {
                        ty_cJSON *_key = ty_cJSON_GetObjectItem(item, "key");
                        ty_cJSON *_value = ty_cJSON_GetObjectItem(item, "value");
                        if (strcmp(_key->valuestring, key) == 0) {
                            ty_cJSON_SetValuestring(_value, value);
                            key_exist = TRUE;
                            break;
                        }
                    }
                    if (!key_exist) {       //key不存在则追加!
                        cJSON *new_item = ty_cJSON_CreateObject();
                        ty_cJSON_AddStringToObject(new_item, "key", key);
                        ty_cJSON_AddStringToObject(new_item, "value", value);
                        ty_cJSON_AddItemToArray(root, new_item);
                    }
                    json_string = ty_cJSON_PrintUnformatted(root);
                    ty_cJSON_Delete(root);
                    if (json_string == NULL) {
                        PR_ERR("[%s][%d] json printformat fail ?\r\n", __FUNCTION__, __LINE__);
                    }
                    else
                        op_ret = OPRT_OK;
                }
            }
            tal_free(file_content);
        }

        if (op_ret == OPRT_OK) {
            ty_gui_fclose(file_hdl);
            file_hdl = ty_gui_fopen(config_file, "w+");        //重新以清零文件的写方式打开文件!
            if (file_hdl != NULL) {
                w_num = strlen(json_string);
                ret_num = ty_gui_fwrite(json_string, w_num, file_hdl);
                if (w_num != ret_num) {
                    PR_ERR("[%s][%d] write key '%s' len mismatch '%d' - '%d' ???\r\n", __FUNCTION__, __LINE__,key, w_num, ret_num);
                    op_ret = OPRT_COM_ERROR;
                }
            }
            else {
                op_ret = OPRT_COM_ERROR;
                PR_ERR("[%s][%d]--------------- config file '%s' open as write fail !", __func__, __LINE__, config_file);
            }
        }
        if (json_string != NULL)
            Free(json_string);
        if (file_hdl != NULL)
            ty_gui_fclose(file_hdl);
    }
    else {
        PR_ERR("[%s][%d]--------------- config file '%s' open as read fail !", __func__, __LINE__, config_file);
    }
    return op_ret;
}

STATIC OPERATE_RET tuya_gui_fs_kv_element_get(TUYA_FILE file_hdl, INT_T file_len, IN CONST CHAR_T *key, OUT CHAR_T **value, OUT UINT_T *p_len)
{
    OPERATE_RET op_ret = OPRT_COM_ERROR;
    UCHAR_T *file_content = NULL;
    ty_cJSON *root = NULL, *item = NULL;
    CHAR_T *json_string = NULL;

    file_content = tal_malloc(file_len+1);
    if (file_content == NULL) {
        PR_ERR("[%s][%d] malloc sizeof of '%u' fail ?\r\n", __FUNCTION__, __LINE__, file_len);
    }
    else {
        INT_T rd_len = 0;
        memset(file_content, 0, file_len+1);
        rd_len = ty_gui_fread(file_content, file_len, file_hdl);
        if (rd_len < 0) {
            PR_ERR("[%s][%d] read file fail '%d'?\r\n", __FUNCTION__, __LINE__, rd_len);
        }
        else {
            root = ty_cJSON_Parse((CHAR_T *)file_content);
            if (NULL == root) {
                PR_ERR("[%s][%d] cjson parse fail", __FUNCTION__, __LINE__);
            }
            else {
                //json_string = ty_cJSON_PrintUnformatted(root);
                ty_cJSON_ArrayForEach(item, root) {
                    ty_cJSON *_key = ty_cJSON_GetObjectItem(item, "key");
                    ty_cJSON *_value = ty_cJSON_GetObjectItem(item, "value");
                    if (strcmp(_key->valuestring, key) == 0) {
                        *p_len = strlen( _value->valuestring);
                        *value = tal_malloc(*p_len);
                        if (*value != NULL) {
                            memcpy(*value,  _value->valuestring, *p_len);
                            op_ret = OPRT_OK;
                        }
                        else {
                            PR_ERR("[%s][%d] malloc fail", __FUNCTION__, __LINE__);
                        }
                        break;
                    }
                }
                if (json_string != NULL) {
                    PR_NOTICE("[%s][%d]--------------- config file disp '%s'\r\n", __func__, __LINE__, json_string);
                    Free(json_string);
                }
                ty_cJSON_Delete(root);
            }
        }
        tal_free(file_content);
    }

    return op_ret;
}

STATIC OPERATE_RET __fs_kv_commom_recovery_from_bk(VOID_T)
{
    OPERATE_RET op_ret = OPRT_COM_ERROR;
    BOOL_T is_bk_exist = FALSE;
    CHAR_T config_file[UI_FILE_PATH_MAX_LEN] = {0};

    memset(config_file, 0, UI_FILE_PATH_MAX_LEN);
    snprintf(config_file, UI_FILE_PATH_MAX_LEN, "%s%s", tuya_app_gui_get_disk_mount_path(), USER_CONFIG_KV_FILE_BACKUP_NAME);
    ty_gui_fs_is_exist(config_file, &is_bk_exist);

    if (is_bk_exist == TRUE) {
        op_ret = __fs_kv_common_config_bk_execute(TRUE);
    }
    return op_ret;
}

OPERATE_RET fs_kv_common_write(IN CONST CHAR_T *key, IN CONST CHAR_T *value, IN CONST UINT_T len)
{
    OPERATE_RET op_ret = OPRT_COM_ERROR;
    BOOL_T is_exist = FALSE;
    CHAR_T config_file[UI_FILE_PATH_MAX_LEN] = {0};

    if (fs_kvs_mutex == NULL) {
        PR_ERR("[%s][%d] fs kv not initiliazed !\r\n", __FUNCTION__, __LINE__);
        return op_ret;
    }

    tal_mutex_lock(fs_kvs_mutex);
    retry:
    memset(config_file, 0, UI_FILE_PATH_MAX_LEN);
    snprintf(config_file, UI_FILE_PATH_MAX_LEN, "%s%s", tuya_app_gui_get_disk_mount_path(), USER_CONFIG_KV_FILE_NAME);
    ty_gui_fs_is_exist(config_file, &is_exist);
    if (is_exist == TRUE) {
        INT_T file_len = 0;
        file_len = ty_gui_fgetsize(config_file);
        if(file_len <= 0) {
            PR_ERR("[%s][%d] get file size fail: '%s'\r\n", __FUNCTION__, __LINE__, config_file);
        }
        else {
            op_ret = tuya_gui_fs_kv_element_set(config_file, file_len, key, value, len);
        }
    }
    else {
        if (__fs_kv_commom_recovery_from_bk() != OPRT_OK) {       //如果备份文件也不存在,则创建新文件!
            PR_NOTICE("[%s][%d]--------------- config file '%s' not exist , creat start !", __func__, __LINE__, config_file);
            op_ret = tuya_gui_fs_kv_element_new(config_file, key, value, len);
        }
        else {          //如果备份文件存在,从备份文件恢复后,重新将element插入到配置文件!
            PR_NOTICE("[%s][%d]--------------- config file recovery from bk successful ,retry write kv again !", __func__, __LINE__);
            goto retry;
        }
    }
    tal_mutex_unlock(fs_kvs_mutex);

#ifdef USER_CONFIG_FILE_BACKUP_ENABLE
    if (op_ret == OPRT_OK) {
        fs_kv_common_config_backup_notify();
    }
#endif

    return op_ret;
}

OPERATE_RET fs_kv_common_read(IN CONST CHAR_T *key, OUT CHAR_T **value, OUT UINT_T *p_len)
{
    OPERATE_RET op_ret = OPRT_COM_ERROR;
    BOOL_T is_exist = FALSE;
    TUYA_FILE file_hdl = NULL;
    CHAR_T config_file[UI_FILE_PATH_MAX_LEN] = {0};

    if (fs_kvs_mutex == NULL) {
        PR_ERR("[%s][%d] fs kv not initiliazed !\r\n", __FUNCTION__, __LINE__);
        return op_ret;
    }

    tal_mutex_lock(fs_kvs_mutex);
    memset(config_file, 0, UI_FILE_PATH_MAX_LEN);
    snprintf(config_file, UI_FILE_PATH_MAX_LEN, "%s%s", tuya_app_gui_get_disk_mount_path(), USER_CONFIG_KV_FILE_NAME);
    ty_gui_fs_is_exist(config_file, &is_exist);
    if (is_exist == TRUE) {
        INT_T file_len = 0;
        file_len = ty_gui_fgetsize(config_file);
        if(file_len <= 0) {
            PR_ERR("[%s][%d] get file size fail: '%s'\r\n", __FUNCTION__, __LINE__, config_file);
        }
        else {
            file_hdl = ty_gui_fopen(config_file, "r");        //以只读方式打开!
            if (file_hdl != NULL) {
                op_ret  = tuya_gui_fs_kv_element_get(file_hdl, file_len, key, value, p_len);
                ty_gui_fclose(file_hdl);
            }
            else {
                PR_ERR("[%s][%d]--------------- config file '%s' open fail !", __func__, __LINE__, config_file);
            }
        }
    }
    else {
        PR_ERR("[%s][%d]--------------- config file '%s' not exist !", __func__, __LINE__, config_file);
    }
    tal_mutex_unlock(fs_kvs_mutex);

    return op_ret;
}

#if 0
STATIC OPERATE_RET fs_kv_common_delete_config(IN BOOL_T is_config)
{
    OPERATE_RET op_ret = OPRT_COM_ERROR;
    CHAR_T config_file[UI_FILE_PATH_MAX_LEN] = {0};

    memset(config_file, 0, UI_FILE_PATH_MAX_LEN);
    if (is_config==TRUE)
        snprintf(config_file, UI_FILE_PATH_MAX_LEN, "%s%s", tuya_app_gui_get_disk_mount_path(), USER_CONFIG_KV_FILE_NAME);
    else
        snprintf(config_file, UI_FILE_PATH_MAX_LEN, "%s%s", tuya_app_gui_get_disk_mount_path(), USER_CONFIG_KV_FILE_BACKUP_NAME);
    op_ret = ty_gui_fs_remove(config_file);
    PR_NOTICE("[%s][%d]--------------- start delete '%s' %s !", __func__, __LINE__, config_file, (op_ret==OPRT_OK)?"successful":"fail");
    return op_ret;
}
#endif

#ifdef USER_CONFIG_KV_FILE_ASYNC_WRITE
STATIC SEM_HANDLE kv_async_wr_sem = NULL;

STATIC OPERATE_RET kv_common_write_async(IN CONST CHAR_T *key, IN CONST BYTE_T *value, IN CONST UINT_T len, IN KV_WR_TYPE w_type)
{
    OPERATE_RET op_ret = OPRT_COM_ERROR;

    if (kv_file_async_list != NULL) {
        tal_mutex_lock(kv_file_async_list->mutex);
        KV_FILE_WR_ELEMENT_S *kv_wr_ele = (KV_FILE_WR_ELEMENT_S *)tal_malloc(SIZEOF(KV_FILE_WR_ELEMENT_S));
    
        if (kv_wr_ele == NULL) {
            tal_mutex_unlock(kv_file_async_list->mutex);
            PR_ERR("%s-%s:malloc fail !", __func__, __LINE__);
            return op_ret;
        }
        memset(kv_wr_ele, 0, sizeof(KV_FILE_WR_ELEMENT_S));
        kv_wr_ele->key = (CHAR_T *)tal_malloc(strlen(key)+1);
        if (kv_wr_ele->key == NULL) {
            tal_free(kv_wr_ele);
            tal_mutex_unlock(kv_file_async_list->mutex);
            PR_ERR("%s-%s:malloc fail !", __func__, __LINE__);
            return op_ret;
        }
        memset(kv_wr_ele->key, 0, strlen(key)+1);
        kv_wr_ele->value = (BYTE_T *)tal_malloc(len+1);
        if (kv_wr_ele->value == NULL) {
            tal_free(kv_wr_ele->key);
            tal_free(kv_wr_ele);
            tal_mutex_unlock(kv_file_async_list->mutex);
            PR_ERR("%s-%s:malloc fail !", __func__, __LINE__);
            return op_ret;
        }
        memset(kv_wr_ele->value, 0, len+1);
        strcpy(kv_wr_ele->key, key);
        memcpy(kv_wr_ele->value, value, len);
        kv_wr_ele->len = len;
        kv_wr_ele->w_type = w_type;
        tuya_list_add_tail(&kv_wr_ele->node, &kv_file_async_list->list_hd);
        tal_mutex_unlock(kv_file_async_list->mutex);
        tal_semaphore_post(kv_async_wr_sem);
        op_ret = OPRT_OK;
    }
    else {
        PR_ERR("%s-%s:kv_file_async list not initilized !", __func__, __LINE__);
    }
    return op_ret;
}

STATIC KV_FILE_WR_ELEMENT_S* kv_common_write_async_ele_get(VOID)
{
    struct tuya_list_head *p = NULL;
    struct tuya_list_head *n = NULL;
    KV_FILE_WR_ELEMENT_S* entry = NULL;
    KV_FILE_WR_ELEMENT_S *kv_wr_ele = NULL;

    if (kv_file_async_list != NULL) {
        tal_mutex_lock(kv_file_async_list->mutex);
        tuya_list_for_each_safe(p, n, &kv_file_async_list->list_hd) {
            entry = tuya_list_entry(p, KV_FILE_WR_ELEMENT_S, node);
            if (NULL != entry) {
                kv_wr_ele = (KV_FILE_WR_ELEMENT_S *)tal_malloc(SIZEOF(KV_FILE_WR_ELEMENT_S));
                if (kv_wr_ele != NULL) {
                    memcpy(kv_wr_ele, entry, SIZEOF(KV_FILE_WR_ELEMENT_S));
                    DeleteNodeAndFree(entry, node);
                    entry = NULL;
                }
            }
        }
        tal_mutex_unlock(kv_file_async_list->mutex);
    }
    return kv_wr_ele;
}

STATIC VOID_T kv_common_write_async_task(VOID_T *args)
{
    KV_FILE_WR_ELEMENT_S *kv_wr_ele = NULL;
    OPERATE_RET rt = OPRT_COM_ERROR;

    for(;;) {
        tal_semaphore_wait(kv_async_wr_sem, SEM_WAIT_FOREVER);
        while((kv_wr_ele = kv_common_write_async_ele_get()) != NULL) {
            //PR_NOTICE("%s-%d: %s write key '%s' len '%d' start ----->", __func__, __LINE__, 
            //    (kv_wr_ele->w_type == TY_KVWR_KV)?"kv":"fs_kv", kv_wr_ele->key, kv_wr_ele->len);
            if (kv_wr_ele->w_type == TY_KVWR_KV)
                rt = wd_common_write(kv_wr_ele->key, kv_wr_ele->value, kv_wr_ele->len);
            else
                rt = fs_kv_common_write(kv_wr_ele->key, (CHAR_T *)kv_wr_ele->value, kv_wr_ele->len);
            if (rt != OPRT_OK) {
                PR_ERR("%s-%d: %s write '%s' err !", __func__, __LINE__, (kv_wr_ele->w_type == TY_KVWR_KV)?"kv":"fs_kv", kv_wr_ele->key);
            }
            tal_free(kv_wr_ele->key);
            tal_free(kv_wr_ele->value);
            tal_free(kv_wr_ele);
            kv_wr_ele = NULL;
        }
    }
}

STATIC OPERATE_RET kv_common_write_async_task_create(VOID)
{
    OPERATE_RET rt = OPRT_COM_ERROR;
    THREAD_CFG_T thrd_cfg;
    THREAD_HANDLE kv_async_thread_handle = NULL;

    rt = tal_semaphore_create_init(&kv_async_wr_sem, 0, 1);
    if (rt != OPRT_OK) {
        PR_ERR("tal_semaphore_create_init err:%d", rt);
        return rt;
    }

    thrd_cfg.priority = THREAD_PRIO_1;
    thrd_cfg.stackDepth = 3*1024;
    thrd_cfg.thrdname = "kv_async_task";
    TUYA_CALL_ERR_RETURN(tal_thread_create_and_start(&kv_async_thread_handle, NULL, NULL, kv_common_write_async_task, NULL, &thrd_cfg));

    return OPRT_OK;
}
#endif

#ifdef LVGL_ENABLE_AUDIO_FILE_PLAY

#ifndef AUDIO_HW_INIT_BY_OTHERS
#if defined (ENABLE_TUYA_SPEAKER_SERVICE)               //老的音频播放
#include "tuya_speaker_service.h"
#elif defined (TUYA_APP_AUDIO_PLAYER)                   //新的基座2.0音频
#include "tuya_voice_service.h"
#endif

#include "tkl_audio.h"
#endif

#ifndef TY_SPK_DEFAULT_VOL
#define TY_SPK_DEFAULT_VOL 70
#endif
#define gui_volume_key                     "gui_volume"
#define AUDIO_QUEUE_NUM 32

STATIC QUEUE_HANDLE audio_play_queue = NULL;
STATIC INT32_T gui_spk_volume = TY_SPK_DEFAULT_VOL;
STATIC INT32_T gui_spk_saved_volume = 0xFF;
STATIC TIMER_ID volume_save_timer = NULL;

STATIC VOID  gui_audio_play_volume_restore(VOID)
{
    CHAR_T buf[16] = {0};

    if (gui_spk_saved_volume != gui_spk_volume) {
        PR_NOTICE("restore volume as '%d'!", gui_spk_volume);
        memset(buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf), "%d", gui_spk_volume);
        wd_common_write(gui_volume_key, (CONST BYTE_T *)buf, strlen(buf)+1);
        gui_spk_saved_volume = gui_spk_volume;
        PR_NOTICE("success to restore volume as '%d'!", gui_spk_volume);
    }
    else {
        PR_NOTICE("ignore repeatedly restore volume as '%d'!", gui_spk_volume);
    }
}

STATIC VOID gui_audio_play_volume_save_timer_cb(TIMER_ID timer_id, VOID_T *arg)
{
    gui_audio_play_volume_restore();
}

STATIC VOID  gui_audio_play_volume_delay_restore(VOID)
{
    //屏幕控件操作太频繁,且同时操作写flash操作,系统会卡住,故此处延后保存音量!
    if (tal_sw_timer_is_running(volume_save_timer))
        tal_sw_timer_stop(volume_save_timer);
    tal_sw_timer_start(volume_save_timer, 5000, TAL_TIMER_ONCE);
}

OPERATE_RET gui_audio_play_event_enqueue(GUI_AUDIO_EVENT_TYPE_E evt_type, CHAR_T* audio_file, INT32_T volume)
{
    OPERATE_RET op_ret = OPRT_COM_ERROR;
    GUI_AUDIO_EVENT_S *audio_evt = NULL;

    if (evt_type == T_GUI_AUDIO_EVT_PLAY && audio_file == NULL) {
        PR_ERR("audio_evt empty audio play file");
        return op_ret;
    }

    audio_evt = (GUI_AUDIO_EVENT_S*)Malloc(SIZEOF(GUI_AUDIO_EVENT_S));
    if( audio_evt == NULL){
        PR_ERR("audio_evt Malloc Failed");
        return op_ret;
    }
    memset(audio_evt, 0, SIZEOF(GUI_AUDIO_EVENT_S));
    audio_evt->audio_evt_type = evt_type;

    if (audio_evt->audio_evt_type == T_GUI_AUDIO_EVT_PLAY) {
        audio_evt->gui_audio_file = (CHAR_T *)Malloc(strlen(audio_file) + 1);
        if( audio_evt->gui_audio_file == NULL){
            PR_ERR("audio_evt Malloc file Failed");
            Free(audio_evt);
            return op_ret;
        }
        memset(audio_evt->gui_audio_file, 0, strlen(audio_file) + 1);
        strcpy(audio_evt->gui_audio_file, audio_file);
    }
    else if (audio_evt->audio_evt_type == T_GUI_AUDIO_EVT_VOLUME) {
        audio_evt->volume = volume;
        if (audio_evt->volume < 0)
            audio_evt->volume = 0;
        else if (audio_evt->volume > 100)
            audio_evt->volume = 100;
    }

    op_ret = tal_queue_post(audio_play_queue, &audio_evt, 0);
    if (OPRT_OK != op_ret) {
        PR_ERR("audio event queue post err");
        if (audio_evt->audio_evt_type == T_GUI_AUDIO_EVT_PLAY && audio_evt->gui_audio_file != NULL)
            Free(audio_evt->gui_audio_file);
        Free(audio_evt);
    }

    return op_ret;
}

STATIC VOID_T gui_audio_play_async_task(VOID_T *args)
{
    OPERATE_RET op_ret = OPRT_OK;
    GUI_AUDIO_EVENT_S *audio_evt = NULL;

    for(;;) {
        op_ret = tal_queue_fetch(audio_play_queue, &audio_evt, QUEUE_WAIT_FROEVER);
        if (OPRT_OK != op_ret) {
            PR_ERR("wait gui rx dp queue error, %d", op_ret);
        }
        else {
            PR_NOTICE("%s-%d: recv audio event '%s'", __func__, __LINE__, 
                (audio_evt->audio_evt_type == T_GUI_AUDIO_EVT_PLAY)?"play file":    \
                (audio_evt->audio_evt_type == T_GUI_AUDIO_EVT_VOLUME)?"set volume":  \
                (audio_evt->audio_evt_type == T_GUI_AUDIO_EVT_PAUSE)?"pause":  \
                (audio_evt->audio_evt_type == T_GUI_AUDIO_EVT_RESUME)?"resume":"unknown");
            if (audio_evt->audio_evt_type == T_GUI_AUDIO_EVT_PLAY) {
                #ifdef AUDIO_HW_INIT_BY_OTHERS
                    //send to ai module to play
                #else
                    //local play
                    #if defined (ENABLE_TUYA_SPEAKER_SERVICE)
                        if (tuya_speaker_service_tone_is_playing()) {
                            tuya_speaker_service_tone_stop();
                        }
                        if (tuya_speaker_service_is_playing()) {
                            tuya_speaker_service_stop();
                        }
                        tuya_speaker_service_local_play(audio_evt->gui_audio_file, NULL, NULL, 1, 0);
                    #elif defined (TUYA_APP_AUDIO_PLAYER)
                        if (tuya_voice_svc_tone_is_playing())
                            tuya_voice_svc_tone_stop();
                        tuya_voice_svc_tone_play(TUYA_VUTILS_CODEC_TYPE_MP3, audio_evt->gui_audio_file, FALSE);
                    #endif
                #endif
                Free(audio_evt->gui_audio_file);
            }
            else if (audio_evt->audio_evt_type == T_GUI_AUDIO_EVT_VOLUME) {
                #ifdef AUDIO_HW_INIT_BY_OTHERS
                    //send to ai module to set volume
                #else
                    //local set volume
                    tkl_ao_set_vol(TKL_AUDIO_TYPE_BOARD, TKL_AO_0, NULL, audio_evt->volume);
                #endif
                gui_spk_volume = audio_evt->volume;
                gui_audio_play_volume_delay_restore();
            }
            else if (audio_evt->audio_evt_type == T_GUI_AUDIO_EVT_PAUSE) {
                #ifdef AUDIO_HW_INIT_BY_OTHERS
                    //send to ai module to do pause
                #else
                    #if defined (ENABLE_TUYA_SPEAKER_SERVICE)
                        if (tuya_audio_player_get_status(TUYA_AUDIO_PLAYER_TYPE_MUSIC) == TUYA_PLAYER_STATE_PLAYING)
                            tuya_speaker_service_pause(0);
                    #elif defined (TUYA_APP_AUDIO_PLAYER)
                        //TODO...
                    #endif
                #endif
            }
            else if (audio_evt->audio_evt_type == T_GUI_AUDIO_EVT_RESUME) {
                #ifdef AUDIO_HW_INIT_BY_OTHERS
                    //send to ai module to do resume
                #else
                    #if defined (ENABLE_TUYA_SPEAKER_SERVICE)
                        if (tuya_audio_player_get_status(TUYA_AUDIO_PLAYER_TYPE_MUSIC) == TUYA_PLAYER_STATE_PAUSED)
                            tuya_speaker_service_resume(0);
                    #elif defined (TUYA_APP_AUDIO_PLAYER)
                        //TODO...
                    #endif
                #endif
            }
            Free(audio_evt);
            audio_evt=  NULL;
        }
    }
}

#ifndef AUDIO_HW_INIT_BY_OTHERS
STATIC OPERATE_RET gui_audio_play_hardware_init(GUI_AUDIO_INFO_S *audio_info)
{
    OPERATE_RET rt = OPRT_COM_ERROR;
    TKL_AUDIO_CONFIG_T audio_config;
    INT32_T spk_gpio            = TUYA_GPIO_NUM_28;
    INT32_T spk_gpio_polarity   = TUYA_GPIO_LEVEL_LOW;
    UINT_T volume_len = 0;
    CHAR_T* volume_str = NULL;

    gui_spk_volume = TY_SPK_DEFAULT_VOL;
    if (audio_info != NULL) {
        spk_gpio = audio_info->spk_gpio;
        spk_gpio_polarity = audio_info->spk_gpio_polarity;
        gui_spk_volume = audio_info->spk_default_vol;
    }
    memset(&audio_config, 0, sizeof(TKL_AUDIO_CONFIG_T));
    audio_config.enable                 = 0;
    audio_config.ai_chn                 = 0;
    audio_config.sample                 = TKL_AUDIO_SAMPLE_16K;
    audio_config.spk_sample             = TKL_AUDIO_SAMPLE_16K;
    audio_config.datebits               = TKL_AUDIO_DATABITS_16;
    audio_config.channel                = TKL_AUDIO_CHANNEL_MONO;
    audio_config.codectype              = TKL_CODEC_AUDIO_PCM;
    audio_config.card                   = TKL_AUDIO_TYPE_BOARD;
    audio_config.spk_gpio               = spk_gpio;
    audio_config.spk_gpio_polarity      = spk_gpio_polarity;
    audio_config.put_cb                 = NULL;

    TUYA_CALL_ERR_RETURN(tkl_ai_init(&audio_config, 0));
    TUYA_CALL_ERR_RETURN(tkl_ai_start(0, 0));

    rt = wd_common_read(gui_volume_key, (BYTE_T **)&volume_str, &volume_len);
    if (rt != OPRT_OK) {
        gui_audio_play_volume_restore();
    }
    else {
        gui_spk_volume = atoi(volume_str);
        Free(volume_str);
    }
    gui_spk_saved_volume = gui_spk_volume;
    PR_NOTICE("%s-%d: default gui volume '%d' !!!", __func__, __LINE__, gui_spk_volume);
    tkl_ao_set_vol(TKL_AUDIO_TYPE_BOARD, TKL_AO_0, NULL, gui_spk_volume);
    return rt;
}

STATIC VOID gui_audio_play_status_notify(UINT32_T evt)
{
    LV_DISP_EVENT_S Sevt = {0};
    LV_TUYAOS_EVENT_S g_dp_evt_s = {0};

    Sevt.event = LLV_EVENT_VENDOR_TUYAOS;
    g_dp_evt_s.event_typ = LV_TUYAOS_EVENT_AUDIO_PLAY_STATUS;
    g_dp_evt_s.param1 = evt;
    Sevt.param = (UINT_T)&g_dp_evt_s;
    tuya_disp_gui_event_send(&Sevt);
}

#if defined(ENABLE_TUYA_SPEAKER_SERVICE)
STATIC OPERATE_RET gui_audio_play_event_cb(TUYA_SPEAKER_SERVICE_EVENT_INFO_S *info, VOID *user_data)
{
    PR_NOTICE("%s-%d---------------> recv event '%d' !!!", __func__, __LINE__, info->event);
    if (TUYA_PLAYER_EVENT_FINISHED == info->event || TUYA_PLAYER_EVENT_STOPPED == info->event ||
        TUYA_PLAYER_EVENT_PAUSED == info->event || TUYA_PLAYER_EVENT_STARTED == info->event) {
        gui_audio_play_status_notify((UINT32_T)info->event);
    }
    return OPRT_OK;
}
#elif defined (TUYA_APP_AUDIO_PLAYER)
STATIC OPERATE_RET gui_voice_event_cd(TUYA_VOICE_SVC_EVT_INFO_S *info, VOID *userdata)
{
    PR_NOTICE("%s-%d---------------> recv event '%d' !!!", __func__, __LINE__, info->event_id);
    if (TUYA_PLAYER_EVENT_FINISHED == info->event_id || TUYA_PLAYER_EVENT_STOPPED == info->event_id ||
        TUYA_PLAYER_EVENT_PAUSED == info->event_id) {
        gui_audio_play_status_notify((UINT32_T)info->event_id);
    }
    return OPRT_OK;
}
#endif

#endif

INT32_T gui_audio_play_volume_get(VOID)
{
    return gui_spk_volume;
}

STATIC OPERATE_RET gui_audio_play_async_task_create(GUI_AUDIO_INFO_S *audio_info)
{
    OPERATE_RET rt = OPRT_COM_ERROR;
    THREAD_CFG_T thrd_cfg;
    THREAD_HANDLE audio_play_thread_handle = NULL;

#ifndef AUDIO_HW_INIT_BY_OTHERS
    //init audio player
    rt = gui_audio_play_hardware_init(audio_info);
    if (rt != OPRT_OK) {
        PR_ERR("tuya audio hardware init error, %d", rt);
        return rt;
    }
    #if defined(ENABLE_TUYA_SPEAKER_SERVICE)
        TUYA_SPEAKER_SERVICE_CONFIG_S player_cfg = {0};
        rt = tuya_speaker_service_init(&player_cfg);
        if (rt != OPRT_OK) {
            PR_ERR("tuya speaker service init error, %d", rt);
            return rt;
        }
        rt = tuya_speaker_service_event_register(DEF_STR_EVENT_MUSIC, gui_audio_play_event_cb, NULL);
        if (rt != OPRT_OK) {
            PR_ERR("tuya speaker service event register error, %d", rt);
            return rt;
        }
    #elif defined (TUYA_APP_AUDIO_PLAYER)
            TUYA_VOICE_SVC_INIT_PARAMS_S player_cfg = {
                .cloud_support = FALSE,
            };
            rt = tuya_voice_svc_init(&player_cfg);
            if (rt != OPRT_OK) {
                PR_ERR("tuya speaker service init error, %d", rt);
                return rt;
            }
            rt = tuya_voice_svc_evt_register(TUYA_VOICE_SVC_EVT_TYPE_TONE, TUYA_VOICE_SVC_EVT_PRIO_NORMAL, gui_voice_event_cd, NULL);
            if (rt != OPRT_OK) {
                PR_ERR("tuya speaker service event register error, %d", rt);
                return rt;
            }
    #endif
#endif

    TUYA_CALL_ERR_RETURN(tal_queue_create_init(&audio_play_queue, SIZEOF(VOID *), AUDIO_QUEUE_NUM));

    thrd_cfg.priority = THREAD_PRIO_1;
    thrd_cfg.stackDepth = 3*1024;
    thrd_cfg.thrdname = "audio_play_task";
    TUYA_CALL_ERR_RETURN(tal_thread_create_and_start(&audio_play_thread_handle, NULL, NULL, gui_audio_play_async_task, NULL, &thrd_cfg));

    TUYA_CALL_ERR_RETURN(tal_sw_timer_create(gui_audio_play_volume_save_timer_cb, NULL, &volume_save_timer));

    return OPRT_OK;
}
#endif

//cp0请求从cp1申请内存
VOID *cp0_req_cp1_malloc_psram(IN UINT_T buf_len)
{
    LV_DISP_EVENT_S Sevt = {0};
    LV_TUYAOS_EVENT_S g_dp_evt_s = {0};
    VOID *buff = NULL;

    Sevt.event = LLV_EVENT_VENDOR_TUYAOS;
    g_dp_evt_s.event_typ = LV_TUYAOS_EVENT_CP1_MALLOC;
    g_dp_evt_s.param1 = (UINT32_T)&buff;
    g_dp_evt_s.param3 = buf_len;
    Sevt.param = (UINT_T)&g_dp_evt_s;
    tuya_disp_gui_event_send(&Sevt);

    if (g_dp_evt_s.param2 != OPRT_OK) {
        PR_ERR(" !!!! req cp1 malloc fail !");
    }
    return buff;
}

//cp0请求从cp1释放内存
OPERATE_RET cp0_req_cp1_free_psram(IN VOID *ptr)
{
    OPERATE_RET op_ret = OPRT_COM_ERROR;
    LV_DISP_EVENT_S Sevt = {0};
    LV_TUYAOS_EVENT_S g_dp_evt_s = {0};

    Sevt.event = LLV_EVENT_VENDOR_TUYAOS;
    g_dp_evt_s.event_typ = LV_TUYAOS_EVENT_CP1_FREE;
    g_dp_evt_s.param1 = (UINT32_T)ptr;
    Sevt.param = (UINT_T)&g_dp_evt_s;
    tuya_disp_gui_event_send(&Sevt);
    if (g_dp_evt_s.param2 != OPRT_OK) {
        PR_ERR(" !!!! req cp1 free fail !");
    }
    else
        op_ret = OPRT_OK;
    return op_ret;
}

STATIC VOID tuya_gui_pre_init_notify_cb(TIMER_ID timerID,PVOID_T pTimerArg)
{
    PR_NOTICE("---------------> pre init notify start!!!");
    if (gui_dp2obj_pre_init_hdl) {
        gui_dp2obj_pre_init_hdl();   //表示用户有控件事件发送
    }
}

STATIC VOID_T __app_gui_unbind_dev_msg_cb(VOID_T *data)
{
    STATIC BOOL_T is_unbinding = FALSE;

    if (!is_unbinding) {
        GW_RESET_S rst = {
            GRT_LOCAL, FALSE
        };
        if (gw_unactive(&rst) == OPRT_OK)               //(tuya_devos_enable_hot_reset:解绑不重启?)
            is_unbinding = TRUE;
        else {
            PR_ERR("[%s][%d]:gw_unactive error", __func__, __LINE__);
        }
    }
}

STATIC VOID tuya_gui_event_cb(LV_DISP_EVENT_S *evt)
{
    OPERATE_RET op_ret = OPRT_COM_ERROR;
    BOOL_T direct_rp = FALSE;

    if ((evt->event & LLV_EVENT_BY_DIRECT_REPORT) != 0) {
        PR_NOTICE("[%s][%d]---------------cp0 recv tag '%s' direct report event '0x%08x' !", __func__, __LINE__, 
            (evt->tag!=NULL)?(CHAR_T *)(evt->tag):"null", evt->event);
        direct_rp = TRUE;
    }
    evt->event &= ~LLV_EVENT_BY_DIRECT_REPORT;          //delete filter flag!
    if (evt->event > LLV_EVENT_VENDOR_MIN && evt->event < LLV_EVENT_VENDOR_USER_MAX) {     //用户系统事件处理
        if (gui_system_event_hdl != NULL)
            gui_system_event_hdl(evt);
        return;
    }
    else if (evt->event > LLV_EVENT_VENDOR_USER_MAX){
        if (evt->event == LLV_EVENT_VENDOR_TUYAOS) {
            LV_TUYAOS_EVENT_S *g_dp_evt_s = (LV_TUYAOS_EVENT_S *)(evt->param);

            switch(g_dp_evt_s->event_typ) {
                case LV_TUYAOS_EVENT_DP_INFO_CREATE:                //cp1:请求创建DP结构体
                    {
                        DEV_CNTL_N_S *dev_cntl = get_gw_dev_cntl();
                        if (dev_cntl) {
                            PR_NOTICE("[%s][%d]--------------->>>>>>>>>>>>>>>>>>>>dp info create request ok !", __func__, __LINE__);
                            g_dp_evt_s->param1 = (UINT32_T)dev_cntl;
                            g_dp_evt_s->param2 = OPRT_OK;   //获取成功!
                        }
                        else {
                            PR_NOTICE("[%s][%d]--------------->>>>>>>>>>>>>>>>>>>>dp info create request fail !", __func__, __LINE__);
                            g_dp_evt_s->param2 = 1;         //获取失败!
                            if (get_gw_active() != ACTIVATED) {
                                //启动激活事件通知注册!
                                PR_NOTICE("[%s][%d]--------------->>>>>>>>>>>>>>>>>>>>start next active capture !", __func__, __LINE__);
                                ty_subscribe_event(EVENT_LINK_ACTIVATE, "app_gui", __app_gui_activated_event, SUBSCRIBE_TYPE_NORMAL);
                            }
                            else {
                                //启动设备初始化成功事件通知注册!
                                PR_NOTICE("[%s][%d]--------------->>>>>>>>>>>>>>>>>>>>start wait device initialized !", __func__, __LINE__);
                                ty_subscribe_event(EVENT_INIT, "app_gui", __app_gui_dev_initialized_event, SUBSCRIBE_TYPE_NORMAL);
                            }
                        }
                    }
                    break;

                case LV_TUYAOS_EVENT_FS_PATH_TYPE_REPORT:           //cp1:请求获取当前文件路径类型信息
                    {
                        g_dp_evt_s->param1 = (UINT_T)tuya_app_gui_get_path_type();
                        //g_dp_evt_s->param1 = (UINT_T)TUYA_GUI_FS_PP_MAIN;
                        PR_NOTICE("[%s][%d]--------------->>>>>>>>>>>>>>>>>>>> fs path type '%d'!", __func__, __LINE__, g_dp_evt_s->param1);
                    }
                    break;

                case LV_TUYAOS_EVENT_FS_FORMAT_REPORT:              //cp1:请求格式化文件系统,慎用!
                    {
                        PR_NOTICE("[%s][%d]--------------->>>>>>>>>>>>>>>>>>>>disk format request !", __func__, __LINE__);
                        g_dp_evt_s->param2 = 1;
                        op_ret = tal_workq_init_delayed(WORKQ_HIGHTPRI, __app_gui_disk_format_msg_cb, NULL, &gui_disk_format_work);
                        if (OPRT_OK != op_ret) {
                            PR_ERR("tal_workq_init_delayed disk format err '%d'!", op_ret);
                        }
                        else {
                            op_ret = tal_workq_start_delayed(gui_disk_format_work, 200, LOOP_ONCE);
                            if (OPRT_OK != op_ret) {
                                PR_ERR("tal_workq_start_delayed disk format err '%d'!", op_ret);
                                tal_workq_cancel_delayed(gui_disk_format_work);
                                gui_disk_format_work = NULL;
                            }
                            else
                                g_dp_evt_s->param2 = OPRT_OK;
                        }
                    }
                    break;

                case LV_TUYAOS_EVENT_WIFI_STATUS_REPORT:            //cp1:请求获取当前wifi连接状态
                    {
                        STATIC SCHAR_T rssi = 0;
                        STATIC CONST CHAR_T *ssid = NULL;
                    #if 0
                        GW_WIFI_NW_STAT_E cur_nw_stat;
                        if (get_wf_gw_nw_status(&cur_nw_stat) == OPRT_OK)
                            g_dp_evt_s->param1 = (UINT_T)cur_nw_stat;
                        else
                            g_dp_evt_s->param1 = (UINT_T)STAT_LOW_POWER;
                    #else
                        rssi = 0;
                        ssid = NULL;
                        if (get_gw_nw_status() == GNS_WAN_VALID) {      //连接状态
                            gw_get_rssi(&rssi);
                            ssid = get_gw_ssid();
                            g_dp_evt_s->param1 = 1;
                            g_dp_evt_s->param2 = (uint32_t)&rssi;
                            g_dp_evt_s->param3 = (uint32_t)ssid;
                        }
                        else            //断网状态
                            g_dp_evt_s->param1 = 0;
                    #endif
                        PR_NOTICE("[%s][%d]--------------->>>>>>>>>>>>>>>>>>>> wifi status '%s', rssi '%d', ssid '%s'!", __func__, __LINE__, 
                            g_dp_evt_s->param1?"on":"off", rssi, (ssid==NULL)?"null":ssid);
                    }
                    break;

                case LV_TUYAOS_EVENT_ACTIVE_STATE_REPORT:
                    {
                        if (get_gw_cntl()->gw_wsm.stat == ACTIVATED) {
                            g_dp_evt_s->param1 = 1;
                        }
                        else {
                            g_dp_evt_s->param1 = 0;
                        }
                    }
                    break;

                case LV_TUYAOS_EVENT_UNBIND_DEV_EXECUTE:            //cp1:请求接绑设备
                    {
                        PR_NOTICE("[%s][%d]--------------->dev unbind start...............", __func__, __LINE__);
                        op_ret = tal_workq_init_delayed(WORKQ_HIGHTPRI, __app_gui_unbind_dev_msg_cb, NULL, &gui_unbind_dev_work);
                        if (OPRT_OK != op_ret) {
                            PR_ERR("tal_workq_init_delayed unbind dev err '%d'!", op_ret);
                        }
                        else {
                            op_ret = tal_workq_start_delayed(gui_unbind_dev_work, 100, LOOP_ONCE);
                            if (OPRT_OK != op_ret) {
                                PR_ERR("tal_workq_start_delayed unbind dev  err '%d'!", op_ret);
                                tal_workq_cancel_delayed(gui_unbind_dev_work);
                                gui_unbind_dev_work = NULL;
                            }
                        }
                    }
                    break;

                case LV_TUYAOS_EVENT_LOCAL_TIME_REPORT:             //cp1:请求获取当前时间
                    {
                        POSIX_TM_S st_time;
                        STATIC CHAR_T time_str[32] = { 0 };
                        memset(&st_time, 0, SIZEOF(POSIX_TM_S));
                        g_dp_evt_s->param2 = 1;         //状态标识!
                        if (tal_time_check_time_sync() == OPRT_OK && tal_time_check_time_zone_sync() == OPRT_OK) {
                            if (OPRT_OK == tal_time_get_local_time_custom(0, &st_time)) {
                                memset(time_str, 0, SIZEOF(time_str));
                                snprintf(time_str, SIZEOF(time_str), "%d %02d-%02d %02d:%02d:%02d",
                                    st_time.tm_year+1900, st_time.tm_mon + 1, st_time.tm_mday, st_time.tm_hour, st_time.tm_min, st_time.tm_sec);
                                PR_DEBUG("!!!! Now time:[%s]", time_str);
                                g_dp_evt_s->param1 = (UINT32_T)time_str;
                                g_dp_evt_s->param2 = OPRT_OK;
                            }
                            else
                                PR_ERR("get_local_time err !");
                        }
                        else
                            PR_ERR("check_time_sync err !");
                    }
                    break;

                case LV_TUYAOS_EVENT_WEATHER_REPORT:                //cp1:请求获取当前天气预报
                    {
                        STATIC CHAR_T *_weather = NULL;
                        UINT_T _len = 0;

                        if (_weather != NULL)
                            Free(_weather);
                        _weather = NULL;
                        if (ty_gfw_gui_weather_get(&_weather, &_len) == OPRT_OK) {
                            g_dp_evt_s->param1 = (UINT32_T)(_weather+1);
                            g_dp_evt_s->param2 = OPRT_OK;
                            g_dp_evt_s->param3 = _len-1;
                        }
                        else {
                            g_dp_evt_s->param2 = 1;         //获取失败!
                        }
                    }
                    break;

                case LV_TUYAOS_EVENT_MAIN_SCREEN_STRATUP:           //cp1: 通知主屏幕启动完成
                    {
                        BOOL_T has_bootonpage = (BOOL_T)g_dp_evt_s->param1;
                        PR_NOTICE("[%s][%d]--------------->>>>>>>>>>>>>>>>>>>>main screen startup completed %s bootOnPage !", __func__, __LINE__,
                            (has_bootonpage==true)?"with":"without");
                        gui_screen_loaded = GUI_BOOTON_WITHOUT_PAGE;
                        if (has_bootonpage) {
                            TIMER_ID  pre_init_timer_id = NULL;
                            gui_screen_loaded = GUI_BOOTON_WITH_PAGE;
                            tal_sw_timer_create(tuya_gui_pre_init_notify_cb,NULL,&pre_init_timer_id);
                            tal_sw_timer_start(pre_init_timer_id,100,TAL_TIMER_ONCE);
                        }
                        ty_publish_event(EVENT_GUI_READY_NOTIFY, NULL);
                    }
                    break;

            #ifdef TUYA_GUI_RESOURCE_DOWNLAOD
                case LV_TUYAOS_EVENT_RESOURCE_UPDATE:               //cp1:请求查询是否有新资源
                    {
                        GW_CNTL_S *gw_cntl = get_gw_cntl();

                        PR_NOTICE("[%s][%d]--------------->>>>>>>>>>>>>>>>>>>>resource update start !", __func__, __LINE__);
                        if (gw_cntl->gw_wsm.stat == ACTIVATED) {     //设备处于绑定状态!
                            if (ty_gfw_gui_file_init() == OPRT_OK)
                                g_dp_evt_s->param2 = OPRT_OK;             //执行成功!
                            else
                                g_dp_evt_s->param2 = 1;                   //执行失败!
                        }
                        else {      //设备未绑定,无效!
                            g_dp_evt_s->param2 = 2;                   //执行失败, 设备未绑定!
                        }
                    }
                    break;

                case LV_TUYAOS_EVENT_REFUSE_RESOURCE_UPDATE:        //cp1拒绝执行更新资源
                    {
                        tfm_kepler_gui_files_download_action(FALSE);
                    }
                    break;

                case LV_TUYAOS_EVENT_AGREE_RESOURCE_UPDATE:         //cp1接受执行更新资源
                    {
                        tfm_kepler_gui_files_download_action(TRUE);
                    }
                    break;
            #endif

                case LV_TUYAOS_EVENT_KV_WRITE:                      //cp1:请求kv写操作
                    {
                    #ifdef USER_CONFIG_KV_FILE_ASYNC_WRITE
                        if (kv_common_write_async((CHAR_T *)g_dp_evt_s->param1, (BYTE_T *)g_dp_evt_s->param3, (UINT_T)g_dp_evt_s->param4, TY_KVWR_KV) == OPRT_OK)
                            g_dp_evt_s->param2 = OPRT_OK;           //执行成功!
                        else
                            g_dp_evt_s->param2 = 1;                 //执行失败!
                    #else
                        if (wd_common_write((CHAR_T *)g_dp_evt_s->param1, (BYTE_T *)g_dp_evt_s->param3, (UINT_T)g_dp_evt_s->param4) == OPRT_OK)
                            g_dp_evt_s->param2 = OPRT_OK;           //执行成功!
                        else
                            g_dp_evt_s->param2 = 1;                 //执行失败!
                    #endif
                    }
                    break;

                case LV_TUYAOS_EVENT_KV_READ:                       //cp1:请求kv读操作
                    {
                        STATIC BYTE_T *kv_out_val = NULL;
                        if (kv_out_val != NULL)
                            Free(kv_out_val);
                        kv_out_val = NULL;

                        if (wd_common_read((CHAR_T *)g_dp_evt_s->param1, (BYTE_T **)&kv_out_val, (UINT_T *)g_dp_evt_s->param4) == OPRT_OK) {
                            g_dp_evt_s->param3 = (UINT_T)&kv_out_val;
                            g_dp_evt_s->param2 = OPRT_OK;           //执行成功!
                        }
                        else
                            g_dp_evt_s->param2 = 1;                 //执行失败!
                    }
                    break;

            #if defined(TUYA_FILE_SYSTEM) && (TUYA_FILE_SYSTEM == 1)
                case LV_TUYAOS_EVENT_FS_KV_WRITE:                   //cp1:请求文件kv写操作
                    {
                    #ifdef USER_CONFIG_KV_FILE_ASYNC_WRITE
                        if (kv_common_write_async((CHAR_T *)g_dp_evt_s->param1, (BYTE_T *)g_dp_evt_s->param3, (UINT_T)g_dp_evt_s->param4, TY_KVWR_FS) == OPRT_OK)
                            g_dp_evt_s->param2 = OPRT_OK;           //执行成功!
                        else
                            g_dp_evt_s->param2 = 1;                 //执行失败!
                    #else
                        if (fs_kv_common_write((CHAR_T *)g_dp_evt_s->param1, (CHAR_T *)g_dp_evt_s->param3, (UINT_T)g_dp_evt_s->param4) == OPRT_OK)
                            g_dp_evt_s->param2 = OPRT_OK;           //执行成功!
                        else
                            g_dp_evt_s->param2 = 1;                 //执行失败!
                    #endif
                    }
                    break;

                case LV_TUYAOS_EVENT_FS_KV_READ:                    //cp1:请求文件kv读操作
                    {
                        STATIC CHAR_T *kv_out_str = NULL;
                        if (kv_out_str != NULL)
                            Free(kv_out_str);
                        kv_out_str = NULL;
                        
                        if (fs_kv_common_read((CHAR_T *)g_dp_evt_s->param1, (CHAR_T **)&kv_out_str, (UINT_T *)g_dp_evt_s->param4) == OPRT_OK) {
                            g_dp_evt_s->param3 = (UINT_T)&kv_out_str;
                            g_dp_evt_s->param2 = OPRT_OK;           //执行成功!
                        }
                        else
                            g_dp_evt_s->param2 = 1;                 //执行失败!
                    }
                    break;

                //case LV_TUYAOS_EVENT_FS_KV_DELETE:
                //    {
                //        fs_kv_common_delete_config((BOOL_T)g_dp_evt_s->param1);
                //    }
                //    break;
            #endif

                case LV_TUYAOS_EVENT_CP0_MALLOC:                    //来自cp1:请求分配psram
                    {
                        void *p_buff = (void *)tkl_system_psram_malloc(g_dp_evt_s->param3);
                        if (p_buff != NULL) {
                            memset(p_buff, 0, g_dp_evt_s->param3);
                            *(void **)g_dp_evt_s->param1 = p_buff;
                            g_dp_evt_s->param2 = OPRT_OK;
                        }
                        else
                            g_dp_evt_s->param2 = 1;
                        p_buff = NULL;
                    }
                    break;

                case LV_TUYAOS_EVENT_CP0_FREE:                      //来自cp1:请求释放psram
                    {
                        tkl_system_psram_free((void *)g_dp_evt_s->param1);
                        g_dp_evt_s->param2 = OPRT_OK;
                    }
                    break;

                case LV_TUYAOS_EVENT_AUDIO_FILE_PLAY:               //来自cp1:请求播放音频文件
                    {
                        CHAR_T *audio_flie = (CHAR_T *)g_dp_evt_s->param1;
                        PR_NOTICE("[%s][%d]--------------->>>>>>>>>>>>>>>>>>>>recv audio file '%s' play request !", __func__, __LINE__, audio_flie);
                    #ifdef LVGL_ENABLE_AUDIO_FILE_PLAY
                        if (gui_audio_play_event_enqueue(T_GUI_AUDIO_EVT_PLAY, audio_flie, 0) == OPRT_OK)
                            g_dp_evt_s->param2 = OPRT_OK;           //执行成功!
                        else
                            g_dp_evt_s->param2 = 1;                 //执行失败
                    #else
                            g_dp_evt_s->param2 = 1;                 //执行失败
                    #endif
                    }
                    break;

                case LV_TUYAOS_EVENT_AUDIO_PLAY_PAUSE:               //来自cp1:请求播放暂停
                    {
                    #ifdef LVGL_ENABLE_AUDIO_FILE_PLAY
                        if (gui_audio_play_event_enqueue(T_GUI_AUDIO_EVT_PAUSE, NULL, 0) == OPRT_OK)
                            g_dp_evt_s->param2 = OPRT_OK;           //执行成功!
                        else
                            g_dp_evt_s->param2 = 1;                 //执行失败
                    #else
                            g_dp_evt_s->param2 = 1;                 //执行失败
                    #endif
                    }
                    break;

                case LV_TUYAOS_EVENT_AUDIO_PLAY_RESUME:               //来自cp1:请求播放继续
                    {
                    #ifdef LVGL_ENABLE_AUDIO_FILE_PLAY
                        if (gui_audio_play_event_enqueue(T_GUI_AUDIO_EVT_RESUME, NULL, 0) == OPRT_OK)
                            g_dp_evt_s->param2 = OPRT_OK;           //执行成功!
                        else
                            g_dp_evt_s->param2 = 1;                 //执行失败
                    #else
                            g_dp_evt_s->param2 = 1;                 //执行失败
                    #endif
                    }
                    break;

                case LV_TUYAOS_EVENT_AUDIO_VOLUME_SET:              //来自cp1:请求设置音量
                    {
                        INT32_T volume = (INT32_T)g_dp_evt_s->param1;
                        PR_NOTICE("[%s][%d]--------------->>>>>>>>>>>>>>>>>>>>recv set volume to '%d' request !", __func__, __LINE__, volume);
                    #ifdef LVGL_ENABLE_AUDIO_FILE_PLAY
                        if (gui_audio_play_event_enqueue(T_GUI_AUDIO_EVT_VOLUME, NULL, volume) == OPRT_OK)
                            g_dp_evt_s->param2 = OPRT_OK;           //执行成功!
                        else
                            g_dp_evt_s->param2 = 1;                 //执行失败

                    #else
                            g_dp_evt_s->param2 = 1;                 //执行失败
                    #endif
                    }
                    break;

                case LV_TUYAOS_EVENT_AUDIO_VOLUME_GET:              //来自cp1:请求获取音量
                    {
                        PR_NOTICE("[%s][%d]--------------->>>>>>>>>>>>>>>>>>>>recv get volume request !", __func__, __LINE__);
                    #ifdef LVGL_ENABLE_AUDIO_FILE_PLAY
                            g_dp_evt_s->param1 = (UINT32_T)gui_audio_play_volume_get();
                            g_dp_evt_s->param2 = OPRT_OK;           //执行成功!

                    #else
                            g_dp_evt_s->param2 = 1;                 //执行失败
                    #endif
                    }
                    break;

            #ifdef LVGL_ENABLE_CLOUD_RECIPE
                case LV_TUYAOS_EVENT_CLOUD_RECIPE:                  //cp1:请求访问云食谱
                    {
                        if (ty_gfw_gui_cloud_recipe_req((VOID *)g_dp_evt_s->param1, 1024) == OPRT_OK)
                            g_dp_evt_s->param2 = OPRT_OK;           //执行成功!
                        else
                            g_dp_evt_s->param2 = 1;                 //执行失败!
                    }
                    break;
            #endif

                default:
                    PR_NOTICE("[%s][%d] cp0 recv unknonw common event '%d' !!!\r\n", __func__, __LINE__, g_dp_evt_s->event_typ);
                    break;
            }
        }
        else {
            PR_NOTICE("[%s][%d] internal system event '%d' unprocessed !!!\r\n", __func__, __LINE__, evt->event);
        }
        return;
    }

    if (gui_initialized) {          //对象事件处理
        /*
        Notice:从gui传过来的屏幕控件事件分两种情况处理:
               1:不需要cp0处理业务逻辑直接转成dp点在这里上报.
               2:需要后面的gui_obj_event_hdl接管,cp0处理业务逻辑后由业务代码自己上报dp.(避免重复上报,那么在这里就不需要直接上报了!)
        */
    #if defined(TUYA_UI2DP_CONVERT) && (TUYA_UI2DP_CONVERT == 1)
        if (direct_rp == TRUE)
            tuya_gui_widget_to_dp_evt_send(evt);    //widget->dp上报通知
    #endif
        if (gui_obj_event_hdl != NULL)              //用户需要额外处理对象，如通用固件需下发至MCU(soc不需要注册处理!)
            gui_obj_event_hdl(evt);
    }
}

STATIC VOID tuya_app_recv_lv_event(VOID *buf, UINT_T len, VOID *args)
{
    app_message_t *msg = NULL;
#ifdef TUYA_APP_MULTI_CORE_IPC
#ifdef TUYA_APP_USE_TAL_IPC
    msg = (app_message_t *)buf;
#else
    struct ipc_msg_s *ipc_msg = (struct ipc_msg_s *)buf;

    if (ipc_msg->type != TKL_IPC_TYPE_LVGL) {
        PR_ERR("[%s][%d]>>>>>>>>>>>>>>recv unknown msg_type '0x%x' \r\n", __func__, __LINE__, ipc_msg->type);
        return;
    }
    msg = (app_message_t *)(ipc_msg->buf32[0]);
#endif
#else
    msg = (app_message_t *)buf;
#endif
    if (msg->cmd == AIC_LV_MSG_EVT_CHANGE) {
        tuya_gui_event_cb((LV_DISP_EVENT_S *)(msg->param1));
    }
}

STATIC BACKLIGHT_OPEN_USER_CB custom_backlight_open = NULL; 
STATIC BACKLIGHT_CLOSE_USER_CB custom_backlight_close = NULL; 
STATIC BACKLIGHT_LIGHT_PERCENT_USER_CB custom_backlight_percent = NULL;

OPERATE_RET tuya_disp_lcd_backlight_open(VOID_T)
{
    if (custom_backlight_open) {
        custom_backlight_open();
    }
    else {
        #ifdef TUYA_MULTI_TYPES_LCD
            if (disp_handle)
                tal_display_bl_open(disp_handle);
            if (disp_handle_slave)
                tal_display_bl_open(disp_handle_slave);
        #else
            tkl_disp_set_brightness(NULL, 100);
        #endif
    }
    ty_disp_lcd_backlight_open();
    return OPRT_OK;
}

OPERATE_RET tuya_disp_lcd_backlight_close(VOID_T)
{
    if (custom_backlight_close) {
        custom_backlight_close();
    }
    else {
        #ifdef TUYA_MULTI_TYPES_LCD
            if (disp_handle)
                tal_display_bl_close(disp_handle);
            if (disp_handle_slave)
                tal_display_bl_close(disp_handle_slave);
        #else
            tkl_disp_set_brightness(NULL, 0);
        #endif
    }
    ty_disp_lcd_backlight_close();
    return OPRT_OK;
}

OPERATE_RET tuya_disp_lcd_backlight_set(UCHAR_T light_percent)
{
    UCHAR_T percent = 0;

    if (light_percent <= 0)
        percent = 0;
    else if (light_percent >= 100)
        percent = 100;
    else
        percent = light_percent;
    if (custom_backlight_percent) {
        custom_backlight_percent(percent);
    }
    else {
        #ifdef TUYA_MULTI_TYPES_LCD
            if (disp_handle) {
                //tal_display_bl_set(disp_handle, light_percent);
            }
            if (disp_handle_slave) {
                //tal_display_bl_set(disp_handle_slave, light_percent);
            }
        #else
            tkl_disp_set_brightness(NULL, percent);
        #endif
    }
    ty_disp_lcd_backlight_set(percent);
    return OPRT_OK;
}

OPERATE_RET tuya_disp_lcd_backlight_set_custom_register(BACKLIGHT_OPEN_USER_CB bl_open, BACKLIGHT_CLOSE_USER_CB bl_close, BACKLIGHT_LIGHT_PERCENT_USER_CB bl_percent)
{
    custom_backlight_open = bl_open;
    custom_backlight_close = bl_close;
    custom_backlight_percent = bl_percent;
    return OPRT_OK;
}

OPERATE_RET tuya_gui_lcd_open(VOID_T *info)
{
    extern unsigned char tuya_app_gui_lvgl_frame_buffer_count(void);
    extern int tuya_app_gui_get_lcd_rotation(void);
    OPERATE_RET rt = OPRT_OK;
    STATIC BOOL_T lcd_inited = FALSE;

    if (lcd_inited) {
        PR_NOTICE("----------%s lcd *re-inited*!", __func__);
        return rt;
    }
    if (info == NULL) {
        PR_NOTICE("----------%s null parameter ???", __func__);
        return OPRT_COM_ERROR;
    }
#ifdef TUYA_MULTI_TYPES_LCD
    GUI_LCDS_INFO_S *disp_info  = (GUI_LCDS_INFO_S *)info;
    GUI_LCD_INFO_S *lcd_info = NULL;
    BOOL_T lcd_type_check = FALSE;              //没有帧缓存,同时又需要屏幕扩展情况下只支持spi/qspi类型屏幕?(芯片一般只提供一组8080/RGB接口)
    UINT32_T i = 0;

    if (tuya_app_gui_lvgl_frame_buffer_count() == 0) {
        if (tuya_app_gui_get_lcd_rotation() != TKL_DISP_ROTATION_0) {
            PR_ERR("----------%s zero frame buffer can not support screen rotation ???", __func__);
            return OPRT_COM_ERROR;
        }
        if ((disp_info->exp_type == TUYA_SCREEN_EXPANSION_H_EXP) || (disp_info->exp_type == TUYA_SCREEN_EXPANSION_V_EXP)) {
            lcd_type_check = TRUE;
        }
    }

    if ((disp_info->exp_type == TUYA_SCREEN_EXPANSION_H_EXP || disp_info->exp_type == TUYA_SCREEN_EXPANSION_V_EXP) && (tuya_app_gui_get_lcd_rotation() != TKL_DISP_ROTATION_0)) {
        PR_ERR("----------%s screen expand can not support screen rotation ???", __func__);
        return OPRT_COM_ERROR;
    }

    if (disp_info->lcd_num <= 2) {              //目前仅支持2个屏幕扩展!
        lcd_info = disp_info->lcd_info[0];
        if (lcd_type_check && lcd_info->display_device->type != DISPLAY_SPI && lcd_info->display_device->type != DISPLAY_QSPI) {
            PR_ERR("----------%s zero frame buffer can not support screen expand as lcd1 type '%d'???", __func__, lcd_info->display_device->type);
            return OPRT_COM_ERROR;
        }
        disp_handle = tal_display_open(lcd_info->display_device, lcd_info->display_cfg);
        if (disp_handle == NULL) {
            rt = OPRT_COM_ERROR;
            PR_ERR("[%s][%d] fail ?\r\n", __func__, __LINE__);
        }
        else {
            if (disp_info->lcd_num > 1) {
                lcd_info = disp_info->lcd_info[1];
                if (lcd_type_check && lcd_info->display_device->type != DISPLAY_SPI && lcd_info->display_device->type != DISPLAY_QSPI) {
                    PR_ERR("----------%s zero frame buffer can not support screen expand as lcd2 type '%d'???", __func__, lcd_info->display_device->type);
                    return OPRT_COM_ERROR;
                }
                disp_handle_slave = tal_display_open(lcd_info->display_device, lcd_info->display_cfg);
                if (disp_handle_slave == NULL) {
                    rt = OPRT_COM_ERROR;
                    PR_ERR("[%s][%d] fail ?\r\n", __func__, __LINE__);
                }
                else
                    tkl_lvgl_display_handle_register((VOID *)disp_handle, (VOID *)disp_handle_slave, disp_info->exp_type);
            }
            else
                tkl_lvgl_display_handle_register((VOID *)disp_handle, NULL, disp_info->exp_type);
        }
    }
    else {
        PR_ERR("[%s][%d] --------------unsupport expanded lcd num '%d' ?\r\n", __func__, __LINE__, disp_info->lcd_num);
        rt = OPRT_COM_ERROR;
    }
    //释放资源
    for (i=0; i<disp_info->lcd_num; i++) {
        if (disp_info->lcd_info[i] != NULL)
            Free(disp_info->lcd_info[i]);
    }
    Free(disp_info->lcd_info);
#else
    TKL_DISP_DEVICE_S lcd;

    lcd.device_info = info;

    tkl_disp_init(&lcd, NULL);
#endif

    if (rt == OPRT_OK)
        lcd_inited = TRUE;
    return rt;
}

STATIC VOID_T tuya_gui_dp_to_widget_evt_send(DP_REPT_CB_DATA* dp_data)
{
    LV_DISP_EVENT_S Sevt = {0};
    LV_TUYAOS_EVENT_S g_dp_evt_s = {.event_typ = LV_TUYAOS_EVENT_DP_REPORT, .param1 = (uint32_t)dp_data };

    Sevt.event = LLV_EVENT_VENDOR_TUYAOS;
    Sevt.param = (UINT_T)&g_dp_evt_s;
    tuya_disp_gui_event_send(&Sevt);
}

//send cp0 dp change to widget
STATIC VOID_T gui_dp_recv_task(VOID_T *args)
{
    OPERATE_RET op_ret = OPRT_OK;
    DP_REPT_CB_DATA* rx_dp_data = NULL;

    for(;;) {
        op_ret = tal_queue_fetch(gui_dp_process_hdl.dp_rx_queue, &rx_dp_data, QUEUE_WAIT_FROEVER);
        if (OPRT_OK != op_ret) {
            TAL_PR_ERR("wait gui rx dp queue error, %d", op_ret);
        }
        else {
            tuya_gui_dp_to_widget_evt_send(rx_dp_data);
            //消息返回,结束释放内存!
            tuya_gui_dp_source_release(rx_dp_data);
            rx_dp_data = NULL;
        }
    }
}

//recv cp1 widget change to dp
STATIC VOID_T gui_dp_send_task(VOID_T *args)
{
    OPERATE_RET rt = OPRT_OK;
    DP_REPT_CB_DATA* tx_dp_data = NULL;

    for(;;) {
        rt = tal_queue_fetch(gui_dp_process_hdl.dp_tx_queue, &tx_dp_data, QUEUE_WAIT_FROEVER);
        if (OPRT_OK != rt) {
            TAL_PR_ERR("wait gui tx dp queue error, %d", rt);
        }
        else {
            //根据实际业务数据区分属于obj还是raw类型数据
            if (tx_dp_data->dp_rept_type == T_OBJ_REPT) {
                TY_OBJ_DP_REPT_S *obj_dp_p = (TY_OBJ_DP_REPT_S *)tx_dp_data->dp_data;
                TUYA_CALL_ERR_LOG(dev_report_dp_json_async(get_gw_cntl()->gw_if.id, obj_dp_p->obj_dp.data, 1));
            }
            else {
                TY_RAW_DP_REPT_S *raw_dp_p = (TY_RAW_DP_REPT_S *)tx_dp_data->dp_data;
                TUYA_CALL_ERR_LOG(dev_report_dp_raw_sync(get_gw_cntl()->gw_if.id, raw_dp_p->dpid, raw_dp_p->data, raw_dp_p->len, 5));
            }
            //release mem
            tuya_gui_dp_source_release(tx_dp_data);
            tx_dp_data = NULL;
        }
    }
}

STATIC OPERATE_RET __gui_dp_report_pre_cb(IN CONST DP_REPT_CB_DATA* dp_data)
{
    OPERATE_RET rt = OPRT_OK;

    return rt;
}

STATIC BOOL_T __gui_dp_is_valid(BYTE_T dpid)
{
    BOOL_T is_valid = FALSE;
    UINT_T i;
    DEV_CNTL_N_S *dev_cntl = get_gw_dev_cntl();
    DP_CNTL_S *dp_cntl = NULL;

    if (dev_cntl != NULL) {
        tal_mutex_lock(dev_cntl->dp_mutex);
        for (i = 0; i < dev_cntl->dp_num; i++) {
            dp_cntl = &dev_cntl->dp[i];
            if(dp_cntl->dp_desc.dp_id == dpid){
                is_valid = TRUE;
                break;
            }
        }
        tal_mutex_unlock(dev_cntl->dp_mutex);
    }

    return is_valid;
}

STATIC OPERATE_RET __gui_dp_report_post_cb(IN CONST OPERATE_RET dp_rslt, IN CONST DP_REPT_CB_DATA* dp_rept_data)
{
    OPERATE_RET rt = OPRT_COM_ERROR;
    UINT_T i;

    if (dp_rslt != OPRT_OK) {
        PR_ERR("******************************* gui dp repot fail ********************************");
        return rt;
    }
    if( dp_rept_data == NULL) {
        PR_ERR("******************************* gui dp repot NULL ********************************");
        return rt;
    }

    DP_REPT_CB_DATA* dp_data = (DP_REPT_CB_DATA*)Malloc(SIZEOF(DP_REPT_CB_DATA));
    if( dp_data == NULL){
        PR_ERR("dp_data Malloc Failed");
        return rt;
    }
    memset(dp_data, 0, SIZEOF(DP_REPT_CB_DATA));

    dp_data->dp_rept_type = dp_rept_data->dp_rept_type;

    TY_OBJ_DP_REPT_S *obj_dp_s = NULL;
    TY_RAW_DP_REPT_S *raw_dp_s = NULL;
    
    if(dp_data->dp_rept_type == T_OBJ_REPT){
        TY_OBJ_DP_REPT_S *rept_obj_dp_p = (TY_OBJ_DP_REPT_S *)dp_rept_data->dp_data;
        obj_dp_s = (TY_OBJ_DP_REPT_S *)Malloc(SIZEOF(TY_OBJ_DP_REPT_S));

        if( obj_dp_s == NULL){
            PR_ERR("dp_data Malloc Failed");
            goto REPT_EXIT;
        }
        dp_data->dp_data = obj_dp_s;
        memcpy(obj_dp_s, rept_obj_dp_p, SIZEOF(TY_OBJ_DP_REPT_S));

        obj_dp_s->obj_dp.dev_id = NULL; //T5不需要
        obj_dp_s->obj_dp.data = (TY_OBJ_DP_S *)Malloc(rept_obj_dp_p->obj_dp.cnt * SIZEOF(TY_OBJ_DP_S));
        if( obj_dp_s->obj_dp.data == NULL){
            PR_ERR("TY_OBJ_DP_S Malloc Failed");
            goto REPT_EXIT;
        }
        memset(obj_dp_s->obj_dp.data, 0, rept_obj_dp_p->obj_dp.cnt * SIZEOF(TY_OBJ_DP_S));
        for(i=0; i<rept_obj_dp_p->obj_dp.cnt; i++)
        {
            if (!__gui_dp_is_valid(rept_obj_dp_p->obj_dp.data[i].dpid)) {
                PR_ERR("TY_OBJ_DP_S invalid obj dp id '%d'", rept_obj_dp_p->obj_dp.data[i].dpid);
                goto REPT_EXIT;
            }
            memcpy(&obj_dp_s->obj_dp.data[i], &rept_obj_dp_p->obj_dp.data[i], SIZEOF(TY_OBJ_DP_S));
            if(obj_dp_s->obj_dp.data[i].type == PROP_STR){
                obj_dp_s->obj_dp.data[i].value.dp_str = (CHAR_T *)Malloc(strlen(rept_obj_dp_p->obj_dp.data[i].value.dp_str) + 1);
                memset(obj_dp_s->obj_dp.data[i].value.dp_str, 0, strlen(rept_obj_dp_p->obj_dp.data[i].value.dp_str) + 1);
                strcpy(obj_dp_s->obj_dp.data[i].value.dp_str, rept_obj_dp_p->obj_dp.data[i].value.dp_str);
            }
        }
        rt = OPRT_OK;
    }
    else if(dp_data->dp_rept_type == T_RAW_REPT){
        TY_RAW_DP_REPT_S *rept_raw_dp_p = (TY_RAW_DP_REPT_S *)dp_rept_data->dp_data;

        if (!__gui_dp_is_valid(rept_raw_dp_p->dpid)) {
            PR_ERR("TY_RAW_DP_S invalid raw dp id '%d'", rept_raw_dp_p->dpid);
            goto REPT_EXIT;
        }

        raw_dp_s = (TY_RAW_DP_REPT_S *)Malloc(SIZEOF(TY_RAW_DP_REPT_S));
        if( raw_dp_s == NULL){
            PR_ERR("raw_dp_data Malloc Failed");
            goto REPT_EXIT;
        }
        dp_data->dp_data = raw_dp_s;
        memcpy(raw_dp_s, rept_raw_dp_p, SIZEOF(TY_RAW_DP_REPT_S));
        raw_dp_s->dev_id = NULL;
        raw_dp_s->data = (BYTE_T*)Malloc(raw_dp_s->len);
        if( raw_dp_s->data == NULL){
            PR_ERR("TY_RAW_DP_S Malloc Failed");
            goto REPT_EXIT;
        }
        memcpy(raw_dp_s->data, rept_raw_dp_p->data, rept_raw_dp_p->len);
        rt = OPRT_OK;
    }
    else{
        PR_ERR("Unvalid data type '%d',did not report data", dp_data->dp_rept_type);
        return OPRT_OK;//此处返回正常，不影响基线的正常逻辑
    }

    rt = tal_queue_post(gui_dp_process_hdl.dp_rx_queue, &dp_data, 0);
    if (OPRT_OK != rt) {
        PR_ERR("rx queue post err");
    }

    REPT_EXIT:
    return rt;
}

STATIC OPERATE_RET tuya_gui_regist_business_process(VOID)
{
    OPERATE_RET rt =  OPRT_OK;
    THREAD_CFG_T thrd_cfg;

    memset(&gui_dp_process_hdl, 0, SIZEOF(TUYA_GUI_DP_CORE_T));
    gui_dp_process_hdl.dp_rept_hd_s.dp_recv_filter_cb = NULL;
    gui_dp_process_hdl.dp_rept_hd_s.dp_rept_pre_cb = __gui_dp_report_pre_cb;
    gui_dp_process_hdl.dp_rept_hd_s.dp_rept_post_cb = __gui_dp_report_post_cb;
    gui_dp_process_hdl.dp_rept_hd_s.need_dp_force = FALSE;

    TUYA_CALL_ERR_RETURN(tuya_iot_regist_dp_rept_cb(&(gui_dp_process_hdl.dp_rept_hd_s)));
    TUYA_CALL_ERR_RETURN(tal_queue_create_init(&gui_dp_process_hdl.dp_rx_queue, SIZEOF(VOID *), GUI_DP_QUEUE_NUM));
    TUYA_CALL_ERR_RETURN(tal_queue_create_init(&gui_dp_process_hdl.dp_tx_queue, SIZEOF(VOID *), GUI_DP_QUEUE_NUM));

    thrd_cfg.priority = THREAD_PRIO_1;
    thrd_cfg.stackDepth = 3*1024;
    thrd_cfg.thrdname = "gui_dprx_task";
    TUYA_CALL_ERR_RETURN(tal_thread_create_and_start(&gui_dp_process_hdl.dp_recv_thread, NULL, NULL, gui_dp_recv_task, NULL, &thrd_cfg));
    thrd_cfg.thrdname = "gui_dptx_task";
    TUYA_CALL_ERR_RETURN(tal_thread_create_and_start(&gui_dp_process_hdl.dp_send_thread, NULL, NULL, gui_dp_send_task, NULL, &thrd_cfg));
    PR_INFO("----------%s successful!", __func__);
    return rt;
}

STATIC VOID tuya_gui_gw_dev_cntl_notify(VOID)
{
    LV_DISP_EVENT_S Sevt = {0};
    LV_TUYAOS_EVENT_S g_dp_evt_s = {0};
    DEV_CNTL_N_S *dev_cntl = get_gw_dev_cntl();

    if (dev_cntl == NULL) {
        PR_WARN("----------%s DEV_CNTL_N_S un-initialized !", __func__);
        return;
    }
    PR_INFO("----------%s DEV_CNTL_N_S initialized !", __func__);
    g_dp_evt_s.event_typ = LV_TUYAOS_EVENT_DP_INFO_CREATE;
    g_dp_evt_s.param1 = (UINT32_T)dev_cntl;

    Sevt.param = (UINT32_T) &g_dp_evt_s;
    Sevt.event = LLV_EVENT_VENDOR_TUYAOS;
    tuya_disp_gui_event_send(&Sevt);
    PR_INFO("----------%s create dp info %s !", __func__, (g_dp_evt_s.param2 == OPRT_OK)?"succesful":"fail");
}

STATIC OPERATE_RET  __app_gui_wifi_connect_event(VOID *data)
{
    PR_INFO("%s:app_gui recv wifi connect event !", __func__);
    gui_wifi_completed = true;
    return OPRT_OK;
}

STATIC OPERATE_RET  __app_gui_activated_event(VOID *data)
{
    activate_info_t *info = (activate_info_t *)data;

    PR_INFO("app_gui recv active event, type:%d, stage:%d", info->type, info->stage);

    if (ACTIVATE_STAGE_START == info->stage) {
        //配网开始
    } 
    else {
        if (ACTIVATE_STAGE_STOP == info->stage) {
            //配网停止，释放资源
        } else if (ACTIVATE_STAGE_SUCCESS == info->stage) {
            //配网成功，释放资源
            PR_INFO("device active is finished, start gui DEV_CNTL_N_S init !!!");
            tuya_gui_gw_dev_cntl_notify();
        } else if (ACTIVATE_STAGE_FAIL_URL == info->stage) {
            //配网超时失败，释放资源
        }
    }
    return OPRT_OK;
}

STATIC OPERATE_RET  __app_gui_dev_initialized_event(VOID *data)
{
    //设备初始化成功
    PR_INFO("device is initialized, start gui DEV_CNTL_N_S init !!!");
    tuya_gui_gw_dev_cntl_notify();
    return OPRT_OK;
}

STATIC OPERATE_RET tuya_gui_fs_init(VOID)
{
    UINT_T len = 0;
    CHAR_T* str = NULL;
    TUYA_GUI_FS_PINPONG_E fs_path_type = TUYA_GUI_FS_PP_UNKNOWN;
    OPERATE_RET op_ret = wd_common_read(GUI_FS_PATH_INFO, (BYTE_T **)&str, &len);

    //1.获取并设置当前有效文件系统路径
    if (OPRT_OK != op_ret) {
        op_ret = ty_gfw_gui_file_system_path_type_restore(TUYA_GUI_FS_PP_MAIN);      //默认主路径
        if (OPRT_OK != op_ret) {
            PR_ERR("re-write fs path type err:%d", op_ret);
            return op_ret;
        }
        op_ret = wd_common_read(GUI_FS_PATH_INFO, (BYTE_T **)&str, &len);
        if (OPRT_OK != op_ret) {
            PR_ERR("re-read fs path type err:%d", op_ret);
            return op_ret;
        }
        PR_INFO("----------%s re-write fs path type sucessful!", __func__);
    }
    fs_path_type = atoi(str);
    //PR_INFO("----------%s get real fs path type '%d' !", __func__, fs_path_type);
    //fs_path_type = TUYA_GUI_FS_PP_MAIN;       //固定主文系统
    Free(str);
    PR_INFO("----------%s get fs path type '%d' !", __func__, fs_path_type);
    tuya_app_gui_set_path_type(fs_path_type);
    PR_INFO("%s:%d----test top path '%s'\r\n", __func__, __LINE__, tuya_app_gui_get_top_path());

    //2.开始挂载文件系统
    TUYA_GUI_LFS_PARTITION_TYPE_E lfs_p_type = tuya_app_gui_get_lfs_partiton_type();
    GUI_FS_DEV_TYPE_T p_type = GUI_DEV_INNER_FLASH;
    if (lfs_p_type == TUYA_GUI_LFS_SPI_FLASH)
        p_type = GUI_DEV_INNER_FLASH;
    else if (lfs_p_type == TUYA_GUI_LFS_QSPI_FLASH)
        p_type = GUI_DEV_EXT_FLASH;
    else if (lfs_p_type == TUYA_GUI_LFS_SD)
        p_type = GUI_DEV_SDCARD;
    else {
        op_ret = OPRT_INVALID_PARM;
        return op_ret;
    }
    PR_INFO("%s:%d----mount path '%s', dev type '%s'\r\n", __func__, __LINE__, 
        tuya_app_gui_get_disk_mount_path(), (p_type == GUI_DEV_INNER_FLASH)?"internal-flash":(p_type == GUI_DEV_EXT_FLASH)?"ext-qspi-flash":"sd-card");
    op_ret = ty_gui_fs_mount(tuya_app_gui_get_disk_mount_path(), p_type);

    return op_ret;
}

OPERATE_RET tuya_gui_system_event_hdl_register(GUI_SYS_EVENT_CB cb)
{
    gui_system_event_hdl = cb;
    return OPRT_OK;
}

OPERATE_RET tuya_gui_obj_event_hdl_register(GUI_OBJ_EVENT_CB cb)
{
    gui_obj_event_hdl = cb;
    return OPRT_OK;
}

OPERATE_RET tuya_gui_dp2obj_pre_init_hdl_register(GUI_DP2OBJ_PRE_INIT_EVENT_CB cb)
{
    gui_dp2obj_pre_init_hdl = cb;
    return OPRT_OK;
}

OPERATE_RET tuya_gui_obj2dp_convert_hdl_register(GUI_OBJ2DP_CONVERT_CB cb)
{
    gui_obj2dp_convert_hdl = cb;
    return OPRT_OK;
}

VOID tuya_gui_dp_source_release(DP_REPT_CB_DATA* dp_data)
{
    UINT_T i = 0;

    if (dp_data == NULL)
        return;

    if (dp_data->dp_rept_type == T_OBJ_REPT) {
        TY_OBJ_DP_REPT_S *obj_dp_s = (TY_OBJ_DP_REPT_S *)dp_data->dp_data;
        for(i = 0; i < obj_dp_s->obj_dp.cnt; i++){
            if((obj_dp_s->obj_dp.data[i].type == PROP_STR) && (obj_dp_s->obj_dp.data[i].value.dp_str != NULL)) {
                Free(obj_dp_s->obj_dp.data[i].value.dp_str);
            }
        }
        Free(obj_dp_s->obj_dp.data);
        Free(obj_dp_s);
    }
    else if (dp_data->dp_rept_type == T_RAW_REPT) {
        TY_RAW_DP_REPT_S *raw_dp_s = (TY_RAW_DP_REPT_S *)dp_data->dp_data;
        if (raw_dp_s->data != NULL && raw_dp_s->len > 0)
            Free(raw_dp_s->data);
        Free(raw_dp_s);
    }
    Free(dp_data);
}

//执行此函数前务必初始化所有的dp数据!!!
VOID tuya_gui_init(BOOL_T is_mf_test, VOID_T *lcd_info, GUI_AUDIO_INFO_S *audio_info, CHAR_T *weather_code)
{
#if 0
    LIST_HEAD gui_list_head;
    GUI_DISP_EVENT_LIST_S *gui_evt = NULL;
#endif
    ty_cJSON_Hooks hooks;
    UINT_T cnt = 0;

#ifdef TUYA_APP_MULTI_CORE_IPC
    TKL_LVGL_CFG_T lv_cfg = {
        .recv_cb = tuya_app_recv_lv_event,
        .recv_arg = NULL,
    };
#endif

    if (gui_initialized)
        return;

    hooks.malloc_fn = tkl_system_psram_malloc;
    hooks.free_fn = tkl_system_psram_free;
    ty_cJSON_InitHooks(&hooks);                 //重定向系统cjson的内存接口至psram.

    if (tal_mutex_create_init(&fs_kvs_mutex) != OPRT_OK) {
        PR_ERR("[%s:%d]:------fs kv mutex init fail ???\r\n", __func__, __LINE__);
        return;
    }

#ifdef USER_CONFIG_KV_FILE_ASYNC_WRITE
    kv_file_async_list = Malloc(SIZEOF(KV_FILE_ASYNC_LIST_S));
    if (kv_file_async_list == NULL) {
        PR_ERR("[%s:%d]:------fkv_file_async list malloc fail ???\r\n", __func__, __LINE__);
        return;
    }
    memset(kv_file_async_list, 0, SIZEOF(KV_FILE_ASYNC_LIST_S));
    if (OPRT_OK != tal_mutex_create_init(&kv_file_async_list->mutex)) {
        PR_ERR("[%s:%d]:------fkv_file_async list mutex init fail ???\r\n", __func__, __LINE__);
        return;
    }
    INIT_LIST_HEAD(&kv_file_async_list->list_hd);

    if (kv_common_write_async_task_create() != OPRT_OK) {
        PR_ERR("[%s:%d]:------fkv_file_async task create fail ???\r\n", __func__, __LINE__);
        return;
    }
#endif

#ifdef LVGL_ENABLE_CLOUD_RECIPE
    if (ty_gfw_gui_cloud_recipe_init() != OPRT_OK) {
        PR_ERR("[%s:%d]:------cloud recipe init fail ???\r\n", __func__, __LINE__);
        return;
    }
#endif

#ifdef LVGL_ENABLE_AUDIO_FILE_PLAY
    if (gui_audio_play_async_task_create(audio_info) != OPRT_OK) {
        PR_ERR("[%s:%d]:------gui audio play init fail ???\r\n", __func__, __LINE__);
        return;
    }
#endif

    while(!(get_gw_cntl()->is_init) && (cnt++ < 15)) {      //等待初始化完成!
        tal_system_sleep(300);
    }
    if (cnt >= 15) {
        PR_WARN("%s-------wait dev inited timeout !\r\n", __func__);
    }

#if defined(TUYA_FILE_SYSTEM) && (TUYA_FILE_SYSTEM == 1)
    //接下来,设备已激活配网和flash的mount同时操作会导致系统卡死,以下流程避开这种情况的同时发生:
    if (get_gw_active() == ACTIVATED) {
        cnt = 0;
        gui_wifi_completed = false;
        ty_subscribe_event(EVENT_WIFI_CONNECT_FAILED, "app_gui", __app_gui_wifi_connect_event, SUBSCRIBE_TYPE_ONETIME);
        ty_subscribe_event(EVENT_WIFI_CONNECT_SUCCESS, "app_gui", __app_gui_wifi_connect_event, SUBSCRIBE_TYPE_ONETIME);
        while ((gui_wifi_completed == false) && (cnt++ < 30)) {
            tal_system_sleep(100);
        }
        PR_WARN("%s-------wait wifi completed stat as '%s'\r\n", __func__, (gui_wifi_completed == false)?"time-out":"ok");
    }
    else {
         PR_WARN("%s-------dev not activated without waitting !\r\n", __func__);
    }
#endif
    gui_mf_test = is_mf_test;

#if defined(TUYA_FILE_SYSTEM) && (TUYA_FILE_SYSTEM == 1)
#ifdef TUYA_APP_USE_TAL_IPC
    if (uf_db_init() != OPRT_OK) {          //创建uf文件系统客户端
        PR_ERR("[%s:%d]:------core0 uf init fail ???\r\n", __func__, __LINE__);
        return;
    }
#endif

    if (tuya_gui_fs_init() != OPRT_OK) {             //文件系统初始化
        PR_ERR("[%s:%d]:------fs init fail ???\r\n", __func__, __LINE__);
        //return;
    }
    else {
#ifdef USER_CONFIG_FILE_BACKUP_ENABLE
        if (fs_kv_common_config_backup_start() != OPRT_OK) {
            PR_ERR("[%s:%d]:------fs kv backup start fail ???\r\n", __func__, __LINE__);
            return;
        }
#endif
    }
#endif

    /*
    天气服务:
    \"w.temp\",                     大气温度
    \"w.thigh\",                    最高温度
    \"w.tlow\",                     最低温度
    \"w.humidity\",                 空气湿度
    \"w.condition\",                天气状况，晴、雨、雪等
    \"w.conditionNum\",             天气状况，晴、雨、雪等(数字编码)
    \"w.sunRise\",                  日出
    \"w.sunSet\",                   日落
    \"w.unix\",                     格林时间
    \"w.local\",                    本地时间
    \"w.windSpeed\",                风速
    \"w.windDir\",                  风向
    \"w.windLevel\",                风级
    \"w.pm25\",                     PM2.5（细颗粒物）
    \"w.date.n\",                   需要预报天数
    城市服务:
    \"c.area\",                  区县或城市名称
    \"c.city\",                  城市名称
    \"c.province\",              省
    */
    if (weather_code == NULL)
        ty_gfw_gui_weather_init("[\"w.temp\",\"w.thigh\",\"w.tlow\",\"w.humidity\",\"w.conditionNum\",\"w.date.1\"]");           //天气预报组件初始化
    else
        ty_gfw_gui_weather_init(weather_code);           //天气预报组件初始化

    //1.初始化ipc事件通道
#ifdef TUYA_APP_MULTI_CORE_IPC
    #ifdef TUYA_APP_USE_TAL_IPC
        if (tuya_app_init_ipc(tuya_app_recv_lv_event) != OPRT_OK) {
            PR_ERR("[%s:%d]:------core0 ipc init fail ???\r\n", __func__, __LINE__);
            return;
        }
    #else
        tkl_lvgl_init((TKL_DISP_INFO_S *)lcd_info, &lv_cfg);
    #endif
#else
    app_no_ipc_cp0_recv_callback_register(tuya_app_recv_lv_event);
#endif

    //2.初始化屏幕
    if (tuya_gui_lcd_open(lcd_info) != OPRT_OK)
        return;

    //3.启动lvgl
#ifdef TUYA_APP_MULTI_CORE_IPC
    tkl_lvgl_start();
#else
    #ifdef LVGL_AS_APP_COMPONENT
        extern void tuya_gui_lvgl_start(void);
        tuya_gui_lvgl_start();
    #else
        #error "No gui app entry defined!"
    #endif
#endif

    gui_initialized = true;

    //4.初始化用户指定的GUI对象:
    if (gui_screen_loaded == GUI_BOOTON_WITHOUT_PAGE) {     //没有启动页面时,执行用户预初始化事件!
        PR_INFO("%s-%d: pre init user hdl without bootonpage !\r\n", __func__, __LINE__);
        if (gui_dp2obj_pre_init_hdl) {
            if (OPRT_OK == gui_dp2obj_pre_init_hdl())   //执行成功,表示用户有控件事件发送
                tal_system_sleep(200);                  //延时避免更新控件时屏幕闪烁!
        }
    }

    //6.点亮屏幕
    if (!tuya_user_app_PwrOnPage_backlight_auto()) {      //启动页面自动开关背光时,此处无需操作背光!
        PR_INFO("%s-%d: start screen backlight on !\r\n", __func__, __LINE__);
        LV_DISP_EVENT_S lv_msg = {.tag = NULL, .event = LLV_EVENT_VENDOR_LCD_WAKE, .param = LV_LCD_LIGHT_FROM_BOOT};
        aic_lvgl_msg_event_change((void *)&lv_msg);
    }

    //注册dp数据及系统业务处理
    if (!gui_mf_test) {
    #if defined(TUYA_UI2DP_CONVERT) && (TUYA_UI2DP_CONVERT == 1)
        tuya_gui_regist_business_process();
    #endif
    }
}

BOOL_T tuya_gui_screen_is_loaded(VOID)
{
    if (gui_screen_loaded > GUI_BOOTON_NONE)
        return TRUE;
    else
        return FALSE;
}
