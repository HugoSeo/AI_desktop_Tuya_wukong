#include <stdio.h>
#include <stdlib.h>
#include "tty_font.h"
#include "tuya_app_gui_fs_path.h"
#include "ty_gui_fs.h"
#include "tkl_memory.h"

#if defined(TUYA_MODULE_T5) && (TUYA_MODULE_T5 == 1)

//#define TTY_FONT_DBG
#define FONT_LOAD_FROM_MEM            //字体太多,申请内存容易失败!
static struct dl_list ttfFileInfoHead;

extern VOID_T tkl_system_psram_free(VOID_T* ptr);
extern VOID_T* tkl_system_psram_malloc(CONST SIZE_T size);

#ifdef FONT_LOAD_FROM_MEM
static OPERATE_RET _load_font_to_psram(char *font_name, void **font_map, size_t *font_map_len)
{
    OPERATE_RET ret = OPRT_COM_ERROR;
    size_t file_len = 0;
    uint8_t *file_content = NULL;
#ifdef TUYA_APP_USE_INTERNAL_FLASH_FS
    int fd = -1;
#else
    TUYA_FILE fd = NULL;
#endif

    TY_GUI_LOG_PRINT("------%s----------[%s]\n\r", __func__, font_name);
    do{
        if(!font_name)
        {
            TY_GUI_LOG_PRINT("[%s][%d]param is null.\r\n", __FUNCTION__, __LINE__);
            ret = OPRT_COM_ERROR;
            break;
        }

    #ifdef TUYA_APP_USE_INTERNAL_FLASH_FS
        struct stat statbuf;
        ret = stat(font_name, &statbuf);
        if(OPRT_OK != ret)
        {
            TY_GUI_LOG_PRINT("[%s][%d] sta fail:%s\r\n", __FUNCTION__, __LINE__, font_name);
            break;
        }

        file_len = statbuf.st_size;
    #else
        file_len = ty_gui_fgetsize(font_name);
        if(file_len <= 0)
        {
            TY_GUI_LOG_PRINT("[%s][%d] sta fail:%s\r\n", __FUNCTION__, __LINE__, font_name);
            break;
        }
    #endif
        file_content = tkl_system_psram_malloc(file_len);
        if (file_content == NULL) {
            ret = OPRT_COM_ERROR;
            TY_GUI_LOG_PRINT("[%s][%d] malloc sizeof of '%u' fail ?\r\n", __FUNCTION__, __LINE__, file_len);
            break;
        }

#ifdef TUYA_APP_USE_INTERNAL_FLASH_FS
        fd = open(font_name, O_RDONLY);
        if(fd <= 0)
#else
        fd = ty_gui_fopen(font_name, "r");
        if(fd == NULL)
#endif
        {
            TY_GUI_LOG_PRINT("[%s][%d]%s fail\r\n", __func__, __LINE__, font_name);
            tkl_system_psram_free(file_content);
            ret = OPRT_COM_ERROR;
            break;
        }

#ifdef TUYA_APP_USE_INTERNAL_FLASH_FS
        read(fd, file_content, file_len);
        close(fd);
#else
        ty_gui_fread(file_content, file_len, fd);
        ty_gui_fclose(fd);
#endif
        *font_map = file_content;
        *font_map_len = file_len;
        ret = OPRT_OK;
    }while(0);

    return ret;
}
#endif

static void display_font_name_list_delete(struct dl_list *displayFontInfoHd)
{
    ui_display_str_node_s *tmp, *n;

    dl_list_for_each_safe(tmp, n, displayFontInfoHd, ui_display_str_node_s, m_list) {
        if(tmp->font_name != NULL){
            tkl_system_free(tmp->font_name);
            dl_list_del(&tmp->m_list);
            tkl_system_free(tmp);
        }
    }
}

