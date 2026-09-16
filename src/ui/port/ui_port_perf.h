#ifndef __UI_PORT_PERF_H__
#define __UI_PORT_PERF_H__

#include <stdbool.h>
#include <stdint.h>

/*
 * Runtime (memory-only) LVGL render-performance overlay.
 *
 * Shows a small "<n> FPS / <n>% CPU" label in the top-right corner of
 * lv_layer_top(), in the spirit of LVGL's native perf monitor — but toggleable
 * at runtime (the native LV_USE_PERF_MONITOR is compile-time only and patches
 * the sys layer, which RULES.md forbids).
 *
 * State is not persisted: the overlay defaults to OFF on every boot.
 *
 * All functions must be called from the UI thread (settings-page event callback
 * and the display monitor_cb both run inside lv_timer_handler).
 */

/* Forward decl so this header stays LVGL-free for the services layer. */
struct _lv_disp_drv_t;

/* Enable/disable the overlay. Creates the label + starts counting on enable;
 * deletes the label + stops updating on disable. */
void ui_port_perf_overlay_set(bool on);

/* Current overlay state (for the settings switch initial value). */
bool ui_port_perf_overlay_get(void);

/* Display monitor_cb hook — registered by ui_port_disp_init(). Fires once per
 * full refresh in the UI thread; counts frames and refreshes the label ~1/s.
 * Near-zero cost while the overlay is disabled. */
void ui_port_perf_on_frame(struct _lv_disp_drv_t *drv, uint32_t time_ms, uint32_t px);

#endif /* __UI_PORT_PERF_H__ */
