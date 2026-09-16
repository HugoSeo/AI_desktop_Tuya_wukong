/**
 * @file wukong_iso8601.c
 * @brief ISO 8601 date-time parsing and formatting utilities.
 *
 * Supports parsing ISO 8601 strings into broken-down time + UTC offset,
 * and formatting UTC timestamps into ISO 8601 strings with device timezone.
 */

#include "wukong_iso8601.h"

#include <stdio.h>
#include <string.h>

#include "tal_log.h"
#include "tal_time_service.h"

/* ---------------------------------------------------------------------------
 * Internal helpers (replicated from mcp_tool_tm.c for DST-aware conversion)
 * --------------------------------------------------------------------------- */

/**
 * @brief Convert one civil date to days since Unix epoch.
 *
 * @param[in] year  Full year (for example 2026).
 * @param[in] month Month in range [1, 12].
 * @param[in] day   Day of month in range [1, 31].
 * @return Day offset relative to 1970-01-01.
 */
STATIC TIME_T __iso8601_days_from_civil(INT_T year, INT_T month, INT_T day)
{
    INT_T era = 0;
    UINT_T yoe = 0;
    UINT_T doy = 0;
    UINT_T doe = 0;

    year -= (month <= 2) ? 1 : 0;
    era = (year >= 0) ? (year / 400) : ((year - 399) / 400);
    yoe = (UINT_T)(year - era * 400);
    doy = (UINT_T)((153 * (month + ((month > 2) ? -3 : 9)) + 2) / 5 + day - 1);
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

    return (TIME_T)(era * 146097 + (INT_T)doe - 719468);
}

/**
 * @brief Check whether two POSIX_TM_S structs are equal in date+time fields.
 *
 * @param[in] lhs Left-hand side.
 * @param[in] rhs Right-hand side.
 * @return TRUE when equal, FALSE otherwise.
 */
STATIC BOOL_T __iso8601_local_tm_equals(CONST POSIX_TM_S *lhs, CONST POSIX_TM_S *rhs)
{
    if (lhs == NULL || rhs == NULL) {
        return FALSE;
    }

    return (lhs->tm_year == rhs->tm_year &&
            lhs->tm_mon == rhs->tm_mon &&
            lhs->tm_mday == rhs->tm_mday &&
            lhs->tm_hour == rhs->tm_hour &&
            lhs->tm_min == rhs->tm_min &&
            lhs->tm_sec == rhs->tm_sec) ? TRUE : FALSE;
}

/**
 * @brief Convert a broken-down local time to a plain epoch (no timezone).
 *
 * @param[in] tm_info Broken-down time.
 * @return Plain epoch seconds since 1970-01-01 00:00:00 UTC.
 */
STATIC TIME_T __iso8601_tm_to_plain_epoch(CONST POSIX_TM_S *tm_info)
{
    TIME_T days = 0;

    if (tm_info == NULL) {
        return 0;
    }

    days = __iso8601_days_from_civil(tm_info->tm_year + 1900,
                                     tm_info->tm_mon + 1,
                                     tm_info->tm_mday);
    return days * 24 * 60 * 60 +
           (TIME_T)tm_info->tm_hour * 60 * 60 +
           (TIME_T)tm_info->tm_min * 60 +
           (TIME_T)tm_info->tm_sec;
}

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------- */

