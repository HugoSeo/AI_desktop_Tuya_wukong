#include "ui_svc_dev_ctrl.h"
#include "svc_ai_player.h"   /* AP-STAT 运行时开关转发 */
#include "gw_intf.h"
#include "tuya_ai_toy.h"
#include "tuya_display_hw.h"
#include "tuya_ws_db.h"
#include "tal_workq_service.h"
#include "tal_memory.h"
#include "ty_cJSON.h"
#include "uni_log.h"
#include "ui_state.h"
#include "ui_i18n.h"
#include "ui_svc_call.h"   /* auto-answer runtime value bridge (call layer) */
#include "ui_svc_nfc.h"    /* NFC runtime value + one-way init bridge */
#include "ui_port_perf.h"  /* FPS/CPU overlay bridge (port layer, bool-only API) */

/* Single KV key stores all UI settings as a JSON object. */
#define UI_SETTINGS_KV_KEY    "ui_settings"

/* JSON field names */
#define UI_JSON_LANGUAGE      "language"
#define UI_JSON_BRIGHTNESS    "brightness"
#define UI_JSON_AUTO_ANSWER   "auto_answer"
#define UI_JSON_P2P_ENABLE    "p2p_enable"
#define UI_JSON_NFC_ENABLE    "nfc_enable"

#define UI_BRIGHTNESS_DEFAULT 50

/* Snapshot passed to the ui_settings save workqueue callback. */
typedef struct {
    uint8_t language;
    uint8_t brightness;
    uint8_t auto_answer;   /* inbound call auto-answer toggle (default OFF; switch hidden) */
    uint8_t p2p_enable;    /* P2P/call feature enable switch (default OFF) */
    uint8_t nfc_enable;    /* NFC feature enable switch (default OFF) */
} ui_save_ctx_t;

/* Last ui_settings values successfully written to KV. Initialized to defaults;
 * updated in load() and after each successful s_save_work(). Used by save() to
 * skip unnecessary KV writes when nothing changed. */
static ui_save_ctx_t s_last_saved = {
    .language    = 0,                  /* UI_LANG_ZH_CN */
    .brightness  = UI_BRIGHTNESS_DEFAULT,
    .auto_answer = 0,                  /* OFF (default; matches UI-layer s_auto_answer) */
    .p2p_enable  = 0,                  /* OFF (default; matches call-layer s_enabled) */
    .nfc_enable  = 0,                  /* OFF (default; matches NFC service state) */
};

/* ---------------------------------------------------------------------------
 * Lifecycle
 * -------------------------------------------------------------------------*/

/* Workqueue callback: writes the ui_settings KV. Runs on WORKQ_SYSTEM, not the
 * UI thread. Device mode (ai_dev_mode) is persisted separately by mode_save(). */
static void s_save_work(void *data)
{
    ui_save_ctx_t *ctx = (ui_save_ctx_t *)data;

    ty_cJSON *root = ty_cJSON_CreateObject();
    if (root) {
        ty_cJSON_AddNumberToObject(root, UI_JSON_LANGUAGE,    ctx->language);
        ty_cJSON_AddNumberToObject(root, UI_JSON_BRIGHTNESS,  ctx->brightness);
        ty_cJSON_AddNumberToObject(root, UI_JSON_AUTO_ANSWER, ctx->auto_answer);
        ty_cJSON_AddNumberToObject(root, UI_JSON_P2P_ENABLE,  ctx->p2p_enable);
        ty_cJSON_AddNumberToObject(root, UI_JSON_NFC_ENABLE,  ctx->nfc_enable);
        char *str = ty_cJSON_PrintUnformatted(root);
        ty_cJSON_Delete(root);
        if (str) {
            OPERATE_RET op_ret = wd_common_write(UI_SETTINGS_KV_KEY,
                                                 (const BYTE_T *)str, strlen(str) + 1);
            if (OPRT_OK == op_ret) {
                /* Update shadow copy so the next save() can skip if nothing changed. */
                s_last_saved = *ctx;
            } else {
                PR_ERR("ui settings kv write failed: %d", op_ret);
            }
            ty_cJSON_free(str);
        }
    }

    tal_free(ctx);
}

