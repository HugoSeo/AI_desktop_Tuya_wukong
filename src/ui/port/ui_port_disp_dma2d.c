#include "ui_port_disp.h"
#include "ui_port_perf.h"
#include "tuya_app_config.h"
#include "lvgl.h"
#include "src/draw/sw/lv_draw_sw.h"
#include "tal_display_service.h"
#include "tuya_display_hw.h"
#include "tal_semaphore.h"
#include "tkl_dma2d.h"
#include "tkl_memory.h"
#include "tuya_dma2d.h"
#include <string.h>

/* Below this size DMA2D setup + IRQ latency costs more than a CPU copy */
#define UI_DMA2D_COPY_MIN_PIXELS 4096

/* Re-check interval while waiting for the display to release a buffer */
#define UI_FLUSH_WAIT_SLICE_MS   50

static TY_DISPLAY_HANDLE s_disp_handle;
static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;

static lv_color_t *s_frame_buf1;
static lv_color_t *s_frame_buf2;

static ty_frame_buffer_t s_frame_desc[2];
static SEM_HANDLE s_flush_sem;

/* Set when the buffer is handed to the display driver, cleared by its
 * free_cb. Depending on the panel a buffer is released either when its
 * own transfer completes (SPI) or only when the NEXT frame replaces it
 * (continuously scanned) — tracking per buffer works for both. */
static volatile uint8_t s_buf_busy[2];

static uint16_t s_hor_res;
static uint16_t s_ver_res;

#if defined(UI_LCD_RGB565_BYTE_SWAP) && UI_LCD_RGB565_BYTE_SWAP
/* SPI 8-bit-interface panels (e.g. T5AI_BOARD_DESKTOP / st7789v2) need the two
 * RGB565 bytes reversed before they go on the wire. Every pixel producer (LVGL,
 * camera YUV->RGB, album JPEG) composes NATIVE RGB565 into the frame buffers, so
 * we reverse the FULLY composed frame once here — into a dedicated scan buffer —
 * and flush that. The two LVGL frame buffers stay native, so direct-mode
 * inter-buffer sync (dma2d_buffer_copy) remains native<->native and consistent.
 * In this mode the flush is synchronous (single scan buffer; the slow SPI bus is
 * the bottleneck anyway, so losing the async pipeline costs nothing). */
static lv_color_t *s_swap_buf;
static ty_frame_buffer_t s_swap_desc;
static SEM_HANDLE s_swap_sem;

static void swap_frame_release_cb(UINT8_T *frame_buff)
{
    (void)frame_buff;
    tal_semaphore_post(s_swap_sem);
}

static inline void rgb565_byteswap(lv_color_t *dst, const lv_color_t *src, uint32_t px)
{
    const uint16_t *s = (const uint16_t *)src;
    uint16_t *d = (uint16_t *)dst;
    for (uint32_t i = 0; i < px; i++) {
        uint16_t v = s[i];
        d[i] = (uint16_t)((v >> 8) | (v << 8));
    }
}
#endif /* UI_LCD_RGB565_BYTE_SWAP */

/* Display driver released a buffer: it is writable again. Buffers are
 * released in submission order, so the freed one is the render target
 * LVGL's flushing flag is gating — open the gate and wake sleepers.
 * NOTE: the RGB driver passes the ty_frame_buffer_t* itself here
 * (tal_display_rgb.c __rgb_task: free_cb(frame)), not frame->frame —
 * match against both conventions. */
static void frame_release_cb(UINT8_T *frame_buff)
{
    if (frame_buff == (UINT8_T *)&s_frame_desc[0] || frame_buff == (UINT8_T *)s_frame_buf1) {
        s_buf_busy[0] = 0;
    } else if (frame_buff == (UINT8_T *)&s_frame_desc[1] || frame_buff == (UINT8_T *)s_frame_buf2) {
        s_buf_busy[1] = 0;
    }
    lv_disp_flush_ready(&s_disp_drv);
    tal_semaphore_post(s_flush_sem);
}

