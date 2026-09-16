#ifndef __UI_SVC_NETMON_H__
#define __UI_SVC_NETMON_H__

#include <stdbool.h>
#include <stdint.h>

/*
 * Network-monitor provider for the diagnostics "Network status" page.
 *
 * Keeps the UI layer free of business headers: pages include only this header
 * and read live network info through these getters. All getters are cheap
 * (direct TAL reads, no caching) and safe to call once per second. String
 * getters return a non-NULL, NUL-terminated string ("--" fallback) backed by
 * a per-getter static buffer — UI-thread use only, copy before storing.
 */

bool        ui_svc_netmon_wifi_connected(void); /* station associated + got IP */
const char *ui_svc_netmon_ssid(void);           /* connected AP SSID */
int8_t      ui_svc_netmon_rssi(void);           /* dBm; 0 = unavailable */
const char *ui_svc_netmon_ip(void);             /* station IPv4 address */
const char *ui_svc_netmon_mac(void);            /* station MAC, xx:xx:xx:xx:xx:xx */

#endif /* __UI_SVC_NETMON_H__ */
