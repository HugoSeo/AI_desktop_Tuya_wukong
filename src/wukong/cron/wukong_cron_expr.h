/**
 * @file wukong_cron_expr.h
 * @brief Wukong cron expression parser public API.
 *
 * This header defines the bitmap-based cron expression type and the minimal
 * APIs used to parse, match, and calculate the next fire timestamp.
 */

#ifndef __WUKONG_CRON_EXPR_H__
#define __WUKONG_CRON_EXPR_H__

#include "tuya_cloud_types.h"
#include "tal_time_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum accepted text length for a 6-field cron expression.
 */
#define WUKONG_CRON_EXPR_MAX_LEN 64

/**
 * @brief Bitmap representation for each cron field.
 *
 * Each bit corresponds to one legal value inside its field range.
 */
typedef struct {
    /** Bitmap for second values in range [0, 59]. */
    UINT64_T second_mask;
    /** Bitmap for minute values in range [0, 59]. */
    UINT64_T minute_mask;
    /** Bitmap for hour values in range [0, 23]. */
    UINT64_T hour_mask;
    /** Bitmap for day-of-month values in range [1, 31]. */
    UINT64_T day_mask;
    /** Bitmap for month values in range [1, 12]. */
    UINT64_T month_mask;
    /** Bitmap for weekday values in range [0, 6]. */
    UINT64_T weekday_mask;
} WUKONG_CRON_EXPR_T;

/**
 * @brief Parse a 6-field cron string into the internal bitmap form.
 *
 * @param[in]  expr_str Input expression string.
 * @param[out] expr     Parsed bitmap expression.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_cron_expr_parse(CONST CHAR_T *expr_str, WUKONG_CRON_EXPR_T *expr);
/**
 * @brief Check whether a broken-down time matches the parsed expression.
 *
 * @param[in] expr    Parsed bitmap expression.
 * @param[in] tm_info Local broken-down time to match.
 * @return TRUE when the time matches, otherwise FALSE.
 */
BOOL_T wukong_cron_expr_match(CONST WUKONG_CRON_EXPR_T *expr, CONST POSIX_TM_S *tm_info);
/**
 * @brief Calculate the next firing timestamp strictly after @p from_ts.
 *
 * @param[in]  expr          Parsed bitmap expression.
 * @param[in]  from_ts       Base timestamp.
 * @param[out] next_fire_ts  Next matching timestamp.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_cron_expr_next_fire(CONST WUKONG_CRON_EXPR_T *expr, TIME_T from_ts, TIME_T *next_fire_ts);

#ifdef __cplusplus
}
#endif

#endif /* __WUKONG_CRON_EXPR_H__ */
