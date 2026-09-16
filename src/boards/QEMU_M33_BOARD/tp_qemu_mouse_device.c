/**
 * @file tp_qemu_mouse_device.c
 * @brief Virtual touch panel device for QEMU_M33_BOARD.
 *
 * Backs the stock tal_tp_open()/tal_tp_read() path (src/drivers/app_tuya_tp/
 * tal_tp/src/tal_tp_service.c) with the platform's virtual mouse pointer
 * instead of a real I2C touch controller. No I2C is involved: init is a
 * no-op and read_pont_info() pulls the latest mouse sample from
 * qemu_disp_pointer_read() (platform layer, see
 * tuyaos_adapter/include/rgb/tkl_rgb_qemu.h in vendor/qemu-m33).
 *
 * Field semantics mirror src/drivers/app_tuya_tp/tdd_tp_driver/src/tp_gt1151.c
 * tp_gt1151_read_pont_info():
 *   - The caller (tal_tp_service.c's __tp_task) zeroes read_point->point_cnt
 *     and the point_arr buffer before every call, so "no touch" is expressed
 *     by simply leaving point_cnt at 0 — there is no explicit "released"
 *     point to emit.
 *   - On a touch, point_cnt is set to 1 (single point, id 0) and
 *     support_get_event is left FALSE, exactly like gt1151: tal_tp_service.c
 *     then derives PRESS_DOWN / CONTACT_MOVE / RELEASE_UP itself by diffing
 *     this call's point set against the previous one (cur_log vs hist_log),
 *     rather than trusting a tp_point_event value from the driver.
 *
 * @copyright Copyright (c) tuya.inc 2026
 *
 */
#include "tuya_cloud_types.h"
#include "tal_tp_service.h"

#if defined(__has_include)
#if __has_include("tkl_rgb_qemu.h")
#include "tkl_rgb_qemu.h"
#define TP_QEMU_HAVE_RGB_HEADER 1
#endif
#endif

#ifndef TP_QEMU_HAVE_RGB_HEADER
/* Platform header (tuyaos_adapter/include/rgb/tkl_rgb_qemu.h) not present at
 * compile time — e.g. building this board before the parallel platform-side
 * work lands. Extern-declare the contract so this file still compiles; drop
 * this fallback once the header ships. */
extern BOOL_T qemu_disp_pointer_read(UINT16_T *x, UINT16_T *y, BOOL_T *pressed);
extern VOID_T qemu_disp_bind_tp_intr_pin(INT_T pin);
#endif

/* No hardware to bring up (no I2C, no reset/interrupt GPIO handshake) — the
 * generic tuya_tp_driver_init() pin/i2c bring-up still runs against the QEMU
 * GPIO stubs (harmless). What this callback must do: hand the INT pin to the
 * platform shim. tal_tp_service's __tp_task blocks on a semaphore posted only
 * by the INT pin's GPIO irq callback, and the QEMU GPIO stub has no real
 * interrupt source — the platform's mouse RX thread soft-triggers this pin on
 * mouse activity instead (emulating a real TP asserting INT while touched).
 * By the time init runs, tuya_tp_driver_init() has already registered and
 * enabled the irq on this pin, so binding here is race-free. */
static void tp_qemu_mouse_init(struct ty_tp_device_cfg *cfg)
{
    if (cfg && cfg->tp_cfg) {
        qemu_disp_bind_tp_intr_pin((INT_T)cfg->tp_cfg->tp_intr.pin);
    }
}

static void tp_qemu_mouse_read_pont_info(ty_tp_read_data_t *read_point)
{
    if (!read_point || !read_point->point_arr) {
        return;
    }

    UINT16_T x = 0, y = 0;
    BOOL_T pressed = FALSE;

    if (!qemu_disp_pointer_read(&x, &y, &pressed) || !pressed) {
        /* No press: leave point_cnt at 0 (already zeroed by the caller),
         * same as gt1151's "no data" path. */
        return;
    }

    read_point->point_cnt = 1;
    read_point->support_get_event = FALSE;
    read_point->point_arr[0].tp_point_id = 0;
    read_point->point_arr[0].tp_point_x = x;
    read_point->point_arr[0].tp_point_y = y;
    read_point->point_arr[0].tp_point_event = TP_EVENT_UNKONW;
}

const ty_tp_device_cfg_t tp_qemu_mouse_device =
{
    .name = "qemu_mouse",
    .id = TP_ID_UNKNOW,
    .width = 320,
    .height = 480,
    .intr_type = TUYA_GPIO_IRQ_FALL,
    .mirror_type = TP_MIRROR_NONE,
    .refresh_rate = 30,
    .max_support_tp_num = 1,
    .init = tp_qemu_mouse_init,
    .read_pont_info = tp_qemu_mouse_read_pont_info,
};
