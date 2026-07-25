#include <string.h>
#include <stdio.h>
#include "crc32i.h"
#include "tuya_cloud_types.h"
#include "tal_memory.h"
#include "uni_log.h"
#include "ty_gui_fs.h"
#include "tuya_app_gui_fs_path.h"
#include "gui_resource_update.h"

extern VOID_T tkl_system_psram_free(VOID_T* ptr);
extern VOID_T* tkl_system_psram_malloc(CONST SIZE_T size);

static CHAR_T *gui_resource_strdup(CONST CHAR_T *s)
{
    CHAR_T *ptr = (char *)tal_malloc(strlen(s)+1);
    if (ptr != NULL) {
        memset(ptr, 0, strlen(s)+1);
        strcpy(ptr, s);
    }
    return ptr;
}
 
static OPERATE_RET gui_resource_read_ver(const CHAR_T *ver_name, CHAR_T **out_ver_id)
{
    OPERATE_RET ret = OPRT_COM_ERROR;
    size_t file_len = 0;
    UCHAR_T *file_content = NULL;
    TUYA_FILE fd = NULL;

    do{
        if(!ver_name)
        {
            PR_ERR("[%s][%d]ver_name is null.\r\n", __FUNCTION__, __LINE__);
            break;
        }
        file_len = ty_gui_fgetsize(ver_name);
        if(file_len <= 0)
        {
            PR_ERR("[%s][%d] sta fail:%s\r\n", __FUNCTION__, __LINE__, ver_name);
            break;
        }
        file_content = tal_malloc(file_len+1);
        if (file_content == NULL) {
            PR_ERR("[%s][%d] malloc sizeof of '%u' fail ?\r\n", __FUNCTION__, __LINE__, file_len);
            break;
        }
        memset(file_content, 0, file_len+1);
        fd = ty_gui_fopen(ver_name, "r");
        if(fd == NULL)
        {
            PR_ERR("[%s][%d]%s fail\r\n", __func__, __LINE__, ver_name);
            tal_free(file_content);
            break;
        }
        ty_gui_fread(file_content, file_len, fd);
        ty_gui_fclose(fd);

        *out_ver_id = (CHAR_T *)file_content;
        ret = OPRT_OK;
    }while(0);

    return ret;
}

