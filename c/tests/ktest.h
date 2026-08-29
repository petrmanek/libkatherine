/**
 * @file
 * @brief Minimal, swappable, dependency-free TAP test harness.
 * @author Petr Mánek
 * @date 20.8.26
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

/*
 * Tests are plain functions:
 *
 *   static void test_foo(void) { KT_CHECK(1 + 1 == 2); }
 *
 * A test program looks like:
 *
 *   int main(void) {
 *       KT_RUN(test_foo);
 *       KT_RUN(test_bar);
 *       return kt_summary();
 *   }
 *
 * KT_CHECK / KT_CHECK_EQ / KT_CHECK_MEM_EQ record a failed assertion and let
 * the test keep running to completion. KT_REQUIRE is the same, except it
 * additionally returns from the current (void) test function on failure --
 * use it after a setup step whose failure would make later checks in the
 * same test memory-unsafe (e.g. a NULL allocation the test then derefs).
 * Nothing here ever exits the process: every KT_RUN'd test always runs, and
 * kt_summary() reports the aggregate outcome at the end.
 *
 * Everything a test needs is the handful of KT_* macros and kt_summary()
 * below; the counters and TAP output are an internal detail that can be
 * swapped out later without touching any test logic.
 */

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int kt_g_test_count       = 0;
static int kt_g_fail_count       = 0;
static int kt_g_cur_failed       = 0;
static const char *kt_g_cur_name = "";

static inline void
kt_report_fail(const char *file, int line, const char *expr)
{
    kt_g_cur_failed = 1;
    printf("# FAIL %s:%d: %s\n", file, line, expr);
}

#define KT_CHECK(cond) \
    do { \
        if (!(cond)) { \
            kt_report_fail(__FILE__, __LINE__, #cond); \
        } \
    } while (0)

#define KT_REQUIRE(cond) \
    do { \
        if (!(cond)) { \
            kt_report_fail(__FILE__, __LINE__, #cond); \
            return; \
        } \
    } while (0)

#define KT_CHECK_EQ(a, b) \
    do { \
        unsigned long long kt_a_ = (unsigned long long) (a); \
        unsigned long long kt_b_ = (unsigned long long) (b); \
        if (kt_a_ != kt_b_) { \
            kt_g_cur_failed = 1; \
            printf("# FAIL %s:%d: %s == %s (0x%llx/%llu != 0x%llx/%llu)\n", __FILE__, __LINE__, #a, #b, kt_a_, kt_a_, \
                kt_b_, kt_b_); \
        } \
    } while (0)

/* Floating-point comparison, in units of representable values rather than of
   an epsilon picked by hand. nextafter() walks to the neighbouring double, so
   the tolerance scales with magnitude automatically -- one ULP near 1e-300 and
   one ULP near 1e300 are both "the next value the type can hold", which is the
   question a test usually means to ask.

   Equality is checked first, so infinities compare equal to themselves; NaN
   compares equal to nothing, itself included.

   KT_CHECK_EQ is unsuitable here: it casts both sides to unsigned long long,
   which truncates a double rather than comparing it. */
static inline int
kt_double_within_ulps(double a, double b, unsigned ulps)
{
    if (isnan(a) || isnan(b)) return 0;
    if (a == b) return 1;
    if (isinf(a) || isinf(b)) return 0;

    double lo = a, hi = a;
    for (unsigned i = 0; i < ulps; ++i) {
        lo = nextafter(lo, -INFINITY);
        hi = nextafter(hi, INFINITY);
    }
    return b >= lo && b <= hi;
}

/* Within a few representable values of each other: the check for a quantity
   that should be the same number, arrived at by a different route. */
#define KT_CHECK_CLOSE(a, b) \
    do { \
        double kt_a_ = (double) (a); \
        double kt_b_ = (double) (b); \
        if (!kt_double_within_ulps(kt_a_, kt_b_, 4)) { \
            kt_g_cur_failed = 1; \
            printf("# FAIL %s:%d: %s ~= %s (%.17g vs %.17g)\n", __FILE__, __LINE__, #a, #b, kt_a_, kt_b_); \
        } \
    } while (0)

/* Within an absolute tolerance, for quantities compared against zero or across
   a scale change, where ULPs are the wrong unit. */
#define KT_CHECK_NEAR(a, b, tol) \
    do { \
        double kt_a_ = (double) (a); \
        double kt_b_ = (double) (b); \
        double kt_t_ = (double) (tol); \
        if (!(fabs(kt_a_ - kt_b_) <= kt_t_)) { \
            kt_g_cur_failed = 1; \
            printf("# FAIL %s:%d: |%s - %s| <= %s (%.17g vs %.17g)\n", __FILE__, __LINE__, #a, #b, #tol, kt_a_, \
                kt_b_); \
        } \
    } while (0)

/* Bit-for-bit equality of two doubles. Reserved for the cases where exactness
   is the property under test rather than an accident of rounding -- naming it
   says the "==" was meant, which a bare comparison cannot. */
#define KT_CHECK_EXACT(a, b) \
    do { \
        double kt_a_ = (double) (a); \
        double kt_b_ = (double) (b); \
        if (!(kt_a_ == kt_b_)) { \
            kt_g_cur_failed = 1; \
            printf("# FAIL %s:%d: %s exactly == %s (%.17g vs %.17g)\n", __FILE__, __LINE__, #a, #b, kt_a_, kt_b_); \
        } \
    } while (0)

#define KT_CHECK_MEM_EQ(p1, p2, n) \
    do { \
        if (memcmp((p1), (p2), (n)) != 0) { \
            kt_report_fail(__FILE__, __LINE__, #p1 " == " #p2); \
        } \
    } while (0)

#define KT_RUN(fn) \
    do { \
        kt_g_cur_failed = 0; \
        kt_g_cur_name   = #fn; \
        fn(); \
        ++kt_g_test_count; \
        if (kt_g_cur_failed) { \
            ++kt_g_fail_count; \
            printf("not ok %d - %s\n", kt_g_test_count, kt_g_cur_name); \
        } else { \
            printf("ok %d - %s\n", kt_g_test_count, kt_g_cur_name); \
        } \
    } while (0)

static inline int
kt_summary(void)
{
    printf("1..%d\n", kt_g_test_count);
    return kt_g_fail_count != 0;
}
