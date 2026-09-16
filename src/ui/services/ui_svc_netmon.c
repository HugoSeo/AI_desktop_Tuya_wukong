#include "ui_svc_netmon.h"
#include <stdio.h>
#include "tuya_iot_config.h"   /* ENABLE_WIFI_SERVICE */

#if defined(ENABLE_WIFI_SERVICE) && (ENABLE_WIFI_SERVICE == 1)
#include "tal_wifi.h"
#include "tuya_devos_utils.h"  /* get_gw_ssid() */
#endif

/*
 * Network-monitor provider for the diagnostics "Network status" page. Thin
 * forwarders over TAL WiFi so the UI layer stays free of tal_* headers.
 * Non-WiFi builds compile the stub branch: not connected, "--" everywhere.
 */

#if defined(ENABLE_WIFI_SERVICE) && (ENABLE_WIFI_SERVICE == 1)

bool ui_svc_netmon_wifi_connected(void)
{
    WF_STATION_STAT_E status = WSS_IDLE;
    if (tal_wifi_station_get_status(&status) != OPRT_OK) {
        return false;
    }
    return (status == WSS_CONN_SUCCESS || status == WSS_GOT_IP);
}

const char *ui_svc_netmon_ssid(void)
{
    if (!ui_svc_netmon_wifi_connected()) {
        return "--";
    }
    const char *ssid = get_gw_ssid();
    return (ssid != NULL && ssid[0] != '\0') ? ssid : "--";
}

int8_t ui_svc_netmon_rssi(void)
{
    SCHAR_T rssi = 0;
    if (!ui_svc_netmon_wifi_connected() ||
        tal_wifi_station_get_conn_ap_rssi(&rssi) != OPRT_OK) {
        return 0;
    }
    return (int8_t)rssi;
}

const char *ui_svc_netmon_ip(void)
{
    static char s_ip[16] = "--";
    NW_IP_S ip = {0};
    if (!ui_svc_netmon_wifi_connected() ||
        tal_wifi_get_ip(WF_STATION, &ip) != OPRT_OK ||
        ip.nwipstr[0] == '\0') {
        return "--";
    }
    snprintf(s_ip, sizeof(s_ip), "%s", ip.nwipstr);
    return s_ip;
}

const char *ui_svc_netmon_mac(void)
{
    static char s_mac[18] = "--";
    NW_MAC_S mac = {0};
    if (tal_wifi_get_mac(WF_STATION, &mac) != OPRT_OK) {
        return "--";
    }
    snprintf(s_mac, sizeof(s_mac), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac.mac[0], mac.mac[1], mac.mac[2],
             mac.mac[3], mac.mac[4], mac.mac[5]);
    return s_mac;
}

#else /* !ENABLE_WIFI_SERVICE */

bool        ui_svc_netmon_wifi_connected(void) { return false; }
const char *ui_svc_netmon_ssid(void)           { return "--"; }
int8_t      ui_svc_netmon_rssi(void)           { return 0; }
const char *ui_svc_netmon_ip(void)             { return "--"; }
const char *ui_svc_netmon_mac(void)            { return "--"; }

#endif /* ENABLE_WIFI_SERVICE */
