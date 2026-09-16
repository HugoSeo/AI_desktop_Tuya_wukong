#ifndef __TUYA_PORT_DISP_COMPAT_H__
#define __TUYA_PORT_DISP_COMPAT_H__

/**
 * @file tuya_port_disp.h  (compat shim for the new UI framework)
 *
 * The tuya-customized libjpeg-turbo (src/ui/vendor/libjpegturbo) hardcodes
 *   #include "tuya_port_disp.h"
 * but only to reach the SRAM allocators below — the original header lived in the
 * legacy GUI layer (src/miscs/gui/lvgl/src/common/tuya_port_disp.h), which the new
 * UI framework retires. Rather than patch the vendored library, this minimal
 * same-named header satisfies that include with just the three symbols it uses.
 * They are defined in the paired src/ui/port/compat/tuya_port_disp.c.
 */

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

VOID *tkl_system_sram_malloc(CONST SIZE_T size);
VOID *tkl_system_sram_calloc(size_t nitems, size_t size);
VOID  tkl_system_sram_free(VOID *ptr);

#ifdef __cplusplus
}
#endif

#endif /* __TUYA_PORT_DISP_COMPAT_H__ */
