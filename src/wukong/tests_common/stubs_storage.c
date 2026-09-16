/*
 * Shared host-test double for the wukong storage record layer.
 *
 * Feature code (tm alarm/reminder) persists through
 * wukong_storage_{write,read,delete,free}() (see wukong_storage.h). On
 * firmware those live in wukong_storage_record.c on top of tkl_fs; on host
 * this file provides a RAM-backed double under the same symbols so feature
 * suites exercise their persistence paths without a filesystem. The double
 * honours the record-layer contract: overwrite semantics, missing entry =>
 * OPRT_NOT_FOUND with *data NULL / *len 0, read buffers are len+1 bytes with
 * byte [len] = '\0', delete is idempotent, free is NULL-safe.
 *
 * Each host test is its own process, so the RAM table starts empty; suites
 * that want a clean slate mid-test can call stub_storage_reset().
 *
 * (The record layer's own contract is tested against its REAL implementation
 * in storage/tests/ — this double is only for suites whose subject is the
 * feature code above it.)
 */
#include "wukong_storage.h"

#include <stdlib.h>
#include <string.h>

#define STUB_STORAGE_MAX_ENTRIES  16
#define STUB_STORAGE_NS_MAX       16
#define STUB_STORAGE_NAME_MAX     64

typedef struct {
    BOOL_T used;
    CHAR_T ns[STUB_STORAGE_NS_MAX];
    CHAR_T name[STUB_STORAGE_NAME_MAX];
    BYTE_T *data;
    UINT_T len;
} stub_storage_entry_t;

static stub_storage_entry_t s_tbl[STUB_STORAGE_MAX_ENTRIES];

static stub_storage_entry_t *find_entry(CONST CHAR_T *ns, CONST CHAR_T *name)
{
    INT_T i;
    for (i = 0; i < STUB_STORAGE_MAX_ENTRIES; i++) {
        if (s_tbl[i].used &&
            strcmp(s_tbl[i].ns, ns) == 0 &&
            strcmp(s_tbl[i].name, name) == 0) {
            return &s_tbl[i];
        }
    }
    return NULL;
}

OPERATE_RET wukong_storage_write(CONST CHAR_T *ns, CONST CHAR_T *name,
                                 CONST BYTE_T *data, UINT_T len)
{
    stub_storage_entry_t *e;
    BYTE_T *buf;
    INT_T i;

    if (ns == NULL || name == NULL || (data == NULL && len > 0)) {
        return OPRT_INVALID_PARM;
    }

    e = find_entry(ns, name);
    if (e == NULL) {
        for (i = 0; i < STUB_STORAGE_MAX_ENTRIES; i++) {
            if (!s_tbl[i].used) {
                e = &s_tbl[i];
                break;
            }
        }
        if (e == NULL) {
            return OPRT_COM_ERROR;
        }
    }

    buf = (BYTE_T *)malloc(len ? len : 1);
    if (buf == NULL) {
        return OPRT_MALLOC_FAILED;
    }
    if (len) {
        memcpy(buf, data, len);
    }

    if (e->data) {
        free(e->data);
    }
    e->used = TRUE;
    strncpy(e->ns, ns, sizeof(e->ns) - 1);
    e->ns[sizeof(e->ns) - 1] = '\0';
    strncpy(e->name, name, sizeof(e->name) - 1);
    e->name[sizeof(e->name) - 1] = '\0';
    e->data = buf;
    e->len = len;
    return OPRT_OK;
}

OPERATE_RET wukong_storage_read(CONST CHAR_T *ns, CONST CHAR_T *name,
                                BYTE_T **data, UINT_T *len)
{
    stub_storage_entry_t *e;
    BYTE_T *buf;

    if (ns == NULL || name == NULL || data == NULL || len == NULL) {
        return OPRT_INVALID_PARM;
    }
    *data = NULL;
    *len = 0;

    e = find_entry(ns, name);
    if (e == NULL) {
        return OPRT_NOT_FOUND;
    }

    /* NUL-terminated like the real record layer so JSON parsers are safe. */
    buf = (BYTE_T *)malloc(e->len + 1);
    if (buf == NULL) {
        return OPRT_MALLOC_FAILED;
    }
    if (e->len) {
        memcpy(buf, e->data, e->len);
    }
    buf[e->len] = '\0';
    *data = buf;
    *len = e->len;
    return OPRT_OK;
}

OPERATE_RET wukong_storage_delete(CONST CHAR_T *ns, CONST CHAR_T *name)
{
    stub_storage_entry_t *e;

    if (ns == NULL || name == NULL) {
        return OPRT_INVALID_PARM;
    }
    e = find_entry(ns, name);
    if (e != NULL) {
        if (e->data) {
            free(e->data);
        }
        memset(e, 0, sizeof(*e));
    }
    return OPRT_OK;
}

VOID wukong_storage_free(BYTE_T *data)
{
    if (data != NULL) {
        free(data);
    }
}

VOID stub_storage_reset(VOID)
{
    INT_T i;
    for (i = 0; i < STUB_STORAGE_MAX_ENTRIES; i++) {
        if (s_tbl[i].data) {
            free(s_tbl[i].data);
        }
    }
    memset(s_tbl, 0, sizeof(s_tbl));
}
