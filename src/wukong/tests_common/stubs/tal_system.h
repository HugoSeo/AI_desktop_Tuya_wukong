#ifndef __TAL_SYSTEM_H__
#define __TAL_SYSTEM_H__

#include <stdint.h>
#include "tuya_cloud_types.h"

/* Host-test stub for the SDK's tal_system.h — only the symbols wukong host
 * tests actually use. Impl provided per-suite. */
typedef uint64_t SYS_TICK_T;

SYS_TIME_T tal_system_get_millisecond(VOID_T);

#endif
