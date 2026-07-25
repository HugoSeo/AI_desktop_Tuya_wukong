/**
 * @file wukong_picture_output.h
 * @brief Picture output module for receiving AI-generated images.
 *        Accumulates streamed JPEG chunks and saves the completed
 *        picture to the album, then notifies via WUKONG_AI_EVENT_ACCEPT_PICTURE.
 * @version 0.1
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef __WUKONG_PICTURE_OUTPUT_H__
#define __WUKONG_PICTURE_OUTPUT_H__

#include "tuya_cloud_types.h"
#include "wukong_picture.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
************************macro define************************
***********************************************************/
#define WUKONG_PICTURE_OUTPUT_MAX_NUM 12

/***********************************************************
********************function declaration********************
***********************************************************/

/**
 * @brief Set the desired output picture dimensions for AI image generation
 * @param[in] width desired width in pixels
 * @param[in] height desired height in pixels
 * @return OPRT_OK on success
 */
OPERATE_RET wukong_picture_output_set_size(uint16_t width, uint16_t height);

/**
 * @brief Accumulate a JPEG chunk and save to album when all chunks are received
 * @param[in] data JPEG chunk data
 * @param[in] len chunk length in bytes
 * @param[in] total_len total expected JPEG size in bytes
 * @return OPRT_OK on success
 */
OPERATE_RET wukong_picture_output_save_to_album(uint8_t *data, uint32_t len, uint32_t total_len);

#ifdef __cplusplus
}
#endif

#endif /* __WUKONG_PICTURE_OUTPUT_H__ */
