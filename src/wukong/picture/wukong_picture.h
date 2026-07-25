/**
 * @file wukong_picture.h
 * @brief Picture album management interface for Wukong AI device.
 *        Provides album open/close, photo navigation, thumbnail generation,
 *        and batch operations built on top of the image_album component.
 * @version 0.1
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef __WUKONG_PICTURE_H__
#define __WUKONG_PICTURE_H__

#include "tuya_cloud_types.h"

#if defined(ENABLE_IMAGE_ALBUM) && (ENABLE_IMAGE_ALBUM == 1)
#include "image_album.h"
#endif

#ifdef __cplusplus
extern "C"
{
#endif

/***********************************************************
************************macro define************************
***********************************************************/
#define WUKONG_PICTURE_ALBUM_STORAGE_MASK IMAGE_ALBUM_STORAGE_TP_ALL

/**
 * @brief Backend selector for @ref image_album_get_next_item / @ref image_album_item_retain_locked paths.
 *        Wukong picture uses in-memory payload pointer from the memory backend.
 */
#define WUKONG_PICTURE_GET_STORAGE_TP IMAGE_ALBUM_STORAGE_TP_MEMORY

/**
 * @brief Storage backend used as source of truth for image list recovery at init.
 *        Set to @ref IMAGE_ALBUM_STORAGE_TP_SD when persistent SD storage is enabled,
 *        0 when no persistent storage (image list starts empty on each boot).
 */
#if defined(ENABLE_IMAGE_ALBUM_STORAGE_SD) && (ENABLE_IMAGE_ALBUM_STORAGE_SD)
#define WUKONG_PICTURE_ALBUM_RECOVER_TP IMAGE_ALBUM_STORAGE_TP_SD
#else
#define WUKONG_PICTURE_ALBUM_RECOVER_TP 0
#endif

