/**
 * @file feishu_proto.c
 * @brief Feishu long-connection WebSocket protobuf frame codec (pure).
 *
 * Implements the standard protobuf wire format and the Feishu long-connection
 * Frame schema (field layout in feishu_proto.h). No network or RTOS
 * dependency: only <string.h> and tuya_cloud_types.h, so it is host testable.
 *
 * The codec is expressed in Frame vocabulary: a FRAME_BUILDER appends fields on
 * encode, a FRAME_SCANNER yields them one at a time on decode. Both carry a
 * sticky error flag, so callers write the frame layout straight through and
 * check once at the end.
 */
#include "feishu_proto.h"

#include <string.h>

/* Protobuf wire types. */
#define PB_WIRE_VARINT 0
#define PB_WIRE_I64    1
#define PB_WIRE_LEN    2
#define PB_WIRE_I32    5

/* Frame field numbers (protobuf tag >> 3). */
#define PB_FIELD_SEQ_ID  1
#define PB_FIELD_LOG_ID  2
#define PB_FIELD_SERVICE 3
#define PB_FIELD_METHOD  4
#define PB_FIELD_HEADER  5
#define PB_FIELD_PAYLOAD 8

/* Header sub-message field numbers. */
#define PB_HDR_FIELD_KEY   1
#define PB_HDR_FIELD_VALUE 2

/* Fixed ACK body — Feishu re-delivers events lacking this reply payload. */
static CONST CHAR_T FEISHU_ACK_PAYLOAD[] = "{\"code\":200}";

/* ------------------------------------------------------------------ */
/*  Codec types                                                        */
/* ------------------------------------------------------------------ */

/** Encode cursor: appends fields. @ref err latches on the first overflow, so
 *  every later op no-ops and the caller checks once at the end. */
typedef struct {
    BYTE_T *buf;
    UINT_T  cap;
    UINT_T  len;
    BOOL_T  err;
} FRAME_BUILDER_T;

/** Decode cursor over one immutable message buffer. @ref err latches on a
 *  malformed / truncated field. */
typedef struct {
    CONST BYTE_T *buf;
    UINT_T        len;
    UINT_T        pos;
    BOOL_T        err;
} FRAME_SCANNER_T;

/** One decoded field: @ref u64 is set for varint fields, @ref bytes/@ref len for
 *  length-delimited ones (fixed-width fields are consumed but carry no value). */
typedef struct {
    UINT32_T      num;
    BYTE_T        wire;
    UINT64_T      u64;
    CONST BYTE_T *bytes;
    UINT_T        len;
} FRAME_FIELD_T;

/* ------------------------------------------------------------------ */
/*  Frame builder — appends fields, overflow tracked as a sticky flag  */
/* ------------------------------------------------------------------ */

/** Append a raw base-128 varint. */
static VOID_T fb_varint(FRAME_BUILDER_T *fb, UINT64_T value)
{
    do {
        if (fb->err || fb->len >= fb->cap) {
            fb->err = TRUE;
            return;
        }
        BYTE_T byte = (BYTE_T)(value & 0x7F);
        value >>= 7;
        if (value) {
            byte |= 0x80;
        }
        fb->buf[fb->len++] = byte;
    } while (value);
}

/** Append a varint scalar field: tag + value. */
static VOID_T frame_put_u64(FRAME_BUILDER_T *fb, UINT32_T field, UINT64_T value)
{
    fb_varint(fb, ((UINT64_T)field << 3) | PB_WIRE_VARINT);
    fb_varint(fb, value);
}

/** Append a length-delimited field: tag + length + raw bytes. */
static VOID_T frame_put_raw(FRAME_BUILDER_T *fb, UINT32_T field,
                            CONST BYTE_T *data, UINT_T len)
{
    fb_varint(fb, ((UINT64_T)field << 3) | PB_WIRE_LEN);
    fb_varint(fb, len);
    if (fb->err || fb->len + len > fb->cap) {
        fb->err = TRUE;
        return;
    }
    memcpy(fb->buf + fb->len, data, len);
    fb->len += len;
}

/** Append one header as field 5: a { key(1), value(2) } string sub-message. */
static VOID_T frame_put_header(FRAME_BUILDER_T *fb, CONST CHAR_T *key, CONST CHAR_T *value)
{
    BYTE_T sub[FEISHU_HEADER_KEY_MAX + FEISHU_HEADER_VALUE_MAX + 8];
    FRAME_BUILDER_T sb = { sub, sizeof(sub), 0, FALSE };
    frame_put_raw(&sb, PB_HDR_FIELD_KEY,   (CONST BYTE_T *)key,   (UINT_T)strlen(key));
    frame_put_raw(&sb, PB_HDR_FIELD_VALUE, (CONST BYTE_T *)value, (UINT_T)strlen(value));
    if (sb.err) {
        fb->err = TRUE;
        return;
    }
    frame_put_raw(fb, PB_FIELD_HEADER, sub, sb.len);
}

