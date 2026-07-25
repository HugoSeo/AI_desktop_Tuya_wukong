//！公共部分处理
#include "gui_common.h"
#include "tal_memory.h"
#include "tal_log.h"

#define GUI_LOG(...)        bk_printf(__VA_ARGS__)

STATIC INT_T s_gui_lang = TY_AI_DEFAULT_LANG;

//！ 关于gui公共部分处理
/* 
    1. 设备状态
    2. 聊天模式
    3. 表情查找
    4. 图片加载
    5. 电量图标
    6. 声音图标
    7. 网络图标
*/

//! status gif
LV_IMG_DECLARE(Initializing);
LV_IMG_DECLARE(Provisioning);
LV_IMG_DECLARE(Connecting);
LV_IMG_DECLARE(Listening);
LV_IMG_DECLARE(Uploading);
LV_IMG_DECLARE(Thinking);
LV_IMG_DECLARE(Speaking);
LV_IMG_DECLARE(Waiting);

/* AI_CHAT_SUB_MODE_E: hold(0), oneshot(1), wakeup(2), free(3); AI_DEVICE_MODE_E: translate(1), p2p(2) */
STATIC CONST gui_lang_desc_t gui_mode_desc[] = {
    {{"长按", "LongPress"}},
    {{"按键", "ShortPress"}},
    {{"唤醒", "Keyword"}},
    {{"随意", "Free"}},
    {{"翻译", "Translate"}},
    {{"视频", "Video"}},
    {{"MAX", "MAX"}},
};

STATIC CONST gui_lang_desc_t gui_stat_desc[] = {
    {{"配网中...",      "Provisioning"}},
    {{"初始化...",      "Initializing"}},
    {{"连接中...",      "Connecting"}},
    {{"待命",           "Standby"}},
    {{"聆听中...",      "Listening"}},
    {{"上传中...",      "Uploading"}},
    {{"思考中...",      "Thinking"}},
    {{"说话中...",      "Speaking"}},
};

typedef struct {
    gui_stat_t          index[GUI_STAT_MAX];
    lv_img_dsc_t       *gif[GUI_STAT_MAX];
    gui_lang_desc_t    *mode;
    gui_lang_desc_t    *stat;
} gui_status_desc_t;

STATIC CONST gui_status_desc_t s_gui_status_desc = {
    .gif   = {&Provisioning, &Initializing, &Connecting, &Waiting, &Listening, &Uploading, &Thinking, &Speaking},
    .index = {GUI_STAT_PROV, GUI_STAT_INIT, GUI_STAT_CONN, GUI_STAT_IDLE, GUI_STAT_LISTEN, GUI_STAT_UPLOAD, GUI_STAT_THINK, GUI_STAT_SPEAK},
    .stat  = gui_stat_desc,
    .mode  = gui_mode_desc,
};

CHAR_T *gui_mode_desc_get(UINT8_T mode)
{
    return s_gui_status_desc.mode[mode].text[s_gui_lang];
}

INT_T gui_status_desc_get(UINT8_T stat,  VOID **text, VOID **gif)
{
    INT_T i = 0;

    GUI_LOG("gui stat %d\r\n", stat);

    for (i = 0; i < GUI_STAT_MAX; i++) {
        if (stat == s_gui_status_desc.index[i]) {
            break;
        }
    }

    if (i == GUI_STAT_MAX) {
        return OPRT_NOT_FOUND;
    }
    
    if (text) {
        *text = s_gui_status_desc.stat[i].text[s_gui_lang];
    }

    if (gif) {
        *gif  = s_gui_status_desc.gif[i];
    }

    return OPRT_OK;
}


VOID gui_lang_set(UINT8_T lang)
{
    if (lang != s_gui_lang) {
        s_gui_lang = lang;
        GUI_LOG("gui lang set %d", lang);
    }
}


INT_T gui_lang_get(VOID)
{
    return s_gui_lang;
}


