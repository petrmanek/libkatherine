/**
 * \file
 * \brief Internal portable millisecond sleep.
 * \author Petr Mánek
 * \date 20.8.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>
#include <katherine/global.h>

//
// IMPORTANT NOTICE:
//
// The following interface is internal.
// It is not intended for user application access.

#ifndef DOXYGEN_SHOULD_SKIP_THIS

#ifdef KATHERINE_WIN
// Lean, so that <windows.h> does not drag in Winsock 1. This header is meant
// to be included before any libc header, which on Windows would otherwise put
// winsock.h ahead of the winsock2.h that the session header needs, and the
// two redefine each other.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <time.h>
#endif

// C11 <threads.h> also offers thrd_sleep, but it is unavailable on some
// supported platforms (Apple's toolchain ships no <threads.h> at all, and
// MSVC only gained it recently), so the sleep is done with the native
// primitives instead.
static inline void
katherine_msleep(uint32_t ms)
{
#ifdef KATHERINE_WIN
    Sleep(ms);
#else
    struct timespec ts = {
        .tv_sec  = ms / 1000,
        .tv_nsec = (long) (ms % 1000) * 1000000L,
    };
    (void) nanosleep(&ts, NULL);
#endif
}

#endif /* DOXYGEN_SHOULD_SKIP_THIS */
