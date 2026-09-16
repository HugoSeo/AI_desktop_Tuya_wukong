#pragma once
/* Host stub for the device uni_log.h (PR_* macros). Args are consumed via a
 * no-op variadic sink so -Wextra -Werror doesn't flag "unused variable" on
 * values that production code only uses in log statements. */
static inline void __pr_sink(const char *fmt, ...) { (void)fmt; }
#define PR_ERR(...)    __pr_sink(__VA_ARGS__)
#define PR_WARN(...)   __pr_sink(__VA_ARGS__)
#define PR_NOTICE(...) __pr_sink(__VA_ARGS__)
#define PR_DEBUG(...)  __pr_sink(__VA_ARGS__)
