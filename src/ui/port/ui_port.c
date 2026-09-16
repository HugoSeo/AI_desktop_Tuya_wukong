#include "tuya_app_config.h"   /* UI_TASK_STACK_SIZE */
#include "ui_port.h"
#include "ui_port_disp.h"
#include "ui_port_indev.h"
#include "lvgl.h"
#include "tal_thread.h"
#include "tal_mutex.h"
#include "tal_semaphore.h"
#include "tal_system.h"

#define UI_LOOP_SLEEP_MIN_MS  4
#define UI_LOOP_SLEEP_MAX_MS  500

/* 栈大小由 src/ui/Kconfig 配置；appconfig 未含该行时走此 fallback */
#ifndef UI_TASK_STACK_SIZE
#define UI_TASK_STACK_SIZE    4096
#endif

#define UI_DEFAULT_PRIORITY   THREAD_PRIO_1

static THREAD_HANDLE s_thread;
static MUTEX_HANDLE  s_mutex;
static SEM_HANDLE    s_start_sem;
static volatile BOOL_T s_running = FALSE;

static void ui_loop_entry(PVOID_T arg)
{
    (void)arg;
    s_running = TRUE;
    tal_semaphore_post(s_start_sem);

    while (s_running) {
        uint32_t sleep_ms;

        tal_mutex_lock(s_mutex);
        sleep_ms = lv_timer_handler();
        tal_mutex_unlock(s_mutex);

        if (sleep_ms > UI_LOOP_SLEEP_MAX_MS) {
            sleep_ms = UI_LOOP_SLEEP_MAX_MS;
        } else if (sleep_ms < UI_LOOP_SLEEP_MIN_MS) {
            sleep_ms = UI_LOOP_SLEEP_MIN_MS;
        }

        tal_system_sleep(sleep_ms);
    }

    tal_semaphore_post(s_start_sem);
}

void ui_port_init(const ui_port_cfg_t *cfg)
{
    tal_mutex_create_init(&s_mutex);
    tal_semaphore_create_init(&s_start_sem, 0, 1);

    lv_init();

    ui_port_disp_cfg_t disp_cfg = {
        .disp_handle = cfg->disp_handle,
        .hor_res = cfg->hor_res,
        .ver_res = cfg->ver_res,
    };
    ui_port_disp_init(&disp_cfg);

    ui_port_indev_cfg_t indev_cfg = {
        .hor_res = cfg->hor_res,
        .ver_res = cfg->ver_res,
    };
    ui_port_indev_init(&indev_cfg);
}

void ui_port_start(void)
{
    if (s_running) return;

    THREAD_CFG_T cfg = {
        .stackDepth = UI_TASK_STACK_SIZE,
        .priority = UI_DEFAULT_PRIORITY,
        .thrdname = "ui_loop",
    };
    tal_thread_create_and_start(&s_thread, NULL, NULL, ui_loop_entry, NULL, &cfg);
    tal_semaphore_wait(s_start_sem, SEM_WAIT_FOREVER);
}

void ui_port_stop(void)
{
    if (!s_running) return;
    s_running = FALSE;
    tal_semaphore_wait(s_start_sem, SEM_WAIT_FOREVER);
    tal_thread_delete(s_thread);
    s_thread = NULL;
}

void ui_port_deinit(void)
{
    ui_port_stop();
    ui_port_disp_deinit();

    if (s_start_sem) {
        tal_semaphore_release(s_start_sem);
        s_start_sem = NULL;
    }
    if (s_mutex) {
        tal_mutex_release(s_mutex);
        s_mutex = NULL;
    }
}

void ui_port_lock(void)
{
    tal_mutex_lock(s_mutex);
}

void ui_port_unlock(void)
{
    tal_mutex_unlock(s_mutex);
}
