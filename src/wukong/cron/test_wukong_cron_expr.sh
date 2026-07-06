#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

cat > "$TMP_DIR/tuya_cloud_types.h" <<'EOF'
#ifndef __TUYA_CLOUD_TYPES_H__
#define __TUYA_CLOUD_TYPES_H__

#include <stdint.h>
#include <stddef.h>

typedef int OPERATE_RET;
typedef int INT_T;
typedef char CHAR_T;
typedef uint64_t UINT64_T;
typedef unsigned int UINT_T;
typedef int BOOL_T;
typedef long long TIME_T;
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
#define PVOID_T void *

#define OPRT_OK 0
#define OPRT_INVALID_PARM (-1)
#define OPRT_NOT_SUPPORTED (-2)

#define SIZEOF(x) ((unsigned int)sizeof(x))

#define TUYA_CHECK_NULL_RETURN(x, ret) \
    do { \
        if ((x) == NULL) { \
            return (ret); \
        } \
    } while (0)

#define TUYA_CALL_ERR_RETURN(expr) \
    do { \
        OPERATE_RET rt = (expr); \
        if (rt != OPRT_OK) { \
            return rt; \
        } \
    } while (0)

#endif
EOF

cat > "$TMP_DIR/tal_time_service.h" <<'EOF'
#ifndef __TAL_TIME_SERVICE_H__
#define __TAL_TIME_SERVICE_H__

#include <time.h>
#include "tuya_cloud_types.h"

typedef struct tm POSIX_TM_S;

#define TEST_TIMEZONE_OFFSET_SEC (8 * 3600)

static inline TIME_T tal_time_mktime(POSIX_TM_S *tm_info)
{
    if (tm_info == NULL) {
        return (TIME_T)-1;
    }
    if (tm_info->tm_sec < 0 || tm_info->tm_sec > 59 ||
        tm_info->tm_min < 0 || tm_info->tm_min > 59 ||
        tm_info->tm_hour < 0 || tm_info->tm_hour > 23 ||
        tm_info->tm_mday < 1 || tm_info->tm_mday > 31 ||
        tm_info->tm_mon < 0 || tm_info->tm_mon > 11) {
        return (TIME_T)-1;
    }
    return (TIME_T)timegm(tm_info);
}

static inline OPERATE_RET tal_time_get_local_time_custom(TIME_T ts, POSIX_TM_S *tm_info)
{
    time_t raw = (time_t)(ts + TEST_TIMEZONE_OFFSET_SEC);
    return gmtime_r(&raw, tm_info) == NULL ? OPRT_INVALID_PARM : OPRT_OK;
}

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

cat > "$TMP_DIR/test_wukong_cron_expr.c" <<'EOF'
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "wukong_cron_expr.h"

#define TEST_TIMEZONE_OFFSET_SEC (8 * 3600)

static int expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "%s\n", msg);
        return 1;
    }
    return 0;
}

static TIME_T local_tm_to_posix(POSIX_TM_S *tm_info)
{
    return (TIME_T)timegm(tm_info) - TEST_TIMEZONE_OFFSET_SEC;
}

int main(void)
{
    WUKONG_CRON_EXPR_T expr;
    TIME_T next_fire = 0;
    POSIX_TM_S tm_info = {0};

    if (expect(wukong_cron_expr_parse("*/15 * * * * *", &expr) == OPRT_OK,
               "expected parser to accept basic 6-field expression")) {
        return 1;
    }

    tm_info.tm_year = 2026 - 1900;
    tm_info.tm_mon = 2;
    tm_info.tm_mday = 6;
    tm_info.tm_hour = 10;
    tm_info.tm_min = 0;
    tm_info.tm_sec = 1;

    if (expect(wukong_cron_expr_next_fire(&expr, local_tm_to_posix(&tm_info), &next_fire) == OPRT_OK,
               "expected next_fire calculation to succeed")) {
        return 1;
    }

    tm_info.tm_sec = 15;
    if (expect(next_fire == local_tm_to_posix(&tm_info),
               "expected next fire to be the next 15-second boundary")) {
        return 1;
    }

    if (expect(wukong_cron_expr_parse("0 */1 * * * *", &expr) == OPRT_OK,
               "expected parser to accept one-minute expression")) {
        return 1;
    }

    tm_info.tm_year = 2026 - 1900;
    tm_info.tm_mon = 2;
    tm_info.tm_mday = 19;
    tm_info.tm_hour = 20;
    tm_info.tm_min = 28;
    tm_info.tm_sec = 33;

    if (expect(wukong_cron_expr_next_fire(&expr, local_tm_to_posix(&tm_info), &next_fire) == OPRT_OK,
               "expected one-minute next_fire calculation to succeed")) {
        return 1;
    }

    tm_info.tm_min = 29;
    tm_info.tm_sec = 0;
    if (expect(next_fire == local_tm_to_posix(&tm_info),
               "expected next fire to be the next local minute boundary")) {
        return 1;
    }

    tm_info.tm_year = 2026 - 1900;
    tm_info.tm_mon = 2;
    tm_info.tm_mday = 23;
    tm_info.tm_hour = 11;
    tm_info.tm_min = 59;
    tm_info.tm_sec = 0;

    if (expect(wukong_cron_expr_next_fire(&expr, local_tm_to_posix(&tm_info), &next_fire) == OPRT_OK,
               "expected hour-boundary next_fire calculation to succeed")) {
        return 1;
    }

    tm_info.tm_hour = 12;
    tm_info.tm_min = 0;
    tm_info.tm_sec = 0;
    if (expect(next_fire == local_tm_to_posix(&tm_info),
               "expected next fire to cross the 59-minute boundary correctly")) {
        return 1;
    }

    tm_info.tm_year = 2026 - 1900;
    tm_info.tm_mon = 2;
    tm_info.tm_mday = 23;
    tm_info.tm_hour = 23;
    tm_info.tm_min = 59;
    tm_info.tm_sec = 0;

    if (expect(wukong_cron_expr_next_fire(&expr, local_tm_to_posix(&tm_info), &next_fire) == OPRT_OK,
               "expected day-boundary next_fire calculation to succeed")) {
        return 1;
    }

    tm_info.tm_mday = 24;
    tm_info.tm_hour = 0;
    tm_info.tm_min = 0;
    tm_info.tm_sec = 0;
    if (expect(next_fire == local_tm_to_posix(&tm_info),
               "expected next fire to cross the day boundary correctly")) {
        return 1;
    }

    if (expect(wukong_cron_expr_parse("61 * * * * *", &expr) != OPRT_OK,
               "expected invalid seconds field to be rejected")) {
        return 1;
    }

    return 0;
}
EOF

cc -D_GNU_SOURCE -std=c99 -Wall -Wextra -Werror \
    -I"$TMP_DIR" \
    -I"$ROOT_DIR/src/wukong/cron" \
    "$ROOT_DIR/src/wukong/cron/wukong_cron_expr.c" \
    "$TMP_DIR/test_wukong_cron_expr.c" \
    -o "$TMP_DIR/test_wukong_cron_expr"

"$TMP_DIR/test_wukong_cron_expr"
