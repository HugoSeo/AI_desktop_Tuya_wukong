/**
 * @file feishu_proto.h
 * @brief Feishu (Lark) long-connection WebSocket protobuf frame codec.
 *
 * A Feishu WebSocket binary message carries exactly one protobuf-encoded
 * Frame. This module is pure: it only decodes/encodes byte buffers and has
 * no dependency on the network stack or the RTOS, so it is fully host
 * testable.
 *
 * Frame wire layout (only the fields we use):
 *   1 = SeqID   (varint)
 *   2 = LogID   (varint)
 *   3 = service (varint)
 *   4 = method  (varint, 0=CONTROL, 1=DATA)
 *   5 = headers[] { 1=key(string), 2=value(string) }
 *   8 = payload (bytes)
 * Relevant header keys are "type" and "message_id".
 *
 * @version 1.0
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __FEISHU_PROTO_H__
#define __FEISHU_PROTO_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"

/* Frame.method values (protobuf field 4). */
#define FEISHU_METHOD_CONTROL 0 /**< ping/pong and other control frames. */
#define FEISHU_METHOD_DATA    1 /**< event delivery frames (carry payload). */

/* Fixed capacities. Feishu v1 text event frames carry a handful of short
 * headers, so these bounds are generous while keeping the frame POD-copyable
 * (important: ACK echoes the whole received frame). */
#define FEISHU_FRAME_MAX_HEADERS 16
#define FEISHU_HEADER_KEY_MAX    32
#define FEISHU_HEADER_VALUE_MAX  128

/** One decoded Frame header (protobuf field 5 sub-message). */
typedef struct {
    CHAR_T key[FEISHU_HEADER_KEY_MAX];
    CHAR_T value[FEISHU_HEADER_VALUE_MAX];
} FEISHU_HEADER_T;

/**
 * @brief A decoded (or to-be-encoded) Feishu Frame.
 *
 * After feishu_frame_parse(), @ref payload points directly into the source
 * buffer (no copy); it stays valid only as long as that buffer does.
 */
typedef struct {
    UINT64_T seq_id;  /**< field 1 */
    UINT64_T log_id;  /**< field 2 */
    INT_T service;    /**< field 3 */
    INT_T method;     /**< field 4: FEISHU_METHOD_CONTROL / FEISHU_METHOD_DATA */
    FEISHU_HEADER_T headers[FEISHU_FRAME_MAX_HEADERS];
    UINT_T header_count;
    CONST BYTE_T *payload; /**< field 8 bytes, or NULL when absent */
    UINT_T payload_len;
} FEISHU_FRAME_T;

/**
 * @brief Decode a protobuf Frame from a raw WebSocket binary message.
 *
 * Bounds-checked: truncated or malformed input never reads past @p len and
 * returns an error instead. Missing optional fields keep their zero default
 * (e.g. an omitted method decodes as FEISHU_METHOD_CONTROL, matching
 * protobuf semantics). Headers beyond FEISHU_FRAME_MAX_HEADERS are skipped
 * but still bounds-checked.
 *
 * @param[in]  buf   Source bytes (may alias frame->payload after return).
 * @param[in]  len   Number of bytes in @p buf.
 * @param[out] frame Decoded frame; zeroed on entry.
 * @return OPRT_OK on success, OPRT_INVALID_PARM on bad args or malformed input.
 */
OPERATE_RET feishu_frame_parse(CONST BYTE_T *buf, UINT_T len, FEISHU_FRAME_T *frame);

/**
 * @brief Look up a decoded header value by key.
 *
 * @param[in] frame Decoded frame.
 * @param[in] key   Header key, e.g. "type" or "message_id".
 * @return Pointer to the value (NUL-terminated), or NULL if not present.
 */
CONST CHAR_T *feishu_frame_header(CONST FEISHU_FRAME_T *frame, CONST CHAR_T *key);

/**
 * @brief Encode a Frame into @p out.
 *
 * Writes fields 1-5 and, when @ref FEISHU_FRAME_T::payload is non-NULL and
 * non-empty, field 8. On success @p out_len receives the encoded length.
 *
 * @param[in]  frame   Frame to encode.
 * @param[out] out     Destination buffer.
 * @param[in]  cap     Capacity of @p out in bytes.
 * @param[out] out_len Encoded length on success.
 * @return OPRT_OK, OPRT_INVALID_PARM on bad args, OPRT_BUFFER_NOT_ENOUGH if
 *         @p out is too small.
 */
OPERATE_RET feishu_frame_encode(CONST FEISHU_FRAME_T *frame, BYTE_T *out, UINT_T cap, UINT_T *out_len);

/**
 * @brief Build an application-level ping frame (CONTROL, header type="ping").
 *
 * @param[in]  service Service id to echo (from the endpoint handshake).
 * @param[out] out     Destination buffer.
 * @param[in]  cap     Capacity of @p out in bytes.
 * @param[out] out_len Encoded length on success.
 * @return OPRT_OK, OPRT_INVALID_PARM, or OPRT_BUFFER_NOT_ENOUGH.
 */
OPERATE_RET feishu_frame_build_ping(INT_T service, BYTE_T *out, UINT_T cap, UINT_T *out_len);

/**
 * @brief Build the ACK for a received event frame.
 *
 * Echoes the received frame's headers (and seq_id/log_id/service/method)
 * unchanged and replaces the payload with {"code":200}. Feishu re-delivers
 * events that are not acknowledged this way.
 *
 * @param[in]  recv    Frame previously decoded by feishu_frame_parse().
 * @param[out] out     Destination buffer.
 * @param[in]  cap     Capacity of @p out in bytes.
 * @param[out] out_len Encoded length on success.
 * @return OPRT_OK, OPRT_INVALID_PARM, or OPRT_BUFFER_NOT_ENOUGH.
 */
OPERATE_RET feishu_frame_build_ack(CONST FEISHU_FRAME_T *recv, BYTE_T *out, UINT_T cap, UINT_T *out_len);

#ifdef __cplusplus
}
#endif
#endif /* __FEISHU_PROTO_H__ */
