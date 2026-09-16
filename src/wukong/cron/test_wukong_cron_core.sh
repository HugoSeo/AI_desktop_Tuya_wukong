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
typedef uint64_t UINT64_T;
typedef int BOOL_T;
typedef char CHAR_T;
typedef long long TIME_T;
typedef void VOID;

#define CONST const
#define STATIC static
#define VOID_T void

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

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
#define TAL_PR_NOTICE(...) do { } while (0)

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

#include <stdlib.h>
#include <string.h>

static inline char *mm_strdup(const char *src)
{
    if (src == NULL) {
        return NULL;
    }
    return strdup(src);
}

#endif
EOF

cat > "$TMP_DIR/tal_time_service.h" <<'EOF'
#ifndef __TAL_TIME_SERVICE_H__
#define __TAL_TIME_SERVICE_H__

#include <time.h>
#include "tuya_cloud_types.h"

typedef struct tm POSIX_TM_S;

extern TIME_T g_fake_now;

static inline TIME_T tal_time_mktime(POSIX_TM_S *tm_info)
{
    return (TIME_T)timegm(tm_info);
}

static inline OPERATE_RET tal_time_get_local_time_custom(TIME_T ts, POSIX_TM_S *tm_info)
{
    time_t raw = (time_t)ts;
    return gmtime_r(&raw, tm_info) == NULL ? OPRT_INVALID_PARM : OPRT_OK;
}

static inline TIME_T tal_time_get_posix(void)
{
    return g_fake_now;
}

#endif
EOF

cat > "$TMP_DIR/tal_sw_timer.h" <<'EOF'
#ifndef __TAL_SW_TIMER_H__
#define __TAL_SW_TIMER_H__

#include "tuya_cloud_types.h"

typedef VOID *TIMER_ID;
typedef VOID (*TAL_TIMER_CB)(TIMER_ID timer_id, VOID_T *arg);

#define TAL_TIMER_ONCE 1
#define TAL_TIMER_CYCLE 2

OPERATE_RET tal_sw_timer_create(TAL_TIMER_CB cb, VOID_T *arg, TIMER_ID *timer_id);
OPERATE_RET tal_sw_timer_start(TIMER_ID timer_id, UINT_T timeout_ms, UINT_T type);
OPERATE_RET tal_sw_timer_stop(TIMER_ID timer_id);
OPERATE_RET tal_sw_timer_delete(TIMER_ID timer_id);
VOID test_timer_fire(TIMER_ID timer_id);

extern int g_timer_start_count;
extern int g_timer_arm_count;
extern TIMER_ID g_last_timer_id;

#endif
EOF

cat > "$TMP_DIR/tal_workq_service.h" <<'EOF'
#ifndef __TAL_WORKQ_SERVICE_H__
#define __TAL_WORKQ_SERVICE_H__

#include "tuya_cloud_types.h"

#define WORKQ_SYSTEM 0

OPERATE_RET tal_workq_schedule(INT_T queue, VOID (*cb)(VOID_T *data), VOID_T *data);

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
#define TY_CJSON_ARRAY  5

ty_cJSON *ty_cJSON_CreateObject(void);
ty_cJSON *ty_cJSON_CreateArray(void);
ty_cJSON *ty_cJSON_CreateString(const CHAR_T *value);
ty_cJSON *ty_cJSON_CreateNumber(INT_T value);
ty_cJSON *ty_cJSON_CreateNull(void);
void ty_cJSON_AddItemToObject(ty_cJSON *object, const CHAR_T *key, ty_cJSON *item);
void ty_cJSON_AddStringToObject(ty_cJSON *object, const CHAR_T *key, const CHAR_T *value);
void ty_cJSON_AddNumberToObject(ty_cJSON *object, const CHAR_T *key, INT_T value);
void ty_cJSON_AddNullToObject(ty_cJSON *object, const CHAR_T *key);
void ty_cJSON_AddBoolToObject(ty_cJSON *object, const CHAR_T *key, BOOL_T value);
void ty_cJSON_AddItemToArray(ty_cJSON *array, ty_cJSON *item);
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