//! 加载图片到psram，必须加载
OPERATE_RET gui_img_load_psram(CHAR_T *filename, lv_img_dsc_t *img_dst)
{
    OPERATE_RET ret = OPRT_COM_ERROR;
    lv_fs_file_t file;
    lv_fs_res_t  res;

    res = lv_fs_open(&file, filename, LV_FS_MODE_RD);
    if (res != LV_FS_RES_OK) {
        LV_LOG_ERROR("Failed to open file: %s\n", filename);
        return ret;
    }

    lv_fs_seek(&file, 0, LV_FS_SEEK_END);
    uint32_t file_size;
    lv_fs_tell(&file, &file_size);
    lv_fs_seek(&file, 0, LV_FS_SEEK_SET);

    // 分配内存以存储文件内容
    UINT8_T *buffer = tal_psram_malloc(file_size);
    if (buffer == NULL) {
        LV_LOG_ERROR("Memory allocation failed\n");
        lv_fs_close(&file);
        return ret;
    }

    uint32_t bytes_read;
    res = lv_fs_read(&file, buffer, file_size, &bytes_read);
    if (res != LV_FS_RES_OK || bytes_read != file_size) {
        LV_LOG_ERROR("Failed to read file: %s\n", filename);
        tal_psram_free(buffer);
    } else {
        LV_LOG_WARN("gif file '%s' load successful !\r\n", filename);
        img_dst->data = buffer;
        img_dst->data_size = file_size;
        ret = OPRT_OK;
    }
    lv_fs_close(&file);
    
    return ret;
}

INT_T gui_emotion_find(CONST gui_emotion_t *emotion, UINT8_T count, CONST CHAR_T *desc)
{
    UINT8_T which = 0;

    INT_T i = 0;
    for (i = 0; i < count; i++) {
        if (0 == strcasecmp(emotion[i].desc, desc)) {
            GUI_LOG("find emotion %s\r\n", emotion[i].desc);
            which = i;
            break;
        }
    }

    return which;
}


CHAR_T *gui_battery_level_get(UINT8_T battery)
{
    GUI_LOG("battery_level %d\r\n", battery);

    if (battery >= 100) {
        return FONT_AWESOME_BATTERY_FULL;
    } else if (battery >= 70 && battery < 100) {
        return FONT_AWESOME_BATTERY_3;
    } else if (battery >= 40 && battery < 70) {
        return FONT_AWESOME_BATTERY_2;
    } else if (battery > 10 && battery < 40) {
        return FONT_AWESOME_BATTERY_1;
    }

    return FONT_AWESOME_BATTERY_EMPTY;
}

CHAR_T *gui_volum_level_get(UINT8_T volum)
{
    GUI_LOG("volum_level %d\r\n", volum);

    if (volum >= 70 && volum <= 100) {
        return FONT_AWESOME_VOLUME_HIGH;
    } else if (volum >= 40 && volum < 70) {
        return  FONT_AWESOME_VOLUME_MEDIUM;
    } else if (volum > 10 && volum < 40){
        return FONT_AWESOME_VOLUME_LOW;
    }

    return FONT_AWESOME_VOLUME_MUTE;
}

CHAR_T *gui_wifi_level_get(UINT8_T net)
{
    return net ? FONT_AWESOME_WIFI : FONT_AWESOME_WIFI_OFF;
}

typedef struct {
    UINT8_T     init;
    UINT8_T     start;
    UINT8_T     last_delay;
    uint16_t    max_chars;
    VOID        *obj;
    VOID        *priv_data;
    VOID        (*text_disp_cb)(VOID *obj, CHAR_T *text, INT_T pos, VOID *priv_data);
    SLIST_HEAD  head;
} gui_text_mgr_t;


gui_text_mgr_t *gui_text_disp_mgr_get(VOID)
{
    STATIC gui_text_mgr_t  mgr;

    return &mgr;
}


STATIC VOID gui_text_default_disp_cb(VOID *obj, CHAR_T *text, INT_T pos, VOID *priv_data)
{
    TAL_PR_DEBUG("text %s, ps %d", text, pos);
    if (pos) {
        lv_label_ins_text(obj, pos, text);
    } else {
        lv_label_set_text(obj, text);
    }
}

INT_T gui_text_disp_pop(gui_text_disp_t **text)
{
    gui_text_mgr_t *mgr = gui_text_disp_mgr_get();
    SLIST_HEAD *pos = mgr->head.next;
    if (pos) {
        // bk_printf("gui_text_disp_pop %08x\n", pos);
        tuya_slist_del(&mgr->head, pos);
       *text = (gui_text_disp_t *)pos;
        return OPRT_OK;
    }
 
    return OPRT_NOT_FOUND;
}

