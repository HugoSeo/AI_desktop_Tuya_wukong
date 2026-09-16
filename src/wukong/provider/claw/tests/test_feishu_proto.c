/**
 * @file test_feishu_proto.c
 * @brief Host unit tests for the Feishu protobuf frame codec.
 *
 * Coverage:
 *  - Parse a known byte sequence (mimiclaw wire format) → method/service/
 *    headers(type,message_id)/payload.
 *  - Encode → decode roundtrip for a full frame.
 *  - build_ping / build_ack roundtrip through the parser.
 *  - Truncated / malformed inputs return an error without overrunning.
 */
#include "wukong_test.h"
#include "tuya_cloud_types.h"
#include "feishu_proto.h"

/*
 * A DATA frame hand-encoded per the mimiclaw wire format:
 *   field1 seq_id=5           : 08 05
 *   field3 service=2          : 18 02
 *   field4 method=1 (DATA)    : 20 01
 *   field5 header {type=event}: 2A 0D  0A 04 "type"  12 05 "event"
 *   field5 header {message_id=om_x}:
 *                               2A 12  0A 0A "message_id"  12 04 "om_x"
 *   field8 payload {"a":1}    : 42 07  7B 22 61 22 3A 31 7D
 */
static const BYTE_T KNOWN_FRAME[] = {
    0x08, 0x05,
    0x18, 0x02,
    0x20, 0x01,
    0x2A, 0x0D, 0x0A, 0x04, 't', 'y', 'p', 'e',
                0x12, 0x05, 'e', 'v', 'e', 'n', 't',
    0x2A, 0x12, 0x0A, 0x0A, 'm', 'e', 's', 's', 'a', 'g', 'e', '_', 'i', 'd',
                0x12, 0x04, 'o', 'm', '_', 'x',
    0x42, 0x07, '{', '"', 'a', '"', ':', '1', '}',
};

static void test_parse_known_frame(void)
{
    FEISHU_FRAME_T f;
    EXPECT_OK(feishu_frame_parse(KNOWN_FRAME, sizeof(KNOWN_FRAME), &f),
              "parse known frame ok");
    EXPECT_EQ(f.method, FEISHU_METHOD_DATA, "method=DATA");
    EXPECT_EQ((long long)f.seq_id, 5, "seq_id=5");
    EXPECT_EQ(f.service, 2, "service=2");
    EXPECT_EQ(f.header_count, 2, "two headers");

    CONST CHAR_T *type = feishu_frame_header(&f, "type");
    CONST CHAR_T *mid = feishu_frame_header(&f, "message_id");
    EXPECT_STR_EQ(type, "event", "header type=event");
    EXPECT_STR_EQ(mid, "om_x", "header message_id=om_x");
    EXPECT_NULL(feishu_frame_header(&f, "absent"), "missing header -> NULL");

    EXPECT_EQ(f.payload_len, 7, "payload len=7");
    EXPECT(f.payload && memcmp(f.payload, "{\"a\":1}", 7) == 0, "payload bytes match");
}

static void test_encode_decode_roundtrip(void)
{
    FEISHU_FRAME_T src;
    memset(&src, 0, sizeof(src));
    src.seq_id = 0x1122334455667788ULL; /* multi-byte varint */
    src.log_id = 42;
    src.service = 9;
    src.method = FEISHU_METHOD_DATA;
    src.header_count = 2;
    strcpy(src.headers[0].key, "type");
    strcpy(src.headers[0].value, "event");
    strcpy(src.headers[1].key, "message_id");
    strcpy(src.headers[1].value, "om_roundtrip_1");
    src.payload = (CONST BYTE_T *)"{\"schema\":\"2.0\"}";
    src.payload_len = 16;

    BYTE_T buf[512];
    UINT_T n = 0;
    EXPECT_OK(feishu_frame_encode(&src, buf, sizeof(buf), &n), "encode ok");
    EXPECT(n > 0, "encoded length > 0");

    FEISHU_FRAME_T dst;
    EXPECT_OK(feishu_frame_parse(buf, n, &dst), "decode ok");
    EXPECT_EQ((long long)dst.seq_id, (long long)src.seq_id, "seq_id roundtrip");
    EXPECT_EQ((long long)dst.log_id, 42, "log_id roundtrip");
    EXPECT_EQ(dst.service, 9, "service roundtrip");
    EXPECT_EQ(dst.method, FEISHU_METHOD_DATA, "method roundtrip");
    EXPECT_EQ(dst.header_count, 2, "header_count roundtrip");
    EXPECT_STR_EQ(feishu_frame_header(&dst, "type"), "event", "type roundtrip");
    EXPECT_STR_EQ(feishu_frame_header(&dst, "message_id"), "om_roundtrip_1",
                  "message_id roundtrip");
    EXPECT_EQ(dst.payload_len, 16, "payload_len roundtrip");
    EXPECT(dst.payload && memcmp(dst.payload, src.payload, 16) == 0,
           "payload roundtrip");
}

static void test_build_ping(void)
{
    BYTE_T buf[64];
    UINT_T n = 0;
    EXPECT_OK(feishu_frame_build_ping(2, buf, sizeof(buf), &n), "build_ping ok");

    FEISHU_FRAME_T f;
    EXPECT_OK(feishu_frame_parse(buf, n, &f), "parse ping ok");
    EXPECT_EQ(f.method, FEISHU_METHOD_CONTROL, "ping method=CONTROL");
    EXPECT_EQ(f.service, 2, "ping service echoed");
    EXPECT_STR_EQ(feishu_frame_header(&f, "type"), "ping", "ping header type=ping");
    EXPECT_EQ(f.payload_len, 0, "ping has no payload");
}