void ui_svc_dev_ctrl_load(void)
{
    BYTE_T *raw  = NULL;
    UINT_T  len  = 0;

    if (OPRT_OK == wd_common_read(UI_SETTINGS_KV_KEY, &raw, &len) && raw) {
        ty_cJSON *root = ty_cJSON_ParseWithLength((const char *)raw, len);
        wd_common_free_data(raw);

        if (root) {
            ty_cJSON *item;

            item = ty_cJSON_GetObjectItem(root, UI_JSON_LANGUAGE);
            if (item && item->type == ty_cJSON_Number) {
                uint8_t lang = (uint8_t)item->valueint;
                if (lang < UI_LANG_MAX) {
                    ui_i18n_set_lang((ui_lang_t)lang);
                    ui_state_set_language(lang);
                }
            }

            item = ty_cJSON_GetObjectItem(root, UI_JSON_BRIGHTNESS);
            if (item && item->type == ty_cJSON_Number) {
                uint8_t brightness = (uint8_t)item->valueint;
                if (brightness <= 100) {
                    ui_state_set_brightness(brightness);
                }
            }

#if ENABLE_UI_CALL_AUTO_ANSWER
            /* Gated with the settings-page switch row (see ui_svc_call.h).
             * While the switch is compiled out, do NOT push a stale persisted
             * ON back into the call layer — with no UI to turn it off, the
             * device would silently auto-answer forever. */
            item = ty_cJSON_GetObjectItem(root, UI_JSON_AUTO_ANSWER);
            if (item) {
                /* Push the persisted auto-answer value into the call layer's
                 * runtime state. Must run BEFORE the P2P-enable restore below, so
                 * that if P2P comes up here the SDK sees the right auto-answer. */
                bool on = ty_cJSON_IsTrue(item) ||
                          (item->type == ty_cJSON_Number && item->valueint != 0);
                ui_svc_call_auto_answer_set(on);
            }
#endif

            item = ty_cJSON_GetObjectItem(root, UI_JSON_P2P_ENABLE);
            if (item) {
                /* Restore the P2P enable switch. enabled_set(true) starts the P2P
                 * stack right here (idempotent); the later ui_svc_call_init() in
                 * ui_services_init() then no-ops. Absent/false key => P2P stays off. */
                bool on = ty_cJSON_IsTrue(item) ||
                          (item->type == ty_cJSON_Number && item->valueint != 0);
                ui_svc_call_enabled_set(on);
            }

            item = ty_cJSON_GetObjectItem(root, UI_JSON_NFC_ENABLE);
            if (item) {
                /* Same restore model as P2P: push the persisted switch value
                 * into the service during startup. set(true) schedules the
                 * one-way PN532 initialization immediately and is idempotent. */
                bool on = ty_cJSON_IsTrue(item) ||
                          (item->type == ty_cJSON_Number && item->valueint != 0);
                /* Seed the shadow before set(): a synchronous workqueue-submit
                 * failure rolls the switch back and calls save() immediately;
                 * save() must still see persisted ON versus corrected OFF. */
                s_last_saved.nfc_enable = on ? 1 : 0;
                ui_svc_nfc_switch_set(on);
            }

            ty_cJSON_Delete(root);
        }
    }

    /* Apply brightness to hardware. */
    tuya_display_hw_backlight_set(ui_state_get_settings()->brightness);

    /* Mode and volume: toy layer already loaded its own KV at init; sync ui_state.
     * Mirror both the device mode and the chat sub-mode so the mode/settings
     * pages start consistent with the chat page. */
    ui_state_set_device_mode((uint8_t)tuya_ai_toy_device_mode_get());
    ui_state_set_chat_sub_mode((uint8_t)tuya_ai_toy_trigger_mode_get());
    ui_state_set_volume(tuya_ai_toy_volume_get());

    /* Sync shadow copy so save() doesn't write back values that were just loaded. */
    s_last_saved.language    = ui_state_get_settings()->language;
    s_last_saved.brightness  = ui_state_get_settings()->brightness;
    s_last_saved.auto_answer = ui_svc_call_auto_answer_get() ? 1 : 0;
    s_last_saved.p2p_enable  = ui_svc_call_enabled_get() ? 1 : 0;
    s_last_saved.nfc_enable  = ui_svc_nfc_switch_get() ? 1 : 0;
}

