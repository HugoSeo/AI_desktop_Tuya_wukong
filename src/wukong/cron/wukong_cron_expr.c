/**
 * @file wukong_cron_expr.c
 * @brief Wukong cron expression parser and next-fire calculator.
 *
 * This module parses a six-field cron expression:
 * `sec min hour day month weekday`
 *
 * Supported field syntax:
 * - wildcard: `*`
 * - single value: `10`
 * - range: `1-5`
 * - list: `1,2,3`
 * - step: `* / 15`, `1-10/2` (without spaces)
 *
 * The parsed form is stored as six bitmasks. `wukong_cron_expr_next_fire()`
 * then searches forward from `from_ts + 1` until it finds the next matching
 * timestamp.
 */

#include "wukong_cron_expr.h"

#include "tal_log.h"

#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Internal parsing helpers
 * --------------------------------------------------------------------------- */
/**
 * @brief Field parser config: accepted range and target bitmask.
 */
typedef struct {
    /** Minimum legal value for the field. */
    INT_T min;
    /** Maximum legal value for the field. */
    INT_T max;
    /** Target bitmask populated during parsing. */
    UINT64_T *mask;
} WUKONG_CRON_FIELD_CFG_T;

/**
 * @brief Build a full bitmask for wildcard comparisons.
 *
 * @param[in] min Minimum legal field value.
 * @param[in] max Maximum legal field value.
 * @return Full mask covering the inclusive range.
 */
STATIC UINT64_T __field_full_mask(INT_T min, INT_T max)
{
    UINT64_T mask = 0;
    INT_T value = 0;

    for (value = min; value <= max; value++) {
        mask |= (1ULL << value);
    }

    return mask;
}

/**
 * @brief Check whether one value is enabled in a field mask.
 *
 * @param[in] mask  Source field mask.
 * @param[in] value Target value.
 * @return TRUE when the value is enabled, otherwise FALSE.
 */
STATIC BOOL_T __field_has_value(UINT64_T mask, INT_T value)
{
    return ((mask & (1ULL << value)) != 0) ? TRUE : FALSE;
}

/**
 * @brief Return the first enabled value in a field mask.
 *
 * @param[in] mask Source field mask.
 * @param[in] min  Minimum legal field value.
 * @param[in] max  Maximum legal field value.
 * @return First enabled value, or `-1` when none exists.
 */
STATIC INT_T __field_first_value(UINT64_T mask, INT_T min, INT_T max)
{
    INT_T value = 0;

    for (value = min; value <= max; value++) {
        if (__field_has_value(mask, value)) {
            return value;
        }
    }

    return -1;
}

/**
 * @brief Return the next enabled value greater than or equal to @p current.
 *
 * @param[in] mask    Source field mask.
 * @param[in] min     Minimum legal field value.
 * @param[in] max     Maximum legal field value.
 * @param[in] current Search start value.
 * @return Next enabled value, or `-1` when none exists.
 */
STATIC INT_T __field_next_value(UINT64_T mask, INT_T min, INT_T max, INT_T current)
{
    INT_T value = 0;

    (VOID)min;

    for (value = current; value <= max; value++) {
        if (__field_has_value(mask, value)) {
            return value;
        }
    }

    return -1;
}

