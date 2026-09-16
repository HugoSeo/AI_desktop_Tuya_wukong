#pragma once
/* Host stub for device httpc.h — just the session/header surface the LLM
 * transport touches (copied from the retired run_test.sh inline stub). */
typedef void* http_session_t;
typedef void (*HTTP_HEAD_ADD_CB)(http_session_t, void*);
typedef int http_hdr_field_sel_t;
static inline int http_add_header(http_session_t s,const void*r,const char*n,const char*v){(void)s;(void)r;(void)n;(void)v;return 0;}
static inline int http_set_timeout(http_session_t s,int ms){(void)s;(void)ms;return 0;}
