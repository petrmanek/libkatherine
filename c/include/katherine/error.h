/**
 * \file
 * \brief The library's own error domain.
 * \author Petr Mánek
 * \date 25.8.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <katherine/global.h>

/**
 * \defgroup katherine_error Errors
 * \ingroup katherine_c_api
 * \brief The library's error domain and its descriptions.
 */

/**
 * \addtogroup katherine_error
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Error domain of the library. Every function that can fail returns one of
 * these enumerators: KATHERINE_E_OK, which is zero, or the code describing
 * what went wrong. Codes are positive, following the userspace convention --
 * negative values are the kernel's idiom for the same purpose, and this is a
 * userspace library. So `if (res)` still reads as "it failed", while
 * `res == KATHERINE_E_TIMEOUT` reads as the specific case.
 *
 * katherine_strerror() renders a code into a human-readable description. It
 * also accepts the negated form libkatherine 1.x returned, so that code being
 * ported reports something sensible either way.
 */
typedef enum katherine_error {
    KATHERINE_E_OK = 0, ///< Success.

    // transport
    KATHERINE_E_TIMEOUT, ///< The operation did not complete within its deadline.
    KATHERINE_E_IO,      ///< A send, receive or socket setup call failed at the OS level; see katherine_udp_last_os_error().
    KATHERINE_E_CLOSED,  ///< The session is no longer usable.
    KATHERINE_E_ADDR,    ///< An address could not be resolved or bound.

    // protocol
    KATHERINE_E_BAD_CRD, ///< A command response datagram had an unexpected shape.
    KATHERINE_E_STRAY,   ///< Datagrams belonging to no request in flight were rejected until the budget for them ran out; see katherine_udp_set_strict_ack().
    KATHERINE_E_PROTO,   ///< The readout violated the wire protocol.

    // device
    KATHERINE_E_UNSUPPORTED, ///< The operation is not supported by this device.
    KATHERINE_E_BAD_CHIP,    ///< The chip identifier is invalid or unexpected.
    KATHERINE_E_STATE,       ///< The call is not valid in the object's current state.
    KATHERINE_E_HW_UNKNOWN,  ///< The hardware reported a condition this library does not recognize.

    // generic
    KATHERINE_E_INVAL,  ///< An argument was invalid.
    KATHERINE_E_NOMEM,  ///< Memory allocation failed.
    KATHERINE_E_SYSTEM, ///< Some other OS-level failure; see katherine_udp_last_os_error() where applicable.
} katherine_error_t;

KATHERINE_EXPORTED const char *
katherine_strerror(int error);

#ifdef __cplusplus
}
#endif

/** \} */
