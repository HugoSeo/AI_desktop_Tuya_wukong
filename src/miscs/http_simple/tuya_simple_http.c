/**
 * @file tuya_simple_http.c
 * @brief Simplified HTTP client implementation without business logic
 * @date 2026-01-13
 */

#include "tuya_simple_http.h"
#include "http_inf.h"
#include "tal_log.h"
#include "tal_memory.h"
#include <string.h>

/* Internal context passed as pri_data to the HTTP callback. */
typedef struct {
    CONST simple_http_opts_t *opts;   /* read resp_hdr_cb from here */
    simple_http_response_t   *resp;   /* write data / len / http_code here */
} __simple_http_priv_t;

/**
 * @brief Receive data with known content length
 */
STATIC OPERATE_RET __simple_http_recv_data(IN HTTP_INF_H_S *hand, OUT BYTE_T **out, OUT UINT_T *out_len)
{
    if (hand->content_len <= 0) {
        TAL_PR_DEBUG("Content length is 0");
        *out = NULL;
        *out_len = 0;
        return OPRT_OK;
    }

    INT_T buf_size = hand->content_len + 1;
    BYTE_T *buf = SIMPLE_HTTP_MALLOC(buf_size);
    if (NULL == buf) {
        TAL_PR_ERR("Malloc failed, size:%d", buf_size);
        return OPRT_MALLOC_FAILED;
    }

    INT_T recv_size = 0;
    INT_T ret = 0;
    while (recv_size < hand->content_len) {
        ret = hand->recv(hand->hand, buf + recv_size, buf_size - recv_size - 1);

        if (ret < 0) {
            TAL_PR_ERR("Recv failed, ret:%d", ret);
            SIMPLE_HTTP_FREE(buf);
            return OPRT_SVC_HTTP_RECV_ERR;
        } else if (0 == ret) {
            break;
        }
        recv_size += ret;
    }

    if (recv_size < hand->content_len) {
        TAL_PR_ERR("Recv data not enough, recv:%d, expect:%d", recv_size, hand->content_len);
        SIMPLE_HTTP_FREE(buf);
        return OPRT_SVC_HTTP_RECV_DA_NOT_ENOUGH;
    }

    buf[recv_size] = '\0';
    *out = buf;
    *out_len = recv_size;

    return OPRT_OK;
}

/**
 * @brief Receive chunked transfer encoding data
 */
STATIC OPERATE_RET __simple_http_recv_chunk_data(IN HTTP_INF_H_S *hand, OUT BYTE_T **out, OUT UINT_T *out_len)
{
    INT_T read_count = 0;
    BYTE_T *chunk_data  = NULL;
    BYTE_T *chunk_block = NULL;
    INT_T total_len = 0;

    chunk_data = SIMPLE_HTTP_MALLOC(SIMPLE_HTTP_CHUNK_DATA_MAX_LEN);
    if (NULL == chunk_data) {
        TAL_PR_ERR("Malloc chunk_data failed, size:%d", SIMPLE_HTTP_CHUNK_DATA_MAX_LEN);
        return OPRT_MALLOC_FAILED;
    }

    chunk_block = SIMPLE_HTTP_MALLOC(SIMPLE_HTTP_CHUNK_DATA_PER_BLOCK);
    if (NULL == chunk_block) {
        TAL_PR_ERR("Malloc chunk_block failed, size:%d", SIMPLE_HTTP_CHUNK_DATA_PER_BLOCK);
        SIMPLE_HTTP_FREE(chunk_data);
        return OPRT_MALLOC_FAILED;
    }

    do {
        read_count = hand->recv(hand->hand, chunk_block, SIMPLE_HTTP_CHUNK_DATA_PER_BLOCK);
        if (read_count > 0) {
            if (total_len + read_count > SIMPLE_HTTP_CHUNK_DATA_MAX_LEN) {
                TAL_PR_ERR("Chunk data too large, total:%d", total_len + read_count);
                SIMPLE_HTTP_FREE(chunk_data);
                SIMPLE_HTTP_FREE(chunk_block);
                return OPRT_SVC_HTTP_RECV_DA_NOT_ENOUGH;
            }
            memcpy(chunk_data + total_len, chunk_block, read_count);
            total_len += read_count;
        }
    } while (read_count > 0);

    chunk_data[total_len] = '\0';
    SIMPLE_HTTP_FREE(chunk_block);
    *out = chunk_data;
    *out_len = total_len;
    TAL_PR_DEBUG("Chunk data received, total_len:%d", total_len);

    return OPRT_OK;
}

