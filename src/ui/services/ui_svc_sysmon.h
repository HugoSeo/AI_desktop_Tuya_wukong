#ifndef __UI_SVC_SYSMON_H__
#define __UI_SVC_SYSMON_H__

#include <stdint.h>

/*
 * System-monitor provider for the diagnostics "System status" page.
 *
 * Keeps the UI layer free of business headers: pages include only this header
 * and read live system stats through these getters. All getters are cheap
 * (direct TAL reads, no caching) and safe to call once per second.
 */

int32_t     ui_svc_sysmon_sram_free(void);    /* free SRAM heap, bytes */
int32_t     ui_svc_sysmon_psram_free(void);   /* free PSRAM heap, bytes; <0 = no PSRAM */
uint32_t    ui_svc_sysmon_uptime_sec(void);   /* seconds since boot */
int32_t     ui_svc_sysmon_reset_reason(void); /* last reset reason code (TUYA_RESET_REASON_E) */

#endif /* __UI_SVC_SYSMON_H__ */
