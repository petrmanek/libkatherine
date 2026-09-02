/**
 * @file
 * @brief Internal portable monotonic clock.
 * @author Petr Mánek
 * @date 24.8.26
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

// Must be defined before this header's own first libc include (and ideally
// before any other libc header in the translation unit), or it has no
// effect: glibc resolves POSIX visibility once, the first time <features.h>
// is pulled in, and does not revisit that decision. Left undefined,
// clock_gettime(), CLOCK_MONOTONIC and struct timespec are invisible under
// a strict standard (e.g. -std=c11), even though they are unconditionally
// available at the platform level. Harmless on Windows, whose headers do
// not gate on it, and deferential to a caller that already set a stricter
// or looser value of its own.
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdint.h>
#include <katherine/global.h>

//
// IMPORTANT NOTICE:
//
// The following interface is internal.
// It is not intended for user application access.

#ifndef DOXYGEN_SHOULD_SKIP_THIS

#ifdef KATHERINE_WIN
#include <windows.h>
#else
#include <time.h>
#endif

#ifdef KATHERINE_WIN

// Monotonic timestamp in nanoseconds since an arbitrary epoch.
// POSIX: clock_gettime(CLOCK_MONOTONIC). Windows: QueryPerformanceCounter,
// frequency cached on first use; ticks are split into whole seconds and
// remainder before scaling so the conversion cannot overflow uint64_t for
// decades of uptime.
static inline uint64_t
katherine_clock_monotonic_ns(void)
{
    // QueryPerformanceFrequency() is guaranteed constant for the life of
    // the process (and has been since Windows XP, so it cannot fail on any
    // platform this header supports), so it is fetched once and cached
    // here. Nothing guards the cache against a data race between threads
    // racing to fill it for the first time: every writer computes the same
    // value from the same system call, so the worst case is a few redundant
    // QueryPerformanceFrequency() calls, never a torn or wrong result.
    static LONGLONG cached_frequency = 0;

    if (cached_frequency == 0) {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        cached_frequency = freq.QuadPart;
    }

    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);

    // Splitting into whole seconds and a sub-second tick remainder before
    // scaling to nanoseconds keeps every intermediate product well under
    // UINT64_MAX: the whole-seconds term would only threaten overflow after
    // roughly 584 years of uptime, and the remainder is by construction
    // smaller than the frequency (typically <= a few tens of MHz), so
    // remainder * 1000000000 fits easily as well.
    LONGLONG whole_seconds  = counter.QuadPart / cached_frequency;
    LONGLONG tick_remainder = counter.QuadPart % cached_frequency;

    return (uint64_t) whole_seconds * 1000000000ULL
        + (uint64_t) tick_remainder * 1000000000ULL / (uint64_t) cached_frequency;
}

#else /* KATHERINE_NIX */

// Monotonic timestamp in nanoseconds since an arbitrary epoch.
// POSIX: clock_gettime(CLOCK_MONOTONIC). Windows: QueryPerformanceCounter,
// frequency cached on first use; ticks are split into whole seconds and
// remainder before scaling so the conversion cannot overflow uint64_t for
// decades of uptime.
static inline uint64_t
katherine_clock_monotonic_ns(void)
{
    struct timespec ts;
    (void) clock_gettime(CLOCK_MONOTONIC, &ts);

    return (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;
}

#endif /* KATHERINE_WIN */

#endif /* DOXYGEN_SHOULD_SKIP_THIS */
