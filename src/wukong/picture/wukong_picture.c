/**
 * @file wukong_picture.c
 * @brief Picture album management implementation.
 *        Wraps image_album / image_album_scan / image_album_thumb to provide
 *        album lifecycle, sequential photo browsing, deletion, and thumbnails.
 * @version 0.1
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "tal_memory.h"
#include "tuya_app_config.h"
#include "uni_log.h"

#include "wukong_ai_agent.h"
#include "wukong_picture.h"

#include "wukong_picture_output.h"

#include "image_album_scan.h"
#include "image_album_thumb.h"

/***********************************************************
************************macro define************************
***********************************************************/

/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef struct {
    IMAGE_ALBUM_HANDLE album_hdl;
    IMAGE_ALBUM_SCAN_HANDLE album_scan_hdl;
    uint32_t scan_img_count;
    /** 1-based index of the picture currently displayed; 0 if unknown / album empty */
    uint32_t cur_view_pos;
    char cur_name[ALBUM_FILENAME_MAX_LEN + 1];
} WUKONG_PICTURE_CTX_T;

/***********************************************************
***********************variable define**********************
***********************************************************/
static WUKONG_PICTURE_CTX_T sg_picture_ctx;

/***********************************************************
***********************function define**********************
***********************************************************/

/**
 * @brief Rebuild scan snapshot after album mutation (delete)
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __wukong_picture_rescan_album(VOID_T)
{
    OPERATE_RET rt;

    if (sg_picture_ctx.album_scan_hdl != NULL) {
        TUYA_CALL_ERR_RETURN(image_album_scan_deinit(sg_picture_ctx.album_scan_hdl));
        sg_picture_ctx.album_scan_hdl = NULL;
    }

    sg_picture_ctx.scan_img_count = 0;
    TUYA_CALL_ERR_RETURN(
        image_album_scan_init(TUYA_PICTURE_ALBUM_NAME, WUKONG_PICTURE_GET_STORAGE_TP, &sg_picture_ctx.album_scan_hdl));
    sg_picture_ctx.scan_img_count = image_album_scan_get_count(sg_picture_ctx.album_scan_hdl);
    return rt;
}

/**
 * @brief Trim album to max image count, deleting oldest images when exceeded
 * @param[in] album_handle album handle
 * @param[in] max_cnt maximum allowed image count
 * @return none
 */
STATIC VOID_T __album_trim_oldest(IMAGE_ALBUM_HANDLE album_handle, UINT32_T max_cnt)
{
    UINT32_T count = 0;

    if (OPRT_OK != image_album_get_committed_count(album_handle, &count)) {
        return;
    }

    while (count > max_cnt) {
        ALBUM_IMAGE_ITEM_T oldest;
        memset(&oldest, 0x00, sizeof(ALBUM_IMAGE_ITEM_T));
        if (OPRT_OK != image_album_get_next_item(album_handle, NULL, WUKONG_PICTURE_GET_STORAGE_TP, &oldest)) {
            break;
        }
        PR_DEBUG("album full(%d/%d), delete oldest: %s", count, max_cnt, oldest.filename);
        image_album_delete(album_handle, oldest.filename);
        count--;
    }
}

/**
 * @brief Initialize the picture album module
 * @return OPRT_OK on success
 */
OPERATE_RET wukong_picture_init(void)
{
    OPERATE_RET rt = OPRT_OK;

    IMAGE_ALBUM_INIT_CFG_T album_init = {
        .storage_mask = WUKONG_PICTURE_ALBUM_STORAGE_MASK,
        .recover_tp = WUKONG_PICTURE_ALBUM_RECOVER_TP,
    };

    TUYA_CALL_ERR_RETURN(image_album_init(TUYA_PICTURE_ALBUM_NAME, &album_init, &sg_picture_ctx.album_hdl));

    TUYA_CALL_ERR_LOG(wukong_picture_output_set_size(TUYA_PICTURE_DEF_OUTPUT_WIDTH, TUYA_PICTURE_DEF_OUTPUT_HEIGHT));

    return rt;
}

/**
 * @brief Get the internal album handle (for use by sibling modules)
 * @return album handle, NULL if not initialized
 */
IMAGE_ALBUM_HANDLE wukong_picture_get_album_handle(void)
{
    return sg_picture_ctx.album_hdl;
}

