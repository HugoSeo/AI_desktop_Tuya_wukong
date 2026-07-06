/**
 * @file tuya_app_gui_tftp.c
 * @author www.tuya.com
 * @brief tuya_app_gui_tftp module is used to 
 * @version 0.1
 * @date 2022-10-28
 *
 * @copyright Copyright (c) tuya.inc 2022
 *
 */

#include "tuya_app_gui_tftp.h"
#include "tuya_app_gui_config.h"

#ifdef TUYA_TFTP_SERVER
#include <stdlib.h>
#include "uni_log.h"
#include "tal_memory.h"
#include "tal_wifi.h"
#include "lwip/apps/tftp_server.h"
#include "tuya_wifi_status.h"
#include "ty_gui_fs.h"
#include "tkl_wifi.h"
#include "tal_sw_timer.h"
#include "tuya_app_gui_fs_path.h"
#include "tuya_app_gui_display_text.h"

STATIC TIMER_ID tftp_srv_timer = NULL;

STATIC OPERATE_RET tuya_app_gui_tftp_mkdir_p(CONST CHAR_T *file_path)
{
    OPERATE_RET rt = OPRT_COM_ERROR;
    CHAR_T tmp[UI_FILE_PATH_MAX_LEN];
    CHAR_T *p = NULL;
    size_t len;

    memset(tmp, 0, sizeof(tmp));
    strncpy(tmp, file_path, sizeof(tmp));

    len = strlen(tmp);
    if(len == 0) {
        PR_ERR("Invalid path: empty string\n");
        return rt;
    }

    // 移除末尾的斜杠（如果有）
    if(tmp[len - 1] == '/')
        tmp[len - 1] = '\0';

    // 从路径的第二个字符开始，避免处理根目录 '/'
    for(p = tmp + 1; *p; p++) {
        if(*p == '/') {
            *p = '\0';
            if(ty_gui_fs_mkdir(tmp) != 0) {
                PR_ERR("mkdir '%s' error\n", tmp);
                return rt;
            }
            PR_NOTICE("%s-successful create folder '%s'\r\n", __func__, tmp);
            *p = '/';
        }
    }

    // 创建最终的目录
    if(ty_gui_fs_mkdir(tmp) != 0) {
        PR_ERR("mkdir '%s' error\n", tmp);
    }
    else {
        rt = OPRT_OK;
        PR_NOTICE("%s-successful create final folder '%s'\r\n", __func__, tmp);
    }

    return rt;
}

STATIC VOID* tuya_tftp_srv_open(CONST CHAR_T* fname, CONST CHAR_T* mode, UCHAR_T write)
{
    TUYA_FILE file_hdl = NULL;
    CHAR_T *ptr = NULL;
    CONST CHAR_T *file_path = NULL;
    CHAR_T *file = NULL;
    BOOL_T valid_file = TRUE;
    

    PR_NOTICE("%s-file name'%s', mode '%s' to '%s'\r\n", __func__, fname, mode, (write!=0)?"write":"read");
    ptr = strrchr(fname, '.');
    if (ptr != NULL) {
        if (strcmp(ptr, ".ttf") == 0 || strcmp(ptr, ".TTF") == 0) {
            file_path = tuya_app_gui_get_font_path();
        }
        else if(strcmp(ptr, ".mp3") == 0 || strcmp(ptr, ".MP3") == 0) {
            file_path = tuya_app_gui_get_music_path();
        }
        else if (strcmp(ptr, ".jpg") == 0 || strcmp(ptr, ".JPG") == 0 ||    \
            strcmp(ptr, ".jpeg") == 0 || strcmp(ptr, ".JPEG") == 0 ||    \
            strcmp(ptr, ".png") == 0 || strcmp(ptr, ".PNG") == 0 ||     \
            strcmp(ptr, ".webp") == 0 || strcmp(ptr, ".WEBP") == 0 ||     \
            strcmp(ptr, ".gif") == 0 || strcmp(ptr, ".GIF") == 0) {
            file_path = tuya_app_gui_get_picture_path();
        }
        else if (strcmp(ptr, ".json") == 0 || strcmp(ptr, ".JSON") == 0) {      //json命名规则:中文如ch_xxx.json,英文如en_xxx.json,etc
            if (strncmp(fname, UI_DISPLAY_NAME_CH, strlen(UI_DISPLAY_NAME_CH)) == 0)
                file_path = tuya_app_gui_get_lang_ch_path();
            else if (strncmp(fname, UI_DISPLAY_NAME_EN, strlen(UI_DISPLAY_NAME_EN)) == 0)
                file_path = tuya_app_gui_get_lang_en_path();
            else if (strncmp(fname, UI_DISPLAY_NAME_KR, strlen(UI_DISPLAY_NAME_KR)) == 0)
                file_path = tuya_app_gui_get_lang_kr_path();
            else if (strncmp(fname, UI_DISPLAY_NAME_JP, strlen(UI_DISPLAY_NAME_JP)) == 0)
                file_path = tuya_app_gui_get_lang_jp_path();
            else if (strncmp(fname, UI_DISPLAY_NAME_FR, strlen(UI_DISPLAY_NAME_FR)) == 0)
                file_path = tuya_app_gui_get_lang_fr_path();
            else if (strncmp(fname, UI_DISPLAY_NAME_DE, strlen(UI_DISPLAY_NAME_DE)) == 0)
                file_path = tuya_app_gui_get_lang_de_path();
            else if (strncmp(fname, UI_DISPLAY_NAME_RU, strlen(UI_DISPLAY_NAME_RU)) == 0)
                file_path = tuya_app_gui_get_lang_ru_path();
            else if (strncmp(fname, UI_DISPLAY_NAME_IN, strlen(UI_DISPLAY_NAME_IN)) == 0)
                file_path = tuya_app_gui_get_lang_in_path();
            else if (strncmp(fname, UI_DISPLAY_NAME_ALL, strlen(UI_DISPLAY_NAME_ALL)) == 0)
                file_path = tuya_app_gui_get_lang_all_path();
            else {
                //valid_file = FALSE;
                //PR_ERR("%s-unknown language file name '%s' !", __func__, fname);
                file_path = tuya_app_gui_get_lang_all_path();
            }
        }
        else if (strcmp(ptr, ".inf") == 0) {
            if (strcmp(fname, "version.inf") == 0)
                file_path = tuya_app_gui_get_top_path();
            else {
                valid_file = FALSE;
                PR_ERR("%s-unknown version file name '%s' !", __func__, fname);
            }
        }
        else {
            valid_file = FALSE;
            PR_ERR("%s-unknown file name '%s' !", __func__, fname);
        }
        if (valid_file) {
            file = (CHAR_T *)tal_malloc(UI_FILE_PATH_MAX_LEN);
            memset(file, 0, UI_FILE_PATH_MAX_LEN);
            snprintf(file, UI_FILE_PATH_MAX_LEN, "%s%s", file_path, fname);
            if (write) {
                //create folder
                if (tuya_app_gui_tftp_mkdir_p(file_path) == OPRT_OK) {
                    PR_NOTICE("%s-------------------write file '%s' to '%s'\r\n", __func__, fname, file);
                    file_hdl = ty_gui_fopen(file, "wb");
                }
                else {
                    PR_NOTICE("%s-------------------create folder '%s' fail ???\r\n", __func__, file_path);
                }
            }
            else {
                PR_NOTICE("%s-------------------read file '%s' from '%s'\r\n", __func__, fname, file);
                file_hdl = ty_gui_fopen(file, "rb");
            }
            if (file_hdl == NULL) {
                PR_ERR("%s-file open fail !", __func__);
            }
            if (file != NULL)
                tal_free(file);
        }
    }
    else {
        PR_ERR("%s-unknown extension file name '%s' !", __func__, fname);
    }

   return file_hdl;
}