cat > "$TMP_DIR/stubs.c" <<'EOF'
#include "ty_cJSON.h"
#include "tal_sw_timer.h"
#include "tal_workq_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_timer_start_count = 0;
int g_timer_arm_count = 0;
TIME_T g_fake_now = 0;
TIMER_ID g_last_timer_id = NULL;

typedef struct {
    TAL_TIMER_CB cb;
    VOID_T *arg;
    BOOL_T armed;
    BOOL_T in_callback;
    BOOL_T stop_called_in_callback;
} timer_stub_t;

static ty_cJSON *new_item(int type)
{
    ty_cJSON *item = calloc(1, sizeof(*item));
    if (item != NULL) {
        item->type = type;
    }
    return item;
}

ty_cJSON *ty_cJSON_CreateObject(void) { return new_item(TY_CJSON_OBJECT); }
ty_cJSON *ty_cJSON_CreateArray(void) { return new_item(TY_CJSON_ARRAY); }
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

static void add_child(ty_cJSON *parent, ty_cJSON *item)
{
    ty_cJSON *tail = NULL;
    if (parent == NULL || item == NULL) {
        return;
    }
    if (parent->child == NULL) {
        parent->child = item;
        return;
    }
    tail = parent->child;
    while (tail->next != NULL) {
        tail = tail->next;
    }
    tail->next = item;
}