static bool display_font_name_list_is_repeat(struct dl_list *displayFontInfoHd, char *font_name, unsigned char font_style)
{
    bool is_repeat = false;
    ui_display_str_node_s *tmp, *n;

    dl_list_for_each_safe(tmp, n, displayFontInfoHd, ui_display_str_node_s, m_list) {
        if((strcasecmp(tmp->font_name, font_name) == 0) && (tmp->font_style == font_style)){
            is_repeat = true;
            break;
        }
    }
    return is_repeat;
}

static void ttf_file_info_release(void)
{
    ttf_file_info_node_s *tmpp, *nn;
    ttf_font_info_node_s *tmp, *n;
    void *mem_addr = NULL;

    dl_list_for_each_safe(tmpp, nn, &ttfFileInfoHead, ttf_file_info_node_s, m_list) {
        dl_list_for_each_safe(tmp, n, &tmpp->ttfFontInfoHead, ttf_font_info_node_s, m_list) {
            dl_list_del(&tmp->m_list);
        #if LVGL_VERSION_MAJOR < 9
            lv_ft_font_destroy(tmp->fft_info.font);         //注销及销毁相关字体资源,否则有内存泄漏!
        #else
            lv_freetype_font_delete(tmp->fft_info.font);
        #endif
            if (tmp->fft_info.mem != NULL) {
                mem_addr = (void *)tmp->fft_info.mem;
                tkl_system_psram_free(mem_addr);    //直接释放const,编译告警!
            }
            tkl_system_free(tmp);
        }
        if (tmpp->file_name != NULL)
            tkl_system_free(tmpp->file_name);
        if (tmpp->font_name != NULL)
            tkl_system_free(tmpp->font_name);
        dl_list_del(&tmpp->m_list);
        tkl_system_free(tmpp);
    }
}

