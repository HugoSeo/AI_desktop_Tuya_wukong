#include "tuya_gui_navigate.h"
#include "tuya_gui_nav_internal.h"
#include "tuya_gui_base.h"
#include "tuya_gui_gesture.h"
#include "tuya_gui_page.h"
#include "tuya_gui_anim.h"
#include "tuya_gui_anim_internal.h"

STATIC UINT_T g_load_new_anim_time = GUI_PAGE_LOAD_NEW_ANIM_DEF_TIME;
STATIC UINT_T g_load_old_anim_time = GUI_PAGE_LOAD_OLD_ANIM_DEF_TIME;
STATIC UINT_T g_page_offset = GUI_PAGE_ANIM_OFFSET;
STATIC TUYA_NAV_PAGE_S g_last_nav;
STATIC SYS_TICK_T start_tick = 0;

VOID gui_anim_load_new_page_anim_time(UINT_T time)
{
    g_load_new_anim_time = time;
}

VOID gui_anim_load_old_page_anim_time(UINT_T time)
{
    g_load_old_anim_time = time;
}

VOID gui_anim_load_anim_offset(UINT_T offset)
{
    g_page_offset = offset;
}

STATIC VOID __anim_set_out_new_ready_cb(lv_anim_t * a)
{
    lv_scr_load(a->var);
}

STATIC VOID __anim_set_out_old_ready_cb(lv_anim_t * a)
{
    lv_disp_t *d;

    lv_obj_set_x(a->var, 0);

    if (g_last_nav.page.page_stop_cb) {
        PR_DEBUG("nav anim page stop id: %s", g_last_nav.page.id);
        tuya_gui_call_atom(g_last_nav.page.page_stop_cb(g_last_nav.obj););
    }

    d = lv_obj_get_disp(a->var);
    if(d != NULL) {
        d->prev_scr = NULL;
    }
}


STATIC VOID __anim_ready_cb(lv_anim_t * a)
{
#if LVGL_VERSION_MAJOR < 9
    PR_DEBUG("anim time: %d use time: %llu", (int)a->time, uni_time_get_posix_ms() - start_tick);
#else
    PR_DEBUG("anim time: %d use time: %llu", (int)a->duration, uni_time_get_posix_ms() - start_tick);
#endif
}

STATIC VOID __anim_set_in_ready_cb(lv_anim_t * a)
{
    lv_disp_t *d;

    lv_obj_set_x(a->var, 0);

    if (g_last_nav.page.page_stop_cb) {
        PR_DEBUG("nav anim page stop id: %s", g_last_nav.page.id);
        tuya_gui_call_atom(g_last_nav.page.page_stop_cb(g_last_nav.obj););
    }

    d = lv_obj_get_disp(a->var);
    if(d != NULL) {
        d->prev_scr = NULL;
    }
}


STATIC void __anim_exec_set_x(void *var, int32_t v)
{
    lv_obj_set_x(var, v);
}

/**
 * @brief 多级页面in\out动画
 *
 */
