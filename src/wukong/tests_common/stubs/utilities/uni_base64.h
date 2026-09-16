/*
 * Host-test stub of utilities/uni_base64.h: production code (now
 * wukong/toolkits/wukong_tool.c, formerly mcp_content.c) includes it for
 * TY_BASE64_BUF_LEN_CALC / tuya_base64_encode. Real implementation lives in
 * TuyaOS's tal_system_service component, unavailable on host; this stub is a
 * real (not fake) base64 encoder so any future test exercising
 * mcp_content_make_image/mcp_base64_encode gets correct output.
 */
#ifndef __UNI_BASE64_H__
#define __UNI_BASE64_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TY_BASE64_BUF_LEN_CALC(slen)   (((slen) / 3 + ((slen) % 3 != 0)) * 4 + 1)   /* 1 for '\0' */

static inline char *tuya_base64_encode(const unsigned char *bindata, char *base64, int binlength)
{
    static const char s_tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i, j;

    for (i = 0, j = 0; i + 2 < binlength; i += 3) {
        base64[j++] = s_tbl[(bindata[i] >> 2) & 0x3F];
        base64[j++] = s_tbl[((bindata[i] & 0x3) << 4) | ((bindata[i + 1] & 0xF0) >> 4)];
        base64[j++] = s_tbl[((bindata[i + 1] & 0xF) << 2) | ((bindata[i + 2] & 0xC0) >> 6)];
        base64[j++] = s_tbl[bindata[i + 2] & 0x3F];
    }

    if (i < binlength) {
        int rem = binlength - i;
        base64[j++] = s_tbl[(bindata[i] >> 2) & 0x3F];
        if (rem == 1) {
            base64[j++] = s_tbl[(bindata[i] & 0x3) << 4];
            base64[j++] = '=';
        } else {
            base64[j++] = s_tbl[((bindata[i] & 0x3) << 4) | ((bindata[i + 1] & 0xF0) >> 4)];
            base64[j++] = s_tbl[(bindata[i + 1] & 0xF) << 2];
        }
        base64[j++] = '=';
    }

    base64[j] = '\0';
    return base64;
}

static inline int tuya_base64_decode(const char *base64, unsigned char *bindata)
{
    (void)base64;
    (void)bindata;
    return -1;   /* not needed by any current host test */
}

#ifdef __cplusplus
}
#endif
#endif /* __UNI_BASE64_H__ */
