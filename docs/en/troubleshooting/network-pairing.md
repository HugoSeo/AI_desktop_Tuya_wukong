<!-- Auto-translated from ../../troubleshooting/network-pairing.md. Do not edit manually. -->
# Pairing / Activation Failure Troubleshooting

## Overview

This guide covers common failures during a wukong AI device's "pairing (netconfig)" and "activation" stages: the device won't enter pairing mode, the App can't find the device, activation fails after pairing, the device connects to the router but the cloud is unreachable, and re-pairing fails after unbinding/factory reset.

Related code:

- Key handling and quick-reset pairing: [`../../../src/tuya_ai_toy_key.c`](../../../src/tuya_ai_toy_key.c)
- Pairing-key long-press reset, Wi-Fi network status callback, LED indication: [`../../../src/tuya_ai_toy.c`](../../../src/tuya_ai_toy.c)
- Authorization info (PID/UUID/AUTHKEY), IoT callback registration, network and MQTT status events: [`../../../src/tuya_app_main.c`](../../../src/tuya_app_main.c)
- Pin Kconfig defaults: [`../../../src/boards/Kconfig`](../../../src/boards/Kconfig)
- First-boot activation wizard and on-screen Wi-Fi page on display boards: [`../../../src/ui/pages/ui_page_activation.c`](../../../src/ui/pages/ui_page_activation.c), [`../../../src/ui/pages/ui_page_wlan.c`](../../../src/ui/pages/ui_page_wlan.c)

Before troubleshooting: confirm the log level (`user_main()` already calls `tal_log_set_manage_attr(TAL_LOG_LEVEL_DEBUG)`, so DEBUG is the default level) and watch the device's live logs via a serial terminal or tyuTool.

## Quick Lookup