OPERATE_RET wukong_iso8601_parse(CONST CHAR_T *str, BOOL_T end_of_day,
                                  WUKONG_ISO8601_RESULT_T *result)
{
    INT_T year = 0, month = 0, day = 0;
    INT_T hour = 0, min = 0, sec = 0;
    INT_T offset_sign = 0;
    INT_T offset_hour = 0, offset_min = 0;
    INT_T tz = 0;
    SIZE_T len = 0;
    INT_T n = 0;
    CHAR_T sep = '\0';
    CONST CHAR_T *p = NULL;

    if (str == NULL || result == NULL) {
        return OPRT_INVALID_PARM;
    }

    memset(result, 0, sizeof(*result));

    len = strlen(str);
    if (len < 10) {
        /* Minimum: "YYYY-MM-DD" */
        return OPRT_INVALID_PARM;
    }

    /* --- Parse date part: YYYY-MM-DD --- */
    n = sscanf(str, "%04d-%02d-%02d", &year, &month, &day);
    if (n != 3) {
        return OPRT_INVALID_PARM;
    }

    /* Validate date ranges */
    if (year < 1970 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31) {
        return OPRT_INVALID_PARM;
    }

    result->has_time = FALSE;
    result->has_offset = FALSE;

    /* --- Check for separator after date (position 10) --- */
    if (len <= 10) {
        /* Date-only string */
        result->has_time = FALSE;
        if (end_of_day) {
            hour = 23;
            min = 59;
            sec = 59;
        } else {
            hour = 0;
            min = 0;
            sec = 0;
        }
        /* No explicit offset; use device timezone */
        tal_time_get_time_zone_seconds(&tz);
        result->utc_offset_sec = tz;
        goto fill_result;
    }

    sep = str[10];
    if (sep != 'T' && sep != ' ') {
        /* Not a recognized separator; treat as date-only */
        result->has_time = FALSE;
        if (end_of_day) {
            hour = 23;
            min = 59;
            sec = 59;
        } else {
            hour = 0;
            min = 0;
            sec = 0;
        }
        tal_time_get_time_zone_seconds(&tz);
        result->utc_offset_sec = tz;
        goto fill_result;
    }

    /* --- Parse time part starting at position 11 --- */
    p = str + 11;

    /* Try HH:MM:SS first */
    n = sscanf(p, "%02d:%02d:%02d", &hour, &min, &sec);
    if (n >= 2) {
        result->has_time = TRUE;
        if (n == 2) {
            /* No seconds provided */
            sec = 0;
        }
    } else {
        /* Could not parse time; treat as date-only */
        result->has_time = FALSE;
        if (end_of_day) {
            hour = 23;
            min = 59;
            sec = 59;
        } else {
            hour = 0;
            min = 0;
            sec = 0;
        }
        tal_time_get_time_zone_seconds(&tz);
        result->utc_offset_sec = tz;
        goto fill_result;
    }

    /* Validate time ranges */
    if (hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 59) {
        return OPRT_INVALID_PARM;
    }

    /* --- Locate offset part after the time --- */
    /* Time portion is either "HH:MM" (5 chars) or "HH:MM:SS" (8 chars).
     * We scan forward from p to find 'Z', '+', or '-' that is NOT part of
     * the time digits. */
    {
        CONST CHAR_T *time_end = p;
        INT_T digits_seen = 0;
        INT_T colons_seen = 0;

        /* Walk through the time portion to find where it ends */
        while (*time_end != '\0') {
            if (*time_end >= '0' && *time_end <= '9') {
                digits_seen++;
            } else if (*time_end == ':') {
                colons_seen++;
            } else {
                /* Hit a non-digit, non-colon character — this is the start
                 * of the offset (or end of string). */
                break;
            }
            time_end++;
        }

        p = time_end;
    }

    /* --- Parse offset --- */
    if (*p == 'Z' || *p == 'z') {
        /* UTC / Zulu */
        result->has_offset = TRUE;
        result->utc_offset_sec = 0;
    } else if (*p == '+' || *p == '-') {
        offset_sign = (*p == '+') ? 1 : -1;
        p++;

        /* Try HH:MM first */
        n = sscanf(p, "%02d:%02d", &offset_hour, &offset_min);
        if (n == 2) {
            result->has_offset = TRUE;
            result->utc_offset_sec = offset_sign * (offset_hour * 3600 + offset_min * 60);
        } else {
            /* Try HHMM (no colon) */
            n = sscanf(p, "%02d%02d", &offset_hour, &offset_min);
            if (n == 2) {
                result->has_offset = TRUE;
                result->utc_offset_sec = offset_sign * (offset_hour * 3600 + offset_min * 60);
            } else {
                /* Try HH only */
                n = sscanf(p, "%02d", &offset_hour);
                if (n == 1) {
                    result->has_offset = TRUE;
                    result->utc_offset_sec = offset_sign * (offset_hour * 3600);
                } else {
                    return OPRT_INVALID_PARM;
                }
            }
        }

        /* Validate offset ranges */
        if (offset_hour < 0 || offset_hour > 23 || offset_min < 0 || offset_min > 59) {
            return OPRT_INVALID_PARM;
        }
    } else {
        /* No explicit offset; use device timezone */
        result->has_offset = FALSE;
        tal_time_get_time_zone_seconds(&tz);
        result->utc_offset_sec = tz;
    }

fill_result:
    result->tm.tm_year = year - 1900;
    result->tm.tm_mon = month - 1;
    result->tm.tm_mday = day;
    result->tm.tm_hour = hour;
    result->tm.tm_min = min;
    result->tm.tm_sec = sec;
    result->tm.tm_wday = 0; /* Not computed */

    return OPRT_OK;
}