static UINT32_T gui_resource_get_file_crc32(const CHAR_T *file_folder, const CHAR_T *file_name)
{
    UINT32_T crc32 = 0;
    CHAR_T *file = NULL;
    size_t file_len = 0;
    UCHAR_T *file_content = NULL;
    TUYA_FILE fd = NULL;
    OPERATE_RET ret = OPRT_COM_ERROR;

    file = (char *)tal_malloc(strlen(file_folder)+strlen(file_name)+1);
    if (file != NULL) {
        memset(file, 0, strlen(file_folder)+strlen(file_name)+1);
        sprintf(file, "%s%s", file_folder, file_name);
        do {
            file_len = ty_gui_fgetsize(file);
            if(file_len <= 0)
            {
                PR_ERR("[%s][%d] sta fail:%s\r\n", __FUNCTION__, __LINE__, file);
                break;
            }

            file_content = tkl_system_psram_malloc(file_len);
            if (file_content == NULL) {
                ret = OPRT_COM_ERROR;
                PR_ERR("[%s][%d] malloc sizeof of '%u' fail ?\r\n", __FUNCTION__, __LINE__, file_len);
                break;
            }
            memset(file_content, 0, file_len);
            fd = ty_gui_fopen(file, "r");
            if(fd == NULL)
            {
                PR_ERR("[%s][%d]%s fail\r\n", __func__, __LINE__, file);
                tkl_system_psram_free(file_content);
                ret = OPRT_COM_ERROR;
                break;
            }

            ty_gui_fread(file_content, file_len, fd);
            ty_gui_fclose(fd);
            crc32 = hash_crc32i_total(file_content, file_len);
            tkl_system_psram_free(file_content);
            ret = OPRT_OK;
        }while(0);
        tal_free(file);
    }
    else {
        PR_ERR("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
    }

    if (OPRT_OK != ret) {
        crc32 = 0;
        PR_ERR("[%s][%d] cal file '%s%s' crc32 fail ???\r\n", __FUNCTION__, __LINE__, file_folder, file_name);
    }
    return crc32;
}

static OPERATE_RET gui_resoucre_folder_statistics(const CHAR_T *file_folder, gui_resource_info_s *gui_res)
{
    OPERATE_RET ret = OPRT_COM_ERROR;
    TUYA_FILEINFO entry = NULL;
    TUYA_DIR dp = NULL;
    const CHAR_T *dir_name = NULL;
    UCHAR_T i = 0;
    UINT32_T j = 0;
    UINT32_T file_cnt = 0;
    INT_T dir_read_ret = 0;

    for (i=0; i<2; i++) {
        ret = ty_gui_dir_open(file_folder, &dp);
        if (ret != OPRT_OK) {
            PR_ERR("[%s][%d] open path '%s' fail\r\n", __FUNCTION__, __LINE__, file_folder);
            ret = OPRT_NOT_FOUND;
            goto err_out;
        }

        for (;;) {
            entry = NULL;
            dir_read_ret = ty_gui_dir_read(dp, &entry);
            if (entry == NULL || dir_read_ret != 0)
                break;
            ty_gui_dir_name(entry, &dir_name);
            if (strcmp(dir_name, ".") == 0 || strcmp(dir_name, "..") == 0) {
                continue;
            }
            if (i==0)
                file_cnt++;
            else {
                //TY_GUI_LOG_PRINT("[%s][%d] find path '%s', has file '%s'\n", __FUNCTION__, __LINE__, file_folder, entry->d_name);
                if (strcmp(file_folder, tuya_app_gui_get_font_path()) == 0) {
                    gui_res->font.node[j].file_name = gui_resource_strdup(dir_name);
                    gui_res->font.node[j].crc32 = gui_resource_get_file_crc32(file_folder, dir_name);
                    if (gui_res->font.node[j].file_name == NULL){
                        PR_ERR("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
                        goto err_out;
                    }
                    gui_res->font.node[j].type = T_GUI_RES_FONT;
                }
                else if (strcmp(file_folder, tuya_app_gui_get_music_path()) == 0) {
                    gui_res->music.node[j].file_name = gui_resource_strdup(dir_name);
                    gui_res->music.node[j].crc32 = gui_resource_get_file_crc32(file_folder, dir_name);
                    if (gui_res->music.node[j].file_name == NULL){
                        PR_ERR("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
                        goto err_out;
                    }
                    gui_res->music.node[j].type = T_GUI_RES_MUSIC;
                }
                else if (strcmp(file_folder, tuya_app_gui_get_picture_path()) == 0) {
                    gui_res->picture.node[j].file_name = gui_resource_strdup(dir_name);
                    gui_res->picture.node[j].crc32 = gui_resource_get_file_crc32(file_folder, dir_name);
                    if (gui_res->picture.node[j].file_name == NULL){
                        PR_ERR("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
                        goto err_out;
                    }
                    gui_res->picture.node[j].type = T_GUI_RES_PICTURE;
                }
                j++;
            }
        }
        ty_gui_dir_close(dp);
        //第一次遍历用于申请空间
        if (i==0 && file_cnt>0) {
            if (strcmp(file_folder, tuya_app_gui_get_font_path()) == 0) {
                gui_res->font.node_num = file_cnt;
                gui_res->font.node = (gui_res_file_s *)tal_malloc(sizeof(gui_res_file_s)*file_cnt);
                if (gui_res->font.node == NULL) {
                    PR_ERR("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
                    goto err_out;
                }
                memset(gui_res->font.node, 0, sizeof(gui_res_file_s)*file_cnt);
            }
            else if (strcmp(file_folder, tuya_app_gui_get_music_path()) == 0) {
                gui_res->music.node_num = file_cnt;
                gui_res->music.node = (gui_res_file_s *)tal_malloc(sizeof(gui_res_file_s)*file_cnt);
                if (gui_res->music.node == NULL) {
                    PR_ERR("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
                    goto err_out;
                }
                memset(gui_res->music.node, 0, sizeof(gui_res_file_s)*file_cnt);
            }
            else if (strcmp(file_folder, tuya_app_gui_get_picture_path()) == 0) {
                gui_res->picture.node_num = file_cnt;
                gui_res->picture.node = (gui_res_file_s *)tal_malloc(sizeof(gui_res_file_s)*file_cnt);
                if (gui_res->picture.node == NULL) {
                    PR_ERR("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
                    goto err_out;
                }
                memset(gui_res->picture.node, 0, sizeof(gui_res_file_s)*file_cnt);
            }
            else {
                PR_ERR("[%s][%d] unknown file folder '%s' ???\r\n", __FUNCTION__, __LINE__, file_folder);
                goto err_out;
            }
        }
    }
    ret = OPRT_OK;

    err_out:
    return ret;
}

static void gui_resource_release(gui_resource_info_s *gui_res)
{
    int i = 0;

    if (gui_res->data_file.node_num > 0) {
        for(i=0; i<gui_res->data_file.node_num; i++){
            if (gui_res->data_file.node[i].file_name != NULL)
                tal_free(gui_res->data_file.node[i].file_name);
            if (gui_res->data_file.node[i].url != NULL)
                tal_free(gui_res->data_file.node[i].url);
            if (gui_res->data_file.node[i].md5 != NULL)
                tal_free(gui_res->data_file.node[i].md5);
        }
        tal_free(gui_res->data_file.node);
    }
    if (gui_res->font.node_num > 0) {
        for(i=0; i<gui_res->font.node_num; i++){
            if (gui_res->font.node[i].file_name != NULL)
                tal_free(gui_res->font.node[i].file_name);
            if (gui_res->font.node[i].url != NULL)
                tal_free(gui_res->font.node[i].url);
            if (gui_res->font.node[i].md5 != NULL)
                tal_free(gui_res->font.node[i].md5);
        }
        tal_free(gui_res->font.node);
    }
    if (gui_res->music.node_num > 0) {
        for(i=0; i<gui_res->music.node_num; i++){
            if (gui_res->music.node[i].file_name != NULL)
                tal_free(gui_res->music.node[i].file_name);
            if (gui_res->music.node[i].url != NULL)
                tal_free(gui_res->music.node[i].url);
            if (gui_res->music.node[i].md5 != NULL)
                tal_free(gui_res->music.node[i].md5);
        }
        tal_free(gui_res->music.node);
    }
    if (gui_res->picture.node_num > 0) {
        for(i=0; i<gui_res->picture.node_num; i++){
            if (gui_res->picture.node[i].file_name != NULL)
                tal_free(gui_res->picture.node[i].file_name);
            if (gui_res->picture.node[i].url != NULL)
                tal_free(gui_res->picture.node[i].url);
            if (gui_res->picture.node[i].md5 != NULL)
                tal_free(gui_res->picture.node[i].md5);
        }
        tal_free(gui_res->picture.node);
    }
    if (gui_res->version.file_name != NULL)
        tal_free(gui_res->version.file_name);
    if (gui_res->version.ver != NULL)
        tal_free(gui_res->version.ver);

    tal_free(gui_res);
}

static gui_resource_info_s *gui_resoucre_read(void)
{
    gui_resource_info_s *gui_res = NULL;
    TUYA_FILEINFO entry1 = NULL, entry2 = NULL;
    TUYA_DIR dp = NULL, dpp = NULL;
    CONST CHAR_T *dir_name = NULL;
    CONST CHAR_T *dir_name1 = NULL;
    CONST CHAR_T *lang_path = NULL;
    UCHAR_T i = 0;
    UINT32_T j = 0;
    UINT32_T file_cnt = 0;
    GUI_LANGUAGE_E language = GUI_LG_UNKNOWN;
    OPERATE_RET rt = OPRT_COM_ERROR;
    INT_T dir_read_ret = 0;

    gui_res = (gui_resource_info_s *)tal_malloc(sizeof(gui_resource_info_s));
    if(!gui_res){
        PR_ERR("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
        goto _finish;
    }
    memset(gui_res, 0, sizeof(gui_resource_info_s));

    //统计:data_file
    for (i=0; i<2; i++) {
        rt = ty_gui_dir_open(tuya_app_gui_get_data_file_path(), &dp);
        if (rt != OPRT_OK) {
            PR_ERR("[%s][%d] open path '%s' fail\r\n", __FUNCTION__, __LINE__, tuya_app_gui_get_data_file_path());
            gui_res->data_file.node_num = 0;
            rt = OPRT_NOT_FOUND;
            break;
        }

        for (;;) {
            entry1 = NULL;
            dir_read_ret = ty_gui_dir_read(dp, &entry1);
            if (entry1 == NULL || dir_read_ret != 0)
                break;
            ty_gui_dir_name(entry1, &dir_name);
            if (strcmp(dir_name, ".") == 0 || strcmp(dir_name, "..") == 0) {
                continue;
            }
            //PR_NOTICE("[%s][%d] find path '%s' file '%s'\n", __FUNCTION__, __LINE__, tuya_app_gui_get_data_file_path(), entry1->d_name);
            lang_path = NULL;
            language = GUI_LG_UNKNOWN;
            if (strcasecmp(dir_name, UI_DISPLAY_NAME_CH) == 0) {
                lang_path = tuya_app_gui_get_lang_ch_path();
                language = GUI_LG_CHINESE;
            }
            else if (strcasecmp(dir_name, UI_DISPLAY_NAME_EN) == 0) {
                lang_path = tuya_app_gui_get_lang_en_path();
                language = GUI_LG_ENGLISH;
            }
            else if (strcasecmp(dir_name, UI_DISPLAY_NAME_KR) == 0) {
                lang_path = tuya_app_gui_get_lang_kr_path();
                language = GUI_LG_KOREAN;
            }
            else if (strcasecmp(dir_name, UI_DISPLAY_NAME_JP) == 0) {
                lang_path = tuya_app_gui_get_lang_jp_path();
                language = GUI_LG_JAPANESE;
            }
            else if (strcasecmp(dir_name, UI_DISPLAY_NAME_FR) == 0) {
                lang_path = tuya_app_gui_get_lang_fr_path();
                language = GUI_LG_FRENCH;
            }
            else if (strcasecmp(dir_name, UI_DISPLAY_NAME_DE) == 0) {
                lang_path = tuya_app_gui_get_lang_de_path();
                language = GUI_LG_GERMAN;
            }
            else if (strcasecmp(dir_name, UI_DISPLAY_NAME_RU) == 0) {
                lang_path = tuya_app_gui_get_lang_ru_path();
                language = GUI_LG_RUSSIAN;
            }
            else if (strcasecmp(dir_name, UI_DISPLAY_NAME_IN) == 0) {
                lang_path = tuya_app_gui_get_lang_in_path();
                language = GUI_LG_INDIA;
            }
        #ifdef GUI_RESOURCE_MANAGMENT_VER_1_0
            else if (strcasecmp(dir_name, UI_DISPLAY_NAME_ALL) == 0) {
                lang_path = tuya_app_gui_get_lang_all_path();
                language = GUI_LG_ALL;
            }
        #endif
            else {
                PR_ERR("[%s][%d] un-supported language path folder '%s' ???\n", __FUNCTION__, __LINE__, dir_name);
            }
            if (lang_path != NULL) {
                rt = ty_gui_dir_open(lang_path, &dpp);
                if (rt != OPRT_OK) {
                    PR_ERR("[%s][%d] open path '%s' fail\r\n", __FUNCTION__, __LINE__, lang_path);
                    goto _finish;
                }

                for (;;) {
                    entry2 = NULL;
                    dir_read_ret = ty_gui_dir_read(dpp, &entry2);
                    if (entry2 == NULL || dir_read_ret != 0)
                        break;
                    ty_gui_dir_name(entry2, &dir_name1);
                    if (strcmp(dir_name1, ".") == 0 || strcmp(dir_name1, "..") == 0) {
                        continue;
                    }
                    if (i==0)
                        file_cnt++;
                    else {
                        //PR_NOTICE("[%s][%d] find path '%s', has file '%s'\n", __FUNCTION__, __LINE__, lang_path, entry2->d_name);
                        gui_res->data_file.node[j].language = language;
                        gui_res->data_file.node[j].file_name = gui_resource_strdup(dir_name1);
                        gui_res->data_file.node[j].crc32 = gui_resource_get_file_crc32(lang_path, dir_name1);
                        if (gui_res->data_file.node[j].file_name == NULL){
                            PR_ERR("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
                            goto _finish;
                        }
                        j++;
                    }
                }
                ty_gui_dir_close(dpp);
            }
        }
        ty_gui_dir_close(dp);
        //第一次遍历用于申请空间
        if (i==0 && file_cnt>0) {
            gui_res->data_file.node_num = file_cnt;
            gui_res->data_file.node = (gui_res_lang_s *)tal_malloc(sizeof(gui_res_lang_s)*file_cnt);
            if (gui_res->data_file.node == NULL) {
                PR_ERR("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
                goto _finish;
            }
            memset(gui_res->data_file.node, 0, sizeof(gui_res_lang_s)*file_cnt);
        }
    }

    //统计:font
    rt = gui_resoucre_folder_statistics(tuya_app_gui_get_font_path(), gui_res);
    if (rt != OPRT_OK) {
        if (rt == OPRT_NOT_FOUND) {
            gui_res->font.node_num = 0;
        }
        else {
            goto _finish;
        }
    }

    //统计:music:保留
    rt = gui_resoucre_folder_statistics(tuya_app_gui_get_music_path(), gui_res);
    if (rt != OPRT_OK) {
        if (rt == OPRT_NOT_FOUND) {
            gui_res->music.node_num = 0;
        }
        else {
            goto _finish;
        }
    }

    //统计:picture
    rt = gui_resoucre_folder_statistics(tuya_app_gui_get_picture_path(), gui_res);
    if (rt != OPRT_OK) {
        if (rt == OPRT_NOT_FOUND) {
            gui_res->picture.node_num = 0;
        }
        else {
            goto _finish;
        }
    }

    //统计:version
    gui_res->version.file_name = gui_resource_strdup(tuya_app_gui_get_version_path());
    if (gui_res->version.file_name == NULL){
        PR_ERR("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
        goto _finish;
    }
    rt = gui_resource_read_ver(tuya_app_gui_get_version_path(), &(gui_res->version.ver));
    if (rt == OPRT_OK) {
        //PR_NOTICE("[%s][%d] find ver file '%s', ver id '%s'\n", __FUNCTION__, __LINE__, tuya_app_gui_get_version_path(), gui_res->version.ver);
        //tuya_gui_resource_show(gui_res);
    }

    _finish:
    if (rt != OPRT_OK) {
        if (gui_res != NULL) {      //释放资源
            gui_resource_release(gui_res);
            gui_res = NULL;
        }
    }

    return gui_res;
}

void tuya_gui_resource_show(gui_resource_info_s *gui_res)
{
    int i = 0;

    PR_NOTICE("[%s][%d] =================================>[curr top_path:'%s']\r\n", __func__, __LINE__, tuya_app_gui_get_top_path());
    if (gui_res != NULL) {
        if (gui_res->data_file.node_num > 0) {
            for(i=0; i<gui_res->data_file.node_num; i++){
                PR_NOTICE("data file '%02d': language '%d' file_name '%-30s' crc32 '%u'\r\n", i, gui_res->data_file.node[i].language,
                    gui_res->data_file.node[i].file_name, gui_res->data_file.node[i].crc32);
            }
        }
        if (gui_res->font.node_num > 0) {
            for(i=0; i<gui_res->font.node_num; i++){
                PR_NOTICE("font '%02d': file_name '%-30s' crc32 '%u'\r\n", i,
                    gui_res->font.node[i].file_name, gui_res->font.node[i].crc32);
            }
        }
        if (gui_res->music.node_num > 0) {
            for(i=0; i<gui_res->music.node_num; i++){
                PR_NOTICE("music '%02d': file_name '%-30s' crc32 '%u'\r\n", i,
                    gui_res->music.node[i].file_name, gui_res->music.node[i].crc32);
            }
        }
        if (gui_res->picture.node_num > 0) {
            for(i=0; i<gui_res->picture.node_num; i++){
                PR_NOTICE("picture '%02d': file_name '%-30s' crc32 '%u'\r\n", i,
                    gui_res->picture.node[i].file_name, gui_res->picture.node[i].crc32);
            }
        }
        PR_NOTICE("version : file_name '%-10s' ver id '%s'\r\n",gui_res->version.file_name, gui_res->version.ver);
    }
    PR_NOTICE("[%s][%d] <=================================\r\n", __func__, __LINE__);
}

gui_resource_info_s *tuya_gui_resource_statistics(void)
{
    return gui_resoucre_read();
}

void tuya_gui_resource_release(gui_resource_info_s *gui_res)
{
    if (gui_res != NULL)
        gui_resource_release(gui_res);
}
