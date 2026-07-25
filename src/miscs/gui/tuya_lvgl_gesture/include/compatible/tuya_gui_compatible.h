#ifndef __TUYA_GUI_COMPATIBLE_H__
#define __TUYA_GUI_COMPATIBLE_H__

#include "tuya_cloud_types.h"
#include "tuya_slist.h"
#include "tkl_mutex.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern VOID_T* tkl_system_psram_malloc(CONST SIZE_T size);
extern VOID_T tkl_system_psram_free(VOID_T* ptr);

#ifndef Malloc
#define Malloc  tkl_system_psram_malloc
#endif
#ifndef Free
#define Free    tkl_system_psram_free
#endif

#ifndef PR_TRACE
#define PR_TRACE    LV_LOG_TRACE
#endif
#ifndef PR_DEBUG
#define PR_DEBUG    LV_LOG_USER
#endif
#ifndef PR_INFO
#define PR_INFO     LV_LOG_INFO
#endif
#ifndef PR_NOTICE
#define PR_NOTICE   LV_LOG_INFO
#endif
#ifndef PR_WARN
#define PR_WARN     LV_LOG_WARN
#endif
#ifndef PR_ERR
#define PR_ERR      LV_LOG_ERROR
#endif
#ifndef PR_USER
#define PR_USER     LV_LOG_USER
#endif

#ifndef TAL_PR_ERR
#define TAL_PR_ERR  LV_LOG_ERROR
#endif

#define LV_HOR_RES_MAX  LV_HOR_RES
#define LV_VER_RES_MAX  LV_VER_RES

//lock
typedef TKL_MUTEX_HANDLE MUTEX_HANDLE;

#define tuya_hal_mutex_create_init(handle)            \
        tkl_mutex_create_init(handle)
    
#define tuya_hal_mutex_lock(handle)                    \
        tkl_mutex_lock(handle)
    
#define tuya_hal_mutex_unlock(handle)                  \
        tkl_mutex_unlock(handle)
    
#define tuya_hal_mutex_release(handle)                 \
        tkl_mutex_release(handle)

#define tal_mutex_create_init   tuya_hal_mutex_create_init
#define tal_mutex_lock          tuya_hal_mutex_lock
#define tal_mutex_unlock        tuya_hal_mutex_unlock
//times
/**
 * @brief time-management module initialization
 * 
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h    
 */
OPERATE_RET __tal_time_service_init(VOID);

/**
 * @brief get IoTOS UTC time in SYS_TICK_T format
 * 
 * @return the current micro-second time in SYS_TICK_T format 
 */
SYS_TICK_T __tal_time_get_posix_ms(VOID);

/**
 * @brief how long times system run
 * 
 * @param[out] pSecTime the current time in second
 * @param[out] pMsTime the current time in micro-second
 * @return VOID 
 */
VOID __tal_time_get_system_time(OUT TIME_S *pSecTime, OUT TIME_MS *pMsTime);

#define uni_time_get_posix_ms() __tal_time_get_posix_ms()

/**
 * @brief  duplicate input string, will malloc a new block of memory
 * 
 * @param[in] str the input string need to duplicate
 * @return new string  
 */
char *_mm_strdup(const char *str);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __GUI_COMPATIBLE_H__

