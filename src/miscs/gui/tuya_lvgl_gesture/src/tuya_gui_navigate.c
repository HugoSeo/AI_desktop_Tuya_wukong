#include "tuya_gui_navigate.h"
#include "tuya_gui_nav_internal.h"
#include "tuya_gui_base.h"
#include "tuya_gui_gesture.h"
#include "tuya_gui_gesture_internal.h"
#include "tuya_gui_history.h"
#include "tuya_gui_page.h"
#include "tuya_gui_anim_internal.h"
#include "tuya_gui_base_event.h"


STATIC TUYA_NAV_MGR_S nav_mgr = {0x0};

STATIC VOID __page_start_check_tabview(TUYA_NAV_PAGE_S *p_new_page);
STATIC OPERATE_RET tuya_gui_nav_hor_page_get(UINT16_T tab_idx, TUYA_NAV_PAGE_S *nav);
STATIC OPERATE_RET gui_nav_hor_page_get_next(CHAR_T *id, TUYA_NAV_PAGE_S *next_page);
STATIC OPERATE_RET gui_nav_hor_page_get_prev(CHAR_T *id, TUYA_NAV_PAGE_S *prev_page);
STATIC OPERATE_RET gui_nav_page_start_unlock(TUYA_NAV_PAGE_S *p_nav,
                                             VOID *start_param, UINT_T param_len, BOOL_T hor_load,
                                             TY_PAGE_ANIM_TP_E anim_type, UINT_T anim_time);

STATIC VOID __refresh_event(INT_T hor_idx)
{
    TUYA_NAV_PAGE_S next = {0x0};

    if (hor_idx <  0 || hor_idx >= nav_mgr.hor.hor_num) {
        PR_TRACE("not need publish:%d", hor_idx);
        return;
    }

    OPERATE_RET ret = tuya_gui_nav_hor_page_get(hor_idx, &next);
    if (OPRT_OK != ret) {
        PR_ERR("got hor nav error: %d", ret);
        return;
    }

    PR_TRACE("request tab: %s refresh", next.page.id);
    CHAR_T name[EVENT_NAME_MAX_LEN + 1] = {0x0};
    int name_cp_len = snprintf(name, sizeof(name), "%s-%s", next.page.id, TY_GUI_EV_REFRESH);
    if (name_cp_len >= sizeof(name)) {
        PR_WARN("id: %s is too long to name:%s, len=%d", next.page.id, name, name_cp_len);
    }

    tuya_publish(name, NULL);
}

STATIC VOID __tabview_event(lv_event_t *e)
{
    TUYA_NAV_PAGE_S nav = {0x0};
    lv_event_code_t event = lv_event_get_code(e);

    if (LV_EVENT_VALUE_CHANGED == event) {
        lv_obj_t *page_obj = lv_tileview_get_tile_act(nav_mgr.hor.hor_tabview->obj);
        TUYA_GUI_PAGE_S *p_page = (TUYA_GUI_PAGE_S *)lv_obj_get_user_data(page_obj);
        if (OPRT_OK != tuya_gui_nav_page_get(p_page->id, &nav)) {
            PR_ERR("got hor nav: %s error", p_page->id);
            return;
        }

        if (0 == strcmp(nav_mgr.curr_page_id, p_page->id)) {
            PR_TRACE("repeat start page: %s", p_page->id);
            return;
        }

        BOOL_T curr_is_hor = tuya_page_is_hor(nav_mgr.curr_page_id);
        if (!curr_is_hor) {
            PR_NOTICE("curr_page_id: %s not hor page", nav_mgr.curr_page_id);
            return;
        }

        PR_DEBUG("start tab id: %s", nav.page.id);
        gui_nav_page_start_unlock(&nav, NULL, 0, FALSE, TY_PAGE_ANIM_TP_NONE, 0);

    } else if (event == LV_EVENT_SCROLL_BEGIN) {
        lv_obj_t *page_obj = lv_tileview_get_tile_act(nav_mgr.hor.hor_tabview->obj);
        if(FALSE == lv_obj_is_valid(page_obj)) {
            PR_ERR("page_obj is invalid");
            return;
        }

        TUYA_GUI_PAGE_S *p_page = (TUYA_GUI_PAGE_S *)lv_obj_get_user_data(page_obj);
        if (OPRT_OK != tuya_gui_nav_page_get(p_page->id, &nav)) {
            PR_ERR("got hor nav: %s error", p_page->id);
            return;
        }

        TUYA_PAGE_PRIV_CTX_S *p_ctx = (TUYA_PAGE_PRIV_CTX_S *)nav.priv_ctx;
        if (p_ctx->is_hor) {
            __refresh_event(nav.hor_idx - 1);
            __refresh_event(nav.hor_idx + 1);
        }
    }
}

