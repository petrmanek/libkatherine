/**
 * @file
 * @brief The library's own error domain.
 * @author Petr Mánek
 * @date 25.8.26
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <katherine/global.h>

/**
 * @addtogroup c_api
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Error domain of the library. Every public function that used to return
 * 0 on success and a positive `<errno.h>` value on failure now returns 0
 * on success and the negative of one of these enumerators on failure;
 * katherine_strerror() renders either form (and, for convenience, the bare
 * positive enumerator too) into a human-readable description.
 */
typedef enum katherine_error {
    KATHERINE_E_OK = 0, ///< Success.

    /* transport */
    KATHERINE_E_TIMEOUT, ///< The operation did not complete within its deadline.
    KATHERINE_E_IO,      ///< A send, receive or socket setup call failed at the OS level; see katherine_udp_last_os_error().
    KATHERINE_E_CLOSED,  ///< The session is no longer usable.
    KATHERINE_E_ADDR,    ///< An address could not be resolved or bound.

    /* protocol */
    KATHERINE_E_BAD_CRD, ///< A command response datagram had an unexpected shape.
    KATHERINE_E_STRAY,   ///< Datagrams belonging to no request in flight were rejected until the budget for them ran out; see katherine_udp_set_strict_ack().
    KATHERINE_E_PROTO,   ///< The readout violated the wire protocol.

    /* device */
    KATHERINE_E_UNSUPPORTED, ///< The operation is not supported by this device.
    KATHERINE_E_BAD_CHIP,    ///< The sensor chip identifier is invalid or unexpected.
    KATHERINE_E_STATE,       ///< The call is not valid in the object's current state.
    KATHERINE_E_HW_UNKNOWN,  ///< The hardware reported a condition this library does not recognize.

    /* generic */
    KATHERINE_E_INVAL,  ///< An argument was invalid.
    KATHERINE_E_NOMEM,  ///< Memory allocation failed.
    KATHERINE_E_SYSTEM, ///< Some other OS-level failure; see katherine_udp_last_os_error() where applicable.
} katherine_error_t;

/**
 * Describe an error code.
 * @param error 0, the negative of a katherine_error_t enumerator (the form
 *   every public function of this library now returns), or the bare
 *   positive enumerator.
 * @return A statically allocated, NUL-terminated, lowercase description.
 *   Never NULL: a value outside the domain renders as "unknown error".
 */
KATHERINE_EXPORTED const char *
katherine_strerror(int error);

#ifdef __cplusplus
}
#endif

/** @} */
