#include "ui_state.h"
#include <string.h>

static ui_state_system_t   s_system;
static ui_state_settings_t s_settings;
static ui_state_chat_t     s_chat;
static uint32_t            s_dirty;

void ui_state_init(void) {
    memset(&s_system, 0, sizeof(s_system));
    memset(&s_settings, 0, sizeof(s_settings));
    memset(&s_chat, 0, sizeof(s_chat));
    s_settings.brightness = 50;   /* default backlight */
    s_chat.chat_status = 1; /* AI_CHAT_IDLE */
    s_dirty = 0;
}

const ui_state_system_t   *ui_state_get_system(void)   { return &s_system; }
const ui_state_settings_t *ui_state_get_settings(void) { return &s_settings; }
const ui_state_chat_t     *ui_state_get_chat(void)     { return &s_chat; }

uint32_t ui_state_consume_dirty(void) {
    uint32_t d = s_dirty;
    s_dirty = 0;
    return d;
}

void ui_state_mark_all_dirty(void) {
    s_dirty = (1u << UI_STATE_GROUP_MAX) - 1u;
}

void ui_state_mark_dirty(ui_state_group_t group) {
    s_dirty |= (1u << group);
}

#define STATE_SET(field, val, group) do { \
    if ((field) == (val)) return;         \
    (field) = (val);                      \
    s_dirty |= (1u << (group));           \
} while (0)

void ui_state_set_battery(uint8_t level)    { STATE_SET(s_system.battery_level, level, UI_STATE_GROUP_SYSTEM); }
void ui_state_set_wifi(uint8_t strength)    { STATE_SET(s_system.wifi_strength, strength, UI_STATE_GROUP_SYSTEM); }
void ui_state_set_volume(uint8_t volume)    { STATE_SET(s_system.volume, volume, UI_STATE_GROUP_SYSTEM); }
void ui_state_set_charging(bool charging)   { STATE_SET(s_system.is_charging, charging, UI_STATE_GROUP_SYSTEM); }
void ui_state_set_online(bool online)       { STATE_SET(s_system.is_online, online, UI_STATE_GROUP_SYSTEM); }
void ui_state_set_timestamp(uint32_t ts)    { STATE_SET(s_system.timestamp, ts, UI_STATE_GROUP_SYSTEM); }
void ui_state_set_calendar_count(uint8_t count) { STATE_SET(s_system.calendar_count, count, UI_STATE_GROUP_SYSTEM); }
void ui_state_set_alarm_count(uint8_t count)    { STATE_SET(s_system.alarm_count, count, UI_STATE_GROUP_SYSTEM); }
void ui_state_set_language(uint8_t lang)      { STATE_SET(s_settings.language,   lang, UI_STATE_GROUP_SETTINGS); }
void ui_state_set_theme_mode(uint8_t mode)    { STATE_SET(s_settings.theme_mode, mode, UI_STATE_GROUP_SETTINGS); }
void ui_state_set_brightness(uint8_t b)       { STATE_SET(s_settings.brightness, b,    UI_STATE_GROUP_SETTINGS); }

void ui_state_set_chat_status(uint8_t status)    { STATE_SET(s_chat.chat_status, status, UI_STATE_GROUP_CHAT); }
void ui_state_set_device_mode(uint8_t mode)      { STATE_SET(s_chat.device_mode, mode, UI_STATE_GROUP_CHAT); }
void ui_state_set_chat_sub_mode(uint8_t sub_mode){ STATE_SET(s_chat.chat_sub_mode, sub_mode, UI_STATE_GROUP_CHAT); }
