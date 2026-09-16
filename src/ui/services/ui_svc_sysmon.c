#include "ui_svc_sysmon.h"
#include "tuya_iot_config.h"   /* ENABLE_EXT_RAM */
#include "tal_memory.h"
#include "tal_system.h"
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
#include "tkl_memory.h"        /* tkl_system_psram_get_free_heap_size */
#endif

/*
 * System-monitor provider for the diagnostics "System status" page. Thin
 * forwarders over TAL so the UI layer stays free of tal_* headers.
 */

int32_t ui_svc_sysmon_sram_free(void)
{
    return tal_system_get_free_heap_size();
}

int32_t ui_svc_sysmon_psram_free(void)
{
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    /* tal_system_psram_get_free_heap_size() is declared but not implemented in
     * this SDK revision (link error) — call the TKL layer directly. */
    return tkl_system_psram_get_free_heap_size();
#else
    return -1;
#endif
}

uint32_t ui_svc_sysmon_uptime_sec(void)
{
    return (uint32_t)(tal_system_get_millisecond() / 1000u);
}

/* Numeric code only — the describe out-param isn't reliably populated on this
 * platform, so callers show the TUYA_RESET_REASON_E value as-is (matches the
 * "system reset reason:[%d]" boot log for cross-referencing). */
int32_t ui_svc_sysmon_reset_reason(void)
{
    return (int32_t)tal_system_get_reset_reason(NULL);
}
