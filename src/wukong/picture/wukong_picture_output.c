/**
 * @file wukong_picture_output.c
 * @brief Picture output module implementation.
 *        Accumulates streamed JPEG chunks into a contiguous buffer,
 *        saves the completed picture to the album, and notifies listeners.
 * @version 0.1
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "base_event.h"
#include "cJSON.h"
#include "tal_memory.h"
#include "tuya_ai_agent.h"
#include "uni_log.h"
#include "wukong_ai_agent.h"

#include "wukong_picture_output.h"

/***********************************************************
************************macro define************************
***********************************************************/
#define WUKONG_PICTURE_OUTPUT_WIDTH_KEY "sys.device.img_resize.width"
#define WUKONG_PICTURE_OUTPUT_HEIGHT_KEY "sys.device.img_resize.height"

/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef struct {
    bool is_start;
    uint32_t total_size;
    uint32_t offset;
    uint8_t *acc_buf;
} WUKONG_PICTURE_STREAM_T;

typedef struct {
    uint16_t set_width;
    uint16_t set_height;
} WUKONG_PICTURE_OUTPUT_CTX_T;

/***********************************************************
***********************variable define**********************
***********************************************************/
static WUKONG_PICTURE_OUTPUT_CTX_T sg_picture_output;
static WUKONG_PICTURE_STREAM_T sg_wukong_pic_stream;

/***********************************************************
***********************function define**********************
***********************************************************/

/**
 * @brief Free partial JPEG accumulator and clear session state
 * @return none
 */
static void __wukong_picture_output_accum_reset(void)
{
    if (sg_wukong_pic_stream.acc_buf != NULL) {
        Free(sg_wukong_pic_stream.acc_buf);
    }
    memset(&sg_wukong_pic_stream, 0, sizeof(WUKONG_PICTURE_STREAM_T));
}

/**
 * @brief Event callback to push output picture dimensions to AI agent custom params
 * @param[in] data unused
 * @return 0 on success
 */
static int __set_output_picture_size_cb(void *data)
{
    (void)data;

    ty_cJSON *custom_param = ty_cJSON_CreateObject();
    if(NULL ==  custom_param) {
        return OPRT_CR_CJSON_ERR;
    }

    ty_cJSON *cj_width = ty_cJSON_CreateObject();
    ty_cJSON_AddNumberToObject(cj_width, "value", sg_picture_output.set_width);
    ty_cJSON_AddItemToObject(custom_param, WUKONG_PICTURE_OUTPUT_WIDTH_KEY, cj_width);

    ty_cJSON *cj_height = ty_cJSON_CreateObject();
    ty_cJSON_AddNumberToObject(cj_height, "value", sg_picture_output.set_height);
    ty_cJSON_AddItemToObject(custom_param, WUKONG_PICTURE_OUTPUT_HEIGHT_KEY, cj_height);

    char *out = ty_cJSON_PrintUnformatted(custom_param);
    ty_cJSON_Delete(custom_param);

    if(out) {
        wukong_ai_agent_set_event_param(out);
        PR_DEBUG("%s", out);
        ty_cJSON_free(out);
    }else {
        PR_ERR("cjson printunformatted failed");
    }

    return 0;
}

/**
 * @brief Set the desired output picture dimensions for AI image generation
 * @param[in] width desired width in pixels
 * @param[in] height desired height in pixels
 * @return OPRT_OK on success
 */
OPERATE_RET wukong_picture_output_set_size(uint16_t width, uint16_t height)
{
    OPERATE_RET rt = OPRT_OK;

    sg_picture_output.set_width = width;
    sg_picture_output.set_height = height;

    TUYA_CALL_ERR_LOG(ty_subscribe_event(EVENT_AI_CLIENT_RUN, 
                                         "set_output_picture_size",
                                         __set_output_picture_size_cb,
                                         SUBSCRIBE_TYPE_NORMAL));

    return rt;
}

/**
 * @brief Accumulate a JPEG chunk and save to album when all chunks are received
 * @param[in] data JPEG chunk data
 * @param[in] len chunk length in bytes
 * @param[in] total_len total expected JPEG size in bytes
 * @return OPRT_OK on success
 */
OPERATE_RET wukong_picture_output_save_to_album(uint8_t *data, uint32_t len, uint32_t total_len)
{
    OPERATE_RET rt = OPRT_OK;

    if (NULL == data || len == 0 || total_len == 0) {
        PR_ERR("invalid param, data:%p, len:%u, total_len:%u", data, len, total_len);
        return OPRT_INVALID_PARM;
    }

    if (false == sg_wukong_pic_stream.is_start) {
        PR_NOTICE("[pic_chain] start accumulating, total_len:%u", total_len);
        sg_wukong_pic_stream.acc_buf = (uint8_t *)Malloc((size_t)total_len);
        if (sg_wukong_pic_stream.acc_buf == NULL) {
            PR_ERR("[pic_chain] malloc %u bytes failed", total_len);
            return OPRT_MALLOC_FAILED;
        }

        sg_wukong_pic_stream.total_size = total_len;
        sg_wukong_pic_stream.offset = 0;
        sg_wukong_pic_stream.is_start = true;
    } else {
        if (sg_wukong_pic_stream.total_size != total_len) {
            PR_ERR("get total size:%u is different %u", total_len, sg_wukong_pic_stream.total_size);
            __wukong_picture_output_accum_reset();
            return OPRT_COM_ERROR;
        }
    }

    if (len > total_len - sg_wukong_pic_stream.offset) {
        PR_ERR("chunk overflow: offset=%u len=%u total=%u", sg_wukong_pic_stream.offset, len,
               sg_wukong_pic_stream.total_size);
        __wukong_picture_output_accum_reset();
        return OPRT_BUFFER_NOT_ENOUGH;
    }

    memcpy(sg_wukong_pic_stream.acc_buf + sg_wukong_pic_stream.offset, data, (size_t)len);
    sg_wukong_pic_stream.offset += len;
    PR_DEBUG("[pic_chain] chunk accumulated, offset:%u/%u", sg_wukong_pic_stream.offset,
             sg_wukong_pic_stream.total_size);

    if (sg_wukong_pic_stream.offset >= sg_wukong_pic_stream.total_size) {
        PR_NOTICE("[pic_chain] all chunks received, total:%u, saving to album", sg_wukong_pic_stream.total_size);
        char name[WUKONG_PICTURE_NAME_MAX_LEN + 1] = {0};

        rt = wukong_picture_save_to_album(sg_wukong_pic_stream.acc_buf, sg_wukong_pic_stream.total_size, name);
        if (rt != OPRT_OK) {
            PR_ERR("[pic_chain] save to album failed, rt:%d", rt);
        } else {
            PR_NOTICE("[pic_chain] save to album success, name:%s, notify ACCEPT_PICTURE", name);
            wukong_ai_event_notify(WUKONG_AI_EVENT_ACCEPT_PICTURE, name);
        }

        __wukong_picture_output_accum_reset();
    }

    return rt;
}