STATIC VOID gui_text_disp_show(gui_text_mgr_t *mgr, gui_text_disp_t *text)
{
    uint32_t offset = _lv_txt_get_encoded_length(text->data);
    uint32_t pos    = _lv_txt_get_encoded_length(lv_label_get_text(mgr->obj));

    // bk_printf("mgr->obj %08x, offset %d, pos %d,  txt %s\n", mgr->obj, offset, pos, text->data);
    if (mgr->max_chars && (mgr->last_delay || ((pos + offset) > mgr->max_chars))) {
        mgr->last_delay = false;
        pos  = 0;
    } 

    if (mgr->text_disp_cb) {
        mgr->text_disp_cb(mgr->obj, text->data, pos, mgr->priv_data); 
    }
}

//! TODO: 打断状态显示
STATIC VOID gui_text_disp_timer(struct _lv_timer_t * timer)
{
    gui_text_mgr_t  *mgr  = (lv_obj_t *)timer->user_data;
    gui_text_disp_t *text = NULL;

    if (!mgr->start) {
        lv_timer_del(timer);
        return;
    }

    //! TODO need more
    if (OPRT_OK != gui_text_disp_pop(&text)) {
        lv_timer_set_period(timer, 100);
        return;
    }

    //! TODO: 组合显示
    gui_text_disp_show(mgr, text);

    if (text->timeindex) {
        lv_timer_set_period(timer, text->timeindex);
    }

    tkl_system_psram_free(text);
}


INT_T gui_text_disp_push(UINT8_T *data, INT_T len)
{
    gui_text_mgr_t *mgr = gui_text_disp_mgr_get();

    // TODO: msg discard
    TAL_PR_DEBUG("xxxx1");
    gui_text_disp_t *text = tal_psram_malloc(sizeof(gui_text_disp_t));
    if (NULL == text) {
        return OPRT_MALLOC_FAILED;
    }
    TAL_PR_DEBUG("xxxx2");
    // uint16_t text_len =  data[0] << 8 | data[1];
    // if (text_len > sizeof(text->data) - 1) {
    //     bk_printf("text len has cut %d-%d=%d", text_len, sizeof(text->data) - 1, text_len - sizeof(text->data) - 1);
    //     text_len = sizeof(text->data) - 1;
    // }
    // text->timeindex = data[2] << 24 | data[3] << 16 | data[4] << 8 | data[5];
    // text->timeindex = 0;
    memcpy(text->data, &data, len);
    text->data[len] = 0;
    tuya_init_slist_node(&text->node);  
    tuya_slist_add_tail(&mgr->head, &text->node);
    TAL_PR_DEBUG("xxxx3");
    // bk_printf("gui_text_disp_push %08x, text %s, timeindex %d\n", text, text->data, text->timeindex);

    return OPRT_OK;
}

STATIC uint16_t gui_txet_disp_area_cacl(lv_obj_t *obj, uint16_t width, uint16_t high) 
{
    lv_font_t *font = lv_obj_get_style_text_font(obj, LV_PART_MAIN);

    uint32_t unicode_char = 0;
    if (!gui_lang_get()) {
        unicode_char = _lv_txt_encoded_next("中", &unicode_char); 
    } else {
        unicode_char = _lv_txt_encoded_next("A", &unicode_char); 
    }

    //！字体大小
    lv_coord_t font_width  = lv_font_get_glyph_width(font, unicode_char, 0);
    lv_coord_t font_height = lv_font_get_line_height(font);
    font_width = font_width ? font_width : font_height;

    //! 有效区域
    width -= (lv_obj_get_style_pad_left(obj, LV_PART_MAIN) + lv_obj_get_style_pad_right(obj, LV_PART_MAIN));
    high  -= (lv_obj_get_style_pad_top(obj, LV_PART_MAIN)  + lv_obj_get_style_pad_bottom(obj, LV_PART_MAIN));
    
    // 计算最大行数（考虑90%安全边界）
    uint16_t max_lines = (high * 0.9) / font_height;
    if (max_lines < 1) max_lines = 1;
    
    // 计算每行最大字符数
    uint16_t chars_per_line = (width * 0.9) / font_width;
    if(chars_per_line < 1) chars_per_line = 1;
    
    // 计算最大字符容量
    uint16_t max_chars = max_lines * chars_per_line;

    bk_printf("gui text width %d, high %d, font_width %d, font_height %d, max_lines %d, chars_per_line %d, max_chars %d\n", 
        width, high, font_width, font_height, max_lines, chars_per_line, max_chars);
    
    return max_chars;
}