/**
 * @brief Save a JPEG picture to the album with a timestamp-based filename
 * @param[in] picture JPEG data buffer
 * @param[in] len JPEG data length in bytes
 * @param[out] name filled with the generated filename (may be NULL)
 * @return OPRT_OK on success
 */
OPERATE_RET wukong_picture_save_to_album(uint8_t *picture, uint32_t len, char name[WUKONG_PICTURE_NAME_MAX_LEN + 1])
{
    OPERATE_RET rt = OPRT_OK;
    IMAGE_ALBUM_HANDLE album_handle = wukong_picture_get_album_handle();

    if (NULL == album_handle) {
        PR_ERR("album handle is null");
        return OPRT_COM_ERROR;
    }

    if (NULL == picture || len == 0) {
        PR_ERR("picture:%p, total_len:%d", picture, len);
        return OPRT_INVALID_PARM;
    }

    char filename[WUKONG_PICTURE_NAME_MAX_LEN + 1] = {0};
    SYS_TICK_T timestamp = tal_time_get_posix_ms();
    snprintf(filename, sizeof(filename), "wukong_pic_%llu", timestamp);

    ALBUM_IMAGE_SAVE_INFO_T info = {
        .filename = filename,
        .format = ALBUM_IMAGE_FORMAT_JPEG,
        .file_data = picture,
        .file_size = len,
        .timestamp = timestamp,
    };

    TUYA_CALL_ERR_RETURN(image_album_save(album_handle, &info));

    PR_NOTICE("[pic_chain] album saved, filename:%s, size:%u", filename, len);
    if (name) {
        strncpy(name, info.filename, WUKONG_PICTURE_NAME_MAX_LEN);
    }

    __album_trim_oldest(album_handle, TUYA_PICTURE_ALBUM_MAX_IMAGE_CNT);

    return OPRT_OK;
}

/**
 * @brief Open the album for sequential scan
 * @return OPRT_OK on success, OPRT_COM_ERROR if already open
 */
OPERATE_RET wukong_picture_open_album(void)
{
    OPERATE_RET rt = OPRT_OK;

    if (sg_picture_ctx.album_scan_hdl) {
        PR_ERR("scan not end");
        return OPRT_COM_ERROR;
    }

    TUYA_CALL_ERR_RETURN(
        image_album_scan_init(TUYA_PICTURE_ALBUM_NAME, WUKONG_PICTURE_GET_STORAGE_TP, &sg_picture_ctx.album_scan_hdl));

    sg_picture_ctx.scan_img_count = image_album_scan_get_count(sg_picture_ctx.album_scan_hdl);
    sg_picture_ctx.cur_view_pos = 0;

    return rt;
}

/**
 * @brief Get the number of pictures in the album
 * @return picture count
 */
uint32_t wukong_picture_get_count(void)
{
    uint32_t count = 0;

    if (sg_picture_ctx.album_scan_hdl) {
        count = sg_picture_ctx.scan_img_count;
    } else {
        image_album_get_committed_count(sg_picture_ctx.album_hdl, &count);
    }

    return count;
}

/**
 * @brief Move scan iterator so the next get_next() loads photo at one_based index
 * @param[in] one_based photo index 1..count in current scan order
 * @return OPRT_OK on success, OPRT_INVALID_PARM if out of range
 */
OPERATE_RET wukong_picture_seek_to_photo(uint32_t one_based)
{
    OPERATE_RET rt = OPRT_OK;

    if (NULL == sg_picture_ctx.album_scan_hdl) {
        return OPRT_COM_ERROR;
    }

    if (one_based == 0U || one_based > sg_picture_ctx.scan_img_count) {
        return OPRT_INVALID_PARM;
    }

    TUYA_CALL_ERR_RETURN(image_album_scan_seek(sg_picture_ctx.album_scan_hdl, one_based - 1U));
    sg_picture_ctx.cur_view_pos = one_based - 1U;

    return rt;
}

/**
 * @brief Get the previous picture in scan order
 * @param[out] pic filled with picture metadata and data
 * @return OPRT_OK on success, OPRT_COM_ERROR if album not open
 */
