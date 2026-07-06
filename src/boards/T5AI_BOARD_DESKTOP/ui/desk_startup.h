#ifndef __DESK_STARTUP_H__
#define __DESK_STARTUP_H__ 
#include "desk_ui_res.h"

typedef struct 
{
    lv_img_dsc_t emoj_gif;
}netcfg_scr_res_t;

typedef struct
{
    lv_obj_t *language_scr;
}language_scr_t;

typedef struct
{
    lv_obj_t *qrcode_scr;
}qrcode_scr_t;

typedef struct
{
    lv_obj_t *cfg_network_scr;
}network_start_scr_t;

typedef struct
{
	lv_obj_t *startup_scr;
    language_scr_t      language_lv;
    qrcode_scr_t        qrcode_lv;
    network_start_scr_t network_start_lv;
}lv_startup_ui_t;


void setup_scr_startup(void);

void setup_scr_language_scr(void);

void setup_scr_qrcode_scr(void);

void setup_scr_network_cfg(void);

int cfg_network_success(void);

static int cfg_network_failed(void);

void network_cfg_res_clear(void);

#endif  // __DESK_STARTUP_H__