void ty_cJSON_AddItemToObject(ty_cJSON *object, const CHAR_T *key, ty_cJSON *item)
{
    if (item != NULL) {
        item->string = key ? strdup(key) : NULL;
    }
    add_child(object, item);
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

void ty_cJSON_AddBoolToObject(ty_cJSON *object, const CHAR_T *key, BOOL_T value)
{
    ty_cJSON_AddNumberToObject(object, key, value ? 1 : 0);
}

void ty_cJSON_AddItemToArray(ty_cJSON *array, ty_cJSON *item)
{
    add_child(array, item);
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
    if (recurse) {
        child = item->child;
        while (child != NULL) {
            ty_cJSON *child_dup = ty_cJSON_Duplicate(child, 1);
            if (item->type == TY_CJSON_ARRAY) {
                ty_cJSON_AddItemToArray(dup, child_dup);
            } else {
                ty_cJSON_AddItemToObject(dup, child->string, child_dup);
            }
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
            if (!first) append_str(buf, cap, len, ",");
            first = FALSE;
            append_str(buf, cap, len, "\"");
            append_str(buf, cap, len, child->string ? child->string : "");
            append_str(buf, cap, len, "\":");
            print_item(child, buf, cap, len);
            child = child->next;
        }
        append_str(buf, cap, len, "}");
        break;
    case TY_CJSON_ARRAY:
        append_str(buf, cap, len, "[");
        child = item->child;
        while (child != NULL) {
            if (!first) append_str(buf, cap, len, ",");
            first = FALSE;
            print_item(child, buf, cap, len);
            child = child->next;
        }
        append_str(buf, cap, len, "]");
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

void ty_cJSON_FreeBuffer(CHAR_T *buffer) { free(buffer); }

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

static ty_cJSON *make_request_object(const CHAR_T *id, const CHAR_T *method, const CHAR_T *message)
{
    ty_cJSON *request = ty_cJSON_CreateObject();
    ty_cJSON *params = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(request, "jsonrpc", "2.0");
    ty_cJSON_AddStringToObject(request, "id", id);
    ty_cJSON_AddStringToObject(request, "method", method);
    ty_cJSON_AddStringToObject(params, "message", message);
    ty_cJSON_AddItemToObject(request, "params", params);
    return request;
}

ty_cJSON *ty_cJSON_Parse(const CHAR_T *text)
{
    ty_cJSON *root = NULL;
    if (text == NULL) {
        return NULL;
    }
    if (strstr(text, "\"cron\"") != NULL && strstr(text, "\"request\"") != NULL) {
        const CHAR_T *id = NULL;
        const CHAR_T *name = "demo";
        const CHAR_T *cron = "*/15 * * * * *";
        const CHAR_T *request_id = "req-1";
        const CHAR_T *method = "echo";
        const CHAR_T *message = "hello";

        if (strstr(text, "\"id\":\"job-1\"") != NULL) {
            id = "job-1";
        }
        if (strstr(text, "\"cron\":\"0 */1 * * * *\"") != NULL) {
            cron = "0 */1 * * * *";
        }

        root = ty_cJSON_CreateObject();
        if (id != NULL) {
            ty_cJSON_AddStringToObject(root, "id", id);
        }
        ty_cJSON_AddStringToObject(root, "name", name);
        ty_cJSON_AddNumberToObject(root, "enabled", 1);
        if (strstr(text, "\"once\":1") != NULL) {
            ty_cJSON_AddNumberToObject(root, "once", 1);
        }
        ty_cJSON_AddStringToObject(root, "cron", cron);
        ty_cJSON_AddItemToObject(root, "request", make_request_object(request_id, method, message));
        return root;
    }
    if (strstr(text, "\"jsonrpc\"") != NULL && strstr(text, "\"method\"") != NULL) {
        const CHAR_T *request_id = "req-1";
        const CHAR_T *method = "echo";
        const CHAR_T *message = "hello";

        return make_request_object(request_id, method, message);
    }
    return NULL;
}

OPERATE_RET tal_sw_timer_create(TAL_TIMER_CB cb, VOID_T *arg, TIMER_ID *timer_id)
{
    timer_stub_t *timer = calloc(1, sizeof(*timer));
    if (timer == NULL) {
        return OPRT_MALLOC_FAILED;
    }
    timer->cb = cb;
    timer->arg = arg;
    *timer_id = timer;
    g_last_timer_id = timer;
    return OPRT_OK;
}

OPERATE_RET tal_sw_timer_start(TIMER_ID timer_id, UINT_T timeout_ms, UINT_T type)
{
    timer_stub_t *timer = (timer_stub_t *)timer_id;
    (void)timeout_ms;
    (void)type;
    if (timer == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (timer->stop_called_in_callback) {
        timer->armed = FALSE;
        return OPRT_COM_ERROR;
    }
    timer->armed = TRUE;
    g_timer_start_count++;
    g_timer_arm_count++;
    return OPRT_OK;
}

OPERATE_RET tal_sw_timer_stop(TIMER_ID timer_id)
{
    timer_stub_t *timer = (timer_stub_t *)timer_id;
    if (timer == NULL) {
        return OPRT_INVALID_PARM;
    }
    timer->armed = FALSE;
    if (timer->in_callback) {
        timer->stop_called_in_callback = TRUE;
    }
    return OPRT_OK;
}

OPERATE_RET tal_sw_timer_delete(TIMER_ID timer_id)
{
    free(timer_id);
    return OPRT_OK;
}

OPERATE_RET tal_workq_schedule(INT_T queue, VOID (*cb)(VOID_T *data), VOID_T *data)
{
    (void)queue;
    cb(data);
    return OPRT_OK;
}

VOID test_timer_fire(TIMER_ID timer_id)
{
    timer_stub_t *timer = (timer_stub_t *)timer_id;

    if (timer == NULL || !timer->armed) {
        return;
    }

    timer->armed = FALSE;
    timer->in_callback = TRUE;
    timer->stop_called_in_callback = FALSE;
    timer->cb(timer_id, timer->arg);
    timer->in_callback = FALSE;
}
EOF

cat > "$TMP_DIR/test_wukong_cron_core.c" <<'EOF'
#include <stdio.h>
#include <string.h>

#include "wukong_cron.h"
#include "tal_sw_timer.h"
#include "tal_time_service.h"

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
    OPERATE_RET rt = OPRT_OK;
    CHAR_T job_id[WUKONG_CRON_JOB_ID_LEN + 1] = {0};
    CHAR_T once_job_id[WUKONG_CRON_JOB_ID_LEN + 1] = {0};
    CHAR_T *job_list_json = NULL;
    CHAR_T *response_json = NULL;
    CONST CHAR_T *job_json =
        "{\"name\":\"demo\",\"enabled\":1,"
        "\"cron\":\"*/15 * * * * *\","
        "\"request\":{\"jsonrpc\":\"2.0\",\"id\":\"req-1\","
        "\"method\":\"echo\",\"params\":{\"message\":\"hello\"}}}";
    CONST CHAR_T *once_job_json =
        "{\"id\":\"once-job\",\"name\":\"once-demo\",\"enabled\":1,\"once\":1,"
        "\"cron\":\"0 */1 * * * *\","
        "\"request\":{\"jsonrpc\":\"2.0\",\"id\":\"req-once\","
        "\"method\":\"echo\",\"params\":{\"message\":\"once\"}}}";

    {
        POSIX_TM_S tm_info = {0};
        tm_info.tm_year = 2026 - 1900;
        tm_info.tm_mon = 2;
        tm_info.tm_mday = 6;
        tm_info.tm_hour = 10;
        tm_info.tm_min = 0;
        tm_info.tm_sec = 1;
        g_fake_now = (TIME_T)timegm(&tm_info);
    }

    if (expect(wukong_cron_init() == OPRT_OK, "expected cron init to succeed")) {
        return 1;
    }

    if (expect(wukong_cron_method_register("echo", echo_handler) == OPRT_OK,
               "expected method registration to succeed")) {
        return 1;
    }

    rt = wukong_cron_job_add(job_json, job_id, sizeof(job_id));
    if (expect(rt == OPRT_OK, "expected job add to succeed")) {
        return 1;
    }

    rt = wukong_cron_job_add(once_job_json, once_job_id, sizeof(once_job_id));
    if (expect(rt == OPRT_OK, "expected once job add to succeed")) {
        return 1;
    }

    if (expect(job_id[0] != '\0', "expected job id to be returned")) {
        return 1;
    }

    if (expect(wukong_cron_time_ready_notify() == OPRT_OK,
               "expected time ready notify to succeed")) {
        return 1;
    }

    if (expect(g_timer_start_count > 0, "expected scheduling to start timer")) {
        return 1;
    }

    if (expect(g_last_timer_id != NULL, "expected cron timer to be created")) {
        return 1;
    }

    g_fake_now += 59;
    g_fake_now += 1;
    test_timer_fire(g_last_timer_id);

    if (expect(g_timer_arm_count > 1, "expected timer to be re-armed after callback")) {
        return 1;
    }

    if (expect(wukong_cron_job_list(&job_list_json) == OPRT_OK,
               "expected job list export to succeed")) {
        return 1;
    }

    if (expect(once_job_id[0] != '\0', "expected once job id to be returned")) {
        return 1;
    }

    if (expect(strstr(job_list_json, once_job_id) == NULL,
               "expected once cron job to be removed after first fire")) {
        return 1;
    }

    ty_cJSON_FreeBuffer(job_list_json);

    if (expect(wukong_cron_job_execute(job_json, &response_json) == OPRT_OK,
               "expected manual job execution to succeed")) {
        return 1;
    }

    if (expect(response_json != NULL && strstr(response_json, "\"status\":\"ok\"") != NULL,
               "expected manual execution response payload")) {
        return 1;
    }

    ty_cJSON_FreeBuffer(response_json);

    if (expect(wukong_cron_job_remove(job_id) == OPRT_OK,
               "expected job remove to succeed")) {
        return 1;
    }

    if (expect(wukong_cron_deinit() == OPRT_OK, "expected cron deinit to succeed")) {
        return 1;
    }

    return 0;
}
EOF

cc -D_GNU_SOURCE -std=c99 -Wall -Wextra -Werror \
    -I"$TMP_DIR" \
    -I"$TMP_DIR/utilities" \
    -I"$ROOT_DIR/src/wukong/cron" \
    "$TMP_DIR/stubs.c" \
    "$ROOT_DIR/src/wukong/cron/wukong_cron_expr.c" \
    "$ROOT_DIR/src/wukong/cron/wukong_cron_rpc.c" \
    "$ROOT_DIR/src/wukong/cron/wukong_cron.c" \
    "$TMP_DIR/test_wukong_cron_core.c" \
    -o "$TMP_DIR/test_wukong_cron_core"

"$TMP_DIR/test_wukong_cron_core"
