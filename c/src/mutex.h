/**
 * \file
 * \brief Internal portable mutex.
 * \author Petr Mánek
 * \date 18.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <errno.h>

#include <katherine/error.h>
#include <katherine/global.h>

//
// IMPORTANT NOTICE:
//
// The following interface is internal.
// It is not intended for user application access.

#ifndef DOXYGEN_SHOULD_SKIP_THIS

#ifdef KATHERINE_WIN
// Lean, for the reason monoclock.h gives: <windows.h> otherwise drags in
// Winsock 1, which redefines what winsock2.h declares.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <pthread.h>
#endif

// Mutual exclusion, in whichever primitive the platform supplies. Every call
// reports in the library's own error codes: the platform's number is mapped
// here rather than handed out, so no caller has to know which platform it is
// on or what an errno means.
//
// Not recursive on either platform: a thread that takes one twice deadlocks
// on Windows and is undefined on POSIX. Nothing here wants recursion, every
// lock being taken and released within one function or one read loop.
//
// The Windows side cannot fail. SRWLOCK is initialized by assignment and its
// operations return void, so the error paths below exist for POSIX and are
// dead weight there -- which is the honest shape, rather than inventing a
// failure Windows does not have.

// A mutex call's own number, in the library's terms. Windows never reaches
// it, every SRWLOCK operation returning void.
static inline katherine_error_t
katherine_mutex_map_error(int err)
{
    if (err == 0) return KATHERINE_E_OK;

    return err == EINVAL ? KATHERINE_E_INVAL : KATHERINE_E_SYSTEM;
}

#ifdef KATHERINE_WIN

typedef SRWLOCK katherine_mutex_t;

static inline katherine_error_t
katherine_mutex_init(katherine_mutex_t *mutex)
{
    InitializeSRWLock(mutex);

    return KATHERINE_E_OK;
}

static inline katherine_error_t
katherine_mutex_fini(katherine_mutex_t *mutex)
{
    // An SRWLOCK holds nothing that needs giving back.
    (void) mutex;

    return KATHERINE_E_OK;
}

static inline katherine_error_t
katherine_mutex_lock(katherine_mutex_t *mutex)
{
    AcquireSRWLockExclusive(mutex);

    return KATHERINE_E_OK;
}

static inline katherine_error_t
katherine_mutex_unlock(katherine_mutex_t *mutex)
{
    ReleaseSRWLockExclusive(mutex);

    return KATHERINE_E_OK;
}

#else /* KATHERINE_NIX */

typedef pthread_mutex_t katherine_mutex_t;

static inline katherine_error_t
katherine_mutex_init(katherine_mutex_t *mutex)
{
    return katherine_mutex_map_error(pthread_mutex_init(mutex, NULL));
}

static inline katherine_error_t
katherine_mutex_fini(katherine_mutex_t *mutex)
{
    return katherine_mutex_map_error(pthread_mutex_destroy(mutex));
}

static inline katherine_error_t
katherine_mutex_lock(katherine_mutex_t *mutex)
{
    return katherine_mutex_map_error(pthread_mutex_lock(mutex));
}

static inline katherine_error_t
katherine_mutex_unlock(katherine_mutex_t *mutex)
{
    return katherine_mutex_map_error(pthread_mutex_unlock(mutex));
}

#endif /* KATHERINE_WIN */

#endif /* DOXYGEN_SHOULD_SKIP_THIS */
