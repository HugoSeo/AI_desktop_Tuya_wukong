#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "lv_vendor.h"

#include "tuya_cloud_types.h"
#include "tkl_system.h"
#include "tkl_thread.h"
#include "tkl_mutex.h"
#include "tkl_semaphore.h"

#include "tuya_port_disp.h"

STATIC TKL_THREAD_HANDLE g_disp_thread_handle = NULL;
STATIC TKL_MUTEX_HANDLE g_disp_mutex = NULL;
STATIC TKL_SEM_HANDLE lvgl_sem = NULL;
STATIC UCHAR_T lvgl_task_state = STATE_INIT;
STATIC BOOL_T lv_vendor_initialized = false;
lv_vnd_config_t vendor_config = {0};

STATIC uint32_t lv_tick_get_callback(void)
{
    return (uint32_t)tkl_system_get_millisecond();
}

void lv_vendor_disp_lock(void)
{
    tkl_mutex_lock(g_disp_mutex);
}

void lv_vendor_disp_unlock(void)
{
    tkl_mutex_unlock(g_disp_mutex);
}

void lv_vendor_init(lv_vnd_config_t *config)
{
    if (lv_vendor_initialized) {
        LV_LOG_INFO("%s already init\n", __func__);
        return;
    }

    memcpy(&vendor_config, config, sizeof(lv_vnd_config_t));

    lv_init();

    lv_port_disp_init();

    lv_port_indev_init();

    lv_tick_set_cb(lv_tick_get_callback);

    if (OPRT_OK != tkl_mutex_create_init(&g_disp_mutex)) {
        LV_LOG_ERROR("%s g_disp_mutex init failed\n", __func__);
        return;
    }

    if (OPRT_OK != tkl_semaphore_create_init(&lvgl_sem, 0, 1)) {
        LV_LOG_ERROR("%s semaphore init failed\n", __func__);
        return;
    }

    lv_vendor_initialized = true;

    LV_LOG_INFO("%s complete\n", __func__);
}

void lv_vendor_deinit(void)
{
    if (lv_vendor_initialized == false) {
        LV_LOG_INFO("%s already deinit\n", __func__);
        return;
    }

#if LV_USE_LOG
    lv_log_register_print_cb(NULL);
#endif

    lv_port_disp_deinit();

    lv_port_indev_deinit();

    lv_tick_set_cb(NULL);

    lv_deinit();

    tkl_mutex_release(g_disp_mutex);

    tkl_semaphore_release(lvgl_sem);

    if (vendor_config.draw_buf_2_1 != NULL) {
        if (vendor_config.draw_buf_use_sram)
            tkl_system_sram_free(vendor_config.draw_buf_2_1_unalign);
        else
            tkl_system_psram_free(vendor_config.draw_buf_2_1_unalign);
    }
    if (vendor_config.draw_buf_2_2 != NULL) {
        if (vendor_config.draw_buf_use_sram)
            tkl_system_sram_free(vendor_config.draw_buf_2_2_unalign);
        else
            tkl_system_psram_free(vendor_config.draw_buf_2_2_unalign);
    }
    if (vendor_config.frame_buf_1 != NULL) {
        tkl_system_psram_free(vendor_config.frame_buf_1_unalign);
    }
    if (vendor_config.frame_buf_2 != NULL) {
        tkl_system_psram_free(vendor_config.frame_buf_2_unalign);
    }

    memset(&vendor_config, 0x00, sizeof(lv_vnd_config_t));

    lv_vendor_initialized = false;

    LV_LOG_INFO("%s complete\n", __func__);
}

STATIC VOID_T lv_tast_entry(VOID_T *arg)
{
    uint32_t sleep_time;

    lvgl_task_state = STATE_RUNNING;

    tkl_semaphore_post(lvgl_sem);

    while(lvgl_task_state == STATE_RUNNING) {
        lv_vendor_disp_lock();
        sleep_time = lv_task_handler();
        lv_vendor_disp_unlock();

        #if CONFIG_LVGL_TASK_SLEEP_TIME_CUSTOMIZE
            sleep_time = CONFIG_LVGL_TASK_SLEEP_TIME;
        #else
            if (sleep_time > 500) {
                sleep_time = 500;
            } else if (sleep_time < 4) {
                sleep_time = 4;
            }
        #endif

        tkl_system_sleep(sleep_time);
        // Modified by TUYA Start
        extern void tuya_app_gui_feed_watchdog(void);
        tuya_app_gui_feed_watchdog();
        // Modified by TUYA End
    }

    tkl_semaphore_post(lvgl_sem);

    tkl_thread_release(g_disp_thread_handle);
    g_disp_thread_handle = NULL;
}

void lv_vendor_start(uint32_t lvgl_task_pri, uint32_t lvgl_stack_size)
{
    if (lvgl_task_state == STATE_RUNNING) {
        LV_LOG_INFO("%s already start\n", __func__);
        return;
    }

#if defined(TUYA_CPU_ARCH_SMP) && defined(TUYA_MODULE_T5)
    if(OPRT_OK != tkl_thread_smp_create(&g_disp_thread_handle, 1, "lvgl", lvgl_stack_size, lvgl_task_pri/*最高优先级*/, lv_tast_entry, NULL)) {
        LV_LOG_ERROR("%s lvgl task create failed\n", __func__);
        return;
    }
#else
    if(OPRT_OK != tkl_thread_create(&g_disp_thread_handle, "lvgl", lvgl_stack_size, lvgl_task_pri/*最高优先级*/, lv_tast_entry, NULL)) {
        LV_LOG_ERROR("%s lvgl task create failed\n", __func__);
        return;
    }
#endif

    tkl_semaphore_wait(lvgl_sem, TKL_SEM_WAIT_FOREVER);

    LV_LOG_INFO("%s complete\n", __func__);
}

void lv_vendor_stop(void)
{
    if (lvgl_task_state == STATE_STOP) {
        LV_LOG_INFO("%s already stop\n", __func__);
        return;
    }

    lvgl_task_state = STATE_STOP;

    tkl_semaphore_wait(lvgl_sem, TKL_SEM_WAIT_FOREVER);

    if (lv_vendor_display_frame_cnt() == 2 || lv_vendor_draw_buffer_cnt() == 2) {
        while (tkl_lvgl_dma2d_is_busy()) {}
    }

    LV_LOG_INFO("%s complete\n", __func__);
}

int lv_vendor_display_frame_cnt(void)
{
    if (vendor_config.frame_buf_1 && vendor_config.frame_buf_2) {
        return 2;
    } else if (vendor_config.frame_buf_1 || vendor_config.frame_buf_2) {
        return 1;
    } else {
        return 0;
    }
}

int lv_vendor_draw_buffer_cnt(void)
{
    if (vendor_config.draw_buf_2_1 && vendor_config.draw_buf_2_2) {
        return 2;
    } else {
        return 1;
    }
}

// Modified by TUYA Start
void __attribute__((weak)) tuya_app_gui_feed_watchdog(void)
{

}

void __attribute__((weak)) lvMsgHandle(void)
{
}

void __attribute__((weak)) lvMsgEventReg(lv_obj_t *obj, lv_event_code_t eventCode)
{
}

void __attribute__((weak)) lvMsgEventDel(lv_obj_t *obj)
{
}
// Modified by TUYA End