OPERATE_RET wukong_picture_get_prev(WUKONG_PICTURE_INFO_T *pic)
{
    OPERATE_RET rt = OPRT_OK;

    if (NULL == sg_picture_ctx.album_scan_hdl) {
        return OPRT_COM_ERROR;
    }

    ALBUM_IMAGE_ITEM_T item;
    memset(&item, 0x00, sizeof(ALBUM_IMAGE_ITEM_T));
    TUYA_CALL_ERR_RETURN(image_album_scan_prev(sg_picture_ctx.album_scan_hdl, &item));

    strncpy(pic->name, item.filename, ALBUM_FILENAME_MAX_LEN);
    strncpy(sg_picture_ctx.cur_name, item.filename, ALBUM_FILENAME_MAX_LEN);
    pic->width = item.attr.width;
    pic->height = item.attr.height;

    uint8_t *file_data = NULL;
    size_t file_size = 0;
    TUYA_CALL_ERR_RETURN(image_album_read(sg_picture_ctx.album_hdl, item.filename, WUKONG_PICTURE_GET_STORAGE_TP,
                                          &file_data, &file_size));
    pic->data = file_data;
    pic->len = (uint32_t)file_size;

    if (sg_picture_ctx.cur_view_pos > 0U) {
        sg_picture_ctx.cur_view_pos--;
    }

    return rt;
}

/**
 * @brief Get the next picture in scan order
 * @param[out] pic filled with picture metadata and data
 * @return OPRT_OK on success, OPRT_COM_ERROR if album not open
 */
OPERATE_RET wukong_picture_get_next(WUKONG_PICTURE_INFO_T *pic)
{
    OPERATE_RET rt = OPRT_OK;

    if (NULL == sg_picture_ctx.album_scan_hdl) {
        PR_ERR("scan not end");
        return OPRT_COM_ERROR;
    }

    ALBUM_IMAGE_ITEM_T item;
    memset(&item, 0x00, sizeof(ALBUM_IMAGE_ITEM_T));
    TUYA_CALL_ERR_RETURN(image_album_scan_next(sg_picture_ctx.album_scan_hdl, &item));

    strncpy(pic->name, item.filename, ALBUM_FILENAME_MAX_LEN);
    strncpy(sg_picture_ctx.cur_name, item.filename, ALBUM_FILENAME_MAX_LEN);
    pic->width = item.attr.width;
    pic->height = item.attr.height;

    uint8_t *file_data = NULL;
    size_t file_size = 0;
    TUYA_CALL_ERR_RETURN(image_album_read(sg_picture_ctx.album_hdl, item.filename, WUKONG_PICTURE_GET_STORAGE_TP,
                                          &file_data, &file_size));
    pic->data = file_data;
    pic->len = (uint32_t)file_size;

    sg_picture_ctx.cur_view_pos++;

    return rt;
}

/**
 * @brief Free picture data buffer inside a WUKONG_PICTURE_INFO_T
 * @param[in] pic picture info whose data buffer should be freed
 * @return none
 */
VOID_T wukong_picture_free_pic_info(WUKONG_PICTURE_INFO_T *pic)
{
    if (pic == NULL) {
        return;
    }
    if (pic->data) {
        image_album_free_file_data(pic->data);
        pic->data = NULL;
        pic->len = 0;
    }
}

/**
 * @brief Get picture info by name (retain + read + release in one call)
 * @param[in] name picture filename to look up
 * @param[out] pic filled with picture metadata and data
 * @return OPRT_OK on success
 */
OPERATE_RET wukong_picture_get_by_name(const char *name, WUKONG_PICTURE_INFO_T *pic)
{
    OPERATE_RET rt = OPRT_OK;

    if (NULL == name || NULL == pic) {
        return OPRT_INVALID_PARM;
    }

    if (NULL == sg_picture_ctx.album_hdl) {
        PR_ERR("album handle is null");
        return OPRT_COM_ERROR;
    }

    ALBUM_IMAGE_ITEM_T item;
    memset(&item, 0x00, sizeof(ALBUM_IMAGE_ITEM_T));
    TUYA_CALL_ERR_RETURN(
        image_album_item_retain_locked(sg_picture_ctx.album_hdl, name, WUKONG_PICTURE_GET_STORAGE_TP, &item));

    strncpy(pic->name, item.filename, ALBUM_FILENAME_MAX_LEN);
    pic->width = item.attr.width;
    pic->height = item.attr.height;

    uint8_t *file_data = NULL;
    size_t file_size = 0;
    rt = image_album_read(sg_picture_ctx.album_hdl, name, WUKONG_PICTURE_GET_STORAGE_TP, &file_data, &file_size);

    TUYA_CALL_ERR_RETURN(image_album_item_release_locked(sg_picture_ctx.album_hdl, name));
    if (rt != OPRT_OK) {
        return rt;
    }

    pic->data = file_data;
    pic->len = (uint32_t)file_size;

    return OPRT_OK;
}

