/**
 * @file tuya_port_disp.c  (compat shim for the new UI framework)
 *
 * Implements the three SRAM allocators declared in the paired tuya_port_disp.h.
 * These exist only to satisfy the vendored libjpeg-turbo (src/ui/vendor/libjpegturbo),
 * which hardcodes #include "tuya_port_disp.h". The names mirror the legacy GUI header;
 * on T5 there is no dedicated SRAM heap, so they forward to the regular allocators.
 */

#include "tuya_port_disp.h" /* paired compat header — let the compiler check decl/def match */
#include "tuya_cloud_types.h"
#include "tkl_memory.h"

VOID *tkl_system_sram_malloc(CONST SIZE_T size)
{
    return tkl_system_malloc(size);
}

VOID *tkl_system_sram_calloc(size_t nitems, size_t size)
{
    VOID *ptr = tkl_system_malloc(nitems * size);
    if (ptr) {
        memset(ptr, 0, nitems * size);
    }
    return ptr;
}

VOID tkl_system_sram_free(VOID *ptr)
{
    tkl_system_free(ptr);
}