void ui_svc_dev_ctrl_save(void)
{
    /* Snapshot the current ui_settings scalar values. Normally called by the
     * settings UI on leave; NFC initialization failure also calls it from
     * WORKQ_SYSTEM so the corrected OFF state is persisted immediately. Device
     * mode is not part of ui_settings — it is persisted by mode_save(). */
    ui_save_ctx_t cur = {
        .language    = ui_state_get_settings()->language,
        .brightness  = ui_state_get_settings()->brightness,
        .auto_answer = ui_svc_call_auto_answer_get() ? 1 : 0,
        .p2p_enable  = ui_svc_call_enabled_get() ? 1 : 0,
        .nfc_enable  = ui_svc_nfc_switch_get() ? 1 : 0,
    };

    /* Skip KV write if nothing changed since the last successful save. */
    if (cur.language    == s_last_saved.language   &&
        cur.brightness  == s_last_saved.brightness &&
        cur.auto_answer == s_last_saved.auto_answer &&
        cur.p2p_enable  == s_last_saved.p2p_enable &&
        cur.nfc_enable  == s_last_saved.nfc_enable) {
        return;
    }

    ui_save_ctx_t *ctx = (ui_save_ctx_t *)tal_malloc(sizeof(ui_save_ctx_t));
    if (!ctx) {
        PR_ERR("ui settings save: alloc failed");
        return;
    }
    *ctx = cur;

    OPERATE_RET op_ret = tal_workq_schedule(WORKQ_SYSTEM, s_save_work, ctx);
    if (OPRT_OK != op_ret) {
        PR_ERR("ui settings save: workq schedule failed: %d", op_ret);
        tal_free(ctx);
    }
}

/* ---------------------------------------------------------------------------
 * Reset
 * -------------------------------------------------------------------------*/

void ui_svc_dev_ctrl_reset(void)
{
    wd_common_delete(UI_SETTINGS_KV_KEY);

    GW_RESET_S rst = { GRT_LOCAL, FALSE };
    OPERATE_RET op_ret = gw_unactive(&rst);
    if (OPRT_OK != op_ret) {
        PR_ERR("gw_unactive failed: %d", op_ret);
    }
}

/* ---------------------------------------------------------------------------
 * Mode
 * -------------------------------------------------------------------------*/

uint8_t ui_svc_dev_ctrl_mode_get(void)
{
    return ui_state_get_chat()->device_mode;
}

void ui_svc_dev_ctrl_mode_set(uint8_t mode)
{
    ui_state_set_device_mode(mode);
}

/* Worker (WORKQ_SYSTEM, not the UI thread): apply the pending device-mode
 * switch. wukong_ai_device_mode_switch() runs the heavy DEINIT/INIT path and
 * persists ai_dev_mode; it is a no-op when the target already matches the
 * current mode. The target is carried in the pointer itself (no alloc/free). */
static void s_mode_save_work(void *data)
{
    AI_DEVICE_MODE_E target = (AI_DEVICE_MODE_E)(uintptr_t)data;
    if (target != tuya_ai_toy_device_mode_get()) {
        wukong_ai_device_mode_switch(target);
    }
}

void ui_svc_dev_ctrl_mode_save(void)
{
    uint8_t target = ui_state_get_chat()->device_mode;

    /* The toy layer's mode is the last-committed truth. Commit only a real
     * delta; the switch itself blocks and must run off the UI thread. */
    if (target == (uint8_t)tuya_ai_toy_device_mode_get()) {
        return;
    }

    OPERATE_RET op_ret = tal_workq_schedule(WORKQ_SYSTEM, s_mode_save_work,
                                            (void *)(uintptr_t)target);
    if (OPRT_OK != op_ret) {
        PR_ERR("ui mode save: workq schedule failed: %d", op_ret);
    }
}