STATIC VOID __anim_page_start(lv_obj_t *new_obj, TY_PAGE_ANIM_TP_E anim_type, UINT_T anim_time)
{
    lv_disp_t *d = lv_obj_get_disp(new_obj);
    UINT_T time = (anim_time == 0) ? g_load_new_anim_time : anim_time;

    lv_anim_t a_new;
    lv_anim_init(&a_new);
    lv_anim_set_var(&a_new, new_obj);
    lv_anim_set_time(&a_new, time);
    lv_anim_set_path_cb(&a_new, lv_anim_path_ease_out);

    lv_anim_t a_old;
    lv_anim_init(&a_old);
    lv_anim_set_var(&a_old, d->act_scr);
    lv_anim_set_time(&a_old, time);
    lv_anim_set_path_cb(&a_old, lv_anim_path_ease_out);

    switch(anim_type) {
        case TY_PAGE_ANIM_TP_IN:
            d->prev_scr = lv_scr_act();
            lv_disp_load_scr(new_obj);

            lv_anim_set_exec_cb(&a_new, __anim_exec_set_x);
            lv_anim_set_values(&a_new, lv_disp_get_hor_res(d) / 2, 0);

            lv_anim_set_exec_cb(&a_old, __anim_exec_set_x);
            lv_anim_set_ready_cb(&a_old, __anim_set_in_ready_cb);
            lv_anim_set_values(&a_old, 0, -g_page_offset);

            lv_anim_start(&a_new);
            lv_anim_start(&a_old);
            break;

        case TY_PAGE_ANIM_TP_IN_SIMPLE:
            if (g_last_nav.page.page_stop_cb) {
                tuya_gui_call_atom(g_last_nav.page.page_stop_cb(g_last_nav.obj););
            }

            lv_scr_load(new_obj);
            lv_anim_set_exec_cb(&a_new, __anim_exec_set_x);
            lv_anim_set_values(&a_new, g_page_offset * time / g_load_new_anim_time, 0);
            lv_anim_set_ready_cb(&a_new, __anim_ready_cb);

            lv_anim_start(&a_new);
            start_tick = uni_time_get_posix_ms();
            break;

        case TY_PAGE_ANIM_TP_OUT:
            d->prev_scr = lv_scr_act();
            lv_disp_load_scr(new_obj);

            lv_anim_set_exec_cb(&a_new, (lv_anim_exec_xcb_t)lv_obj_set_x);
            lv_anim_set_values(&a_new, lv_obj_get_x(new_obj) - g_page_offset, lv_obj_get_x(new_obj));

            lv_anim_set_exec_cb(&a_old, __anim_exec_set_x);
            lv_anim_set_ready_cb(&a_old, __anim_set_out_old_ready_cb);
            lv_anim_set_values(&a_old, 0, lv_disp_get_hor_res(d) / 2);

            lv_anim_start(&a_new);
            lv_anim_start(&a_old);
            break;
            
        case TY_PAGE_ANIM_TP_OUT_SIMPLE:
            if (g_last_nav.page.page_stop_cb) {
                tuya_gui_call_atom(g_last_nav.page.page_stop_cb(g_last_nav.obj););
            }

            lv_scr_load(new_obj);
            lv_anim_set_exec_cb(&a_new, __anim_exec_set_x);
            lv_anim_set_values(&a_new, (lv_obj_get_x(new_obj) - g_page_offset * time / g_load_new_anim_time), lv_obj_get_x(new_obj));
            lv_anim_set_ready_cb(&a_new, __anim_ready_cb);

            lv_anim_start(&a_new);
            start_tick = uni_time_get_posix_ms();
            break;

        default:
            break;
    }
}

STATIC VOID __anim_zoom_press_cb(void *obj, int32_t v)
{
    lv_img_set_zoom(obj, v);
}