/* LVGL spins on this while draw_buf->flushing is set; the semaphore is only
 * a wakeup hint, the flag re-checked by the caller is the truth. */
static void disp_wait_cb(lv_disp_drv_t *drv)
{
    (void)drv;
    tal_semaphore_wait(s_flush_sem, UI_FLUSH_WAIT_SLICE_MS);
}

/* Rendering starts right after refr_sync_areas(): make sure no DMA2D
 * transfer is still writing the buffer the CPU is about to render into. */
static void disp_render_start_cb(lv_disp_drv_t *drv)
{
    (void)drv;
    tuya_dma2d_drain();
}

/* Dual-buffer sync for direct mode: LVGL calls this from refr_sync_areas()
 * to copy areas still valid on the on-screen buffer into the render buffer.
 * The copy runs synchronously through tuya_dma2d_memcpy (shared engine mutex) so it
 * never races the camera-thread conversion over the single engine. */
static void dma2d_buffer_copy(lv_draw_ctx_t *draw_ctx,
                              void *dest_buf, lv_coord_t dest_stride, const lv_area_t *dest_area,
                              void *src_buf, lv_coord_t src_stride, const lv_area_t *src_area)
{
    /* refr_sync_areas() runs before LVGL's own flushing guard in
     * refr_invalid_areas(), so with async flush_ready the destination
     * buffer may still be owned by the display here — wait it out. */
    int dest_idx = (dest_buf == (void *)s_frame_buf1) ? 0 : 1;
    while (s_buf_busy[dest_idx]) {
        tal_semaphore_wait(s_flush_sem, UI_FLUSH_WAIT_SLICE_MS);
    }

    uint32_t px = (uint32_t)lv_area_get_width(dest_area) * lv_area_get_height(dest_area);

    if (px >= UI_DMA2D_COPY_MIN_PIXELS) {
        TKL_DMA2D_FRAME_INFO_T in_frame = {0};
        TKL_DMA2D_FRAME_INFO_T out_frame = {0};

        in_frame.type = TUYA_FRAME_FMT_RGB565;
        in_frame.pbuf = (CHAR_T *)src_buf;
        in_frame.width = src_stride;
        in_frame.height = s_ver_res;
        in_frame.axis.x_axis = src_area->x1;
        in_frame.axis.y_axis = src_area->y1;
        in_frame.width_cp = lv_area_get_width(src_area);
        in_frame.height_cp = lv_area_get_height(src_area);

        out_frame.type = TUYA_FRAME_FMT_RGB565;
        out_frame.pbuf = (CHAR_T *)dest_buf;
        out_frame.width = dest_stride;
        out_frame.height = s_ver_res;
        out_frame.axis.x_axis = dest_area->x1;
        out_frame.axis.y_axis = dest_area->y1;

        if (tuya_dma2d_memcpy(&in_frame, &out_frame) == OPRT_OK) {
            return;
        }
    }

    /* CPU writes must not run while DMA2D writes the same buffer: adjacent
     * areas can share cache lines at their edges */
    tuya_dma2d_drain();
    lv_draw_sw_buffer_copy(draw_ctx, dest_buf, dest_stride, dest_area,
                           src_buf, src_stride, src_area);
}

/* Reclaim the boot-splash framebuffer once LVGL has taken over the scan.
 * boot_splash is only compiled on some boards (e.g. T5AI_BOARD); the weak
 * no-op is overridden by its strong definition there, and is a harmless
 * no-op elsewhere — so ui_port carries no hard dependency on boot_splash. */
__attribute__((weak)) void tuya_boot_splash_free_canvas(void) {}
static uint8_t s_splash_reclaim_frames;

