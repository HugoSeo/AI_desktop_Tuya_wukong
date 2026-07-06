/**
 * @file wukong_picture_input.h
 * @brief Picture input queue for sending album photos to the AI agent.
 *        Supports queuing up to WUKONG_PICTURE_INPUT_MAX_NUM pictures
 *        with optional text, then batch-sending them during a conversation.
 * @version 0.1
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef __WUKONG_PICTURE_INPUT_H__
#define __WUKONG_PICTURE_INPUT_H__

#include "tuya_cloud_types.h"
#include "wukong_picture.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
************************macro define************************
***********************************************************/
#define WUKONG_PICTURE_INPUT_MAX_NUM 3

/***********************************************************
********************function declaration********************
***********************************************************/

/**
 * @brief Add a picture from album to the input queue (retain-lock only, data read deferred)
 * @param[in] filename album picture filename
 * @param[in] text optional text to send along with the picture (NULL if none)
 * @return OPRT_OK on success, OPRT_EXCEED_UPPER_LIMIT if queue is full
 */
OPERATE_RET wukong_picture_input_add_from_album(char *filename, char *text);

/**
 * @brief Remove a queued picture by filename and release its retain-lock
 * @param[in] filename album picture filename to remove
 * @return OPRT_OK on success, OPRT_NOT_FOUND if not in queue
 */
OPERATE_RET wukong_picture_input_del_from_album(char *filename);

/**
 * @brief Send all queued pictures to the AI agent and clear the queue.
 *        Each picture is read, sent, and freed one at a time to save memory.
 *        Fires WUKONG_AI_EVENT_SEND_PICTURE_END when at least one picture was sent.
 * @return OPRT_OK on success
 */
OPERATE_RET wukong_picture_input_from_album(void);

/**
 * @brief Get the number of pictures currently queued
 * @return number of queued pictures
 */
uint32_t wukong_picture_input_get_num(void);

#ifdef __cplusplus
}
#endif

#endif /* __WUKONG_PICTURE_INPUT_H__ */