#ifndef ALBUM_FILENAME_MAX_LEN
#define WUKONG_PICTURE_NAME_MAX_LEN 64
#else
#define WUKONG_PICTURE_NAME_MAX_LEN ALBUM_FILENAME_MAX_LEN
#endif

    /***********************************************************
    ***********************typedef define***********************
    ***********************************************************/

    /** @brief Picture metadata and file data container */
    typedef struct
    {
        char name[WUKONG_PICTURE_NAME_MAX_LEN + 1];
        uint16_t width;
        uint16_t height;
        uint32_t len;
        uint8_t *data;
    } WUKONG_PICTURE_INFO_T;

    /** @brief Single thumbnail entry with RGB565 pixel data */
    typedef struct
    {
        char name[WUKONG_PICTURE_NAME_MAX_LEN + 1];
        uint16_t width;   /**< actual thumbnail width in pixels */
        uint16_t height;  /**< actual thumbnail height in pixels */
        uint32_t size;    /**< pixel buffer size in bytes */
        uint8_t *data;    /**< thumbnail pixel buffer (RGB565), NULL on decode failure */
    } WUKONG_PICTURE_THUMB_T;

    /** @brief Thumbnail list container */
    typedef struct
    {
        WUKONG_PICTURE_THUMB_T *items;  /**< array of thumbnail entries */
        uint32_t count;                 /**< number of valid entries */
    } WUKONG_PICTURE_THUMB_LIST_T;

    /***********************************************************
    ********************function declaration********************
    ***********************************************************/

    /**
     * @brief Initialize the picture album module
     * @return OPRT_OK on success
     */
    OPERATE_RET wukong_picture_init(void);

    /**
     * @brief Save a JPEG picture to the album
     * @param[in] picture JPEG data buffer
     * @param[in] len JPEG data length in bytes
     * @param[out] name filled with the generated filename (may be NULL)
     * @return OPRT_OK on success
     */
    OPERATE_RET wukong_picture_save_to_album(uint8_t *picture, uint32_t len, char name[WUKONG_PICTURE_NAME_MAX_LEN + 1]);

    /**
     * @brief Get picture info by name (retain + read + release)
     * @param[in] name picture filename to look up
     * @param[out] pic filled with picture metadata and data; caller must free with wukong_picture_free_pic_info()
     * @return OPRT_OK on success, OPRT_INVALID_PARM if bad parameters, OPRT_NOT_FOUND if not in album
     */
    OPERATE_RET wukong_picture_get_by_name(const char *name, WUKONG_PICTURE_INFO_T *pic);

    /**
     * @brief Release the retain-lock previously acquired by image_album_item_retain_locked()
     * @param[in] name picture filename to release
     * @return OPRT_OK on success
     */
    OPERATE_RET wukong_picture_release_locked_from_album(const char *name);

    /**
     * @brief Free picture data buffer inside a WUKONG_PICTURE_INFO_T
     * @param[in] pic picture info whose data buffer should be freed
     * @return none
     */
    VOID_T wukong_picture_free_pic_info(WUKONG_PICTURE_INFO_T *pic);

    /**
     * @brief Open the album for sequential scan
     * @return OPRT_OK on success, OPRT_COM_ERROR if already open
     */
    OPERATE_RET wukong_picture_open_album(void);

    /**
     * @brief Get the number of pictures in the album
     * @return picture count (0 if album not open and committed count query fails)
     */
    uint32_t wukong_picture_get_count(void);

    /**
     * @brief Move scan iterator so the next wukong_picture_get_next() loads photo at one_based index
     * @param[in] one_based photo index 1..count in current scan order
     * @return OPRT_OK on success, OPRT_INVALID_PARM if out of range, OPRT_COM_ERROR if album not open
     */
    OPERATE_RET wukong_picture_seek_to_photo(uint32_t one_based);

    /**
     * @brief Get the previous picture in scan order
     * @param[out] pic filled with picture metadata and data; caller must free with wukong_picture_free_pic_info()
     * @return OPRT_OK on success, OPRT_COM_ERROR if album not open
     */
    OPERATE_RET wukong_picture_get_prev(WUKONG_PICTURE_INFO_T *pic);

    /**
     * @brief Get the next picture in scan order
     * @param[out] pic filled with picture metadata and data; caller must free with wukong_picture_free_pic_info()
     * @return OPRT_OK on success, OPRT_COM_ERROR if album not open
     */
    OPERATE_RET wukong_picture_get_next(WUKONG_PICTURE_INFO_T *pic);

    /**
     * @brief Delete the currently displayed picture and rescan the album
     * @return OPRT_OK on success, OPRT_COM_ERROR if no current picture
     */
    OPERATE_RET wukong_picture_delete_current(void);

    /**
     * @brief Close the album scan session and release scan resources
     * @return OPRT_OK on success
     */
    OPERATE_RET wukong_picture_close_album(void);

    /**
     * @brief Get thumbnail list of all pictures in the album
     * @param[in] thumb_w desired thumbnail width in pixels
     * @param[in] thumb_h desired thumbnail height in pixels
     * @param[out] list filled with thumbnail array; caller must free with wukong_picture_free_thumb_list()
     * @return OPRT_OK on success, OPRT_INVALID_PARM if bad parameters
     */
    OPERATE_RET wukong_picture_get_thumb_list(uint16_t thumb_w, uint16_t thumb_h,
                                              WUKONG_PICTURE_THUMB_LIST_T *list);

    /**
     * @brief Free thumbnail list returned by wukong_picture_get_thumb_list()
     * @param[in] list thumbnail list to free
     * @return none
     */
    VOID_T wukong_picture_free_thumb_list(WUKONG_PICTURE_THUMB_LIST_T *list);

    /**
     * @brief Batch delete pictures by filenames
     * @param[in] names array of filename strings to delete
     * @param[in] count number of filenames in the array
     * @return OPRT_OK on success, OPRT_INVALID_PARM if bad parameters
     */
    OPERATE_RET wukong_picture_delete_batch(const char *names[], uint32_t count);

    /**
     * @brief Get the filename of the currently displayed picture
     * @param[out] name buffer to receive the filename (at least WUKONG_PICTURE_NAME_MAX_LEN + 1 bytes)
     * @return OPRT_OK on success, OPRT_NOT_FOUND if no current picture
     */
    OPERATE_RET wukong_picture_get_current_name(char name[WUKONG_PICTURE_NAME_MAX_LEN + 1]);

#ifdef __cplusplus
}
#endif

#endif /* __WUKONG_PICTURE_H__ */
