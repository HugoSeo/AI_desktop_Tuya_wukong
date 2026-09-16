#include "ui_port_disp.h"
#include "ui_port_perf.h"
#include "tuya_app_config.h"
#include "lvgl.h"
#include "tal_display_service.h"
#include "tal_semaphore.h"
#include "tkl_memory.h"
#include <string.h>

static TY_DISPLAY_HANDLE s_disp_handle;
static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;
static lv_color_t *s_buf1;
static lv_color_t *s_buf2;

static ty_frame_buffer_t s_frame_buf[2];
static SEM_HANDLE s_vsync_sem;

static void frame_release_cb(UINT8_T *frame_buff)
{
    (void)frame_buff;
    tal_semaphore_post(s_vsync_sem);
}

/* See ui_port_disp_dma2d.c: weak no-op overridden by boot_splash's strong def on
 * boards that compile it; reclaims the retained boot-splash framebuffer. */
__attribute__((weak)) void tuya_boot_splash_free_canvas(void) {}
static uint8_t s_splash_reclaim_frames;

static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    if (s_disp_handle == NULL) {
        lv_disp_flush_ready(drv);
        return;
    }

    /* full_refresh: every call is one full frame (synchronous). By the 2nd
     * frame, frame 1 is on screen and the splash buffer is no longer scanned. */
    if (s_splash_reclaim_frames < 2 && ++s_splash_reclaim_frames == 2) {
        tuya_boot_splash_free_canvas();
    }

    int idx = (color_p == s_buf1) ? 0 : 1;

    s_frame_buf[idx].type = TYPE_PSRAM;
    s_frame_buf[idx].fmt = TY_PIXEL_FMT_RGB565;
    s_frame_buf[idx].x_start = 0;
    s_frame_buf[idx].y_start = 0;
    s_frame_buf[idx].width = drv->hor_res;
    s_frame_buf[idx].height = drv->ver_res;
    s_frame_buf[idx].len = (uint32_t)drv->hor_res * drv->ver_res * sizeof(lv_color_t);
    s_frame_buf[idx].frame = (uint8_t *)color_p;
    s_frame_buf[idx].free_cb = frame_release_cb;
    s_frame_buf[idx].pdata = NULL;

    tal_display_flush(s_disp_handle, &s_frame_buf[idx]);

    tal_semaphore_wait(s_vsync_sem, SEM_WAIT_FOREVER);

    lv_disp_flush_ready(drv);
}

void ui_port_disp_init(const ui_port_disp_cfg_t *cfg)
{
    s_disp_handle = (TY_DISPLAY_HANDLE)cfg->disp_handle;

    uint32_t buf_pixels = (uint32_t)cfg->hor_res * cfg->ver_res;

    s_buf1 = (lv_color_t *)tkl_system_psram_malloc(buf_pixels * sizeof(lv_color_t));
    s_buf2 = (lv_color_t *)tkl_system_psram_malloc(buf_pixels * sizeof(lv_color_t));
    if (s_buf1 == NULL || s_buf2 == NULL) {
        return;
    }
    memset(s_buf1, 0, buf_pixels * sizeof(lv_color_t));
    memset(s_buf2, 0, buf_pixels * sizeof(lv_color_t));

    tal_semaphore_create_init(&s_vsync_sem, 1, 1);

    memset(s_frame_buf, 0, sizeof(s_frame_buf));

    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2, buf_pixels);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = cfg->hor_res;
    s_disp_drv.ver_res = cfg->ver_res;
    s_disp_drv.flush_cb = disp_flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    s_disp_drv.full_refresh = 1;
    s_disp_drv.monitor_cb = ui_port_perf_on_frame;   /* runtime FPS/CPU overlay (no-op while disabled) */

    lv_disp_drv_register(&s_disp_drv);
}

void ui_port_disp_deinit(void)
{
    lv_disp_remove(lv_disp_get_default());

    if (s_buf1) {
        tkl_system_psram_free(s_buf1);
        s_buf1 = NULL;
    }
    if (s_buf2) {
        tkl_system_psram_free(s_buf2);
        s_buf2 = NULL;
    }
    if (s_vsync_sem) {
        tal_semaphore_release(s_vsync_sem);
        s_vsync_sem = NULL;
    }
    s_disp_handle = NULL;
}