static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    (void)area;

    /* Direct mode: LVGL renders dirty areas straight into the full-screen
     * buffer; only the last call of a refresh cycle submits the frame. */
    if (s_disp_handle == NULL || !lv_disp_flush_is_last(drv)) {
        lv_disp_flush_ready(drv);
        return;
    }

    /* By the 2nd committed frame, frame 1 is latched and the RGB controller
     * scans LVGL's own buffer — safe to free the retained boot-splash buffer. */
    if (s_splash_reclaim_frames < 2 && ++s_splash_reclaim_frames == 2) {
        tuya_boot_splash_free_canvas();
    }

    tuya_dma2d_drain();

#if defined(UI_LCD_RGB565_BYTE_SWAP) && UI_LCD_RGB565_BYTE_SWAP
    /* Byte-reverse the composed frame into the scan buffer, flush it synchronously
     * (wait the transfer out before reusing the single scan buffer / re-rendering). */
    rgb565_byteswap(s_swap_buf, color_p, (uint32_t)s_hor_res * s_ver_res);

    /* One logical frame may span several stacked panels (dual-LCD boards, e.g.
     * EYES = top half -> panel 0, bottom half -> panel 1). Slice the scan buffer
     * by each panel's row band and flush+wait per panel. Single-LCD boards report
     * count 1 with a full-height band, so this is unchanged for them. */
    uint8_t n = tuya_display_hw_get_lcd_count();
    for (uint8_t i = 0; i < n; i++) {
        TY_DISPLAY_HANDLE h = NULL;
        uint16_t y_off = 0, rows = s_ver_res;
        if (tuya_display_hw_get_lcd_region(i, &h, &y_off, &rows) != OPRT_OK || h == NULL) {
            continue;
        }
        s_swap_desc.frame   = (uint8_t *)s_swap_buf + (uint32_t)y_off * s_hor_res * sizeof(lv_color_t);
        s_swap_desc.x_start = 0;
        s_swap_desc.y_start = 0;     /* per-panel origin; GRAM offset added by the driver */
        s_swap_desc.width   = s_hor_res;
        s_swap_desc.height  = rows;
        s_swap_desc.len     = (uint32_t)s_hor_res * rows * sizeof(lv_color_t);
        s_swap_desc.free_cb = swap_frame_release_cb;
        if (tal_display_flush(h, &s_swap_desc) == OPRT_OK) {
            tal_semaphore_wait(s_swap_sem, SEM_WAIT_FOREVER);
        }
    }
    lv_disp_flush_ready(drv);
    return;
#else
    int idx = (color_p == s_frame_buf1) ? 0 : 1;

    s_frame_desc[idx].frame = (uint8_t *)color_p;
    s_frame_desc[idx].free_cb = frame_release_cb;

    s_buf_busy[idx] = 1;
    if (tal_display_flush(s_disp_handle, &s_frame_desc[idx]) != OPRT_OK) {
        /* driver rejected the frame: free_cb will never fire */
        s_buf_busy[idx] = 0;
        lv_disp_flush_ready(drv);
        return;
    }

    /* No blocking here. The flushing flag gates the OTHER buffer — the one
     * LVGL swaps to and renders next. If it is already free (first frame,
     * or its release already came in), clear the flag now; otherwise its
     * free_cb will. NOTE: do not map "this buffer's free_cb" to flush_ready —
     * panels that hold a buffer until the next frame replaces it would
     * deadlock (frame N+1 can't render before frame N's buffer is freed,
     * but freeing needs frame N+1 flushed). */
    if (!s_buf_busy[idx ^ 1]) {
        lv_disp_flush_ready(drv);
    }
#endif /* UI_LCD_RGB565_BYTE_SWAP */
}

