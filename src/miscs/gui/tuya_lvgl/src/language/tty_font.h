#ifndef __GUI_TTY_FONT_H_
#define __GUI_TTY_FONT_H_

#include "lvgl.h"
#include "dl_list.h"
#include "_cJSON.h"
#include "tuya_cloud_types.h"
#include "tuya_app_gui_core_config.h"

#if defined(TUYA_MODULE_T5) && (TUYA_MODULE_T5 == 1)

#define ENABLE_TTF_STYLE_CONFIG             //支持字重(0:一般/1:斜体/2:加粗)设置

#if LVGL_VERSION_MAJOR >= 9
typedef struct {
    const char * name;  /* The name of the font file */
    const void * mem;   /* The pointer of the font file */
    size_t mem_size;    /* The size of the memory */
    lv_font_t * font;   /* point to lvgl font */
    uint16_t weight;    /* font size */
    uint16_t style;     /* font style */
} lv_ft_info_t;
#endif

typedef struct
{
    char *font_name;            //字体名
    struct dl_list      m_list;
} ui_display_font_name_s;

typedef struct
{
    uint16_t font_weight;       //字体大小;
    struct dl_list      m_list;
} ui_display_font_weight_s;

typedef struct
{
    char *font_name;
    unsigned char font_style;   //0:一般/1:斜体/2:加粗
    struct dl_list      m_list;
} ui_display_str_node_s;

typedef struct
{
    uint16_t weight;        //字体大小
    lv_ft_info_t fft_info;
    struct dl_list      m_list;
} ttf_font_info_node_s;

typedef struct
{
    char *file_name;                    //当前ttf文件名,带font名,带/font/目录, 如: "/font/montserratMedium.ttf"
    char *font_name;                    //从fft文件名中获取的font名,不带/font/目录及不带.ttf, 如:"montserratMedium"
    struct dl_list ttfFontInfoHead;     //当前ttf文件所有font链表头
    struct dl_list      m_list;
} ttf_file_info_node_s;

OPERATE_RET gui_ttf_init(cJSON *ui_display_json);
lv_font_t *gui_get_fft_font(char *node_name, char *font_name, LV_FT_FONT_STYLE font_style);

#endif
#endif

