/**
 * @file
 * @brief Internal portable thread for the test suite's mock readouts.
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
// before any other libc include in the translation unit), or it has no
// effect: glibc resolves POSIX visibility once, the first time <features.h>
// is pulled in, and does not revisit that decision. Left undefined,
// pthread_create()/pthread_join() are invisible under a strict standard
// (e.g. -std=c11), even though they are unconditionally available at the
// platform level. Harmless on Windows, whose headers do not gate on it, and
// deferential to a caller that already set a stricter or looser value of its
// own.
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <katherine/global.h>

/*
 * IMPORTANT NOTICE:
 *
 * The following interface is internal.
 * It is not intended for user application access.
 */

#ifndef DOXYGEN_SHOULD_SKIP_THIS

#ifdef KATHERINE_WIN
// <windows.h> internally drags in the legacy Winsock 1.1 header unless
// Winsock 2 was already established first, so a consumer that also talks
// sockets must include <katherine/udp.h> (or <winsock2.h>) ahead of this
// header, exactly as tools/ksim/main.c does for stopsig.h.
#include <windows.h>
#else
#include <pthread.h>
#endif

/* One thread started with kthread_start(), and the only operation this
 * suite's mock readouts ever need afterwards: a single blocking join. No
 * detach, no cancellation -- kspawn_proc_t is the model to extend if a future
 * test needs more. */
typedef struct kthread {
#ifdef KATHERINE_WIN
    HANDLE handle;
    void *(*fn)(void *); /* stashed for the trampoline below */
    void *arg;
#else
    pthread_t thread;
#endif
} kthread_t;

#ifdef KATHERINE_WIN

/* Adapts a pthread-style start routine (returns void *, as every caller of
 * this header writes one) to the DWORD WINAPI signature CreateThread()
 * requires. The returned value is discarded, exactly as pthread_join(...,
 * NULL) below discards it. */
static DWORD WINAPI
kthread_trampoline(LPVOID param)
{
    kthread_t *t = (kthread_t *) param;
    (void) t->fn(t->arg);
    return 0;
}

/* Starts fn(arg) on a new thread. Returns 0 on success, an errno-shaped
 * value otherwise -- CreateThread() fails with a GetLastError() code, not an
 * errno value, and there is no meaningful platform-independent mapping
 * between the two domains, so EAGAIN simply flags "could not start" to
 * callers that only branch on zero/non-zero (same reasoning as
 * kspawn_start()'s use of ENOENT). */
static inline int
kthread_start(kthread_t *t, void *(*fn)(void *), void *arg)
{
    t->fn  = fn;
    t->arg = arg;

    t->handle = CreateThread(NULL, 0, kthread_trampoline, t, 0, NULL);
    if (t->handle == NULL) return EAGAIN;

    return 0;
}

/* Blocks until the thread started by kthread_start() has returned. Returns 0
 * on success, an errno-shaped value otherwise (see kthread_start() for why
 * EINVAL stands in for a GetLastError() code here). */
static inline int
kthread_join(kthread_t *t)
{
    if (WaitForSingleObject(t->handle, INFINITE) != WAIT_OBJECT_0) return EINVAL;

    (void) CloseHandle(t->handle);
    return 0;
}

#else /* KATHERINE_NIX */

/* Starts fn(arg) on a new thread. Returns 0 on success, an errno value
 * otherwise (pthread_create()'s own return convention). */
static inline int
kthread_start(kthread_t *t, void *(*fn)(void *), void *arg)
{
    return pthread_create(&t->thread, NULL, fn, arg);
}

/* Blocks until the thread started by kthread_start() has returned. Returns 0
 * on success, an errno value otherwise (pthread_join()'s own return
 * convention). */
static inline int
kthread_join(kthread_t *t)
{
    return pthread_join(t->thread, NULL);
}

#endif /* KATHERINE_WIN */

#endif /* DOXYGEN_SHOULD_SKIP_THIS */
