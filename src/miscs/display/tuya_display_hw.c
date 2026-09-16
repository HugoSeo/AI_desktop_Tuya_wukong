#include "tuya_display_hw.h"
#include "tdd_lcd.h"
#include <string.h>

static TY_DISPLAY_HANDLE s_lcd_handle = NULL;
static TY_DISPLAY_HANDLE s_lcd_handle2 = NULL;   /* 2nd panel of a dual-LCD board */
static TY_TP_HANDLE s_tp_handle = NULL;
static tuya_display_lcd_cfg_t s_lcd_cfg = {0};
static tuya_display_lcd_cfg_t s_lcd_cfg2 = {0};

/* Open one panel by name + GPIO cfg. Returns NULL on failure. */
static TY_DISPLAY_HANDLE display_open_lcd(const tuya_display_lcd_cfg_t *lcd)
{
    ty_display_device_s *device = NULL;
    for (UINT_T t = DISPLAY_RGB; t <= DISPLAY_SPI; t++) {
        device = (ty_display_device_s *)tdd_lcd_driver_query(lcd->lcd_name, t);
        if (device != NULL) break;
    }
    if (device == NULL) {
        return NULL;
    }
    return tal_display_open(device, (ty_display_cfg *)lcd->lcd_cfg);
}

OPERATE_RET tuya_display_hw_init_lcd(const tuya_display_hw_cfg_t *cfg)
{
    if (cfg == NULL) {
        return OPRT_INVALID_PARM;
    }

    if (s_lcd_handle == NULL && cfg->lcd.lcd_name != NULL) {
        s_lcd_cfg = cfg->lcd;

        s_lcd_handle = display_open_lcd(&cfg->lcd);
        if (s_lcd_handle == NULL) {
            return OPRT_COM_ERROR;
        }

        /* Optional 2nd panel (dual-screen boards, e.g. EYES). Stacked below the
         * first: the logical display is lcd.height + lcd2.height tall. */
        if (s_lcd_handle2 == NULL && cfg->lcd2.lcd_name != NULL) {
            s_lcd_cfg2 = cfg->lcd2;
            s_lcd_handle2 = display_open_lcd(&cfg->lcd2);
            if (s_lcd_handle2 == NULL) {
                return OPRT_COM_ERROR;
            }
        }
    }

    return OPRT_OK;
}

OPERATE_RET tuya_display_hw_init_tp(const tuya_display_hw_cfg_t *cfg)
{
    if (cfg == NULL) {
        return OPRT_INVALID_PARM;
    }

    if (s_tp_handle == NULL && cfg->tp.tp_device != NULL && cfg->tp.tp_cfg != NULL) {
        ty_tp_device_cfg_t tp_dev = *cfg->tp.tp_device;
        tp_dev.width = cfg->lcd.width;
        tp_dev.height = cfg->lcd.height;

        s_tp_handle = tal_tp_open(&tp_dev, (ty_tp_usr_cfg_t *)cfg->tp.tp_cfg);
    }

    return OPRT_OK;
}

OPERATE_RET tuya_display_hw_init(const tuya_display_hw_cfg_t *cfg)
{
    OPERATE_RET rt = tuya_display_hw_init_lcd(cfg);
    if (rt != OPRT_OK) {
        return rt;
    }
    return tuya_display_hw_init_tp(cfg);
}

OPERATE_RET tuya_display_hw_deinit(void)
{
    if (s_tp_handle != NULL) {
        tal_tp_close(s_tp_handle);
        s_tp_handle = NULL;
    }
    if (s_lcd_handle2 != NULL) {
        tal_display_close(s_lcd_handle2);
        s_lcd_handle2 = NULL;
    }
    if (s_lcd_handle != NULL) {
        tal_display_close(s_lcd_handle);
        s_lcd_handle = NULL;
    }
    memset(&s_lcd_cfg, 0, sizeof(s_lcd_cfg));
    memset(&s_lcd_cfg2, 0, sizeof(s_lcd_cfg2));
    return OPRT_OK;
}

TY_DISPLAY_HANDLE tuya_display_hw_get_handle(void)
{
    return s_lcd_handle;
}

uint16_t tuya_display_hw_get_width(void)
{
    return s_lcd_cfg.width;
}

uint16_t tuya_display_hw_get_height(void)
{
    /* Dual-LCD: panels are stacked, so the logical canvas is the sum of heights. */
    uint16_t h = s_lcd_cfg.height;
    if (s_lcd_handle2 != NULL) {
        h += s_lcd_cfg2.height;
    }
    return h;
}

uint8_t tuya_display_hw_get_lcd_count(void)
{
    return (s_lcd_handle2 != NULL) ? 2 : 1;
}

OPERATE_RET tuya_display_hw_get_lcd_region(uint8_t idx, TY_DISPLAY_HANDLE *handle,
                                           uint16_t *y_off, uint16_t *rows)
{
    if (idx == 0) {
        if (handle) *handle = s_lcd_handle;
        if (y_off)  *y_off  = 0;
        if (rows)   *rows   = s_lcd_cfg.height;
        return OPRT_OK;
    }
    if (idx == 1 && s_lcd_handle2 != NULL) {
        if (handle) *handle = s_lcd_handle2;
        if (y_off)  *y_off  = s_lcd_cfg.height;   /* below panel 0 */
        if (rows)   *rows   = s_lcd_cfg2.height;
        return OPRT_OK;
    }
    return OPRT_INVALID_PARM;
}

tuya_disp_rotation_e tuya_display_hw_get_rotation(void)
{
    return s_lcd_cfg.rotation;
}

OPERATE_RET tuya_display_hw_backlight_open(void)
{
    if (s_lcd_handle == NULL) {
        return OPRT_COM_ERROR;
    }
    return tal_display_bl_open(s_lcd_handle);
}

OPERATE_RET tuya_display_hw_backlight_close(void)
{
    if (s_lcd_handle == NULL) {
        return OPRT_COM_ERROR;
    }
    return tal_display_bl_close(s_lcd_handle);
}

OPERATE_RET tuya_display_hw_backlight_set(uint8_t percent)
{
    if (s_lcd_handle == NULL) {
        return OPRT_COM_ERROR;
    }
    uint8_t val = (percent > 100) ? 100 : percent;
    // return tal_display_bl_set(s_lcd_handle, val);
    return OPRT_OK; // Backlight control is handled by the board layer; ignore here.
}

OPERATE_RET tuya_display_hw_tp_read(ty_tp_point_info_t *point)
{
    if (s_tp_handle == NULL) {
        return OPRT_COM_ERROR;
    }
    return tal_tp_read(point);
}
