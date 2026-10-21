/**
 * \file
 * \brief Composing a string from parts, shared by every katherine_*_snprint().
 *
 * In a header rather than repr.c because a module that formats its own
 * private type -- the link backends and their sessions -- would otherwise
 * have to put that function in repr.c to reach these.
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>
#include <stdio.h>

//
// IMPORTANT NOTICE:
//
// The following interface is internal.
// It is not intended for user application access.

#ifndef DOXYGEN_SHOULD_SKIP_THIS

#ifdef __cplusplus
extern "C" {
#endif

// Internal: given a growing buffer buf of capacity cap and a byte offset
// off already accounted for by earlier appends (some possibly already
// truncated, i.e. off may exceed cap), computes the destination pointer and
// remaining room for the next append: NULL/0 once off has reached cap. This
// is the one piece of overflow-clamp arithmetic every composing snprint
// function below shares, through the two macros built on it.
static inline void
repr_cursor(char *buf, size_t cap, size_t off, char **dst, size_t *rem)
{
    *rem = cap > off ? cap - off : 0;
    *dst = *rem > 0 ? buf + off : NULL;
}

// Appends formatted text at the current offset, exactly like one more
// snprintf() call continuing the same string: off is advanced by the number
// of bytes the append would have produced given unlimited room (snprintf()'s
// own return value), so that after the last of a sequence of REPR_APPENDF /
// REPR_NEST calls, off holds the true (possibly truncated) total length --
// the same contract a single snprintf() call has, extended over the whole
// composed string.
#define REPR_APPENDF(buf, cap, off, ...) \
    do { \
        char *dst_; \
        size_t rem_; \
        repr_cursor((buf), (cap), (off), &dst_, &rem_); \
        (off) += (size_t) snprintf(dst_, rem_, __VA_ARGS__); \
    } while (0)

// Appends the rendering of a nested katherine_*_t value, produced by
// calling its own katherine_*_snprint() (fn) at the current offset -- the
// same idiom as REPR_APPENDF above, except the appended text comes from
// another snprint call instead of a format string. This is how every nested
// struct (config's triggers/dacs/test_pulse_config, device's two udp
// sessions, a pixel type's coord, ...) composes into its owner's string.
#define REPR_NEST(buf, cap, off, fn, ...) \
    do { \
        char *dst_; \
        size_t rem_; \
        repr_cursor((buf), (cap), (off), &dst_, &rem_); \
        (off) += (size_t) fn(dst_, rem_, __VA_ARGS__); \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* DOXYGEN_SHOULD_SKIP_THIS */
