/**
 * @file gui_common.h
 * @author www.tuya.com
 * @version 0.1
 * @date 2025-02-07
 *
 * @copyright Copyright (c) tuya.inc 2025
 *
 */

 #ifndef __TUYA_GUI_COMMON__
 #define __TUYA_GUI_COMMON__

#include "tuya_device_cfg.h"
#include "tkl_lvgl.h"
#include "lvgl.h"
#include "font_awesome_symbols.h"
#include "tuya_slist.h"

#ifdef __cplusplus
extern "C" {
#endif 

#define GUI_SUPPORT_LANG_NUM        2

typedef struct {
    CHAR_T    *text[GUI_SUPPORT_LANG_NUM];
} gui_lang_desc_t;


typedef struct {
    SLIST_HEAD  node;
    UINT32_T    timeindex;
    CHAR_T        data[33 + 3];
} gui_text_disp_t;

typedef struct {
    CONST VOID  *source;
    CONST CHAR_T  *desc;
} gui_emotion_t;

typedef enum {
    GUI_STAT_INIT,
    GUI_STAT_IDLE,
    GUI_STAT_LISTEN,
    GUI_STAT_UPLOAD,
    GUI_STAT_THINK,
    GUI_STAT_SPEAK,
    GUI_STAT_PROV,
    GUI_STAT_CONN,
    GUI_STAT_MAX,
} gui_stat_t;


VOID  gui_lang_set(UINT8_T lang);
INT_T   gui_lang_get(VOID);
INT_T   gui_emotion_find(CONST gui_emotion_t *emotion, UINT8_T count, CONST CHAR_T *desc);
OPERATE_RET gui_img_load_psram(CHAR_T *filename, lv_img_dsc_t *img_dst);

/* gui_status_bar 
---------------------------
mode   [gif]stat   vol cell
---------------------------
*/
CHAR_T *gui_wifi_level_get(UINT8_T net);
CHAR_T *gui_volum_level_get(UINT8_T volum);
CHAR_T *gui_battery_level_get(UINT8_T battery);
CHAR_T *gui_mode_desc_get(UINT8_T mode);
INT_T   gui_status_desc_get(UINT8_T stat, VOID **text, VOID **gif);

INT_T gui_txet_disp_init(lv_obj_t *obj, VOID *priv_data, UINT16_T width, UINT16_T high);
INT_T gui_text_disp_start(VOID);
INT_T gui_text_disp_stop(VOID);
INT_T gui_text_disp_pop(gui_text_disp_t **text);
INT_T gui_text_disp_push(UINT8_T *data, INT_T len);
INT_T gui_text_disp_free(VOID);
INT_T gui_txet_disp_set_cb(VOID (*text_disp_cb)(VOID *obj, CHAR_T *text, INT_T pos, VOID *priv_data));
INT_T gui_text_disp_set_windows(lv_obj_t *obj, VOID *priv_data, UINT16_T width, UINT16_T high);


#ifdef __cplusplus
}
#endif

#endif /* __TUYA_GUI_COMMON__ */

