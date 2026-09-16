#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

mkdir -p "$TMP_DIR/utilities"

cat > "$TMP_DIR/tuya_cloud_types.h" <<'EOF'
#ifndef __TUYA_CLOUD_TYPES_H__
#define __TUYA_CLOUD_TYPES_H__

#include <stdint.h>
#include <stddef.h>

typedef int OPERATE_RET;
typedef int INT_T;
typedef unsigned int UINT_T;
typedef int BOOL_T;
typedef char CHAR_T;
typedef void VOID;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define CONST const
#define STATIC static
#define VOID_T void

#define OPRT_OK 0
#define OPRT_INVALID_PARM (-1)
#define OPRT_NOT_SUPPORTED (-2)
#define OPRT_MALLOC_FAILED (-3)
#define OPRT_COM_ERROR (-4)
#define OPRT_NOT_FOUND (-5)

#define TUYA_CHECK_NULL_RETURN(x, ret) \
    do { \
        if ((x) == NULL) { \
            return (ret); \
        } \
    } while (0)

#endif
EOF

cat > "$TMP_DIR/tal_log.h" <<'EOF'
#ifndef __TAL_LOG_H__
#define __TAL_LOG_H__

#define TAL_PR_DEBUG(...) do { } while (0)
#define TAL_PR_ERR(...) do { } while (0)

#endif
EOF

cat > "$TMP_DIR/tal_memory.h" <<'EOF'
#ifndef __TAL_MEMORY_H__
#define __TAL_MEMORY_H__

#include <stdlib.h>

#define tal_calloc calloc
#define tal_free free

#endif
EOF

cat > "$TMP_DIR/utilities/mix_method.h" <<'EOF'
#ifndef __MIX_METHOD_H__
#define __MIX_METHOD_H__

#include <string.h>
#include <stdlib.h>

static inline char *mm_strdup(const char *src)
{
    if (src == NULL) {
        return NULL;
    }
    return strdup(src);
}

#endif
EOF

cat > "$TMP_DIR/ty_cJSON.h" <<'EOF'
#ifndef __TY_CJSON_H__
#define __TY_CJSON_H__

#include "tuya_cloud_types.h"

typedef struct ty_cJSON {
    int type;
    CHAR_T *string;
    CHAR_T *valuestring;
    INT_T valueint;
    struct ty_cJSON *child;
    struct ty_cJSON *next;
} ty_cJSON;

#define TY_CJSON_OBJECT 1
#define TY_CJSON_STRING 2
#define TY_CJSON_NUMBER 3
#define TY_CJSON_NULL   4

ty_cJSON *ty_cJSON_CreateObject(void);
ty_cJSON *ty_cJSON_CreateString(const CHAR_T *value);
ty_cJSON *ty_cJSON_CreateNumber(INT_T value);
ty_cJSON *ty_cJSON_CreateNull(void);
void ty_cJSON_AddItemToObject(ty_cJSON *object, const CHAR_T *key, ty_cJSON *item);
void ty_cJSON_AddStringToObject(ty_cJSON *object, const CHAR_T *key, const CHAR_T *value);
void ty_cJSON_AddNumberToObject(ty_cJSON *object, const CHAR_T *key, INT_T value);
void ty_cJSON_AddNullToObject(ty_cJSON *object, const CHAR_T *key);
ty_cJSON *ty_cJSON_GetObjectItem(const ty_cJSON *object, const CHAR_T *key);
ty_cJSON *ty_cJSON_Duplicate(const ty_cJSON *item, int recurse);
CHAR_T *ty_cJSON_PrintUnformatted(const ty_cJSON *item);
void ty_cJSON_FreeBuffer(CHAR_T *buffer);
void ty_cJSON_Delete(ty_cJSON *item);
ty_cJSON *ty_cJSON_Parse(const CHAR_T *text);

static inline BOOL_T ty_cJSON_IsObject(const ty_cJSON *item) { return item && item->type == TY_CJSON_OBJECT; }
static inline BOOL_T ty_cJSON_IsString(const ty_cJSON *item) { return item && item->type == TY_CJSON_STRING; }
static inline BOOL_T ty_cJSON_IsNumber(const ty_cJSON *item) { return item && item->type == TY_CJSON_NUMBER; }
static inline BOOL_T ty_cJSON_IsNull(const ty_cJSON *item) { return item && item->type == TY_CJSON_NULL; }

#endif
EOF

cat > "$TMP_DIR/ty_cJSON_stub.c" <<'EOF'
#include "ty_cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ty_cJSON *new_item(int type)
{
    ty_cJSON *item = calloc(1, sizeof(*item));
    if (item != NULL) {
        item->type = type;
    }
    return item;
}

ty_cJSON *ty_cJSON_CreateObject(void) { return new_item(TY_CJSON_OBJECT); }

