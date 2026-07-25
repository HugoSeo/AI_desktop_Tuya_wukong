#include "tuya_gui_history.h"
#include "tuya_gui_navigate.h"
#include "tuya_gui_nav_internal.h"

typedef struct {
    CHAR_T          id[GUI_PAGE_ID_MAX];
    TY_PAGE_TP_E    type;
    SLIST_HEAD      node;    

} TUYA_HISTORY_PAGE_S;

typedef struct {
    SLIST_HEAD      list;
    UINT16_T        num;
    MUTEX_HANDLE    mutex;

} TUYA_HISTORY_MGR_S;

STATIC TUYA_HISTORY_MGR_S history_mgr = {0x0};

OPERATE_RET tuya_gui_history_start()
{
    OPERATE_RET rt = OPRT_OK;
    memset(&history_mgr, 0x0, sizeof(TUYA_HISTORY_MGR_S));
    TUYA_CALL_ERR_RETURN(tuya_hal_mutex_create_init(&history_mgr.mutex));
    return rt;
}

STATIC OPERATE_RET tuya_gui_history_del(CHAR_T *id)
{
    TUYA_HISTORY_PAGE_S *p_node = NULL;
    SLIST_HEAD *pos = NULL, *next = NULL;

    tuya_hal_mutex_lock(history_mgr.mutex); 
    SLIST_FOR_EACH_ENTRY_SAFE(p_node, TUYA_HISTORY_PAGE_S, pos, next, &history_mgr.list, node) {   
        if (0 == strcmp(id, p_node->id)) {
            history_mgr.num--;
            PR_DEBUG("del %s num: %d", p_node->id, history_mgr.num);
            tuya_slist_del(&history_mgr.list, &p_node->node);
            Free(p_node);
        }
    }

    tuya_hal_mutex_unlock(history_mgr.mutex);
    return OPRT_OK;
}

//删除近期history中当前 到 此id的所有记录
STATIC OPERATE_RET __gui_history_del_recent(CHAR_T *id)
{
    TUYA_HISTORY_PAGE_S *p_node = NULL;
    SLIST_HEAD *pos = NULL, *next = NULL;
    BOOL_T find = FALSE;

    tuya_hal_mutex_lock(history_mgr.mutex); 
    SLIST_FOR_EACH_ENTRY_SAFE(p_node, TUYA_HISTORY_PAGE_S, pos, next, &history_mgr.list, node) {   
        if (0 == strcmp(id, p_node->id)) {
            find = TRUE;
        }

        history_mgr.num--;
        PR_DEBUG("del recent %s num: %d", p_node->id, history_mgr.num);
        tuya_slist_del(&history_mgr.list, &p_node->node);
        Free(p_node);

        if (find) {
            break;
        }
    }

    tuya_hal_mutex_unlock(history_mgr.mutex);
    return OPRT_OK;
}

OPERATE_RET tuya_gui_history_push(TY_PAGE_TP_E type, CHAR_T *page_id)
{
    TUYA_CHECK_NULL_RETURN(page_id, OPRT_INVALID_PARM);

    tuya_gui_history_del(page_id);
    TUYA_HISTORY_PAGE_S *p_page = NULL;
    NEW_SLIST_NODE(TUYA_HISTORY_PAGE_S, p_page);
    TUYA_CHECK_NULL_RETURN(p_page, OPRT_MALLOC_FAILED);
    memset(p_page, 0x0, sizeof(TUYA_HISTORY_PAGE_S));

    p_page->type = type;
    snprintf(p_page->id, sizeof(p_page->id), "%s", page_id);

    tuya_hal_mutex_lock(history_mgr.mutex); 
    tuya_slist_add_head(&history_mgr.list, &(p_page->node));
    history_mgr.num ++;
    PR_DEBUG("history add %s type: %d, num: %d", page_id, type, history_mgr.num);
    tuya_hal_mutex_unlock(history_mgr.mutex); 

    return OPRT_OK;
}

