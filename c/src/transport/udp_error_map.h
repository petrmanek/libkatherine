/**
 * \file
 * \brief Translation of platform socket errors into the library's error domain.
 * \author Petr Mánek
 * \date 15.9.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <katherine/error.h>
#include <katherine/global.h>

#ifdef KATHERINE_WIN
#include <winsock2.h>
#else
#include <errno.h>
#endif

//
// IMPORTANT NOTICE:
//
// The following interface is internal.
// It is not intended for user application access.

#ifndef DOXYGEN_SHOULD_SKIP_THIS

// Both platforms' mappings live here rather than one in each transport, so
// that a divergence between them is visible in one screen instead of inferred
// by reading two files. They are a pair by construction: the same wire failure
// must yield the same katherine_error_t whichever transport observed it, and
// that is the property c/tests/test_error_map.c asserts.
//
// The values are far apart enough that mixing the domains up cannot be caught
// by testing -- measured on MSVC 19.51: EAGAIN 11 against WSAEWOULDBLOCK
// 10035, EINVAL 22 against WSAEINVAL 10022, ENOMEM 12 against WSAENOBUFS
// 10055, ETIMEDOUT 138 against WSAETIMEDOUT 10060, EINTR 4 against WSAEINTR
// 10004. Nothing overlaps, so a Winsock code fed to the errno table matches
// none of its cases and reaches the fallback silently, which is the defect
// this header exists to make impossible to write again.

#ifdef KATHERINE_WIN

/**
 * Maps a Winsock error code to the library's error domain.
 *
 * Windows has two OS error domains, and only this one is mapped. Socket calls
 * report through WSAGetLastError() (and WSAStartup() through its return
 * value), whereas the mutex calls report through GetLastError() in the Win32
 * system-error space; those codes are not passed here, since the two spaces
 * carry unrelated meanings for the same number. That separation is safe to
 * rely on rather than merely conventional: the 10000-11999 band is reserved
 * for Winsock within the Win32 error space, so no GetLastError() code can
 * collide with a case below.
 *
 * \param err Raw Winsock code, as returned by WSAGetLastError()
 * \param fallback Group to report if err does not match a specific case
 * \return The mapped enumerator
 */
static inline katherine_error_t
katherine_udp_map_socket_error(int err, katherine_error_t fallback)
{
    switch (err) {
    // A receive that found nothing within SO_RCVTIMEO reports WSAETIMEDOUT
    // where POSIX reports EAGAIN; WSAEWOULDBLOCK is its non-blocking
    // counterpart. Neither is a failure of the socket, only of the wait.
    case WSAETIMEDOUT:
    case WSAEWOULDBLOCK:
        return KATHERINE_E_TIMEOUT;
    case WSAEINVAL:
        return KATHERINE_E_INVAL;
    // Winsock spends one code on what POSIX splits between ENOMEM and
    // ENOBUFS -- allocation failure and a full transmit queue -- so its single
    // case answers both of the POSIX cases below.
    case WSAENOBUFS:
        return KATHERINE_E_NOMEM;
    default:
        return fallback;
    }
}

#else

/**
 * Maps a POSIX `<errno.h>` value to the library's error domain.
 *
 * One domain covers everything this transport calls: the sockets report in it
 * and so do the pthreads, whose functions return an errno value directly
 * rather than setting the variable. Windows splits the two, which is why its
 * counterpart above documents a restriction this one does not need.
 *
 * The cases apply wherever they turn up, not only at the syscall each
 * was first observed at; anything else falls back to the group the caller
 * names, since the OS-level detail is preserved separately, in
 * katherine_udp_t::last_os_error.
 *
 * \param err Raw `<errno.h>` value
 * \param fallback Group to report if err does not match a specific case
 * \return The mapped enumerator
 */
static inline katherine_error_t
katherine_udp_map_socket_error(int err, katherine_error_t fallback)
{
    switch (err) {
    case EAGAIN:
#if EWOULDBLOCK != EAGAIN
    case EWOULDBLOCK:
#endif
    case ETIMEDOUT:
        return KATHERINE_E_TIMEOUT;
    case EINVAL:
        return KATHERINE_E_INVAL;
    // Two distinct errno values for one condition class, and POSIX really does
    // separate them: ENOMEM is "no memory available", ENOBUFS is "the output
    // queue for a network interface was full ... may be caused by transient
    // congestion" (sendto(2)). Winsock spends a single code, WSAENOBUFS, on
    // both, so mapping only ENOMEM here made a full transmit queue -- the
    // common case under load -- report KATHERINE_E_NOMEM on Windows and fall
    // through to the caller's fallback on POSIX. Same wire condition, two
    // enumerators, which is the divergence this header exists to prevent.
    case ENOMEM:
    case ENOBUFS:
        return KATHERINE_E_NOMEM;
    default:
        return fallback;
    }
}

#endif /* KATHERINE_WIN */

#endif /* DOXYGEN_SHOULD_SKIP_THIS */