/**
 * @brief Expand one scalar/range/step fragment into the destination field mask.
 *
 * @param[in,out] mask  Target field mask.
 * @param[in]     min   Minimum legal field value.
 * @param[in]     max   Maximum legal field value.
 * @param[in]     start Inclusive range start.
 * @param[in]     end   Inclusive range end.
 * @param[in]     step  Step value.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __field_add_range(UINT64_T *mask, INT_T min, INT_T max, INT_T start, INT_T end, INT_T step)
{
    INT_T value = 0;

    TUYA_CHECK_NULL_RETURN(mask, OPRT_INVALID_PARM);

    if (step <= 0 || start < min || end > max || start > end) {
        return OPRT_INVALID_PARM;
    }

    for (value = start; value <= end; value += step) {
        *mask |= (1ULL << value);
    }

    return OPRT_OK;
}

/**
 * @brief Parse one comma-separated cron item such as `*`, `1-5/2`, or `10`.
 *
 * @param[in,out] item Item string that may be tokenized in place.
 * @param[in]     cfg  Field parser configuration.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __field_parse_item(CHAR_T *item, WUKONG_CRON_FIELD_CFG_T *cfg)
{
    CHAR_T *slash = NULL;
    CHAR_T *dash = NULL;
    INT_T step = 1;
    INT_T start = 0;
    INT_T end = 0;

    TUYA_CHECK_NULL_RETURN(item, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(cfg, OPRT_INVALID_PARM);

    slash = strchr(item, '/');
    if (slash != NULL) {
        *slash = '\0';
        slash++;
        if (*slash == '\0') {
            return OPRT_INVALID_PARM;
        }
        step = (INT_T)strtol(slash, NULL, 10);
    }

    if (strcmp(item, "*") == 0) {
        start = cfg->min;
        end = cfg->max;
        return __field_add_range(cfg->mask, cfg->min, cfg->max, start, end, step);
    }

    dash = strchr(item, '-');
    if (dash != NULL) {
        *dash = '\0';
        dash++;
        if (*item == '\0' || *dash == '\0') {
            return OPRT_INVALID_PARM;
        }
        start = (INT_T)strtol(item, NULL, 10);
        end = (INT_T)strtol(dash, NULL, 10);
        return __field_add_range(cfg->mask, cfg->min, cfg->max, start, end, step);
    }

    if (slash != NULL) {
        if (*item == '\0') {
            return OPRT_INVALID_PARM;
        }
        start = (INT_T)strtol(item, NULL, 10);
        end = cfg->max;
        return __field_add_range(cfg->mask, cfg->min, cfg->max, start, end, step);
    }

    start = (INT_T)strtol(item, NULL, 10);
    return __field_add_range(cfg->mask, cfg->min, cfg->max, start, start, 1);
}

/**
 * @brief Parse a full field made of comma-separated items into one bitmap.
 *
 * @param[in,out] field_str Field string that may be tokenized in place.
 * @param[in]     min       Minimum legal field value.
 * @param[in]     max       Maximum legal field value.
 * @param[out]    mask      Parsed field mask.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __field_parse(CHAR_T *field_str, INT_T min, INT_T max, UINT64_T *mask)
{
    CHAR_T *save_ptr = NULL;
    CHAR_T *item = NULL;
    WUKONG_CRON_FIELD_CFG_T cfg = {0};

    TUYA_CHECK_NULL_RETURN(field_str, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(mask, OPRT_INVALID_PARM);

    cfg.min = min;
    cfg.max = max;
    cfg.mask = mask;

    *mask = 0;
    item = strtok_r(field_str, ",", &save_ptr);
    while (item != NULL) {
        OPERATE_RET rt = __field_parse_item(item, &cfg);
        if (rt != OPRT_OK) {
            return rt;
        }
        item = strtok_r(NULL, ",", &save_ptr);
    }

    return (*mask == 0) ? OPRT_INVALID_PARM : OPRT_OK;
}

/**
 * @brief Match day-of-month and weekday using common cron OR semantics.
 *
 * When both fields are restricted, either one may match.
 *
 * @param[in] expr    Parsed cron expression.
 * @param[in] tm_info Local broken-down time.
 * @return TRUE when the day fields match, otherwise FALSE.
 */
STATIC BOOL_T __day_matches(CONST WUKONG_CRON_EXPR_T *expr, CONST POSIX_TM_S *tm_info)
{
    BOOL_T day_all = FALSE;
    BOOL_T weekday_all = FALSE;
    BOOL_T day_match = FALSE;
    BOOL_T weekday_match = FALSE;
    INT_T weekday = 0;

    TUYA_CHECK_NULL_RETURN(expr, FALSE);
    TUYA_CHECK_NULL_RETURN(tm_info, FALSE);

    day_all = (expr->day_mask == __field_full_mask(1, 31)) ? TRUE : FALSE;
    weekday_all = (expr->weekday_mask == __field_full_mask(0, 6)) ? TRUE : FALSE;
    weekday = tm_info->tm_wday;
    if (weekday == 7) {
        weekday = 0;
    }

    day_match = __field_has_value(expr->day_mask, tm_info->tm_mday);
    weekday_match = __field_has_value(expr->weekday_mask, weekday);

    if (day_all && weekday_all) {
        return TRUE;
    }
    if (day_all) {
        return weekday_match;
    }
    if (weekday_all) {
        return day_match;
    }

    return (day_match || weekday_match) ? TRUE : FALSE;
}

