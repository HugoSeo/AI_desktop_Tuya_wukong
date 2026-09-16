#include "ui_port_perf.h"
#include "lvgl.h"

/* Top-right inset; y clears the global statusbar pill (lv_layer_top, height 32). */
#define PERF_OFFSET_X   (-6)
#define PERF_OFFSET_Y   (34)
#define PERF_UPDATE_MS  (1000)

static bool      s_enabled   = false;
static lv_obj_t *s_label     = NULL;   /* overlay label on lv_layer_top() */
static uint32_t  s_frames    = 0;      /* frames counted since last label update */
static uint32_t  s_last_tick = 0;      /* lv_tick base of the current 1s window */

/* Runs in the UI thread after every full refresh (full_refresh=1 -> 1 flush =
 * 1 frame). Count frames; once a second recompute FPS and CPU and repaint the
 * label. The text changes at most 1/s, so the invalidated area is tiny. */
void ui_port_perf_on_frame(struct _lv_disp_drv_t *drv, uint32_t time_ms, uint32_t px)
{
    (void)drv;
    (void)time_ms;
    (void)px;

    if (!s_enabled || s_label == NULL) {
        return;
    }

    s_frames++;

    uint32_t elaps = lv_tick_elaps(s_last_tick);
    if (elaps >= PERF_UPDATE_MS) {
        int fps = (int)((s_frames * 1000) / elaps);
        int cpu = 100 - (int)lv_timer_get_idle();   /* same metric as LVGL native monitor */
        lv_label_set_text_fmt(s_label, "%d FPS\n%d%% CPU", fps, cpu);
        s_frames    = 0;
        s_last_tick = lv_tick_get();
    }
}

void ui_port_perf_overlay_set(bool on)
{
    if (on == s_enabled) {
        return;
    }
    s_enabled = on;

    if (on) {
        if (s_label == NULL) {
            s_label = lv_label_create(lv_layer_top());
            lv_obj_set_style_bg_color(s_label, lv_color_black(), 0);
            lv_obj_set_style_bg_opa(s_label, LV_OPA_50, 0);
            lv_obj_set_style_text_color(s_label, lv_color_white(), 0);
            lv_obj_set_style_pad_all(s_label, 4, 0);
            lv_obj_set_style_radius(s_label, 4, 0);
            lv_obj_set_style_text_align(s_label, LV_TEXT_ALIGN_RIGHT, 0);
            lv_label_set_text(s_label, "-- FPS\n-- CPU");
            lv_obj_align(s_label, LV_ALIGN_TOP_RIGHT, PERF_OFFSET_X, PERF_OFFSET_Y);
        }
        lv_obj_move_foreground(s_label);
        s_frames    = 0;
        s_last_tick = lv_tick_get();
    } else if (s_label) {
        /* Called from the UI thread (settings event cb) — safe to delete now. */
        lv_obj_del(s_label);
        s_label = NULL;
    }
}

bool ui_port_perf_overlay_get(void)
{
    return s_enabled;
}