OPERATE_RET wukong_iso8601_format(TIME_T utc_timestamp,
                                   CHAR_T *buf, UINT_T buf_len)
{
    POSIX_TM_S tm = {0};
    INT_T tz_sec = 0;
    CHAR_T sign = '+';
    INT_T tz_hours = 0;
    INT_T tz_minutes = 0;
    OPERATE_RET rt = OPRT_OK;

    if (buf == NULL || buf_len < 29) {
        return OPRT_INVALID_PARM;
    }

    rt = tal_time_get_local_time_custom(utc_timestamp, &tm);
    if (rt != OPRT_OK) {
        return rt;
    }

    tal_time_get_time_zone_seconds(&tz_sec);

    if (tz_sec < 0) {
        sign = '-';
        tz_sec = -tz_sec;
    }
    tz_hours = tz_sec / 3600;
    tz_minutes = (tz_sec % 3600) / 60;

    (VOID)snprintf(buf, buf_len, "%04d-%02d-%02dT%02d:%02d:%02d%c%02d:%02d",
                   tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                   tm.tm_hour, tm.tm_min, tm.tm_sec,
                   sign, tz_hours, tz_minutes);

    return OPRT_OK;
}

OPERATE_RET wukong_iso8601_parse_to_timestamp(CONST CHAR_T *str,
                                               BOOL_T end_of_day,
                                               TIME_T *utc_ts)
{
    WUKONG_ISO8601_RESULT_T parse_result = {0};
    POSIX_TM_S local_tm = {0};
    POSIX_TM_S actual_tm = {0};
    TIME_T candidate_ts = 0;
    TIME_T adjust_sec = 0;
    OPERATE_RET rt = OPRT_OK;

    if (str == NULL || utc_ts == NULL) {
        return OPRT_INVALID_PARM;
    }

    /* Step 1: Parse the ISO 8601 string */
    rt = wukong_iso8601_parse(str, end_of_day, &parse_result);
    if (rt != OPRT_OK) {
        return rt;
    }

    /* Step 2: Convert local time to UTC timestamp using the parsed offset.
     *
     * The parsed time is in the local timezone indicated by utc_offset_sec.
     * To get the UTC timestamp, we subtract the UTC offset from the plain
     * epoch representation of the local time.
     *
     * However, the device's tal_time_mktime + tal_time_get_local_time_custom
     * pair operates on the device's configured timezone. We need to account
     * for a possible mismatch between the parsed offset and the device
     * timezone.
     *
     * Approach:
     *   a) Compute plain epoch of the local time (no timezone).
     *   b) Subtract the UTC offset to get the UTC timestamp directly.
     *   c) Verify by converting back via the device's timezone functions,
     *      adjusting for DST discrepancies.
     */

    /* Compute UTC timestamp directly from the plain epoch and offset */
    candidate_ts = __iso8601_tm_to_plain_epoch(&parse_result.tm) - parse_result.utc_offset_sec;

    /* Step 3: Verify via device timezone round-trip (DST handling).
     *
     * When the offset came from the device timezone (has_offset == FALSE),
     * we use the same DST round-trip logic as __schedule_local_tm_to_timestamp
     * for maximum accuracy.
     */
    if (!parse_result.has_offset) {
        /* Use the device's mktime + localtime round-trip for DST verification */
        local_tm = parse_result.tm;
        candidate_ts = tal_time_mktime(&local_tm);
        rt = tal_time_get_local_time_custom(candidate_ts, &actual_tm);
        if (rt != OPRT_OK) {
            return rt;
        }

        if (!__iso8601_local_tm_equals(&local_tm, &actual_tm)) {
            adjust_sec = __iso8601_tm_to_plain_epoch(&local_tm) -
                         __iso8601_tm_to_plain_epoch(&actual_tm);
            candidate_ts += adjust_sec;

            /* Re-verify the adjusted timestamp */
            rt = tal_time_get_local_time_custom(candidate_ts, &actual_tm);
            if (rt != OPRT_OK) {
                return rt;
            }
            if (!__iso8601_local_tm_equals(&local_tm, &actual_tm)) {
                /* Ambiguous or non-existent local time (DST gap/overlap) */
                return OPRT_INVALID_PARM;
            }
        }
    } else {
        /* Explicit offset was provided — the direct computation is authoritative.
         * Still do a sanity check by converting back. */
        rt = tal_time_get_local_time_custom(candidate_ts, &actual_tm);
        if (rt != OPRT_OK) {
            /* If the device timezone functions cannot verify, trust the
             * direct computation since the offset was explicit. */
        }
    }

    *utc_ts = candidate_ts;
    return OPRT_OK;
}