static void test_build_ack(void)
{
    FEISHU_FRAME_T recv;
    EXPECT_OK(feishu_frame_parse(KNOWN_FRAME, sizeof(KNOWN_FRAME), &recv),
              "parse recv for ack");

    BYTE_T buf[256];
    UINT_T n = 0;
    EXPECT_OK(feishu_frame_build_ack(&recv, buf, sizeof(buf), &n), "build_ack ok");

    FEISHU_FRAME_T ack;
    EXPECT_OK(feishu_frame_parse(buf, n, &ack), "parse ack ok");
    /* Headers (and identity fields) are echoed verbatim. */
    EXPECT_EQ((long long)ack.seq_id, 5, "ack echoes seq_id");
    EXPECT_EQ(ack.service, 2, "ack echoes service");
    EXPECT_EQ(ack.method, recv.method, "ack echoes method");
    EXPECT_STR_EQ(feishu_frame_header(&ack, "type"), "event", "ack echoes type");
    EXPECT_STR_EQ(feishu_frame_header(&ack, "message_id"), "om_x",
                  "ack echoes message_id");
    /* Payload is replaced by the fixed ACK body. */
    EXPECT_EQ(ack.payload_len, 12, "ack payload len");
    EXPECT(ack.payload && memcmp(ack.payload, "{\"code\":200}", 12) == 0,
           "ack payload {\"code\":200}");
}

static void test_malformed_inputs(void)
{
    FEISHU_FRAME_T f;

    /* NULL args. */
    EXPECT_ERR(feishu_frame_parse(NULL, 4, &f), OPRT_INVALID_PARM, "parse NULL buf");
    EXPECT_ERR(feishu_frame_parse(KNOWN_FRAME, 4, NULL), OPRT_INVALID_PARM,
               "parse NULL frame");

    /* Empty buffer: a valid (if trivial) frame, method defaults to CONTROL. */
    EXPECT_OK(feishu_frame_parse(KNOWN_FRAME, 0, &f), "empty buffer parses");
    EXPECT_EQ(f.method, FEISHU_METHOD_CONTROL, "empty frame method default");
    EXPECT_EQ(f.header_count, 0, "empty frame no headers");

    /* Truncated varint: tag present, value byte missing. */
    static const BYTE_T trunc_varint[] = { 0x08 };
    EXPECT_ERR(feishu_frame_parse(trunc_varint, sizeof(trunc_varint), &f),
               OPRT_INVALID_PARM, "truncated varint -> error");

    /* Length-delimited field claims more bytes than remain. */
    static const BYTE_T trunc_bytes[] = { 0x42, 0x05, 0x7B, 0x22 };
    EXPECT_ERR(feishu_frame_parse(trunc_bytes, sizeof(trunc_bytes), &f),
               OPRT_INVALID_PARM, "over-long length -> error");

    /* Length = 2^32 + 5, buffer holds exactly 5 trailing bytes. A 32-bit
     * (UINT_T)n truncates to 5 and the field looks perfectly in-bounds;
     * only a 64-bit compare rejects it. */
    static const BYTE_T huge_len[] = { 0x42, 0x85, 0x80, 0x80, 0x80, 0x10,
                                       'p', 'a', 'y', 'l', 'o' };
    EXPECT_ERR(feishu_frame_parse(huge_len, sizeof(huge_len), &f),
               OPRT_INVALID_PARM, "64-bit length truncation -> error");

    /* Length = 0xFFFFFFFF: (UINT_T)pos + n wraps 32-bit to a small value that
     * passes `pos + n > len`, but a subtract-form check rejects it. */
    static const BYTE_T wrap_len[] = { 0x42, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F };
    EXPECT_ERR(feishu_frame_parse(wrap_len, sizeof(wrap_len), &f),
               OPRT_INVALID_PARM, "length addition wraparound -> error");

    /* Header sub-message truncated inside field 5. */
    static const BYTE_T trunc_header[] = { 0x2A, 0x0D, 0x0A, 0x04, 't', 'y' };
    EXPECT_ERR(feishu_frame_parse(trunc_header, sizeof(trunc_header), &f),
               OPRT_INVALID_PARM, "truncated header -> error");

    /* Unknown/unskippable wire type (field3 wire-type 7). */
    static const BYTE_T bad_wire[] = { 0x1F, 0x00 };
    EXPECT_ERR(feishu_frame_parse(bad_wire, sizeof(bad_wire), &f),
               OPRT_INVALID_PARM, "bad wire type -> error");

    /* Prefix of the known frame, cut mid-header. */
    EXPECT_ERR(feishu_frame_parse(KNOWN_FRAME, 10, &f), OPRT_INVALID_PARM,
               "cut mid-frame -> error");
}

static void test_encode_buffer_too_small(void)
{
    BYTE_T tiny[2];
    UINT_T n = 0;
    EXPECT_ERR(feishu_frame_build_ping(2, tiny, sizeof(tiny), &n),
               OPRT_BUFFER_NOT_ENOUGH, "ping into tiny buffer -> error");

    FEISHU_FRAME_T recv;
    feishu_frame_parse(KNOWN_FRAME, sizeof(KNOWN_FRAME), &recv);
    EXPECT_ERR(feishu_frame_build_ack(&recv, tiny, sizeof(tiny), &n),
               OPRT_BUFFER_NOT_ENOUGH, "ack into tiny buffer -> error");
}

int main(void)
{
    test_parse_known_frame();
    test_encode_decode_roundtrip();
    test_build_ping();
    test_build_ack();
    test_malformed_inputs();
    test_encode_buffer_too_small();
    TEST_END();
}
