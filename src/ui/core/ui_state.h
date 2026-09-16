#ifndef __UI_STATE_H__
#define __UI_STATE_H__

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    UI_STATE_GROUP_SYSTEM = 0,
    UI_STATE_GROUP_SETTINGS,
    UI_STATE_GROUP_CHAT,
    UI_STATE_GROUP_MAX
} ui_state_group_t;

typedef struct {
    uint8_t  battery_level;
    uint8_t  wifi_strength;
    uint8_t  volume;
    uint8_t  calendar_count;
    uint8_t  alarm_count;
    bool     is_charging;
    bool     is_online;
    uint32_t timestamp;
} ui_state_system_t;

typedef struct {
    uint8_t  language;
    uint8_t  theme_mode;
    uint8_t  brightness;
} ui_state_settings_t;

typedef struct {
    uint8_t  chat_status;
    uint8_t  device_mode;
    uint8_t  chat_sub_mode;
} ui_state_chat_t;

void ui_state_init(void);

const ui_state_system_t   *ui_state_get_system(void);
const ui_state_settings_t *ui_state_get_settings(void);
const ui_state_chat_t     *ui_state_get_chat(void);

void ui_state_set_battery(uint8_t level);
void ui_state_set_wifi(uint8_t strength);
void ui_state_set_volume(uint8_t volume);
void ui_state_set_charging(bool charging);
void ui_state_set_online(bool online);
void ui_state_set_timestamp(uint32_t ts);
void ui_state_set_calendar_count(uint8_t count);
void ui_state_set_alarm_count(uint8_t count);
void ui_state_set_language(uint8_t lang);
void ui_state_set_theme_mode(uint8_t mode);
void ui_state_set_brightness(uint8_t brightness);

void ui_state_set_chat_status(uint8_t status);
void ui_state_set_device_mode(uint8_t mode);
void ui_state_set_chat_sub_mode(uint8_t sub_mode);

uint32_t ui_state_consume_dirty(void);
void     ui_state_mark_all_dirty(void);
void     ui_state_mark_dirty(ui_state_group_t group);

#endif /* __UI_STATE_H__ */
