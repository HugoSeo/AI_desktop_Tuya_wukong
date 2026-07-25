#include <stdio.h>
#include <stdlib.h>
#include "env_data.h"
#include "_cJSON.h"
#include "common_function.h"
#include "tty_font.h"
#include "tuya_app_gui_display_text.h"
#include "tuya_app_gui_core_config.h"
#include "tuya_cloud_types.h"
#include "ty_gui_fs.h"
#include "tkl_memory.h"

#if defined(TUYA_MODULE_T5) && (TUYA_MODULE_T5 == 1)
typedef struct
{
    char *node_name;
    char *text_name;
    char *font_name;
    unsigned char style;                    //0:一般/1:斜体/2:加粗
    struct dl_list      m_list;
} ui_display_text_config_node_s;

static cJSON *g_ui_lang_root_json = NULL;
static cJSON *g_ui_display_json = NULL;
static bool display_txt_inited = false;

static struct dl_list textConfigInfoHead;

extern VOID_T tkl_system_psram_free(VOID_T* ptr);
extern VOID_T* tkl_system_psram_malloc(CONST SIZE_T size);

static char *_display_text_config_get_text_name(char *node_name);
static char *_display_text_config_get_font_name(char *node_name, LV_FT_FONT_STYLE *font_style);

char * tuya_app_gui_display_text_get(char *node_name)
{
    char *text_name = NULL;

    if (!display_txt_inited) {
        TY_GUI_LOG_PRINT("[%s][%d]: display txt uninitialized  ???\r\n", __FUNCTION__, __LINE__);
        return NULL;
    }

    text_name = _display_text_config_get_text_name(node_name);
    if(text_name == NULL) {
        TY_GUI_LOG_PRINT("[%s][%d]:don't find text string for '%s' ???\r\n", __FUNCTION__, __LINE__, node_name);
    }

    return text_name;
}

/*node_name对应json文件里的字库名*/
const void *tuya_app_gui_display_text_font_get(char *node_name)
{
    char *font_name = NULL;
    LV_FT_FONT_STYLE font_style = FT_FONT_STYLE_NORMAL;

    if (!display_txt_inited) {
        TY_GUI_LOG_PRINT("[%s][%d]: display txt uninitialized  ???\r\n", __FUNCTION__, __LINE__);
        return NULL;
    }

    font_name = _display_text_config_get_font_name(node_name, &font_style);
#ifndef ENABLE_TTF_STYLE_CONFIG
    font_style = FT_FONT_STYLE_NORMAL;
#endif
    if(font_name != NULL)
    {
        if(0 == strcmp(font_name, "montserrat_14")) {
            #if LV_FONT_MONTSERRAT_14
            TY_GUI_LOG_PRINT("[%s][%d]:use internal montserrat_14\r\n", __FUNCTION__, __LINE__);
            return (void *)&lv_font_montserrat_14;
            #else
            #error "no define montserrat_14....................................."
            #endif
        }
        else {      //ttf font!
            return (void *)gui_get_fft_font(node_name, font_name, font_style);
        }
    }
    else {
        TY_GUI_LOG_PRINT("[%s][%d]: font name not exist at node '%s'???\r\n", __FUNCTION__, __LINE__, node_name);
    }

    return NULL;
}

const char *tuya_app_gui_display_text_font_get_language_str(void)
{
    const char *lang_str = NULL;
    GUI_LANGUAGE_E lang = tuya_app_gui_display_text_font_get_language();

    if (lang == GUI_LG_CHINESE) {
    #ifdef GUI_RESOURCE_MANAGMENT_VER_1_0
        lang_str = "zh";
    #else
        lang_str = UI_DISPLAY_NAME_CH;
    #endif
    }
    else if (lang == GUI_LG_ENGLISH) {
        lang_str = UI_DISPLAY_NAME_EN;
    }
    else if (lang == GUI_LG_KOREAN) {
    #ifdef GUI_RESOURCE_MANAGMENT_VER_1_0
        lang_str = "ko";
    #else
        lang_str = UI_DISPLAY_NAME_KR;
    #endif
    }
    else if (lang == GUI_LG_JAPANESE) {
    #ifdef GUI_RESOURCE_MANAGMENT_VER_1_0
        lang_str = "ja";
    #else
        lang_str = UI_DISPLAY_NAME_JP;
    #endif
    }
    else if (lang == GUI_LG_FRENCH) {
        lang_str = UI_DISPLAY_NAME_FR;
    }
    else if (lang == GUI_LG_GERMAN) {
        lang_str = UI_DISPLAY_NAME_DE;
    }
    else if (lang == GUI_LG_RUSSIAN) {
        lang_str = UI_DISPLAY_NAME_RU;
    }
    else if (lang == GUI_LG_INDIA) {
    #ifdef GUI_RESOURCE_MANAGMENT_VER_1_0
        lang_str = "hi";
    #else
        lang_str = UI_DISPLAY_NAME_IN;
    #endif
    }
    else {
        TY_GUI_LOG_PRINT("[%s][%d] un-supported language type '%d' ???\n", __FUNCTION__, __LINE__, lang);
    }

    return lang_str;
}

