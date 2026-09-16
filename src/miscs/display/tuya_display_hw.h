#ifndef __TUYA_DISPLAY_HW_H__
#define __TUYA_DISPLAY_HW_H__

#include "tuya_cloud_types.h"
#include "tal_display_service.h"
#include "tal_tp_service.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TUYA_DISP_ROTATE_0 = 0,
    TUYA_DISP_ROTATE_90,
    TUYA_DISP_ROTATE_180,
    TUYA_DISP_ROTATE_270,
} tuya_disp_rotation_e;

typedef struct {
    const char *lcd_name;
    uint16_t width;
    uint16_t height;
    tuya_disp_rotation_e rotation;
    const ty_display_cfg *lcd_cfg;
} tuya_display_lcd_cfg_t;

typedef struct {
    const ty_tp_device_cfg_t *tp_device;
    const ty_tp_usr_cfg_t *tp_cfg;
} tuya_display_tp_cfg_t;

typedef struct {
    tuya_display_lcd_cfg_t lcd;    /* primary panel (single-LCD boards use only this) */
    tuya_display_lcd_cfg_t lcd2;   /* optional 2nd panel; lcd_name==NULL => single LCD.
                                    * Used by dual-screen boards (EYES): the two panels are
                                    * stacked into ONE logical display of height
                                    * lcd.height + lcd2.height — lcd = top rows, lcd2 = bottom. */
    tuya_display_tp_cfg_t tp;
} tuya_display_hw_cfg_t;

/* Full display HW init: LCD panel(s) then touch panel. Equivalent to
 * tuya_display_hw_init_lcd() followed by tuya_display_hw_init_tp().
 * Both sub-steps are idempotent, so calling this after the split APIs is a no-op. */
OPERATE_RET tuya_display_hw_init(const tuya_display_hw_cfg_t *cfg);

/* Bring up only the LCD panel(s) — enough to render. Touch is NOT opened.
 * Split out so a board can show its boot splash right after the LCD is live and
 * open the (slow, ~1s) touch panel afterwards, overlapping it with the splash. */
OPERATE_RET tuya_display_hw_init_lcd(const tuya_display_hw_cfg_t *cfg);

/* Open the touch panel. Safe to call after tuya_display_hw_init_lcd(); idempotent. */
OPERATE_RET tuya_display_hw_init_tp(const tuya_display_hw_cfg_t *cfg);

OPERATE_RET tuya_display_hw_deinit(void);

TY_DISPLAY_HANDLE tuya_display_hw_get_handle(void);
uint16_t tuya_display_hw_get_width(void);
uint16_t tuya_display_hw_get_height(void);   /* logical height = sum of all panels */
tuya_disp_rotation_e tuya_display_hw_get_rotation(void);

/* Physical-panel enumeration for the flush layer.
 * Single-LCD boards return count 1 and region {handle, y_off 0, rows = height}.
 * Dual-LCD boards return 2, each panel mapping to a contiguous row band of the
 * full-frame buffer (panel 0 = top, panel 1 = bottom). The flush layer slices
 * the composed frame by these bands and sends each band to its own handle. */
uint8_t tuya_display_hw_get_lcd_count(void);
OPERATE_RET tuya_display_hw_get_lcd_region(uint8_t idx, TY_DISPLAY_HANDLE *handle,
                                           uint16_t *y_off, uint16_t *rows);

OPERATE_RET tuya_display_hw_backlight_open(void);
OPERATE_RET tuya_display_hw_backlight_close(void);
OPERATE_RET tuya_display_hw_backlight_set(uint8_t percent);

OPERATE_RET tuya_display_hw_tp_read(ty_tp_point_info_t *point);

#ifdef __cplusplus
}
#endif

#endif /* __TUYA_DISPLAY_HW_H__ */