/* ---------------------------------------------------------------------------
 * Language
 * -------------------------------------------------------------------------*/

uint8_t ui_svc_dev_ctrl_language_get(void)
{
    return ui_state_get_settings()->language;
}

void ui_svc_dev_ctrl_language_set(uint8_t lang)
{
    ui_i18n_set_lang((ui_lang_t)lang);
    ui_state_set_language(lang);
}

/* ---------------------------------------------------------------------------
 * Volume
 * -------------------------------------------------------------------------*/

uint8_t ui_svc_dev_ctrl_volume_get(void)
{
    return ui_state_get_system()->volume;
}

void ui_svc_dev_ctrl_volume_set(uint8_t volume)
{
    wukong_audio_player_set_vol(volume);
    ui_state_set_volume(volume);
}

/* Worker (WORKQ_SYSTEM, not the UI thread): route the committed volume through
 * the canonical toy setter — it syncs s_ai_toy->volume, blocking-writes KV, and
 * async-reports the volume DP (dpid 3). The 0~100 value is carried in the
 * pointer itself, so no alloc/free is needed for a single byte. */
static void s_volume_commit_work(void *data)
{
    uint8_t volume = (uint8_t)(uintptr_t)data;
    tuya_ai_toy_volume_set(volume);
}

void ui_svc_dev_ctrl_volume_commit(void)
{
    uint8_t target = ui_state_get_system()->volume;

    /* The toy layer's value is the last-committed/reported truth. The live drag
     * path (volume_set) updates only the player + ui_state, so on release
     * ui_state may lead the toy value — commit only a real delta. A plain tap
     * (no change) or a repeated release is a no-op (no spurious DP / KV write). */
    if (target == tuya_ai_toy_volume_get()) {
        return;
    }

    OPERATE_RET op_ret = tal_workq_schedule(WORKQ_SYSTEM, s_volume_commit_work,
                                            (void *)(uintptr_t)target);
    if (OPRT_OK != op_ret) {
        PR_ERR("ui volume commit: workq schedule failed: %d", op_ret);
    }
}

/* ---------------------------------------------------------------------------
 * Brightness
 * -------------------------------------------------------------------------*/

uint8_t ui_svc_dev_ctrl_brightness_get(void)
{
    return ui_state_get_settings()->brightness;
}

void ui_svc_dev_ctrl_brightness_set(uint8_t brightness)
{
    if (brightness > 100) {
        brightness = 100;
    }
    ui_state_set_brightness(brightness);
    tuya_display_hw_backlight_set(brightness);
}

/* ---------------------------------------------------------------------------
 * FPS/CPU overlay (memory-only) — thin forwarders to the port layer so the
 * settings page never includes port/LVGL headers directly.
 * -------------------------------------------------------------------------*/

bool ui_svc_dev_ctrl_fps_overlay_get(void)
{
    return ui_port_perf_overlay_get();
}

void ui_svc_dev_ctrl_fps_overlay_set(bool on)
{
    ui_port_perf_overlay_set(on);
}

/* ---------------------------------------------------------------------------
 * AP-STAT playback diagnostics (memory-only) — thin forwarders to the audio
 * player so the diag page never includes player headers directly. Rows are
 * only shown when the stats code is compiled in (AI_PLAYER_DEBUG_STATS=y).
 * -------------------------------------------------------------------------*/

bool ui_svc_dev_ctrl_ap_stat_available(void)
{
    return AI_PLAYER_DEBUG_STATS_AVAILABLE != 0;
}

bool ui_svc_dev_ctrl_ap_stat_get(void)
{
    return tuya_ai_player_debug_stats_get();
}

void ui_svc_dev_ctrl_ap_stat_set(bool on)
{
    tuya_ai_player_debug_stats_set(on);
}