ty_cJSON *ty_cJSON_CreateString(const CHAR_T *value)
{
    ty_cJSON *item = new_item(TY_CJSON_STRING);
    if (item != NULL && value != NULL) {
        item->valuestring = strdup(value);
    }
    return item;
}

ty_cJSON *ty_cJSON_CreateNumber(INT_T value)
{
    ty_cJSON *item = new_item(TY_CJSON_NUMBER);
    if (item != NULL) {
        item->valueint = value;
    }
    return item;
}

ty_cJSON *ty_cJSON_CreateNull(void) { return new_item(TY_CJSON_NULL); }

void ty_cJSON_AddItemToObject(ty_cJSON *object, const CHAR_T *key, ty_cJSON *item)
{
    ty_cJSON *tail = NULL;

    if (object == NULL || item == NULL) {
        return;
    }

    item->string = key ? strdup(key) : NULL;
    if (object->child == NULL) {
        object->child = item;
        return;
    }

    tail = object->child;
    while (tail->next != NULL) {
        tail = tail->next;
    }
    tail->next = item;
}

void ty_cJSON_AddStringToObject(ty_cJSON *object, const CHAR_T *key, const CHAR_T *value)
{
    ty_cJSON_AddItemToObject(object, key, ty_cJSON_CreateString(value));
}

void ty_cJSON_AddNumberToObject(ty_cJSON *object, const CHAR_T *key, INT_T value)
{
    ty_cJSON_AddItemToObject(object, key, ty_cJSON_CreateNumber(value));
}

void ty_cJSON_AddNullToObject(ty_cJSON *object, const CHAR_T *key)
{
    ty_cJSON_AddItemToObject(object, key, ty_cJSON_CreateNull());
}

ty_cJSON *ty_cJSON_GetObjectItem(const ty_cJSON *object, const CHAR_T *key)
{
    ty_cJSON *child = NULL;

    if (object == NULL || key == NULL) {
        return NULL;
    }

    child = object->child;
    while (child != NULL) {
        if (child->string != NULL && strcmp(child->string, key) == 0) {
            return child;
        }
        child = child->next;
    }

    return NULL;
}

ty_cJSON *ty_cJSON_Duplicate(const ty_cJSON *item, int recurse)
{
    ty_cJSON *dup = NULL;
    ty_cJSON *child = NULL;

    if (item == NULL) {
        return NULL;
    }

    dup = new_item(item->type);
    if (dup == NULL) {
        return NULL;
    }

    if (item->string != NULL) {
        dup->string = strdup(item->string);
    }
    if (item->valuestring != NULL) {
        dup->valuestring = strdup(item->valuestring);
    }
    dup->valueint = item->valueint;

    if (recurse && item->child != NULL) {
        child = item->child;
        while (child != NULL) {
            ty_cJSON_AddItemToObject(dup, child->string, ty_cJSON_Duplicate(child, 1));
            child = child->next;
        }
    }

    return dup;
}

static void append_str(char **buf, size_t *cap, size_t *len, const char *text)
{
    size_t need = strlen(text);
    if (*len + need + 1 > *cap) {
        *cap = (*len + need + 1) * 2;
        *buf = realloc(*buf, *cap);
    }
    memcpy(*buf + *len, text, need);
    *len += need;
    (*buf)[*len] = '\0';
}

static void print_item(const ty_cJSON *item, char **buf, size_t *cap, size_t *len)
{
    char num_buf[32];
    ty_cJSON *child = NULL;
    BOOL_T first = TRUE;

    if (item == NULL) {
        append_str(buf, cap, len, "null");
        return;
    }

    switch (item->type) {
    case TY_CJSON_OBJECT:
        append_str(buf, cap, len, "{");
        child = item->child;
        while (child != NULL) {
            if (!first) {
                append_str(buf, cap, len, ",");
            }
            first = FALSE;
            append_str(buf, cap, len, "\"");
            append_str(buf, cap, len, child->string ? child->string : "");
            append_str(buf, cap, len, "\":");
            print_item(child, buf, cap, len);
            child = child->next;
        }
        append_str(buf, cap, len, "}");
        break;
    case TY_CJSON_STRING:
        append_str(buf, cap, len, "\"");
        append_str(buf, cap, len, item->valuestring ? item->valuestring : "");
        append_str(buf, cap, len, "\"");
        break;
    case TY_CJSON_NUMBER:
        snprintf(num_buf, sizeof(num_buf), "%d", item->valueint);
        append_str(buf, cap, len, num_buf);
        break;
    case TY_CJSON_NULL:
    default:
        append_str(buf, cap, len, "null");
        break;
    }
}

CHAR_T *ty_cJSON_PrintUnformatted(const ty_cJSON *item)
{
    size_t cap = 128;
    size_t len = 0;
    char *buf = calloc(1, cap);
    if (buf == NULL) {
        return NULL;
    }
    print_item(item, &buf, &cap, &len);
    return buf;
}

