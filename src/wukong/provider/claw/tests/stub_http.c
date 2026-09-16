/* HTTP transport double: injectable canned response + last-request capture. */
#include "tuya_simple_http.h"
#include <string.h>
#include <stdlib.h>
static const char *s_body="{}"; static int s_code=200;
static char s_last_body[2048];
void __test_set_http_response(const char *b,int c){ s_body=b; s_code=c; }
const char *__test_get_last_body(void){ return s_last_body; }
OPERATE_RET tuya_simple_http_post(const CHAR_T*u,const BYTE_T*d,const UINT_T l,
                                  const simple_http_opts_t*o,simple_http_response_t*r){
    (void)u;(void)o;
    UINT_T copy_len = l < sizeof(s_last_body)-1 ? l : sizeof(s_last_body)-1;
    memcpy(s_last_body, d, copy_len); s_last_body[copy_len] = '\0';
    r->len=(UINT_T)strlen(s_body); r->data=(BYTE_T*)malloc(r->len+1);
    memcpy(r->data,s_body,r->len+1); r->http_code=s_code; return 0;
}
