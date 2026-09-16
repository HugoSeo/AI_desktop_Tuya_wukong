/**
 * @file lv_tef.h
 * @brief LVGL 控件：循环播放 TEF (Tuya Emotion Format) 动画。
 *
 * 用法同 lv_gif：create 后 set_src 即循环播放，删控件即停。
 * 素材为编译进固件的 .tef 常量数组（miscs/tef/tools/gen_assets.py 生成），
 * 播放期间须常驻。解码发生在 LVGL timer（UI 线程）中，多控件可并存，
 * 每控件 PSRAM 占用约 画布 w*h*2 + 索引 w*h + 字典 16KB + tinfl 11KB。
 *
 * @copyright Copyright (c) tuya.inc 2026
 */
#ifndef __LV_TEF_H__
#define __LV_TEF_H__

#include "lvgl.h"
#include "tef_player.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lv_img_t img;
    lv_img_dsc_t imgdsc;
    tef_dec_t *dec;
    uint16_t *canvas;            /* PSRAM RGB565 画布（native 字节序） */
    lv_timer_t *timer;
} lv_tef_t;

extern const lv_obj_class_t lv_tef_class;

lv_obj_t *lv_tef_create(lv_obj_t *parent);

/**
 * @brief 设置素材并开始循环播放；失败时控件保持空 img（同 lv_gif）。
 * @param data .tef 字节流（须常驻，如 flash 常量数组）
 * @param len  字节数
 */
void lv_tef_set_src(lv_obj_t *obj, const uint8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* __LV_TEF_H__ */
