#include <stdio.h>
#include <string.h>
// #include <common/bk_include.h>
// #include <os/os.h>
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "lv_vendor.h"
// #include "driver/dma2d.h"

#include "tkl_display.h"
#include "tkl_thread.h"
#include "tkl_mutex.h"
#include "tkl_semaphore.h"
#include "tkl_system.h"
//#include "tal_log.h"
#include "tkl_memory.h"
#include "tuya_port_disp.h"

//static beken_thread_t g_disp_thread_handle;
static TKL_THREAD_HANDLE g_disp_thread_handle;
//static uint32_t g_init_stack_size = (1024 * 4);
// static beken_mutex_t g_disp_mutex;
static TKL_MUTEX_HANDLE g_disp_mutex;
//static beken_semaphore_t lvgl_sem;
static TKL_SEM_HANDLE lvgl_sem;
static uint8_t lvgl_task_state = STATE_INIT;
static bool lv_vendor_initialized = false;
lv_vnd_config_t vendor_config = {0};


#define LOGI LV_LOG_INFO
#define LOGW LV_LOG_WARN
#define LOGE LV_LOG_ERROR
#define LOGD LV_LOG_TRACE


void lv_vendor_disp_lock(void)
{
    //rtos_lock_mutex(&g_disp_mutex);
    tkl_mutex_lock(g_disp_mutex);
}

void lv_vendor_disp_unlock(void)
{
    //rtos_unlock_mutex(&g_disp_mutex);
    tkl_mutex_unlock(g_disp_mutex);
}

void lv_vendor_init(lv_vnd_config_t *config)
{
    OPERATE_RET ret;

    if (lv_vendor_initialized) {
        LOGI("%s already init\n", __func__);
        return;
    }

    //os_memcpy(&vendor_config, config, sizeof(lv_vnd_config_t));
    memcpy(&vendor_config, config, sizeof(lv_vnd_config_t));

    lv_init();

    ret = lv_port_disp_init();
    if (ret != OPRT_OK) {
        LOGE("%s lv_port_disp_init failed\n", __func__);
        return;
    }

    lv_port_indev_init();

    // ret = rtos_init_mutex(&g_disp_mutex);
    ret = tkl_mutex_create_init(&g_disp_mutex);
    if (OPRT_OK != ret) {
        LOGE("%s g_disp_mutex init failed\n", __func__);
        return;
    }

    //ret = rtos_init_semaphore_ex(&lvgl_sem, 1, 0);
    ret = tkl_semaphore_create_init(&lvgl_sem, 0, 1);
    if (OPRT_OK != ret) {
        LOGE("%s semaphore init failed\n", __func__);
        return;
    }

    lv_vendor_initialized = true;

    LOGI("%s complete\n", __func__);
}

void lv_vendor_deinit(void)
{
    if (lv_vendor_initialized == false) {
        LOGI("%s already deinit\n", __func__);
        return;
    }

    lv_port_disp_deinit();

    lv_port_indev_deinit();

    //rtos_deinit_mutex(&g_disp_mutex);
    tkl_mutex_release(g_disp_mutex);

    //rtos_deinit_semaphore(&lvgl_sem);
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

    LOGI("%s complete\n", __func__);
}

static void lv_tast_entry(void *arg)
{
    uint32_t sleep_time;

    lvgl_task_state = STATE_RUNNING;

    //rtos_set_semaphore(&lvgl_sem);
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

        //rtos_delay_milliseconds(sleep_time);
        tkl_system_sleep(sleep_time);
        // Modified by TUYA Start
        extern void tuya_app_gui_feed_watchdog(void);
        tuya_app_gui_feed_watchdog();
        // Modified by TUYA End
    }

    //rtos_set_semaphore(&lvgl_sem);
    tkl_semaphore_post(lvgl_sem);

    //rtos_delete_thread(NULL);
    tkl_thread_release(g_disp_thread_handle);
}

void lv_vendor_start(uint32_t lvgl_task_pri, uint32_t lvgl_stack_size)
{
    OPERATE_RET ret;

    if (lvgl_task_state == STATE_RUNNING) {
        LOGI("%s already start\n", __func__);
        return;
    }

    ret = tkl_thread_create(&g_disp_thread_handle,
                            "lvgl",
                            lvgl_stack_size,
                            lvgl_task_pri,
                            (THREAD_FUNC_T)lv_tast_entry,
                            0);
    if(OPRT_OK != ret) {
        LOGE("%s lvgl task create failed\n", __func__);
        return;
    }

    //ret = rtos_get_semaphore(&lvgl_sem, BEKEN_NEVER_TIMEOUT);
    ret = tkl_semaphore_wait(lvgl_sem, TKL_SEM_WAIT_FOREVER);
    if (OPRT_OK != ret) {
        LOGE("%s lvgl_sem get failed\n", __func__);
    }

    LOGI("%s complete\n", __func__);
}

void lv_vendor_stop(void)
{
    OPERATE_RET ret;

    if (lvgl_task_state == STATE_STOP) {
        LOGI("%s already stop\n", __func__);
        return;
    }

    lvgl_task_state = STATE_STOP;

    //ret = rtos_get_semaphore(&lvgl_sem, BEKEN_NEVER_TIMEOUT);
    ret = tkl_semaphore_wait(lvgl_sem, TKL_SEM_WAIT_FOREVER);
    if (OPRT_OK != ret) {
        LOGE("%s lvgl_sem get failed\n", __func__);
    }

    if (lv_vendor_display_frame_cnt() == 2 || lv_vendor_draw_buffer_cnt() == 2) {
        while (tkl_lvgl_dma2d_is_busy()) {}
    }

    LOGI("%s complete\n", __func__);
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
