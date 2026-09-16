#ifndef TUYA_APP_CONFIG_H
#define TUYA_APP_CONFIG_H
/*
 * Host-test stub for the KConfig-generated tuya_app_config.h.
 *
 * Host unit tests never run KConfig, so no CONFIG_* macros are defined here.
 * Code that keys off such macros therefore takes its no-macro path — notably
 * WUKONG_STORAGE_ENABLE stays undefined, so the storage layer compiles to its
 * inert stubs and the deferred-load gates resolve to an immediate load.
 * Firmware uses the real generated apps/.../include/tuya_app_config.h.
 */

/* Minimal macros used by wukong_profile.c Device card. */
#define TUYA_LCD_WIDTH_VAL  320
#define TUYA_LCD_HEIGHT_VAL 480

/* LLM model name shown in the Device card (real value comes from Kconfig). */
#define CLAW_LLM_MODEL "glm-test"

#endif /* TUYA_APP_CONFIG_H */
