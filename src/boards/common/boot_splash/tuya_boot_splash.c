#include "tuya_boot_splash.h"
#include "tuya_display_hw.h"
#include "tal_display_service.h"
#include "ty_frame_buff.h"
#include "tal_log.h"
#include "tkl_memory.h"
#include <string.h>
#include "tal_thread.h"
#include "tal_semaphore.h"
#include "tef_player.h"

/* 见设计文档「刷新模型」:缓冲与描述符静态保留,free_cb=NULL,flush 不等待。
 * 字节交换:TEF 在调色板加载时按 TEF_RGB565_BYTE_SWAP 一次完成(local.mk 按板
 * 传递),画布内像素已是面板字节序,提交时无需也不能再整屏交换(脏块增量渲染,
 * 未变化像素会被二次交换)。当前 splash 板(RGB/QEMU)均无需交换。 */
static uint8_t *s_splash_buf;                 /* 全屏 RGB565,保留至 UI 接管,不 free */
static ty_frame_buffer_t s_splash_desc[2];    /* 每物理面板一个(双屏=2) */
static uint16_t s_w, s_h;                     /* 面板逻辑宽高 */

/* TEF 动画线程状态(仅 board init 单次启动) */
static THREAD_HANDLE  s_tef_thread;
static SEM_HANDLE     s_tef_done;    /* 线程播完整轮后 post;stop() 等它(保证动画完整播放) */
static const uint8_t *s_tef_data;
static uint32_t       s_tef_len;
static BOOL_T         s_backlight_on;

/* 分配并清黑全屏保留缓冲;取面板宽高。幂等(已分配则直接返回 OK)。 */
static OPERATE_RET splash_alloc_canvas(void)
{
    if (s_splash_buf != NULL) {
        return OPRT_OK;
    }
    if (tuya_display_hw_get_handle() == NULL) {
        TAL_PR_ERR("boot splash: no display handle");
        return OPRT_COM_ERROR;
    }
    s_w = tuya_display_hw_get_width();
    s_h = tuya_display_hw_get_height();
    if (s_w == 0 || s_h == 0) {
        TAL_PR_ERR("boot splash: bad resolution %u x %u", s_w, s_h);
        return OPRT_COM_ERROR;
    }
    uint32_t bytes = (uint32_t)s_w * s_h * 2;
    s_splash_buf = (uint8_t *)tkl_system_psram_malloc(bytes);
    if (s_splash_buf == NULL) {
        TAL_PR_ERR("boot splash: psram malloc %u failed", bytes);
        return OPRT_MALLOC_FAILED;
    }
    memset(s_splash_buf, 0, bytes);
    return OPRT_OK;
}

/* 把 s_splash_buf 逐面板 band 提交(异步,free_cb=NULL,不等待)。 */
static void splash_commit(void)
{
    uint8_t n = tuya_display_hw_get_lcd_count();
    if (n > 2) {
        TAL_PR_ERR("boot splash: %u panels exceed supported 2, extra dropped", n);
        n = 2;
    }
    for (uint8_t i = 0; i < n; i++) {
        TY_DISPLAY_HANDLE h_i = NULL;
        uint16_t y_off = 0, rows = s_h;
        if (tuya_display_hw_get_lcd_region(i, &h_i, &y_off, &rows) != OPRT_OK || h_i == NULL) {
            continue;
        }
        ty_frame_buffer_t *desc = &s_splash_desc[i];
        memset(desc, 0, sizeof(*desc));
        desc->type    = TYPE_PSRAM;
        desc->fmt     = TY_PIXEL_FMT_RGB565;
        desc->x_start = 0;
        desc->y_start = 0;
        desc->width   = s_w;
        desc->height  = rows;
        desc->len     = (uint32_t)s_w * rows * 2;
        desc->frame   = s_splash_buf + (uint32_t)y_off * s_w * 2;
        desc->free_cb = NULL;
        desc->pdata   = NULL;
        if (tal_display_flush(h_i, desc) != OPRT_OK) {
            TAL_PR_ERR("boot splash: flush panel %u failed", i);
        }
    }
}

/* 每帧上屏回调:整屏提交;首帧落屏后才开背光(避免上电白屏/花屏一闪)。 */
static void splash_tef_commit(void *ud)
{
    (void)ud;
    splash_commit();
    if (!s_backlight_on) {
        tuya_display_hw_backlight_open();
        s_backlight_on = TRUE;
    }
}

static void splash_tef_task(void *arg)
{
    (void)arg;
    /* 阻塞播完一遍(素材画布在整屏 canvas 内居中);不可中途打断:必须完整
     * 播完一轮(需求),末帧定格于 s_splash_buf(保留)。 */
    OPERATE_RET rt = tef_player_play_once(s_tef_data, s_tef_len,
                                          (uint16_t *)s_splash_buf, s_w, s_h, s_w,
                                          splash_tef_commit, NULL);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("boot splash: tef play failed %d", rt);
    }
    if (s_tef_done) {
        tal_semaphore_post(s_tef_done);
    }
    THREAD_HANDLE self = s_tef_thread;
    s_tef_thread = NULL;
    tal_thread_delete(self);
}

OPERATE_RET tuya_boot_splash_show_tef(const uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0) {
        return OPRT_INVALID_PARM;
    }
    OPERATE_RET rt = splash_alloc_canvas();
    if (rt != OPRT_OK) {
        return rt;
    }
    s_tef_data = data;
    s_tef_len = len;

    rt = tal_semaphore_create_init(&s_tef_done, 0, 1);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("boot splash: tef sem init failed %d", rt);
        goto fail_canvas;
    }
    /* 栈深与 tef_player 解码线程对齐(流式解压路径同源) */
    THREAD_CFG_T cfg = {8192, THREAD_PRIO_0, "boot_splash"};
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    cfg.psram_mode = 1;
#endif
    rt = tal_thread_create_and_start(&s_tef_thread, NULL, NULL, splash_tef_task, NULL, &cfg);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("boot splash: tef thread start failed %d", rt);
        tal_semaphore_release(s_tef_done);
        s_tef_done = NULL;
        goto fail_canvas;
    }
    TAL_PR_NOTICE("boot splash tef started (canvas %u x %u)", s_w, s_h);
    return OPRT_OK;

fail_canvas:
    tkl_system_psram_free(s_splash_buf);
    s_splash_buf = NULL;
    return rt;
}

void tuya_boot_splash_stop(void)
{
    if (s_tef_done == NULL) {
        return;     /* 未启动动画线程或已等过 */
    }
    /* 阻塞等动画完整播完一轮(不打断):线程走完后 post s_tef_done。
     * 即 UI 必须等开机动画播放完整才能接管。单轮时长有界,不会无限等待。 */
    tal_semaphore_wait(s_tef_done, SEM_WAIT_FOREVER);
    tal_semaphore_release(s_tef_done);
    s_tef_done = NULL;
}

void tuya_boot_splash_free_canvas(void)
{
    /* 仅在 LVGL 接管扫描后由 ui_port 调用,此时 RGB 控制器已切到 LVGL 自己的 framebuffer,
     * s_splash_buf 不再被扫描,可安全释放。幂等。 */
    if (s_splash_buf != NULL) {
        tkl_system_psram_free(s_splash_buf);
        s_splash_buf = NULL;
    }
}
