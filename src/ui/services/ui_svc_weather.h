#ifndef __UI_SVC_WEATHER_H__
#define __UI_SVC_WEATHER_H__

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int8_t  temp;
    int8_t  high;
    int8_t  low;
    char    desc[16];
} ui_svc_weather_t;

typedef void (*ui_svc_weather_cb_t)(const ui_svc_weather_t *weather);

void ui_svc_weather_init(void);
void ui_svc_weather_set_cb(ui_svc_weather_cb_t cb);

bool ui_svc_weather_available(void);
const ui_svc_weather_t *ui_svc_weather_get(void);

#endif /* __UI_SVC_WEATHER_H__ */
