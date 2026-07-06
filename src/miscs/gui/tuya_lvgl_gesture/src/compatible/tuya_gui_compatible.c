#include "tuya_gui_compatible.h"
#include "tkl_system.h"
#include <string.h>
#include <stdio.h>

/***********************************************************
*************************micro define***********************
***********************************************************/

/***********************************************************
*************************variable define********************
***********************************************************/
STATIC MUTEX_HANDLE s_time_mutex = NULL;
STATIC SYS_TIME_T s_time_last_ms = 0;
STATIC TIME_T s_time_cloud_posix = 0;

/***********************************************************
*************************function define********************
***********************************************************/
STATIC SYS_TIME_T tal_system_get_millisecond(VOID_T)
{
    return  tkl_system_get_millisecond();
}

/**
 * @brief time-management module initialization
 * 
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h    
 */
OPERATE_RET __tal_time_service_init(VOID)
{
    if(s_time_mutex) {
        return OPRT_OK;
    }

    OPERATE_RET op_ret = OPRT_OK;
    op_ret = tuya_hal_mutex_create_init(&s_time_mutex);
    if(OPRT_OK != op_ret) {
        return op_ret;
    }
    
    s_time_cloud_posix = 0;
    s_time_last_ms = tal_system_get_millisecond();

    return OPRT_OK;
}

/**
 * @brief get IoTOS UTC time in SYS_TICK_T format
 * 
 * @return the current micro-second time in SYS_TICK_T format 
 */
SYS_TICK_T __tal_time_get_posix_ms(VOID)
{
    SYS_TIME_T tmp_cur_posix_time_ms = 0;

    tuya_hal_mutex_lock(s_time_mutex);
    SYS_TIME_T curr_time_ms = tal_system_get_millisecond();

    if (s_time_last_ms > curr_time_ms) { // recycle
        s_time_cloud_posix += ((0x100000000 - s_time_last_ms) / 1000);
        s_time_last_ms = 0;
    }

    tmp_cur_posix_time_ms  = (SYS_TICK_T)s_time_cloud_posix * 1000;
    tmp_cur_posix_time_ms += (curr_time_ms - s_time_last_ms);
    tuya_hal_mutex_unlock(s_time_mutex);

    return tmp_cur_posix_time_ms;
}

/**
 * @brief how long times system run
 * 
 * @param[out] pSecTime the current time in second
 * @param[out] pMsTime the current time in micro-second
 * @return VOID 
 */
VOID __tal_time_get_system_time(OUT TIME_S *pSecTime, OUT TIME_MS *pMsTime)
{
    STATIC SYS_TIME_T last_ms     = 0;
    STATIC SYS_TIME_T roll_ms = 0;
    SYS_TIME_T curr_ms = 0;

    tuya_hal_mutex_lock(s_time_mutex);

    curr_ms = tal_system_get_millisecond();
    if (last_ms > curr_ms) { // recycle 
        roll_ms += 0x100000000;
    }
    if (pMsTime) {
        *pMsTime  = (curr_ms + roll_ms) % 1000;
    }
    if (pSecTime) {
        *pSecTime = (curr_ms + roll_ms) / 1000;
    }
    last_ms = curr_ms;

    tuya_hal_mutex_unlock(s_time_mutex);
}

char *_mm_strdup(const char *str)
{
    if((void *)0 == str) {
        return (void *)0;
    }

    char *tmp = Malloc(strlen(str)+1);
    if((void *)0 == tmp) {
        return (void *)0;
    }
    memcpy(tmp, str, strlen(str));
    tmp[strlen(str)] = '\0';

    return tmp;
}