STATIC VOID __anim_zoom_ready_cb(lv_anim_t * a)
{
    if (((lv_obj_t *)(a->var))->user_data) {
        lv_img_cache_invalidate_src(((lv_obj_t *)(a->var))->user_data);

        // 缩放动画页内存常驻，不释放快照buf，只释放object
        lv_mem_free(((lv_obj_t *)(a->var))->user_data);
    }

    lv_obj_del(((lv_obj_t *)(a->var))->parent);
    lv_obj_clear_flag(a->user_data, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief 对新页面的缩放动画
 *
 */
STATIC VOID __anim_zoom(lv_obj_t *obj, UINT_T anim_time)
{
    static uint32_t snapshots_buff_size = 0;
    static void * snapshots_buff = NULL;

    // 创建缩放动画常驻内存,
    // TODO： 可以把内存申请这个地方放到外面公用
#if LVGL_VERSION_MAJOR < 9
    uint32_t need_size = lv_snapshot_buf_size_needed(obj, LV_IMG_CF_TRUE_COLOR_ALPHA);
#else
    LV_ASSERT_NULL(obj);
    lv_obj_update_layout(obj);

    /*Width and height determine snapshot image size.*/
    int32_t w = lv_obj_get_width(obj);
    int32_t h = lv_obj_get_height(obj);
    int32_t ext_size = _lv_obj_get_ext_draw_size(obj);
    w += ext_size * 2;
    h += ext_size * 2;

    uint8_t px_size = lv_color_format_get_bpp(LV_COLOR_FORMAT_NATIVE_WITH_ALPHA);
    uint32_t need_size = w * h * ((px_size + 7) >> 3);
#endif
    if(snapshots_buff_size < need_size) {
        if(snapshots_buff) {
            lv_mem_free(snapshots_buff);
            snapshots_buff = NULL;
        }

        snapshots_buff = lv_mem_alloc(need_size);
        if(snapshots_buff == NULL) {
            PR_ERR("snapshot buf mem is null");
            return;
        }
        snapshots_buff_size = need_size;
    }

    // 创建缩放动画 object
    lv_img_dsc_t * snapshots = lv_mem_alloc(sizeof(lv_img_dsc_t));
    if(snapshots == NULL) {
        PR_ERR("snapshot dsc mem is null");
        return;
    }

    // 先去除原obj的背景
    const void * bg_img_src = lv_obj_get_style_bg_img_src(obj, LV_PART_MAIN);
    lv_opa_t bg_opa = LV_OPA_100;
    if(bg_img_src) {
        lv_obj_set_style_bg_img_src(obj, NULL, LV_PART_MAIN);
        bg_opa = lv_obj_get_style_bg_opa(obj, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(obj, LV_OPA_0, LV_PART_MAIN);
    }

    // 给页面增加快照
    lv_snapshot_take_to_buf(obj, LV_IMG_CF_TRUE_COLOR_ALPHA, snapshots, snapshots_buff, snapshots_buff_size);
    lv_obj_t *img_parent = lv_img_create(lv_layer_top());
    lv_obj_t *img = lv_img_create(img_parent);
    lv_obj_align_to(img, img_parent, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_img_set_src(img, snapshots);
    lv_obj_set_user_data(img, snapshots);

    //隐藏当前对象, 并复原背景
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    if(bg_img_src) {
        lv_obj_set_style_bg_img_src(obj, bg_img_src, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(obj, bg_opa, LV_PART_MAIN);
        // 动画父对象也加入背景
        lv_obj_set_style_bg_img_src(img_parent, bg_img_src, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(img_parent, bg_opa, LV_PART_MAIN);
    }

    // 缩放动画
    lv_anim_t zoom_anim;
    lv_anim_init(&zoom_anim);
    lv_anim_set_var(&zoom_anim, img);
    lv_anim_set_values(&zoom_anim, LV_IMG_ZOOM_NONE - 10, LV_IMG_ZOOM_NONE);
    lv_anim_set_exec_cb(&zoom_anim, __anim_zoom_press_cb);
    lv_anim_set_time(&zoom_anim, (anim_time == 0) ? 100 : anim_time);
    lv_anim_set_ready_cb(&zoom_anim, __anim_zoom_ready_cb);
    lv_anim_set_user_data(&zoom_anim, obj);
    lv_anim_start(&zoom_anim); // 用时间线的话 这个要注释掉
}

VOID tuya_gui_nav_anim_start(lv_obj_t *new_obj, TUYA_NAV_PAGE_S *p_last_nav, TY_PAGE_ANIM_TP_E anim_type, UINT_T anim_time)
{
    switch (anim_type) {
        case TY_PAGE_ANIM_TP_ZOOM:
            __anim_zoom(new_obj, anim_time);
            break;

        case TY_PAGE_ANIM_TP_IN:
        case TY_PAGE_ANIM_TP_OUT:
        case TY_PAGE_ANIM_TP_IN_SIMPLE:
        case TY_PAGE_ANIM_TP_OUT_SIMPLE:
            if (p_last_nav) {
                memcpy(&g_last_nav, p_last_nav, sizeof(TUYA_NAV_PAGE_S));
            }
            __anim_page_start(new_obj, anim_type, anim_time);
            break;

        default:
            break;
    }
}

OPERATE_RET tuya_gui_nav_anim_clear()
{
    lv_disp_t *disp = lv_disp_get_default();

    if (disp->prev_scr && lv_anim_get(disp->prev_scr, __anim_exec_set_x)) {
        PR_DEBUG("clear nav anim prev_scr");
        lv_anim_del(disp->prev_scr, __anim_exec_set_x);
        lv_obj_set_x(disp->prev_scr, 0);

        if (g_last_nav.page.page_stop_cb) {
            PR_DEBUG("nav anim clear, page stop id: %s", g_last_nav.page.id);
            tuya_gui_call_atom(g_last_nav.page.page_stop_cb(g_last_nav.obj););
        }

        disp->prev_scr = NULL;

    }

    return OPRT_OK;
}