OPERATE_RET feishu_frame_encode(CONST FEISHU_FRAME_T *frame, BYTE_T *out, UINT_T cap, UINT_T *out_len)
{
    if (frame == NULL || out == NULL || out_len == NULL) {
        return OPRT_INVALID_PARM;
    }

    FRAME_BUILDER_T fb = { out, cap, 0, FALSE };
    frame_put_u64(&fb, PB_FIELD_SEQ_ID,  frame->seq_id);
    frame_put_u64(&fb, PB_FIELD_LOG_ID,  frame->log_id);
    frame_put_u64(&fb, PB_FIELD_SERVICE, (UINT64_T)(UINT32_T)frame->service);
    frame_put_u64(&fb, PB_FIELD_METHOD,  (UINT64_T)(UINT32_T)frame->method);

    for (UINT_T i = 0; i < frame->header_count; i++) {
        frame_put_header(&fb, frame->headers[i].key, frame->headers[i].value);
    }

    if (frame->payload != NULL && frame->payload_len > 0) {
        frame_put_raw(&fb, PB_FIELD_PAYLOAD, frame->payload, frame->payload_len);
    }

    if (fb.err) {
        return OPRT_BUFFER_NOT_ENOUGH;
    }
    *out_len = fb.len;
    return OPRT_OK;
}

/* ------------------------------------------------------------------ */
/*  Frame scanner — yields one field per step over an immutable buffer */
/* ------------------------------------------------------------------ */

/** Read a raw base-128 varint at the cursor. FALSE on truncation. */
static BOOL_T fc_varint(FRAME_SCANNER_T *fc, UINT64_T *out)
{
    UINT64_T value = 0;
    INT_T shift = 0;
    while (fc->pos < fc->len && shift <= 63) {
        BYTE_T byte = fc->buf[fc->pos++];
        value |= ((UINT64_T)(byte & 0x7F)) << shift;
        if ((byte & 0x80) == 0) {
            *out = value;
            return TRUE;
        }
        shift += 7;
    }
    return FALSE;
}

/** Take the next field. Returns FALSE at the clean end of the buffer, or on a
 *  malformed field (which also sets @ref FRAME_SCANNER_T::err). */
static BOOL_T frame_take(FRAME_SCANNER_T *fc, FRAME_FIELD_T *f)
{
    if (fc->pos >= fc->len) {
        return FALSE; /* clean end */
    }

    UINT64_T tag = 0;
    if (!fc_varint(fc, &tag)) {
        fc->err = TRUE;
        return FALSE;
    }
    f->num   = (UINT32_T)(tag >> 3);
    f->wire  = (BYTE_T)(tag & 0x07);
    f->u64   = 0;
    f->bytes = NULL;
    f->len   = 0;

    switch (f->wire) {
    case PB_WIRE_VARINT:
        if (!fc_varint(fc, &f->u64)) {
            fc->err = TRUE;
            return FALSE;
        }
        return TRUE;
    case PB_WIRE_LEN: {
        UINT64_T n = 0;
        /* Compare in 64-bit against remaining bytes; pos<=len is invariant so
         * the subtraction can't underflow. Guards both the >32-bit case (a
         * truncating (UINT_T)n would slip through) and pos+n wraparound. */
        if (!fc_varint(fc, &n) || n > (UINT64_T)(fc->len - fc->pos)) {
            fc->err = TRUE;
            return FALSE;
        }
        f->bytes = fc->buf + fc->pos;
        f->len   = (UINT_T)n;
        fc->pos += (UINT_T)n;
        return TRUE;
    }
    case PB_WIRE_I64:
        if ((UINT64_T)(fc->len - fc->pos) < 8) {
            fc->err = TRUE;
            return FALSE;
        }
        fc->pos += 8;
        return TRUE;
    case PB_WIRE_I32:
        if ((UINT64_T)(fc->len - fc->pos) < 4) {
            fc->err = TRUE;
            return FALSE;
        }
        fc->pos += 4;
        return TRUE;
    default:
        fc->err = TRUE;
        return FALSE;
    }
}

/** Decode field 5's { key(1), value(2) } sub-message and store it, keeping at
 *  most FEISHU_FRAME_MAX_HEADERS (extras are still validated). FALSE if malformed. */