void ty_cJSON_FreeBuffer(CHAR_T *buffer)
{
    free(buffer);
}

void ty_cJSON_Delete(ty_cJSON *item)
{
    ty_cJSON *next = NULL;
    ty_cJSON *child = NULL;

    if (item == NULL) {
        return;
    }

    child = item->child;
    while (child != NULL) {
        next = child->next;
        ty_cJSON_Delete(child);
        child = next;
    }

    free(item->string);
    free(item->valuestring);
    free(item);
}

ty_cJSON *ty_cJSON_Parse(const CHAR_T *text)
{
    (void)text;
    return NULL;
}
EOF

cat > "$TMP_DIR/test_wukong_cron_rpc.c" <<'EOF'
#include <stdio.h>

#include "wukong_cron_rpc.h"

static OPERATE_RET echo_handler(const ty_cJSON *params, ty_cJSON **result)
{
    (void)params;
    *result = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(*result, "status", "ok");
    return OPRT_OK;
}

static int expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "%s\n", msg);
        return 1;
    }
    return 0;
}

int main(void)
{
    ty_cJSON *request = NULL;
    ty_cJSON *response = NULL;
    ty_cJSON *node = NULL;

    if (expect(wukong_cron_rpc_init() == OPRT_OK, "expected rpc init to succeed")) {
        return 1;
    }

    if (expect(wukong_cron_rpc_method_register("echo", echo_handler) == OPRT_OK,
               "expected method register to succeed")) {
        return 1;
    }

    request = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(request, "jsonrpc", "2.0");
    ty_cJSON_AddStringToObject(request, "id", "1");
    ty_cJSON_AddStringToObject(request, "method", "echo");
    ty_cJSON_AddItemToObject(request, "params", ty_cJSON_CreateObject());

    if (expect(wukong_cron_rpc_execute_json(request, &response) == OPRT_OK,
               "expected valid rpc request to execute")) {
        return 1;
    }

    node = ty_cJSON_GetObjectItem(response, "result");
    if (expect(node != NULL && ty_cJSON_IsObject(node), "expected response.result object")) {
        return 1;
    }
    node = ty_cJSON_GetObjectItem(node, "status");
    if (expect(node != NULL && ty_cJSON_IsString(node), "expected response.result.status string")) {
        return 1;
    }

    ty_cJSON_Delete(request);
    ty_cJSON_Delete(response);

    request = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(request, "jsonrpc", "1.0");
    ty_cJSON_AddStringToObject(request, "id", "2");
    ty_cJSON_AddStringToObject(request, "method", "echo");

    if (expect(wukong_cron_rpc_execute_json(request, &response) == OPRT_OK,
               "expected invalid request to return rpc error response")) {
        return 1;
    }

    node = ty_cJSON_GetObjectItem(response, "error");
    if (expect(node != NULL && ty_cJSON_IsObject(node), "expected error object for invalid request")) {
        return 1;
    }
    node = ty_cJSON_GetObjectItem(node, "code");
    if (expect(node != NULL && ty_cJSON_IsNumber(node) &&
               node->valueint == WUKONG_CRON_RPC_ERR_INVALID_REQUEST,
               "expected invalid request error code")) {
        return 1;
    }

    ty_cJSON_Delete(request);
    ty_cJSON_Delete(response);

    request = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(request, "jsonrpc", "2.0");
    ty_cJSON_AddStringToObject(request, "id", "3");
    ty_cJSON_AddStringToObject(request, "method", "missing");

    if (expect(wukong_cron_rpc_execute_json(request, &response) == OPRT_OK,
               "expected missing method to return rpc error response")) {
        return 1;
    }

    node = ty_cJSON_GetObjectItem(response, "error");
    node = ty_cJSON_GetObjectItem(node, "code");
    if (expect(node != NULL && ty_cJSON_IsNumber(node) &&
               node->valueint == WUKONG_CRON_RPC_ERR_METHOD_NOT_FOUND,
               "expected method not found error code")) {
        return 1;
    }

    ty_cJSON_Delete(request);
    ty_cJSON_Delete(response);
    wukong_cron_rpc_deinit();

    return 0;
}
EOF

cc -D_GNU_SOURCE -std=c99 -Wall -Wextra -Werror \
    -I"$TMP_DIR" \
    -I"$TMP_DIR/utilities" \
    -I"$ROOT_DIR/src/wukong/cron" \
    "$TMP_DIR/ty_cJSON_stub.c" \
    "$ROOT_DIR/src/wukong/cron/wukong_cron_rpc.c" \
    "$TMP_DIR/test_wukong_cron_rpc.c" \
    -o "$TMP_DIR/test_wukong_cron_rpc"

"$TMP_DIR/test_wukong_cron_rpc"