void ui_port_disp_init(const ui_port_disp_cfg_t *cfg)
{
    s_disp_handle = (TY_DISPLAY_HANDLE)cfg->disp_handle;
    s_hor_res = cfg->hor_res;
    s_ver_res = cfg->ver_res;

    uint32_t buf_pixels = (uint32_t)cfg->hor_res * cfg->ver_res;
    uint32_t buf_bytes = buf_pixels * sizeof(lv_color_t);

    s_frame_buf1 = (lv_color_t *)tkl_system_psram_malloc(buf_bytes);
    s_frame_buf2 = (lv_color_t *)tkl_system_psram_malloc(buf_bytes);

    if (!s_frame_buf1 || !s_frame_buf2) {
        return;
    }

    memset(s_frame_buf1, 0, buf_bytes);
    memset(s_frame_buf2, 0, buf_bytes);

    /* DMA2D engine is owned by the shared module (also used by the camera
     * conversion); ref-counted, so init order vs. the camera does not matter. */
    tuya_dma2d_init();

    tal_semaphore_create_init(&s_flush_sem, 0, 1);
    s_buf_busy[0] = 0;
    s_buf_busy[1] = 0;

    memset(s_frame_desc, 0, sizeof(s_frame_desc));
    for (int i = 0; i < 2; i++) {
        s_frame_desc[i].type = TYPE_PSRAM;
        s_frame_desc[i].fmt = TY_PIXEL_FMT_RGB565;
        s_frame_desc[i].x_start = 0;
        s_frame_desc[i].y_start = 0;
        s_frame_desc[i].width = s_hor_res;
        s_frame_desc[i].height = s_ver_res;
        s_frame_desc[i].len = buf_bytes;
        s_frame_desc[i].pdata = NULL;
    }

#if defined(UI_LCD_RGB565_BYTE_SWAP) && UI_LCD_RGB565_BYTE_SWAP
    /* Dedicated scan buffer holding the byte-reversed frame sent to the panel. */
    s_swap_buf = (lv_color_t *)tkl_system_psram_malloc(buf_bytes);
    if (!s_swap_buf) {
        return;
    }
    tal_semaphore_create_init(&s_swap_sem, 0, 1);
    s_swap_desc = s_frame_desc[0];   /* same TYPE/fmt/geometry; .frame set per flush */
    s_swap_desc.frame = (uint8_t *)s_swap_buf;
#endif

    lv_disp_draw_buf_init(&s_draw_buf, s_frame_buf1, s_frame_buf2, buf_pixels);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = cfg->hor_res;
    s_disp_drv.ver_res = cfg->ver_res;
    s_disp_drv.flush_cb = disp_flush_cb;
    s_disp_drv.wait_cb = disp_wait_cb;
    s_disp_drv.render_start_cb = disp_render_start_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    s_disp_drv.direct_mode = 1;
    s_disp_drv.monitor_cb = ui_port_perf_on_frame;   /* runtime FPS/CPU overlay (no-op while disabled) */

    lv_disp_t *disp = lv_disp_drv_register(&s_disp_drv);
    if (disp && disp->driver->draw_ctx) {
        disp->driver->draw_ctx->buffer_copy = dma2d_buffer_copy;
    }
}

void ui_port_disp_deinit(void)
{
    /* Don't tear down while a frame is in flight or DMA2D is writing */
    while (s_draw_buf.flushing) {
        tal_semaphore_wait(s_flush_sem, UI_FLUSH_WAIT_SLICE_MS);
    }
    tuya_dma2d_drain();

    lv_disp_remove(lv_disp_get_default());

    if (s_frame_buf1) { tkl_system_psram_free(s_frame_buf1); s_frame_buf1 = NULL; }
    if (s_frame_buf2) { tkl_system_psram_free(s_frame_buf2); s_frame_buf2 = NULL; }
#if defined(UI_LCD_RGB565_BYTE_SWAP) && UI_LCD_RGB565_BYTE_SWAP
    if (s_swap_buf) { tkl_system_psram_free(s_swap_buf); s_swap_buf = NULL; }
    if (s_swap_sem) { tal_semaphore_release(s_swap_sem); s_swap_sem = NULL; }
#endif

    if (s_flush_sem) { tal_semaphore_release(s_flush_sem); s_flush_sem = NULL; }

    /* Drop our reference on the shared DMA2D engine (camera may still hold one). */
    tuya_dma2d_deinit();

    s_disp_handle = NULL;
}
