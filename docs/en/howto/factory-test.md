<!-- Auto-translated from ../../howto/factory-test.md. Do not edit manually. -->
# Whole-Product Factory Test

## Goal

Explains how to use the whole-product factory test during trial/mass production: on power-up the device automatically scans for the factory-test hotspot, **connects to it and joins the LAN**, and the Tuya factory-test host tool then drives the whole-unit test (key/LED/record-and-play test items, firmware upgrade). The entry point of the chain is `prodtest_ssid_scan()` (SDK `svc_mf_test` component); the app-side reference implementation is `ty_ai_toy_mf_test_init()` in `src/miscs/mftest/tuya_ai_toy_mf_test.c`.

```
Power-up → tuya_app_main.c
  ├─ ty_ai_toy_mf_test_init()      # prodtest_app_register() registers test SSIDs and callbacks
  └─ prodtest_ssid_scan(500)       # scans channel-6 beacons in a 500ms window
       ├─ factory-test SSID found (default ty_ai_mf_test)
       │     → connect to the hotspot → UDP device-discovery broadcast (host claims device by MAC/SN)
       │     → the "Finished Product Test - AI Product" host tool drives test items / firmware upgrade
       ├─ local self-check SSID found (default ty_ai_mf_test_1, a wukong extension for offline quick checks)
       │     → no connection: LED blinking (~1Hz) + prompt-tone loop for a quick speaker/mainboard check
       └─ nothing found / guards fail → returns false, normal business startup
```

Once triggered the test takes over: `tuya_app_main` returns directly and normal pairing/AI business never starts. SSID summary:

