#ifndef __UI_SVC_DEV_CTRL_H__
#define __UI_SVC_DEV_CTRL_H__

#include <stdint.h>
#include <stdbool.h>

/*
 * Device-control actions for the settings UI.
 *
 * Persistence model:
 *   load()      — call once at startup; restores all settings from KV to runtime state.
 *   save()      — call when a settings page exits; flushes ui_settings (language,
 *                 brightness, auto-answer, P2P, NFC) to the ui_settings KV.
 *   mode_save() — call when the device mode is switched; flushes only the device
 *                 mode to the ai_dev_mode KV. Kept separate from save().
 *   *_set()     — runtime only, no KV write; cheap to call on every tap/change.
 *   *_get()     — reads from runtime state (ui_state), not KV.
 */

/* Startup restore: reads all persisted settings from KV and applies to runtime. */
void ui_svc_dev_ctrl_load(void);

/* UI-settings flush: writes the ui_settings KV (language/brightness/auto-answer/
 * P2P/NFC). Normally called on settings-page exit; services may also call it
 * after runtime correction. Does NOT touch the device mode. */
void ui_svc_dev_ctrl_save(void);

/* Factory-reset (unbind) the device. Schedules gw_unactive() on the system
 * workqueue and returns immediately — caller should show a loading indicator. */
void ui_svc_dev_ctrl_reset(void);

/* Device main mode (maps 1:1 to AI_DEVICE_MODE_E). */
uint8_t ui_svc_dev_ctrl_mode_get(void);
void    ui_svc_dev_ctrl_mode_set(uint8_t mode);   /* runtime only */

/* Device-mode flush: if the runtime mode differs from the toy layer's current
 * mode, switches it on WORKQ_SYSTEM (persists the ai_dev_mode KV). Call after a
 * mode switch; does NOT touch ui_settings. */
void    ui_svc_dev_ctrl_mode_save(void);

/* UI language (maps 1:1 to ui_lang_t). */
uint8_t ui_svc_dev_ctrl_language_get(void);
void    ui_svc_dev_ctrl_language_set(uint8_t lang);   /* runtime only */

/* Volume 0~100.
 *   set()    — runtime only: applies to the audio player + ui_state immediately.
 *              Cheap; safe to call on every slider tick. Does NOT report the DP
 *              or persist to KV.
 *   commit() — call once when the volume gesture ends (slider release): if the
 *              value changed vs. the toy layer, routes it through the canonical
 *              toy setter on WORKQ_SYSTEM (syncs s_ai_toy->volume, persists KV,
 *              reports volume DP to cloud). Off the UI thread (KV write blocks). */
uint8_t ui_svc_dev_ctrl_volume_get(void);
void    ui_svc_dev_ctrl_volume_set(uint8_t volume);
void    ui_svc_dev_ctrl_volume_commit(void);

/* Backlight brightness 0~100. set() applies immediately; save() persists. */
uint8_t ui_svc_dev_ctrl_brightness_get(void);
void    ui_svc_dev_ctrl_brightness_set(uint8_t brightness);

/* LVGL FPS/CPU overlay. Memory-only (no KV persistence) — defaults OFF on boot.
 * Forwards to the port-layer overlay; the service itself never touches LVGL. */
bool ui_svc_dev_ctrl_fps_overlay_get(void);
bool ui_svc_dev_ctrl_ap_stat_available(void); /* 统计代码是否编入(AI_PLAYER_DEBUG_STATS) */
bool ui_svc_dev_ctrl_ap_stat_get(void);       /* AP-STAT 运行时开关(内存态,默认关) */
void ui_svc_dev_ctrl_ap_stat_set(bool on);
void ui_svc_dev_ctrl_fps_overlay_set(bool on);

#endif /* __UI_SVC_DEV_CTRL_H__ */
