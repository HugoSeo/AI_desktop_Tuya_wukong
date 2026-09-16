#ifndef __UI_PORT_INDEV_H__
#define __UI_PORT_INDEV_H__

#include "tuya_cloud_types.h"
#include "tuya_app_config.h"

typedef struct {
    uint16_t hor_res;
    uint16_t ver_res;
} ui_port_indev_cfg_t;

void ui_port_indev_init(const ui_port_indev_cfg_t *cfg);

#endif /* __UI_PORT_INDEV_H__ */