STATIC OPERATE_RET tuya_gui_nav_set_page_obj(CHAR_T *id, TUYA_NAV_PAGE_S *p_nav)
{
    TUYA_NAV_PAGE_S *p_node = NULL;
    SLIST_HEAD *pos = NULL, *next = NULL;

    TUYA_CHECK_NULL_RETURN(p_nav, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(id, OPRT_INVALID_PARM);

    tuya_gui_mutex_lock();
    SLIST_FOR_EACH_ENTRY_SAFE(p_node, TUYA_NAV_PAGE_S, pos, next, &nav_mgr.page_list, node) {
        if (!strcmp(id, p_node->page.id)) {
            p_node->obj = p_nav->obj;
            tuya_gui_mutex_unlock();
            return OPRT_OK;
        }
    }

    tuya_gui_mutex_unlock();
    return OPRT_NOT_FOUND;
}

STATIC OPERATE_RET __nav_del_page(TUYA_NAV_PAGE_S *p_nav)
{
    TUYA_CHECK_NULL_RETURN(p_nav, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(p_nav->obj, OPRT_INVALID_PARM);

    tuya_gui_mutex_lock();
    if (p_nav->page.page_stop_cb) {
        p_nav->page.page_stop_cb(p_nav->obj);
    }

    lv_obj_del_async(p_nav->obj);

    TUYA_PAGE_PRIV_CTX_S *p_ctx = (TUYA_PAGE_PRIV_CTX_S *)p_nav->priv_ctx;
    if(TRUE == p_ctx->is_hor) {
        nav_mgr.hor.hor_num --;
    }
    
    tuya_gui_mutex_unlock();

    p_nav->obj = NULL;
    tuya_gui_nav_set_page_obj(p_nav->page.id, p_nav);
    PR_DEBUG("delete page: %s", p_nav->page.id);
    return OPRT_OK;
}

/***********************************************************
*  Function: gui_nav_page_start_unlock
*  Desc:     page start，函数内不加锁
*  Input:    hor_load: hor page是否load以及set tile id，默认TRUE
*  Return:   OPRT_OK: success  Other: fail

*  page start时会直接隐藏上一个page：
    1. 隐藏sys/top页面 
    2. 解决上一个create时创建的页面，下拉时会触发上一个页面的点击事件
    3. tabview的页面不需要隐藏
    4. 暂不考虑TY_PAGE_ANIM_TP_IN/TY_PAGE_ANIM_TP_OUT(帧率难满足)
***********************************************************/
STATIC OPERATE_RET gui_nav_page_start_unlock(TUYA_NAV_PAGE_S *p_nav,
                                             VOID *start_param, UINT_T param_len, BOOL_T hor_load,
                                             TY_PAGE_ANIM_TP_E anim_type, UINT_T anim_time)
{
    TUYA_CHECK_NULL_RETURN(p_nav, OPRT_INVALID_PARM);

    OPERATE_RET ret = OPRT_OK;
    TUYA_PAGE_PRIV_CTX_S *p_ctx = (TUYA_PAGE_PRIV_CTX_S *)p_nav->priv_ctx;
    BOOL_T curr_is_hor = tuya_page_is_hor(nav_mgr.curr_page_id);

    TUYA_NAV_PAGE_S last_nav = {0x0};
    ret = tuya_gui_nav_page_get(nav_mgr.curr_page_id, &last_nav);
    if (OPRT_OK == ret) {
        if (TY_PAGE_ANIM_TP_NONE == anim_type || TY_PAGE_ANIM_TP_ZOOM == anim_type) {
            if (last_nav.page.page_stop_cb) {
                last_nav.page.page_stop_cb(last_nav.obj);
            }
        } 

        if (!curr_is_hor) {
            lv_obj_add_flag(last_nav.obj, LV_OBJ_FLAG_HIDDEN);
        }
    }

    tuya_gui_nav_anim_clear();
    __page_start_check_tabview(p_nav);

    PR_DEBUG("start nav page: %s type: %d hor_idx: %d anim_type: %d, anim_time: %d", 
        p_nav->page.id, p_nav->page.type, p_nav->hor_idx, anim_type, anim_time);

    lv_obj_t *obj = p_nav->obj;
    if (p_ctx->is_hor) {
        obj = nav_mgr.hor.hor_tabview->obj;
        
        if (!curr_is_hor || hor_load) {
            lv_obj_set_tile_id(obj, p_nav->hor_idx, 0, FALSE);
        }

    } else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }

    tuya_gui_nav_set_page_obj(p_nav->page.id, p_nav);
    tuya_gui_history_push(p_nav->page.type, p_nav->page.id);
    snprintf(nav_mgr.curr_page_id, sizeof(nav_mgr.curr_page_id), "%s", p_nav->page.id);

    SYS_TICK_T start_tick = uni_time_get_posix_ms();
    if (TY_PAGE_ANIM_TP_ZOOM == anim_type) {
        lv_scr_load(obj);
        if (p_nav->page.page_start_cb) {
            p_nav->page.page_start_cb(p_nav->obj, start_param, param_len);
        }

        tuya_gui_nav_anim_start(obj, &last_nav, anim_type, anim_time);

    } else if (TY_PAGE_ANIM_TP_NONE == anim_type) {
        lv_scr_load(obj);
        if (p_nav->page.page_start_cb) {
            p_nav->page.page_start_cb(p_nav->obj, start_param, param_len);
        }

    } else {
        tuya_gui_nav_anim_start(obj, &last_nav, anim_type, anim_time);
        if (p_nav->page.page_start_cb) {
            p_nav->page.page_start_cb(p_nav->obj, start_param, param_len);
        }
    }

    SYS_TICK_T load_tick = uni_time_get_posix_ms()- start_tick;
    PR_DEBUG("page start use time: %llu id: %s", load_tick, p_nav->page.id);
    if(load_tick > TUYA_GUI_PAGE_LOAD_SLOW_TIME) {
        TUYA_GUI_ERR_RPT_PAGE_LOAD_SLOW(p_nav->page.id);
    }
    return OPRT_OK;
}

STATIC VOID __page_start_check_tabview(TUYA_NAV_PAGE_S *p_new_page)
{
    OPERATE_RET ret = OPRT_OK;
    TUYA_NAV_PAGE_S *hor_tabview = nav_mgr.hor.hor_tabview;
    TUYA_NAV_PAGE_S curr_nav = {0x0};
    TUYA_PAGE_PRIV_CTX_S *p_curr_ctx = NULL;

    if(NULL == p_new_page) {
        PR_ERR("invalid param");
        return;
    }
    ret = tuya_gui_nav_page_get(nav_mgr.curr_page_id, &curr_nav);
    if (OPRT_OK != ret) {
        PR_ERR("not find current pos: %s", nav_mgr.curr_page_id);
        return;
    }

    p_curr_ctx = (TUYA_PAGE_PRIV_CTX_S *)curr_nav.priv_ctx;

    TUYA_PAGE_PRIV_CTX_S *p_ctx = (TUYA_PAGE_PRIV_CTX_S *)p_new_page->priv_ctx;
    if ((NULL == p_curr_ctx || !p_curr_ctx->is_hor) && p_ctx->is_hor) {   //system tabview
        if (hor_tabview->page.page_start_cb) {
            tuya_gui_call_atom(hor_tabview->page.page_start_cb(hor_tabview->obj, NULL, 0););
        }

    } else if (p_curr_ctx && p_curr_ctx->is_hor && !p_ctx->is_hor) {
        if (hor_tabview->page.page_stop_cb) {
            tuya_gui_call_atom(hor_tabview->page.page_stop_cb(hor_tabview->obj););
        }
    }
}

OPERATE_RET tuya_gui_navigate_start()
{
    memset(&nav_mgr, 0x0, sizeof(TUYA_NAV_MGR_S));
    // 默认left to right 方向
    nav_mgr.dir = LV_BASE_DIR_LTR;
    return gui_gesture_start();
}

OPERATE_RET tuya_gui_add_sys_tabview(CHAR_T *id, TY_PAGE_INIT_CB init_cb)
{
    TUYA_NAV_PAGE_S *p_nav = NULL;

    if (nav_mgr.hor.hor_tabview) {
        PR_ERR("hor system tabview already exist");
        return OPRT_COM_ERROR;
    }

    NEW_SLIST_NODE(TUYA_NAV_PAGE_S, p_nav);
    TUYA_CHECK_NULL_RETURN(p_nav, OPRT_MALLOC_FAILED);

    TUYA_PAGE_PRIV_CTX_S *p_ctx = (TUYA_PAGE_PRIV_CTX_S *)Malloc(sizeof(TUYA_PAGE_PRIV_CTX_S));
    if (NULL == p_ctx) {
        Free(p_nav);
        PR_ERR("malloc error");
        return OPRT_MALLOC_FAILED;
    }

    memset(p_nav, 0x0,sizeof(TUYA_NAV_PAGE_S));
    memset(p_ctx, 0x0,sizeof(TUYA_PAGE_PRIV_CTX_S));
    p_nav->priv_ctx = (TY_PAGE_PRIV_CTX)p_ctx;

    if (init_cb) {
        init_cb(&p_nav->page);
    }

    tuya_gui_mutex_lock();
    p_nav->obj = lv_tileview_create(NULL);
    lv_obj_add_flag(p_nav->obj, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scrollbar_mode(p_nav->obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(p_nav->obj, __tabview_event, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(p_nav->obj, gui_gesture_screen_event, LV_EVENT_ALL, NULL);

    if (p_nav->page.page_create_cb) {
        p_nav->page.page_create_cb(p_nav->obj);
    }
    tuya_gui_mutex_unlock();

    nav_mgr.hor.hor_tabview = p_nav;
    PR_DEBUG("add hor system tabview");
    return OPRT_OK;
}

STATIC OPERATE_RET __nav_page_init(TUYA_NAV_PAGE_S *p_nav, TY_PAGE_INIT_CB init_cb)
{
    TUYA_CHECK_NULL_RETURN(p_nav, OPRT_INVALID_PARM);
    init_cb(&p_nav->page);

    TUYA_PAGE_PRIV_CTX_S *p_ctx = (TUYA_PAGE_PRIV_CTX_S *)p_nav->priv_ctx;
    if (p_ctx->is_hor) {
        if (NULL == nav_mgr.hor.hor_tabview) {
            PR_DEBUG("auto create hor system tabview");
            tuya_gui_add_sys_tabview("sys_tabview", NULL);
        }

        p_nav->obj = lv_tileview_add_tile(nav_mgr.hor.hor_tabview->obj, nav_mgr.hor.hor_num, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
        p_nav->hor_idx = nav_mgr.hor.hor_num ++;
        lv_obj_set_style_bg_opa(p_nav->obj, LV_OPA_0, 0);

    } else if (TY_PAGE_LAYER_DEF == p_ctx->layer) {
        p_nav->obj = lv_obj_create(NULL);
        lv_obj_add_event_cb(p_nav->obj, gui_gesture_screen_event, LV_EVENT_ALL, NULL);

    } else if (TY_PAGE_LAYER_TOP == p_ctx->layer || TY_PAGE_LAYER_SYS == p_ctx->layer) {
        if (TY_PAGE_LAYER_TOP == p_ctx->layer) {
            p_nav->obj = lv_obj_create(lv_layer_top());
        } else {
            p_nav->obj = lv_obj_create(lv_layer_sys());
        }
        
        lv_obj_add_flag(p_nav->obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(lv_obj_get_parent(p_nav->obj), gui_gesture_screen_event, LV_EVENT_ALL, NULL);
    }

    if (p_nav->obj) {
        lv_obj_set_user_data(p_nav->obj, (void *)&p_nav->page);
        gui_base_page_init(p_nav->obj);
        lv_obj_set_style_base_dir(p_nav->obj, nav_mgr.dir, 0);

        if (p_nav->page.page_create_cb) {
            p_nav->page.page_create_cb(p_nav->obj);
        }
    }

    return OPRT_OK;
}

OPERATE_RET tuya_gui_nav_page_add(CHAR_T *id, TY_PAGE_INIT_CB init_cb)
{
    TUYA_NAV_PAGE_S *p_nav = NULL;
    OPERATE_RET rt = OPRT_OK;

    if (NULL == id || NULL == init_cb) {
        PR_ERR("invalld param");
        return OPRT_INVALID_PARM;
    }

    NEW_SLIST_NODE(TUYA_NAV_PAGE_S, p_nav);
    TUYA_CHECK_NULL_RETURN(p_nav, OPRT_MALLOC_FAILED);

    TUYA_PAGE_PRIV_CTX_S *p_ctx = (TUYA_PAGE_PRIV_CTX_S *)Malloc(sizeof(TUYA_PAGE_PRIV_CTX_S));
    if (NULL == p_ctx) {
        Free(p_nav);
        PR_ERR("malloc error");
        return OPRT_MALLOC_FAILED;
    }

    memset(p_nav, 0x0,sizeof(TUYA_NAV_PAGE_S));
    memset(p_ctx, 0x0,sizeof(TUYA_PAGE_PRIV_CTX_S));
    p_ctx->is_keep = TRUE;      //默认保存上次start page的入参
    snprintf(p_nav->page.id, sizeof(p_nav->page.id), "%s", id);
    p_nav->priv_ctx = (TY_PAGE_PRIV_CTX)p_ctx;

    tuya_gui_mutex_lock();
    tuya_slist_add_head(&nav_mgr.page_list, &(p_nav->node));
    nav_mgr.page_num ++;
    PR_DEBUG("page add, id: %s type: %d, page_num:%d", id, p_nav->page.type, nav_mgr.page_num);

    rt = __nav_page_init(p_nav, init_cb);
    tuya_gui_mutex_unlock();
    return rt;
}

TUYA_NAV_PAGE_S * tuya_gui_nav_page_get_unlock(CONST CHAR_T *id)
{
    TUYA_NAV_PAGE_S *p_node = NULL;
    SLIST_HEAD *pos = NULL, *next = NULL;

    TUYA_CHECK_NULL_RETURN(id, NULL);
    SLIST_FOR_EACH_ENTRY_SAFE(p_node, TUYA_NAV_PAGE_S, pos, next, &nav_mgr.page_list, node) {
        if (!strcmp(id, p_node->page.id)) {
            return p_node;
        }
    }

    return NULL;
}

OPERATE_RET tuya_gui_nav_page_get(CHAR_T *id, TUYA_NAV_PAGE_S *nav)
{
    if (NULL == id || NULL == nav) {
        PR_ERR("invalid param");
        return OPRT_INVALID_PARM;
    }

    tuya_gui_mutex_lock();
    TUYA_NAV_PAGE_S *p_node = tuya_gui_nav_page_get_unlock(id);
    if (NULL == p_node) {
        PR_ERR("id: %s not find", id);
        tuya_gui_mutex_unlock();
        return OPRT_NOT_FOUND;
    }

    memcpy(nav, p_node, sizeof(TUYA_NAV_PAGE_S));
    tuya_gui_mutex_unlock();
    return OPRT_OK;
}

STATIC OPERATE_RET tuya_gui_nav_hor_page_get(UINT16_T tab_idx, TUYA_NAV_PAGE_S *nav)
{
    TUYA_NAV_PAGE_S *p_node = NULL;
    SLIST_HEAD *pos = NULL, *next = NULL;

    TUYA_CHECK_NULL_RETURN(nav, OPRT_INVALID_PARM);
    tuya_gui_mutex_lock();
    SLIST_FOR_EACH_ENTRY_SAFE(p_node, TUYA_NAV_PAGE_S, pos, next, &nav_mgr.page_list, node) {
        TUYA_PAGE_PRIV_CTX_S *p_ctx = (TUYA_PAGE_PRIV_CTX_S *)p_node->priv_ctx;
        if (p_ctx->is_hor && tab_idx == p_node->hor_idx) {
            memcpy(nav, p_node, sizeof(TUYA_NAV_PAGE_S));
            tuya_gui_mutex_unlock();
            return OPRT_OK;
        }
    }

    tuya_gui_mutex_unlock();
    return OPRT_NOT_FOUND;
}


OPERATE_RET tuya_gui_nav_obj_get(CHAR_T *id, lv_obj_t **obj)
{
    if (NULL == id) {
        PR_ERR("invalid param");
        return OPRT_INVALID_PARM;
    }

    tuya_gui_mutex_lock();
    TUYA_NAV_PAGE_S *p_node = tuya_gui_nav_page_get_unlock(id);
    if (NULL == p_node) {
        PR_ERR("id: %s not find", id);
        tuya_gui_mutex_unlock();
        return OPRT_NOT_FOUND;
    }

    *obj = p_node->obj;
    tuya_gui_mutex_unlock();
    return OPRT_OK;
}

STATIC OPERATE_RET gui_nav_hor_page_get_next(CHAR_T *id, TUYA_NAV_PAGE_S *next_page)
{
    TUYA_NAV_PAGE_S *p_node = NULL;
    SLIST_HEAD *pos = NULL, *next = NULL;
    BOOL_T find = FALSE;

    if (NULL == id || NULL == next_page) {
        PR_ERR("invalid param");
        return OPRT_INVALID_PARM;
    }

    tuya_gui_mutex_lock();
    SLIST_FOR_EACH_ENTRY_SAFE(p_node, TUYA_NAV_PAGE_S, pos, next, &nav_mgr.page_list, node) {
        TUYA_PAGE_PRIV_CTX_S *p_ctx = (TUYA_PAGE_PRIV_CTX_S *)p_node->priv_ctx;
        if (!find && !strcmp(id, p_node->page.id)) {
            find = TRUE;

        } else if (find && p_ctx->is_hor) {
            memcpy(next_page, p_node, sizeof(TUYA_NAV_PAGE_S));
            tuya_gui_mutex_unlock();
            return OPRT_OK;
        }
    }

    tuya_gui_mutex_unlock();
    return OPRT_NOT_FOUND;
}

STATIC OPERATE_RET gui_nav_hor_page_get_prev(CHAR_T *id, TUYA_NAV_PAGE_S *prev_page)
{
    TUYA_NAV_PAGE_S *p_node = NULL, *p_prev = NULL;
    SLIST_HEAD *pos = NULL, *next = NULL;

    if (NULL == id || NULL == prev_page) {
        PR_ERR("invalid param");
        return OPRT_INVALID_PARM;
    }

    tuya_gui_mutex_lock();
    SLIST_FOR_EACH_ENTRY_SAFE(p_node, TUYA_NAV_PAGE_S, pos, next, &nav_mgr.page_list, node) {
        TUYA_PAGE_PRIV_CTX_S *p_ctx = (TUYA_PAGE_PRIV_CTX_S *)p_node->priv_ctx;
        if (!strcmp(id, p_node->page.id)) {
            if (NULL == p_prev) {
                PR_DEBUG("curr page is first");
                break;
            }

            memcpy(prev_page, p_prev, sizeof(TUYA_NAV_PAGE_S));
            tuya_gui_mutex_unlock();
            return OPRT_OK;
        }

        if (p_ctx->is_hor) {
            p_prev = p_node;
        }
    }

    tuya_gui_mutex_unlock();
    return OPRT_NOT_FOUND;
}

OPERATE_RET tuya_gui_goto_page_anim(CHAR_T *id, VOID *start_param, UINT_T param_len, TY_PAGE_ANIM_TP_E anim_type, UINT_T anim_time)
{
    TUYA_NAV_PAGE_S *p_node = NULL;
    SLIST_HEAD *pos = NULL, *next = NULL;
    OPERATE_RET rt = OPRT_OK;

    if ((NULL == id) || (0 == strlen(id))) {
        PR_ERR("invalld param");
        return OPRT_INVALID_PARM;
    }

    tuya_gui_mutex_lock();
    SLIST_FOR_EACH_ENTRY_SAFE(p_node, TUYA_NAV_PAGE_S, pos, next, &nav_mgr.page_list, node) {
        if (!strcmp(id, p_node->page.id)) {
            if (p_node->start_param) {
                Free(p_node->start_param);
                p_node->start_param = NULL;
                p_node->param_len = 0;
            }

            TUYA_PAGE_PRIV_CTX_S *p_ctx = (TUYA_PAGE_PRIV_CTX_S *)p_node->priv_ctx;
            if (p_ctx->is_keep && start_param && param_len > 0) {
                p_node->start_param = (VOID *)Malloc(param_len + 1); /*fix: 防止拷贝时无结束符，导致后期使用该参数时堆溢出*/
                if (NULL == p_node->start_param) {
                    PR_ERR("malloc error");
                    tuya_gui_mutex_unlock();
                    TUYA_GUI_ERR_RPT_PAGE_LOAD_FAILED(id);
                    return OPRT_MALLOC_FAILED;
                }

                memset(p_node->start_param, 0x0, param_len + 1);
                memcpy(p_node->start_param, start_param, param_len);
                p_node->param_len = param_len;
            }

            TUYA_NAV_PAGE_S nav = {0x0};
            memcpy(&nav, p_node, sizeof(TUYA_NAV_PAGE_S));
            rt = gui_nav_page_start_unlock(&nav, start_param, param_len, TRUE, anim_type, anim_time);
            tuya_gui_mutex_unlock();

            if (OPRT_OK != rt) {
                PR_ERR("start page: %s error: %d", id, rt);
                TUYA_GUI_ERR_RPT_PAGE_LOAD_FAILED(id);
            }
            return rt;
        }
    }
    tuya_gui_mutex_unlock();
    PR_DEBUG("not found: %s", id);
    return OPRT_NOT_FOUND;
}

OPERATE_RET tuya_gui_goto_page(CHAR_T *id, VOID *start_param, UINT_T param_len)
{
    return tuya_gui_goto_page_anim(id, start_param, param_len, TY_PAGE_ANIM_TP_NONE, 0);
}

OPERATE_RET gui_nav_set_top_gesture(CHAR_T *id)
{
    TUYA_CHECK_NULL_RETURN(id, OPRT_INVALID_PARM);
    PR_DEBUG("navigate set top gesture: %s", id);
    snprintf(nav_mgr.top_gesture, sizeof(nav_mgr.top_gesture), "%s", id);
    return OPRT_OK;
}

OPERATE_RET gui_nav_get_top_gesture(CHAR_T *id, UINT_T len)
{
    TUYA_CHECK_NULL_RETURN(id, OPRT_INVALID_PARM);
    if(0 >= len){
        return OPRT_INVALID_PARM;
    }

    snprintf(id, len, "%s", nav_mgr.top_gesture);
    return OPRT_OK;    
}

OPERATE_RET tuya_gui_page_delete(CHAR_T *id)
{
    TUYA_CHECK_NULL_RETURN(id, OPRT_INVALID_PARM);
    TUYA_NAV_PAGE_S nav = {0x0};
    OPERATE_RET ret = tuya_gui_nav_page_get(id, &nav);
    if (OPRT_OK != ret) {
        PR_ERR("get nav: %s error: %d", id, ret);
        return ret;
    }

    return __nav_del_page(&nav);
}

STATIC OPERATE_RET __gui_page_reload_anim(CHAR_T *id, TY_PAGE_ANIM_TP_E anim_type, UINT_T anim_time)
{
    TUYA_NAV_PAGE_S nav = {0x0};

    tuya_gui_mutex_lock();
    OPERATE_RET ret = tuya_gui_nav_page_get(id, &nav);
    if (OPRT_OK != ret) {
        PR_ERR("got hor nav: %s error: %d", id, ret);
        tuya_gui_mutex_unlock();
        return ret;
    }

    ret = gui_nav_page_start_unlock(&nav, nav.start_param, nav.param_len, TRUE, anim_type, anim_time);
    tuya_gui_mutex_unlock();
    return ret;
}

OPERATE_RET tuya_gui_page_reload(CHAR_T *id)
{
    return __gui_page_reload_anim(id, TY_PAGE_ANIM_TP_NONE, 0);
}

OPERATE_RET tuya_gui_page_reload_anim(CHAR_T *id, TY_PAGE_ANIM_TP_E anim_type, UINT_T anim_time)
{
    return __gui_page_reload_anim(id, anim_type, anim_time);
}

BOOL_T tuya_page_get_forbid_cover(CHAR_T *id)
{
    BOOL_T forbid_cover = FALSE;

    tuya_gui_mutex_lock();
    TUYA_NAV_PAGE_S *p_nav = tuya_gui_nav_page_get_unlock(id);
    if (NULL == p_nav) {
        PR_ERR("id: %s not find", id);
        tuya_gui_mutex_unlock();
        return OPRT_NOT_FOUND;
    }

    TUYA_PAGE_PRIV_CTX_S *p_ctx = (TUYA_PAGE_PRIV_CTX_S *)p_nav->priv_ctx;
    forbid_cover = p_ctx->forbid_cover;
    tuya_gui_mutex_unlock();

    return forbid_cover;
}

OPERATE_RET tuya_page_set_forbid_cover(CHAR_T *id, BOOL_T forbid_cover)
{
    TUYA_CHECK_NULL_RETURN(id, OPRT_INVALID_PARM);

    tuya_gui_mutex_lock();
    TUYA_NAV_PAGE_S *p_nav = tuya_gui_nav_page_get_unlock(id);
    if (NULL == p_nav) {
        PR_ERR("id: %s not find", id);
        tuya_gui_mutex_unlock();
        return OPRT_NOT_FOUND;
    }

    TUYA_PAGE_PRIV_CTX_S *p_ctx = (TUYA_PAGE_PRIV_CTX_S *)p_nav->priv_ctx;
    p_ctx->forbid_cover = forbid_cover;
    PR_DEBUG("set page forbid_cover: %d", forbid_cover);
    tuya_gui_mutex_unlock();

    return OPRT_OK;
}

OPERATE_RET tuya_page_set_layer(CHAR_T *id, TY_PAGE_LAYER_E layer)
{
    TUYA_CHECK_NULL_RETURN(id, OPRT_INVALID_PARM);

    tuya_gui_mutex_lock();
    TUYA_NAV_PAGE_S *p_nav = tuya_gui_nav_page_get_unlock(id);
    if (NULL == p_nav) {
        PR_ERR("id: %s not find", id);
        tuya_gui_mutex_unlock();
        return OPRT_NOT_FOUND;
    }

    TUYA_PAGE_PRIV_CTX_S *p_ctx = (TUYA_PAGE_PRIV_CTX_S *)p_nav->priv_ctx;
    p_ctx->layer = layer;
    PR_DEBUG("set page layer: %d", layer);
    tuya_gui_mutex_unlock();

    return OPRT_OK;
}

OPERATE_RET tuya_page_set_hor(CHAR_T *id, BOOL_T is_hor)
{
    TUYA_CHECK_NULL_RETURN(id, OPRT_INVALID_PARM);

    tuya_gui_mutex_lock();
    TUYA_NAV_PAGE_S *p_nav = tuya_gui_nav_page_get_unlock(id);
    if (NULL == p_nav) {
        PR_ERR("id: %s not find", id);
        tuya_gui_mutex_unlock();
        return OPRT_NOT_FOUND;
    }

    TUYA_PAGE_PRIV_CTX_S *p_ctx = (TUYA_PAGE_PRIV_CTX_S *)p_nav->priv_ctx;
    p_ctx->is_hor = is_hor;
    PR_DEBUG("set page is_hor: %d", is_hor);
    tuya_gui_mutex_unlock();

    return OPRT_OK;
}

BOOL_T tuya_page_is_hor(CHAR_T *id)
{
    BOOL_T is_hor = FALSE;
    TUYA_CHECK_NULL_RETURN(id, is_hor);

    tuya_gui_mutex_lock();
    TUYA_NAV_PAGE_S *p_nav = tuya_gui_nav_page_get_unlock(id);
    if (NULL == p_nav) {
        PR_ERR("id: %s not find", id);
        tuya_gui_mutex_unlock();
        return is_hor;
    }

    TUYA_PAGE_PRIV_CTX_S *p_ctx = (TUYA_PAGE_PRIV_CTX_S *)p_nav->priv_ctx;
    is_hor = p_ctx->is_hor;
    tuya_gui_mutex_unlock();

    return is_hor;
}

OPERATE_RET tuya_page_set_gesture_triggered(CHAR_T *id, BOOL_T is_gesture_triggered)
{
    TUYA_CHECK_NULL_RETURN(id, OPRT_INVALID_PARM);

    tuya_gui_mutex_lock();
    TUYA_NAV_PAGE_S *p_nav = tuya_gui_nav_page_get_unlock(id);
    if (NULL == p_nav) {
        PR_ERR("id: %s not find", id);
        tuya_gui_mutex_unlock();
        return OPRT_NOT_FOUND;
    }
    TUYA_PAGE_PRIV_CTX_S *p_ctx = (TUYA_PAGE_PRIV_CTX_S *)p_nav->priv_ctx;
    p_ctx->is_gesture_triggered = is_gesture_triggered;
    PR_DEBUG("set page is_gesture_triggered: %d", is_gesture_triggered);
    tuya_gui_mutex_unlock();

    return OPRT_OK;
}

BOOL_T tuya_page_is_gesture_triggered(CHAR_T *id)
{
    BOOL_T is_gesture_triggered = FALSE;
    TUYA_CHECK_NULL_RETURN(id, is_gesture_triggered);

    tuya_gui_mutex_lock();
    TUYA_NAV_PAGE_S *p_nav = tuya_gui_nav_page_get_unlock(id);
    if (NULL == p_nav) {
        PR_ERR("id: %s not find", id);
        tuya_gui_mutex_unlock();
        return is_gesture_triggered;
    }

    TUYA_PAGE_PRIV_CTX_S *p_ctx = (TUYA_PAGE_PRIV_CTX_S *)p_nav->priv_ctx;
    is_gesture_triggered = p_ctx->is_gesture_triggered;
    tuya_gui_mutex_unlock();

    return is_gesture_triggered;
}

OPERATE_RET tuya_page_set_swipe_back(CHAR_T *id, BOOL_T is_swipe_back)
{
    TUYA_CHECK_NULL_RETURN(id, OPRT_INVALID_PARM);

    tuya_gui_mutex_lock();
    TUYA_NAV_PAGE_S *p_nav = tuya_gui_nav_page_get_unlock(id);
    if (NULL == p_nav) {
        PR_ERR("id: %s not find", id);
        tuya_gui_mutex_unlock();
        return OPRT_NOT_FOUND;
    }
    TUYA_PAGE_PRIV_CTX_S *p_ctx = (TUYA_PAGE_PRIV_CTX_S *)p_nav->priv_ctx;
    p_ctx->is_swipe_back = is_swipe_back;
    PR_DEBUG("set page is_swipe_back: %d", is_swipe_back);
    tuya_gui_mutex_unlock();

    return OPRT_OK;
}

BOOL_T tuya_page_is_swipe_back(CHAR_T *id)
{
    BOOL_T is_swipe_back = FALSE;
    TUYA_CHECK_NULL_RETURN(id, is_swipe_back);

    tuya_gui_mutex_lock();
    TUYA_NAV_PAGE_S *p_nav = tuya_gui_nav_page_get_unlock(id);
    if (NULL == p_nav) {
        PR_ERR("id: %s not find", id);
        tuya_gui_mutex_unlock();
        return is_swipe_back;
    }

    TUYA_PAGE_PRIV_CTX_S *p_ctx = (TUYA_PAGE_PRIV_CTX_S *)p_nav->priv_ctx;
    is_swipe_back = p_ctx->is_swipe_back;
    tuya_gui_mutex_unlock();

    return is_swipe_back;
}

OPERATE_RET tuya_page_set_swipe_back_fn(CHAR_T *id,  TY_SWIPE_BACK_FUNCTION fn)
{
    TUYA_CHECK_NULL_RETURN(id, OPRT_INVALID_PARM);

    tuya_gui_mutex_lock();
    TUYA_NAV_PAGE_S *p_nav = tuya_gui_nav_page_get_unlock(id);
    if (NULL == p_nav) {
        PR_ERR("id: %s not find", id);
        tuya_gui_mutex_unlock();
        return OPRT_NOT_FOUND;
    }

    TUYA_PAGE_PRIV_CTX_S *p_ctx = (TUYA_PAGE_PRIV_CTX_S *)p_nav->priv_ctx;
    p_ctx->back_fn = fn;
    PR_DEBUG("set page backFn: %p", fn);
    tuya_gui_mutex_unlock();

    tuya_page_set_swipe_back(id, TRUE);

    return OPRT_OK;
}

TY_SWIPE_BACK_FUNCTION tuya_page_get_swipe_back_fn(CHAR_T *id)
{
    TY_SWIPE_BACK_FUNCTION fn = NULL;
    TUYA_CHECK_NULL_RETURN(id, fn);

    tuya_gui_mutex_lock();
    TUYA_NAV_PAGE_S *p_nav = tuya_gui_nav_page_get_unlock(id);
    if (NULL == p_nav) {
        PR_ERR("id: %s not find", id);
        tuya_gui_mutex_unlock();
        return fn;
    }

    TUYA_PAGE_PRIV_CTX_S *p_ctx = (TUYA_PAGE_PRIV_CTX_S *)p_nav->priv_ctx;
    fn = p_ctx->back_fn;
    tuya_gui_mutex_unlock();

    return fn;
}

OPERATE_RET tuya_page_set_name(CHAR_T *id, CHAR_T *name)
{
    TUYA_CHECK_NULL_RETURN(id, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(name, OPRT_INVALID_PARM);

    tuya_gui_mutex_lock();
    TUYA_NAV_PAGE_S *p_nav = tuya_gui_nav_page_get_unlock(id);
    if (NULL == p_nav) {
        PR_ERR("id: %s not find", id);
        tuya_gui_mutex_unlock();
        return OPRT_NOT_FOUND;
    }

    TUYA_PAGE_PRIV_CTX_S *p_ctx = (TUYA_PAGE_PRIV_CTX_S *)p_nav->priv_ctx;
    p_ctx->page_name = _mm_strdup(name);
    tuya_gui_mutex_unlock();

    PR_DEBUG("set page tab name: %s", name);
    return OPRT_OK;
}

OPERATE_RET tuya_page_set_param_keep(CHAR_T *id, BOOL_T is_keep)
{
    TUYA_CHECK_NULL_RETURN(id, OPRT_INVALID_PARM);

    tuya_gui_mutex_lock();
    TUYA_NAV_PAGE_S *p_nav = tuya_gui_nav_page_get_unlock(id);
    if (NULL == p_nav) {
        PR_ERR("id: %s not find", id);
        tuya_gui_mutex_unlock();
        return OPRT_NOT_FOUND;
    }

    TUYA_PAGE_PRIV_CTX_S *p_ctx = (TUYA_PAGE_PRIV_CTX_S *)p_nav->priv_ctx;
    p_ctx->is_keep = is_keep;
    PR_DEBUG("set page is_keep: %d", is_keep);
    tuya_gui_mutex_unlock();

    return OPRT_OK;
}

OPERATE_RET tuya_page_set_type(CONST CHAR_T *id, TY_PAGE_TP_E type)
{
    TUYA_CHECK_NULL_RETURN(id, OPRT_INVALID_PARM);

    tuya_gui_mutex_lock();
    TUYA_NAV_PAGE_S *p_nav = tuya_gui_nav_page_get_unlock(id);
    if (NULL == p_nav) {
        PR_ERR("id: %s not find", id);
        tuya_gui_mutex_unlock();
        return OPRT_NOT_FOUND;
    }

    p_nav->page.type = type;
    tuya_gui_mutex_unlock();

    PR_DEBUG("set page type: %d", type);
    return OPRT_OK;
}

OPERATE_RET tuya_page_set_create_cb(CONST CHAR_T *id, TY_PAGE_CREATE_CB cb)
{
    TUYA_CHECK_NULL_RETURN(id, OPRT_INVALID_PARM);

    tuya_gui_mutex_lock();
    TUYA_NAV_PAGE_S *p_nav = tuya_gui_nav_page_get_unlock(id);
    if (NULL == p_nav) {
        PR_ERR("id: %s not find", id);
        tuya_gui_mutex_unlock();
        return OPRT_NOT_FOUND;
    }

    p_nav->page.page_create_cb = cb;
    PR_DEBUG("set page create_cb");
    tuya_gui_mutex_unlock();

    return OPRT_OK;
}

OPERATE_RET tuya_page_set_start_cb(CONST CHAR_T *id, TY_PAGE_START_CB cb)
{
    TUYA_CHECK_NULL_RETURN(id, OPRT_INVALID_PARM);

    tuya_gui_mutex_lock();
    TUYA_NAV_PAGE_S *p_nav = tuya_gui_nav_page_get_unlock(id);
    if (NULL == p_nav) {
        PR_ERR("id: %s not find", id);
        tuya_gui_mutex_unlock();
        return OPRT_NOT_FOUND;
    }

    p_nav->page.page_start_cb = cb;
    tuya_gui_mutex_unlock();

    return OPRT_OK;
}

OPERATE_RET tuya_page_set_stop_cb(CONST CHAR_T *id, TY_PAGE_STOP_CB cb)
{
    TUYA_CHECK_NULL_RETURN(id, OPRT_INVALID_PARM);

    tuya_gui_mutex_lock();
    TUYA_NAV_PAGE_S *p_nav = tuya_gui_nav_page_get_unlock(id);
    if (NULL == p_nav) {
        PR_ERR("id: %s not find", id);
        tuya_gui_mutex_unlock();
        return OPRT_NOT_FOUND;
    }

    p_nav->page.page_stop_cb = cb;
    tuya_gui_mutex_unlock();

    return OPRT_OK;
}

OPERATE_RET tuya_page_set_hor_tab_hidden(BOOL_T hidden) 
{
    tuya_gui_mutex_lock();
    lv_obj_t *obj = nav_mgr.hor.hor_tabview->obj;
    if (NULL == obj) {
        PR_ERR("hor tab obj NULL");
        tuya_gui_mutex_unlock();
        return OPRT_COM_ERROR;
    }

    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }

    tuya_gui_mutex_unlock();
    PR_DEBUG("set hor tab hidden: %d", hidden);
    return OPRT_OK;
}

VOID tuya_gui_set_base_dir(lv_base_dir_t dir)
{
    if (dir == nav_mgr.dir) {
        PR_DEBUG("%s dir(%d) is same", __func__, dir);
        return;
    }

    TUYA_NAV_PAGE_S *p_node = NULL;
    SLIST_HEAD *pos = NULL, *next = NULL;
    nav_mgr.dir = dir;
    tuya_gui_mutex_lock();
    SLIST_FOR_EACH_ENTRY_SAFE(p_node, TUYA_NAV_PAGE_S, pos, next, &nav_mgr.page_list, node) {
        if (p_node->obj) {
            lv_obj_set_style_base_dir(p_node->obj, dir, 0);
        }
    }
    
    // 给lv_layer_sys设置方向
    lv_obj_set_style_base_dir(lv_layer_sys(), dir, 0);
    tuya_gui_mutex_unlock();
}

OPERATE_RET gui_nav_get_current_page_id(CHAR_T *id, int len)
{
    TUYA_CHECK_NULL_RETURN(id, OPRT_INVALID_PARM);
    if(0 >= len){
        return OPRT_INVALID_PARM;
    }

    snprintf(id, len, "%s", nav_mgr.curr_page_id);
    return OPRT_OK;
}
OPERATE_RET gui_nav_set_hor_idx(CHAR_T *id, UINT8_T hor_idx)
{
    TUYA_NAV_PAGE_S *p_node = NULL;
    SLIST_HEAD *pos = NULL, *next = NULL;

    TUYA_CHECK_NULL_RETURN(id, OPRT_INVALID_PARM);

    tuya_gui_mutex_lock();
    SLIST_FOR_EACH_ENTRY_SAFE(p_node, TUYA_NAV_PAGE_S, pos, next, &nav_mgr.page_list, node) {
        TUYA_PAGE_PRIV_CTX_S *p_ctx = (TUYA_PAGE_PRIV_CTX_S *)p_node->priv_ctx;
        if(FALSE == p_ctx->is_hor){
            continue;
        }

        if (!strcmp(id, p_node->page.id)) {
            p_node->hor_idx = hor_idx;
            lv_obj_set_pos(p_node->obj, hor_idx * lv_obj_get_content_width(p_node->obj),lv_obj_get_y(p_node->obj));
            tuya_gui_mutex_unlock();
            return OPRT_OK;
        }
    }

    tuya_gui_mutex_unlock();
    return OPRT_NOT_FOUND;
}
