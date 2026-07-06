#ifdef GUI_CPU_AFFINITY_ENABLE
#define _GNU_SOURCE
#include <sched.h>
#include <pthread.h>
#endif

#include "tuya_gui_api.h"
#include "tuya_gui_base.h"
#include "tuya_gui_navigate.h"
#include "tuya_gui_history.h"
#include "tuya_gui_nav_internal.h"
#include "tuya_gui_page_internal.h"
#include "tuya_gui_page.h"

OPERATE_RET tuya_gui_gesture_init()
{
    OPERATE_RET ret = OPRT_OK;

    __tal_time_service_init();
    gui_com_page_style_init();
    tuya_gui_navigate_start();
    tuya_gui_history_start();

    tuya_gui_mutex_init();

    return ret;
}