/**
 * @brief Internal HTTP callback: records status code, fires resp_hdr_cb, receives body.
 */
STATIC OPERATE_RET __simple_http_callback(HTTP_INF_H_S *hand)
{
    if (hand->pri_data == NULL) {
        return OPRT_OK;
    }

    __simple_http_priv_t   *priv = (__simple_http_priv_t *)hand->pri_data;
    simple_http_response_t *resp = priv->resp;

    resp->http_code = hand->status_code;

    if (priv->opts != NULL && priv->opts->resp_hdr_cb != NULL) {
        priv->opts->resp_hdr_cb((http_session_t)hand->hand, priv->opts->resp_hdr_cb_data);
    }

    OPERATE_RET op_ret = OPRT_OK;
    BYTE_T *out = NULL;
    UINT_T out_len = 0;

    if (TRUE == hand->chunked) {
        op_ret = __simple_http_recv_chunk_data(hand, &out, &out_len);
        if (OPRT_OK != op_ret) {
            TAL_PR_ERR("Recv chunk data failed, ret:%d", op_ret);
            return op_ret;
        }
    } else {
        op_ret = __simple_http_recv_data(hand, &out, &out_len);
        if (OPRT_OK != op_ret) {
            TAL_PR_ERR("Recv data failed, ret:%d", op_ret);
            return op_ret;
        }
    }

    resp->data = out;
    resp->len  = out_len;
    TAL_PR_DEBUG("HTTP status:%d, data_len:%d", hand->status_code, out_len);
    return OPRT_OK;
}

OPERATE_RET tuya_simple_http_get(IN CONST CHAR_T              *url,
                                  IN CONST simple_http_opts_t  *opts,
                                  OUT      simple_http_response_t *response)
{
    if (NULL == url || NULL == response) {
        TAL_PR_ERR("Invalid parameters");
        return OPRT_INVALID_PARM;
    }

    memset(response, 0, sizeof(*response));

    __simple_http_priv_t priv = { opts, response };

    /* Expanded from the http_inf_client_get_session_with_head macro (same
     * flags, incl. keep-alive) with ONE change: session_persistence off —
     * the shared session cache is what we opt out of, not the header. */
    OPERATE_RET op_ret = http_inf_client_get_session(
        url,
        __simple_http_callback,
        (PVOID_T *)&priv,
        opts ? opts->add_head_cb   : NULL,
        opts ? opts->add_head_data : NULL,
        STANDARD_HDR_FLAGS | HDR_ADD_CONN_KEEP_ALIVE,
        FALSE   /* session_persistence */
    );

    if (OPRT_OK != op_ret) {
        TAL_PR_ERR("HTTP GET failed, url:%s, ret:%d", url, op_ret);
        if (response->data) {
            SIMPLE_HTTP_FREE(response->data);
            response->data = NULL;
            response->len  = 0;
        }
        return op_ret;
    }

    TAL_PR_DEBUG("HTTP GET success, url:%s, response_len:%d", url, response->len);
    return OPRT_OK;
}

OPERATE_RET tuya_simple_http_post(IN CONST CHAR_T              *url,
                                   IN CONST BYTE_T              *data,
                                   IN CONST UINT_T               len,
                                   IN CONST simple_http_opts_t  *opts,
                                   OUT      simple_http_response_t *response)
{
    if (NULL == url || NULL == data || NULL == response) {
        TAL_PR_ERR("Invalid parameters");
        return OPRT_INVALID_PARM;
    }

    memset(response, 0, sizeof(*response));

    __simple_http_priv_t priv = { opts, response };

    http_hdr_field_sel_t combined_flags = (STANDARD_HDR_FLAGS | HDR_ADD_CONN_KEEP_ALIVE)
                                         | (opts ? opts->field_flags : 0);

    OPERATE_RET op_ret = http_inf_client_post_field_session(
        url,
        __simple_http_callback,
        data,
        len,
        opts ? opts->add_head_cb   : NULL,
        opts ? opts->add_head_data : NULL,
        NULL,
        (PVOID_T *)&priv,
        combined_flags,
        FALSE
    );

    if (OPRT_OK != op_ret) {
        TAL_PR_ERR("HTTP POST failed, url:%s, ret:%d", url, op_ret);
        if (response->data) {
            SIMPLE_HTTP_FREE(response->data);
            response->data = NULL;
            response->len  = 0;
        }
        return op_ret;
    }

    TAL_PR_DEBUG("HTTP POST success, url:%s, response_len:%d", url, response->len);
    return OPRT_OK;
}