/**
 * @brief Convert a timestamp to local broken-down time.
 *
 * @param[in]  ts      Source timestamp.
 * @param[out] tm_info Converted local broken-down time.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __get_tm(TIME_T ts, POSIX_TM_S *tm_info)
{
    TUYA_CHECK_NULL_RETURN(tm_info, OPRT_INVALID_PARM);

    memset(tm_info, 0, sizeof(*tm_info));

    return tal_time_get_local_time_custom(ts, tm_info);
}

/**
 * @brief Compare whether two local broken-down times are equal to second precision.
 *
 * @param[in] lhs Left-hand local broken-down time.
 * @param[in] rhs Right-hand local broken-down time.
 * @return TRUE when both timestamps represent the same local calendar time.
 */
STATIC BOOL_T __tm_equals(CONST POSIX_TM_S *lhs, CONST POSIX_TM_S *rhs)
{
    TUYA_CHECK_NULL_RETURN(lhs, FALSE);
    TUYA_CHECK_NULL_RETURN(rhs, FALSE);

    if (lhs->tm_year != rhs->tm_year || lhs->tm_mon != rhs->tm_mon ||
        lhs->tm_mday != rhs->tm_mday || lhs->tm_hour != rhs->tm_hour ||
        lhs->tm_min != rhs->tm_min || lhs->tm_sec != rhs->tm_sec) {
        return FALSE;
    }

    return TRUE;
}

/**
 * @brief Return whether the specified Gregorian year is a leap year.
 *
 * @param[in] tm_year `struct tm` year field, i.e. years since 1900.
 * @return TRUE when February has 29 days.
 */
STATIC BOOL_T __is_leap_year(INT_T tm_year)
{
    INT_T year = tm_year + 1900;

    if ((year % 4) != 0) {
        return FALSE;
    }
    if ((year % 100) != 0) {
        return TRUE;
    }
    return ((year % 400) == 0) ? TRUE : FALSE;
}

/**
 * @brief Return the number of days in the specified local month.
 *
 * @param[in] tm_year `struct tm` year field, i.e. years since 1900.
 * @param[in] tm_mon  `struct tm` month field, range [0, 11].
 * @return Day count for the requested month.
 */
STATIC INT_T __days_in_month(INT_T tm_year, INT_T tm_mon)
{
    STATIC CONST INT_T days_per_month[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };

    if (tm_mon == 1 && __is_leap_year(tm_year)) {
        return 29;
    }
    return days_per_month[tm_mon];
}

/**
 * @brief Normalize a local broken-down time so all fields fall within legal ranges.
 *
 * @param[in,out] tm_info Local broken-down time to normalize in place.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __normalize_local_tm(POSIX_TM_S *tm_info)
{
    INT_T days = 0;

    TUYA_CHECK_NULL_RETURN(tm_info, OPRT_INVALID_PARM);

    if (tm_info->tm_sec < 0 || tm_info->tm_min < 0 || tm_info->tm_hour < 0 ||
        tm_info->tm_mday <= 0 || tm_info->tm_mon < 0) {
        return OPRT_INVALID_PARM;
    }

    tm_info->tm_min += tm_info->tm_sec / 60;
    tm_info->tm_sec %= 60;

    tm_info->tm_hour += tm_info->tm_min / 60;
    tm_info->tm_min %= 60;

    days = tm_info->tm_hour / 24;
    tm_info->tm_hour %= 24;
    tm_info->tm_mday += days;

    while (tm_info->tm_mon >= 12) {
        tm_info->tm_year += 1;
        tm_info->tm_mon -= 12;
    }

    while (tm_info->tm_mday > __days_in_month(tm_info->tm_year, tm_info->tm_mon)) {
        tm_info->tm_mday -= __days_in_month(tm_info->tm_year, tm_info->tm_mon);
        tm_info->tm_mon += 1;
        while (tm_info->tm_mon >= 12) {
            tm_info->tm_year += 1;
            tm_info->tm_mon -= 12;
        }
    }

    return OPRT_OK;
}

/**
 * @brief Convert a local broken-down time back to POSIX time robustly across timezone semantics.
 *
 * Some platforms expose `tal_time_get_local_time_custom()` as local time, while
 * `tal_time_mktime()` may interpret the same fields as UTC. This helper first
 * tries the direct conversion, then validates the round trip through
 * `__get_tm()`. If the round trip does not match, it searches common timezone
 * offsets in 15-minute steps until it finds the timestamp whose local time
 * matches the target fields.
 *
 * @param[in]  tm_info Local broken-down time to convert.
 * @param[out] ts      Converted POSIX timestamp.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __local_tm_to_ts(CONST POSIX_TM_S *tm_info, TIME_T *ts)
{
    POSIX_TM_S tm_copy;
    POSIX_TM_S verify_tm;
    TIME_T direct_ts = 0;
    TIME_T candidate_ts = 0;
    INT_T offset_quarter = 0;

    TUYA_CHECK_NULL_RETURN(tm_info, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(ts, OPRT_INVALID_PARM);

    tm_copy = *tm_info;
    if (__normalize_local_tm(&tm_copy) != OPRT_OK) {
        return OPRT_INVALID_PARM;
    }
    direct_ts = tal_time_mktime(&tm_copy);
    if (__get_tm(direct_ts, &verify_tm) == OPRT_OK && __tm_equals(&tm_copy, &verify_tm)) {
        *ts = direct_ts;
        return OPRT_OK;
    }

    for (offset_quarter = -(14 * 4); offset_quarter <= (14 * 4); offset_quarter++) {
        candidate_ts = direct_ts - (TIME_T)(offset_quarter * 15 * 60);
        if (__get_tm(candidate_ts, &verify_tm) != OPRT_OK) {
            continue;
        }
        if (__tm_equals(&tm_copy, &verify_tm)) {
            *ts = candidate_ts;
            return OPRT_OK;
        }
    }

    return OPRT_INVALID_PARM;
}

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------- */
/**
 * @brief Parse a six-field cron expression into bitmap form.
 *
 * @param[in]  expr_str Input expression string.
 * @param[out] expr     Parsed bitmap expression.
 */
