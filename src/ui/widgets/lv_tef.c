/**
 * @file lv_tef.c
 * @brief TEF 动画控件 — 仿 lv_gif：lv_img 派生 + 自有画布 + lv_timer 逐帧解码。
 *
 * 帧节拍 = 素材头 fps（timer 周期一次设定）；解码在 UI 线程串行执行，
 * 天然满足 tef_dec_* 的同线程约束。画布指针仅存于实例字段，无 static 缓存。
 *
 * @copyright Copyright (c) tuya.inc 2026
 */
#include <string.h>

#include "lv_tef.h"
#include "tkl_memory.h"
#include "tal_log.h"

#define MY_CLASS &lv_tef_class

static void lv_tef_constructor(const lv_obj_class_t *class_p, lv_obj_t *obj);
static void lv_tef_destructor(const lv_obj_class_t *class_p, lv_obj_t *obj);
static void next_frame_cb(lv_timer_t *t);
static void clean_src(lv_tef_t *tef);

const lv_obj_class_t lv_tef_class = {
    .constructor_cb = lv_tef_constructor,
    .destructor_cb = lv_tef_destructor,
    .instance_size = sizeof(lv_tef_t),
    .base_class = &lv_img_class,
};

lv_obj_t *lv_tef_create(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_class_create_obj(MY_CLASS, parent);
    lv_obj_class_init_obj(obj);
    return obj;
}

void lv_tef_set_src(lv_obj_t *obj, const uint8_t *data, uint32_t len)
{
    lv_tef_t *tef = (lv_tef_t *)obj;
    uint16_t w, h;

    clean_src(tef);

    if (tef_dec_open(&tef->dec, data, len) != OPRT_OK) {
        TAL_PR_WARN("lv_tef: bad src");
        return;
    }
    w = tef_dec_width(tef->dec);
    h = tef_dec_height(tef->dec);
    tef->canvas = (uint16_t *)tkl_system_psram_malloc((uint32_t)w * h * 2);
    if (tef->canvas == NULL) {
        TAL_PR_ERR("lv_tef: canvas %ux%u alloc failed", w, h);
        clean_src(tef);
        return;
    }
    memset(tef->canvas, 0, (uint32_t)w * h * 2);
    tef_dec_set_dst(tef->dec, tef->canvas, w);

    /* 立即解第 0 帧（关键帧），画布从首帧起有效，避免上屏闪黑 */
    if (tef_dec_next(tef->dec) != OPRT_OK) {
        TAL_PR_ERR("lv_tef: first frame decode failed");
        clean_src(tef);
        return;
    }

    tef->imgdsc.header.always_zero = 0;
    tef->imgdsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    tef->imgdsc.header.w = w;
    tef->imgdsc.header.h = h;
    tef->imgdsc.data = (const uint8_t *)tef->canvas;
    tef->imgdsc.data_size = (uint32_t)w * h * 2;
    lv_img_set_src(obj, &tef->imgdsc);

    lv_timer_set_period(tef->timer, tef_dec_frame_ms(tef->dec));
    lv_timer_reset(tef->timer);
    lv_timer_resume(tef->timer);
}

/* 释放素材相关资源（timer 本体保留）。顺序约束：先失效 img cache 再释放
 * 画布，防 LVGL 缓存条目指向已释放内存 */
static void clean_src(lv_tef_t *tef)
{
    lv_timer_pause(tef->timer);
    if (tef->imgdsc.data != NULL) {
        lv_img_cache_invalidate_src(&tef->imgdsc);
        tef->imgdsc.data = NULL;
    }
    if (tef->dec != NULL) {
        tef_dec_close(tef->dec);
        tef->dec = NULL;
    }
    if (tef->canvas != NULL) {
        tkl_system_psram_free(tef->canvas);
        tef->canvas = NULL;
    }
}

static void lv_tef_constructor(const lv_obj_class_t *class_p, lv_obj_t *obj)
{
    lv_tef_t *tef = (lv_tef_t *)obj;

    LV_UNUSED(class_p);
    tef->dec = NULL;
    tef->canvas = NULL;
    tef->imgdsc.data = NULL;
    tef->timer = lv_timer_create(next_frame_cb, 100, obj);
    lv_timer_pause(tef->timer);
}

static void lv_tef_destructor(const lv_obj_class_t *class_p, lv_obj_t *obj)
{
    LV_UNUSED(class_p);
    clean_src((lv_tef_t *)obj);
    lv_timer_del(((lv_tef_t *)obj)->timer);
}

static void next_frame_cb(lv_timer_t *t)
{
    lv_obj_t *obj = (lv_obj_t *)t->user_data;
    lv_tef_t *tef = (lv_tef_t *)obj;

    if (tef->dec == NULL) {
        lv_timer_pause(t);
        return;
    }
    if (tef_dec_next(tef->dec) != OPRT_OK) {
        /* 素材损坏：定格末帧，不重试 */
        TAL_PR_ERR("lv_tef: decode failed, freeze");
        lv_timer_pause(t);
        return;
    }
    lv_img_cache_invalidate_src(lv_img_get_src(obj));
    lv_obj_invalidate(obj);
}
