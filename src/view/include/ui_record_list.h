/**
 * @file ui_record_list.h
 * @brief Voice recording list + playback overlay screen API
 * @version 1.0
 * @date 2026-05-13
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __UI_RECORD_LIST_H__
#define __UI_RECORD_LIST_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"
#include "lvgl.h"

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */

/**
 * @brief One recording entry shown in the record list screen
 * @note  Pure display struct, no ownership of dynamic buffers. The list
 *        module deep-copies the contents on ui_record_list_replace_push(),
 *        so the caller does not need to keep the source alive.
 *
 *        transcribe_status: -1 未上传 / 0 处理中 / 1 完成 / 2 失败
 *        See docs/adr/0002-recording-transcribe-status-staging.md.
 *
 *        md5_unavailable: TRUE iff the source REC_ITEM_T::md5 is 16
 *        zero bytes (hash failed at capture time, ADR-0001). Derived,
 *        not persisted. UI uses it to disable the upload button.
 */
typedef struct {
    INT_T    id;
    CHAR_T   name[64];
    CHAR_T   datetime_str[32];
    UINT32_T duration_sec;
    INT_T    transcribe_status;
    BOOL_T   md5_unavailable;
} UI_RECORD_ITEM_T;

/* ---------------------------------------------------------------------------
 * Function declarations
 * --------------------------------------------------------------------------- */

/**
 * @brief Create the record list screen (does NOT load/show it)
 * @return none
 * @note The screen hosts both the scrollable list view and an overlay
 *       playback panel; the playback panel is hidden by default and
 *       becomes visible when a list item is tapped.
 */
VOID_T setup_scr_record_list(VOID_T);

/**
 * @brief Show the record list screen (creates if needed)
 * @return none
 */
VOID_T ui_record_list_show(VOID_T);

/**
 * @brief Hide the record list screen and release per-show resources
 * @return none
 */
VOID_T ui_record_list_hide(VOID_T);

/**
 * @brief Get the record list screen object
 * @return record list screen pointer, NULL if not created
 */
lv_obj_t *ui_record_list_get_scr(VOID_T);

/**
 * @brief Begin replacing the displayed list items
 * @return none
 * @note Releases all currently held nodes immediately. Pair with
 *       ui_record_list_replace_commit() to refresh the screen, even
 *       when no item is pushed (= empty list).
 */
VOID_T ui_record_list_replace_begin(VOID_T);

/**
 * @brief Append one item to the in-progress replacement
 * @param[in] item view fields to copy (must not be NULL)
 * @return OPRT_OK on success, OPRT_MALLOC_FAILED when out of memory,
 *         OPRT_INVALID_PARM when item is NULL
 * @note Caller may free the source after this call returns; the list
 *       module deep-copies into its own node. Items pushed beyond the
 *       internal cap are silently dropped (returns OPRT_OK).
 */
OPERATE_RET ui_record_list_replace_push(CONST UI_RECORD_ITEM_T *item);

/**
 * @brief Commit the in-progress replacement and rebuild the screen
 * @return none
 * @note Safe to call before the screen is created; the list is rebuilt
 *       lazily on the next ui_record_list_show().
 */
VOID_T ui_record_list_replace_commit(VOID_T);

/**
 * @brief Update the upload progress bar on the playback overlay
 * @param[in] percent progress in [0, 100]
 * @return none
 * @note No-op when the record list screen is not visible. The progress
 *       bar container is auto-shown when percent < 100 and auto-hidden
 *       when percent reaches 100 (or is reset to 0).
 */
VOID_T ui_record_list_set_upload_progress(UINT8_T percent);

/**
 * @brief Refresh the transcribe-status badge on the visible playback card
 * @param[in] transcribe_status new status value (-1/0/1/2)
 * @return none
 * @note Only updates when the playback card is currently shown (i.e.
 *       s_record_list_play_item_valid). The list-card badges are
 *       refreshed separately via ui_record_runtime_refresh_ui_list().
 *       Used by the runtime upload pipeline at the 100% edge to flip
 *       -1 → 0 visibly while the user is still looking at the play card.
 */
VOID_T ui_record_list_set_play_status(INT_T transcribe_status);

#ifdef __cplusplus
}
#endif

#endif /* __UI_RECORD_LIST_H__ */
