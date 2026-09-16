/**
 * @file test_iso8601.c
 * @brief Unit tests for wukong_iso8601 parse / format / parse_to_timestamp.
 */

#include <stdio.h>
#include <string.h>

#include "wukong_test.h"
#include "wukong_iso8601.h"

/* ---- observable state from stubs_iso8601.c ---- */
extern INT_T g_stub_tz_offset_sec;
extern VOID stub_set_tz_offset(INT_T tz_sec);

int main(void)
{
    WUKONG_ISO8601_RESULT_T result = {0};
    CHAR_T buf[64] = {0};
    TIME_T ts = 0;

    /* Default stub timezone: +08:00 (28800 seconds) */
    stub_set_tz_offset(28800);

    /* ==================================================================
     * Test wukong_iso8601_parse
     * ================================================================== */

    /* --- 1. test_parse_full_datetime_with_offset --- */
    {
        EXPECT_OK(wukong_iso8601_parse("2025-06-12T14:30:00+08:00",
                                        FALSE, &result),
                  "parse full datetime with offset should succeed");

        EXPECT_EQ(result.tm.tm_year, 125,
                  "year should be 125 (2025-1900)");
        EXPECT_EQ(result.tm.tm_mon, 5,
                  "month should be 5 (June, 0-based)");
        EXPECT_EQ(result.tm.tm_mday, 12,
                  "mday should be 12");
        EXPECT_EQ(result.tm.tm_hour, 14,
                  "hour should be 14");
        EXPECT_EQ(result.tm.tm_min, 30,
                  "min should be 30");
        EXPECT_EQ(result.tm.tm_sec, 0,
                  "sec should be 0");
        EXPECT_EQ(result.utc_offset_sec, 28800,
                  "utc_offset_sec should be 28800 (+08:00)");
        EXPECT(result.has_time == TRUE,
               "has_time should be TRUE for full datetime");
        EXPECT(result.has_offset == TRUE,
               "has_offset should be TRUE when offset is explicit");
    }

    /* --- 2. test_parse_datetime_without_seconds --- */
    {
        EXPECT_OK(wukong_iso8601_parse("2025-06-12T14:30+08:00",
                                        FALSE, &result),
                  "parse datetime without seconds should succeed");

        EXPECT_EQ(result.tm.tm_hour, 14,
                  "hour should be 14");
        EXPECT_EQ(result.tm.tm_min, 30,
                  "min should be 30");
        EXPECT_EQ(result.tm.tm_sec, 0,
                  "sec should default to 0 when omitted");
        EXPECT(result.has_time == TRUE,
               "has_time should be TRUE");
    }

    /* --- 3. test_parse_zulu_time --- */
    {
        EXPECT_OK(wukong_iso8601_parse("2025-06-12T06:30:00Z",
                                        FALSE, &result),
                  "parse Zulu time should succeed");

        EXPECT_EQ(result.tm.tm_hour, 6,
                  "hour should be 6");
        EXPECT_EQ(result.tm.tm_min, 30,
                  "min should be 30");
        EXPECT_EQ(result.utc_offset_sec, 0,
                  "utc_offset_sec should be 0 for Zulu time");
        EXPECT(result.has_offset == TRUE,
               "has_offset should be TRUE for Z suffix");
    }

    /* --- 4. test_parse_space_separator --- */
    {
        EXPECT_OK(wukong_iso8601_parse("2025-06-12 14:30:00+08:00",
                                        FALSE, &result),
                  "parse with space separator should succeed");

        EXPECT_EQ(result.tm.tm_year, 125,
                  "year should be 125 (2025-1900)");
        EXPECT_EQ(result.tm.tm_mon, 5,
                  "month should be 5 (June)");
        EXPECT_EQ(result.tm.tm_mday, 12,
                  "mday should be 12");
        EXPECT_EQ(result.tm.tm_hour, 14,
                  "hour should be 14");
        EXPECT_EQ(result.tm.tm_min, 30,
                  "min should be 30");
        EXPECT_EQ(result.tm.tm_sec, 0,
                  "sec should be 0");
        EXPECT_EQ(result.utc_offset_sec, 28800,
                  "utc_offset_sec should be 28800 (+08:00)");
    }

    /* --- 5. test_parse_date_only_start_of_day --- */
    {
        EXPECT_OK(wukong_iso8601_parse("2025-06-12", FALSE, &result),
                  "parse date-only (start of day) should succeed");

        EXPECT_EQ(result.tm.tm_hour, 0,
                  "hour should be 0 for start of day");
        EXPECT_EQ(result.tm.tm_min, 0,
                  "min should be 0 for start of day");
        EXPECT_EQ(result.tm.tm_sec, 0,
                  "sec should be 0 for start of day");
        EXPECT(result.has_time == FALSE,
               "has_time should be FALSE for date-only");
    }

    /* --- 6. test_parse_date_only_end_of_day --- */
    {
        EXPECT_OK(wukong_iso8601_parse("2025-06-12", TRUE, &result),
                  "parse date-only (end of day) should succeed");

        EXPECT_EQ(result.tm.tm_hour, 23,
                  "hour should be 23 for end of day");
        EXPECT_EQ(result.tm.tm_min, 59,
                  "min should be 59 for end of day");
        EXPECT_EQ(result.tm.tm_sec, 59,
                  "sec should be 59 for end of day");
        EXPECT(result.has_time == FALSE,
               "has_time should be FALSE for date-only");
    }

    /* --- 7. test_parse_offset_shorthand --- */
    {
        /* +HH:MM form */
        EXPECT_OK(wukong_iso8601_parse("2025-06-12T14:30:00+08:00",
                                        FALSE, &result),
                  "parse +HH:MM offset should succeed");
        EXPECT_EQ(result.utc_offset_sec, 28800,
                  "+08:00 offset should be 28800");

        /* +HHMM form (no colon in offset) */
        EXPECT_OK(wukong_iso8601_parse("2025-06-12T14:30:00+0800",
                                        FALSE, &result),
                  "parse +HHMM offset should succeed");
        EXPECT_EQ(result.utc_offset_sec, 28800,
                  "+0800 offset should be 28800");

        /* -05:00 form */
        EXPECT_OK(wukong_iso8601_parse("2025-06-12T14:30:00-05:00",
                                        FALSE, &result),
                  "parse -HH:MM offset should succeed");
        EXPECT_EQ(result.utc_offset_sec, -18000,
                  "-05:00 offset should be -18000");
    }

    /* --- 8. test_parse_invalid_empty --- */
    {
        EXPECT_ERR(wukong_iso8601_parse("", FALSE, &result),
                   OPRT_INVALID_PARM,
                   "empty string should return OPRT_INVALID_PARM");
    }

    /* --- 9. test_parse_invalid_format --- */
    {
        EXPECT_ERR(wukong_iso8601_parse("not-a-date", FALSE, &result),
                   OPRT_INVALID_PARM,
                   "non-date string should return OPRT_INVALID_PARM");
    }

    /* --- 10. test_parse_invalid_month --- */
    {
        EXPECT_ERR(wukong_iso8601_parse("2025-13-01T00:00:00+08:00",
                                         FALSE, &result),
                   OPRT_INVALID_PARM,
                   "month 13 should return OPRT_INVALID_PARM");
    }

    /* --- 11. test_parse_invalid_hour --- */
    {
        EXPECT_ERR(wukong_iso8601_parse("2025-06-12T25:00:00+08:00",
                                         FALSE, &result),
                   OPRT_INVALID_PARM,
                   "hour 25 should return OPRT_INVALID_PARM");
    }

    /* --- 12. test_parse_no_offset_uses_device_tz --- */
    {
        stub_set_tz_offset(28800);

        EXPECT_OK(wukong_iso8601_parse("2025-06-12T14:30:00",
                                        FALSE, &result),
                  "parse without offset should succeed");

        EXPECT(result.has_offset == FALSE,
               "has_offset should be FALSE when no offset in string");
        EXPECT_EQ(result.utc_offset_sec, 28800,
                  "utc_offset_sec should come from device timezone (+08:00)");

        /* Change stub timezone and re-verify */
        stub_set_tz_offset(-18000); /* -05:00 */

        EXPECT_OK(wukong_iso8601_parse("2025-06-12T14:30:00",
                                        FALSE, &result),
                  "parse with different device tz should succeed");
        EXPECT_EQ(result.utc_offset_sec, -18000,
                  "utc_offset_sec should reflect updated device tz (-05:00)");

        /* Restore default */
        stub_set_tz_offset(28800);
    }

    /* ==================================================================
     * Test wukong_iso8601_format
     * ================================================================== */

    /* --- 13. test_format_known_timestamp --- */
    {
        /*
         * 2025-06-12T06:30:00 UTC = 1749712200
         * In +08:00 timezone this displays as 2025-06-12T14:30:00+08:00
         *
         * Verify the computation:
         *   2025-01-01 00:00:00 UTC = 1735689600
         *   Jan=31, Feb=28, Mar=31, Apr=30, May=31 => 31+28+31+30+31 = 151 days
         *   151 + 11 = 162 days from Jan 1 to Jun 12
         *   1735689600 + 162*86400 = 1735689600 + 13996800 = 1749686400
         *   + 14*3600 + 30*60 = 50400 + 1800 = 52200
         *   1749686400 + 52200 = 1749738600
         *   But wait: that's 14:30:00 UTC, not 06:30:00 UTC.
         *   06:30:00 UTC = 1749738600 - 8*3600 = 1749738600 - 28800 = 1749709800
         *
         * Let's verify using the stub which uses gmtime_r (no TZ adjustment):
         *   The stub tal_time_get_local_time_custom uses gmtime_r, so it returns
         *   UTC time. But the format function appends the device TZ offset.
         *   With gmtime_r(1749709800) we get 2025-06-12T06:30:00 UTC.
         *   Then the format adds +08:00 because tal_time_get_time_zone_seconds
         *   returns 28800. So the output would be "2025-06-12T06:30:00+08:00".
         *
         * For the format to produce "2025-06-12T14:30:00+08:00", the
         * tal_time_get_local_time_custom must apply the timezone offset.
         * Since our stub uses gmtime_r (no TZ offset), the timestamp that
         * produces "14:30:00" via gmtime_r is 14:30:00 UTC on 2025-06-12.
         *
         * So we use the timestamp for 2025-06-12T14:30:00 UTC and expect
         * the format output to be "2025-06-12T14:30:00+08:00".
         */
        TIME_T known_ts = 1749738600; /* 2025-06-12T14:30:00 UTC */

        stub_set_tz_offset(28800);

        EXPECT_OK(wukong_iso8601_format(known_ts, buf, sizeof(buf)),
                  "format known timestamp should succeed");

        EXPECT_STR_EQ(buf, "2025-06-12T14:30:00+08:00",
                      "formatted output should match expected string");
    }

    /* --- 14. test_format_buffer_too_small --- */
    {
        CHAR_T small_buf[28];

        EXPECT_ERR(wukong_iso8601_format(1749738600, small_buf,
                                          sizeof(small_buf)),
                   OPRT_INVALID_PARM,
                   "format with buf_len < 29 should return OPRT_INVALID_PARM");
    }

    /* ==================================================================
     * Test wukong_iso8601_parse_to_timestamp
     * ================================================================== */

    /* --- 15. test_parse_to_timestamp_full --- */
    {
        TIME_T parsed_ts = 0;
        CHAR_T roundtrip[64] = {0};

        stub_set_tz_offset(28800);

        EXPECT_OK(wukong_iso8601_parse_to_timestamp(
                      "2025-06-12T14:30:00+08:00", FALSE, &parsed_ts),
                  "parse_to_timestamp full should succeed");

        /*
         * Verify the UTC timestamp directly:
         *   2025-06-12T14:30:00+08:00 means the local time 14:30:00 in +08:00,
         *   which is 2025-06-12T06:30:00 UTC.
         *   Expected epoch: 2025-06-12T06:30:00 UTC.
         *
         * Compute: days from epoch to 2025-06-12 = 20250 (verified separately).
         *   1749686400 (2025-06-12 00:00:00 UTC) + 6*3600 + 30*60 = 1749709800
         */
        EXPECT_EQ(parsed_ts, 1749709800,
                  "parsed UTC timestamp should be 2025-06-12T06:30:00 UTC");

        /* Round-trip format: with gmtime_r stubs the output reflects UTC time
         * with device TZ suffix, so "2025-06-12T06:30:00+08:00". */
        EXPECT_OK(wukong_iso8601_format(parsed_ts, roundtrip, sizeof(roundtrip)),
                  "format round-trip should succeed");

        EXPECT_STR_EQ(roundtrip, "2025-06-12T06:30:00+08:00",
                      "round-trip format should reflect stub gmtime_r behavior");
    }

    /* --- 16. test_parse_to_timestamp_date_only_start --- */
    {
        TIME_T start_ts = 0;
        CHAR_T formatted[64] = {0};

        stub_set_tz_offset(28800);

        EXPECT_OK(wukong_iso8601_parse_to_timestamp(
                      "2025-06-12", FALSE, &start_ts),
                  "parse_to_timestamp date-only start should succeed");

        /* Format back and verify it corresponds to 00:00:00 local time */
        EXPECT_OK(wukong_iso8601_format(start_ts, formatted, sizeof(formatted)),
                  "format date-only start timestamp should succeed");

        /* With stubs using gmtime_r and device TZ +08:00:
         *   The parse interprets 2025-06-12 00:00:00 in +08:00,
         *   giving UTC 2025-06-11T16:00:00.
         *   Format via gmtime_r returns UTC time, then appends +08:00:
         *   "2025-06-11T16:00:00+08:00".
         *   This is correct: 2025-06-12 00:00:00 +08:00 = 2025-06-11 16:00:00 UTC.
         *
         * However, the stub tal_time_mktime uses timegm (no TZ offset),
         * and tal_time_get_local_time_custom uses gmtime_r (no TZ offset).
         * The has_offset=FALSE path goes through mktime+localtime round-trip.
         * With timegm: 2025-06-12 00:00:00 -> epoch for UTC midnight.
         * gmtime_r of that -> 2025-06-12 00:00:00. tm equals, no DST adjust.
         * So candidate_ts = timegm of 2025-06-12 00:00:00 = 2025-06-12T00:00:00 UTC.
         * Formatting: gmtime_r(2025-06-12T00:00:00 UTC) -> "2025-06-12T00:00:00"
         * plus +08:00 suffix -> "2025-06-12T00:00:00+08:00"
         */
        EXPECT_STR_EQ(formatted, "2025-06-12T00:00:00+08:00",
                      "date-only start should format as midnight local time");
    }

    /* --- 17. test_parse_to_timestamp_date_only_end --- */
    {
        TIME_T end_ts = 0;
        CHAR_T formatted[64] = {0};

        stub_set_tz_offset(28800);

        EXPECT_OK(wukong_iso8601_parse_to_timestamp(
                      "2025-06-12", TRUE, &end_ts),
                  "parse_to_timestamp date-only end should succeed");

        /* Format back and verify it corresponds to 23:59:59 local time */
        EXPECT_OK(wukong_iso8601_format(end_ts, formatted, sizeof(formatted)),
                  "format date-only end timestamp should succeed");

        EXPECT_STR_EQ(formatted, "2025-06-12T23:59:59+08:00",
                      "date-only end should format as end-of-day local time");
    }

    /* --- 18. test_parse_to_timestamp_invalid --- */
    {
        EXPECT_ERR(wukong_iso8601_parse_to_timestamp("bad-input",
                                                       FALSE, &ts),
                   OPRT_INVALID_PARM,
                   "parse_to_timestamp with bad string should return OPRT_INVALID_PARM");

        EXPECT_ERR(wukong_iso8601_parse_to_timestamp("2025-13-01T00:00:00+08:00",
                                                       FALSE, &ts),
                   OPRT_INVALID_PARM,
                   "parse_to_timestamp with invalid month should return OPRT_INVALID_PARM");
    }

    TEST_END();
}
