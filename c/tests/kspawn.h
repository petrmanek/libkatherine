/**
 * \file
 * \brief Internal portable child process control for the test suite.
 * \author Petr Mánek
 * \date 21.8.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

// Must be defined before this header's own first libc include (and ideally
// before any other libc header in the translation unit), or it has no
// effect: glibc resolves POSIX visibility once, the first time <features.h>
// is pulled in, and does not revisit that decision. Left undefined, fork(),
// execv(), kill() and waitpid() are invisible under a strict standard (e.g.
// -std=c11), even though they are unconditionally available at the platform
// level. Harmless on Windows, whose headers do not gate on it, and
// deferential to a caller that already set a stricter or looser value of its
// own. Consumers that also include libc headers of their own follow
// tools/ksim/main.c and define it at the very top of the file.
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <katherine/global.h>

//
// IMPORTANT NOTICE:
//
// The following interface is internal.
// It is not intended for user application access.

#ifndef DOXYGEN_SHOULD_SKIP_THIS

#ifdef KATHERINE_WIN
// <windows.h> internally drags in the legacy Winsock 1.1 header unless
// Winsock 2 was already established first, so a consumer that also talks
// sockets must include <katherine/udp.h> (or <winsock2.h>) ahead of this
// header, exactly as tools/ksim/main.c does for stopsig.h.
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#else
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

// One spawned child process. Zero-initialize (`= {0}`) before use if any
// function other than kspawn_start() may run first: every function is a
// no-op on a structure that owns no child, but only if the flag says so.
typedef struct kspawn_proc {
#ifdef KATHERINE_WIN
    HANDLE handle; /* process handle, valid while owned */
#else
    pid_t pid; /* child process identifier, valid while owned */
#endif
    bool owned; /* a live, unreaped child belongs to this structure */
} kspawn_proc_t;

#ifdef KATHERINE_WIN

// Upper bound on the command line built from argv: every character of every
// argument could be a backslash that has to be doubled, and each argument
// adds a pair of quotes plus a separating space.
static inline size_t
kspawn_cmdline_size(char *const argv[])
{
    size_t total = 1; /* the terminator */

    for (size_t i = 0; argv[i] != NULL; ++i) {
        total += 2 * strlen(argv[i]) + 3;
    }

    return total;
}

// Appends one argument to a command line in the quoting dialect
// CommandLineToArgvW() -- and hence the CRT startup code of the child --
// parses back: the argument is wrapped in quotes, a run of backslashes is
// doubled only where it precedes a quote (embedded or closing), and an
// embedded quote is escaped with one further backslash. Writes at most the
// per-argument allowance of kspawn_cmdline_size().
static inline void
kspawn_append_quoted(char *out, size_t *len, const char *arg)
{
    size_t n = *len;

    out[n++] = '"';

    for (const char *p = arg; *p != '\0'; ++p) {
        size_t slashes = 0;
        while (*p == '\\') {
            ++slashes;
            ++p;
        }

        if (*p == '\0') {
            // Trailing backslashes end up in front of the closing quote,
            // where they would escape it, so they double as well.
            for (size_t i = 0; i < 2 * slashes; ++i) out[n++] = '\\';
            break;
        }

        for (size_t i = 0; i < (*p == '"' ? 2 * slashes + 1 : slashes); ++i) out[n++] = '\\';
        out[n++] = *p;
    }

    out[n++] = '"';
    *len     = n;
}

