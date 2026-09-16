#pragma once
/* Host stub for tuya_simple_http.h — opts/response structs + post signature
 * (stub_http.c provides the injectable implementation). */
#include "tuya_cloud_types.h"
#include "httpc.h"
#include <stdlib.h>
#define SIMPLE_HTTP_MALLOC malloc
#define SIMPLE_HTTP_FREE   free
typedef struct { HTTP_HEAD_ADD_CB add_head_cb; void*add_head_data;
                 HTTP_HEAD_ADD_CB resp_hdr_cb; void*resp_hdr_cb_data; int field_flags; } simple_http_opts_t;
typedef struct { BYTE_T *data; UINT_T len; INT_T http_code; } simple_http_response_t;
OPERATE_RET tuya_simple_http_post(const CHAR_T*,const BYTE_T*,const UINT_T,const simple_http_opts_t*,simple_http_response_t*);