/**
 * @brief Delete the currently displayed picture and rescan the album
 * @return OPRT_OK on success, OPRT_COM_ERROR if no current picture
 */
OPERATE_RET wukong_picture_delete_current(void)
{
    uint32_t old_cnt;
    uint32_t pos_before;
    uint32_t new_pos;

    if (sg_picture_ctx.album_hdl == NULL || sg_picture_ctx.cur_name[0] == '\0') {
        PR_ERR("no current picture to delete");
        return OPRT_COM_ERROR;
    }

    old_cnt = sg_picture_ctx.scan_img_count;
    pos_before = (sg_picture_ctx.cur_view_pos > 0U) ? sg_picture_ctx.cur_view_pos : old_cnt;
    if (pos_before == 0U) {
        pos_before = 1U;
    }

    PR_DEBUG("album: delete picture %s", sg_picture_ctx.cur_name);
    OPERATE_RET rt = image_album_delete(sg_picture_ctx.album_hdl, sg_picture_ctx.cur_name);
    if (rt != OPRT_OK) {
        PR_ERR("image_album_delete failed: %d", rt);
        return rt;
    }

    sg_picture_ctx.cur_name[0] = '\0';

    TUYA_CALL_ERR_RETURN(__wukong_picture_rescan_album());

    if (sg_picture_ctx.scan_img_count == 0U) {
        sg_picture_ctx.cur_view_pos = 0U;
        return OPRT_OK;
    }

    new_pos = pos_before;
    if (new_pos > sg_picture_ctx.scan_img_count) {
        new_pos = sg_picture_ctx.scan_img_count;
    }

    TUYA_CALL_ERR_RETURN(wukong_picture_seek_to_photo(new_pos));

    return OPRT_OK;
}

/**
 * @brief Close the album scan session and release scan resources
 * @return OPRT_OK on success
 */
OPERATE_RET wukong_picture_close_album(void)
{
    OPERATE_RET rt = OPRT_OK;

    if (NULL == sg_picture_ctx.album_scan_hdl) {
        return OPRT_OK;
    }

    TUYA_CALL_ERR_RETURN(image_album_scan_deinit(sg_picture_ctx.album_scan_hdl));
    sg_picture_ctx.album_scan_hdl = NULL;
    sg_picture_ctx.scan_img_count = 0;
    sg_picture_ctx.cur_view_pos = 0U;

    return rt;
}

/**
 * @brief Get the filename of the currently displayed picture
 * @param[out] name buffer to receive the filename
 * @return OPRT_OK on success, OPRT_NOT_FOUND if no current picture
 */
OPERATE_RET wukong_picture_get_current_name(char name[WUKONG_PICTURE_NAME_MAX_LEN + 1])
{
    if (name == NULL) {
        return OPRT_INVALID_PARM;
    }

    if (sg_picture_ctx.cur_name[0] == '\0') {
        return OPRT_NOT_FOUND;
    }

    strncpy(name, sg_picture_ctx.cur_name, WUKONG_PICTURE_NAME_MAX_LEN);
    name[WUKONG_PICTURE_NAME_MAX_LEN] = '\0';

    return OPRT_OK;
}

/**
 * @brief Get thumbnail list of all pictures in the album
 * @param[in] thumb_w desired thumbnail width in pixels
 * @param[in] thumb_h desired thumbnail height in pixels
 * @param[out] list filled with thumbnail array
 * @return OPRT_OK on success, OPRT_INVALID_PARM if bad parameters
 */