// Starts `path` with the NULL-terminated `argv` (argv[0] is the child's own
// idea of its name, as on POSIX). The child inherits this process's standard
// streams, so its diagnostics land wherever the parent's do. Returns 0 on
// success, an errno value otherwise.
static inline int
kspawn_start(kspawn_proc_t *proc, const char *path, char *const argv[])
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    size_t len = 0;
    char *cmdline;
    BOOL ok;

    proc->owned = false;

    cmdline = (char *) malloc(kspawn_cmdline_size(argv));
    if (cmdline == NULL) return ENOMEM;

    for (size_t i = 0; argv[i] != NULL; ++i) {
        if (i > 0) cmdline[len++] = ' ';
        kspawn_append_quoted(cmdline, &len, argv[i]);
    }
    cmdline[len] = '\0';

    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    // Handle inheritance has to be asked for twice: bInheritHandles below
    // permits it at all, and STARTF_USESTDHANDLES names the three handles
    // the child should start with. Without the latter, a child whose parent
    // had its output redirected to a file (as under ctest) would write to
    // the console instead of the log.
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

    ok = CreateProcessA(path, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    free(cmdline);

    // CreateProcessA() fails with a GetLastError() code, not an errno value,
    // and there is no meaningful platform-independent mapping between the
    // two domains; ENOENT simply flags "could not start" to callers that
    // only branch on zero/non-zero.
    if (!ok) return ENOENT;

    (void) CloseHandle(pi.hThread);
    proc->handle = pi.hProcess;
    proc->owned  = true;
    return 0;
}

// Reports whether the child is still running. A child observed to have
// exited is reaped here and released, so that a later kspawn_stop() cannot
// signal a process identifier the system has since reused.
static inline bool
kspawn_alive(kspawn_proc_t *proc)
{
    if (!proc->owned) return false;

    if (WaitForSingleObject(proc->handle, 0) == WAIT_TIMEOUT) return true;

    (void) CloseHandle(proc->handle);
    proc->owned = false;
    return false;
}

// Terminates the child and reaps it, blocking until it is gone. Idempotent:
// a no-op once the child has been reaped, here or by kspawn_alive().
static inline void
kspawn_stop(kspawn_proc_t *proc)
{
    if (!proc->owned) return;

    // No console-control equivalent of SIGTERM can be aimed at a single
    // process (GenerateConsoleCtrlEvent() addresses a whole process group),
    // so the child is terminated outright and does not get to run its own
    // shutdown path.
    (void) TerminateProcess(proc->handle, 1);
    (void) WaitForSingleObject(proc->handle, INFINITE);
    (void) CloseHandle(proc->handle);
    proc->owned = false;
}

#else /* KATHERINE_NIX */

// Starts `path` with the NULL-terminated `argv` (argv[0] is the child's own
// idea of its name, as on POSIX). The child inherits this process's standard
// streams, so its diagnostics land wherever the parent's do. Returns 0 on
// success, an errno value otherwise.
static inline int
kspawn_start(kspawn_proc_t *proc, const char *path, char *const argv[])
{
    pid_t pid;

    proc->owned = false;

    pid = fork();
    if (pid < 0) return errno;

    if (pid == 0) {
        // Either execv() replaces this image (discarding the inherited copy
        // of the parent's stdio buffers) or _exit() leaves them unflushed,
        // so no buffered parent output can be emitted twice. A failed exec
        // is reported as an exit status rather than through errno: the
        // parent learns about it from kspawn_alive() alone, which is all its
        // callers need in order to distinguish "the child never came up".
        (void) execv(path, argv);
        _exit(127);
    }

    proc->pid   = pid;
    proc->owned = true;
    return 0;
}

// Reports whether the child is still running. A child observed to have
// exited is reaped here and released, so that a later kspawn_stop() cannot
// signal a process identifier the system has since reused.
static inline bool
kspawn_alive(kspawn_proc_t *proc)
{
    pid_t res;

    if (!proc->owned) return false;

    do {
        res = waitpid(proc->pid, NULL, WNOHANG);
    } while (res < 0 && errno == EINTR);

    if (res == 0) return true;

    proc->owned = false;
    return false;
}

// Terminates the child and reaps it, blocking until it is gone. Idempotent:
// a no-op once the child has been reaped, here or by kspawn_alive().
static inline void
kspawn_stop(kspawn_proc_t *proc)
{
    if (!proc->owned) return;

    // A child that has already exited is a zombie until reaped, and a
    // zombie still accepts a signal, so the two steps below need no
    // liveness test between them.
    (void) kill(proc->pid, SIGTERM);
    while (waitpid(proc->pid, NULL, 0) < 0 && errno == EINTR) { }

    proc->owned = false;
}

#endif /* KATHERINE_WIN */

#endif /* DOXYGEN_SHOULD_SKIP_THIS */