| Path | Default SSID | Match rule | Hotspot requirement |
|---|---|---|---|
| Factory test (standard flow) | `ty_ai_mf_test` (the SDK's original default `tuya_mdev_test4` is overridden by the app) | Exact | 2.4G, open (no password), connectable |
| Local self-check (optional wukong extension) | `ty_ai_mf_test_1` | Prefix collection in the SDK + exact filter in the app callback | Beacon visibility only |

## Prerequisites

- Build config: `ENABLE_PRODUCT_AUTOTEST=1` (preset by the SDK platform) and `CONFIG_ENABLE_AI_MF_TEST=y` (already on for T5AI_BOARD; check `build/appconfig/<board>` for others).
- **Device not paired and not activated**: any pairing record skips the scan; after the device has been activated and running for over 15 minutes, the KV close flag `MF_TEST_CLOSE_FLAG` is written and the factory-test entry is **permanently closed** (protects retail devices from same-named hotspots) — only a full erase + reflash restores it.
- Factory-test router: SSID `ty_ai_mf_test`, **2.4G, no password**, preferably pinned to **channel 6** (the device scan only looks at channel 6, see `PRODTEST_LISTEN_CHANNEL`).
- Host PC: the **Finished Product Test - AI Product** tool in Tuya's "Production Solution" suite (PMS account login), on the same LAN as the test router; for installation and operation see [Tuya Developer Platform: AI voice box factory test](https://developer.tuya.com/cn/docs/iot/T5-AI-01?id=Kekc0a3zoz6n0).
- The device should normally have its authorization written already; follow your production work-order / test-JSON configuration.

## Steps

### 1. Set up the production-line environment

Start the test router as specified above; install the test tool on the host, import the test JSON (or enter a work-order number), and confirm connection parameters such as the UDP port (5560) — see the public operation doc above.

### 2. Power up the device

Start the hotspot **before** powering the device (the scan window is only 500ms). Once the SSID is found the device connects to the hotspot and broadcasts device-discovery packets; when it appears in the host tool's device list, run the test per the tool's flow.

### 3. (Optional) Customize the test SSIDs

Two options:

| Method | What to change | Scope |
|---|---|---|
| API registration | Build a `prodtest_app_cfg_t` and call `prodtest_app_register()` (a later registration overrides an earlier one); the `prod_ssid` field is the factory-test SSID | Fully customizable: factory-test SSID, self-check SSID list, callbacks, `gwcm_mode` |
| Edit macros and rebuild | `PRODUCT_TEST_WIFI` (factory test) / `PRODUCT_TEST_WIFI_1` (local self-check) in `src/miscs/mftest/tuya_ai_toy_mf_test.c` | Both types |

Reference for API registration (this is what `ty_ai_toy_mf_test_init()` does):

```c
#include "prod_test.h"

static const char *my_selftest_list[] = { "my_selftest_ssid" };  /* local self-check list, optional */

prodtest_app_cfg_t cfg = {
    .gwcm_mode  = GWCM_OLD_PROD,              /* GWCM_OLD skips the scan entirely */
    .prod_ssid  = "my_mf_ssid",               /* factory-test SSID, exact match */
    .ssid_list  = my_selftest_list,
    .ssid_count = 1,
    .file_name  = APP_BIN_NAME,
    .file_ver   = USER_SW_VER,
    .app_cb     = my_ssid_info_cb,            /* callback once a self-check hotspot is found */
    .product_cb = my_mf_cmd_proc,             /* app-side extension handler for test commands */
};
prodtest_app_register(&cfg);
```

Registration must happen before `prodtest_ssid_scan()` (the two are adjacent in `tuya_app_main.c`; editing inside `ty_ai_toy_mf_test_init()` is enough).

## Verification

The serial log by stage:

| Log | Meaning |
|---|---|
| `ty_ai_toy_mf_test_init` | Test config registered |
| `prodtest_ssid_scan` (`ignored` = not triggered, normal startup) | Hotspot found, factory test entered |
| `prodtest_connect success` | Connected to the test hotspot |
| `Send Broadcast UDP Discover Package...` | Broadcasting device discovery, waiting for the host to claim it |

On the host side: the device appears in the device list (by MAC/SN) and test items report results one by one after Run.

Local self-check path (`ty_ai_mf_test_1`) whole-unit behavior: LED off = weak signal (RSSI < -60dBm; move closer and power-cycle); LED blinking (~1Hz) + prompt-tone loop = self-check in progress (up to 4 hours); LED solid on = finished.

To exit factory-test mode: remove the hotspot and power-cycle.

## Common Issues

**No scan activity at all on power-up (not even `prodtest_ssid_scan ignored`)**
`CONFIG_ENABLE_AI_MF_TEST` is off (the `ty_ai_toy_mf_test_init` log will be missing too), or the SDK's `ENABLE_PRODUCT_AUTOTEST` is not 1 — the whole block is compiled out.

**The log always says `prodtest_ssid_scan ignored`**
Check the guards in order:
1. **Already paired**: any pairing record skips the scan. Unbind or factory-reset to clear pairing info, then retry.
2. **Activated for over 15 minutes**: the log shows `have actived over 15 min, not enter mf_init`; the KV close flag has been written and this device's factory-test entry is permanently closed (retail-device protection) — only a full erase + reflash restores it; on the production line, use a non-activated device instead.
3. **Wrong hotspot channel**: the scan only looks at channel 6; pin the router to channel 6.
4. **Wrong timing**: the scan window is only 500ms; start the hotspot before powering the device.
5. **`gwcm_mode` set to `GWCM_OLD`**: that mode never scans (unless `prodtest_ignore_wcm()` is called first); the reference implementation uses `GWCM_OLD_PROD`.

**SSID found but connection fails (`prodtest_connect failed`)**
Confirm the hotspot is 2.4G, open (no password), with decent signal; on connection failure the device abandons the test and boots normally — power-cycle to retry.

**Connected to the hotspot but the host cannot discover the device**
The host PC must be on the same LAN as the test router; check the tool's UDP port setting (5560) and that the PC firewall allows UDP.

**The SSID matching rules are easy to confuse**
The factory-test `prod_ssid` is an **exact** match; the self-check `ssid_list` is a **prefix** match at the SDK collection stage (`strncmp` — hotspots sharing the prefix, e.g. `ty_ai_mf_test_10`, are also collected), and the wukong reference implementation then applies an **exact** filter in its callback before actually starting the self-check. Mind this two-layer difference when writing your own `app_cb`, and keep production-line SSIDs well isolated.

## Support

If you encounter issues during development, you can post on the TuyaOS Developer Forum [Connected Device Section](https://www.tuyaos.com/viewforum.php?f=11) for help.
