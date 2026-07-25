#include "tuya_gui_compatible.h"

static MUTEX_HANDLE s_tuya_gui_mutex;

void tuya_gui_mutex_init(void)
{
    //必须是递归锁
    tkl_mutex_create_init(&s_tuya_gui_mutex);
    //rtos_init_recursive_mutex(&s_tuya_gui_mutex);
}

void tuya_gui_mutex_lock(void)
{
    tkl_mutex_lock(s_tuya_gui_mutex);
    //rtos_lock_recursive_mutex(&s_tuya_gui_mutex);
}

void tuya_gui_mutex_unlock(void)
{
    tkl_mutex_unlock(s_tuya_gui_mutex);
    //rtos_unlock_recursive_mutex(&s_tuya_gui_mutex);
}