static OPERATE_RET ttf_font_info_fill(struct dl_list *displayFontInfoHd)
{
    OPERATE_RET ret = OPRT_OK;
    void *g_PingFang_map = NULL;
    /*
    tmpp->file_name格式为目录+文件名,类似:                /font/STHUPO.TTF
    tmp->font_name格式为字体名_字体大小,类似:               STHUPO_14
    */
    ui_display_str_node_s *tmp, *n;
    ttf_file_info_node_s *tmpp, *nn;
    ttf_font_info_node_s *ttfFontNode = NULL;
    uint16_t font_weight = 0;        //字体大小
    char *ptr = NULL;
    bool found_node = false;

    dl_list_for_each_safe(tmp, n, displayFontInfoHd, ui_display_str_node_s, m_list) {
        ptr = strrchr(tmp->font_name, '_');
        if (ptr != NULL) {
            found_node = false;
            font_weight = atoi(ptr+1);
            tmp->font_name[ptr-tmp->font_name] = '.';   //提高搜索精确度,比较如字串: "STHUPO."
            tmp->font_name[ptr-tmp->font_name+1] = '\0';
            TY_GUI_LOG_PRINT("[%s][%d] try to find font '%s_%d'\r\n", __FUNCTION__, __LINE__, tmp->font_name, font_weight);
            dl_list_for_each_safe(tmpp, nn, &ttfFileInfoHead, ttf_file_info_node_s, m_list) {
                if(strncasecmp(tmp->font_name, tmpp->file_name+strlen(tuya_app_gui_get_font_path()), strlen(tmp->font_name)) == 0){
                    TY_GUI_LOG_PRINT("[%s][%d] found matched file '%s'\r\n", __FUNCTION__, __LINE__, tmpp->file_name);
                    ttfFontNode = (ttf_font_info_node_s *)tkl_system_malloc(sizeof(ttf_font_info_node_s));
                    if(!ttfFontNode){
                        TY_GUI_LOG_PRINT("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
                        return OPRT_COM_ERROR;
                    }
                    memset(ttfFontNode, 0, sizeof(ttf_font_info_node_s));
                    ttfFontNode->weight = font_weight;
                #ifdef FONT_LOAD_FROM_MEM
                    size_t g_PingFang_map_len = 0;
                    ret = _load_font_to_psram(tmpp->file_name, &g_PingFang_map, &g_PingFang_map_len);
                    if(ret != OPRT_OK)
                    {
                        TY_GUI_LOG_PRINT("[%s][%d] load ttf font '%s' fail\r\n", __FUNCTION__, __LINE__, tmpp->file_name);
                        tkl_system_free(ttfFontNode);
                        return OPRT_COM_ERROR;
                    }
                    ttfFontNode->fft_info.mem = g_PingFang_map;         //从内存中加载字体
                    ttfFontNode->fft_info.mem_size = g_PingFang_map_len;
                #else
                    ttfFontNode->fft_info.mem = NULL;
                    ttfFontNode->fft_info.mem_size = 0;
                #endif
                    ttfFontNode->fft_info.name = tmpp->file_name;       //"/font/"fft_font_info->file_name;
                    ttfFontNode->fft_info.weight = font_weight;         //决定字体大小
                #ifdef ENABLE_TTF_STYLE_CONFIG
                    if (tmp->font_style != FT_FONT_STYLE_NORMAL && tmp->font_style != FT_FONT_STYLE_ITALIC && tmp->font_style != FT_FONT_STYLE_BOLD) {
                        TY_GUI_LOG_PRINT("[%s][%d] unknown ttf font '%s' style '%d', use default normal style !\r\n", __FUNCTION__, __LINE__, 
                            tmpp->file_name, tmp->font_style);
                        ttfFontNode->fft_info.style = FT_FONT_STYLE_NORMAL;
                    }
                    else
                        ttfFontNode->fft_info.style = tmp->font_style;      //字体风格:0:常规/1:斜体/2:加粗
                #else
                    ttfFontNode->fft_info.style = FT_FONT_STYLE_NORMAL;     //字体风格:常规/斜体/加粗
                #endif
                #if LVGL_VERSION_MAJOR < 9
                    if(!lv_ft_font_init(&(ttfFontNode->fft_info)))
                #else
                    ttfFontNode->fft_info.font = lv_freetype_font_create_from_memory((const uint8_t *)ttfFontNode->fft_info.mem, ttfFontNode->fft_info.mem_size, \
                                    LV_FREETYPE_FONT_RENDER_MODE_BITMAP, ttfFontNode->fft_info.weight, ttfFontNode->fft_info.style);
                    if (ttfFontNode->fft_info.font == NULL)
                #endif
                    {
                        TY_GUI_LOG_PRINT("fft font '%s %d' create failed.\r\n", tmpp->file_name, font_weight);
                        tkl_system_free(ttfFontNode);
                        if (g_PingFang_map != NULL)
                            tkl_system_psram_free(g_PingFang_map);
                        g_PingFang_map = NULL;
                        return OPRT_COM_ERROR;
                    }
                    dl_list_add(&tmpp->ttfFontInfoHead, &ttfFontNode->m_list);
                    found_node = true;
                    break;
                }
            }
            if (!found_node) {
                TY_GUI_LOG_PRINT("[%s][%d] no found font '%s_%d'\r\n", __FUNCTION__, __LINE__, tmp->font_name, font_weight);
                //ret = OPRT_COM_ERROR;        //有可能使用本地字库非ttf字库,如montserrat_14,故暂时不用报错!
                //break;
            }
        }
        else {
            TY_GUI_LOG_PRINT("[%s][%d] unknown config font '%s'\r\n", __FUNCTION__, __LINE__, tmp->font_name);
            ret = OPRT_COM_ERROR;
            break;
        }
    }

    return ret;
}

static bool tty_font_name_is_repeat(struct dl_list *FontNameInfoHd, char *font_name)
{
    bool is_repeat = false;
    ui_display_font_name_s *tmp, *n;

    dl_list_for_each_safe(tmp, n, FontNameInfoHd, ui_display_font_name_s, m_list) {
        if(strcasecmp(tmp->font_name, font_name) == 0){
            is_repeat = true;
            break;
        }
    }
    return is_repeat;
}

static bool tty_font_weight_is_repeat(struct dl_list *FontWeightInfoHd, uint16_t font_weight)
{
    bool is_repeat = false;
    ui_display_font_weight_s *tmp, *n;

    dl_list_for_each_safe(tmp, n, FontWeightInfoHd, ui_display_font_weight_s, m_list) {
        if(tmp->font_weight == font_weight){
            is_repeat = true;
            break;
        }
    }
    return is_repeat;
}

/*
    按照实际的字体种类和字体大小数目重新初始化freetype,失败则使用默认的freetype初始化参数!
*/
static OPERATE_RET ttf_font_freetype_reinit(struct dl_list *displayFontInfoHd)
{
    OPERATE_RET ret = OPRT_OK;

#if LV_FREETYPE_CACHE_SIZE >= 0
    #if LVGL_VERSION_MAJOR < 9
    static uint16_t last_max_faces = LV_FREETYPE_CACHE_FT_FACES, last_max_sizes = LV_FREETYPE_CACHE_FT_SIZES;
    #else
    static uint16_t last_max_faces = 1, last_max_sizes = 1;
    #endif
    uint16_t max_faces = 0, max_sizes = 0;
    ui_display_str_node_s *tmp, *n;
    char *tmp_font_name = NULL;
    uint16_t tmp_font_weight = 0;
    char *ptr = NULL;
    int i = 0;
    struct dl_list FontNameInfoHead;
    struct dl_list FontWeightInfoHead;
    ui_display_font_name_s *font_name_node = NULL, *tmp_name = NULL, *n_name = NULL;
    ui_display_font_weight_s *font_weight_node = NULL, *tmp_weight = NULL, *n_weight = NULL;

    dl_list_init(&FontNameInfoHead);
    dl_list_init(&FontWeightInfoHead);

    dl_list_for_each_safe(tmp, n, displayFontInfoHd, ui_display_str_node_s, m_list) {
        tmp_font_name = (char *)tkl_system_malloc(strlen(tmp->font_name)+1);
        if (tmp_font_name == NULL) {
            ret = OPRT_COM_ERROR;
            TY_GUI_LOG_PRINT("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
            goto err_out;
        }
        memset(tmp_font_name, 0, strlen(tmp->font_name)+1);
        strcpy(tmp_font_name, tmp->font_name);
        ptr = strrchr(tmp_font_name, '_');
        if (ptr == NULL) {
            ret = OPRT_COM_ERROR;
            TY_GUI_LOG_PRINT("[%s][%d] invalid font name '%s' !\r\n", __FUNCTION__, __LINE__, tmp_font_name);
            tkl_system_free(tmp_font_name);
            goto err_out;
        }
        tmp_font_weight = atoi(ptr+1);
        tmp_font_name[ptr-tmp_font_name] = '\0';
        TY_GUI_LOG_PRINT("[%s][%d] '%02d' valid font name '%s', weight '%d' !\r\n", __FUNCTION__, __LINE__, i, tmp_font_name, tmp_font_weight);
        if (!tty_font_name_is_repeat(&FontNameInfoHead, tmp_font_name)) {
            font_name_node = (ui_display_font_name_s *)tkl_system_malloc(sizeof(ui_display_font_name_s));
            if(!font_name_node){
                TY_GUI_LOG_PRINT("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
                ret = OPRT_COM_ERROR;
                tkl_system_free(tmp_font_name);
                goto err_out;
            }
            memset(font_name_node, 0, sizeof(ui_display_font_name_s));
            font_name_node->font_name = (char *)tkl_system_malloc(strlen(tmp_font_name)+1);
            memset(font_name_node->font_name, 0, strlen(tmp_font_name)+1);
            strcpy(font_name_node->font_name, tmp_font_name);
            dl_list_add(&FontNameInfoHead, &font_name_node->m_list);
            max_faces++;
        }
        if (!tty_font_weight_is_repeat(&FontWeightInfoHead, tmp_font_weight)) {
            font_weight_node = (ui_display_font_weight_s *)tkl_system_malloc(sizeof(ui_display_font_weight_s));
            if(!font_weight_node){
                TY_GUI_LOG_PRINT("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
                ret = OPRT_COM_ERROR;
                tkl_system_free(tmp_font_name);
                goto err_out;
            }
            memset(font_weight_node, 0, sizeof(ui_display_font_weight_s));
            font_weight_node->font_weight = tmp_font_weight;
            dl_list_add(&FontWeightInfoHead, &font_weight_node->m_list);
            max_sizes++;
        }
        tkl_system_free(tmp_font_name);
        i++;
    }

    if (last_max_faces != max_faces || last_max_sizes != max_sizes) {
        TY_GUI_LOG_PRINT("[%s][%d] final freetype init max_faces '%d', max_sizes '%d'\r\n", __FUNCTION__, __LINE__, max_faces, max_sizes);
    #if LVGL_VERSION_MAJOR < 9
        lv_freetype_destroy();
        lv_freetype_init(max_faces, max_sizes, LV_FREETYPE_CACHE_SIZE);
    #else
        lv_freetype_uninit();
        lv_freetype_init(max_faces*max_sizes*LV_FREETYPE_CACHE_FT_GLYPH_CNT);       //LV_FREETYPE_CACHE_FT_GLYPH_CNT:以实际产品中的字体数目替代!
    #endif
        last_max_faces = max_faces;
        last_max_sizes = max_sizes;
    }

    err_out:
    dl_list_for_each_safe(tmp_name, n_name, &FontNameInfoHead, ui_display_font_name_s, m_list) {
        dl_list_del(&tmp_name->m_list);
        if (tmp_name->font_name)
            tkl_system_free(tmp_name->font_name);
        tkl_system_free(tmp_name);
    }
    dl_list_for_each_safe(tmp_weight, n_weight, &FontWeightInfoHead, ui_display_font_weight_s, m_list) {
        dl_list_del(&tmp_weight->m_list);
        tkl_system_free(tmp_weight);
    }
#endif
    return ret;
}

#ifdef TTY_FONT_DBG
static void display_font_name_list_dump(struct dl_list *displayFontInfoHd)
{
    ui_display_str_node_s *tmp, *n;
    int i = 0;

    dl_list_for_each_safe(tmp, n, displayFontInfoHd, ui_display_str_node_s, m_list) {
        if(tmp->font_name != NULL){
            TY_GUI_LOG_PRINT("[%s][%d]--->dbg: num '%d' ui display font name'%s' font style '%d'\r\n", __FUNCTION__, __LINE__, i++, tmp->font_name, tmp->font_style);
        }
    }
    TY_GUI_LOG_PRINT("[%s][%d]--->dbg: total '%d' ui display font name\r\n", __FUNCTION__, __LINE__, i);
}

static void ttf_file_info_list_dump(void)
{
    ttf_file_info_node_s *tmpp, *nn;
    ttf_font_info_node_s *tmp, *n;
    int i = 0, j = 0;

    dl_list_for_each_safe(tmpp, nn, &ttfFileInfoHead, ttf_file_info_node_s, m_list) {
        TY_GUI_LOG_PRINT("[%s][%d]===>dbg: num '%d' ttf file name:'%s' font name '%s'\r\n", __FUNCTION__, __LINE__, i, tmpp->file_name, tmpp->font_name);
        dl_list_for_each_safe(tmp, n, &tmpp->ttfFontInfoHead, ttf_font_info_node_s, m_list) {
            TY_GUI_LOG_PRINT("[%s][%d]--->dbg: num '%d/%d' ttf font node weight:'%d' line_height: '%d' base_line: '%d' style '%d'\r\n", __FUNCTION__, __LINE__, 
                j++, i, tmp->weight, tmp->fft_info.font->line_height, tmp->fft_info.font->base_line, tmp->fft_info.style);
        }
        TY_GUI_LOG_PRINT("[%s][%d]--->dbg: total '%d' ttf font node under file '%s' \r\n", __FUNCTION__, __LINE__, j, tmpp->file_name);
        j = 0;
        i++;
    }
    TY_GUI_LOG_PRINT("[%s][%d]===>dbg: total '%d' ttf file \r\n", __FUNCTION__, __LINE__, i);
}
#endif

lv_font_t *gui_get_fft_font(char *node_name, char *font_name, LV_FT_FONT_STYLE font_style)
{
    /*
    tmpp->file_name格式为目录+文件名,类似:                /font/STHUPO.TTF
    node_name格式为字体名_字体大小,类似:                    STHUPO_14
    */
    lv_font_t *font = NULL;
    ttf_file_info_node_s *tmpp, *nn;
    ttf_font_info_node_s *tmp, *n;
    char *ptr = NULL;
    uint16_t font_weight = 0;        //字体大小
    char nname[64];

    if ((strlen(font_name)+1) > sizeof(nname)) {
        TY_GUI_LOG_PRINT("[%s][%d]: font name '%s' is too long > '%d' ???\r\n", __FUNCTION__, __LINE__, font_name, sizeof(nname));
        return font;
    }

    memset(nname, 0, sizeof(nname));
    strcpy(nname, font_name);
    ptr = strrchr(nname, '_');
    if (ptr == NULL) {
        TY_GUI_LOG_PRINT("[%s][%d]:unknown font name '%s' ???\r\n", __FUNCTION__, __LINE__, nname);
    }
    else {
        font_weight = atoi(ptr+1);
        nname[ptr-nname] = '\0';
        dl_list_for_each_safe(tmpp, nn, &ttfFileInfoHead, ttf_file_info_node_s, m_list) {
            if (strcasecmp(tmpp->font_name, nname) == 0) {
                dl_list_for_each_safe(tmp, n, &tmpp->ttfFontInfoHead, ttf_font_info_node_s, m_list) {
                    if (tmp->weight == font_weight && tmp->fft_info.style == font_style) {
                        font = tmp->fft_info.font;
                        break;
                    }
                }
            }
            if (font != NULL)
                break;
        }
    }

    if (font == NULL) {
        TY_GUI_LOG_PRINT("[%s][%d]:doesn't find node '%s' fft font '%s' style '%d', uses default font !\r\n", __FUNCTION__, __LINE__, node_name, font_name, font_style);
        font = (lv_font_t *)LV_FONT_DEFAULT;
    }
    else {
        TY_GUI_LOG_PRINT("[%s][%d]: find node '%s' fft font '%s' style '%d'!\r\n", __FUNCTION__, __LINE__, node_name, font_name, font_style);
    }
    return font;
}

OPERATE_RET gui_ttf_init(cJSON *ui_display_json)
{
    static bool power_start = true;
    OPERATE_RET ret = OPRT_COM_ERROR;
#ifdef TUYA_APP_USE_INTERNAL_FLASH_FS
    DIR *dp = NULL;
    struct dirent *entry;
#else
    TUYA_FILEINFO entry = NULL;
    TUYA_DIR dp = NULL;
    CONST CHAR_T *dir_name = NULL;
#endif
    //void *g_PingFang_map = NULL;
    //size_t g_PingFang_map_len = 0;
    //uint16_t g_weight = 0;
    struct dl_list displayFontInfoHead;
    ui_display_str_node_s *fontNameNode = NULL;
    //ui_display_str_node_s *tmp, *n;
    ttf_file_info_node_s *ttfFileNode = NULL;
    int i = 0;
    cJSON *current_element = NULL;
    char *ptr = NULL;
    char *ptr_tmp = NULL;
    unsigned char style = 0;

    if (ui_display_json == NULL) {
        TY_GUI_LOG_PRINT("[%s][%d] open ttf font json fail\r\n", __FUNCTION__, __LINE__);
        return ret;
    }

    //释放空间
    if (!power_start)   //第一次上电或复位,不用释放!
        ttf_file_info_release();

    dl_list_init(&displayFontInfoHead);
    dl_list_init(&ttfFileInfoHead);
    power_start = false;

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
            continue;
        }

        if (fontWeightItem != NULL) {
            style = (unsigned char)atoi(fontWeightItem->valuestring);
        }
        else
            style = 0;

        if (libItem->valuestring != NULL && !display_font_name_list_is_repeat(&displayFontInfoHead, libItem->valuestring, style)) {
            fontNameNode = (ui_display_str_node_s *)tkl_system_malloc(sizeof(ui_display_str_node_s));
            if(!fontNameNode){
                TY_GUI_LOG_PRINT("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
                goto _finish;
            }
            fontNameNode->font_name = (char *)tkl_system_malloc(strlen(libItem->valuestring) + 1);
            if(!fontNameNode->font_name){
                TY_GUI_LOG_PRINT("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
                goto _finish;
            }
            memset(fontNameNode->font_name, 0, strlen(libItem->valuestring) + 1);
            strcpy(fontNameNode->font_name, libItem->valuestring);
            fontNameNode->font_style = style;
            dl_list_add(&displayFontInfoHead, &fontNameNode->m_list);
        }
    }
    tkl_system_psram_free(libKey);
    tkl_system_psram_free(fontWeightKey);
    #undef res_max_len
#else
    cJSON_ArrayForEach(current_element, ui_display_json) {
        // 检查当前元素的类型
        cJSON *array_element = cJSON_GetArrayItem(current_element, 1);
        cJSON *array_style = cJSON_GetArrayItem(current_element, 2);

        if (array_style != NULL)
            style = (unsigned char)atoi(array_style->valuestring);
        else
            style = 0;
        if (array_element->valuestring != NULL && !display_font_name_list_is_repeat(&displayFontInfoHead, array_element->valuestring, style)) {
            fontNameNode = (ui_display_str_node_s *)tkl_system_malloc(sizeof(ui_display_str_node_s));
            if(!fontNameNode){
                TY_GUI_LOG_PRINT("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
                goto _finish;
            }
            fontNameNode->font_name = (char *)tkl_system_malloc(strlen(array_element->valuestring)+1);
            if(!fontNameNode->font_name){
                TY_GUI_LOG_PRINT("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
                goto _finish;
            }
            memset(fontNameNode->font_name, 0, strlen(array_element->valuestring)+1);
            strcpy(fontNameNode->font_name, array_element->valuestring);
            fontNameNode->font_style = style;
            dl_list_add(&displayFontInfoHead, &fontNameNode->m_list);
        }
    }
#endif

#ifdef TTY_FONT_DBG
    display_font_name_list_dump(&displayFontInfoHead);
#endif

#if LV_USE_FREETYPE
    ttf_font_freetype_reinit(&displayFontInfoHead);     //重新按照新的字体相关参数初始化lv_freetype_init
#endif

#ifdef TUYA_APP_USE_INTERNAL_FLASH_FS
    dp = opendir(tuya_app_gui_get_font_path());
#else
    ty_gui_dir_open(tuya_app_gui_get_font_path(), &dp);
#endif
    if (dp == NULL) {
        TY_GUI_LOG_PRINT("[%s][%d] open ttf font fail\r\n", __FUNCTION__, __LINE__);
        goto _finish;
    }

    //遍历目录
    i= 0;
#ifdef TUYA_APP_USE_INTERNAL_FLASH_FS
    while ((entry = readdir(dp))) {
        TY_GUI_LOG_PRINT("[%s][%d] find ttf font folder file '%s'\n", __FUNCTION__, __LINE__, entry->d_name);
        if (strstr(entry->d_name, ".ttf") != NULL || strstr(entry->d_name, ".TTF") != NULL) {
            ttfFileNode = (ttf_file_info_node_s *)tkl_system_malloc(sizeof(ttf_file_info_node_s));
            if(!ttfFileNode){
                TY_GUI_LOG_PRINT("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
                goto _finish;
            }
            memset(ttfFileNode, 0, sizeof(ttf_file_info_node_s));
            dl_list_init(&(ttfFileNode->ttfFontInfoHead));         //初始化当前文件font节点链表
            ttfFileNode->file_name = (char *)tkl_system_malloc(strlen(tuya_app_gui_get_font_path())+strlen(entry->d_name)+1);
            ttfFileNode->font_name = (char *)tkl_system_malloc(strlen(entry->d_name));
            if(!ttfFileNode->file_name || !ttfFileNode->font_name){
                TY_GUI_LOG_PRINT("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
                goto _finish;
            }
            memset(ttfFileNode->file_name, 0, strlen(tuya_app_gui_get_font_path())+strlen(entry->d_name)+1);
            memset(ttfFileNode->font_name, 0, strlen(entry->d_name));
            sprintf(ttfFileNode->file_name, "%s%s", tuya_app_gui_get_font_path(),entry->d_name);
            ptr = strrchr(entry->d_name, '.');
            entry->d_name[ptr-entry->d_name] = '\0';
            strcpy(ttfFileNode->font_name, entry->d_name);
            dl_list_add(&ttfFileInfoHead, &ttfFileNode->m_list);
            i++;
        }
    }

    closedir(dp);
#else
    INT_T dir_read_ret = 0;
    for (;;) {
        entry = NULL;
        dir_read_ret = ty_gui_dir_read(dp, &entry);
        if (entry == NULL || dir_read_ret != 0)
            break;
        ty_gui_dir_name(entry, &dir_name);
        if (strcmp(dir_name, ".") == 0 || strcmp(dir_name, "..") == 0) {
            continue;
        }
        TY_GUI_LOG_PRINT("[%s][%d] find ttf font folder file '%s'\n", __FUNCTION__, __LINE__, dir_name);
        if (strstr(dir_name, ".ttf") != NULL || strstr(dir_name, ".TTF") != NULL) {
            ttfFileNode = (ttf_file_info_node_s *)tkl_system_malloc(sizeof(ttf_file_info_node_s));
            if(!ttfFileNode){
                TY_GUI_LOG_PRINT("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
                goto _finish;
            }
            memset(ttfFileNode, 0, sizeof(ttf_file_info_node_s));
            dl_list_init(&(ttfFileNode->ttfFontInfoHead));         //初始化当前文件font节点链表
            ttfFileNode->file_name = (char *)tkl_system_malloc(strlen(tuya_app_gui_get_font_path())+strlen(dir_name)+1);
            ttfFileNode->font_name = (char *)tkl_system_malloc(strlen(dir_name)+1);
            if(!ttfFileNode->file_name || !ttfFileNode->font_name){
                TY_GUI_LOG_PRINT("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
                goto _finish;
            }
            memset(ttfFileNode->file_name, 0, strlen(tuya_app_gui_get_font_path())+strlen(dir_name)+1);
            memset(ttfFileNode->font_name, 0, strlen(dir_name)+1);
            sprintf(ttfFileNode->file_name, "%s%s", tuya_app_gui_get_font_path(),dir_name);
            strcpy(ttfFileNode->font_name, dir_name);
            ptr_tmp = ttfFileNode->font_name;
            ptr = strrchr(ptr_tmp, '.');
            ptr_tmp[ptr-ptr_tmp] = '\0';
            dl_list_add(&ttfFileInfoHead, &ttfFileNode->m_list);
            i++;
        }
    }
    ty_gui_dir_close(dp);
#endif
    if (i == 0) {
        TY_GUI_LOG_PRINT("[%s][%d] found no valid ttf font !\r\n", __FUNCTION__, __LINE__);
        goto _finish;
    }

    ret= ttf_font_info_fill(&displayFontInfoHead);
    TY_GUI_LOG_PRINT("----%s: all ttf font created %s\r\n", __func__, (ret==OPRT_OK)?"successfully":"fail");

    _finish:
    //releas resource:displayFontInfoHead
    display_font_name_list_delete(&displayFontInfoHead);
    if (ret != OPRT_OK)
        ttf_file_info_release();
    else {
    #ifdef TTY_FONT_DBG
        ttf_file_info_list_dump();
    #endif
    }

    return ret;
}
#endif
