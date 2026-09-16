#ifndef __UI_PORT_DISP_H__
#define __UI_PORT_DISP_H__

#include "tuya_cloud_types.h"

typedef struct {
    void *disp_handle;
    uint16_t hor_res;
    uint16_t ver_res;
} ui_port_disp_cfg_t;

void ui_port_disp_init(const ui_port_disp_cfg_t *cfg);
void ui_port_disp_deinit(void);

#endif /* __UI_PORT_DISP_H__ */