INT_T gui_txet_disp_init(lv_obj_t *obj, VOID *priv_data, uint16_t width, uint16_t high) 
{
    gui_text_mgr_t *mgr = gui_text_disp_mgr_get();

    if (mgr->init) {
        return OPRT_OK;
    }

    mgr->start        = 0;
    mgr->last_delay   = 0;
    mgr->max_chars    = 0;
    mgr->text_disp_cb = gui_text_default_disp_cb;
    mgr->obj        = obj;
    mgr->priv_data  = priv_data;
    tuya_init_slist_node(&mgr->head);

    if (obj && width && high) {
        mgr->max_chars = gui_txet_disp_area_cacl(obj, width, high);
    }

    mgr->init = true;
    bk_printf("gui text disp init\n");

    return OPRT_OK;
}

INT_T gui_txet_disp_set_cb(VOID (*text_disp_cb)(VOID *obj, CHAR_T *text, INT_T pos, VOID *priv_data))
{
    gui_text_mgr_t *mgr = gui_text_disp_mgr_get();

    if (NULL == text_disp_cb) {
        return OPRT_INVALID_PARM;
    }

    mgr->text_disp_cb = text_disp_cb;

    return OPRT_OK;
}

INT_T gui_text_disp_set_windows(lv_obj_t *obj, VOID *priv_data, uint16_t width, uint16_t high)
{
    gui_text_mgr_t *mgr   = gui_text_disp_mgr_get();
    mgr->obj        = obj;
    mgr->priv_data  = priv_data;

    if (obj && width && high) {
        mgr->max_chars = gui_txet_disp_area_cacl(obj, width, high);
    }

    return OPRT_OK;
}


INT_T gui_text_disp_start(VOID)
{
    gui_text_mgr_t  *mgr  = gui_text_disp_mgr_get();
    gui_text_disp_t *text = NULL;

    bk_printf("gui text disp start\n");
    mgr->start      = true;
    //! TODO need more
    if (OPRT_OK != gui_text_disp_pop(&text)) {
        mgr->last_delay = true;
        bk_printf("gui text pop delay\n");
        lv_timer_create(gui_text_disp_timer, 100, mgr);
        return OPRT_NOT_FOUND;
    }

    mgr->last_delay = false;
    if (mgr->text_disp_cb) {
        mgr->text_disp_cb(mgr->obj, text->data, 0, mgr->priv_data);
    }
    lv_timer_create(gui_text_disp_timer, text->timeindex, mgr);
    tkl_system_psram_free(text);

    return OPRT_OK;
}

INT_T gui_text_disp_free(VOID)
{
    gui_text_mgr_t *mgr = gui_text_disp_mgr_get();

    SLIST_HEAD            *pos   = NULL;
    gui_text_disp_t       *text  = NULL;

    SLIST_FOR_EACH_ENTRY(text, gui_text_disp_t, pos, (&mgr->head), node) {
        tkl_system_psram_free(text);
    }

    tuya_init_slist_node(&mgr->head);

    return OPRT_OK;
}

INT_T gui_text_disp_stop(VOID)
{
    gui_text_mgr_t *mgr   = gui_text_disp_mgr_get();
    gui_text_disp_t *text = NULL;

    if (mgr->start) {
        mgr->start = false;
        mgr->last_delay = false;
        //! 输出所有后续
        bk_printf("gui text disp all\n");
        while (0 == mgr->max_chars && OPRT_OK == gui_text_disp_pop(&text)) {
            gui_text_disp_show(mgr, text);
            tkl_system_psram_free(text);
        }
        gui_text_disp_free();
        bk_printf("gui text disp stop\n");
    }

    return OPRT_OK;
}