| You're seeing… | See section |
|---|---|
| Long-pressing the pairing key / repeatedly power-cycling, but the device never enters pairing mode | [1. Device Won't Enter Pairing Mode](#1-device-wont-enter-pairing-mode) |
| The device is in pairing mode, but the Tuya Smart App can't find it / can't add it | [2. App Can't Find the Device](#2-app-cant-find-the-device) |
| The App's pairing flow completes (Wi-Fi password entered), but it ultimately reports activation failure | [3. Pairing Succeeds but Activation Fails](#3-pairing-succeeds-but-activation-fails) |
| The device is connected to the router (has internet access), but stays offline and dialogue doesn't work | [4. Connected to the Router but Cloud MQTT Doesn't Connect](#4-connected-to-the-router-but-cloud-mqtt-doesnt-connect) |
| The device can't re-pair directly after being unbound in the App or factory-reset locally | [5. Re-pairing After Unbinding / Factory Reset](#5-re-pairing-after-unbinding--factory-reset) |

> **Display boards — read this first**: on boards with the new UI framework enabled (`CONFIG_ENABLE_TUYA_UI=y`), an unactivated device boots straight into the **first-boot activation wizard** (language → activation method → QR activation, implemented in `src/ui/pages/ui_page_activation.c`), which adds two pairing entries compared to screenless boards:
>
> - **App activation** (the wizard default): the device returns to the home page and waits for the App — AP+BLE concurrent pairing is already running in the background from boot, same as the App pairing flow on screenless boards, so the checks in sections 1/2 apply as-is;
> - **Manual activation (QR direct-connect)**: connect to the router on the on-screen WLAN page first (soft keyboard for the password), then a cloud activation short-link QR code is shown — scan it with the Tuya Smart App to finish activation. This path **does not rely on AP/BLE discovery**, so it is a fallback when the App can't find the device. If the QR code never appears, the WLAN link is usually down (see section 4) or the cloud short link isn't ready (check PID/license, see section 3).
>
> "Skip" on the wizard only dismisses the UI and does not stop the background pairing broadcast; QR activation is complete only on the `EVENT_POST_ACTIVATE` event — a connected MQTT session alone (`EVENT_MQTT_CONNECTED`) does not count.

---

## 1. Device Won't Enter Pairing Mode

### Troubleshooting Steps

1. **Check whether the current board has a dedicated "pairing key" GPIO configured.** `TUYA_AI_TOY_NET_PIN_NUM` is defined in [`../../../src/boards/Kconfig`](../../../src/boards/Kconfig), and its **default value is 64 (disabled)** — only `T5AI_BOARD_EVB` (12), `T5AI_BOARD_EVB_PRO` (7), and `T5AI_BOARD_ROBOT` (4) have a default pairing-key pin. The default board `T5AI_BOARD` (as well as `T5AI_BOARD_DESKTOP`/`T5AI_BOARD_EYES`/`T2AI_BOARD`/`L511_*`) has no pairing key at all — `tuya_ai_toy_key_init()` skips initialization outright when the pin equals `TUYA_GPIO_NUM_MAX` (see `tuya_ai_toy_key_init()` in [`../../../src/tuya_ai_toy_key.c`](../../../src/tuya_ai_toy_key.c)). On these boards, **it is expected that long-pressing a pairing key does nothing** — use the "repeated power-cycle reset" method covered next instead.
2. **Check whether the repeated power-cycle reset conditions are met.** The reset-pairing logic lives in [`../../../src/tuya_ai_toy_key.c`](../../../src/tuya_ai_toy_key.c): on every power-up, `__reset_netconfig_start()` increments a persisted counter (uFILE `rst_cnt`) and starts a one-shot 5-second timer that clears the counter after 5 seconds; only when there are **3 consecutive power-ups within 5 seconds** (`RESET_NETCNT_MAX`) does `__reset_netconfig_check()` trigger `tuya_iot_wf_gw_fast_unactive(GWCM_OLD, WF_START_SMART_AP_CONCURRENT)` on the next power-up, entering pairing mode. Log keywords:
   - `ai toy -> power up/down 3 times reset counter start` (printed on every power-up)
   - `reset cnt clear!` (5 seconds passed without reaching 3 power-ups, counter was cleared — the interval between power-cycles was too long)
   - `Reset ctrl data!` (3 power-ups reached, about to trigger pairing)
   If you only ever see `reset cnt clear!` repeating without `Reset ctrl data!`, it means each power-off/power-on interval exceeded 5 seconds — you need to power-cycle faster.
3. **Check whether Wi-Fi service was enabled at build time.** The `tuya_iot_wf_gw_fast_unactive()` call inside `__reset_netconfig_check()` that triggers pairing is wrapped in `#if defined(ENABLE_WIFI_SERVICE) && (ENABLE_WIFI_SERVICE == 1)`; if that macro isn't enabled (e.g. on a wired/cellular-only board), the reset-counter logic won't actually trigger Wi-Fi pairing, and you need to follow the connectivity-setup flow appropriate to that board's network type instead.
4. **Check whether pressing the pairing key itself produces any response.** For boards that do have a pairing key (EVB/EVB_PRO/ROBOT), the long-press threshold is `LONG_KEY_TIME * 10 = 4000ms` (`tuya_ai_toy_init()` in [`../../../src/tuya_ai_toy.c`](../../../src/tuya_ai_toy.c)); pressing it should print `ai toy -> net pin pressed <type>` in the log, and a long press (where `type` is `LONG_KEY`) should print `net pin long press trigger Reset!`. If you never see a `net pin pressed` log at all, check the GPIO wiring and electrical levels first (it's initialized as `TUYA_GPIO_PULLUP` with `low_detect=TRUE`, so a press should pull the line low).

### Resolution

- Boards without a pairing key: use the "3 power-ups within 5 seconds" method to enter pairing mode, and make sure the power-off interval is short.
- Boards with a pairing key: hold it down for more than 4 seconds and watch for `net pin pressed`/`long press trigger Reset` logs; no log at all means check the hardware wiring first, while a log with no resulting pairing mode means continue to section 2.
- If you need to enable/change the pairing-key pin on a custom board, change `TUYA_AI_TOY_NET_PIN_NUM` in the Pin Configuration menu of `make app_menuconfig`, then re-run `make app_config`.

---

## 2. App Can't Find the Device

### Troubleshooting Steps

1. **Confirm the device has actually entered pairing-broadcast mode (see section 1), rather than still being in a "paired/offline" state.** The network-status callback `__on_ai_toy_wf_nw_stat_cb()` ([`../../../src/tuya_ai_toy.c`](../../../src/tuya_ai_toy.c)) prints `network status = <nw_stat>, ...`; when entering pairing-broadcast mode, `nw_stat` should be `STAT_UNPROVISION_AP_STA_UNCFG` (smart+AP concurrent pairing in progress), at which point the LED blinks with a 200ms period (`tuya_ai_toy_led_flash(200)`). If it stays stuck at `STAT_LOW_POWER` or any other value, pairing was never actually triggered — go back to section 1.
2. **Confirm the app-side startup mode.** In `__soc_device_init()` ([`../../../src/tuya_app_main.c`](../../../src/tuya_app_main.c)), the device starts with `tuya_iot_wf_soc_dev_init(GWCM_OLD, WF_START_AP_FIRST, ...)`; `WF_START_AP_FIRST` means both AP and Smart pairing are supported, defaulting to AP-first. If the pairing method selected on the App side doesn't match what the device is currently broadcasting (e.g. the App tries Smart pairing while the device is in AP hotspot mode), neither side will find the other.
3. **Common Wi-Fi environment constraints (general pairing constraints, not verifiable from this repo's code, for reference only):**
   > **Note**: whether the router is on the 2.4GHz band, whether the SSID/password contains special characters or is too long, whether AP isolation is enabled, whether the phone and the device are on the same LAN — these are general prerequisites for Tuya pairing; follow the App's own prompts and the official pairing documentation for authoritative guidance, as this document does not verify them further.

### Resolution

- First confirm the LED is blinking in pairing mode and the log shows `STAT_UNPROVISION_AP_STA_UNCFG`, then open the App to add the device.
- If the device is in AP hotspot mode, choose "AP pairing"/"hotspot pairing" in the App and manually connect to the device's hotspot before configuring it.
- Confirm the phone is connected to a 2.4GHz Wi-Fi network with no special characters in the SSID/password, and try a different router if necessary.

---

## 3. Pairing Succeeds but Activation Fails

### Troubleshooting Steps (in order of likelihood)

1. **PID and authorization info (UUID/AUTHKEY) mismatch — the most common cause.** The demo project hardcodes a sample PID by default ([`../../../src/tuya_app_main.c`](../../../src/tuya_app_main.c) `#define PID "gcwfmdfkv6824tuh" // T5AI_BOARD_DESKTOP`); the authorization info shipped with the code is only meant for running the sample. A comment in the code explicitly warns that using the default authorization as-is can trigger "multi-user conflict" issues, and it must be replaced with a PID + authorization code applied for your own product on the Tuya IoT Developer Platform (two free sets are available, or you can purchase authorization — see the [Quick Start](../quickstart.md), "Step 1: Create a Product," "3.3 Change the PID and Authorization Info").
2. **Not using hardcoded authorization, relying instead on mass-production (MF) burned authorization — but MF wasn't burned, or the burned info doesn't match the PID.** When `UUID`/`AUTHKEY` aren't defined in the code, the device takes the `mf_init()` mass-production authorization branch (`__soc_device_init()` in [`../../../src/tuya_app_main.c`](../../../src/tuya_app_main.c)) — confirm the Tuya cloud module tool has correctly burned the authorization info matching this PID.
3. **The cloud activation flow itself fails or times out.** The activation state machine is implemented in the SDK component `svc_devos` (`tuya_svc_devos_activate.c`, not under this repo's path, so no link is provided). Common log keywords:
   - `activate fail` — the activation flow ran to completion but was judged a failure (`tuya_svc_devos_activate_finish(FALSE)`)
   - `linkage not ready` — the network link wasn't ready yet when activation was triggered; it backs off and retries automatically
   - `activate backoff` / `activate timeout` — retries with backoff up to `retry_cnt` (default 10) times; once retries are exhausted, an `EVENT_LINK_ACTIVATE` failure is reported
   - `result is null` — the cloud didn't return a valid activation result, usually related to a PID/authorization mismatch or a cloud-side issue (the parser requires the returned data to include `secKey`/`localKey`/`devId`, otherwise it's judged unparseable)
4. **Check whether the app-side callback received a clear failure indication.** `__soc_dev_status_changed_cb()` ([`../../../src/tuya_app_main.c`](../../../src/tuya_app_main.c)) only prints `SOC TUYA-Cloud Status:<status>`; this demo does no additional handling of failure states, so you need to pin down the exact stage using the log keywords above.

### Resolution

- First check and replace the PID + authorization info for your own product (the `PID` macro and `UUID`/`AUTHKEY` in `../../../src/tuya_app_main.c`, or the corresponding mass-production configuration) — this is currently the most common known cause of activation failure.
- If using mass-production burned authorization, verify that the PID burned by the mass-production tool exactly matches the PID the project was built with.
- If the log is stuck at `linkage not ready`/`activate backoff`, first check network connectivity per section 4 — the device retries automatically once connectivity is restored.
- If you persistently see `result is null`, contact the Tuya IoT Platform to verify that PID's product configuration (schema/PID status) is in order.

---

## 4. Connected to the Router but Cloud MQTT Doesn't Connect

### Troubleshooting Steps

1. **Distinguish the two independent states "link up" and "MQTT connected."** `__soc_dev_net_status_cb()` ([`../../../src/tuya_app_main.c`](../../../src/tuya_app_main.c)) fires on `EVENT_LINK_UP`/`EVENT_LINK_DOWN`/`EVENT_MQTT_CONNECTED`. Log keywords:
   - `linkage status changed, current status is up` — only means the network layer (IP obtained) is up
   - Only if `mqtt is connected!` follows right after has MQTT actually established a connection; if you only see the former without the latter, it's stuck at the MQTT connection stage.
2. **Watch the MQTT client's own state-transition logs.** The SDK component `svc_tuya_cloud` (`tuya_svc_mqtt_client.c`, not under this repo's path) prints `[<broker_domain>] mqtt state change <old> -> <new>` on MQTT state changes; repeatedly bouncing between a few states usually indicates the connection is being rejected or the handshake is failing. `mqtt_ping err`/`respond timeout` indicates the connection succeeded but the heartbeat is misbehaving (unstable network / intermittent firewall interference).
3. **Common root causes to check:**
   - Whether the device's system time is correct: there's a cloud time-sync step before the MQTT/TLS handshake (`mqc_app_get_cloud_time_sync()` in `mqc_app_time.c`); a badly skewed device RTC can cause certificate validation/handshake failures.
   - Whether DNS resolution is working: a failed MQTT connection clears the DNS cache and retries (`unw_clear_all_dns_cache()`); if the network environment has DNS issues or is forced through a custom DNS, this can trigger repeated reconnects.
   - Whether the network egress allows the domains/ports required by Tuya's cloud MQTT service:
     > **Note**: the exact domain list and port numbers (TLS/plaintext) depend on the current SDK and cloud environment; this document does not list specific values — follow the Tuya IoT Platform documentation or a support ticket for authoritative guidance.

### Resolution

- First confirm the log shows `linkage status changed, current status is up`; if that line is missing, the problem is at the network layer (router/DHCP), not MQTT.
- If you have "link up" but no "mqtt is connected," check the device's system time, DNS, and whether the network egress has a firewall/content filter blocking MQTT traffic.
- If the log shows `mqtt state change` bouncing repeatedly and never stabilizing, try a different network environment (e.g. a phone hotspot) as a comparison test, to rule out restrictions from the current router/LAN.

---

## 5. Re-pairing After Unbinding / Factory Reset

### Troubleshooting Steps

1. **Understand that "unbinding in the App" and "local/remote factory reset" are two different reset types, but lead to the same conclusion: both require going through the pairing flow again.** The reset types are defined in the SDK header `tuya_cloud_com_defs.h` (`GW_RESET_TYPE_E`, not under this repo's path): `GW_REMOTE_UNACTIVE` (App-side unbind), `GW_LOCAL_RESET_FACTORY`/`GW_REMOTE_RESET_FACTORY` (factory reset), `GW_RESET_DATA_FACTORY` (factory reset plus extra local data clearing). The difference between them is only in **how deep the local data clearing goes** (factory reset usually additionally clears locally saved historical data, while unbinding may not) — it is not about whether re-pairing is required. **Once the binding relationship is severed, the cloud must issue a new token via the App to re-establish the "user-device" binding; the device's old locally saved token is no longer valid** — so in both cases the device falls back to an unactivated state after reset, and the App must run through "add device"/pairing again.
2. **Confirm the device actually returns to an unpaired state after reset, rather than getting stuck in an intermediate state.** After the reset completes, you should see `nw_stat` fall back (see the `network status = ...` log in section 2), after which pairing can be re-triggered per section 1. If there's no change in the LED/log after reset, the reset callback may not have been triggered correctly.
3. **This demo currently does not distinguish between reset types.** `__soc_dev_reset_inform_cb()` ([`../../../src/tuya_app_main.c`](../../../src/tuya_app_main.c)) only prints `reset type <type>` on receiving `GW_RESET_TYPE_E type`, and force-syncs the cache once if KV caching is enabled — **it does not branch on `type` to apply different local data cleanup for "unbind" vs. "factory reset."** If your product needs to distinguish the scope of local data clearing between the two (e.g. only clearing history on factory reset), you need to extend this callback yourself to branch on `type`.

### Resolution

- It's expected behavior that the device cannot re-bind directly after unbinding or a factory reset: use the App to run through "add device" pairing again (back to sections 1 and 2), rather than trying to skip pairing and bind directly.
- If your product needs differentiated handling (e.g. "unbind keeps some local data, factory reset clears everything"), extend `__soc_dev_reset_inform_cb()` to branch on the specific `GW_RESET_TYPE_E` value — this demo does not implement that today.

---

## Related Documents

- [Quick Start](../quickstart.md): the full flow for creating a product, obtaining a PID and authorization info, and flashing firmware.
- [Architecture](../architecture.md): where key handling and quick-reset pairing sit in the overall initialization chain.

## Support

If you encounter issues during development, you can post on the TuyaOS Developer Forum [Connected Device Section](https://www.tuyaos.com/viewforum.php?f=11) for help.
