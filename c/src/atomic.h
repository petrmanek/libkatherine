/**
 * \file
 * \brief Internal atomic access to an ordinary bool.
 * \author Petr Mánek
 * \date 19.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <katherine/global.h>

//
// IMPORTANT NOTICE:
//
// The following interface is internal.
// It is not intended for user application access.

#ifndef DOXYGEN_SHOULD_SKIP_THIS

// The flags these operate on stay plain bool in the installed headers, which
// the C++ wrapper and the Python binding both read directly and neither can
// spell _Atomic. So the atomicity lives at the access rather than in the
// type, which is also what keeps the structures' layout unchanged.

// The split is the compiler, not the platform: MinGW's gcc and clang both
// build for Windows and both have the GNU builtins.
#ifdef _MSC_VER

#include <intrin.h>

// _InterlockedOr8 with a zero operand reads without changing anything, and
// is a full barrier on every architecture MSVC targets -- unlike a volatile
// read, whose ordering depends on /volatile:ms.
static inline bool
katherine_atomic_load_bool(const bool *flag)
{
    return _InterlockedOr8((volatile char *) flag, 0) != 0;
}

static inline void
katherine_atomic_store_bool(bool *flag, bool value)
{
    (void) _InterlockedExchange8((volatile char *) flag, (char) value);
}

#else /* GCC, Clang, MinGW */

static inline bool
katherine_atomic_load_bool(const bool *flag)
{
    return __atomic_load_n(flag, __ATOMIC_ACQUIRE);
}

static inline void
katherine_atomic_store_bool(bool *flag, bool value)
{
    __atomic_store_n(flag, value, __ATOMIC_RELEASE);
}

#endif /* _MSC_VER */

#endif /* DOXYGEN_SHOULD_SKIP_THIS */