static int _load_display_file(char **file_string)
{
    GUI_LANGUAGE_E language = tuya_app_gui_display_text_font_get_language();
    OPERATE_RET ret = OPRT_COM_ERROR;
    char *display_file = NULL;
    CONST char *file_path = NULL;
#ifdef TUYA_APP_USE_INTERNAL_FLASH_FS
    struct dirent *entry;
    DIR *dp = NULL;
#else
    TUYA_FILEINFO entry = NULL;
    TUYA_DIR dp = NULL;
    CONST CHAR_T *dir_name = NULL;
    INT_T dir_read_ret = 0;
#endif

#ifdef GUI_RESOURCE_MANAGMENT_VER_1_0
    file_path = tuya_app_gui_get_lang_all_path();
#else
    if(GUI_LG_CHINESE == language)
        file_path = tuya_app_gui_get_lang_ch_path();
    else if(GUI_LG_ENGLISH == language)
        file_path = tuya_app_gui_get_lang_en_path();
    else if(GUI_LG_KOREAN == language)
        file_path = tuya_app_gui_get_lang_kr_path();
    else if(GUI_LG_JAPANESE == language)
        file_path = tuya_app_gui_get_lang_jp_path();
    else if(GUI_LG_FRENCH == language)
        file_path = tuya_app_gui_get_lang_fr_path();
    else if(GUI_LG_GERMAN == language)
        file_path = tuya_app_gui_get_lang_de_path();
    else if(GUI_LG_RUSSIAN == language)
        file_path = tuya_app_gui_get_lang_ru_path();
    else if(GUI_LG_INDIA == language)
        file_path = tuya_app_gui_get_lang_in_path();
    else {
        TY_GUI_LOG_PRINT("[%s][%d] unknown language type '%d' ???\r\n", __FUNCTION__, __LINE__, language);
        return ret;
    }
#endif

#ifdef TUYA_APP_USE_INTERNAL_FLASH_FS
    dp = opendir(file_path);
#else
    ret = ty_gui_dir_open(file_path, &dp);
    if (ret != OPRT_OK) {
        TY_GUI_LOG_PRINT("[%s][%d] open path '%s' fail\r\n", __FUNCTION__, __LINE__, file_path);
    }
#endif
    if (dp == NULL) {
        TY_GUI_LOG_PRINT("[%s][%d] open ui language config fail\r\n", __FUNCTION__, __LINE__);
    }
    else {
        //遍历目录
    #ifdef TUYA_APP_USE_INTERNAL_FLASH_FS
        while ((entry = readdir(dp))) {
            TY_GUI_LOG_PRINT("[%s][%d] find ui language config folder file '%s'\n", __FUNCTION__, __LINE__, entry->d_name);
            if (strstr(entry->d_name, ".json") != NULL || strstr(entry->d_name, ".JSON") != NULL) {
                display_file = (char *)tkl_system_malloc(strlen(file_path)+strlen(entry->d_name)+1);
                if(!display_file){
                    TY_GUI_LOG_PRINT("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
                    break;
                }
                memset(display_file, 0, strlen(file_path)+strlen(entry->d_name)+1);
                sprintf(display_file, "%s%s", file_path, entry->d_name);
                break;
            }
        }
    #else
        for (;;) {
            entry = NULL;
            dir_read_ret = ty_gui_dir_read(dp, &entry);
            if (entry == NULL || dir_read_ret != 0)
                break;
            ty_gui_dir_name(entry, &dir_name);
            if (strcmp(dir_name, ".") == 0 || strcmp(dir_name, "..") == 0) {
                continue;
            }
            TY_GUI_LOG_PRINT("[%s][%d] find ui language config folder file '%s'\n", __FUNCTION__, __LINE__, dir_name);
            if (strstr(dir_name, ".json") != NULL || strstr(dir_name, ".JSON") != NULL) {
                display_file = (char *)tkl_system_malloc(strlen(file_path)+strlen(dir_name)+1);
                if(!display_file){
                    TY_GUI_LOG_PRINT("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
                    break;
                }
                memset(display_file, 0, strlen(file_path)+strlen(dir_name)+1);
                sprintf(display_file, "%s%s", file_path, dir_name);
                break;
            }
        }
    #endif
        if (display_file != NULL) {
            ret = cf_read_file_to_psram(display_file, file_string);
            TY_GUI_LOG_PRINT("[%s][%d]:load language '%d' ret '%d' in file '%s'\r\n", __FUNCTION__, __LINE__, language, ret, display_file);
            tkl_system_free(display_file);
        }
    }

    if (dp != NULL) {
    #ifdef TUYA_APP_USE_INTERNAL_FLASH_FS
        closedir(dp);
    #else
        ty_gui_dir_close(dp);
    #endif
    }

    return ret;
}

static OPERATE_RET _display_text_config_load(void)
{
    char *file_string = NULL;
    //cJSON *root_json = NULL;
    OPERATE_RET ret = OPRT_OK;
/* 更新后完整json格式如下:
{
  "lang": "en",
  "data": {
    "language_test": [
      "ENGLISH",
      "NotoSansSC-VariableFont_wght_new_15"
    ],
    "language_test1": [
      "ENGLISH",
      "STHUPO_new_20"
    ],
    "language_test2": [
      "ENGLISH",
      "STHUPO_new_20"
    ],
    "homePTextDes": [
      "To Enrich People's Lives by Connecting the World",
      "simhei_new_21"
    ]
  }
}
*/
    ret = _load_display_file(&file_string);
    if(OPRT_OK == ret)
    {
        g_ui_lang_root_json = cJSON_Parse(file_string);
    #ifdef GUI_RESOURCE_MANAGMENT_VER_1_0
        const char *lang_str = tuya_app_gui_display_text_font_get_language_str();
        if (lang_str) {
            g_ui_display_json = cJSON_GetObjectItem(g_ui_lang_root_json, lang_str);
            if (g_ui_display_json) {
                #if 0
                char *p_root_dbg = cJSON_PrintUnformatted(g_ui_display_json);
                if (p_root_dbg == NULL) {
                    TY_GUI_LOG_PRINT("[%s][%d]:load language json config fail ???\r\n", __FUNCTION__, __LINE__);
                    g_ui_display_json = NULL;
                    ret = OPRT_COM_ERROR;
                }
                else {
                    TY_GUI_LOG_PRINT("[%s][%d]:lang_root_json: '%s'\r\n", __FUNCTION__, __LINE__, p_root_dbg);
                    tkl_system_psram_free(p_root_dbg);
                    p_root_dbg = NULL;
                }
                #endif
            }
            else {
                TY_GUI_LOG_PRINT("[%s][%d]:not found lang '%s' content ?\r\n", __FUNCTION__, __LINE__, lang_str);
            }
        }
    #else
        g_ui_display_json = cJSON_GetObjectItem(g_ui_lang_root_json, "data");
    #endif
        if (!g_ui_display_json) {
            TY_GUI_LOG_PRINT("[%s][%d]:load language json config fail ???\r\n", __FUNCTION__, __LINE__);
            ret = OPRT_COM_ERROR;
        }
        tkl_system_psram_free(file_string);
    }
    return ret;
}

static OPERATE_RET _display_text_config_restore(cJSON *ui_display_json)
{
    static bool power_start = true;
    OPERATE_RET ret = OPRT_OK;
    cJSON *current_element = NULL;
    ui_display_text_config_node_s *node = NULL;
    ui_display_text_config_node_s *tmpp, *nn;

    //释放空间
    if (!power_start) {   //第一次上电或复位,不用释放!
        dl_list_for_each_safe(tmpp, nn, &textConfigInfoHead, ui_display_text_config_node_s, m_list) {
            if (tmpp->node_name != NULL)
                tkl_system_psram_free(tmpp->node_name);
            if (tmpp->text_name != NULL)
                tkl_system_psram_free(tmpp->text_name);
            if (tmpp->font_name != NULL)
                tkl_system_psram_free(tmpp->font_name);
            dl_list_del(&tmpp->m_list);
            tkl_system_psram_free(tmpp);
        }
    }

    dl_list_init(&textConfigInfoHead);
    power_start = false;
    if (ui_display_json == NULL) {
        TY_GUI_LOG_PRINT("[%s][%d] ui_display_json is null !\r\n", __FUNCTION__, __LINE__);
        ret = OPRT_COM_ERROR;
        return ret;
    }

    // 遍历 JSON 对象
#ifdef GUI_RESOURCE_MANAGMENT_VER_1_0
    #define res_max_len     64
    cJSON* libItem = NULL;
    cJSON* fontWeightItem = NULL;
    char *libKey = NULL, *fontWeightKey = NULL;
    UINT32_T mem_len = res_max_len;

    libKey = (char *)tkl_system_psram_malloc(mem_len);
    fontWeightKey = (char *)tkl_system_psram_malloc(mem_len);
    if (libKey == NULL || fontWeightKey == NULL) {
        TY_GUI_LOG_PRINT("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
        ret = OPRT_COM_ERROR;
        return ret;
    }
    cJSON_ArrayForEach(current_element, ui_display_json) {
        const char* key = current_element->string;

        if (strlen(key) > (mem_len-16)) {
            TY_GUI_LOG_PRINT("[%s][%d] key length '%d' is too long,realloc !\r\n", __FUNCTION__, __LINE__, strlen(key));
            tkl_system_psram_free(libKey);
            tkl_system_psram_free(fontWeightKey);
            mem_len = strlen(key) + 16;
            libKey = (char *)tkl_system_psram_malloc(mem_len);
            fontWeightKey = (char *)tkl_system_psram_malloc(mem_len);
            if (libKey == NULL || fontWeightKey == NULL) {
                TY_GUI_LOG_PRINT("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
                ret = OPRT_COM_ERROR;
                return ret;
            }
        }
        if (strstr(key, "@")) continue;

        node = (ui_display_text_config_node_s *)tkl_system_psram_malloc(sizeof(ui_display_text_config_node_s));
        if (!node) {
            TY_GUI_LOG_PRINT("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
            ret = OPRT_COM_ERROR;
            break;
        }
        memset(node, 0, sizeof(ui_display_text_config_node_s));
        node->node_name = (char *)tkl_system_psram_malloc(strlen(key) + 1);
        if (!node->node_name) {
            TY_GUI_LOG_PRINT("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
            ret = OPRT_COM_ERROR;
            break;
        }
        memset(node->node_name, 0, strlen(key) + 1);
        strcpy(node->node_name, key);

        // get ralated @lib and @fontWeight
        memset(libKey, 0, mem_len);
        memset(fontWeightKey, 0, mem_len);
        snprintf(libKey, mem_len, "%s@lib", key);
        snprintf(fontWeightKey, mem_len, "%s@fontWeight", key);

        libItem = cJSON_GetObjectItem(ui_display_json, libKey);
        fontWeightItem = cJSON_GetObjectItem(ui_display_json, fontWeightKey);
        if (libItem == NULL) {
            TY_GUI_LOG_PRINT("[%s][%d] empty info at node '%s', continue !\r\n", __FUNCTION__, __LINE__, key);
            //ret = OPRT_COM_ERROR;
            //break;
            tkl_system_psram_free(node->node_name);
            tkl_system_psram_free(node);
            continue;
        }

        node->text_name = (char *)tkl_system_psram_malloc(strlen(current_element->valuestring) + 1);
        node->font_name = (char *)tkl_system_psram_malloc(strlen(libItem->valuestring) + 1);
        if (node->text_name == NULL || node->font_name == NULL) {
            TY_GUI_LOG_PRINT("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
            ret = OPRT_COM_ERROR;
            break;
        }
        memset(node->text_name, 0, strlen(current_element->valuestring) + 1);
        memset(node->font_name, 0, strlen(libItem->valuestring) + 1);
        strcpy(node->text_name, current_element->valuestring);
        strcpy(node->font_name, libItem->valuestring);
        if (fontWeightItem != NULL) {
            node->style = (unsigned char)atoi(fontWeightItem->valuestring);
        }
        dl_list_add(&textConfigInfoHead, &node->m_list);
        //TY_GUI_LOG_PRINT("[%s][%d] get info at node '%s' %s' '%s' '%d'\r\n", __FUNCTION__, __LINE__, 
        //    node->node_name, node->text_name, node->font_name, node->style);
    }
    tkl_system_psram_free(libKey);
    tkl_system_psram_free(fontWeightKey);
    #undef res_max_len
#else
    cJSON *array_text = NULL, *array_font = NULL, *array_font_style = NULL;
    cJSON_ArrayForEach(current_element, ui_display_json) {
        node = (ui_display_text_config_node_s *)tkl_system_psram_malloc(sizeof(ui_display_text_config_node_s));
        if (!node) {
            TY_GUI_LOG_PRINT("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
            ret = OPRT_COM_ERROR;
            break;
        }
        memset(node, 0, sizeof(ui_display_text_config_node_s));
        node->node_name = (char *)tkl_system_psram_malloc(strlen(current_element->string) + 1);
        if (!node->node_name) {
            TY_GUI_LOG_PRINT("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
            ret = OPRT_COM_ERROR;
            break;
        }
        memset(node->node_name, 0, strlen(current_element->string) + 1);
        strcpy(node->node_name, current_element->string);
        array_text = cJSON_GetArrayItem(current_element, 0);
        array_font = cJSON_GetArrayItem(current_element, 1);
        array_font_style = cJSON_GetArrayItem(current_element, 2);
        if (array_text == NULL || array_font == NULL) {
            TY_GUI_LOG_PRINT("[%s][%d] empty info at node '%s'\r\n", __FUNCTION__, __LINE__, current_element->valuestring);
            ret = OPRT_COM_ERROR;
            break;
        }
        node->text_name = (char *)tkl_system_psram_malloc(strlen(array_text->valuestring) + 1);
        node->font_name = (char *)tkl_system_psram_malloc(strlen(array_font->valuestring) + 1);
        if (node->text_name == NULL || node->font_name == NULL) {
            TY_GUI_LOG_PRINT("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
            ret = OPRT_COM_ERROR;
            break;
        }
        memset(node->text_name, 0, strlen(array_text->valuestring) + 1);
        memset(node->font_name, 0, strlen(array_font->valuestring) + 1);
        strcpy(node->text_name, array_text->valuestring);
        strcpy(node->font_name, array_font->valuestring);
        if (array_font_style != NULL) {
            node->style = (unsigned char)atoi(array_font_style->valuestring);
        }
        dl_list_add(&textConfigInfoHead, &node->m_list);
        //TY_GUI_LOG_PRINT("[%s][%d] get info at node '%s' %s' '%s'\r\n", __FUNCTION__, __LINE__, node->node_name, node->text_name, node->font_name);
    }
#endif

    return ret;
}

static char *_display_text_config_get_text_name(char *node_name)
{
    ui_display_text_config_node_s *tmpp, *nn;
    char *text_name = NULL;

    dl_list_for_each_safe(tmpp, nn, &textConfigInfoHead, ui_display_text_config_node_s, m_list) {
        if (strcmp(tmpp->node_name, node_name) == 0) {
            text_name = tmpp->text_name;
            break;
        }
    }
    return text_name;
}

static char *_display_text_config_get_font_name(char *node_name, LV_FT_FONT_STYLE *font_style)
{
    ui_display_text_config_node_s *tmpp, *nn;
    char *font_name = NULL;

    dl_list_for_each_safe(tmpp, nn, &textConfigInfoHead, ui_display_text_config_node_s, m_list) {
        if (strcmp(tmpp->node_name, node_name) == 0) {
            font_name = tmpp->font_name;
            *font_style = (LV_FT_FONT_STYLE)(tmpp->style);
            break;
        }
    }
    return font_name;
}

OPERATE_RET tuya_app_display_text_init(void)
{
    OPERATE_RET ret = OPRT_OK;

    display_txt_inited = false;
#if defined(TUYA_IMG_DIRECT_FLUSH) && (TUYA_IMG_DIRECT_FLUSH == 1)      //直接屏幕刷新不做字库初始化(lvgl未做初始化)
    return ret;
#endif
#if !(defined(TUYA_FILE_SYSTEM) && (TUYA_FILE_SYSTEM == 1))
    TY_GUI_LOG_PRINT("[%s][%d] no config file system !!! \r\n", __FUNCTION__, __LINE__);
    return ret;
#endif
    ret = _display_text_config_load();
    ret = _display_text_config_restore(g_ui_display_json);  //字体配置保存
    if (OPRT_OK == ret)
        ret = gui_ttf_init(g_ui_display_json);                  //ttf字体库初始化
    display_txt_inited = (OPRT_OK == ret)?true:false;
    if (g_ui_lang_root_json != NULL) {
        cJSON_Delete(g_ui_lang_root_json);
        g_ui_lang_root_json = NULL;
    }
    g_ui_display_json = NULL;
    return ret;
}

#else
char * tuya_app_gui_display_text_get(char *node_name)
{
    TY_GUI_LOG_PRINT("[%s][%d]: unsupport ? \r\n", __FUNCTION__, __LINE__);
    return NULL;
}

const void *tuya_app_gui_display_text_font_get(char *node_name)
{
    TY_GUI_LOG_PRINT("[%s][%d]: unsupport ? \r\n", __FUNCTION__, __LINE__);
    return NULL;
}

OPERATE_RET tuya_app_display_text_init(void)
{
    TY_GUI_LOG_PRINT("[%s][%d]: unsupport ? \r\n", __FUNCTION__, __LINE__);
    return OPRT_COM_ERROR;
}
#endif

GUI_LANGUAGE_E tuya_app_gui_display_text_font_get_language(void)
{
    return ed_get_language();
}
