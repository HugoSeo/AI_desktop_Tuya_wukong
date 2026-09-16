#ifndef __UI_PORT_H__
#define __UI_PORT_H__

#include "tuya_cloud_types.h"

typedef struct {
    void *disp_handle;
    uint16_t hor_res;
    uint16_t ver_res;
    uint32_t task_priority;
    uint32_t task_stack_size;
} ui_port_cfg_t;

void ui_port_init(const ui_port_cfg_t *cfg);
void ui_port_start(void);
void ui_port_stop(void);
void ui_port_deinit(void);
void ui_port_lock(void);
void ui_port_unlock(void);

#endif /* __UI_PORT_H__ */