OPERATE_RET wukong_picture_get_thumb_list(uint16_t thumb_w, uint16_t thumb_h,
                                          WUKONG_PICTURE_THUMB_LIST_T *list)
{
    OPERATE_RET rt = OPRT_OK;

    if (list == NULL || thumb_w == 0 || thumb_h == 0) {
        return OPRT_INVALID_PARM;
    }

    memset(list, 0, sizeof(WUKONG_PICTURE_THUMB_LIST_T));

    IMAGE_ALBUM_SORT_OPT_T sort_opt = {
        .key   = IMAGE_ALBUM_SORT_SAVE_SEQ,
        .order = IMAGE_ALBUM_SORT_DESC,
    };

    ALBUM_THUMB_ITER_HANDLE iter = NULL;
    TUYA_CALL_ERR_RETURN(
        image_album_thumb_iter_init(TUYA_PICTURE_ALBUM_NAME, WUKONG_PICTURE_GET_STORAGE_TP, &sort_opt, &iter));

    uint32_t total = image_album_thumb_iter_count(iter);
    if (total == 0) {
        image_album_thumb_iter_deinit(iter);
        return OPRT_OK;
    }

    ALBUM_THUMB_CFG_T cfg = {
        .width  = thumb_w,
        .height = thumb_h,
        .fmt    = ALBUM_THUMB_FMT_RGB565,
        .fit    = ALBUM_THUMB_FIT_COVER,
    };

    ALBUM_THUMB_BATCH_T batch = {0};
    rt = image_album_thumb_iter_next(iter, &cfg, total, &batch);
    if (rt != OPRT_OK) {
        image_album_thumb_iter_deinit(iter);
        return rt;
    }

    list->items = (WUKONG_PICTURE_THUMB_T *)tal_malloc(sizeof(WUKONG_PICTURE_THUMB_T) * batch.count);
    if (list->items == NULL) {
        image_album_thumb_batch_free(&batch);
        image_album_thumb_iter_deinit(iter);
        return OPRT_MALLOC_FAILED;
    }

    uint32_t i;
    for (i = 0; i < batch.count; i++) {
        WUKONG_PICTURE_THUMB_T *dst = &list->items[i];
        ALBUM_THUMB_ITEM_T *src = &batch.items[i];

        strncpy(dst->name, src->filename, WUKONG_PICTURE_NAME_MAX_LEN);
        dst->name[WUKONG_PICTURE_NAME_MAX_LEN] = '\0';
        dst->width  = src->thumb.width;
        dst->height = src->thumb.height;
        dst->size   = src->thumb.size;
        dst->data   = src->thumb.buf;
        /* take ownership of pixel buffer, prevent batch_free from releasing it */
        src->thumb.buf = NULL;
    }
    list->count = batch.count;

    image_album_thumb_batch_free(&batch);
    image_album_thumb_iter_deinit(iter);

    return OPRT_OK;
}

/**
 * @brief Free thumbnail list returned by wukong_picture_get_thumb_list()
 * @param[in] list thumbnail list to free
 * @return none
 */
VOID_T wukong_picture_free_thumb_list(WUKONG_PICTURE_THUMB_LIST_T *list)
{
    if (list == NULL || list->items == NULL) {
        return;
    }

    uint32_t i;
    for (i = 0; i < list->count; i++) {
        if (list->items[i].data) {
            tal_free(list->items[i].data);
            list->items[i].data = NULL;
        }
    }
    tal_free(list->items);
    list->items = NULL;
    list->count = 0;
}

/**
 * @brief Release the retain-lock on a picture by name
 * @param[in] name picture filename to release
 * @return OPRT_OK on success
 */
OPERATE_RET wukong_picture_release_locked_from_album(const char *name)
{
    if (NULL == name) {
        return OPRT_INVALID_PARM;
    }

    if (NULL == sg_picture_ctx.album_hdl) {
        PR_ERR("album handle is null");
        return OPRT_COM_ERROR;
    }

    return image_album_item_release_locked(sg_picture_ctx.album_hdl, name);
}

/**
 * @brief Batch delete pictures by filenames
 * @param[in] names array of filename strings to delete
 * @param[in] count number of filenames in the array
 * @return OPRT_OK on success
 */
OPERATE_RET wukong_picture_delete_batch(const char *names[], uint32_t count)
{
    if (names == NULL || count == 0) {
        return OPRT_INVALID_PARM;
    }

    if (sg_picture_ctx.album_hdl == NULL) {
        PR_ERR("album handle is null");
        return OPRT_COM_ERROR;
    }

    uint32_t i;
    for (i = 0; i < count; i++) {
        if (names[i] && names[i][0]) {
            OPERATE_RET rt = image_album_delete(sg_picture_ctx.album_hdl, names[i]);
            if (rt != OPRT_OK) {
                PR_ERR("batch delete failed: %s, rt=%d", names[i], rt);
            }
        }
    }

    return OPRT_OK;
}