static BOOL_T frame_add_header(FEISHU_FRAME_T *frame, CONST BYTE_T *buf, UINT_T len)
{
    FEISHU_HEADER_T scratch;
    FEISHU_HEADER_T *h = (frame->header_count < FEISHU_FRAME_MAX_HEADERS)
                         ? &frame->headers[frame->header_count]
                         : &scratch;
    memset(h, 0, sizeof(*h));

    FRAME_SCANNER_T fc = { buf, len, 0, FALSE };
    FRAME_FIELD_T f;
    while (frame_take(&fc, &f)) {
        CHAR_T *dst = NULL;
        UINT_T dst_cap = 0;
        if (f.wire == PB_WIRE_LEN && f.num == PB_HDR_FIELD_KEY) {
            dst = h->key;   dst_cap = sizeof(h->key);
        } else if (f.wire == PB_WIRE_LEN && f.num == PB_HDR_FIELD_VALUE) {
            dst = h->value; dst_cap = sizeof(h->value);
        }
        if (dst != NULL) {
            UINT_T n = (f.len < dst_cap - 1) ? f.len : dst_cap - 1;
            memcpy(dst, f.bytes, n);
            dst[n] = '\0';
        }
    }
    if (fc.err) {
        return FALSE;
    }
    if (frame->header_count < FEISHU_FRAME_MAX_HEADERS) {
        frame->header_count++;
    }
    return TRUE;
}

OPERATE_RET feishu_frame_parse(CONST BYTE_T *buf, UINT_T len, FEISHU_FRAME_T *frame)
{
    if (buf == NULL || frame == NULL) {
        return OPRT_INVALID_PARM;
    }
    memset(frame, 0, sizeof(*frame));

    FRAME_SCANNER_T fc = { buf, len, 0, FALSE };
    FRAME_FIELD_T f;
    while (frame_take(&fc, &f)) {
        switch (f.num) {
        case PB_FIELD_SEQ_ID:
            if (f.wire == PB_WIRE_VARINT) { frame->seq_id = f.u64; }
            break;
        case PB_FIELD_LOG_ID:
            if (f.wire == PB_WIRE_VARINT) { frame->log_id = f.u64; }
            break;
        case PB_FIELD_SERVICE:
            if (f.wire == PB_WIRE_VARINT) { frame->service = (INT_T)f.u64; }
            break;
        case PB_FIELD_METHOD:
            if (f.wire == PB_WIRE_VARINT) { frame->method = (INT_T)f.u64; }
            break;
        case PB_FIELD_HEADER:
            if (f.wire == PB_WIRE_LEN && !frame_add_header(frame, f.bytes, f.len)) {
                return OPRT_INVALID_PARM;
            }
            break;
        case PB_FIELD_PAYLOAD:
            if (f.wire == PB_WIRE_LEN) {
                frame->payload     = f.bytes;
                frame->payload_len = f.len;
            }
            break;
        default:
            break; /* unknown field already consumed by frame_take */
        }
    }
    if (fc.err) {
        return OPRT_INVALID_PARM;
    }
    return OPRT_OK;
}

CONST CHAR_T *feishu_frame_header(CONST FEISHU_FRAME_T *frame, CONST CHAR_T *key)
{
    if (frame == NULL || key == NULL) {
        return NULL;
    }
    for (UINT_T i = 0; i < frame->header_count; i++) {
        if (strcmp(frame->headers[i].key, key) == 0) {
            return frame->headers[i].value;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Control-frame builders                                             */
/* ------------------------------------------------------------------ */

OPERATE_RET feishu_frame_build_ping(INT_T service, BYTE_T *out, UINT_T cap, UINT_T *out_len)
{
    FEISHU_FRAME_T ping;

    memset(&ping, 0, sizeof(ping));
    ping.service = service;
    ping.method = FEISHU_METHOD_CONTROL;
    ping.header_count = 1;
    strcpy(ping.headers[0].key, "type");
    strcpy(ping.headers[0].value, "ping");
    return feishu_frame_encode(&ping, out, cap, out_len);
}

OPERATE_RET feishu_frame_build_ack(CONST FEISHU_FRAME_T *recv, BYTE_T *out, UINT_T cap, UINT_T *out_len)
{
    if (recv == NULL) {
        return OPRT_INVALID_PARM;
    }

    /* Echo the received frame verbatim, then swap in the ACK payload. */
    FEISHU_FRAME_T ack = *recv;
    ack.payload = (CONST BYTE_T *)FEISHU_ACK_PAYLOAD;
    ack.payload_len = (UINT_T)(sizeof(FEISHU_ACK_PAYLOAD) - 1);
    return feishu_frame_encode(&ack, out, cap, out_len);
}