OPERATE_RET wukong_cron_expr_parse(CONST CHAR_T *expr_str, WUKONG_CRON_EXPR_T *expr)
{
    CHAR_T expr_buf[WUKONG_CRON_EXPR_MAX_LEN] = {0};
    CHAR_T *save_ptr = NULL;
    CHAR_T *fields[6] = {NULL};
    UINT_T index = 0;
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(expr_str, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(expr, OPRT_INVALID_PARM);

    memset(expr, 0, sizeof(*expr));

    if (strlen(expr_str) >= sizeof(expr_buf)) {
        return OPRT_INVALID_PARM;
    }

    strncpy(expr_buf, expr_str, sizeof(expr_buf) - 1);

    fields[index] = strtok_r(expr_buf, " ", &save_ptr);
    while (fields[index] != NULL && index < 5) {
        index++;
        fields[index] = strtok_r(NULL, " ", &save_ptr);
    }

    if (index != 5 || fields[5] == NULL || strtok_r(NULL, " ", &save_ptr) != NULL) {
        return OPRT_INVALID_PARM;
    }

    rt = __field_parse(fields[0], 0, 59, &expr->second_mask);
    if (rt != OPRT_OK) {
        return rt;
    }
    rt = __field_parse(fields[1], 0, 59, &expr->minute_mask);
    if (rt != OPRT_OK) {
        return rt;
    }
    rt = __field_parse(fields[2], 0, 23, &expr->hour_mask);
    if (rt != OPRT_OK) {
        return rt;
    }
    rt = __field_parse(fields[3], 1, 31, &expr->day_mask);
    if (rt != OPRT_OK) {
        return rt;
    }
    rt = __field_parse(fields[4], 1, 12, &expr->month_mask);
    if (rt != OPRT_OK) {
        return rt;
    }
    rt = __field_parse(fields[5], 0, 6, &expr->weekday_mask);
    if (rt != OPRT_OK) {
        return rt;
    }

    return OPRT_OK;
}

/**
 * @brief Check whether a broken-down local time matches the parsed expression.
 *
 * @param[in] expr    Parsed bitmap expression.
 * @param[in] tm_info Local broken-down time to match.
 * @return TRUE when the time matches, otherwise FALSE.
 */
BOOL_T wukong_cron_expr_match(CONST WUKONG_CRON_EXPR_T *expr, CONST POSIX_TM_S *tm_info)
{
    TUYA_CHECK_NULL_RETURN(expr, FALSE);
    TUYA_CHECK_NULL_RETURN(tm_info, FALSE);

    if (!__field_has_value(expr->second_mask, tm_info->tm_sec) ||
        !__field_has_value(expr->minute_mask, tm_info->tm_min) ||
        !__field_has_value(expr->hour_mask, tm_info->tm_hour) ||
        !__field_has_value(expr->month_mask, tm_info->tm_mon + 1)) {
        return FALSE;
    }

    return __day_matches(expr, tm_info);
}

/**
 * @brief Search forward from `from_ts + 1` until a valid firing timestamp is found.
 *
 * @param[in]  expr          Parsed cron expression.
 * @param[in]  from_ts       Base timestamp; the result is strictly later than this value.
 * @param[out] next_fire_ts  Next matching timestamp.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_cron_expr_next_fire(CONST WUKONG_CRON_EXPR_T *expr, TIME_T from_ts, TIME_T *next_fire_ts)
{
    TIME_T candidate = 0;
    TIME_T limit_ts = 0;

    TUYA_CHECK_NULL_RETURN(expr, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(next_fire_ts, OPRT_INVALID_PARM);

    *next_fire_ts = 0;
    candidate = from_ts + 1;
    limit_ts = from_ts + (TIME_T)(366 * 24 * 3600 * 5);

    while (candidate <= limit_ts) {
        POSIX_TM_S tm_info;
        INT_T next_value = 0;

        if (__get_tm(candidate, &tm_info) != OPRT_OK) {
            return OPRT_INVALID_PARM;
        }

        if (!__field_has_value(expr->month_mask, tm_info.tm_mon + 1)) {
            next_value = __field_next_value(expr->month_mask, 1, 12, tm_info.tm_mon + 2);
            if (next_value < 0) {
                tm_info.tm_year += 1;
                next_value = __field_first_value(expr->month_mask, 1, 12);
            }
            tm_info.tm_mon = next_value - 1;
            tm_info.tm_mday = 1;
            tm_info.tm_hour = 0;
            tm_info.tm_min = 0;
            tm_info.tm_sec = 0;
            if (__local_tm_to_ts(&tm_info, &candidate) != OPRT_OK) {
                return OPRT_INVALID_PARM;
            }
            continue;
        }

        if (!__day_matches(expr, &tm_info)) {
            tm_info.tm_mday += 1;
            tm_info.tm_hour = 0;
            tm_info.tm_min = 0;
            tm_info.tm_sec = 0;
            if (__local_tm_to_ts(&tm_info, &candidate) != OPRT_OK) {
                return OPRT_INVALID_PARM;
            }
            continue;
        }

        if (!__field_has_value(expr->hour_mask, tm_info.tm_hour)) {
            next_value = __field_next_value(expr->hour_mask, 0, 23, tm_info.tm_hour + 1);
            if (next_value < 0) {
                tm_info.tm_mday += 1;
                tm_info.tm_hour = 0;
            } else {
                tm_info.tm_hour = next_value;
            }
            tm_info.tm_min = 0;
            tm_info.tm_sec = 0;
            if (__local_tm_to_ts(&tm_info, &candidate) != OPRT_OK) {
                return OPRT_INVALID_PARM;
            }
            continue;
        }

        if (!__field_has_value(expr->minute_mask, tm_info.tm_min)) {
            next_value = __field_next_value(expr->minute_mask, 0, 59, tm_info.tm_min + 1);
            if (next_value < 0) {
                tm_info.tm_hour += 1;
                tm_info.tm_min = 0;
            } else {
                tm_info.tm_min = next_value;
            }
            tm_info.tm_sec = 0;
            if (__local_tm_to_ts(&tm_info, &candidate) != OPRT_OK) {
                return OPRT_INVALID_PARM;
            }
            continue;
        }

        if (!__field_has_value(expr->second_mask, tm_info.tm_sec)) {
            next_value = __field_next_value(expr->second_mask, 0, 59, tm_info.tm_sec + 1);
            if (next_value < 0) {
                tm_info.tm_min += 1;
                tm_info.tm_sec = 0;
            } else {
                tm_info.tm_sec = next_value;
            }
            if (__local_tm_to_ts(&tm_info, &candidate) != OPRT_OK) {
                return OPRT_INVALID_PARM;
            }
            continue;
        }

        *next_fire_ts = candidate;
        return OPRT_OK;
    }

    return OPRT_INVALID_PARM;
}
