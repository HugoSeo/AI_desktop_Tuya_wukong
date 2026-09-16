/**
 * @file wukong_iso8601.h
 * @brief ISO 8601 date-time parsing and formatting utilities.
 *
 * Supports parsing ISO 8601 strings into broken-down time + UTC offset,
 * and formatting UTC timestamps into ISO 8601 strings with device timezone.
 */

#ifndef WUKONG_ISO8601_H
#define WUKONG_ISO8601_H

#include "tuya_cloud_types.h"
#include "tal_time_service.h"

/**
 * @brief Result of parsing an ISO 8601 string.
 */
typedef struct {
    POSIX_TM_S tm;            /**< Broken-down local time. */
    INT_T utc_offset_sec;     /**< UTC offset in seconds (e.g. +28800 for +08:00). */
    BOOL_T has_time;          /**< FALSE when input was date-only ("2025-06-12"). */
    BOOL_T has_offset;        /**< TRUE when timezone offset was explicitly provided. */
} WUKONG_ISO8601_RESULT_T;

/**
 * @brief Parse an ISO 8601 string into broken-down time + UTC offset.
 *
 * Supported formats:
 *   "2025-06-12T14:30:00+08:00"  (full datetime with offset)
 *   "2025-06-12T14:30+08:00"     (without seconds)
 *   "2025-06-12T06:30:00Z"       (Zulu/UTC)
 *   "2025-06-12 14:30:00+08:00"  (space separator, lenient)
 *   "2025-06-12"                 (date only)
 *
 * When input is date-only:
 *   - end_of_day=FALSE -> time defaults to 00:00:00
 *   - end_of_day=TRUE  -> time defaults to 23:59:59
 *
 * When no explicit offset is provided, the device timezone from
 * tal_time_get_time_zone_seconds() is used.
 *
 * @param[in]  str        ISO 8601 string to parse.
 * @param[in]  end_of_day When TRUE and date-only, default time to 23:59:59.
 * @param[out] result     Parsed result.
 * @return OPRT_OK on success, OPRT_INVALID_PARM on parse failure.
 */
OPERATE_RET wukong_iso8601_parse(CONST CHAR_T *str, BOOL_T end_of_day,
                                  WUKONG_ISO8601_RESULT_T *result);

/**
 * @brief Format a UTC timestamp as an ISO 8601 string with device timezone.
 *
 * Output format: "2025-06-12T14:30:00+08:00"
 * Buffer must be at least 29 bytes.
 *
 * @param[in]  utc_timestamp  UTC POSIX timestamp.
 * @param[out] buf            Output buffer.
 * @param[in]  buf_len        Buffer length (minimum 29).
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_iso8601_format(TIME_T utc_timestamp,
                                   CHAR_T *buf, UINT_T buf_len);

/**
 * @brief Parse an ISO 8601 string and convert directly to a UTC timestamp.
 *
 * Combines wukong_iso8601_parse with local-time-to-UTC conversion.
 * Uses the embedded offset (or device timezone as fallback) for conversion.
 * Handles DST via round-trip verification.
 *
 * @param[in]  str        ISO 8601 string to parse.
 * @param[in]  end_of_day When TRUE and date-only, default time to 23:59:59.
 * @param[out] utc_ts     Resulting UTC timestamp.
 * @return OPRT_OK on success, OPRT_INVALID_PARM on parse failure.
 */
OPERATE_RET wukong_iso8601_parse_to_timestamp(CONST CHAR_T *str,
                                               BOOL_T end_of_day,
                                               TIME_T *utc_ts);

#endif /* WUKONG_ISO8601_H */