STATIC VOID tuya_tftp_srv_close(VOID* handle)
{
    ty_gui_fclose(handle);
}

STATIC INT32_T tuya_tftp_srv_read(VOID* handle, VOID* buf, INT32_T bytes)
{
    INT32_T len = ty_gui_fread(buf, bytes, handle);
    PR_NOTICE("%s =======len: '%d'\r\n",__func__, len);
    return len;
}

STATIC INT32_T tuya_tftp_srv_write(VOID* handle, struct pbuf* p)
{
    INT32_T len = ty_gui_fwrite(p->payload, p->len, handle);
    PR_NOTICE("%s =======len: '%d'\r\n",__func__, len);
    return len;
}

STATIC VOID tuya_tftp_srv_start_timeout(UINT_T timerID, PVOID_T pTimerArg)
{
    NW_IP_S ip;
    GW_WIFI_NW_STAT_E cur_nw_stat;
    BOOL_T tftp_srv_start = FALSE;
    struct tftp_context *tftp_hdl = NULL;

    if (OPRT_OK == get_wf_gw_nw_status(&cur_nw_stat)) {
        if (cur_nw_stat > STAT_STA_DISC && cur_nw_stat != STAT_PROXY_ACTIVED) {
            memset(&ip, 0, SIZEOF(ip));
            if (OPRT_OK != tal_wifi_get_ip(WF_STATION, &ip)) {
                PR_ERR("get ip fail:%d");
            }
            else {
                PR_NOTICE("get ip successful ip:'%s'[tftp use port:69], gw:'%s'\r\n", ip.ip, ip.gw);
                tftp_hdl = (struct tftp_context *)tal_malloc(sizeof(struct tftp_context));
                if (tftp_hdl != NULL) {
                    tftp_hdl->open = tuya_tftp_srv_open;
                    tftp_hdl->close = tuya_tftp_srv_close;
                    tftp_hdl->read = tuya_tftp_srv_read;
                    tftp_hdl->write = tuya_tftp_srv_write;
                    if (tftp_init(tftp_hdl) == ERR_OK) {
                        PR_NOTICE("tftp start successful !\r\n");
                        tal_sw_timer_stop(tftp_srv_timer);
                        tal_sw_timer_delete(tftp_srv_timer);
                        tftp_srv_start = TRUE;
                    }
                    else
                        PR_ERR("tftp start fail !\r\n");
                }
                else {
                    PR_ERR("tftp malloc fail !\r\n");
                }
            }
        }
    }
    if (!tftp_srv_start)
        tal_sw_timer_start(tftp_srv_timer, 3 * 1000, TAL_TIMER_ONCE);
}

OPERATE_RET tuya_app_gui_tftp_server_init(VOID)
{
    OPERATE_RET rt = OPRT_COM_ERROR;

    rt = tal_sw_timer_create((TAL_TIMER_CB)tuya_tftp_srv_start_timeout, NULL, &tftp_srv_timer);
    if (OPRT_OK != rt) {
        PR_ERR("create tftp srv timer fail !");
    }
    else {
        PR_NOTICE("create tftp srv timer ok !");
        rt = tal_sw_timer_start(tftp_srv_timer, 3 * 1000, TAL_TIMER_ONCE);  //3s
        if(OPRT_OK != rt) {
            PR_NOTICE("tftp srv timer start fail !");
        }
        else
            PR_NOTICE("tftp srv timer start ok !");
    }
    return rt;
}
#endif
