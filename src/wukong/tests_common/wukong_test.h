#ifndef WUKONG_TEST_H
#define WUKONG_TEST_H
/**
 * @file wukong_test.h
 * @brief Header-only C test framework with TAP output.
 *
 * - Continue-on-failure: all assertions execute regardless of prior failures
 * - TAP format on stdout: ok/not ok lines + plan (1..N)
 * - Diagnostics on stderr: file:line + got/expected on failure
 */
#include <stdio.h>
#include <string.h>

static int wt__count = 0;
static int wt__fails = 0;

static void wt__pass(int n, const char *desc)
{
    printf("ok %d - %s\n", n, desc);
}

static void wt__fail(int n, const char *desc, const char *file, int line)
{
    printf("not ok %d - %s\n", n, desc);
    fprintf(stderr, "  # FAIL at %s:%d: %s\n", file, line, desc);
    wt__fails++;
}

#define EXPECT(cond, desc) do { \
    wt__count++; \
    if (cond) { wt__pass(wt__count, desc); } \
    else { wt__fail(wt__count, desc, __FILE__, __LINE__); } \
} while (0)

#define EXPECT_OK(ret, desc) do { \
    OPERATE_RET _r = (ret); \
    wt__count++; \
    if (_r == OPRT_OK) { wt__pass(wt__count, desc); } \
    else { \
        wt__fail(wt__count, desc, __FILE__, __LINE__); \
        fprintf(stderr, "  # got %d, expected 0 (OPRT_OK)\n", (int)_r); \
    } \
} while (0)

#define EXPECT_ERR(ret, code, desc) do { \
    OPERATE_RET _r = (ret); \
    wt__count++; \
    if (_r == (code)) { wt__pass(wt__count, desc); } \
    else { \
        wt__fail(wt__count, desc, __FILE__, __LINE__); \
        fprintf(stderr, "  # got %d, expected %d\n", (int)_r, (int)(code)); \
    } \
} while (0)

#define EXPECT_EQ(a, b, desc) do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    wt__count++; \
    if (_a == _b) { wt__pass(wt__count, desc); } \
    else { \
        wt__fail(wt__count, desc, __FILE__, __LINE__); \
        fprintf(stderr, "  # got %lld, expected %lld\n", _a, _b); \
    } \
} while (0)

#define EXPECT_NE(a, b, desc) EXPECT((long long)(a) != (long long)(b), desc)
#define EXPECT_NULL(ptr, desc) EXPECT((ptr) == NULL, desc)
#define EXPECT_NOT_NULL(ptr, desc) EXPECT((ptr) != NULL, desc)

#define EXPECT_STR_CONTAINS(hay, needle, desc) do { \
    const char *_h = (hay), *_n = (needle); \
    wt__count++; \
    if (_h && _n && strstr(_h, _n)) { wt__pass(wt__count, desc); } \
    else { \
        wt__fail(wt__count, desc, __FILE__, __LINE__); \
        fprintf(stderr, "  # needle \"%s\" not found\n", _n ? _n : "(null)"); \
    } \
} while (0)

#define EXPECT_STR_EQ(a, b, desc) do { \
    const char *_a = (a), *_b = (b); \
    wt__count++; \
    if (_a && _b && strcmp(_a, _b) == 0) { wt__pass(wt__count, desc); } \
    else { \
        wt__fail(wt__count, desc, __FILE__, __LINE__); \
        fprintf(stderr, "  # got \"%s\", expected \"%s\"\n", \
                _a ? _a : "(null)", _b ? _b : "(null)"); \
    } \
} while (0)

#define TEST_END() do { \
    printf("1..%d\n", wt__count); \
    fprintf(stderr, "# %d tests, %d failures\n", wt__count, wt__fails); \
    return wt__fails > 0 ? 1 : 0; \
} while (0)

#endif /* WUKONG_TEST_H */