OPERATE_RET tuya_gui_history_get_current(CHAR_T *id)
{
    TUYA_HISTORY_PAGE_S *p_node = NULL;
    SLIST_HEAD *pos = NULL, *next = NULL;

    tuya_hal_mutex_lock(history_mgr.mutex); 
    SLIST_FOR_EACH_ENTRY_SAFE(p_node, TUYA_HISTORY_PAGE_S, pos, next, &history_mgr.list, node) {   
        strcpy(id, p_node->id);
        tuya_hal_mutex_unlock(history_mgr.mutex); 
        return OPRT_OK;
    }


    tuya_hal_mutex_unlock(history_mgr.mutex);
    PR_DEBUG("not find current");
    return OPRT_NOT_FOUND;
}

TY_PAGE_TP_E tuya_gui_history_get_current_type()
{
    TUYA_HISTORY_PAGE_S *p_node = NULL;
    SLIST_HEAD *pos = NULL, *next = NULL;
    TY_PAGE_TP_E type = 0x0;

    tuya_hal_mutex_lock(history_mgr.mutex);
    SLIST_FOR_EACH_ENTRY_SAFE(p_node, TUYA_HISTORY_PAGE_S, pos, next, &history_mgr.list, node) {
        type = p_node->type;
        tuya_hal_mutex_unlock(history_mgr.mutex);
        return type;
    }

    tuya_hal_mutex_unlock(history_mgr.mutex);
    PR_DEBUG("not find current");
    return type;
}

STATIC OPERATE_RET __gui_history_get_last(CHAR_T *id, TY_PAGE_TP_E type)
{
    TUYA_HISTORY_PAGE_S *p_node = NULL;
    SLIST_HEAD *pos = NULL, *next = NULL;
    CHAR_T current[GUI_PAGE_ID_MAX] = {0x0};
    BOOL_T first = TRUE;

    tuya_hal_mutex_lock(history_mgr.mutex); 
    SLIST_FOR_EACH_ENTRY_SAFE(p_node, TUYA_HISTORY_PAGE_S, pos, next, &history_mgr.list, node) {   
        if (!first && (type & p_node->type) && 0 != strcmp(current, p_node->id)) {
            strcpy(id, p_node->id);
            tuya_hal_mutex_unlock(history_mgr.mutex); 
            PR_DEBUG("get last: %s get type: %d", id, type);
            return OPRT_OK;

        } else if (first) {
            strcpy(current, p_node->id);
            first = FALSE;
        }  
    }

    tuya_hal_mutex_unlock(history_mgr.mutex);
    PR_DEBUG("not find last");
    return OPRT_NOT_FOUND;
}

OPERATE_RET tuya_gui_history_get_special_last(TY_PAGE_TP_E type, CHAR_T *id)
{
    return __gui_history_get_last(id, type);
}

STATIC OPERATE_RET __gui_history_goto_special_last_anim(TY_PAGE_TP_E type, TY_PAGE_ANIM_TP_E anim_type, UINT_T anim_time)
{
    OPERATE_RET rt = OPRT_OK;
    CHAR_T id[GUI_PAGE_ID_MAX] = {0x0};

    TUYA_CALL_ERR_RETURN(__gui_history_get_last(id, type));
    __gui_history_del_recent(id);
    return tuya_gui_page_reload_anim(id, anim_type, anim_time);
}

OPERATE_RET tuya_gui_history_goto_special_last(TY_PAGE_TP_E type)
{
    return __gui_history_goto_special_last_anim(type, TY_PAGE_ANIM_TP_NONE, 0);
}

OPERATE_RET tuya_gui_history_goto_special_last_anim(TY_PAGE_TP_E type, TY_PAGE_ANIM_TP_E anim_type, UINT_T anim_time)
{
    return __gui_history_goto_special_last_anim(type, anim_type, anim_time);
}
