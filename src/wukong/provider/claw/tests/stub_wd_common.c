/*
 * wd_common_* KV double: small in-memory key/value store so code that
 * reads/writes KV via wd_common (services self-managing their own secrets)
 * can be exercised on the host. __test_kv_reset() clears entries between cases.
 */
#include "tuya_ws_db.h"
#include <stdlib.h>
#include <string.h>

#define WD_STUB_MAX_ENTRIES 8
#define WD_STUB_KEY_LEN     64

typedef struct {
    CHAR_T key[WD_STUB_KEY_LEN];
    BYTE_T *value;
    UINT_T len;
    BOOL_T used;
} wd_stub_entry_t;

static wd_stub_entry_t s_entries[WD_STUB_MAX_ENTRIES];
static int s_free_count = 0; /* bumped by wd_common_free_data; lets tests assert no leak */

static wd_stub_entry_t *__wd_stub_find(CONST CHAR_T *key)
{
    int i;
    for (i = 0; i < WD_STUB_MAX_ENTRIES; i++) {
        if (s_entries[i].used && strcmp(s_entries[i].key, key) == 0) {
            return &s_entries[i];
        }
    }
    return NULL;
}

static wd_stub_entry_t *__wd_stub_alloc(CONST CHAR_T *key)
{
    int i;
    UINT_T klen = (UINT_T)strlen(key);
    if (klen >= WD_STUB_KEY_LEN) {
        klen = WD_STUB_KEY_LEN - 1;
    }
    for (i = 0; i < WD_STUB_MAX_ENTRIES; i++) {
        if (!s_entries[i].used) {
            memcpy(s_entries[i].key, key, klen);
            s_entries[i].key[klen] = '\0';
            s_entries[i].used = TRUE;
            return &s_entries[i];
        }
    }
    return NULL;
}

/* Test control: wipe the whole store between cases. */
void __test_kv_reset(void)
{
    int i;
    for (i = 0; i < WD_STUB_MAX_ENTRIES; i++) {
        if (s_entries[i].value) {
            free(s_entries[i].value);
        }
    }
    memset(s_entries, 0, sizeof(s_entries));
    s_free_count = 0;
}

/* Test control: number of wd_common_free_data() calls since the last reset. */
int __test_free_count(void)
{
    return s_free_count;
}

OPERATE_RET wd_common_write(CONST CHAR_T *key, CONST BYTE_T *value, UINT_T len)
{
    if (NULL == key || NULL == value) {
        return OPRT_INVALID_PARM;
    }
    wd_stub_entry_t *e = __wd_stub_find(key);
    if (NULL == e) {
        e = __wd_stub_alloc(key);
    }
    if (NULL == e) {
        return OPRT_EXCEED_UPPER_LIMIT;
    }
    if (e->value) {
        free(e->value);
    }
    /* malloc(0) is implementation-defined (may return NULL); always allocate
     * at least 1 byte so a stored empty string ("") still yields a non-NULL
     * value pointer, matching real KV backends and exercising the caller's
     * "value non-NULL but len == 0" edge case deterministically. */
    e->value = malloc(len ? len : 1);
    if (NULL == e->value) {
        return OPRT_MALLOC_FAILED;
    }
    memcpy(e->value, value, len);
    e->len = len;
    return OPRT_OK;
}

OPERATE_RET wd_common_read(CONST CHAR_T *key, BYTE_T **value, UINT_T *len)
{
    if (NULL == key || NULL == value || NULL == len) {
        return OPRT_INVALID_PARM;
    }
    wd_stub_entry_t *e = __wd_stub_find(key);
    if (NULL == e) {
        return OPRT_NOT_FOUND;
    }
    *value = malloc(e->len ? e->len : 1);
    if (NULL == *value) {
        return OPRT_MALLOC_FAILED;
    }
    memcpy(*value, e->value, e->len);
    *len = e->len;
    return OPRT_OK;
}

OPERATE_RET wd_common_delete(CONST CHAR_T *key)
{
    wd_stub_entry_t *e = __wd_stub_find(key);
    if (NULL == e) {
        return OPRT_NOT_FOUND;
    }
    free(e->value);
    memset(e, 0, sizeof(*e));
    return OPRT_OK;
}

VOID wd_common_free_data(BYTE_T *data)
{
    s_free_count++;
    free(data);
}
