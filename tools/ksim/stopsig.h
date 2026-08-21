/**
 * @file
 * @brief Internal portable stop-signal handler for ksim.
 * @author Petr Mánek
 * @date 21.8.26
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
// is pulled in, and does not revisit that decision. Left undefined, struct
// sigaction is invisible under a strict standard (e.g. -std=c11), even
// though it is unconditionally available at the platform level. Harmless on
// Windows, whose headers do not gate on it, and deferential to a caller
// that already set a stricter or looser value of its own.
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <signal.h>
#include <katherine/global.h>

/*
 * IMPORTANT NOTICE:
 *
 * The following interface is internal.
 * It is not intended for user application access.
 */

#ifndef DOXYGEN_SHOULD_SKIP_THIS

#ifdef KATHERINE_WIN
#include <windows.h>
#else
#include <string.h>
#endif

// Retains the flag pointer between installation and delivery. Written once
// by ksim_install_stop_handler() before the handler is armed; read (and
// written) only by the handler itself afterwards. On Windows, the console
// handler runs on its own thread spawned by the system, so this pointer --
// or rather the sig_atomic_t it points to -- is the only state the two
// threads share; sig_atomic_t is exactly the type the C standard guarantees
// can be written on one thread/signal context and observed on another
// without tearing.
static volatile sig_atomic_t *ksim_stopsig_flag = NULL;

#ifdef KATHERINE_WIN

// Runs on a system-created thread, separate from the one that called
// ksim_install_stop_handler() and from the main program thread; it must not
// touch anything but ksim_stopsig_flag.
//
// CTRL_CLOSE_EVENT (the console window being closed) gives the process
// roughly 5 seconds to return from this handler before Windows force-
// terminates it; whatever the main thread does with the flag (e.g. flushing
// a log, closing sockets) races that deadline, not just the flag write.
//
// Returning TRUE tells the system this handler dealt with the event, so the
// default handler -- which would otherwise terminate the process outright
// on CTRL_C_EVENT -- does not also run. CTRL_BREAK_EVENT, CTRL_LOGOFF_EVENT
// and CTRL_SHUTDOWN_EVENT are left to the next handler / default action:
// ksim only promises Ctrl-C and window-close semantics.
static BOOL WINAPI
ksim_stopsig_console_handler(DWORD ctrl_type)
{
    switch (ctrl_type) {
    case CTRL_C_EVENT:
    case CTRL_CLOSE_EVENT:
        if (ksim_stopsig_flag != NULL) *ksim_stopsig_flag = 1;
        return TRUE;
    default:
        return FALSE;
    }
}

/* Installs a handler setting *flag to 1 on SIGINT/SIGTERM (POSIX: sigaction)
 * or on console Ctrl-C / window close (Windows: SetConsoleCtrlHandler).
 * Returns 0 on success, an errno value otherwise. The flag pointer is
 * retained in a file-scope static (single-consumer header). */
static inline int
ksim_install_stop_handler(volatile sig_atomic_t *flag)
{
    ksim_stopsig_flag = flag;

    // SetConsoleCtrlHandler() fails with a GetLastError() code, not an
    // errno value, and there is no meaningful platform-independent mapping
    // between the two domains; EINVAL simply flags "installation failed" to
    // callers that only branch on zero/non-zero.
    if (!SetConsoleCtrlHandler(ksim_stopsig_console_handler, TRUE)) return EINVAL;

    return 0;
}

#else /* KATHERINE_NIX */

static void
ksim_stopsig_handler(int signum)
{
    (void) signum;
    if (ksim_stopsig_flag != NULL) *ksim_stopsig_flag = 1;
}

/* Installs a handler setting *flag to 1 on SIGINT/SIGTERM (POSIX: sigaction)
 * or on console Ctrl-C / window close (Windows: SetConsoleCtrlHandler).
 * Returns 0 on success, an errno value otherwise. The flag pointer is
 * retained in a file-scope static (single-consumer header). */
static inline int
ksim_install_stop_handler(volatile sig_atomic_t *flag)
{
    ksim_stopsig_flag = flag;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = ksim_stopsig_handler;

    // Deliberately no SA_RESTART: a caller blocked in a receive with a
    // timeout wants EINTR to unwind promptly on a stop signal rather than
    // have the interrupted call transparently resume.
    if (sigaction(SIGINT, &sa, NULL) != 0) return errno;
    if (sigaction(SIGTERM, &sa, NULL) != 0) return errno;

    return 0;
}

#endif /* KATHERINE_WIN */

#endif /* DOXYGEN_SHOULD_SKIP_THIS */
